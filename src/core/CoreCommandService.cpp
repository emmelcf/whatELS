#include "CoreCommandService.h"
#include "SettingsService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include <cmath>

namespace whatels::core {

CoreCommandService& CoreCommandService::instance() {
  static CoreCommandService s_instance;
  return s_instance;
}

bool CoreCommandService::canExecuteMotion(const core_client::ClientContext& ctx, String* errReason) const {
  // Check permission
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    if (errReason) *errReason = "PERMISSION_DENIED";
    return false;
  }

  // Check [SAFE-01]: Engine must be IDLE
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (errReason) *errReason = "MOTION_LOCKED";
    return false;
  }

  return true;
}

CommandResult CoreCommandService::execute(const StartFeedCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("startFeed", valErr ? valErr : "INVALID_PARAM");
  }

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("startFeed", err);
  }

  bool ok = false;
  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    ModeRegistry::activateMode(ModeIds::GEARBOX);
    gb->setActiveAxis(cmd.axis == AXIS_X ? "X" : "Z");
    ok = gb->startFeed(cmd.axis, cmd.syncSpindle, cmd.feedRate, cmd.direction, cmd.targetDistDu);
  } else if (cmd.syncSpindle) {
    SpindleSyncParams params;
    params.axis = cmd.axis;
    params.direction = cmd.direction;
    params.pitchDupr = cmd.feedRate;
    params.targetDistDu = cmd.targetDistDu;
    params.respectSoftLimits = true;
    ok = MotionEngine::instance().engageSpindleSync(params);
  } else {
    VelocityFeedParams params;
    params.axis = cmd.axis;
    params.direction = cmd.direction;
    params.feedRateDuPerMin = cmd.feedRate;
    params.targetDistDu = cmd.targetDistDu;
    params.respectSoftLimits = true;
    ok = MotionEngine::instance().engageVelocityFeed(params);
  }

  if (!ok) {
    return CommandResult::failed("startFeed", "ENGINE_REJECTED");
  }

  return CommandResult::ok("startFeed");
}

CommandResult CoreCommandService::execute(const JogStepCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogStep", valErr ? valErr : "INVALID_PARAM");
  }

  if (!ctx.hasPermission(core_client::PERM_JOG_PULSE)) {
    return CommandResult::failed("jogStep", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isAnyAxisMoving()) {
    return CommandResult::failed("jogStep", "MOTION_LOCKED");
  }

  int8_t dir = (cmd.distDu >= 0) ? 1 : -1;
  int32_t stepDu = std::abs(cmd.distDu);
  bool ok = MotionEngine::instance().startJog(cmd.axis, dir, stepDu);
  if (!ok) {
    return CommandResult::failed("jogStep", "JOG_REJECTED");
  }
  return CommandResult::ok("jogStep");
}

CommandResult CoreCommandService::execute(const JogHoldCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogHold", valErr ? valErr : "INVALID_PARAM");
  }

  if (!ctx.hasPermission(core_client::PERM_JOG_PULSE)) {
    return CommandResult::failed("jogHold", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isAnyAxisMoving()) {
    return CommandResult::failed("jogHold", "MOTION_LOCKED");
  }

  bool ok = MotionEngine::instance().startJog(cmd.axis, cmd.direction, 0); // Continuous
  if (!ok) {
    return CommandResult::failed("jogHold", "JOG_REJECTED");
  }
  return CommandResult::ok("jogHold");
}

CommandResult CoreCommandService::execute(const JogReleaseCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogRelease", valErr ? valErr : "INVALID_PARAM");
  }

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
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisZero", valErr ? valErr : "INVALID_PARAM");
  }

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("axisZero", err);
  }
  MotionEngine::instance().zeroAxis(cmd.axis);
  return CommandResult::ok("axisZero");
}

CommandResult CoreCommandService::execute(const AxisPosCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisPosDu", valErr ? valErr : "INVALID_PARAM");
  }

  String err;
  if (!canExecuteMotion(ctx, &err)) {
    return CommandResult::failed("axisPosDu", err);
  }
  MotionEngine::instance().setAxisPositionDu(cmd.axis, cmd.posDu);
  return CommandResult::ok("axisPosDu");
}

