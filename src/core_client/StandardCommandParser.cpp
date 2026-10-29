#include "StandardCommandParser.h"
#include "core/Config.h"
#include "core/CoreCommandService.h"
#include "core/TelemetrySerializer.h"
#include "core/SettingsService.h"
#include "core/PresetService.h"
#include "modes/ModeRegistry.h"

#include <cstring>
#include <cstdlib>
#include <cctype>
#include <cmath>
#include <array>
#include <string_view>

namespace whatels::core_client {

namespace {

// =============================================================================
// 1. UNIFIED IN-PLACE NUMBER & GRAMMAR PARSERS (ZERO HEAP ALLOCATION)
// =============================================================================

bool isValidIdentifier(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

// Single authoritative in-place number parser replacing 3 hand-rolled loops
bool parseNumber(std::string_view s, double* out) {
  if (s.empty() || !out) return false;
  size_t i = 0;
  bool negative = false;
  if (s[0] == '-') {
    negative = true;
    i++;
  } else if (s[0] == '+') {
    i++;
  }
  if (i >= s.size()) return false;

  double val = 0.0;
  size_t dotPos = s.find('.', i);
  size_t intEnd = (dotPos != std::string_view::npos) ? dotPos : s.size();

  for (; i < intEnd; ++i) {
    if (s[i] < '0' || s[i] > '9') return false;
    val = val * 10.0 + (s[i] - '0');
  }

  if (dotPos != std::string_view::npos) {
    i = dotPos + 1;
    if (i >= s.size()) return false; // Trailing dot with no digits
    double frac = 0.1;
    for (; i < s.size(); ++i, frac *= 0.1) {
      if (s[i] < '0' || s[i] > '9') return false;
      val += (s[i] - '0') * frac;
    }
  }

  *out = negative ? -val : val;
  return true;
}

bool parseDeciMicrons(std::string_view s, int32_t* outValue) {
  if (s.empty() || !outValue) return false;
  double val = 0.0;
  if (!parseNumber(s, &val)) return false;
  *outValue = (s.find('.') != std::string_view::npos)
      ? static_cast<int32_t>(std::lround(val * 10000.0))
      : static_cast<int32_t>(val);
  return true;
}

bool parseUint16(std::string_view s, uint16_t* outValue, uint16_t minVal = 0) {
  double val = 0.0;
  if (!parseNumber(s, &val) || val < minVal || val > 65535.0 || s.find('.') != std::string_view::npos) {
    return false;
  }
  *outValue = static_cast<uint16_t>(val);
  return true;
}

bool parseFloat32(std::string_view s, float* outValue) {
  double val = 0.0;
  if (!parseNumber(s, &val)) return false;
  *outValue = static_cast<float>(val);
  return true;
}

bool parseBool(std::string_view s, bool* outValue) {
  if (!outValue) return false;
  if (s == "1") { *outValue = true;  return true; }
  if (s == "0") { *outValue = false; return true; }
  return false;
}

bool parseAxis(std::string_view s, AxisId* outAxis) {
  if (!outAxis) return false;
  if (s == "Z") {
    *outAxis = AXIS_Z;
    return true;
  }
  if (s == "X") {
    *outAxis = AXIS_X;
    return true;
  }
  return false;
}

// Single authoritative mapping of text representation to machine physical polarity
// -1: infeed into cut / toward chuck / toward centerline (Bound 1 / Lower limit)
// +1: retract away from cut / toward tailstock (Bound 2 / Upper limit)
bool parsePolarity(std::string_view s, int8_t* outPolarity) {
  if (!outPolarity) return false;
  if (s == "-" || s == "-1" || s == "LEFT" || s == "CHUCK" || s == "CENTER" ||
      s == "IN" || s == "INFEED" || s == "REV" || s == "UP") {
    *outPolarity = -1;
    return true;
  }
  if (s == "+" || s == "+1" || s == "1" || s == "RIGHT" || s == "TAIL" ||
      s == "RETRACT" || s == "OUT" || s == "FWD" || s == "DOWN") {
    *outPolarity = 1;
    return true;
  }
  return false;
}

bool parseLimitBound(std::string_view s, int8_t* outBound) {
  if (!outBound) return false;
  if (s == "1" || s == "L") {
    *outBound = 1;
    return true;
  }
  if (s == "2" || s == "R") {
    *outBound = 2;
    return true;
  }
  int8_t polarity = 0;
  if (parsePolarity(s, &polarity)) {
    *outBound = (polarity < 0) ? 1 : 2;
    return true;
  }
  return false;
}

// =============================================================================
// 2. STACK-ONLY ARGUMENT TOKENIZER (ZERO DYNAMIC ALLOCATION)
// =============================================================================

struct ArgList {
  struct Entry {
    std::string_view key;
    std::string_view val;
  };
  std::array<Entry, 16> items{};
  size_t count = 0;
  bool isKv = false;

  std::string_view get(std::string_view key, size_t pos = SIZE_MAX) const {
    if (isKv) {
      for (size_t i = 0; i < count; ++i) {
        if (items[i].key == key) return items[i].val;
      }
      return {};
    }
    return (pos < count) ? items[pos].val : std::string_view{};
  }

  bool has(std::string_view key) const {
    for (size_t i = 0; i < count; ++i) {
      if (items[i].key == key) return true;
    }
    return false;
  }
};

std::string_view unquote(std::string_view s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

bool tokenizeArgs(std::string_view s, ArgList* out) {
  if (s.empty()) return true;

  size_t start = 0;
  bool inQuotes = false;

  for (size_t i = 0; i <= s.size(); ++i) {
    char c = (i < s.size()) ? s[i] : ',';

    if (c == '"') {
      inQuotes = !inQuotes;
    } else if (c == ',' && !inQuotes) {
      if (i == start) return false; // Leading, double (',,'), or trailing comma

      std::string_view token = s.substr(start, i - start);
      start = i + 1;

      size_t colon = token.find(':');
      if (colon != std::string_view::npos) {
        if (colon == 0) return false;
        if (out->count > 0 && !out->isKv) return false; // Mixed positional + KV rejected
        out->isKv = true;
        std::string_view key = token.substr(0, colon);
        if (!isValidIdentifier(key)) return false;
        out->items[out->count++] = { key, unquote(token.substr(colon + 1)) };
      } else {
        if (out->isKv) return false; // Mixed KV + positional rejected
        out->items[out->count++] = { {}, unquote(token) };
      }

      if (out->count >= out->items.size()) return false;
    }
  }

  return !inQuotes && (start > s.size());
}

// =============================================================================
// 3. PARAMETER EXTRACTOR & VALIDATION HELPERS
// =============================================================================

bool extractAxis(const ArgList& args, const char* cmdName, AxisId* outAxis, core::CommandResult* outErr) {
  auto ax = args.get("axis", 0);
  if (ax.empty() || !parseAxis(ax, outAxis)) {
    *outErr = core::CommandResult::failed(cmdName, "INVALID_PARAM:axis");
    return false;
  }
  return true;
}

struct ParamExtractor {
  const ArgList& args;
  const char* failKey = nullptr;
  bool missing = false;

  bool du(std::string_view key, int32_t& out, bool req = true, bool posOnly = false, std::string_view alias = {}) {
    auto v = args.get(key);
    if (v.empty() && !alias.empty()) v = args.get(alias);
    if (v.empty()) {
      if (req) { failKey = key.data(); missing = true; return false; }
      return true;
    }
    if (!parseDeciMicrons(v, &out) || (posOnly && out <= 0)) {
      failKey = key.data(); missing = false; return false;
    }
    return true;
  }

  bool u16(std::string_view key, uint16_t& out, bool req = false, uint16_t minVal = 0) {
    auto v = args.get(key);
    if (v.empty()) {
      if (req) { failKey = key.data(); missing = true; return false; }
      return true;
    }
    if (!parseUint16(v, &out, minVal)) {
      failKey = key.data(); missing = false; return false;
    }
    return true;
  }

  bool f32(std::string_view key, float& out, bool req = false, std::string_view alias = {}) {
    auto v = args.get(key);
    if (v.empty() && !alias.empty()) v = args.get(alias);
    if (v.empty()) {
      if (req) { failKey = key.data(); missing = true; return false; }
      return true;
    }
    if (!parseFloat32(v, &out)) {
      failKey = key.data(); missing = false; return false;
    }
    return true;
  }

  bool boolean(std::string_view key, bool& out, bool defaultVal = false) {
    auto v = args.get(key);
    if (v.empty()) { out = defaultVal; return true; }
    return parseBool(v, &out);
  }

  bool taperAngle(float& out) {
    auto ang = args.get("angle");
    auto rat = args.get("ratio");
    if (!ang.empty()) {
      if (parseFloat32(ang, &out)) return true;
      failKey = "angle"; missing = false; return false;
    }
    if (!rat.empty()) {
      size_t c = rat.find(':');
      float r = 0;
      if (c != std::string_view::npos && parseFloat32(rat.substr(c + 1), &r) && r > 0) {
        out = static_cast<float>(std::atan(1.0f / r) * 180.0f / M_PI);
        return true;
      }
      failKey = "ratio"; missing = false; return false;
    }
    failKey = "angle"; missing = true; return false;
  }

  String formatError() const {
    return String(missing ? "MISSING_PARAM:" : "INVALID_PARAM:") + (failKey ? failKey : "");
  }
};

// =============================================================================
// 4. MOTION COMMAND HANDLERS
// =============================================================================

core::CommandResult parseStartFeed(const ArgList& args, const ClientContext& ctx) {
  if (!args.isKv && args.count < 2) return core::CommandResult::failed("CMD.startFeed", "MISSING_ARGUMENTS");

  core::StartFeedCmd cmd;
  core::CommandResult err;
  if (!extractAxis(args, "CMD.startFeed", &cmd.axis, &err)) return err;

  auto feedStr = args.get("feed", 1);
  if (feedStr.empty() || !parseDeciMicrons(feedStr, &cmd.feedRate) || cmd.feedRate <= 0) {
    bool hasFeed = args.has("feed");
    return core::CommandResult::failed("CMD.startFeed",
      (!hasFeed && args.isKv) ? "MISSING_PARAM:feed" : "INVALID_PARAM:feed");
  }

  auto dirStr = args.get("dir", 2);
  if (dirStr.empty() || !parsePolarity(dirStr, &cmd.direction)) {
    return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:dir");
  }

  auto syncStr = args.get("sync", 3);
  if (syncStr.empty()) {
    cmd.syncSpindle = true;
  } else if (!parseBool(syncStr, &cmd.syncSpindle)) {
    return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:sync");
  }

  if (args.has("dist") || (!args.isKv && args.count > 4)) {
    auto distStr = args.get("dist", 4);
    if (distStr.empty() || !parseDeciMicrons(distStr, &cmd.targetDistDu)) {
      return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:dist");
    }
  }

  auto res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.startFeed";
  return res;
}

core::CommandResult parseStartCycle(const ArgList& args, const ClientContext& ctx) {
  if (!args.isKv) return core::CommandResult::unrecognized();
  auto mode = args.get("mode");
  if (mode.empty()) return core::CommandResult::failed("CMD.startCycle", "MISSING_PARAM:mode");

  ParamExtractor p{ args, nullptr, false };
  core::StartCycleCmd cmd;

  if (mode == "thread" || mode == "threading") {
    core::ThreadingCycleParams cp;
    if (!p.du("pitch", cp.pitchDupr, true, true) ||
        !p.du("depth", cp.depthDu, true, true, "doc") ||
        !p.du("zStart", cp.zStartDu) || !p.du("zEnd", cp.zEndDu) ||
        !p.du("xClear", cp.xClearDu) ||
        !p.u16("passes", cp.passes, false, 1) ||
        !p.u16("spring", cp.springPasses) ||
        !p.f32("angle", cp.infeedAngle, false, "infeed") ||
        !p.boolean("step", cp.stepMode)) {
      return core::CommandResult::failed("CMD.startCycle", p.formatError());
    }
    cmd.params = cp;
  } else if (mode == "turn" || mode == "turning") {
    core::TurningCycleParams cp;
    if (!p.du("feed", cp.feedDupr, true, true) ||
        !p.du("doc", cp.docDu, true, true) ||
        !p.du("zStart", cp.zStartDu) || !p.du("zEnd", cp.zEndDu) ||
        !p.du("xStart", cp.xStartDu) || !p.du("xClear", cp.xClearDu) ||
        !p.u16("passes", cp.passes, false, 1) ||
        !p.boolean("sync", cp.syncSpindle, true) ||
        !p.boolean("step", cp.stepMode)) {
      return core::CommandResult::failed("CMD.startCycle", p.formatError());
    }
    cmd.params = cp;
  } else if (mode == "face" || mode == "facing") {
    core::FacingCycleParams cp;
    if (!p.du("feed", cp.feedDupr, true, true) ||
        !p.du("doc", cp.docDu, true, true) ||
        !p.du("xStart", cp.xStartDu) || !p.du("xEnd", cp.xEndDu) ||
        !p.du("zStart", cp.zStartDu) || !p.du("zClear", cp.zClearDu) ||
        !p.u16("passes", cp.passes, false, 1) ||
        !p.boolean("sync", cp.syncSpindle, true) ||
        !p.boolean("step", cp.stepMode)) {
      return core::CommandResult::failed("CMD.startCycle", p.formatError());
    }
    cmd.params = cp;
  } else if (mode == "taper") {
    core::TaperCycleParams cp;
    if (!p.du("feed", cp.feedDupr, true, true) ||
        !p.taperAngle(cp.taperAngleDeg) ||
        !p.du("zStart", cp.zStartDu) || !p.du("zEnd", cp.zEndDu) ||
        !p.du("xStart", cp.xStartDu) || !p.du("xClear", cp.xClearDu) ||
        !p.boolean("sync", cp.syncSpindle, true) ||
        !p.boolean("step", cp.stepMode)) {
      return core::CommandResult::failed("CMD.startCycle", p.formatError());
    }
    cmd.params = cp;
  } else if (mode == "cutoff" || mode == "parting") {
    core::CutoffCycleParams cp;
    if (!p.du("feed", cp.feedDupr, true, true) ||
        !p.du("peck", cp.peckDepthDu, true, true) ||
        !p.du("retract", cp.retractDu, true, true) ||
        !p.du("xStart", cp.xStartDu) || !p.du("xEnd", cp.xEndDu) ||
        !p.du("zPos", cp.zPosDu)) {
      return core::CommandResult::failed("CMD.startCycle", p.formatError());
    }
    cmd.params = cp;
  } else {
    return core::CommandResult::failed("CMD.startCycle", String("UNKNOWN_MODE:") + mode.data());
  }

  auto res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = "CMD.startCycle";
  return res;
}

struct CmdRoute {
  std::string_view name;
  const char* canonicalCmd;
  core::CommandResult (*run)(const ArgList&, const ClientContext&);
};

static const CmdRoute CMD_ROUTES[] = {
  { "startFeed", "CMD.startFeed", parseStartFeed },
  { "startCycle", "CMD.startCycle", parseStartCycle },
  { "jogStep", "CMD.jogStep", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; int32_t dist = 0; core::CommandResult err;
    if (!extractAxis(a, "CMD.jogStep", &ax, &err)) return err;
    auto d = a.get("dist", 1);
    if (d.empty() || !parseDeciMicrons(d, &dist)) return core::CommandResult::failed("CMD.jogStep", "INVALID_PARAM:dist");
    auto res = core::CoreCommandService::instance().execute(core::JogStepCmd{ax, dist}, c);
    res.command = "CMD.jogStep";
    return res;
  }},
  { "jogHold", "CMD.jogHold", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; int8_t dir = 0; core::CommandResult err;
    if (!extractAxis(a, "CMD.jogHold", &ax, &err)) return err;
    auto d = a.get("dir", 1);
    if (d.empty() || !parsePolarity(d, &dir)) return core::CommandResult::failed("CMD.jogHold", "INVALID_PARAM:dir");
    auto res = core::CoreCommandService::instance().execute(core::JogHoldCmd{ax, dir}, c);
    res.command = "CMD.jogHold";
    return res;
  }},
  { "jogRelease", "CMD.jogRelease", [](const ArgList& a, const ClientContext& c) {
    core::JogReleaseCmd cmd; core::CommandResult err;
    if (a.count > 0 && !extractAxis(a, "CMD.jogRelease", &cmd.axis, &err)) return err;
    auto res = core::CoreCommandService::instance().execute(cmd, c);
    res.command = "CMD.jogRelease";
    return res;
  }},
  { "axisZero", "CMD.axisZero", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; core::CommandResult err;
    if (!extractAxis(a, "CMD.axisZero", &ax, &err)) return err;
    auto res = core::CoreCommandService::instance().execute(core::AxisZeroCmd{ax}, c);
    res.command = "CMD.axisZero";
    return res;
  }},
  { "axisPosDu", "CMD.axisPosDu", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; int32_t pos = 0; core::CommandResult err;
    if (!extractAxis(a, "CMD.axisPosDu", &ax, &err)) return err;
    auto p = a.get("pos", 1);
    if (p.empty() || !parseDeciMicrons(p, &pos)) return core::CommandResult::failed("CMD.axisPosDu", "INVALID_PARAM:pos");
    auto res = core::CoreCommandService::instance().execute(core::AxisPosCmd{ax, pos}, c);
    res.command = "CMD.axisPosDu";
    return res;
  }},
  { "axisLimitDu", "CMD.axisLimitDu", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; int8_t bound = 0; int32_t coord = 0; core::CommandResult err;
    if (!extractAxis(a, "CMD.axisLimitDu", &ax, &err)) return err;
    auto b = a.get("bound", 1), cd = a.get("coord", 2);
    if (b.empty() || !parseLimitBound(b, &bound)) return core::CommandResult::failed("CMD.axisLimitDu", "INVALID_PARAM:bound");
    if (cd.empty() || !parseDeciMicrons(cd, &coord)) return core::CommandResult::failed("CMD.axisLimitDu", "INVALID_PARAM:coord");
    auto res = core::CoreCommandService::instance().execute(core::AxisLimitCmd{ax, bound, coord}, c);
    res.command = "CMD.axisLimitDu";
    return res;
  }},
  { "axisClearLimit", "CMD.axisClearLimit", [](const ArgList& a, const ClientContext& c) {
    AxisId ax; int8_t bound = 0; core::CommandResult err;
    if (!extractAxis(a, "CMD.axisClearLimit", &ax, &err)) return err;
    auto b = a.get("bound", 1);
    if (b.empty() || !parseLimitBound(b, &bound)) return core::CommandResult::failed("CMD.axisClearLimit", "INVALID_PARAM:bound");
    auto res = core::CoreCommandService::instance().execute(core::AxisClearLimitCmd{ax, bound}, c);
    res.command = "CMD.axisClearLimit";
    return res;
  }},
};

