#include "CommandDispatcher.h"
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

// =============================================================================
// FORWARD DECLARATIONS OF DEDICATED COMMAND HANDLERS
// =============================================================================

static bool handleGet(const char* args, String* reply);
static bool handleModes(const char* args, String* reply);
static bool handleSettings(const char* args, String* reply);
static bool handleStartFeed(const char* args, String* reply);
static bool handleStop(const char* args, String* reply);
static bool handleJogHold(const char* args, String* reply);
static bool handleJogRelease(const char* args, String* reply);
static bool handleJogStep(const char* args, String* reply);
static bool handleJogStepDu(const char* args, String* reply);
static bool handleFeedStepDu(const char* args, String* reply);
static bool handleFeedStepCycle(const char* args, String* reply);
static bool handleFeedRev(const char* args, String* reply);
static bool handleFeedDupr(const char* args, String* reply);
static bool handleFeedMm(const char* args, String* reply);
static bool handleFeedTpi(const char* args, String* reply);
static bool handleFeedInch(const char* args, String* reply);
static bool handleFeedNudge(const char* args, String* reply);
static bool handleAxisZero(const char* args, String* reply);
static bool handleAxisPosDu(const char* args, String* reply);
static bool handleAxisLimitDu(const char* args, String* reply);
static bool handleAxisClearLimit(const char* args, String* reply);
static bool handleGearAxis(const char* args, String* reply);
static bool handleGearSync(const char* args, String* reply);
static bool handleGearDir(const char* args, String* reply);
static bool handleTaperRatio(const char* args, String* reply);
static bool handleTaperAngle(const char* args, String* reply);
static bool handleTaperPreset(const char* args, String* reply);
static bool handleMode(const char* args, String* reply);
static bool handleSimEncoder(const char* args, String* reply);
static bool handleSimPulses(const char* args, String* reply);
static bool handleSimRpm(const char* args, String* reply);
static bool handlePing(const char* args, String* reply);
#if ENABLE_SPINDLE_SIM
static bool handleSpindleSim(const char* args, String* reply);
static bool handleSpindleSimTurn(const char* args, String* reply);
#endif
static bool handleMeasure(const char* args, String* reply);

// =============================================================================
// COMMAND ROUTING TABLE & DESCRIPTORS
// =============================================================================

struct CommandRoute {
  const char* name;
  CommandId   id;
  bool (*handler)(const char* args, String* reply);
};

static const CommandRoute COMMAND_ROUTES[] = {
  { "get",           CommandId::CMD_NONE,        handleGet },
  { "modes",         CommandId::CMD_NONE,        handleModes },
  { "settings",      CommandId::CMD_NONE,        handleSettings },
  { "startFeed",     CommandId::CMD_RUN,         handleStartFeed },
  { "stop",          CommandId::CMD_STOP,        handleStop },
  { "jogHold",       CommandId::CMD_JOG,         handleJogHold },
  { "jogRelease",    CommandId::CMD_STOP,        handleJogRelease },
  { "jogStep",       CommandId::CMD_JOG,         handleJogStep },
  { "jogStepDu",     CommandId::CMD_SET_PARAM,   handleJogStepDu },
  { "feedStepDu",    CommandId::CMD_SET_PARAM,   handleFeedStepDu },
  { "feedStepCycle", CommandId::CMD_STEP_CYCLE,  handleFeedStepCycle },
  { "feedRev",       CommandId::CMD_SET_DIR,     handleFeedRev },
  { "feedDupr",      CommandId::CMD_SET_PITCH,   handleFeedDupr },
  { "feedMm",        CommandId::CMD_SET_PITCH,   handleFeedMm },
  { "feedTpi",       CommandId::CMD_SET_PITCH,   handleFeedTpi },
  { "feedInch",      CommandId::CMD_SET_PITCH,   handleFeedInch },
  { "feedNudge",     CommandId::CMD_FEED_NUDGE,  handleFeedNudge },
  { "axisZero",      CommandId::CMD_ZERO_AXIS,   handleAxisZero },
  { "axisPosDu",     CommandId::CMD_ZERO_AXIS,   handleAxisPosDu },
  { "axisLimitDu",   CommandId::CMD_SET_LIMIT,   handleAxisLimitDu },
  { "axisClearLimit",CommandId::CMD_SET_LIMIT,   handleAxisClearLimit },
  { "gearAxis",      CommandId::CMD_SET_AXIS,    handleGearAxis },
  { "gearSync",      CommandId::CMD_SET_SYNC,    handleGearSync },
  { "gearDir",       CommandId::CMD_SET_DIR,     handleGearDir },
  { "feedDir",       CommandId::CMD_SET_DIR,     handleGearDir },
  { "taperRatio",    CommandId::CMD_SET_PARAM,   handleTaperRatio },
  { "taperAngle",    CommandId::CMD_SET_PARAM,   handleTaperAngle },
  { "taperPreset",   CommandId::CMD_SET_PARAM,   handleTaperPreset },
  { "mode",          CommandId::CMD_SWITCH_MODE, handleMode },
  { "simEncoder",    CommandId::CMD_NONE,        handleSimEncoder },
  { "simPulses",     CommandId::CMD_NONE,        handleSimPulses },
  { "simRpm",        CommandId::CMD_NONE,        handleSimRpm },
  { "ping",          CommandId::CMD_NONE,        handlePing },
#if ENABLE_SPINDLE_SIM
  { "spindleSim",    CommandId::CMD_NONE,        handleSpindleSim },
  { "spindleSimTurn",CommandId::CMD_NONE,        handleSpindleSimTurn },
#endif
  { "measure",       CommandId::CMD_SET_PARAM,   handleMeasure },
};