CommandResult CoreCommandService::execute(const AxisLimitCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisLimitDu", valErr ? valErr : "INVALID_PARAM");
  }

  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("axisLimitDu", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isMoving(cmd.axis)) {
    return CommandResult::failed("axisLimitDu", "MOTION_LOCKED");
  }
  LimitDirection dir = (cmd.limitBound == 2) ? LimitDirection::LIMIT_RIGHT_OR_DOWN : LimitDirection::LIMIT_LEFT_OR_UP;
  bool ok = SettingsService::setAxisLimitDu(cmd.axis, dir, cmd.coordDu);
  if (!ok) {
    return CommandResult::failed("axisLimitDu", "LIMIT_OUT_OF_BOUNDS");
  }
  return CommandResult::ok("axisLimitDu");
}

CommandResult CoreCommandService::execute(const AxisClearLimitCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisClearLimit", valErr ? valErr : "INVALID_PARAM");
  }

  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("axisClearLimit", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isMoving(cmd.axis)) {
    return CommandResult::failed("axisClearLimit", "MOTION_LOCKED");
  }
  LimitDirection dir = (cmd.limitBound == 2) ? LimitDirection::LIMIT_RIGHT_OR_DOWN : LimitDirection::LIMIT_LEFT_OR_UP;
  SettingsService::clearAxisLimit(cmd.axis, dir);
  return CommandResult::ok("axisClearLimit");
}

CommandResult CoreCommandService::execute(const HoldCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  // Feed hold is universally permitted across all sessions and pauses active axes
  if (MotionEngine::instance().isMoving(AXIS_X) && !MotionEngine::instance().isMoving(AXIS_Z)) {
    MotionEngine::instance().pauseFeedHold(AXIS_X);
  } else {
    MotionEngine::instance().pauseFeedHold(AXIS_Z);
  }
  return CommandResult::ok("hold");
}

CommandResult CoreCommandService::execute(const ResumeCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("resume", "PERMISSION_DENIED");
  }
  if (MotionEngine::instance().isFeedHold(AXIS_X) && !MotionEngine::instance().isFeedHold(AXIS_Z)) {
    MotionEngine::instance().resumeFeedHold(AXIS_X);
  } else {
    MotionEngine::instance().resumeFeedHold(AXIS_Z);
  }
  return CommandResult::ok("resume");
}

CommandResult CoreCommandService::execute(const StepCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  if (!ctx.hasPermission(core_client::PERM_MOTION_START)) {
    return CommandResult::failed("step", "PERMISSION_DENIED");
  }
  // Multi-pass canned cycle step-advance pending IELSMode implementation (Refs Issue #10)
  return CommandResult::failed("step", "UNAVAILABLE");
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
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("simRpm", valErr ? valErr : "INVALID_PARAM");
  }
  if (!ctx.hasPermission(core_client::PERM_SIMULATION)) {
    return CommandResult::failed("simRpm", "PERMISSION_DENIED");
  }
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->setSimulatedRpm(cmd.rpm);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simRpm");
}

CommandResult CoreCommandService::execute(const SimEncoderCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("simEncoder", valErr ? valErr : "INVALID_PARAM");
  }
  if (!ctx.hasPermission(core_client::PERM_SIMULATION)) {
    return CommandResult::failed("simEncoder", "PERMISSION_DENIED");
  }
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (cmd.active && (MotionEngine::instance().isAnyAxisMoving() || (hal && hal->getFilteredRpm() > 0))) {
    return CommandResult::failed("simEncoder", "SAFETY_INTERLOCK_ACTIVE");
  }
  if (hal) hal->setSimulationActive(cmd.active);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simEncoder");
}

CommandResult CoreCommandService::execute(const SimPulsesCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("simPulses", valErr ? valErr : "INVALID_PARAM");
  }
  if (!ctx.hasPermission(core_client::PERM_SIMULATION)) {
    return CommandResult::failed("simPulses", "PERMISSION_DENIED");
  }
  ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
  if (hal) hal->injectSimulatedPulses(cmd.pulses);
  MotionEngine::instance().touchSimHeartbeat();
  return CommandResult::ok("simPulses");
}

} // namespace whatels::core

