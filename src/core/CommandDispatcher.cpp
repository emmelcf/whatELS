#include "core/CommandDispatcher.h"
#include "Config.h"
#include "MachineModel.h"
#include "core/TelemetrySerializer.h"
#include "SettingsService.h"
#include "PresetService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include "motion/MotionConversions.h"
#include "motion/kernel/MotionEngine.h"
#include "core/OperatorIntent.h"

namespace nanoels {
namespace core {

// =============================================================================
// FORWARD DECLARATIONS OF CLEAN DEDICATED COMMAND HANDLERS
// =============================================================================

static CommandResult handleStartFeed(const char* args);
static CommandResult handleStop(const char* args);
static CommandResult handleJogHold(const char* args);
static CommandResult handleJogRelease(const char* args);
static CommandResult handleJogStep(const char* args);
static CommandResult handleJogStepDu(const char* args);
static CommandResult handleFeedNudge(const char* args);
static CommandResult handleFeedStepCycle(const char* args);
static CommandResult handleFeedStepDu(const char* args);
static CommandResult handleAxisZero(const char* args);
static CommandResult handleAxisPosDu(const char* args);
static CommandResult handleAxisLimitDu(const char* args);
static CommandResult handleAxisClearLimit(const char* args);
static CommandResult handleTaperRatio(const char* args);
static CommandResult handleTaperAngle(const char* args);
static CommandResult handleTaperPreset(const char* args);
static CommandResult handleMode(const char* args);
static CommandResult handleSimEncoder(const char* args);
static CommandResult handleSimPulses(const char* args);
static CommandResult handleSimRpm(const char* args);
static CommandResult handlePing(const char* args);

// Staged Ingress Compat Handlers
static CommandResult handleGearAxis(const char* args);
static CommandResult handleGearSync(const char* args);
static CommandResult handleGearDir(const char* args);
static CommandResult handleFeedMm(const char* args);
static CommandResult handleFeedTpi(const char* args);
static CommandResult handleFeedDupr(const char* args);
static CommandResult handleOn(const char* args);

// Dynamic Command ID Resolvers
static CommandId resolveOnCommandId(const char* args) {
  return (args && atoi(args) == 0) ? CommandId::CMD_STOP : CommandId::CMD_RUN;
}

// =============================================================================
// ROUTING TABLE (Zero Hardcoded Dispatcher Logic)
// =============================================================================

struct CommandRoute {
  const char*   name;
  CommandId     id;
  CommandResult (*handler)(const char* args);
  CommandId     (*resolveId)(const char* args); // Optional dynamic resolver
};

static const CommandRoute COMMAND_ROUTES[] = {
  { "startFeed",     CommandId::CMD_RUN,         handleStartFeed,     nullptr },
  { "stop",          CommandId::CMD_STOP,        handleStop,          nullptr },
  { "jogHold",       CommandId::CMD_JOG,         handleJogHold,       nullptr },
  { "jogRelease",    CommandId::CMD_STOP,        handleJogRelease,    nullptr },
  { "jogStep",       CommandId::CMD_JOG,         handleJogStep,       nullptr },
  { "jogStepDu",     CommandId::CMD_SET_PARAM,   handleJogStepDu,     nullptr },
  { "feedNudge",     CommandId::CMD_FEED_NUDGE,  handleFeedNudge,     nullptr },
  { "feedStepCycle", CommandId::CMD_STEP_CYCLE,  handleFeedStepCycle, nullptr },
  { "feedStepDu",    CommandId::CMD_SET_PARAM,   handleFeedStepDu,    nullptr },
  { "axisZero",      CommandId::CMD_ZERO_AXIS,   handleAxisZero,      nullptr },
  { "axisPosDu",     CommandId::CMD_ZERO_AXIS,   handleAxisPosDu,     nullptr },
  { "axisLimitDu",   CommandId::CMD_SET_LIMIT,   handleAxisLimitDu,   nullptr },
  { "axisClearLimit",CommandId::CMD_SET_LIMIT,   handleAxisClearLimit,nullptr },
  { "taperRatio",    CommandId::CMD_SET_PARAM,   handleTaperRatio,    nullptr },
  { "taperAngle",    CommandId::CMD_SET_PARAM,   handleTaperAngle,    nullptr },
  { "taperPreset",   CommandId::CMD_SET_PARAM,   handleTaperPreset,   nullptr },
  { "mode",          CommandId::CMD_SWITCH_MODE, handleMode,          nullptr },
  { "simEncoder",    CommandId::CMD_NONE,        handleSimEncoder,    nullptr },
  { "simPulses",     CommandId::CMD_NONE,        handleSimPulses,     nullptr },
  { "simRpm",        CommandId::CMD_NONE,        handleSimRpm,        nullptr },
  { "ping",          CommandId::CMD_NONE,        handlePing,          nullptr },
  // Staged Ingress Modal Routes
  { "gearAxis",      CommandId::CMD_SET_AXIS,    handleGearAxis,      nullptr },
  { "gearSync",      CommandId::CMD_SET_SYNC,    handleGearSync,      nullptr },
  { "gearDir",       CommandId::CMD_SET_DIR,     handleGearDir,       nullptr },
  { "feedDir",       CommandId::CMD_SET_DIR,     handleGearDir,       nullptr },
  { "feedMm",        CommandId::CMD_SET_PITCH,   handleFeedMm,        nullptr },
  { "feedTpi",       CommandId::CMD_SET_PITCH,   handleFeedTpi,       nullptr },
  { "feedDupr",      CommandId::CMD_SET_PITCH,   handleFeedDupr,      nullptr },
  { "on",            CommandId::CMD_RUN,         handleOn,            resolveOnCommandId }
};

static const size_t NUM_COMMAND_ROUTES = sizeof(COMMAND_ROUTES) / sizeof(COMMAND_ROUTES[0]);

// =============================================================================
// HELPER: TARGET AXIS EXTRACTION
// =============================================================================

static AxisId extractTargetAxis(const char* args) {
  if (!args || !*args) return AXIS_Z;
  if (args[0] == 'X' || args[0] == 'x') return AXIS_X;
  if (args[0] == 'Z' || args[0] == 'z') return AXIS_Z;

  const char* axPtr = strstr(args, "axis:");
  if (axPtr) {
    char c = axPtr[5];
    if (c == 'X' || c == 'x') return AXIS_X;
  }
  return AXIS_Z;
}

static AxisId parseAxisWithDual(const char* val, bool* outIsDual) {
  if (!val) {
    if (outIsDual) *outIsDual = false;
    return AXIS_Z;
  }
  if (strcasecmp(val, "ZX") == 0 || strcasecmp(val, "Z+X") == 0 || strcmp(val, "2") == 0 || strcasecmp(val, "TAPER") == 0) {
    if (outIsDual) *outIsDual = true;
    return AXIS_Z;
  }
  if (outIsDual) *outIsDual = false;
  return MotionConversions::parseAxisId(val, AXIS_Z);
}

// =============================================================================
// COMMAND HANDLER IMPLEMENTATIONS
// =============================================================================

static CommandResult handleStartFeed(const char* args) {
  if (!args || !*args) return CommandResult::failed("startFeed", "Missing arguments");
  const char* p = args;
  AxisId axis = AXIS_Z;
  bool isDualAxis = false;
  bool isSync = true;
  int32_t feedRateDu = 0;
  int8_t dir = 0;
  int32_t distDu = -1;
  bool hasFeed = false;
  bool hasDir = false;

  if (strchr(p, ':') != nullptr) {
    const char* cur = p;
    while (*cur) {
      while (*cur == ' ' || *cur == ',') cur++;
      if (!*cur) break;

      char key[16] = {0};
      size_t kIdx = 0;
      while (*cur && *cur != ':' && *cur != '=' && *cur != ',' && kIdx < sizeof(key) - 1) {
        key[kIdx++] = *cur++;
      }
      if (*cur != ':' && *cur != '=') {
        while (*cur && *cur != ',') cur++;
        continue;
      }
      cur++;

      char val[64] = {0};
      size_t vIdx = 0;
      if (*cur == '"') {
        cur++;
        while (*cur && *cur != '"' && vIdx < sizeof(val) - 1) {
          val[vIdx++] = *cur++;
        }
        if (*cur == '"') cur++;
      } else {
        while (*cur && *cur != ',' && vIdx < sizeof(val) - 1) {
          val[vIdx++] = *cur++;
        }
      }

      if (strcasecmp(key, "axis") == 0 || strcasecmp(key, "a") == 0) {
        axis = parseAxisWithDual(val, &isDualAxis);
      } else if (strcasecmp(key, "sync") == 0 || strcasecmp(key, "s") == 0) {
        isSync = (strcasecmp(val, "1") == 0 || strcasecmp(val, "sync") == 0 || strcasecmp(val, "true") == 0);
      } else if (strcasecmp(key, "feed") == 0 || strcasecmp(key, "rate") == 0 || strcasecmp(key, "f") == 0) {
        feedRateDu = strchr(val, '.') ? MotionConversions::mmToDu(atof(val)) : atol(val);
        hasFeed = (feedRateDu > 0);
      } else if (strcasecmp(key, "dir") == 0 || strcasecmp(key, "d") == 0) {
        dir = MotionConversions::semanticToDirection(axis, val);
        hasDir = (dir != 0);
      } else if (strcasecmp(key, "dist") == 0) {
        distDu = strchr(val, '.') ? MotionConversions::mmToDu(atof(val)) : atol(val);
      }
    }

    if (!hasFeed || !hasDir) {
      return CommandResult::failed("startFeed", "Requires feed:<rate> and dir:<dir>");
    }
  } else {
    char axisToken[8] = {0};
    char syncToken[16] = {0};
    char rateToken[32] = {0};
    char dirToken[16] = {0};
    char distToken[32] = {0};

    int parsed = sscanf(p, "%7[^,],%15[^,],%31[^,],%15[^,],%31s",
                        axisToken, syncToken, rateToken, dirToken, distToken);
    if (parsed < 4) {
      return CommandResult::failed("startFeed", "Requires axis:<axis>,sync:<sync>,feed:<rate>,dir:<dir>,[dist:<dist>]");
    }

    axis = parseAxisWithDual(axisToken, &isDualAxis);
    isSync = (strcasecmp(syncToken, "1") == 0 || strcasecmp(syncToken, "sync") == 0 || strcasecmp(syncToken, "true") == 0);
    dir = MotionConversions::semanticToDirection(axis, dirToken);
    if (dir == 0) {
      return CommandResult::failed("startFeed", "Invalid direction");
    }

    feedRateDu = strchr(rateToken, '.') ? MotionConversions::mmToDu(atof(rateToken)) : atol(rateToken);
    if (parsed >= 5 && distToken[0] && strcmp(distToken, "-1") != 0) {
      distDu = strchr(distToken, '.') ? MotionConversions::mmToDu(atof(distToken)) : atol(distToken);
    }
  }

  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (!gb) {
    return CommandResult::failed("startFeed", "Gearbox mode not registered");
  }

  ModeRegistry::activateMode(ModeIds::GEARBOX);
  gb->setActiveAxis(isDualAxis ? "ZX" : (axis == AXIS_X ? "X" : "Z"));
  if (isDualAxis && isSync) {
    SettingsService::requestMode(static_cast<int>(LatheMode::CONE));
  }
  bool ok = gb->startFeed(axis, isSync, feedRateDu, dir, distDu);
  return ok ? CommandResult::ok("startFeed") : CommandResult::failed("startFeed", "Feed engagement failed");
}

static CommandResult handleStop(const char* args) {
  (void)args;
  IELSMode* active = ModeRegistry::getActiveMode();
  if (active && active->isRunning()) {
    active->stop(StopType::NORMAL_STOP);
  }
  MotionEngine::instance().stopAll();
  SettingsService::requestIsOn(false);
  return CommandResult::ok("stop");
}

static CommandResult handleJogHold(const char* args) {
  if (!args || !*args) return CommandResult::failed("jogHold", "Missing axis argument");
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  const char* p = strchr(args, ',');
  if (!p) return CommandResult::failed("jogHold", "Missing direction");
  p++;
  while (*p == ' ' || *p == '\t') p++;
  int8_t dir = MotionConversions::semanticToDirection(axis, p);
  if (dir == 0) return CommandResult::failed("jogHold", "Invalid direction");

  MotionCommandPacket pkt;
  pkt.type = MotionCommandPacket::Type::EXECUTE_JOG;
  pkt.axis = axis;
  pkt.direction = dir;
  pkt.targetCoordDu = -1;
  bool ok = MotionEngine::instance().postCommand(pkt);
  return ok ? CommandResult::ok("jogHold") : CommandResult::failed("jogHold", "Failed to post jog command");
}

static CommandResult handleJogRelease(const char* args) {
  bool ok = false;
  if (!args || !*args || strcasecmp(args, "ALL") == 0) {
    MotionCommandPacket pkt;
    pkt.type = MotionCommandPacket::Type::STOP_NORMAL;
    pkt.axis = AXIS_Z;
    MotionEngine::instance().postCommand(pkt);
    pkt.axis = AXIS_X;
    ok = MotionEngine::instance().postCommand(pkt);
  } else {
    char ax = args[0];
    AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
    MotionCommandPacket pkt;
    pkt.type = MotionCommandPacket::Type::STOP_NORMAL;
    pkt.axis = axis;
    ok = MotionEngine::instance().postCommand(pkt);
  }
  return ok ? CommandResult::ok("jogRelease") : CommandResult::failed("jogRelease", "Jog release failed");
}

static CommandResult handleJogStep(const char* args) {
  if (!args || !*args) return CommandResult::failed("jogStep", "Missing arguments");
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  const char* p = strchr(args, ',');
  if (!p) return CommandResult::failed("jogStep", "Missing direction");
  p++;
  while (*p == ' ' || *p == '\t') p++;
  int8_t dir = MotionConversions::semanticToDirection(axis, p);
  if (dir == 0) return CommandResult::failed("jogStep", "Invalid direction");

  int32_t stepDu = SettingsService::getJogStepDu();
  if (stepDu <= 0) stepDu = 1000;

  MotionCommandPacket pkt;
  pkt.type = MotionCommandPacket::Type::EXECUTE_JOG;
  pkt.axis = axis;
  pkt.direction = dir;
  pkt.targetCoordDu = stepDu;
  bool ok = MotionEngine::instance().postCommand(pkt);
  return ok ? CommandResult::ok("jogStep") : CommandResult::failed("jogStep", "Failed to post jog step");
}

static CommandResult handleJogStepDu(const char* args) {
  if (!args || !*args) return CommandResult::failed("jogStepDu", "Missing value");
  SettingsService::setJogStepDu(atol(args));
  return CommandResult::ok("jogStepDu");
}

static CommandResult handleFeedStepDu(const char* args) {
  if (!args || !*args) return CommandResult::failed("feedStepDu", "Missing value");
  SettingsService::setJogStepDu(atol(args));
  return CommandResult::ok("feedStepDu");
}

static CommandResult handleFeedStepCycle(const char* args) {
  (void)args;
  SettingsService::cycleJogStep();
  return CommandResult::ok("feedStepCycle");
}

static CommandResult handleFeedNudge(const char* args) {
  if (!args || !*args) return CommandResult::failed("feedNudge", "Missing direction");
  int dir = atoi(args);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb && gb->nudgeFeed(dir)) {
    return CommandResult::ok("feedNudge");
  }
  return CommandResult::failed("feedNudge", "Nudge not applicable");
}

static CommandResult handleAxisZero(const char* args) {
  if (!args || !*args) return CommandResult::failed("axisZero", "Missing axis");
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  MotionEngine::instance().zeroAxis(axis);
  return CommandResult::ok("axisZero");
}

static CommandResult handleAxisPosDu(const char* args) {
  if (!args || !*args) return CommandResult::failed("axisPosDu", "Missing arguments");
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  const char* comma = strchr(args, ',');
  if (!comma) return CommandResult::failed("axisPosDu", "Missing coordinate");
  long posDu = atol(comma + 1);
  MotionEngine::instance().setAxisPositionDu(axis, posDu);
  return CommandResult::ok("axisPosDu");
}

static CommandResult handleAxisLimitDu(const char* args) {
  if (!args || !*args) return CommandResult::failed("axisLimitDu", "Missing arguments");
  char ax = args[0];
  const char* p = strchr(args, ',');
  if (!p) return CommandResult::failed("axisLimitDu", "Missing limit side");
  p++;
  bool isLeft = (*p == 'L' || *p == 'l' || *p == '0' || *p == 'U' || *p == 'u');
  const char* p2 = strchr(p, ',');
  if (!p2) return CommandResult::failed("axisLimitDu", "Missing coordinate");
  long du = atol(p2 + 1);
  AxisId axisId = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeft ? LimitDirection::LIMIT_LEFT_OR_UP : LimitDirection::LIMIT_RIGHT_OR_DOWN;
  SettingsService::setAxisLimitDu(axisId, dir, du);
  return CommandResult::ok("axisLimitDu");
}

static CommandResult handleAxisClearLimit(const char* args) {
  if (!args || !*args) return CommandResult::failed("axisClearLimit", "Missing arguments");
  char ax = args[0];
  const char* p = strchr(args, ',');
  if (!p) return CommandResult::failed("axisClearLimit", "Missing limit side");
  p++;
  bool isLeft = (*p == 'L' || *p == 'l' || *p == '0' || *p == 'U' || *p == 'u');
  AxisId axisId = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeft ? LimitDirection::LIMIT_LEFT_OR_UP : LimitDirection::LIMIT_RIGHT_OR_DOWN;
  SettingsService::clearAxisLimit(axisId, dir);
  return CommandResult::ok("axisClearLimit");
}

static CommandResult handleTaperRatio(const char* args) {
  if (!args || !*args) return CommandResult::failed("taperRatio", "Missing ratio");
  float ratio = atof(args);
  SettingsService::setTaperRatio(ratio);
  return CommandResult::ok("taperRatio");
}

static CommandResult handleTaperAngle(const char* args) {
  if (!args || !*args) return CommandResult::failed("taperAngle", "Missing angle");
  float angle = atof(args);
  float ratio = tan(angle * (M_PI / 180.0));
  SettingsService::setTaperRatio(ratio);
  return CommandResult::ok("taperAngle");
}

static CommandResult handleTaperPreset(const char* args) {
  if (!args || !*args) return CommandResult::failed("taperPreset", "Missing preset");
  int presetId = 0;
  float angleDeg = 0.0f;
  float ratioVal = 0.0f;
  sscanf(args, "%d,%f,%f", &presetId, &angleDeg, &ratioVal);
  if (SettingsService::setTaperPreset(presetId, angleDeg, ratioVal)) {
    return CommandResult::ok("taperPreset");
  }
  return CommandResult::failed("taperPreset", "Failed to set taper preset");
}

static CommandResult handleMode(const char* args) {
  if (!args || !*args) return CommandResult::failed("mode", "Missing mode");
  IELSMode* targetBySlug = ModeRegistry::getModeBySlug(args);
  if (targetBySlug) {
    ModeRegistry::activateMode(targetBySlug->getId());
    if (targetBySlug->getId() == ModeIds::GEARBOX) {
      UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
      if (gb) {
        if (!gb->isSync()) SettingsService::requestMode(static_cast<int>(LatheMode::ASYNC));
        else if (strcasecmp(gb->getActiveAxis(), "X") == 0) SettingsService::requestMode(static_cast<int>(LatheMode::XGEAR));
        else if (strcasecmp(gb->getActiveAxis(), "ZX") == 0) SettingsService::requestMode(static_cast<int>(LatheMode::CONE));
        else SettingsService::requestMode(static_cast<int>(LatheMode::NORMAL));
      }
    }
    return CommandResult::ok("mode");
  }

  int m = atoi(args);
  if (m >= 0 && m <= 7) {
    SettingsService::requestMode(m);
    if (m == ModeIds::GEARBOX) {
      ModeRegistry::activateMode(ModeIds::GEARBOX);
    }
    return CommandResult::ok("mode");
  }
  return CommandResult::failed("mode", "Invalid mode ID");
}

static CommandResult handleSimEncoder(const char* args) {
  if (!args || !*args) return CommandResult::failed("simEncoder", "Missing args");
  bool active = atoi(args) != 0;
  if (active) {
    ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
    if (MotionEngine::instance().isAnyAxisMoving() || (hal && hal->getFilteredRpm() > 0)) {
      return CommandResult::failed("simEncoder", "SAFETY_INTERLOCK_ACTIVE");
    }
  }
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->setSimulationActive(active);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simEncoder");
}

static CommandResult handleSimPulses(const char* args) {
  if (!args || !*args) return CommandResult::failed("simPulses", "Missing pulses");
  int32_t pulses = atol(args);
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->injectSimulatedPulses(pulses);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simPulses");
}

static CommandResult handleSimRpm(const char* args) {
  if (!args || !*args) return CommandResult::failed("simRpm", "Missing RPM");
  float rpm = atof(args);
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->setSimulatedRpm(rpm);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simRpm");
}

static CommandResult handlePing(const char* args) {
  (void)args;
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("ping");
}

// Staged Ingress Handlers
static CommandResult handleGearAxis(const char* args) {
  if (!args || !*args) return CommandResult::failed("gearAxis", "Missing axis");
  const char* val = args;
  const char* axStr = "Z";
  int targetLowLevelMode = static_cast<int>(LatheMode::NORMAL);
  if (strcasecmp(val, "X") == 0 || strcmp(val, "1") == 0) {
    axStr = "X";
    targetLowLevelMode = static_cast<int>(LatheMode::XGEAR);
  } else if (strcasecmp(val, "ZX") == 0 || strcasecmp(val, "Z+X") == 0 || strcmp(val, "2") == 0 || strcasecmp(val, "TAPER") == 0) {
    axStr = "ZX";
    targetLowLevelMode = static_cast<int>(LatheMode::CONE);
  }
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    gb->setActiveAxis(axStr);
    if (gb->isSync()) {
      SettingsService::requestMode(targetLowLevelMode);
    }
    return CommandResult::ok("gearAxis");
  }
  return CommandResult::failed("gearAxis", "Gearbox mode unavailable");
}

