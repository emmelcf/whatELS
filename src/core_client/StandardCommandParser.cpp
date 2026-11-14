#include "StandardCommandParser.h"
#include "core/Config.h"
#include "core/CoreCommandService.h"
#include "core/TelemetrySerializer.h"
#include "core/SettingsService.h"
#include "core/PresetService.h"
#include "motion/kernel/MotionEngine.h"
#include "modes/ModeRegistry.h"
#include "core/ISystemInfoHAL.h"

#include <array>
#include <cctype>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <string_view>

namespace whatels::core_client {

namespace {

// =============================================================================
// 1. UNIFIED IN-PLACE NUMBER & GRAMMAR PARSERS (ZERO HEAP ALLOCATION)
// =============================================================================

[[nodiscard]] bool isValidIdentifier(std::string_view s) {
  if (s.empty()) return false;
  for (char c : s) {
    if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
  }
  return true;
}

[[nodiscard]] bool parseInt32(std::string_view s, int32_t* outValue) {
  if (s.empty() || !outValue) return false;
  if (s.front() == '+') {
    s.remove_prefix(1);
    if (s.empty() || s.front() == '-') return false;
  }
  int32_t val = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec != std::errc{} || ptr != s.data() + s.size()) return false;
  *outValue = val;
  return true;
}

[[nodiscard]] bool parseFloat(std::string_view s, float* outValue) {
  if (s.empty() || !outValue) return false;
  if (s.front() == '+') {
    s.remove_prefix(1);
    if (s.empty() || s.front() == '-') return false;
  }
  // Reject leading whitespace or non-numeric prefixes that from_chars wouldn't match anyway,
  // and reject inf/nan strings by requiring digit or leading '-' followed by digit.
  char first = s.front();
  if (!((first >= '0' && first <= '9') || (first == '-' && s.size() > 1 && s[1] >= '0' && s[1] <= '9'))) {
    return false;
  }
  float val = 0.0f;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), val);
  if (ec != std::errc{} || ptr != s.data() + s.size() || !std::isfinite(val)) return false;
  *outValue = val;
  return true;
}

[[nodiscard]] bool parseDeciMicrons(std::string_view s, int32_t* outValue) {
  return parseInt32(s, outValue);
}

[[nodiscard]] bool parseBool(std::string_view s, bool* outValue) {
  if (!outValue) return false;
  if (s == "1") { *outValue = true;  return true; }
  if (s == "0") { *outValue = false; return true; }
  return false;
}

[[nodiscard]] bool parseAxis(std::string_view s, AxisId* outAxis) {
  if (!outAxis) return false;
  if (s == "Z") { *outAxis = AXIS_Z; return true; }
  if (s == "X") { *outAxis = AXIS_X; return true; }
  return false;
}

[[nodiscard]] bool parseAxisOrCompound(std::string_view s, AxisId* outAxis, bool* outIsDual) {
  if (!outAxis || !outIsDual) return false;
  if (s == "ZX") {
    *outAxis = AXIS_Z;
    *outIsDual = true;
    return true;
  }
  *outIsDual = false;
  return parseAxis(s, outAxis);
}

// Shared axis-aware directional token mapper:
// -1: Z toward chuck (LEFT/CHUCK), X into centerline (CENTER/IN/INFEED/UP)
// +1: Z toward tailstock (RIGHT/TAIL), X retract (RETRACT/OUT/DOWN)
[[nodiscard]] bool parseAxisDirectionToken(AxisId axis, std::string_view s, int8_t* outSign) {
  if (!outSign) return false;
  if (axis == AXIS_Z) {
    if (s == "LEFT" || s == "CHUCK")  { *outSign = -1; return true; }
    if (s == "RIGHT" || s == "TAIL")  { *outSign =  1; return true; }
  } else if (axis == AXIS_X) {
    if (s == "CENTER" || s == "IN" || s == "INFEED" || s == "UP") { *outSign = -1; return true; }
    if (s == "RETRACT" || s == "OUT" || s == "DOWN")               { *outSign =  1; return true; }
  }
  return false;
}

[[nodiscard]] bool parsePolarity(AxisId axis, std::string_view s, int8_t* outPolarity) {
  if (!outPolarity) return false;
  if (s == "-" || s == "-1") { *outPolarity = -1; return true; }
  if (s == "+" || s == "+1") { *outPolarity =  1; return true; }
  return parseAxisDirectionToken(axis, s, outPolarity);
}

using LimitBoundT = decltype(core::AxisLimitCmd::limitBound);

