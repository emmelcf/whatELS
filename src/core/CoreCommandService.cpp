#include "CoreCommandService.h"
#include <cmath>

namespace whatels::core {

CoreCommandService& CoreCommandService::instance() {
  static CoreCommandService s_instance;
  return s_instance;
}

bool CoreCommandService::canExecuteMotion(const core_client::ClientContext& ctx, String* errReason) const {
  // Check permission
  if (!(ctx.effectivePerms & core_client::PERM_MOTION_START)) {
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

bool CoreCommandService::validateEnvelope(int32_t zStart, int32_t zEnd, int32_t xStart, int32_t xEnd, String* errReason) const {
  (void)zStart;
  (void)zEnd;
  (void)xStart;
  (void)xEnd;
  (void)errReason;
  // Limit boundary check logic here
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
  if (cmd.syncSpindle) {
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
    return CommandResult::failed("startFeed", "Engine rejected move");
  }

  return CommandResult::ok("startFeed");
}

CommandResult CoreCommandService::execute(const JogStepCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogStep", valErr ? valErr : "INVALID_PARAM");
  }

  if (!(ctx.effectivePerms & core_client::PERM_JOG_PULSE)) {
    return CommandResult::failed("jogStep", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isAnyAxisMoving()) {
    return CommandResult::failed("jogStep", "MOTION_LOCKED");
  }

  int8_t dir = (cmd.distDu >= 0) ? 1 : -1;
  int32_t stepDu = std::abs(cmd.distDu);
  bool ok = MotionEngine::instance().startJog(cmd.axis, dir, stepDu);
  if (!ok) {
    return CommandResult::failed("jogStep", "Jog step rejected");
  }
  return CommandResult::ok("jogStep");
}

CommandResult CoreCommandService::execute(const JogHoldCmd& cmd, const core_client::ClientContext& ctx) {
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogHold", valErr ? valErr : "INVALID_PARAM");
  }

  if (!(ctx.effectivePerms & core_client::PERM_JOG_PULSE)) {
    return CommandResult::failed("jogHold", "PERMISSION_DENIED");
  }

  if (MotionEngine::instance().isAnyAxisMoving()) {
    return CommandResult::failed("jogHold", "MOTION_LOCKED");
  }

  bool ok = MotionEngine::instance().startJog(cmd.axis, cmd.direction, 0); // Continuous
  if (!ok) {
    return CommandResult::failed("jogHold", "Jog hold rejected");
  }
  return CommandResult::ok("jogHold");
}

CommandResult CoreCommandService::execute(const JogReleaseCmd& cmd, const core_client::ClientContext& ctx) {
  (void)ctx;
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("jogRelease", valErr ? valErr : "INVALID_PARAM");
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
  (void)ctx;
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisLimitDu", valErr ? valErr : "INVALID_PARAM");
  }

  if (MotionEngine::instance().isMoving(cmd.axis)) {
    return CommandResult::failed("axisLimitDu", "MOTION_LOCKED");
  }
  size_t slot = (cmd.limitBound == 2) ? 1 : 0;
  const char* label = (cmd.limitBound == 2) ? "RIGHT" : "LEFT";
  bool ok = MotionEngine::instance().setSoftLimitDu(cmd.axis, slot, cmd.coordDu, label, true);
  if (!ok) {
    return CommandResult::failed("axisLimitDu", "Limit out of bounds");
  }
  return CommandResult::ok("axisLimitDu");
}

CommandResult CoreCommandService::execute(const AxisClearLimitCmd& cmd, const core_client::ClientContext& ctx) {
  (void)ctx;
  const char* valErr = nullptr;
  if (!cmd.validate(&valErr)) {
    return CommandResult::failed("axisClearLimit", valErr ? valErr : "INVALID_PARAM");
  }

  if (MotionEngine::instance().isMoving(cmd.axis)) {
    return CommandResult::failed("axisClearLimit", "MOTION_LOCKED");
  }
  size_t slot = (cmd.limitBound == 2) ? 1 : 0;
  MotionEngine::instance().getSoftLimits(cmd.axis).clearLimit(slot);
  return CommandResult::ok("axisClearLimit");
}

CommandResult CoreCommandService::execute(const HoldCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  // Coordinated radial retract: pause feed hold
  MotionEngine::instance().pauseFeedHold(AXIS_Z);
  return CommandResult::ok("hold");
}

CommandResult CoreCommandService::execute(const ResumeCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  MotionEngine::instance().resumeFeedHold(AXIS_Z);
  return CommandResult::ok("resume");
}

CommandResult CoreCommandService::execute(const StepCmd& cmd, const core_client::ClientContext& ctx) {
  (void)cmd;
  (void)ctx;
  // Step mode advance
  return CommandResult::ok("step");
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

} // namespace whatels::core