static CommandResult handleGearSync(const char* args) {
  if (!args || !*args) return CommandResult::failed("gearSync", "Missing sync value");
  bool isSync = (atoi(args) != 0) || (strcasecmp(args, "true") == 0);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    gb->setSync(isSync);
    return CommandResult::ok("gearSync");
  }
  return CommandResult::failed("gearSync", "Gearbox mode unavailable");
}

static CommandResult handleGearDir(const char* args) {
  if (!args || !*args) return CommandResult::failed("gearDir", "Missing direction");
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    if (gb->setDirection(args)) {
      return CommandResult::ok("gearDir");
    }
  }
  return CommandResult::failed("gearDir", "Invalid direction");
}

static CommandResult handleFeedMm(const char* args) {
  if (!args || !*args) return CommandResult::failed("feedMm", "Missing pitch value");
  float mm = atof(args);
  SettingsService::setFeedrateMm(mm, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) gb->setFeedRateMm(std::abs(mm));
  return CommandResult::ok("feedMm");
}

static CommandResult handleFeedTpi(const char* args) {
  if (!args || !*args) return CommandResult::failed("feedTpi", "Missing TPI value");
  float tpi = atof(args);
  SettingsService::setFeedrateTpi(tpi, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb && tpi > 0.0f) {
    int32_t dupr = static_cast<int32_t>(std::round(254000.0f / tpi));
    gb->setFeedRate(dupr);
  }
  return CommandResult::ok("feedTpi");
}