[[nodiscard]] bool parseLimitBound(AxisId axis, std::string_view s, LimitBoundT* outBound) {
  if (!outBound) return false;
  if (s == "L" || s == "MIN") { *outBound = static_cast<LimitBoundT>(1); return true; }
  if (s == "R" || s == "MAX") { *outBound = static_cast<LimitBoundT>(2); return true; }
  int8_t sign = 0;
  if (!parseAxisDirectionToken(axis, s, &sign)) return false;
  *outBound = static_cast<LimitBoundT>(sign < 0 ? 1 : 2);
  return true;
}

// =============================================================================
// 2. STACK-ONLY ARGUMENT TOKENIZER & STRING HELPERS (ZERO DYNAMIC ALLOCATION)
// =============================================================================

struct ArgList {
  struct Entry {
    std::string_view key;
    std::string_view val;
  };
  std::array<Entry, 16> items{};
  size_t count = 0;
  bool isKv = false;

  [[nodiscard]] std::string_view get(std::string_view key, size_t pos = SIZE_MAX) const {
    if (isKv) {
      for (size_t i = 0; i < count; ++i) {
        if (items[i].key == key) return items[i].val;
      }
      return {};
    }
    return (pos < count) ? items[pos].val : std::string_view{};
  }

  [[nodiscard]] bool has(std::string_view key) const {
    for (size_t i = 0; i < count; ++i) {
      if (items[i].key == key) return true;
    }
    return false;
  }
};

[[nodiscard]] std::string_view unquote(std::string_view s) {
  if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
    return s.substr(1, s.size() - 2);
  }
  return s;
}

[[nodiscard]] bool tokenizeArgs(std::string_view s, ArgList* out) {
  if (!out) return false;
  if (s.empty()) return true;

  size_t start = 0;
  bool inQuotes = false;

  for (size_t i = 0; i <= s.size(); ++i) {
    char c = (i < s.size()) ? s[i] : ',';

    if (c == '"') {
      inQuotes = !inQuotes;
      continue;
    }

    if (!inQuotes && i < s.size() && (c == ' ' || c == '\t' || c == '=')) {
      return false; // Reject unquoted whitespace and '=' inside KV/positional args
    }

    if (c == ',' && !inQuotes) {
      if (i == start) return false; // Leading, double (',,'), or trailing comma
      if (out->count >= out->items.size()) return false;

      std::string_view token = s.substr(start, i - start);
      start = i + 1;

      size_t colon = token.find(':');
      if (colon != std::string_view::npos) {
        if (colon == 0) return false;
        if (out->count > 0 && !out->isKv) return false;
        out->isKv = true;
        std::string_view key = token.substr(0, colon);
        if (!isValidIdentifier(key)) return false;
        if (out->has(key)) return false;
        out->items[out->count++] = { key, unquote(token.substr(colon + 1)) };
      } else {
        if (out->isKv) return false;
        out->items[out->count++] = { {}, unquote(token) };
      }
    }
  }

  return !inQuotes && (start > s.size());
}

template<typename Fn>
auto withNullTerminated(std::string_view sv, Fn&& fn) {
  if (sv.empty()) {
    return fn(static_cast<const char*>(nullptr));
  }
  char stackBuf[256];
  if (sv.size() < sizeof(stackBuf)) {
    std::memcpy(stackBuf, sv.data(), sv.size());
    stackBuf[sv.size()] = '\0';
    return fn(static_cast<const char*>(stackBuf));
  }
  std::string heapBuf(sv);
  return fn(heapBuf.c_str());
}

[[nodiscard]] bool isMotionOrHoldActive() {
  auto& me = MotionEngine::instance();
  return me.isAnyAxisMoving() || me.isFeedHold(AXIS_Z) || me.isFeedHold(AXIS_X);
}

// =============================================================================
// 3. PARAMETER EXTRACTOR, VALIDATION & COMMAND EXECUTION HELPERS
// =============================================================================

[[nodiscard]] bool validateAllowedKeys(const ArgList& args, const char* cmdName,
                                       std::initializer_list<std::string_view> allowedKeys,
                                       size_t maxPositional, core::CommandResult* outErr) {
  if (args.isKv) {
    for (size_t i = 0; i < args.count; ++i) {
      bool found = false;
      for (auto allowed : allowedKeys) {
        if (args.items[i].key == allowed) {
          found = true;
          break;
        }
      }
      if (!found) {
        char errBuf[64];
        std::snprintf(errBuf, sizeof(errBuf), "INVALID_PARAM:%.*s",
                      static_cast<int>(args.items[i].key.size()), args.items[i].key.data());
        *outErr = core::CommandResult::failed(cmdName, errBuf);
        return false;
      }
    }
  } else if (args.count > maxPositional) {
    *outErr = core::CommandResult::failed(cmdName, "INVALID_PARAM");
    return false;
  }
  return true;
}

