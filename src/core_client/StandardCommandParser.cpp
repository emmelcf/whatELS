#include "StandardCommandParser.h"
#include "core/CoreCommandService.h"
#include "core/TelemetrySerializer.h"
#include "core/SettingsService.h"
#include "core/PresetService.h"
#include "modes/ModeRegistry.h"

#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <vector>
#include <string_view>
#include <algorithm>

namespace whatels::core_client {

// =============================================================================
// INTERNAL STRICT GRAMMAR & VALIDATION UTILITIES
// =============================================================================

namespace {

/**
 * @brief Strips trailing CR/LF line terminators without touching internal bytes.
 */
std::string_view stripLineTerminators(std::string_view s) {
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n')) {
    s.remove_suffix(1);
  }
  return s;
}

/**
 * @brief Validates universal message syntax rules:
 *        1. Zero unquoted spaces or tabs anywhere in the message.
 *        2. Double quotes must be strictly balanced.
 *        3. At most one '=' in the entire message (separating header from arguments).
 */
bool validateGrammar(std::string_view line, size_t* outEqPos) {
  bool inQuotes = false;
  size_t eqCount = 0;
  size_t firstEq = std::string_view::npos;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    if (!inQuotes) {
      // Invariant: Unquoted whitespace is strictly forbidden anywhere in commands
      if (c == ' ' || c == '\t') {
        return false;
      }
      if (c == '=') {
        eqCount++;
        if (firstEq == std::string_view::npos) {
          firstEq = i;
        }
      }
    }
  }

  // Unbalanced quotes
  if (inQuotes) return false;

  // Invariant: At most one '=' per command (separating header from arguments)
  if (eqCount > 1) return false;

  if (outEqPos) *outEqPos = firstEq;
  return true;
}

bool isValidIdentifier(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
      return false;
    }
  }
  return true;
}

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

struct KeyValuePair {
  std::string key;
  std::string val;
};

/**
 * @brief Parses strictly formatted key-value arguments: name1:val1,name2:val2,...
 * @details Rejects any token lacking a colon, containing an '=', or having trailing commas.
 */
bool parseKeyValuesStrict(const char* args, std::vector<KeyValuePair>& result) {
  if (!args || *args == '\0') return true;

  const char* p = args;
  while (*p) {
    if (*p == ',') return false; // Leading or double comma

    const char* start = p;
    bool inQuote = false;
    while (*p && (inQuote || *p != ',')) {
      if (*p == '"') inQuote = !inQuote;
      p++;
    }
    if (inQuote) return false; // Unbalanced quote

    std::string_view token(start, p - start);
    if (token.empty()) return false;

    size_t colonPos = std::string_view::npos;
    bool tokenInQuote = false;
    for (size_t i = 0; i < token.size(); ++i) {
      if (token[i] == '"') tokenInQuote = !tokenInQuote;
      else if (!tokenInQuote && token[i] == ':') {
        colonPos = i;
        break;
      }
    }

    if (colonPos == std::string_view::npos || colonPos == 0) {
      return false; // Key-value pair must have name:val format
    }

    std::string_view k = token.substr(0, colonPos);
    std::string_view v = token.substr(colonPos + 1);

    if (!isValidIdentifier(k)) return false;

    // Strip outer quotes from value if present
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"') {
      v.remove_prefix(1);
      v.remove_suffix(1);
    }

    result.push_back({ std::string(k), std::string(v) });
    if (*p == ',') {
      p++;
      if (!*p) return false; // Trailing comma forbidden
    }
  }
  return true;
}

const KeyValuePair* findKey(const std::vector<KeyValuePair>& pairs, std::string_view key) {
  for (const auto& kv : pairs) {
    if (equalsIgnoreCase(kv.key, key)) {
      return &kv;
    }
  }
  return nullptr;
}

bool parseDeciMicrons(const std::string& str, int32_t* outVal) {
  if (str.empty()) return false;
  char* end = nullptr;
  if (str.find('.') != std::string::npos) {
    double mm = std::strtod(str.c_str(), &end);
    if (end == str.c_str() || *end != '\0') return false;
    *outVal = static_cast<int32_t>(std::lround(mm * 10000.0));
    return true;
  } else {
    long du = std::strtol(str.c_str(), &end, 10);
    if (end == str.c_str() || *end != '\0') return false;
    *outVal = static_cast<int32_t>(du);
    return true;
  }
}

bool parseBoolVal(const std::string& str, bool defaultVal = false) {
  if (str.empty()) return defaultVal;
  if (str == "1" || equalsIgnoreCase(str, "true") || equalsIgnoreCase(str, "yes") || equalsIgnoreCase(str, "sync")) {
    return true;
  }
  if (str == "0" || equalsIgnoreCase(str, "false") || equalsIgnoreCase(str, "no") || equalsIgnoreCase(str, "async")) {
    return false;
  }
  return defaultVal;
}