static const size_t NUM_COMMAND_ROUTES = sizeof(COMMAND_ROUTES) / sizeof(COMMAND_ROUTES[0]);

// =============================================================================
// HELPER: TARGET AXIS EXTRACTION
// =============================================================================

static AxisId extractTargetAxis(const char* args) {
  if (!args || !*args) return AXIS_Z;
  while (*args == ' ' || *args == '\t') args++;
  if (*args == 'X' || *args == 'x') return AXIS_X;
  if (*args == 'Z' || *args == 'z') return AXIS_Z;
  const char* p = strstr(args, "axis:");
  if (p) {
    char a = p[5];
    if (a == 'X' || a == 'x') return AXIS_X;
  }
  return AXIS_Z;
}

// =============================================================================
// DEDICATED HANDLER IMPLEMENTATIONS
// =============================================================================

static bool handleGet(const char* args, String* reply) {
  uint8_t mask = parseDomainQuery(args);
  MachineModel m = MachineModel::captureAll();
  String rep = TelemetrySerializer::buildTelemetry(m, mask);
  if (reply) *reply = rep;
  return true;
}

static bool handleModes(const char* args, String* reply) {
  (void)args;
  if (reply) {
    uint32_t caps = SettingsService::getActiveProfile().capabilities;
    *reply = ModeRegistry::serializeDiscoveryJson(caps, false) + "\n";
  }
  return true;
}

static bool handleSettings(const char* args, String* reply) {
  if (!args || !*args) return false;
  if (*args == '.') args++;
  if (strncasecmp(args, "get", 3) == 0) {
    const char* key = (args[3] == '=') ? args + 4 : nullptr;
    return SettingsService::get(key, reply);
  } else if (strncasecmp(args, "set=", 4) == 0) {
    return SettingsService::set(args + 4, reply);
  }
  return false;
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

static bool handleStartFeed(const char* args, String* reply) {
  if (!args || !*args) return false;
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
      if (reply) *reply = "ERR:CMD.startFeed requires feed:<rate> and dir:<dir>\n";
      return false;
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
      if (reply) *reply = "ERR:CMD.startFeed requires axis:<axis>,sync:<sync>,feed:<rate>,dir:<dir>,[dist:<dist>]\n";
      return false;
    }

    axis = parseAxisWithDual(axisToken, &isDualAxis);
    isSync = (strcasecmp(syncToken, "1") == 0 || strcasecmp(syncToken, "sync") == 0 || strcasecmp(syncToken, "true") == 0);
    dir = MotionConversions::semanticToDirection(axis, dirToken);
    if (dir == 0) {
      if (reply) *reply = "ERR:CMD.startFeed invalid direction\n";
      return false;
    }

    feedRateDu = strchr(rateToken, '.') ? MotionConversions::mmToDu(atof(rateToken)) : atol(rateToken);
    if (parsed >= 5 && distToken[0] && strcmp(distToken, "-1") != 0) {
      distDu = strchr(distToken, '.') ? MotionConversions::mmToDu(atof(distToken)) : atol(distToken);
    }
  }

  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (!gb) {
    if (reply) *reply = "ERR:Gearbox mode not registered\n";
    return false;
  }

  ModeRegistry::activateMode(ModeIds::GEARBOX);
  gb->setActiveAxis(isDualAxis ? "ZX" : (axis == AXIS_X ? "X" : "Z"));
  if (isDualAxis && isSync) {
    SettingsService::requestMode(static_cast<int>(LatheMode::CONE));
  }
  bool ok = gb->startFeed(axis, isSync, feedRateDu, dir, distDu);
  if (ok) {
    if (reply) *reply = "OK:CMD.startFeed\n";
    return true;
  } else {
    if (reply) *reply = "ERR:CMD.startFeed failed\n";
    return false;
  }
}