[[nodiscard]] bool extractAxis(const ArgList& args, const char* cmdName, AxisId* outAxis, core::CommandResult* outErr) {
  auto ax = args.get("axis", 0);
  if (ax.empty() || !parseAxis(ax, outAxis)) {
    *outErr = core::CommandResult::failed(cmdName, "INVALID_PARAM:axis");
    return false;
  }
  return true;
}

template<typename CmdT>
[[nodiscard]] core::CommandResult execCmd(const char* canonicalCmd, const CmdT& cmd, const ClientContext& ctx) {
  auto res = core::CoreCommandService::instance().execute(cmd, ctx);
  res.command = canonicalCmd;
  return res;
}

struct RouteEntry {
  enum class ArgPolicy : uint8_t { NONE, REQUIRED, OPTIONAL };
  using HandlerFn = core::CommandResult (*)(const RouteEntry&, std::string_view, bool, const ClientContext&, core::ISystemInfoHAL*);

  std::string_view ns;
  std::string_view act;
  const char* canonicalCmd;
  ArgPolicy argPolicy;
  uint16_t requiredPerm;
  HandlerFn handler;
};

template<typename CmdT>
core::CommandResult handleNoArgCmd(const RouteEntry& route, std::string_view, bool, const ClientContext& ctx, core::ISystemInfoHAL*) {
  return execCmd(route.canonicalCmd, CmdT{}, ctx);
}

template<core::CommandResult (*ParseFn)(const ArgList&, const ClientContext&)>
core::CommandResult handleArgCmd(const RouteEntry&, std::string_view argsStr, bool, const ClientContext& ctx, core::ISystemInfoHAL*) {
  ArgList args;
  if (!tokenizeArgs(argsStr, &args)) return core::CommandResult::unrecognized();
  return ParseFn(args, ctx);
}

// =============================================================================
// 4. CMD.* TYPED PARSERS
// =============================================================================

core::CommandResult parseStartFeed(const ArgList& args, const ClientContext& ctx) {
  if (!args.isKv) return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM");

  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.startFeed", {"axis", "feed", "dir", "sync", "dist", "xdir", "taper"}, 0, &err)) {
    return err;
  }

  core::StartFeedCmd cmd;
  auto ax = args.get("axis");
  if (ax.empty() || !parseAxisOrCompound(ax, &cmd.axis, &cmd.isDualAxis)) {
    return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:axis");
  }

  auto feedStr = args.get("feed");
  if (feedStr.empty()) return core::CommandResult::failed("CMD.startFeed", "MISSING_PARAM:feed");
  if (!parseDeciMicrons(feedStr, &cmd.feedRate) || cmd.feedRate <= 0) {
    return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:feed");
  }

  auto dirStr = args.get("dir");
  if (dirStr.empty() || !parsePolarity(cmd.axis, dirStr, &cmd.direction)) {
    return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:dir");
  }

  if (cmd.isDualAxis) {
    if (!args.has("xdir")) return core::CommandResult::failed("CMD.startFeed", "MISSING_PARAM:xdir");
    auto xdirStr = args.get("xdir");
    if (xdirStr.empty() || !parsePolarity(AXIS_X, xdirStr, &cmd.xDirection)) {
      return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:xdir");
    }

    if (!args.has("taper")) return core::CommandResult::failed("CMD.startFeed", "MISSING_PARAM:taper");
    auto taperStr = args.get("taper");
    if (taperStr.empty() || !parseDeciMicrons(taperStr, &cmd.taperRatioDu) || cmd.taperRatioDu <= 0) {
      return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:taper");
    }
  } else {
    if (args.has("xdir"))  return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:xdir");
    if (args.has("taper")) return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:taper");
  }

  if (args.has("sync")) {
    auto syncStr = args.get("sync");
    if (syncStr.empty() || !parseBool(syncStr, &cmd.syncSpindle)) {
      return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:sync");
    }
  } else {
    cmd.syncSpindle = true;
  }

  if (args.has("dist")) {
    auto distStr = args.get("dist");
    if (distStr.empty() || !parseDeciMicrons(distStr, &cmd.targetDistDu)) {
      return core::CommandResult::failed("CMD.startFeed", "INVALID_PARAM:dist");
    }
  }

  return execCmd("CMD.startFeed", cmd, ctx);
}

