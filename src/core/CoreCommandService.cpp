#include "CoreCommandService.h"
#include "SettingsService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"

namespace whatels::core {

namespace {

template <typename CmdT>
bool validateCmd(const CmdT& cmd, const char* cmdName, CommandResult& outErr) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    outErr = CommandResult::failed(cmdName, valErr ? valErr : "INVALID_PARAM");
    return false;
  }
  return true;
}

bool checkJogGate(const core_client::ClientContext& ctx, const char* cmdName, CommandResult& outErr) {
  if (!ctx.hasPermission(core_client::PERM_JOG_PULSE)) {
    outErr = CommandResult::failed(cmdName, "PERMISSION_DENIED");
    return false;
  }
  if (MotionEngine::instance().isAnyAxisMoving()) {
    outErr = CommandResult::failed(cmdName, "MOTION_LOCKED");
    return false;
  }
  return true;
}

bool checkAxisLimitGate(const core_client::ClientContext& ctx, AxisId axis, const char* cmdName, CommandResult& outErr) {
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    outErr = CommandResult::failed(cmdName, "PERMISSION_DENIED");
    return false;
  }
  if (MotionEngine::instance().isMoving(axis)) {
    outErr = CommandResult::failed(cmdName, "MOTION_LOCKED");
    return false;
  }
  return true;
}

bool checkSimGate(const core_client::ClientContext& ctx,
                  const char* cmdName,
                  bool checkInterlock,
                  ISpindleEncoderHAL*& outHal,
                  CommandResult& outErr) {
  if (!ctx.hasPermission(core_client::PERM_SIMULATION)) {
    outErr = CommandResult::failed(cmdName, "PERMISSION_DENIED");
    return false;
  }
  outHal = MotionEngine::instance().getEncoderHAL();
  if (!outHal) {
    outErr = CommandResult::failed(cmdName, "UNAVAILABLE");
    return false;
  }
  const bool realSpindleSpinning = !outHal->isSimulationActive() && (outHal->getFilteredRpm() > 0);
  if (checkInterlock && (MotionEngine::instance().isAnyAxisMoving() || realSpindleSpinning)) {
    outErr = CommandResult::failed(cmdName, "SAFETY_INTERLOCK_ACTIVE");
    return false;
  }
  return true;
}

bool checkModeGate(CommandId modeCmd, const char* cmdName, bool requireActiveMode, IELSMode*& outMode, CommandResult& outErr) {
  outMode = ModeRegistry::getActiveMode();
  if (!outMode) {
    if (requireActiveMode) {
      outErr = CommandResult::failed(cmdName, "UNAVAILABLE");
      return false;
    }
    return true;
  }
  if (!outMode->canExecuteCommand(modeCmd, outMode->getState())) {
    outErr = CommandResult::failed(cmdName, "MODE_LOCKED");
    return false;
  }
  return true;
}

} // namespace

CoreCommandService& CoreCommandService::instance() {
  static CoreCommandService s_instance;
  return s_instance;
}

bool CoreCommandService::canExecuteMotion(const core_client::ClientContext& ctx, String* errReason) const {
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    if (errReason) *errReason = "PERMISSION_DENIED";
    return false;
  }

  // [SAFE-01]: Engine must be IDLE (neither moving nor in feed hold)
  const MotionEngine& engine = MotionEngine::instance();
  if (engine.isAnyAxisMoving() || engine.isFeedHold(AXIS_Z) || engine.isFeedHold(AXIS_X)) {
    if (errReason) *errReason = "MOTION_LOCKED";
    return false;
  }

  return true;
}