AxisId parseAxis(const std::string& str) {
  if (equalsIgnoreCase(str, "X")) return AXIS_X;
  return AXIS_Z;
}

int8_t parseDirection(const std::string& str) {
  if (str == "-1" || str == "-" || equalsIgnoreCase(str, "LEFT") || equalsIgnoreCase(str, "CHUCK") ||
      equalsIgnoreCase(str, "CENTER") || equalsIgnoreCase(str, "IN") || equalsIgnoreCase(str, "INFEED") ||
      equalsIgnoreCase(str, "REV")) {
    return -1;
  }
  if (str == "+1" || str == "1" || str == "+" || equalsIgnoreCase(str, "RIGHT") || equalsIgnoreCase(str, "TAIL") ||
      equalsIgnoreCase(str, "RETRACT") || equalsIgnoreCase(str, "OUT") || equalsIgnoreCase(str, "FWD")) {
    return 1;
  }
  return -1;
}

} // namespace

// =============================================================================
// STANDARD COMMAND PARSER IMPLEMENTATION
// =============================================================================

StandardCommandParser& StandardCommandParser::instance() {
  static StandardCommandParser parser;
  return parser;
}

String StandardCommandParser::formatWireResponse(const core::CommandResult& res) {
  if (res.status == core::CommandStatus::UNRECOGNIZED) {
    return "ERR:UNKNOWN_COMMAND\n";
  }

  if (res.status == core::CommandStatus::FAILED) {
    String out = "ERR:";
    out += res.command;
    if (!res.payload.empty()) {
      out += ":";
      out += res.payload;
    }
    out += "\n";
    return out;
  }

  // OK Success
  if (res.payload.empty()) {
    String out = "OK:";
    out += res.command;
    out += "\n";
    return out;
  }

  // Multi-line snapshot frames (e.g. STATE.get or SYS.get)
  if (res.payload.find('\n') != std::string::npos || res.payload.rfind("K.", 0) == 0) {
    if (res.payload.back() != '\n') {
      return res.payload + "\n";
    }
    return res.payload;
  }

  String out = "OK:";
  out += res.command;
  out += ":";
  out += res.payload;
  out += "\n";
  return out;
}