core::CommandResult parseCmd(std::string_view act, std::string_view argsStr, bool hasArgs, const ClientContext& ctx) {
  struct NoArg {
    std::string_view act;
    const char* cmd;
    core::CommandResult (*run)(const ClientContext&);
  };
  static const NoArg NO_ARGS[] = {
    { "stop",   "CMD.stop",   [](const ClientContext& c) { return core::CoreCommandService::instance().execute(core::StopCmd{}, c); } },
    { "hold",   "CMD.hold",   [](const ClientContext& c) { return core::CoreCommandService::instance().execute(core::HoldCmd{}, c); } },
    { "resume", "CMD.resume", [](const ClientContext& c) { return core::CoreCommandService::instance().execute(core::ResumeCmd{}, c); } },
    { "step",   "CMD.step",   [](const ClientContext& c) { return core::CoreCommandService::instance().execute(core::StepCmd{}, c); } },
    { "ping",   "CMD.ping",   [](const ClientContext& c) { return core::CoreCommandService::instance().execute(core::PingCmd{}, c); } },
  };

  for (const auto& r : NO_ARGS) {
    if (act == r.act) {
      if (hasArgs) return core::CommandResult::unrecognized();
      auto res = r.run(ctx);
      res.command = r.cmd;
      return res;
    }
  }

  if (act == "jogRelease" && !hasArgs) {
    auto res = core::CoreCommandService::instance().execute(core::JogReleaseCmd{}, ctx);
    res.command = "CMD.jogRelease";
    return res;
  }

  for (const auto& r : CMD_ROUTES) {
    if (act == r.name) {
      if (!hasArgs || argsStr.empty()) {
        return core::CommandResult::failed(r.canonicalCmd, "MISSING_ARGUMENTS");
      }
      ArgList args;
      if (!tokenizeArgs(argsStr, &args)) return core::CommandResult::unrecognized();
      return r.run(args, ctx);
    }
  }

  return core::CommandResult::unrecognized();
}