static CommandResult handleFeedDupr(const char* args) {
  if (!args || !*args) return CommandResult::failed("feedDupr", "Missing DUPR value");
  long dupr = atol(args);
  SettingsService::setFeedrateDupr(dupr, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) gb->setFeedRate(std::abs(dupr));
  return CommandResult::ok("feedDupr");
}

static CommandResult handleOn(const char* args) {
  if (!args || !*args) return CommandResult::failed("on", "Missing argument");
  int on = atoi(args);
  if (on == 0) {
    return handleStop("");
  } else {
    IELSMode* active = ModeRegistry::getActiveMode();
    if (active && !active->isRunning()) {
      if (!active->start()) {
        return CommandResult::failed("on", "Failed to start active mode");
      }
    }
    SettingsService::requestIsOn(true);
    return CommandResult::ok("on");
  }
}

// =============================================================================
// MAIN ENTRY POINT: SINGLE-GATE DISPATCHER WITH DEFENSIVE FAULT CONTAINMENT
// =============================================================================

CommandResult CommandDispatcher::dispatch(const char* payload, size_t length, uint8_t clientId) {
  (void)clientId;
  if (!payload || length == 0) {
    return CommandResult::unrecognized();
  }

  // Trim leading whitespace
  while (length > 0 && (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n')) {
    payload++;
    length--;
  }
  // Trim trailing whitespace
  while (length > 0 && (payload[length - 1] == ' ' || payload[length - 1] == '\t' || payload[length - 1] == '\r' || payload[length - 1] == '\n')) {
    length--;
  }
  if (length == 0) {
    return CommandResult::unrecognized();
  }

  // Guarantee clean, stack-allocated null-terminated local buffer
  char cleanBuf[256];
  if (length >= sizeof(cleanBuf)) length = sizeof(cleanBuf) - 1;
  memcpy(cleanBuf, payload, length);
  cleanBuf[length] = '\0';
  payload = cleanBuf;

  // 1. Sysinfo query
  if (length >= 8 && strncmp(payload, "?sysinfo", 8) == 0) {
    String rep = "SYSINFO.version=" CONTROLLER_VERSION_TEXT "\n";
    rep += "SYSINFO.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    rep += "SYSINFO.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    rep += "SYSINFO.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
#if ENABLE_SPINDLE_SIM
    rep += "SYSINFO.features=spindleSim\n";
#endif
    return CommandResult::ok("sysinfo", rep);
  }

  // 2. Settings Namespace
  if (length >= 8 && strncasecmp(payload, "SETTINGS", 8) == 0) {
    const char* sub = payload + 8;
    if (*sub == '.') sub++;
    String reply = "";
    bool ok = false;
    if (strncasecmp(sub, "getGroup=", 9) == 0) {
      ok = SettingsService::getGroup(sub + 9, &reply);
    } else if (strncasecmp(sub, "resetGroup=", 11) == 0) {
      ok = SettingsService::resetGroup(sub + 11, &reply);
    } else if (strcasecmp(sub, "preset.list") == 0) {
      ok = SettingsService::listPresets(&reply);
    } else if (strncasecmp(sub, "preset.get=", 11) == 0) {
      ok = SettingsService::getPresetJson(sub + 11, &reply);
    } else if (strncasecmp(sub, "preset.apply=", 13) == 0) {
      ok = SettingsService::applyPreset(sub + 13, &reply);
    } else if (strcasecmp(sub, "preset.save") == 0) {
      ok = SettingsService::saveCurrentAsPreset(nullptr, &reply);
    } else if (strncasecmp(sub, "preset.save=", 12) == 0) {
      ok = SettingsService::saveCurrentAsPreset(sub + 12, &reply);
    } else if (strncasecmp(sub, "preset.delete=", 14) == 0) {
      ok = SettingsService::deletePreset(sub + 14, &reply);
    } else if (strncasecmp(sub, "preset.setDefault=", 18) == 0) {
      ok = SettingsService::setDefaultPreset(sub + 18, &reply);
    } else if (strcasecmp(sub, "preset.resetToDefault") == 0) {
      ok = SettingsService::resetToDefaultPreset(&reply);
    } else if (strncasecmp(sub, "preset=", 7) == 0) {
      ok = SettingsService::setPreset(sub + 7, &reply);
    } else if (strncasecmp(sub, "get", 3) == 0) {
      const char* key = (sub[3] == '=') ? sub + 4 : nullptr;
      ok = SettingsService::get(key, &reply);
    } else if (strncasecmp(sub, "set=", 4) == 0) {
      ok = SettingsService::set(sub + 4, &reply);
    } else if (strncasecmp(sub, "reset=", 6) == 0) {
      ok = SettingsService::reset(sub + 6, &reply);
    } else if (strncasecmp(sub, "schema", 6) == 0) {
      const char* name = (sub[6] == '=') ? sub + 7 : nullptr;
      ok = SettingsService::getSchema(name, &reply);
    }
    return ok ? CommandResult::ok("settings", reply) : CommandResult::failed("settings", reply.length() > 0 ? reply : "Settings operation failed");
  }

  // 3. Preset Namespace
  if (length >= 6 && strncasecmp(payload, "PRESET", 6) == 0) {
    const char* sub = payload + 6;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    String reply = "";
    bool ok = PresetService::dispatch(sub, strlen(sub), &reply);
    return ok ? CommandResult::ok("preset", reply) : CommandResult::failed("preset", reply.length() > 0 ? reply : "Preset operation failed");
  }

  // 4. Mode Namespace (MODE discover)
  if (length >= 4 && strncasecmp(payload, "MODE", 4) == 0) {
    const char* sub = payload + 4;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    if (strncasecmp(sub, "discover", 8) == 0) {
      String reply = ModeRegistry::serializeDiscoveryJson(SettingsService::getActiveProfile().capabilities, false) + "\n";
      return CommandResult::ok("mode.discover", reply);
    }
  }

  // 5. Domain State Queries (?get=K,I / ?state)
  if ((length >= 4 && strncmp(payload, "?get", 4) == 0) ||
      (length >= 6 && strncmp(payload, "?state", 6) == 0)) {
    uint8_t mask = DOMAIN_ALL;
    const char* eq = strchr(payload, '=');
    if (eq) {
      mask = 0;
      const char* p = eq + 1;
      while (*p) {
        if (*p == 'K' || *p == 'k') mask |= DOMAIN_KINEMATICS;
        else if (*p == 'I' || *p == 'i') mask |= DOMAIN_INTENT;
        else if (*p == 'C' || *p == 'c') mask |= DOMAIN_CYCLE;
        else if (*p == 'P' || *p == 'p') mask |= DOMAIN_PROPERTIES;
        else if (strncasecmp(p, "ALL", 3) == 0) { mask = DOMAIN_ALL; break; }
        p++;
      }
    }
    MachineModel m = MachineModel::captureAll();
    String rep = TelemetrySerializer::buildTelemetry(m, mask);
    return CommandResult::ok("get", rep);
  }

  // 6. Canonical CMD.* Namespace (Single-Gate Architecture)
  if (length >= 4 && strncmp(payload, "CMD.", 4) == 0) {
    const char* cmd = payload + 4;

    char cmdName[32] = {0};
    const char* args = "";
    const char* eq = strchr(cmd, '=');
    if (eq) {
      size_t nameLen = eq - cmd;
      if (nameLen >= sizeof(cmdName)) nameLen = sizeof(cmdName) - 1;
      strncpy(cmdName, cmd, nameLen);
      cmdName[nameLen] = '\0';
      args = eq + 1;
    } else {
      strncpy(cmdName, cmd, sizeof(cmdName) - 1);
      cmdName[sizeof(cmdName) - 1] = '\0';
    }

    // Match against Command Router Table
    for (size_t i = 0; i < NUM_COMMAND_ROUTES; ++i) {
      const CommandRoute& route = COMMAND_ROUTES[i];
      if (strcasecmp(cmdName, route.name) == 0) {
        AxisId targetAxis = extractTargetAxis(args);

        // Determine effective command ID (dynamic resolver if present)
        CommandId effectiveId = route.resolveId ? route.resolveId(args) : route.id;

        // =====================================================================
        // THE SINGLE GATE: Enforce in-motion safety contract [SAFE-01]
        // =====================================================================
        if (effectiveId != CommandId::CMD_NONE) {
          IELSMode* active = ModeRegistry::getActiveMode();
          if (active && !active->canExecuteCommand(effectiveId, active->getState(), targetAxis)) {
            return CommandResult::failed(route.name, "Command locked during active motion");
          }
        }

        // =====================================================================
        // DEFENSIVE EXECUTION SANDBOX
        // =====================================================================
#if __cpp_exceptions
        try {
          return route.handler(args);
        } catch (const std::exception& ex) {
          return CommandResult::failed(route.name, String("INTERNAL_ERROR:") + ex.what());
        } catch (...) {
          return CommandResult::failed(route.name, "INTERNAL_ERROR:unknown_fault");
        }
#else
        return route.handler(args);
#endif
      }
    }

    // Prefix-style commands like CMD.settings.get
    if (strncasecmp(cmd, "settings.", 9) == 0) {
      String reply = "";
      bool ok = SettingsService::get(cmd + 9, &reply);
      return ok ? CommandResult::ok("settings", reply) : CommandResult::failed("settings", "Settings read failed");
    }
  }

  return CommandResult::unrecognized(payload);
}

} // namespace core
} // namespace nanoels