core::CommandResult StandardCommandParser::parseAndDispatch(const char* line, size_t length, const ClientContext& ctx) {
  if (!line || length == 0) {
    return core::CommandResult::unrecognized();
  }

  std::string_view raw(line, length);
  std::string_view stripped = stripLineTerminators(raw);
  if (stripped.empty()) {
    return core::CommandResult::unrecognized();
  }

  // Pillar 1 Clean Break Rule: All legacy '?' queries are rejected immediately
  if (stripped.front() == '?') {
    return core::CommandResult::unrecognized();
  }

  // Strict Syntax Invariants: Zero unquoted spaces, balanced quotes, at most one '='
  size_t eqPos = std::string_view::npos;
  if (!validateGrammar(stripped, &eqPos)) {
    return core::CommandResult::unrecognized();
  }

  // Extract Header: strictly <NAMESPACE>.<action>
  bool hasEquals = (eqPos != std::string_view::npos);
  std::string_view headerView = hasEquals ? stripped.substr(0, eqPos) : stripped;
  std::string_view argsView = hasEquals ? stripped.substr(eqPos + 1) : "";

  // A trailing naked '=' without arguments (e.g. "CMD.stop=" or "CMD.startFeed=") is invalid syntax
  if (hasEquals && argsView.empty()) {
    return core::CommandResult::unrecognized();
  }

  // Header must contain strictly one dot '.' separating namespace and action
  size_t dotPos = headerView.find('.');
  if (dotPos == std::string_view::npos || dotPos == 0 || dotPos == headerView.size() - 1) {
    return core::CommandResult::unrecognized();
  }

  // Ensure no multiple dots in header
  if (headerView.find('.', dotPos + 1) != std::string_view::npos) {
    return core::CommandResult::unrecognized();
  }

  std::string_view nsView = headerView.substr(0, dotPos);
  std::string_view actView = headerView.substr(dotPos + 1);

  if (!isValidIdentifier(nsView) || !isValidIdentifier(actView)) {
    return core::CommandResult::unrecognized();
  }

  std::string action(actView);
  const char* args = hasEquals ? argsView.data() : nullptr;
  std::string argsStr(argsView); // Null-terminated copy

  if (equalsIgnoreCase(nsView, "CMD")) {
    return parseCmd(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  } else if (equalsIgnoreCase(nsView, "STATE")) {
    return parseState(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  } else if (equalsIgnoreCase(nsView, "SETTINGS")) {
    return parseSettings(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  } else if (equalsIgnoreCase(nsView, "PROPERTIES")) {
    return parseProperties(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  } else if (equalsIgnoreCase(nsView, "SYS")) {
    return parseSys(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  } else if (equalsIgnoreCase(nsView, "CLIENT")) {
    return parseClient(action.c_str(), hasEquals ? argsStr.c_str() : nullptr, ctx);
  }

  return core::CommandResult::unrecognized();
}

// =============================================================================
// NAMESPACE: CMD
// =============================================================================

core::CommandResult StandardCommandParser::parseCmd(const char* action, const char* args, const ClientContext& ctx) {
  if (equalsIgnoreCase(action, "startFeed")) {
    return parseStartFeed(args, ctx);
  } else if (equalsIgnoreCase(action, "startCycle")) {
    return parseStartCycle(args, ctx);
  } else if (equalsIgnoreCase(action, "stop")) {
    if (args != nullptr) return core::CommandResult::unrecognized();
    core::CommandResult res = core::CoreCommandService::instance().execute(core::StopCmd{}, ctx);
    res.command = "CMD.stop";
    return res;
  } else if (equalsIgnoreCase(action, "hold")) {
    if (args != nullptr) return core::CommandResult::unrecognized();
    core::CommandResult res = core::CoreCommandService::instance().execute(core::HoldCmd{}, ctx);
    res.command = "CMD.hold";
    return res;
  } else if (equalsIgnoreCase(action, "resume")) {
    if (args != nullptr) return core::CommandResult::unrecognized();
    core::CommandResult res = core::CoreCommandService::instance().execute(core::ResumeCmd{}, ctx);
    res.command = "CMD.resume";
    return res;
  } else if (equalsIgnoreCase(action, "step")) {
    if (args != nullptr) return core::CommandResult::unrecognized();
    core::CommandResult res = core::CoreCommandService::instance().execute(core::StepCmd{}, ctx);
    res.command = "CMD.step";
    return res;
  } else if (equalsIgnoreCase(action, "jogStep")) {
    return parseJogStep(args, ctx);
  } else if (equalsIgnoreCase(action, "jogHold")) {
    return parseJogHold(args, ctx);
  } else if (equalsIgnoreCase(action, "jogRelease")) {
    return parseJogRelease(args, ctx);
  } else if (equalsIgnoreCase(action, "axisZero")) {
    return parseAxisZero(args, ctx);
  } else if (equalsIgnoreCase(action, "axisPosDu")) {
    return parseAxisPosDu(args, ctx);
  } else if (equalsIgnoreCase(action, "axisLimitDu")) {
    return parseAxisLimitDu(args, ctx);
  } else if (equalsIgnoreCase(action, "axisClearLimit")) {
    return parseAxisClearLimit(args, ctx);
  } else if (equalsIgnoreCase(action, "ping")) {
    if (args != nullptr) return core::CommandResult::unrecognized();
    core::CommandResult res = core::CoreCommandService::instance().execute(core::PingCmd{}, ctx);
    res.command = "CMD.ping";
    return res;
  }

  return core::CommandResult::unrecognized();
}

core::CommandResult StandardCommandParser::parseStartFeed(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.startFeed", "MISSING_ARGUMENTS");
  }

  core::StartFeedCmd cmd;

  // Check if args are Key-Value (contains ':') or Positional CSV
  if (std::strchr(args, ':') != nullptr) {
    std::vector<KeyValuePair> pairs;
    if (!parseKeyValuesStrict(args, pairs)) {
      return core::CommandResult::unrecognized();
    }

    const auto* ax = findKey(pairs, "axis");
    if (ax) cmd.axis = parseAxis(ax->val);

    const auto* dir = findKey(pairs, "dir");
    if (dir) cmd.direction = parseDirection(dir->val);

    const auto* sync = findKey(pairs, "sync");
    if (sync) cmd.syncSpindle = parseBoolVal(sync->val, true);

    const auto* feed = findKey(pairs, "feed");
    if (feed) {
      if (!parseDeciMicrons(feed->val, &cmd.feedRate) || cmd.feedRate <= 0) {
        return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:feed");
      }
    } else {
      return core::CommandResult::failed("CMD.startFeed", "MISSING_PARAM:feed");
    }

    const auto* dist = findKey(pairs, "dist");
    if (dist) {
      parseDeciMicrons(dist->val, &cmd.targetDistDu);
    }
  } else {
    // Positional CSV: axis,feed,dir,[sync],[dist]
    char buf[128];
    std::strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char* token = std::strtok(buf, ",");
    int idx = 0;
    while (token) {
      std::string t(token);
      if (idx == 0) {
        cmd.axis = parseAxis(t);
      } else if (idx == 1) {
        if (!parseDeciMicrons(t, &cmd.feedRate) || cmd.feedRate <= 0) {
          return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:feed");
        }
      } else if (idx == 2) {
        cmd.direction = parseDirection(t);
      } else if (idx == 3) {
        cmd.syncSpindle = parseBoolVal(t, true);
      } else if (idx == 4) {
        parseDeciMicrons(t, &cmd.targetDistDu);
      }
      token = std::strtok(nullptr, ",");
      idx++;
    }

    if (idx < 2) {
      return core::CommandResult::failed("CMD.startFeed", "MISSING_ARGUMENTS");
    }
  }

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.startFeed";
  return res;
}

core::CommandResult StandardCommandParser::parseStartCycle(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.startCycle", "MISSING_ARGUMENTS");
  }

  std::vector<KeyValuePair> pairs;
  if (!parseKeyValuesStrict(args, pairs)) {
    return core::CommandResult::unrecognized();
  }

  const auto* modeKv = findKey(pairs, "mode");
  if (!modeKv || modeKv->val.empty()) {
    return core::CommandResult::failed("CMD.startCycle", "MISSING_PARAM:mode");
  }

  const std::string& mode = modeKv->val;

  // 1. Threading Mode
  if (equalsIgnoreCase(mode, "thread") || equalsIgnoreCase(mode, "threading")) {
    core::ThreadingCycleParams p;

    const auto* pitch = findKey(pairs, "pitch");
    if (!pitch || !parseDeciMicrons(pitch->val, &p.pitchDupr) || p.pitchDupr <= 0) {
      return core::CommandResult::failed("CMD.startCycle", pitch ? "INVALID_PARAM:pitch" : "MISSING_PARAM:pitch");
    }

    const auto* depth = findKey(pairs, "depth");
    if (!depth) depth = findKey(pairs, "doc");
    if (!depth || !parseDeciMicrons(depth->val, &p.depthDu) || p.depthDu <= 0) {
      return core::CommandResult::failed("CMD.startCycle", depth ? "INVALID_PARAM:depth" : "MISSING_PARAM:depth");
    }

    const auto* zStart = findKey(pairs, "zStart");
    if (!zStart || !parseDeciMicrons(zStart->val, &p.zStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", zStart ? "INVALID_PARAM:zStart" : "MISSING_PARAM:zStart");
    }

    const auto* zEnd = findKey(pairs, "zEnd");
    if (!zEnd || !parseDeciMicrons(zEnd->val, &p.zEndDu)) {
      return core::CommandResult::failed("CMD.startCycle", zEnd ? "INVALID_PARAM:zEnd" : "MISSING_PARAM:zEnd");
    }

    const auto* xClear = findKey(pairs, "xClear");
    if (!xClear || !parseDeciMicrons(xClear->val, &p.xClearDu)) {
      return core::CommandResult::failed("CMD.startCycle", xClear ? "INVALID_PARAM:xClear" : "MISSING_PARAM:xClear");
    }

    const auto* passes = findKey(pairs, "passes");
    if (passes) {
      int val = std::atoi(passes->val.c_str());
      if (val < 1) return core::CommandResult::failed("CMD.startCycle", "INVALID_PARAM:passes");
      p.passes = static_cast<uint16_t>(val);
    }

    const auto* spring = findKey(pairs, "spring");
    if (spring) {
      p.springPasses = static_cast<uint16_t>(std::atoi(spring->val.c_str()));
    }

    const auto* angle = findKey(pairs, "angle");
    if (!angle) angle = findKey(pairs, "infeed");
    if (angle) {
      p.infeedAngle = static_cast<float>(std::atof(angle->val.c_str()));
    }

    const auto* step = findKey(pairs, "step");
    if (step) {
      p.stepMode = parseBoolVal(step->val, false);
    }

    core::StartCycleCmd cmd;
    cmd.params = p;
    core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
    res.command = "CMD.startCycle";
    return res;
  }

  // 2. Turning Mode
  if (equalsIgnoreCase(mode, "turn") || equalsIgnoreCase(mode, "turning")) {
    core::TurningCycleParams p;

    const auto* feed = findKey(pairs, "feed");
    if (!feed || !parseDeciMicrons(feed->val, &p.feedDupr) || p.feedDupr <= 0) {
      return core::CommandResult::failed("CMD.startCycle", feed ? "INVALID_PARAM:feed" : "MISSING_PARAM:feed");
    }

    const auto* doc = findKey(pairs, "doc");
    if (!doc || !parseDeciMicrons(doc->val, &p.docDu) || p.docDu <= 0) {
      return core::CommandResult::failed("CMD.startCycle", doc ? "INVALID_PARAM:doc" : "MISSING_PARAM:doc");
    }

    const auto* zStart = findKey(pairs, "zStart");
    if (!zStart || !parseDeciMicrons(zStart->val, &p.zStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", zStart ? "INVALID_PARAM:zStart" : "MISSING_PARAM:zStart");
    }

    const auto* zEnd = findKey(pairs, "zEnd");
    if (!zEnd || !parseDeciMicrons(zEnd->val, &p.zEndDu)) {
      return core::CommandResult::failed("CMD.startCycle", zEnd ? "INVALID_PARAM:zEnd" : "MISSING_PARAM:zEnd");
    }

    const auto* xStart = findKey(pairs, "xStart");
    if (!xStart || !parseDeciMicrons(xStart->val, &p.xStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", xStart ? "INVALID_PARAM:xStart" : "MISSING_PARAM:xStart");
    }

    const auto* xClear = findKey(pairs, "xClear");
    if (!xClear || !parseDeciMicrons(xClear->val, &p.xClearDu)) {
      return core::CommandResult::failed("CMD.startCycle", xClear ? "INVALID_PARAM:xClear" : "MISSING_PARAM:xClear");
    }

    const auto* passes = findKey(pairs, "passes");
    if (passes) {
      int val = std::atoi(passes->val.c_str());
      if (val < 1) return core::CommandResult::failed("CMD.startCycle", "INVALID_PARAM:passes");
      p.passes = static_cast<uint16_t>(val);
    }

    const auto* sync = findKey(pairs, "sync");
    if (sync) p.syncSpindle = parseBoolVal(sync->val, true);

    const auto* step = findKey(pairs, "step");
    if (step) p.stepMode = parseBoolVal(step->val, false);

    core::StartCycleCmd cmd;
    cmd.params = p;
    core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
    res.command = "CMD.startCycle";
    return res;
  }

  // 3. Facing Mode
  if (equalsIgnoreCase(mode, "face") || equalsIgnoreCase(mode, "facing")) {
    core::FacingCycleParams p;

    const auto* feed = findKey(pairs, "feed");
    if (!feed || !parseDeciMicrons(feed->val, &p.feedDupr) || p.feedDupr <= 0) {
      return core::CommandResult::failed("CMD.startCycle", feed ? "INVALID_PARAM:feed" : "MISSING_PARAM:feed");
    }

    const auto* doc = findKey(pairs, "doc");
    if (!doc || !parseDeciMicrons(doc->val, &p.docDu) || p.docDu <= 0) {
      return core::CommandResult::failed("CMD.startCycle", doc ? "INVALID_PARAM:doc" : "MISSING_PARAM:doc");
    }

    const auto* xStart = findKey(pairs, "xStart");
    if (!xStart || !parseDeciMicrons(xStart->val, &p.xStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", xStart ? "INVALID_PARAM:xStart" : "MISSING_PARAM:xStart");
    }

    const auto* xEnd = findKey(pairs, "xEnd");
    if (!xEnd || !parseDeciMicrons(xEnd->val, &p.xEndDu)) {
      return core::CommandResult::failed("CMD.startCycle", xEnd ? "INVALID_PARAM:xEnd" : "MISSING_PARAM:xEnd");
    }

    const auto* zStart = findKey(pairs, "zStart");
    if (!zStart || !parseDeciMicrons(zStart->val, &p.zStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", zStart ? "INVALID_PARAM:zStart" : "MISSING_PARAM:zStart");
    }

    const auto* zClear = findKey(pairs, "zClear");
    if (!zClear || !parseDeciMicrons(zClear->val, &p.zClearDu)) {
      return core::CommandResult::failed("CMD.startCycle", zClear ? "INVALID_PARAM:zClear" : "MISSING_PARAM:zClear");
    }

    const auto* passes = findKey(pairs, "passes");
    if (passes) {
      int val = std::atoi(passes->val.c_str());
      if (val < 1) return core::CommandResult::failed("CMD.startCycle", "INVALID_PARAM:passes");
      p.passes = static_cast<uint16_t>(val);
    }

    const auto* sync = findKey(pairs, "sync");
    if (sync) p.syncSpindle = parseBoolVal(sync->val, true);

    const auto* step = findKey(pairs, "step");
    if (step) p.stepMode = parseBoolVal(step->val, false);

    core::StartCycleCmd cmd;
    cmd.params = p;
    core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
    res.command = "CMD.startCycle";
    return res;
  }

  // 4. Taper Mode
  if (equalsIgnoreCase(mode, "taper")) {
    core::TaperCycleParams p;

    const auto* feed = findKey(pairs, "feed");
    if (!feed || !parseDeciMicrons(feed->val, &p.feedDupr) || p.feedDupr <= 0) {
      return core::CommandResult::failed("CMD.startCycle", feed ? "INVALID_PARAM:feed" : "MISSING_PARAM:feed");
    }

    const auto* angle = findKey(pairs, "angle");
    const auto* ratio = findKey(pairs, "ratio");
    if (angle) {
      p.taperAngleDeg = static_cast<float>(std::atof(angle->val.c_str()));
    } else if (ratio) {
      // ratio e.g. "1:5" -> atan(1/5)
      const char* colon = std::strchr(ratio->val.c_str(), ':');
      if (colon && *(colon + 1)) {
        double r = std::atof(colon + 1);
        if (r > 0) p.taperAngleDeg = static_cast<float>(std::atan(1.0 / r) * 180.0 / M_PI);
      }
    } else {
      return core::CommandResult::failed("CMD.startCycle", "MISSING_PARAM:angle");
    }

    const auto* zStart = findKey(pairs, "zStart");
    if (!zStart || !parseDeciMicrons(zStart->val, &p.zStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", zStart ? "INVALID_PARAM:zStart" : "MISSING_PARAM:zStart");
    }

    const auto* zEnd = findKey(pairs, "zEnd");
    if (!zEnd || !parseDeciMicrons(zEnd->val, &p.zEndDu)) {
      return core::CommandResult::failed("CMD.startCycle", zEnd ? "INVALID_PARAM:zEnd" : "MISSING_PARAM:zEnd");
    }

    const auto* xStart = findKey(pairs, "xStart");
    if (!xStart || !parseDeciMicrons(xStart->val, &p.xStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", xStart ? "INVALID_PARAM:xStart" : "MISSING_PARAM:xStart");
    }

    const auto* xClear = findKey(pairs, "xClear");
    if (!xClear || !parseDeciMicrons(xClear->val, &p.xClearDu)) {
      return core::CommandResult::failed("CMD.startCycle", xClear ? "INVALID_PARAM:xClear" : "MISSING_PARAM:xClear");
    }

    const auto* step = findKey(pairs, "step");
    if (step) p.stepMode = parseBoolVal(step->val, false);

    core::StartCycleCmd cmd;
    cmd.params = p;
    core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
    res.command = "CMD.startCycle";
    return res;
  }

  // 5. Cutoff Mode
  if (equalsIgnoreCase(mode, "cutoff") || equalsIgnoreCase(mode, "parting")) {
    core::CutoffCycleParams p;

    const auto* feed = findKey(pairs, "feed");
    if (!feed || !parseDeciMicrons(feed->val, &p.feedDupr) || p.feedDupr <= 0) {
      return core::CommandResult::failed("CMD.startCycle", feed ? "INVALID_PARAM:feed" : "MISSING_PARAM:feed");
    }

    const auto* peck = findKey(pairs, "peck");
    if (!peck || !parseDeciMicrons(peck->val, &p.peckDepthDu) || p.peckDepthDu <= 0) {
      return core::CommandResult::failed("CMD.startCycle", peck ? "INVALID_PARAM:peck" : "MISSING_PARAM:peck");
    }

    const auto* retract = findKey(pairs, "retract");
    if (!retract || !parseDeciMicrons(retract->val, &p.retractDu) || p.retractDu <= 0) {
      return core::CommandResult::failed("CMD.startCycle", retract ? "INVALID_PARAM:retract" : "MISSING_PARAM:retract");
    }

    const auto* xStart = findKey(pairs, "xStart");
    if (!xStart || !parseDeciMicrons(xStart->val, &p.xStartDu)) {
      return core::CommandResult::failed("CMD.startCycle", xStart ? "INVALID_PARAM:xStart" : "MISSING_PARAM:xStart");
    }

    const auto* xEnd = findKey(pairs, "xEnd");
    if (!xEnd || !parseDeciMicrons(xEnd->val, &p.xEndDu)) {
      return core::CommandResult::failed("CMD.startCycle", xEnd ? "INVALID_PARAM:xEnd" : "MISSING_PARAM:xEnd");
    }

    const auto* zPos = findKey(pairs, "zPos");
    if (!zPos || !parseDeciMicrons(zPos->val, &p.zPosDu)) {
      return core::CommandResult::failed("CMD.startCycle", zPos ? "INVALID_PARAM:zPos" : "MISSING_PARAM:zPos");
    }

    core::StartCycleCmd cmd;
    cmd.params = p;
    core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
    res.command = "CMD.startCycle";
    return res;
  }

  return core::CommandResult::failed("CMD.startCycle", String("UNKNOWN_MODE:") + mode);
}

core::CommandResult StandardCommandParser::parseJogStep(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.jogStep", "MISSING_ARGUMENTS");
  }

  core::JogStepCmd cmd;
  if (std::strchr(args, ':') != nullptr) {
    std::vector<KeyValuePair> pairs;
    if (!parseKeyValuesStrict(args, pairs)) return core::CommandResult::unrecognized();
    const auto* ax = findKey(pairs, "axis");
    if (ax) cmd.axis = parseAxis(ax->val);
    const auto* dist = findKey(pairs, "dist");
    if (dist) parseDeciMicrons(dist->val, &cmd.distDu);
  } else {
    char buf[64];
    std::strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* token = std::strtok(buf, ",");
    if (token) cmd.axis = parseAxis(token);
    token = std::strtok(nullptr, ",");
    if (token) parseDeciMicrons(token, &cmd.distDu);
  }

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.jogStep";
  return res;
}

core::CommandResult StandardCommandParser::parseJogHold(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.jogHold", "MISSING_ARGUMENTS");
  }

  core::JogHoldCmd cmd;
  if (std::strchr(args, ':') != nullptr) {
    std::vector<KeyValuePair> pairs;
    if (!parseKeyValuesStrict(args, pairs)) return core::CommandResult::unrecognized();
    const auto* ax = findKey(pairs, "axis");
    if (ax) cmd.axis = parseAxis(ax->val);
    const auto* dir = findKey(pairs, "dir");
    if (dir) cmd.direction = parseDirection(dir->val);
  } else {
    char buf[64];
    std::strncpy(buf, args, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    char* token = std::strtok(buf, ",");
    if (token) cmd.axis = parseAxis(token);
    token = std::strtok(nullptr, ",");
    if (token) cmd.direction = parseDirection(token);
  }

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.jogHold";
  return res;
}

core::CommandResult StandardCommandParser::parseJogRelease(const char* args, const ClientContext& ctx) {
  core::JogReleaseCmd cmd;
  if (args && *args != '\0') {
    cmd.axis = parseAxis(args);
  }
  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.jogRelease";
  return res;
}

core::CommandResult StandardCommandParser::parseAxisZero(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.axisZero", "MISSING_ARGUMENTS");
  }
  core::AxisZeroCmd cmd{ parseAxis(args) };
  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.axisZero";
  return res;
}

core::CommandResult StandardCommandParser::parseAxisPosDu(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.axisPosDu", "MISSING_ARGUMENTS");
  }

  core::AxisPosCmd cmd;
  char buf[64];
  std::strncpy(buf, args, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char* token = std::strtok(buf, ",");
  if (token) cmd.axis = parseAxis(token);
  token = std::strtok(nullptr, ",");
  if (token) parseDeciMicrons(token, &cmd.posDu);

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.axisPosDu";
  return res;
}

core::CommandResult StandardCommandParser::parseAxisLimitDu(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.axisLimitDu", "MISSING_ARGUMENTS");
  }

  core::AxisLimitCmd cmd;
  char buf[64];
  std::strncpy(buf, args, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char* token = std::strtok(buf, ",");
  if (token) cmd.axis = parseAxis(token);
  token = std::strtok(nullptr, ",");
  if (token) {
    std::string b(token);
    cmd.limitBound = (b == "2" || equalsIgnoreCase(b, "R") || equalsIgnoreCase(b, "RIGHT") || equalsIgnoreCase(b, "TAIL")) ? 2 : 1;
  }
  token = std::strtok(nullptr, ",");
  if (token) parseDeciMicrons(token, &cmd.coordDu);

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.axisLimitDu";
  return res;
}

core::CommandResult StandardCommandParser::parseAxisClearLimit(const char* args, const ClientContext& ctx) {
  if (!args || *args == '\0') {
    return core::CommandResult::failed("CMD.axisClearLimit", "MISSING_ARGUMENTS");
  }

  core::AxisClearLimitCmd cmd;
  char buf[64];
  std::strncpy(buf, args, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';
  char* token = std::strtok(buf, ",");
  if (token) cmd.axis = parseAxis(token);
  token = std::strtok(nullptr, ",");
  if (token) {
    std::string b(token);
    cmd.limitBound = (b == "2" || equalsIgnoreCase(b, "R") || equalsIgnoreCase(b, "RIGHT") || equalsIgnoreCase(b, "TAIL")) ? 2 : 1;
  }

  core::CommandResult res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.axisClearLimit";
  return res;
}

// =============================================================================
// NAMESPACE: STATE
// =============================================================================

core::CommandResult StandardCommandParser::parseState(const char* action, const char* args, const ClientContext& /*ctx*/) {
  if (!equalsIgnoreCase(action, "get")) {
    return core::CommandResult::unrecognized();
  }

  uint8_t domainMask = DOMAIN_ALL;
  if (args && *args != '\0' && !equalsIgnoreCase(args, "ALL")) {
    domainMask = DOMAIN_NONE;
    const char* p = args;
    while (*p) {
      if (*p == ',') { p++; continue; }
      char c = std::toupper(static_cast<unsigned char>(*p));
      if (c == 'K') domainMask |= DOMAIN_KINEMATICS;
      else if (c == 'I') domainMask |= DOMAIN_INTENT;
      else if (c == 'C') domainMask |= DOMAIN_CYCLE;
      else if (c == 'P') domainMask |= DOMAIN_PROPERTIES;
      else return core::CommandResult::unrecognized();
      p++;
    }
    if (domainMask == DOMAIN_NONE) domainMask = DOMAIN_ALL;
  }

  String telemetry = TelemetrySerializer::buildTelemetry(domainMask);
  return core::CommandResult::ok("STATE.get", telemetry);
}

// =============================================================================
// NAMESPACE: SETTINGS
// =============================================================================

core::CommandResult StandardCommandParser::parseSettings(const char* action, const char* args, const ClientContext& ctx) {
  if (equalsIgnoreCase(action, "get")) {
    String reply;
    bool ok = SettingsService::get((args && *args) ? args : nullptr, &reply);
    return ok ? core::CommandResult::ok("SETTINGS.get", reply) : core::CommandResult::failed("SETTINGS.get", reply.empty() ? "Read failed" : reply);
  }

  if (equalsIgnoreCase(action, "set")) {
    if (!ctx.hasPermission(PERM_SETTINGS_WRITE)) {
      return core::CommandResult::failed("SETTINGS.set", "PERMISSION_DENIED", 403);
    }
    String reply;
    bool ok = SettingsService::set(args, &reply);
    return ok ? core::CommandResult::ok("SETTINGS.set", reply) : core::CommandResult::failed("SETTINGS.set", reply.empty() ? "Write failed" : reply);
  }

  if (equalsIgnoreCase(action, "reset")) {
    if (!ctx.hasPermission(PERM_SETTINGS_WRITE)) {
      return core::CommandResult::failed("SETTINGS.reset", "PERMISSION_DENIED", 403);
    }
    String reply;
    bool ok = SettingsService::reset(args, &reply);
    return ok ? core::CommandResult::ok("SETTINGS.reset", reply) : core::CommandResult::failed("SETTINGS.reset", reply.empty() ? "Reset failed" : reply);
  }

  if (equalsIgnoreCase(action, "schema")) {
    String reply;
    bool ok = SettingsService::getSchema(args, &reply);
    return ok ? core::CommandResult::ok("SETTINGS.schema", reply) : core::CommandResult::failed("SETTINGS.schema", "Schema not found");
  }

  if (equalsIgnoreCase(action, "presets")) {
    String reply;
    bool ok = SettingsService::listPresets(&reply);
    return ok ? core::CommandResult::ok("SETTINGS.presets", reply) : core::CommandResult::failed("SETTINGS.presets", "Preset list failed");
  }

  return core::CommandResult::unrecognized();
}

// =============================================================================
// NAMESPACE: PROPERTIES
// =============================================================================

core::CommandResult StandardCommandParser::parseProperties(const char* action, const char* /*args*/, const ClientContext& /*ctx*/) {
  if (equalsIgnoreCase(action, "modes")) {
    String reply = ModeRegistry::serializeDiscoveryJson(SettingsService::getActiveProfile().capabilities, false);
    return core::CommandResult::ok("PROPERTIES.modes", reply);
  }

  if (equalsIgnoreCase(action, "profiles")) {
    String reply;
    bool ok = SettingsService::listPresets(&reply);
    return ok ? core::CommandResult::ok("PROPERTIES.profiles", reply) : core::CommandResult::failed("PROPERTIES.profiles", "Failed to list profiles");
  }

  return core::CommandResult::unrecognized();
}

// =============================================================================
// NAMESPACE: SYS
// =============================================================================

core::CommandResult StandardCommandParser::parseSys(const char* action, const char* args, const ClientContext& /*ctx*/) {
  if (equalsIgnoreCase(action, "get")) {
    if (args && equalsIgnoreCase(args, "health")) {
      String out = "SYS.uptime=0\nSYS.heap=142 KB\nSYS.wifi=connected\n";
      return core::CommandResult::ok("SYS.get", out);
    }
    // Default: info
    String out = "SYS.version=1.0.5\nSYS.chip=ESP32-S3\nSYS.flash=16 MB\nSYS.features=spindleSim,ota\n";
    return core::CommandResult::ok("SYS.get", out);
  }

  return core::CommandResult::unrecognized();
}

// =============================================================================
// NAMESPACE: CLIENT
// =============================================================================

core::CommandResult StandardCommandParser::parseClient(const char* action, const char* args, const ClientContext& /*ctx*/) {
  if (equalsIgnoreCase(action, "hello")) {
    if (!args || *args == '\0') {
      return core::CommandResult::unrecognized();
    }

    std::vector<KeyValuePair> pairs;
    if (!parseKeyValuesStrict(args, pairs)) {
      return core::CommandResult::unrecognized();
    }

    const auto* type = findKey(pairs, "type");
    std::string profileType = type ? type->val : "unknown";

    String ack = "session_id=1,type=" + profileType + ",controllerVer=1.0.5,perms=0xFFFF";
    return core::CommandResult::ok("CLIENT.hello", ack);
  }

  return core::CommandResult::unrecognized();
}

} // namespace whatels::core_client