static bool handleStop(const char* args, String* reply) {
  (void)args;
  IELSMode* active = ModeRegistry::getActiveMode();
  if (active && active->isRunning()) {
    active->stop(StopType::NORMAL_STOP);
  }
  MotionEngine::instance().stopAll();
  SettingsService::requestIsOn(false);
  if (reply) *reply = "OK:CMD.stop\n";
  return true;
}

static bool handleJogHold(const char* args, String* reply) {
  if (!args || !*args) return false;
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  const char* p = strchr(args, ',');
  if (!p) return false;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  int8_t dir = MotionConversions::semanticToDirection(axis, p);
  if (dir == 0) return false;

  MotionCommandPacket pkt;
  pkt.type = MotionCommandPacket::Type::EXECUTE_JOG;
  pkt.axis = axis;
  pkt.direction = dir;
  pkt.targetCoordDu = -1;
  bool ok = MotionEngine::instance().postCommand(pkt);
  if (ok && reply) *reply = "OK:CMD.jogHold\n";
  return ok;
}

static bool handleJogRelease(const char* args, String* reply) {
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
  if (ok && reply) *reply = "OK:CMD.jogRelease\n";
  return ok;
}

static bool handleJogStep(const char* args, String* reply) {
  if (!args || !*args) return false;
  char ax = args[0];
  AxisId axis = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  const char* p = strchr(args, ',');
  if (!p) return false;
  p++;
  while (*p == ' ' || *p == '\t') p++;
  int8_t dir = MotionConversions::semanticToDirection(axis, p);
  if (dir == 0) return false;

  int32_t stepDu = SettingsService::getJogStepDu();
  if (stepDu <= 0) stepDu = 1000;

  MotionCommandPacket pkt;
  pkt.type = MotionCommandPacket::Type::EXECUTE_JOG;
  pkt.axis = axis;
  pkt.direction = dir;
  pkt.targetCoordDu = stepDu;
  bool ok = MotionEngine::instance().postCommand(pkt);
  if (ok && reply) *reply = "OK:CMD.jogStep\n";
  return ok;
}

static bool handleJogStepDu(const char* args, String* reply) {
  if (!args || !*args) return false;
  SettingsService::setJogStepDu(atol(args));
  if (reply) *reply = "OK:CMD.jogStepDu\n";
  return true;
}

static bool handleFeedStepDu(const char* args, String* reply) {
  if (!args || !*args) return false;
  SettingsService::setJogStepDu(atol(args));
  if (reply) *reply = "OK:CMD.feedStepDu\n";
  return true;
}

static bool handleFeedStepCycle(const char* args, String* reply) {
  (void)args;
  SettingsService::cycleJogStep();
  if (reply) *reply = "OK:CMD.feedStepCycle\n";
  return true;
}

static bool handleFeedRev(const char* args, String* reply) {
  (void)args;
  SettingsService::reverseFeedDirection();
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) gb->setDirectionSign(-gb->getDirection());
  if (reply) *reply = "OK:CMD.feedRev\n";
  return true;
}

static bool handleFeedDupr(const char* args, String* reply) {
  if (!args || !*args) return false;
  long dupr = atol(args);
  SettingsService::setFeedrateDupr(dupr, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) gb->setFeedRate(std::abs(dupr));
  if (reply) *reply = "OK:CMD.feedDupr\n";
  return true;
}

static bool handleFeedMm(const char* args, String* reply) {
  if (!args || !*args) return false;
  float mm = atof(args);
  SettingsService::setFeedrateMm(mm, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) gb->setFeedRateMm(std::abs(mm));
  if (reply) *reply = "OK:CMD.feedMm\n";
  return true;
}