// =============================================================================
// 5. NAMESPACE ADAPTERS (STATE, SETTINGS, PROPERTIES, SYS, CLIENT)
// =============================================================================

core::CommandResult parseState(std::string_view act, std::string_view argsStr, const ClientContext&) {
  if (act != "get") return core::CommandResult::unrecognized();

  uint8_t mask = DOMAIN_ALL;
  if (!argsStr.empty() && argsStr != "ALL") {
    mask = DOMAIN_NONE;
    for (char c : argsStr) {
      if (c == ',') continue;
      if (c == 'K') mask |= DOMAIN_KINEMATICS;
      else if (c == 'I') mask |= DOMAIN_INTENT;
      else if (c == 'C') mask |= DOMAIN_CYCLE;
      else if (c == 'P') mask |= DOMAIN_PROPERTIES;
      else return core::CommandResult::unrecognized();
    }
    if (mask == DOMAIN_NONE) mask = DOMAIN_ALL;
  }
  return core::CommandResult::ok("STATE.get", TelemetrySerializer::buildTelemetry(mask));
}

template<typename Func>
core::CommandResult dispatchServiceCall(const char* cmdName, Func func, const char* args) {
  String reply;
  bool ok = func(args, &reply);
  return ok ? core::CommandResult::ok(cmdName, reply)
            : core::CommandResult::failed(cmdName, reply.empty() ? "Operation failed" : reply);
}