core::CommandResult parseStartCycle(const ArgList& args, const ClientContext&) {
  if (!args.isKv) return core::CommandResult::unrecognized();
  auto mode = args.get("mode");
  if (mode.empty()) return core::CommandResult::failed("CMD.startCycle", "MISSING_PARAM:mode");
  return core::CommandResult::failed("CMD.startCycle", "UNAVAILABLE");
}

core::CommandResult parseJogStep(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.jogStep", {"axis", "dist"}, 2, &err)) return err;

  AxisId ax;
  int32_t dist = 0;
  if (!extractAxis(args, "CMD.jogStep", &ax, &err)) return err;

  auto distStr = args.get("dist", 1);
  if (distStr.empty() || !parseDeciMicrons(distStr, &dist)) {
    return core::CommandResult::failed("CMD.jogStep", "INVALID_PARAM:dist");
  }
  return execCmd("CMD.jogStep", core::JogStepCmd{ax, dist}, ctx);
}

core::CommandResult parseJogHold(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.jogHold", {"axis", "dir"}, 2, &err)) return err;

  AxisId ax;
  int8_t dir = 0;
  if (!extractAxis(args, "CMD.jogHold", &ax, &err)) return err;

  auto dirStr = args.get("dir", 1);
  if (dirStr.empty() || !parsePolarity(ax, dirStr, &dir)) {
    return core::CommandResult::failed("CMD.jogHold", "INVALID_PARAM:dir");
  }
  return execCmd("CMD.jogHold", core::JogHoldCmd{ax, dir}, ctx);
}

core::CommandResult parseJogRelease(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.jogRelease", {"axis"}, 1, &err)) return err;

  core::JogReleaseCmd cmd;
  if (args.count > 0) {
    if (!extractAxis(args, "CMD.jogRelease", &cmd.axis, &err)) return err;
    cmd.hasAxis = true;
  }
  return execCmd("CMD.jogRelease", cmd, ctx);
}

core::CommandResult parseAxisZero(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.axisZero", {"axis"}, 1, &err)) return err;

  AxisId ax;
  if (!extractAxis(args, "CMD.axisZero", &ax, &err)) return err;
  return execCmd("CMD.axisZero", core::AxisZeroCmd{ax}, ctx);
}

core::CommandResult parseAxisPosDu(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.axisPosDu", {"axis", "pos"}, 2, &err)) return err;

  AxisId ax;
  int32_t pos = 0;
  if (!extractAxis(args, "CMD.axisPosDu", &ax, &err)) return err;

  auto posStr = args.get("pos", 1);
  if (posStr.empty() || !parseDeciMicrons(posStr, &pos)) {
    return core::CommandResult::failed("CMD.axisPosDu", "INVALID_PARAM:pos");
  }
  return execCmd("CMD.axisPosDu", core::AxisPosCmd{ax, pos}, ctx);
}

core::CommandResult parseAxisLimitDu(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.axisLimitDu", {"axis", "bound", "coord"}, 3, &err)) return err;

  AxisId ax;
  LimitBoundT bound{};
  int32_t coord = 0;
  if (!extractAxis(args, "CMD.axisLimitDu", &ax, &err)) return err;

  auto boundStr = args.get("bound", 1);
  auto coordStr = args.get("coord", 2);
  if (boundStr.empty() || !parseLimitBound(ax, boundStr, &bound)) {
    return core::CommandResult::failed("CMD.axisLimitDu", "INVALID_PARAM:bound");
  }
  if (coordStr.empty() || !parseDeciMicrons(coordStr, &coord)) {
    return core::CommandResult::failed("CMD.axisLimitDu", "INVALID_PARAM:coord");
  }
  return execCmd("CMD.axisLimitDu", core::AxisLimitCmd{ax, bound, coord}, ctx);
}

core::CommandResult parseAxisClearLimit(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.axisClearLimit", {"axis", "bound"}, 2, &err)) return err;

  AxisId ax;
  LimitBoundT bound{};
  if (!extractAxis(args, "CMD.axisClearLimit", &ax, &err)) return err;

  auto boundStr = args.get("bound", 1);
  if (boundStr.empty() || !parseLimitBound(ax, boundStr, &bound)) {
    return core::CommandResult::failed("CMD.axisClearLimit", "INVALID_PARAM:bound");
  }
  return execCmd("CMD.axisClearLimit", core::AxisClearLimitCmd{ax, bound}, ctx);
}

core::CommandResult parseSimRpm(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.simRpm", {"rpm"}, 1, &err)) return err;

  auto rpmStr = args.get("rpm", 0);
  float rpmVal = 0.0f;
  if (rpmStr.empty() || !parseFloat(rpmStr, &rpmVal) || rpmVal < 0.0f) {
    return core::CommandResult::failed("CMD.simRpm", "INVALID_PARAM:rpm");
  }
  return execCmd("CMD.simRpm", core::SimRpmCmd{rpmVal}, ctx);
}