static bool handleFeedTpi(const char* args, String* reply) {
  if (!args || !*args) return false;
  float tpi = atof(args);
  SettingsService::setFeedrateTpi(tpi, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb && tpi > 0.0f) {
    int32_t dupr = static_cast<int32_t>(std::round(254000.0f / tpi));
    gb->setFeedRate(dupr);
  }
  if (reply) *reply = "OK:CMD.feedTpi\n";
  return true;
}

static bool handleFeedInch(const char* args, String* reply) {
  if (!args || !*args) return false;
  float inch = atof(args);
  SettingsService::setFeedrateInch(inch, false);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb && inch > 0.0f) {
    int32_t dupr = static_cast<int32_t>(std::round(inch * 254000.0f));
    gb->setFeedRate(dupr);
  }
  if (reply) *reply = "OK:CMD.feedInch\n";
  return true;
}

static bool handleFeedNudge(const char* args, String* reply) {
  if (!args || !*args) return false;
  int dir = atoi(args);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb && gb->nudgeFeed(dir)) {
    if (reply) *reply = "OK:CMD.feedNudge\n";
    return true;
  }
  return false;
}

static bool handleAxisZero(const char* args, String* reply) {
  if (!args || !*args) return false;
  char axChar = args[0];
  AxisId axis = (axChar == 'X' || axChar == 'x') ? AXIS_X : AXIS_Z;
  MotionEngine::instance().zeroAxis(axis);
  if (reply) *reply = "OK:CMD.axisZero\n";
  return true;
}

static bool handleAxisPosDu(const char* args, String* reply) {
  if (!args || !*args) return false;
  char axChar = args[0];
  const char* p = strchr(args, ',');
  if (!p) return false;
  long du = atol(p + 1);
  AxisId axis = (axChar == 'X' || axChar == 'x') ? AXIS_X : AXIS_Z;
  MotionEngine::instance().setAxisPositionDu(axis, du);
  if (reply) *reply = "OK:CMD.axisPosDu\n";
  return true;
}

static bool handleAxisLimitDu(const char* args, String* reply) {
  if (!args || !*args) return false;
  char ax = args[0];
  const char* p = strchr(args, ',');
  if (!p) return false;
  p++;
  // Slot 0 = Left/Chuck/Infeed (0, L, l, U, u); Slot 1 = Right/Tail/Retract (1, R, r, D, d)
  bool isLeft = (*p == 'L' || *p == 'l' || *p == '0' || *p == 'U' || *p == 'u');
  const char* p2 = strchr(p, ',');
  if (!p2) return false;
  long du = atol(p2 + 1);
  AxisId axisId = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeft ? LimitDirection::LIMIT_LEFT_OR_UP : LimitDirection::LIMIT_RIGHT_OR_DOWN;
  SettingsService::setAxisLimitDu(axisId, dir, du);
  if (reply) *reply = "OK:CMD.axisLimitDu\n";
  return true;
}

static bool handleAxisClearLimit(const char* args, String* reply) {
  if (!args || !*args) return false;
  char ax = args[0];
  const char* p = strchr(args, ',');
  if (!p) return false;
  p++;
  // Slot 0 = Left/Chuck/Infeed (0, L, l, U, u); Slot 1 = Right/Tail/Retract (1, R, r, D, d)
  bool isLeft = (*p == 'L' || *p == 'l' || *p == '0' || *p == 'U' || *p == 'u');
  AxisId axisId = (ax == 'X' || ax == 'x') ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeft ? LimitDirection::LIMIT_LEFT_OR_UP : LimitDirection::LIMIT_RIGHT_OR_DOWN;
  SettingsService::clearAxisLimit(axisId, dir);
  if (reply) *reply = "OK:CMD.axisClearLimit\n";
  return true;
}

static bool handleGearAxis(const char* args, String* reply) {
  if (!args || !*args) return false;
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
    if (reply) *reply = "OK:CMD.gearAxis\n";
    return true;
  }
  return false;
}