CommandResult CoreCommandService::execute(const StartFeedCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "startFeed", errRes)) return errRes;

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("startFeed", err);
  }

  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (!gb) {
    return CommandResult::failed("startFeed", "UNAVAILABLE");
  }

  ModeRegistry::activateMode(ModeIds::GEARBOX);
  bool ok = gb->startFeed(cmd.axis, cmd.syncSpindle, cmd.feedRate, cmd.direction,
                          cmd.targetDistDu, cmd.isDualAxis, cmd.xDirection, cmd.taperRatioDu);
  if (!ok) {
    return CommandResult::failed("startFeed", "ENGINE_REJECTED");
  }

  return CommandResult::ok("startFeed");
}

CommandResult CoreCommandService::execute(const JogStepCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "jogStep", errRes)) return errRes;
  if (!checkJogGate(ctx, "jogStep", errRes)) return errRes;

  int8_t dir = (cmd.distDu >= 0) ? 1 : -1;
  int32_t stepDu = (cmd.distDu < 0) ? -cmd.distDu : cmd.distDu; // Safe: INT32_MIN rejected by validate()
  if (!MotionEngine::instance().startJog(cmd.axis, dir, stepDu)) {
    return CommandResult::failed("jogStep", "JOG_REJECTED");
  }
  return CommandResult::ok("jogStep");
}

CommandResult CoreCommandService::execute(const JogHoldCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "jogHold", errRes)) return errRes;
  if (!checkJogGate(ctx, "jogHold", errRes)) return errRes;

  if (!MotionEngine::instance().startJog(cmd.axis, cmd.direction, 0)) { // Continuous
    return CommandResult::failed("jogHold", "JOG_REJECTED");
  }
  return CommandResult::ok("jogHold");
}

CommandResult CoreCommandService::execute(const JogReleaseCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "jogRelease", errRes)) return errRes;

  if (!ctx.hasPermission(core_client::PERM_JOG_PULSE)) {
    return CommandResult::failed("jogRelease", "PERMISSION_DENIED");
  }

  if (cmd.hasAxis) {
    MotionEngine::instance().stop(cmd.axis, false);
  } else {
    MotionEngine::instance().stopAll(false);
  }
  return CommandResult::ok("jogRelease");
}

CommandResult CoreCommandService::execute(const AxisZeroCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "axisZero", errRes)) return errRes;

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("axisZero", err);
  }
  MotionEngine::instance().zeroAxis(cmd.axis);
  return CommandResult::ok("axisZero");
}

CommandResult CoreCommandService::execute(const AxisPosCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "axisPosDu", errRes)) return errRes;

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("axisPosDu", err);
  }
  MotionEngine::instance().setAxisPositionDu(cmd.axis, cmd.posDu);
  return CommandResult::ok("axisPosDu");
}

CommandResult CoreCommandService::execute(const AxisLimitCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "axisLimitDu", errRes)) return errRes;
  if (!checkAxisLimitGate(ctx, cmd.axis, "axisLimitDu", errRes)) return errRes;

  LimitDirection dir = (cmd.limitBound == LimitBound::UPPER)
      ? LimitDirection::LIMIT_RIGHT_OR_DOWN
      : LimitDirection::LIMIT_LEFT_OR_UP;
  if (!SettingsService::setAxisLimitDu(cmd.axis, dir, cmd.coordDu)) {
    return CommandResult::failed("axisLimitDu", "LIMIT_OUT_OF_BOUNDS");
  }
  return CommandResult::ok("axisLimitDu");
}

CommandResult CoreCommandService::execute(const AxisClearLimitCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "axisClearLimit", errRes)) return errRes;
  if (!checkAxisLimitGate(ctx, cmd.axis, "axisClearLimit", errRes)) return errRes;

  LimitDirection dir = (cmd.limitBound == LimitBound::UPPER)
      ? LimitDirection::LIMIT_RIGHT_OR_DOWN
      : LimitDirection::LIMIT_LEFT_OR_UP;
  SettingsService::clearAxisLimit(cmd.axis, dir);
  return CommandResult::ok("axisClearLimit");
}