core::CommandResult parseSimEncoder(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.simEncoder", {"active"}, 1, &err)) return err;

  auto activeStr = args.get("active", 0);
  bool active = false;
  if (activeStr.empty() || !parseBool(activeStr, &active)) {
    return core::CommandResult::failed("CMD.simEncoder", "INVALID_PARAM:active");
  }
  return execCmd("CMD.simEncoder", core::SimEncoderCmd{true, active}, ctx);
}

core::CommandResult parseSimPulses(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.simPulses", {"pulses"}, 1, &err)) return err;

  auto pulsesStr = args.get("pulses", 0);
  int32_t pulses = 0;
  if (pulsesStr.empty() || !parseInt32(pulsesStr, &pulses) || pulses == 0) {
    return core::CommandResult::failed("CMD.simPulses", "INVALID_PARAM:pulses");
  }
  return execCmd("CMD.simPulses", core::SimPulsesCmd{pulses}, ctx);
}

core::CommandResult parseFeedNudge(const ArgList& args, const ClientContext& ctx) {
  core::CommandResult err;
  if (!validateAllowedKeys(args, "CMD.feedNudge", {"delta"}, 1, &err)) return err;

  auto deltaStr = args.get("delta", 0);
  int32_t deltaVal = 0;
  if (deltaStr.empty() || !parseInt32(deltaStr, &deltaVal) || (deltaVal != 1 && deltaVal != -1)) {
    return core::CommandResult::failed("CMD.feedNudge", "INVALID_PARAM:delta");
  }
  return execCmd("CMD.feedNudge", core::FeedNudgeCmd{static_cast<int8_t>(deltaVal)}, ctx);
}

// =============================================================================
// 5. NON-CMD NAMESPACE HANDLERS (STATE, SETTINGS, PROPERTIES, SYS, CLIENT)
// =============================================================================

template<typename Func>
core::CommandResult dispatchServiceCall(const char* cmdName, Func func, std::string_view argsStr) {
  return withNullTerminated(argsStr, [&](const char* args) {
    String reply;
    bool ok = func(args, &reply);
    return ok ? core::CommandResult::ok(cmdName, reply)
              : core::CommandResult::failed(cmdName, reply.length() == 0 ? "Operation failed" : reply);
  });
}

bool listPresetsAdapter(const char*, String* r) {
  return SettingsService::listPresets(r);
}

core::CommandResult handleStateGet(const RouteEntry&, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  uint8_t mask = DOMAIN_ALL;
  if (!argsStr.empty() && argsStr != "ALL") {
    mask = DOMAIN_NONE;
    size_t start = 0;
    for (size_t i = 0; i <= argsStr.size(); ++i) {
      if (i == argsStr.size() || argsStr[i] == ',') {
        if (i == start) return core::CommandResult::unrecognized();
        std::string_view tok = argsStr.substr(start, i - start);
        start = i + 1;
        if      (tok == "K")   mask |= DOMAIN_KINEMATICS;
        else if (tok == "I")   mask |= DOMAIN_INTENT;
        else if (tok == "C")   mask |= DOMAIN_CYCLE;
        else if (tok == "P")   mask |= DOMAIN_PROPERTIES;
        else if (tok == "ALL") mask |= DOMAIN_ALL;
        else return core::CommandResult::unrecognized();
      }
    }
  }
  return core::CommandResult::ok("STATE.get", TelemetrySerializer::buildTelemetry(mask));
}

core::CommandResult handleSettingsGet(const RouteEntry& r, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  return dispatchServiceCall(r.canonicalCmd, SettingsService::get, argsStr);
}

core::CommandResult handleSettingsSchema(const RouteEntry& r, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  return dispatchServiceCall(r.canonicalCmd, SettingsService::getSchema, argsStr);
}

core::CommandResult handleSettingsPresets(const RouteEntry& r, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  return dispatchServiceCall(r.canonicalCmd, listPresetsAdapter, argsStr);
}

core::CommandResult handleSettingsReset(const RouteEntry& r, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  return dispatchServiceCall(r.canonicalCmd, SettingsService::reset, argsStr);
}