static bool handleGearSync(const char* args, String* reply) {
  if (!args || !*args) return false;
  bool sync = (strcasecmp(args, "0") != 0 && strcasecmp(args, "ASYNC") != 0 && strcasecmp(args, "false") != 0);
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    gb->setSync(sync);
    if (!sync) {
      SettingsService::requestMode(static_cast<int>(LatheMode::ASYNC));
    } else {
      const char* curAx = gb->getActiveAxis();
      if (strcasecmp(curAx, "X") == 0) SettingsService::requestMode(static_cast<int>(LatheMode::XGEAR));
      else if (strcasecmp(curAx, "ZX") == 0) SettingsService::requestMode(static_cast<int>(LatheMode::CONE));
      else SettingsService::requestMode(static_cast<int>(LatheMode::NORMAL));
    }
    if (reply) *reply = "OK:CMD.gearSync\n";
    return true;
  }
  return false;
}

static bool handleGearDir(const char* args, String* reply) {
  if (!args || !*args) return false;
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    gb->setDirection(args);
    if (reply) *reply = "OK:CMD.gearDir\n";
    return true;
  }
  return false;
}

static bool handleTaperRatio(const char* args, String* reply) {
  if (!args || !*args) return false;
  if (SettingsService::setTaperRatio(atof(args))) {
    if (reply) *reply = "OK:CMD.taperRatio\n";
    return true;
  }
  return false;
}

static bool handleTaperAngle(const char* args, String* reply) {
  if (!args || !*args) return false;
  if (SettingsService::setTaperAngle(atof(args))) {
    if (reply) *reply = "OK:CMD.taperAngle\n";
    return true;
  }
  return false;
}

static bool handleTaperPreset(const char* args, String* reply) {
  if (!args || !*args) return false;
  int presetId = 0;
  float angleDeg = 0.0f;
  float ratioVal = 0.0f;
  sscanf(args, "%d,%f,%f", &presetId, &angleDeg, &ratioVal);
  if (SettingsService::setTaperPreset(presetId, angleDeg, ratioVal)) {
    if (reply) *reply = "OK:CMD.taperPreset\n";
    return true;
  }
  return false;
}

static bool handleMode(const char* args, String* reply) {
  if (!args || !*args) return false;
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
    if (reply) *reply = "OK:CMD.mode\n";
    return true;
  }

  int m = atoi(args);
  if (m >= 0 && m <= 7) {
    SettingsService::requestMode(m);
    if (m == ModeIds::GEARBOX) {
      ModeRegistry::activateMode(ModeIds::GEARBOX);
    }
    if (reply) *reply = "OK:CMD.mode\n";
    return true;
  }
  return false;
}

static bool handleSimEncoder(const char* args, String* reply) {
  if (!args || !*args) return false;
  bool active = atoi(args) != 0;
  if (active) {
    // Safety check: reject if physical spindle is rotating or any axis is moving
    ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
    if (MotionEngine::instance().isAnyAxisMoving() || (hal && hal->getFilteredRpm() > 0)) {
      if (reply) *reply = "ERR:SAFETY_INTERLOCK_ACTIVE\n";
      return true;
    }
  }
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->setSimulationActive(active);
  MotionEngine::instance().touchSimHeartbeat();
  if (reply) *reply = "OK:CMD.simEncoder\n";
  return true;
}

static bool handleSimPulses(const char* args, String* reply) {
  if (!args || !*args) return false;
  int32_t pulses = atol(args);
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->injectSimulatedPulses(pulses);
  MotionEngine::instance().touchSimHeartbeat();
  if (reply) *reply = "OK:CMD.simPulses\n";
  return true;
}

static bool handleSimRpm(const char* args, String* reply) {
  if (!args || !*args) return false;
  float rpm = atof(args);
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->setSimulatedRpm(rpm);
  MotionEngine::instance().touchSimHeartbeat();
  if (reply) *reply = "OK:CMD.simRpm\n";
  return true;
}

static bool handlePing(const char* args, String* reply) {
  (void)args;
  MotionEngine::instance().touchSimHeartbeat();
  if (reply) *reply = "OK:CMD.ping\n";
  return true;
}

#if ENABLE_SPINDLE_SIM
static bool handleSpindleSim(const char* args, String* reply) {
  if (!args || !*args) return false;
  int active = atoi(args);
  const char* p = strchr(args, ',');
  float rpm = p ? atof(p + 1) : 0.0f;
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) {
    hal->setSimulationActive(active != 0);
    hal->setSimulatedRpm(rpm);
  }
  MotionEngine::instance().touchSimHeartbeat();
  if (reply) *reply = String("SIM.spindle=") + (active != 0 ? "1" : "0") + "," + String(rpm, 1) + "\n";
  return true;
}