core::CommandResult parseSettings(std::string_view act, std::string_view argsStr, const ClientContext& ctx) {
  std::string nullTerminatedArgs(argsStr);
  const char* args = argsStr.empty() ? nullptr : nullTerminatedArgs.c_str();

  if (act == "get")     return dispatchServiceCall("SETTINGS.get", SettingsService::get, args);
  if (act == "schema")  return dispatchServiceCall("SETTINGS.schema", SettingsService::getSchema, args);
  if (act == "presets") return dispatchServiceCall("SETTINGS.presets", [](const char*, String* r) { return SettingsService::listPresets(r); }, args);

  if (act == "set" || act == "reset") {
    if (!ctx.hasPermission(PERM_SETTINGS_WRITE)) {
      return core::CommandResult::failed(act == "set" ? "SETTINGS.set" : "SETTINGS.reset", "PERMISSION_DENIED", 403);
    }
    return (act == "set") ? dispatchServiceCall("SETTINGS.set", SettingsService::set, args)
                          : dispatchServiceCall("SETTINGS.reset", SettingsService::reset, args);
  }

  return core::CommandResult::unrecognized();
}

core::CommandResult parseProperties(std::string_view act, std::string_view argsStr, const ClientContext&) {
  if (act == "modes") {
    return core::CommandResult::ok("PROPERTIES.modes",
      ModeRegistry::serializeDiscoveryJson(SettingsService::getActiveProfile().capabilities, false));
  }
  std::string nullTerminatedArgs(argsStr);
  const char* args = argsStr.empty() ? nullptr : nullTerminatedArgs.c_str();

  if (act == "profiles") return dispatchServiceCall("PROPERTIES.profiles", [](const char*, String* r) { return SettingsService::listPresets(r); }, args);
  if (act == "get")      return dispatchServiceCall("PROPERTIES.get", SettingsService::get, args);

  return core::CommandResult::unrecognized();
}