core::CommandResult handleSettingsSet(const RouteEntry&, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  constexpr std::string_view kProfilePrefix = "profile:";
  if (argsStr.size() >= kProfilePrefix.size() &&
      argsStr.substr(0, kProfilePrefix.size()) == kProfilePrefix) {
    if (isMotionOrHoldActive()) {
      return core::CommandResult::failed("SETTINGS.set", "MOTION_LOCKED");
    }
    std::string_view id = argsStr.substr(kProfilePrefix.size());
    if (id.empty()) {
      return core::CommandResult::failed("SETTINGS.set", "INVALID_PARAM:profile");
    }
    return withNullTerminated(id, [&](const char* idStr) {
      String reply;
      if (SettingsService::setPreset(idStr, &reply)) {
        return core::CommandResult::ok("SETTINGS.set", String("profile:") + idStr);
      }
      std::string_view errSv(reply.c_str(), reply.length());
      while (!errSv.empty() && (errSv.back() == '\n' || errSv.back() == '\r')) {
        errSv.remove_suffix(1);
      }
      if (errSv.empty()) {
        return core::CommandResult::failed("SETTINGS.set", "INVALID_PARAM:profile");
      }
      std::string errStr(errSv);
      return core::CommandResult::failed("SETTINGS.set", errStr.c_str());
    });
  }
  return dispatchServiceCall("SETTINGS.set", SettingsService::set, argsStr);
}

core::CommandResult handleCatalogSave(const RouteEntry&, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  if (isMotionOrHoldActive()) {
    return core::CommandResult::failed("SETTINGS.catalogSave", "MOTION_LOCKED");
  }
  return dispatchServiceCall("SETTINGS.catalogSave", PresetService::save, argsStr);
}

core::CommandResult handleCatalogDelete(const RouteEntry&, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL*) {
  if (isMotionOrHoldActive()) {
    return core::CommandResult::failed("SETTINGS.catalogDelete", "MOTION_LOCKED");
  }
  return dispatchServiceCall("SETTINGS.catalogDelete", PresetService::remove, argsStr);
}

core::CommandResult handlePropertiesModes(const RouteEntry&, std::string_view argsStr, bool hasEquals, const ClientContext&, core::ISystemInfoHAL*) {
  bool includeAll = false;
  if (hasEquals) {
    if (argsStr == "all") includeAll = true;
    else return core::CommandResult::unrecognized();
  }
  return core::CommandResult::ok("PROPERTIES.modes",
      ModeRegistry::serializeDiscoveryJson(SettingsService::getActiveProfile().capabilities, includeAll));
}

core::CommandResult handlePropertiesCatalog(const RouteEntry&, std::string_view argsStr, bool hasEquals, const ClientContext&, core::ISystemInfoHAL*) {
  if (!hasEquals) {
    String reply;
    return PresetService::discover(nullptr, &reply)
        ? core::CommandResult::ok("PROPERTIES.catalog", reply)
        : core::CommandResult::failed("PROPERTIES.catalog", reply.length() == 0 ? "Operation failed" : reply);
  }
  constexpr std::string_view kPathPrefix = "path=";
  if (argsStr.size() > kPathPrefix.size() &&
      argsStr.substr(0, kPathPrefix.size()) == kPathPrefix &&
      argsStr.find('&') == std::string_view::npos &&
      argsStr.find(' ') == std::string_view::npos) {
    std::string_view pathVal = argsStr.substr(kPathPrefix.size());
    if (pathVal.find('/') == std::string_view::npos) {
      return dispatchServiceCall("PROPERTIES.catalog", PresetService::discover, pathVal);
    }
  }
  return dispatchServiceCall("PROPERTIES.catalog", PresetService::get, argsStr);
}

core::CommandResult handleSysGet(const RouteEntry&, std::string_view argsStr, bool, const ClientContext&, core::ISystemInfoHAL* hal) {
  if (!argsStr.empty() && argsStr != "info" && argsStr != "health") {
    return core::CommandResult::failed("SYS.get", "INVALID_PARAM");
  }
  if (!hal) return core::CommandResult::failed("SYS.get", "UNAVAILABLE");

  if (argsStr == "health") {
    char buf[128];
    int n = std::snprintf(buf, sizeof(buf), "SYS.uptime=%lu\nSYS.heap=%lu KB\nSYS.wifi=%s\n",
                          static_cast<unsigned long>(hal->getUptimeSeconds()),
                          static_cast<unsigned long>(hal->getFreeHeapBytes() / 1024),
                          hal->getWifiStatus().c_str());
    if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
      return core::CommandResult::failed("SYS.get", "RESPONSE_TRUNCATED");
    }
    return core::CommandResult::ok("SYS.get", buf);
  }

  char buf[256];
  int n = std::snprintf(buf, sizeof(buf), "SYS.version=%s\nSYS.chip=%s rev %lu\nSYS.flash=%lu MB\nSYS.features=%s\n",
                        hal->getVersion(),
                        hal->getChipModel(),
                        static_cast<unsigned long>(hal->getChipRevision()),
                        static_cast<unsigned long>(hal->getFlashSizeBytes() / 1024 / 1024),
                        hal->getFeatures());
  if (n < 0 || static_cast<size_t>(n) >= sizeof(buf)) {
    return core::CommandResult::failed("SYS.get", "RESPONSE_TRUNCATED");
  }
  return core::CommandResult::ok("SYS.get", buf);
}