CommandResult CoreCommandService::execute(const HoldCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  IELSMode* activeMode = nullptr;
  CommandResult errRes;
  if (!checkModeGate(CommandId::CMD_FEED_HOLD, "hold", false, activeMode, errRes)) {
    return errRes;
  }

  // Feed hold is universally permitted across all sessions and pauses all active axes
  MotionEngine& engine = MotionEngine::instance();
  const bool movingZ = engine.isMoving(AXIS_Z);
  const bool movingX = engine.isMoving(AXIS_X);
  if (movingZ) engine.pauseFeedHold(AXIS_Z);
  if (movingX) engine.pauseFeedHold(AXIS_X);
  if (!movingZ && !movingX) {
    engine.pauseFeedHold(AXIS_Z);
  }
  return CommandResult::ok("hold");
}

CommandResult CoreCommandService::execute(const ResumeCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("resume", "PERMISSION_DENIED");
  }
  IELSMode* activeMode = nullptr;
  CommandResult errRes;
  if (!checkModeGate(CommandId::CMD_FEED_RESUME, "resume", false, activeMode, errRes)) {
    return errRes;
  }

  MotionEngine& engine = MotionEngine::instance();
  if (engine.isFeedHold(AXIS_Z)) engine.resumeFeedHold(AXIS_Z);
  if (engine.isFeedHold(AXIS_X)) engine.resumeFeedHold(AXIS_X);
  return CommandResult::ok("resume");
}

CommandResult CoreCommandService::execute(const StepCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("step", "PERMISSION_DENIED");
  }
  IELSMode* activeMode = nullptr;
  CommandResult errRes;
  if (!checkModeGate(CommandId::CMD_STEP_CYCLE, "step", true, activeMode, errRes)) {
    return errRes;
  }
  // Multi-pass canned cycle step-advance pending IELSMode implementation (Refs Issue #10)
  return CommandResult::failed("step", "UNAVAILABLE");
}

CommandResult CoreCommandService::execute(const FeedNudgeCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "feedNudge", errRes)) return errRes;

  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("feedNudge", "PERMISSION_DENIED");
  }

  IELSMode* activeMode = nullptr;
  if (!checkModeGate(CommandId::CMD_FEED_NUDGE, "feedNudge", true, activeMode, errRes)) {
    return errRes;
  }

  if (!activeMode->setParam("nudge", static_cast<float>(cmd.delta))) {
    return CommandResult::failed("feedNudge", "ENGINE_REJECTED");
  }
  return CommandResult::ok("feedNudge");
}

CommandResult CoreCommandService::execute(const StopCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  // Emergency stop is unconditional across all sessions and states
  MotionEngine::instance().stopAll(true);
  return CommandResult::ok("stop");
}

CommandResult CoreCommandService::execute(const PingCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("ping");
}

CommandResult CoreCommandService::execute(const SimRpmCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "simRpm", errRes)) return errRes;

  ISpindleEncoderHAL* hal = nullptr;
  if (!checkSimGate(ctx, "simRpm", true, hal, errRes)) return errRes;

  hal->setSimulatedRpm(cmd.rpm);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simRpm");
}

CommandResult CoreCommandService::execute(const SimEncoderCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "simEncoder", errRes)) return errRes;

  ISpindleEncoderHAL* hal = nullptr;
  if (!checkSimGate(ctx, "simEncoder", cmd.active, hal, errRes)) return errRes;

  hal->setSimulationActive(cmd.active);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simEncoder");
}

CommandResult CoreCommandService::execute(const SimPulsesCmd& cmd, const core_client::ClientContext& ctx) {
  CommandResult errRes;
  if (!validateCmd(cmd, "simPulses", errRes)) return errRes;

  ISpindleEncoderHAL* hal = nullptr;
  if (!checkSimGate(ctx, "simPulses", true, hal, errRes)) return errRes;

  hal->injectSimulatedPulses(cmd.pulses);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simPulses");
}

} // namespace whatels::core