core::CommandResult parseSys(std::string_view act, std::string_view argsStr, const ClientContext&) {
  if (act != "get") return core::CommandResult::unrecognized();

  if (argsStr == "health") {
#ifdef ARDUINO
    String out = "SYS.uptime=" + String(millis() / 1000) + "\n";
    out += "SYS.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
    out += "SYS.wifi=connected\n";
    return core::CommandResult::ok("SYS.get", out);
#else
    return core::CommandResult::ok("SYS.get", "SYS.uptime=0\nSYS.heap=142 KB\nSYS.wifi=connected\n");
#endif
  }

#ifdef ARDUINO
  String out = "SYS.version=" CONTROLLER_VERSION_TEXT "\n";
  out += "SYS.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
  out += "SYS.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
  out += "SYS.features=spindleSim,ota\n";
  return core::CommandResult::ok("SYS.get", out);
#else
  return core::CommandResult::ok("SYS.get", "SYS.version=1.0.5\nSYS.chip=ESP32-S3\nSYS.flash=16 MB\nSYS.features=spindleSim,ota\n");
#endif
}

core::CommandResult parseClient(std::string_view act, std::string_view argsStr, const ClientContext& ctx) {
  if (act != "hello") return core::CommandResult::unrecognized();

  ArgList args;
  if (!tokenizeArgs(argsStr, &args) || !args.isKv) return core::CommandResult::unrecognized();

  auto type = args.get("type");
  std::string profileType = type.empty() ? "unknown" : std::string(type);

  char ack[128];
  snprintf(ack, sizeof(ack), "session_id=%u,type=%s,controllerVer=1.0.5,perms=0x%04X",
           ctx.sessionId == 255 ? 1 : ctx.sessionId,
           profileType.c_str(),
           ctx.effectivePerms & 0xFFFF);

  return core::CommandResult::ok("CLIENT.hello", ack);
}

} // namespace