core::CommandResult handleClientHello(const RouteEntry&, std::string_view argsStr, bool, const ClientContext& ctx, core::ISystemInfoHAL*) {
  ArgList args;
  if (!tokenizeArgs(argsStr, &args) || !args.isKv) return core::CommandResult::unrecognized();

  core::CommandResult err;
  if (!validateAllowedKeys(args, "CLIENT.hello", {"type", "ver", "label"}, 0, &err)) {
    return err;
  }

  auto type = args.get("type");
  if (type.empty()) return core::CommandResult::failed("CLIENT.hello", "MISSING_PARAM:type");

  char ack[128];
  int n = std::snprintf(ack, sizeof(ack),
                        "session_id=%u,type=%.*s,controllerVer=" CONTROLLER_VERSION_TEXT ",perms=0x%04X",
                        ctx.sessionId == 255 ? 1 : ctx.sessionId,
                        static_cast<int>(type.size()), type.data(),
                        ctx.effectivePerms & 0xFFFF);
  if (n < 0 || static_cast<size_t>(n) >= sizeof(ack)) {
    return core::CommandResult::failed("CLIENT.hello", "INVALID_PARAM:type");
  }
  return core::CommandResult::ok("CLIENT.hello", ack);
}

// =============================================================================
// 6. UNIFIED CONSTEXPR ROUTE TABLE
// =============================================================================

static constexpr RouteEntry ROUTES[] = {
  { "CMD",        "stop",           "CMD.stop",           RouteEntry::ArgPolicy::NONE,     0,                   handleNoArgCmd<core::StopCmd> },
  { "CMD",        "hold",           "CMD.hold",           RouteEntry::ArgPolicy::NONE,     0,                   handleNoArgCmd<core::HoldCmd> },
  { "CMD",        "resume",         "CMD.resume",         RouteEntry::ArgPolicy::NONE,     0,                   handleNoArgCmd<core::ResumeCmd> },
  { "CMD",        "step",           "CMD.step",           RouteEntry::ArgPolicy::NONE,     0,                   handleNoArgCmd<core::StepCmd> },
  { "CMD",        "ping",           "CMD.ping",           RouteEntry::ArgPolicy::NONE,     0,                   handleNoArgCmd<core::PingCmd> },
  { "CMD",        "startFeed",      "CMD.startFeed",      RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseStartFeed> },
  { "CMD",        "startCycle",     "CMD.startCycle",     RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseStartCycle> },
  { "CMD",        "jogStep",        "CMD.jogStep",        RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseJogStep> },
  { "CMD",        "jogHold",        "CMD.jogHold",        RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseJogHold> },
  { "CMD",        "jogRelease",     "CMD.jogRelease",     RouteEntry::ArgPolicy::OPTIONAL, 0,                   handleArgCmd<parseJogRelease> },
  { "CMD",        "axisZero",       "CMD.axisZero",       RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseAxisZero> },
  { "CMD",        "axisPosDu",      "CMD.axisPosDu",      RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseAxisPosDu> },
  { "CMD",        "axisLimitDu",    "CMD.axisLimitDu",    RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseAxisLimitDu> },
  { "CMD",        "axisClearLimit", "CMD.axisClearLimit", RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseAxisClearLimit> },
  { "CMD",        "simRpm",         "CMD.simRpm",         RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseSimRpm> },
  { "CMD",        "simEncoder",     "CMD.simEncoder",     RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseSimEncoder> },
  { "CMD",        "simPulses",      "CMD.simPulses",      RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseSimPulses> },
  { "CMD",        "feedNudge",      "CMD.feedNudge",      RouteEntry::ArgPolicy::REQUIRED, 0,                   handleArgCmd<parseFeedNudge> },

  { "STATE",      "get",            "STATE.get",          RouteEntry::ArgPolicy::OPTIONAL, 0,                   handleStateGet },

  { "SETTINGS",   "get",            "SETTINGS.get",       RouteEntry::ArgPolicy::OPTIONAL, 0,                   handleSettingsGet },
  { "SETTINGS",   "schema",         "SETTINGS.schema",    RouteEntry::ArgPolicy::OPTIONAL, 0,                   handleSettingsSchema },
  { "SETTINGS",   "presets",        "SETTINGS.presets",   RouteEntry::ArgPolicy::NONE,     0,                   handleSettingsPresets },
  { "SETTINGS",   "set",            "SETTINGS.set",       RouteEntry::ArgPolicy::REQUIRED, PERM_SETTINGS_WRITE, handleSettingsSet },
  { "SETTINGS",   "reset",          "SETTINGS.reset",     RouteEntry::ArgPolicy::REQUIRED, PERM_SETTINGS_WRITE, handleSettingsReset },
  { "SETTINGS",   "catalogSave",    "SETTINGS.catalogSave",   RouteEntry::ArgPolicy::REQUIRED, PERM_SETTINGS_WRITE, handleCatalogSave },
  { "SETTINGS",   "catalogDelete",  "SETTINGS.catalogDelete", RouteEntry::ArgPolicy::REQUIRED, PERM_SETTINGS_WRITE, handleCatalogDelete },

  { "PROPERTIES", "modes",          "PROPERTIES.modes",   RouteEntry::ArgPolicy::OPTIONAL, 0,                   handlePropertiesModes },
  { "PROPERTIES", "profiles",       "PROPERTIES.profiles",RouteEntry::ArgPolicy::NONE,     0,                   handleSettingsPresets },
  { "PROPERTIES", "get",            "PROPERTIES.get",     RouteEntry::ArgPolicy::REQUIRED, 0,                   handleSettingsGet },
  { "PROPERTIES", "catalog",        "PROPERTIES.catalog", RouteEntry::ArgPolicy::OPTIONAL, 0,                   handlePropertiesCatalog },

  { "SYS",        "get",            "SYS.get",            RouteEntry::ArgPolicy::OPTIONAL, PERM_READ_TELEMETRY, handleSysGet },

  { "CLIENT",     "hello",          "CLIENT.hello",       RouteEntry::ArgPolicy::REQUIRED, 0,                   handleClientHello },
};

} // namespace