static bool handleSpindleSimTurn(const char* args, String* reply) {
  (void)reply;
  if (!args || !*args) return false;
  float deg = atof(args);
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) {
    float cpr = (hal->getCountsPerSpindleRev() > 0)
                    ? static_cast<float>(hal->getCountsPerSpindleRev())
                    : SettingsService::getActiveProfile().spindle.countsPerRev();
    int32_t counts = lroundf(deg / 360.0f * cpr);
    hal->injectSimulatedPulses(counts);
  }
  MotionEngine::instance().touchSimHeartbeat();
  return true;
}
#endif

static bool handleMeasure(const char* args, String* reply) {
  if (!args || !*args) return false;
  int meas = atoi(args);
  SettingsService::setMeasure(meas);
  if (reply) *reply = "OK:CMD.measure\n";
  return true;
}

// =============================================================================
// MAIN ENTRY POINT & SINGLE-GATE DISPATCHER
// =============================================================================

bool CommandDispatcher::dispatch(const char* payload, size_t length, String* reply, uint8_t clientId) {
  (void)clientId;
  if (!payload || length == 0) return false;

  // Trim leading whitespace
  while (length > 0 && (*payload == ' ' || *payload == '\t' || *payload == '\r' || *payload == '\n')) {
    payload++;
    length--;
  }
  // Trim trailing whitespace
  while (length > 0 && (payload[length - 1] == ' ' || payload[length - 1] == '\t' || payload[length - 1] == '\r' || payload[length - 1] == '\n')) {
    length--;
  }
  if (length == 0) return false;

  // Guarantee clean, null-terminated local buffer for all string tokens
  char cleanBuf[256];
  if (length >= sizeof(cleanBuf)) length = sizeof(cleanBuf) - 1;
  memcpy(cleanBuf, payload, length);
  cleanBuf[length] = '\0';
  payload = cleanBuf;

  // 1. Sysinfo request
  if (length >= 8 && strncmp(payload, "?sysinfo", 8) == 0) {
    String rep = "SYSINFO.version=" CONTROLLER_VERSION_TEXT "\n";
    rep += "SYSINFO.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    rep += "SYSINFO.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    rep += "SYSINFO.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
#if ENABLE_SPINDLE_SIM
    rep += "SYSINFO.features=spindleSim\n";
#endif
    if (reply) *reply = rep;
    return true;
  }

  // 2. Settings Namespace
  if (length >= 8 && strncasecmp(payload, "SETTINGS", 8) == 0) {
    const char* sub = payload + 8;
    if (*sub == '.') sub++;
    if (strncasecmp(sub, "getGroup=", 9) == 0) {
      return SettingsService::getGroup(sub + 9, reply);
    } else if (strncasecmp(sub, "resetGroup=", 11) == 0) {
      return SettingsService::resetGroup(sub + 11, reply);
    } else if (strcasecmp(sub, "preset.list") == 0) {
      return SettingsService::listPresets(reply);
    } else if (strncasecmp(sub, "preset.get=", 11) == 0) {
      return SettingsService::getPresetJson(sub + 11, reply);
    } else if (strncasecmp(sub, "preset.apply=", 13) == 0) {
      return SettingsService::applyPreset(sub + 13, reply);
    } else if (strcasecmp(sub, "preset.save") == 0) {
      return SettingsService::saveCurrentAsPreset(nullptr, reply);
    } else if (strncasecmp(sub, "preset.save=", 12) == 0) {
      return SettingsService::saveCurrentAsPreset(sub + 12, reply);
    } else if (strncasecmp(sub, "preset.delete=", 14) == 0) {
      return SettingsService::deletePreset(sub + 14, reply);
    } else if (strncasecmp(sub, "preset.setDefault=", 18) == 0) {
      return SettingsService::setDefaultPreset(sub + 18, reply);
    } else if (strcasecmp(sub, "preset.resetToDefault") == 0) {
      return SettingsService::resetToDefaultPreset(reply);
    } else if (strncasecmp(sub, "preset=", 7) == 0) {
      return SettingsService::setPreset(sub + 7, reply);
    } else if (strncasecmp(sub, "get", 3) == 0) {
      const char* key = (sub[3] == '=') ? sub + 4 : nullptr;
      return SettingsService::get(key, reply);
    } else if (strncasecmp(sub, "set=", 4) == 0) {
      return SettingsService::set(sub + 4, reply);
    } else if (strncasecmp(sub, "reset=", 6) == 0) {
      return SettingsService::reset(sub + 6, reply);
    } else if (strncasecmp(sub, "schema", 6) == 0) {
      const char* name = (sub[6] == '=') ? sub + 7 : nullptr;
      return SettingsService::getSchema(name, reply);
    }
  }

  // 2b. Preset Namespace
  if (length >= 6 && strncasecmp(payload, "PRESET", 6) == 0) {
    const char* sub = payload + 6;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    return PresetService::dispatch(sub, strlen(sub), reply);
  }

  // 2c. Mode Namespace
  if (length >= 4 && strncasecmp(payload, "MODE", 4) == 0) {
    const char* sub = payload + 4;
    if (*sub == '.') sub++;
    while (*sub == ' ' || *sub == '\t') sub++;
    if (strncasecmp(sub, "discover", 8) == 0) {
      const char* q = strchr(sub, '?');
      uint32_t caps = 0;
      bool includeAll = false;

      if (q) {
        q++;
        if (strncasecmp(q, "all=1", 5) == 0 || strncasecmp(q, "all=true", 8) == 0) {
          includeAll = true;
          caps = 0xFFFFFFFF;
        } else if (strncasecmp(q, "caps=", 5) == 0) {
          caps = (uint32_t)strtoul(q + 5, nullptr, 0);
        } else if (strncasecmp(q, "profile=", 8) == 0) {
          const char* profSlug = q + 8;
          const LatheConfiguration* prof = findPresetBySlug(profSlug);
          if (prof) {
            caps = prof->capabilities;
          } else {
            LatheConfiguration loaded;
            if (loadProfileFromLittleFS(profSlug, &loaded)) {
              caps = loaded.capabilities;
            } else {
              caps = SettingsService::getActiveProfile().capabilities;
            }
          }
        } else {
          caps = SettingsService::getActiveProfile().capabilities;
        }
      } else {
        caps = SettingsService::getActiveProfile().capabilities;
      }

      if (reply) {
        *reply = ModeRegistry::serializeDiscoveryJson(caps, includeAll) + "\n";
      }
      return true;
    }
  }

  // 3. Domain State Queries (?get=K,I / ?state / ?get=ALL)
  if ((length >= 4 && strncmp(payload, "?get", 4) == 0) ||
      (length >= 6 && strncmp(payload, "?state", 6) == 0)) {
    uint8_t mask = parseDomainQuery(payload);
    MachineModel m = MachineModel::captureAll();
    String rep = TelemetrySerializer::buildTelemetry(m, mask);
    if (reply) *reply = rep;
    return true;
  }

  // 4. Canonical CMD.* Namespace (Single-Gate Architecture)
  if (length >= 4 && strncmp(payload, "CMD.", 4) == 0) {
    const char* cmd = payload + 4;

    // Tokenize command name and argument payload
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

        // =====================================================================
        // THE SINGLE GATE: Enforce in-motion safety contract [SAFE-01]
        // =====================================================================
        if (route.id != CommandId::CMD_NONE) {
          IELSMode* active = ModeRegistry::getActiveMode();
          if (active && !active->canExecuteCommand(route.id, active->getState(), targetAxis)) {
            if (reply) *reply = "ERR:Command locked during active motion\n";
            return false;
          }
        }

        // Invoke dedicated command handler
        return route.handler(args, reply);
      }
    }

    // Also check for prefix-style commands like settings.* (e.g. CMD.settings.get)
    if (strncasecmp(cmd, "settings.", 9) == 0) {
      return handleSettings(cmd + 9, reply);
    }
  }

  // 5. Backward Compatibility: @pitch: and @dupr:
  if (length >= 7 && (strncmp(payload, "@pitch:", 7) == 0 || strncmp(payload, "@pitch=", 7) == 0)) {
    float val = atof(payload + 7);
    if (abs(val) > 0.00001f) {
      SettingsService::setFeedrateMm(val, false);
      return true;
    }
  }
  if (length >= 6 && (strncmp(payload, "@dupr:", 6) == 0 || strncmp(payload, "@dupr=", 6) == 0)) {
    long du = atol(payload + 6);
    if (du != 0) {
      SettingsService::setFeedrateDupr(du, false);
      return true;
    }
  }

  return false;
}