// =============================================================================
// 6. MAIN CONTROLLER PARSER & WIRE RESPONSE FORMATTER
// =============================================================================

StandardCommandParser& StandardCommandParser::instance() {
  static StandardCommandParser parser;
  return parser;
}

String StandardCommandParser::formatWireResponse(const core::CommandResult& res) {
  if (res.status == core::CommandStatus::UNRECOGNIZED) return "ERR:UNKNOWN_COMMAND\n";

  if (res.status == core::CommandStatus::FAILED) {
    String out = "ERR:";
    if (res.command) out += res.command;
    if (!res.payload.empty()) {
      out += ":";
      out += res.payload;
    }
    return out + "\n";
  }

  if (res.payload.empty()) {
    String out = "OK:";
    if (res.command) out += res.command;
    return out + "\n";
  }

  if (res.payload.find('\n') != std::string::npos || res.payload.rfind("K.", 0) == 0) {
    return (res.payload.back() == '\n') ? res.payload : res.payload + "\n";
  }

  String out = "OK:";
  if (res.command) out += res.command;
  out += ":";
  out += res.payload;
  return out + "\n";
}

core::CommandResult StandardCommandParser::parseAndDispatch(const char* line, size_t length, const ClientContext& ctx) {
  if (!line || length == 0) return core::CommandResult::unrecognized();

  std::string_view raw(line, length);
  while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n')) raw.remove_suffix(1);
  if (raw.empty() || raw.front() == '?') return core::CommandResult::unrecognized();

  // Single-pass grammar and whitespace validation
  bool inQuotes = false;
  size_t equalsPos = std::string_view::npos;
  size_t equalsCount = 0;

  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }
    if (!inQuotes) {
      if (c == ' ' || c == '\t') return core::CommandResult::unrecognized(); // Zero unquoted whitespace
      if (c == '=') {
        equalsCount++;
        if (equalsPos == std::string_view::npos) equalsPos = i;
      }
    }
  }

  if (inQuotes || equalsCount > 1) return core::CommandResult::unrecognized();

  bool hasEquals = (equalsPos != std::string_view::npos);
  std::string_view header = hasEquals ? raw.substr(0, equalsPos) : raw;
  std::string_view argsStr = hasEquals ? raw.substr(equalsPos + 1) : "";
  if (hasEquals && argsStr.empty()) return core::CommandResult::unrecognized(); // Trailing '=' error

  size_t dotPos = header.find('.');
  if (dotPos == std::string_view::npos || dotPos == 0 ||
      dotPos == header.size() - 1 || header.find('.', dotPos + 1) != std::string_view::npos) {
    return core::CommandResult::unrecognized();
  }

  std::string_view ns = header.substr(0, dotPos);
  std::string_view act = header.substr(dotPos + 1);
  if (!isValidIdentifier(ns) || !isValidIdentifier(act)) return core::CommandResult::unrecognized();

  if (ns == "CMD")        return parseCmd(act, argsStr, hasEquals, ctx);
  if (ns == "STATE")      return parseState(act, argsStr, ctx);
  if (ns == "SETTINGS")   return parseSettings(act, argsStr, ctx);
  if (ns == "PROPERTIES") return parseProperties(act, argsStr, ctx);
  if (ns == "SYS")        return parseSys(act, argsStr, ctx);
  if (ns == "CLIENT")     return parseClient(act, argsStr, ctx);

  return core::CommandResult::unrecognized();
}

} // namespace whatels::core_client