// =============================================================================
// 7. MAIN CONTROLLER PARSER & WIRE RESPONSE FORMATTER
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
    if (res.payload.length() > 0) {
      out += ":";
      out += res.payload;
    }
    return out + "\n";
  }

  if (res.payload.length() == 0) {
    String out = "OK:";
    if (res.command) out += res.command;
    return out + "\n";
  }

  const char* p = res.payload.c_str();
  size_t len = res.payload.length();
  if (std::strchr(p, '\n') != nullptr || (len >= 2 && p[0] == 'K' && p[1] == '.')) {
    return (p[len - 1] == '\n') ? res.payload : res.payload + "\n";
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

  // Phase 1 (Envelope): Extract <NS>.<act>, hasEquals, and opaque argsStr up to first '='.
  size_t equalsPos = raw.find('=');
  bool hasEquals = (equalsPos != std::string_view::npos);
  std::string_view header = hasEquals ? raw.substr(0, equalsPos) : raw;
  std::string_view argsStr = hasEquals ? raw.substr(equalsPos + 1) : std::string_view{};

  if (hasEquals && argsStr.empty()) return core::CommandResult::unrecognized();

  for (char c : header) {
    if (c == ' ' || c == '\t' || c == '"') return core::CommandResult::unrecognized();
  }

  size_t dotPos = header.find('.');
  if (dotPos == std::string_view::npos || dotPos == 0 ||
      dotPos == header.size() - 1 || header.find('.', dotPos + 1) != std::string_view::npos) {
    return core::CommandResult::unrecognized();
  }

  std::string_view ns = header.substr(0, dotPos);
  std::string_view act = header.substr(dotPos + 1);
  if (!isValidIdentifier(ns) || !isValidIdentifier(act)) return core::CommandResult::unrecognized();

  // Phase 2 (Unified Route Table Dispatch)
  for (const auto& route : ROUTES) {
    if (route.ns == ns && route.act == act) {
      if (route.argPolicy == RouteEntry::ArgPolicy::NONE && hasEquals) {
        return core::CommandResult::unrecognized();
      }
      if (route.argPolicy == RouteEntry::ArgPolicy::REQUIRED && !hasEquals) {
        return core::CommandResult::failed(route.canonicalCmd, "MISSING_ARGUMENTS");
      }
      if (route.requiredPerm != 0 && !ctx.hasPermission(route.requiredPerm)) {
        return core::CommandResult::failed(route.canonicalCmd, "PERMISSION_DENIED", 403);
      }
      return route.handler(route, argsStr, hasEquals, ctx, _systemInfoHal);
    }
  }

  return core::CommandResult::unrecognized();
}

} // namespace whatels::core_client
