#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

#include "core/CoreCommands.h"
#include "core/CoreCommandService.h"
#include "core_client/ClientContext.h"
#include "motion/kernel/MotionEngine.h"
#include "motion/hal/MockStepperHAL.h"
#include "motion/hal/MockSpindleEncoderHAL.h"
#include "core/SettingsService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"

// Minimal test doubles for SettingsService and ModeRegistry
static LatheConfiguration s_mockConfig;
const LatheConfiguration& SettingsService::getActiveProfile() {
  return s_mockConfig;
}

bool SettingsService::setAxisLimitDu(AxisId axis, LimitDirection dir, long limitDu) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  const char* label = (dir == LIMIT_LEFT_OR_UP) ? "LEFT" : "RIGHT";
  return MotionEngine::instance().setSoftLimitDu(axis, slot, limitDu, label, true);
}

bool SettingsService::clearAxisLimit(AxisId axis, LimitDirection dir) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  MotionEngine::instance().getSoftLimits(axis).clearLimit(slot);
  return true;
}

bool SettingsService::requestIsOn(bool /*on*/) { return true; }
bool SettingsService::setFeedrateDupr(long /*dupr*/, bool /*preserveDirection*/) { return true; }
long SettingsService::getJogStepDu() { return 1000; }

static UnifiedGearboxMode s_gearboxMode;

UnifiedGearboxMode* ModeRegistry::getGearboxMode() {
  return &s_gearboxMode;
}

IELSMode* ModeRegistry::getActiveMode() {
  return &s_gearboxMode;
}

bool ModeRegistry::activateMode(ModeId /*id*/) {
  return true;
}

using namespace whatels::core;
using namespace whatels::core_client;

static void printHeader(const char* name) {
  std::cout << "\n==================================================\n";
  std::cout << " TEST: " << name << "\n";
  std::cout << "==================================================\n";
}

void testCommandValidationAndInvariants() {
  printHeader("1. Command Struct Domain Invariant Validation");

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  // StartFeedCmd validation tests (including default-initialized axis rejection)
  {
    StartFeedCmd uninitCmd;
    uninitCmd.direction = -1;
    uninitCmd.feedRate = 1500;
    CommandResult res = CoreCommandService::instance().execute(uninitCmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:axis");

    StartFeedCmd cmd;
    cmd.axis = (AxisId)99;
    cmd.direction = -1;
    cmd.feedRate = 1500;
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:axis");

    cmd.axis = AXIS_Z;
    cmd.direction = 0; // Invalid polarity
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:dir");

    cmd.direction = -1;
    cmd.feedRate = 0; // Zero feed rate invalid
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:feed");

    cmd.feedRate = 1500;
    cmd.targetDistDu = 0; // Stroke of 0 is invalid
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:dist");

    // Dual-axis (ZX) requires xDirection (-1 or +1) and taperRatioDu (> 0)
    cmd.targetDistDu = -1;
    cmd.isDualAxis = true;
    cmd.xDirection = 0;
    cmd.taperRatioDu = 1000;
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:xdir");

    cmd.xDirection = 1;
    cmd.taperRatioDu = 0;
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:taper");
  }

  // JogStepCmd validation tests
  {
    JogStepCmd cmd;
    cmd.axis = AXIS_Z;
    cmd.distDu = 0; // 0 distance jog step is invalid
    CommandResult res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:dist");

    cmd.axis = (AxisId)5;
    cmd.distDu = 100;
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:axis");

    cmd.axis = AXIS_Z;
    cmd.distDu = INT32_MIN; // INT32_MIN rejected to avoid abs() UB
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:dist");
  }

  // JogHoldCmd validation tests
  {
    JogHoldCmd cmd;
    cmd.axis = AXIS_Z;
    cmd.direction = 5; // Invalid polarity
    CommandResult res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:dir");
  }

  // AxisLimitCmd validation tests
  {
    AxisLimitCmd cmd;
    cmd.axis = AXIS_Z;
    cmd.limitBound = static_cast<LimitBound>(3); // Only LOWER/UPPER valid
    cmd.coordDu = 1000;
    CommandResult res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:bound");
  }

  // FeedNudgeCmd validation tests
  {
    FeedNudgeCmd cmd;
    cmd.delta = 0;
    CommandResult res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:delta");

    cmd.delta = 2;
    res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:delta");
  }

  std::cout << "  [PASS] Domain validation correctly rejects invalid/omitted physical parameters.\n";
}

void testSafe01MotionSafetyGate() {
  printHeader("2. [SAFE-01] Motion Safety Interlock & Permissions");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine::instance().setStepperHAL(&stepperHal);
  MotionEngine::instance().setEncoderHAL(&encoderHal);

  ClientContext rootCtx;
  rootCtx.effectivePerms = PERM_ALL;

  ClientContext readOnlyCtx;
  readOnlyCtx.effectivePerms = PERM_READ_TELEMETRY;

  // 1. Permission check: read-only client cannot start feed
  StartFeedCmd feedCmd;
  feedCmd.axis = AXIS_Z;
  feedCmd.direction = -1;
  feedCmd.feedRate = 1500;
  feedCmd.syncSpindle = false;

  CommandResult res1 = CoreCommandService::instance().execute(feedCmd, readOnlyCtx);
  assert(!res1.isOk());
  assert(res1.status == CommandStatus::FAILED);
  std::cout << "  [PASS] Read-only client correctly rejected: " << res1.payload << "\n";

  // 2. Permission check: read-only client cannot jog
  JogStepCmd jogCmd;
  jogCmd.axis = AXIS_Z;
  jogCmd.distDu = 1000;
  CommandResult res2 = CoreCommandService::instance().execute(jogCmd, readOnlyCtx);
  assert(!res2.isOk());
  std::cout << "  [PASS] Read-only jog correctly rejected: " << res2.payload << "\n";

  // 3. Emergency stop is ALWAYS permitted, even for read-only clients
  StopCmd stopCmd;
  CommandResult res3 = CoreCommandService::instance().execute(stopCmd, readOnlyCtx);
  assert(res3.isOk());
  std::cout << "  [PASS] Emergency stop permitted for all clients.\n";

  MotionEngine::instance().setStepperHAL(nullptr);
  MotionEngine::instance().setEncoderHAL(nullptr);
}

void testZeroGhostLimitsPersistence() {
  printHeader("3. Zero Ghost Limits (ADR-15) Shoulder Limit Persistence");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine::instance().setStepperHAL(&stepperHal);
  MotionEngine::instance().setEncoderHAL(&encoderHal);

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  // Set shoulder limit directly via canonical AxisLimitCmd
  AxisLimitCmd limitCmd;
  limitCmd.axis = AXIS_Z;
  limitCmd.limitBound = LimitBound::LOWER; // Lower / Chuck shoulder
  limitCmd.coordDu = -35000; // Shoulder at -3.5 mm

  CommandResult res = CoreCommandService::instance().execute(limitCmd, ctx);
  assert(res.isOk());

  // Verify that the shoulder limit persists directly in the controller's active soft limit table
  const SoftLimitTable& zLimits = MotionEngine::instance().getSoftLimits(AXIS_Z);
  const SoftLimit& slot0 = zLimits.getSlot(0);

  assert(slot0.isEnforced());
  assert(slot0.coordDu == -35000);
  std::cout << "  [PASS] Shoulder limit persisted to SoftLimitTable: slot0 coord=" 
            << slot0.coordDu << " du (" << slot0.label << ")\n";

  MotionEngine::instance().setStepperHAL(nullptr);
  MotionEngine::instance().setEncoderHAL(nullptr);
}

void testHoldAndResumeLifecycle() {
  printHeader("4. In-Flight Lifecycle: Hold and Resume");

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  HoldCmd holdCmd;
  CommandResult holdRes = CoreCommandService::instance().execute(holdCmd, ctx);
  assert(holdRes.isOk());
  std::cout << "  [PASS] CMD.hold executed cleanly.\n";

  ResumeCmd resumeCmd;
  CommandResult resumeRes = CoreCommandService::instance().execute(resumeCmd, ctx);
  assert(resumeRes.isOk());
  std::cout << "  [PASS] CMD.resume executed cleanly.\n";

  StepCmd stepCmd;
  CommandResult stepRes = CoreCommandService::instance().execute(stepCmd, ctx);
  assert(!stepRes.isOk());
  assert(stepRes.payload == "UNAVAILABLE");
  std::cout << "  [PASS] CMD.step cleanly reports UNAVAILABLE (pending Issue #10).\n";
}

void testSimulationCommandsAndPermissions() {
  printHeader("5. Simulation Commands (SimRpmCmd, SimEncoderCmd, SimPulsesCmd)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine::instance().setStepperHAL(&stepperHal);
  MotionEngine::instance().setEncoderHAL(&encoderHal);

  ClientContext simCtx;
  simCtx.effectivePerms = PERM_SIMULATION;

  ClientContext noSimCtx;
  noSimCtx.effectivePerms = PERM_READ_TELEMETRY;

  SimEncoderCmd encCmd{true, true};
  CommandResult denyRes = CoreCommandService::instance().execute(encCmd, noSimCtx);
  assert(!denyRes.isOk());
  assert(denyRes.payload == "PERMISSION_DENIED");

  CommandResult okEnc = CoreCommandService::instance().execute(encCmd, simCtx);
  assert(okEnc.isOk());

  SimRpmCmd rpmCmd{300.0f};
  CommandResult okRpm = CoreCommandService::instance().execute(rpmCmd, simCtx);
  assert(okRpm.isOk());

  SimPulsesCmd pulsesCmd{400};
  CommandResult okPulses = CoreCommandService::instance().execute(pulsesCmd, simCtx);
  assert(okPulses.isOk());

  // [SAFE-01] Interlock: real spindle RPM > 0 (simulation inactive) blocks SimEncoder, SimRpm, SimPulses
  encoderHal.setSimulationActive(false);
  encoderHal.setSimulatedRpm(0.0f);
  // Simulate real spindle RPM by setting filtered RPM via MockSpindleEncoderHAL
  encoderHal.setFilteredRpm(250);

  CommandResult blockEnc = CoreCommandService::instance().execute(encCmd, simCtx);
  assert(!blockEnc.isOk());
  assert(blockEnc.payload == "SAFETY_INTERLOCK_ACTIVE");

  CommandResult blockRpm = CoreCommandService::instance().execute(rpmCmd, simCtx);
  assert(!blockRpm.isOk());
  assert(blockRpm.payload == "SAFETY_INTERLOCK_ACTIVE");

  CommandResult blockPulses = CoreCommandService::instance().execute(pulsesCmd, simCtx);
  assert(!blockPulses.isOk());
  assert(blockPulses.payload == "SAFETY_INTERLOCK_ACTIVE");

  // Fail cleanly if encoder HAL is nullptr
  MotionEngine::instance().setEncoderHAL(nullptr);
  CommandResult noHalRpm = CoreCommandService::instance().execute(rpmCmd, simCtx);
  assert(!noHalRpm.isOk());
  assert(noHalRpm.payload == "UNAVAILABLE");
  MotionEngine::instance().setEncoderHAL(nullptr);
  MotionEngine::instance().setStepperHAL(nullptr);

  std::cout << "  [PASS] Simulation commands enforce PERM_SIMULATION, [SAFE-01] interlock, and null HAL check.\n";
}

void testDualAxisHoldAndFeedHoldMotionGate() {
  printHeader("6. Dual-Axis ZX Hold/Resume & FeedHold Motion Gate");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine& engine = MotionEngine::instance();
  engine.setStepperHAL(&stepperHal);
  engine.setEncoderHAL(&encoderHal);

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  // Start dual-axis ZX velocity feed
  StartFeedCmd zxCmd;
  zxCmd.axis = AXIS_Z;
  zxCmd.isDualAxis = true;
  zxCmd.direction = -1;
  zxCmd.xDirection = 1;
  zxCmd.taperRatioDu = 1000;
  zxCmd.syncSpindle = false;
  zxCmd.feedRate = 500000;

  CommandResult startRes = CoreCommandService::instance().execute(zxCmd, ctx);
  assert(startRes.isOk());
  assert(engine.isMoving(AXIS_Z));
  assert(engine.isMoving(AXIS_X));

  // Hold pauses BOTH Z and X
  CommandResult holdRes = CoreCommandService::instance().execute(HoldCmd{}, ctx);
  assert(holdRes.isOk());
  assert(engine.isFeedHold(AXIS_Z));
  assert(engine.isFeedHold(AXIS_X));

  // Stop active pulse trains so isAnyAxisMoving() becomes false while feedHold remains true
  engine.stopAll(false);
  engine.pauseFeedHold(AXIS_Z);
  engine.pauseFeedHold(AXIS_X);

  // canExecuteMotion() must block while either axis is in feed hold
  String err;
  assert(!CoreCommandService::instance().canExecuteMotion(ctx, &err));
  assert(err == "MOTION_LOCKED");

  // Resume clears BOTH Z and X feed holds
  s_gearboxMode.onEnter(&engine);
  CommandResult resumeRes = CoreCommandService::instance().execute(ResumeCmd{}, ctx);
  assert(resumeRes.isOk());
  assert(!engine.isFeedHold(AXIS_Z));
  assert(!engine.isFeedHold(AXIS_X));

  engine.setStepperHAL(nullptr);
  engine.setEncoderHAL(nullptr);

  std::cout << "  [PASS] Dual-axis ZX Hold/Resume and isFeedHold() motion gate verified.\n";
}

void testFeedNudgeAndModesContract() {
  printHeader("7. CMD.feedNudge & Modes Manifest / Gate Contract");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine& engine = MotionEngine::instance();
  engine.setStepperHAL(&stepperHal);
  engine.setEncoderHAL(&encoderHal);
  s_gearboxMode.onEnter(&engine);

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  ClientContext readOnlyCtx;
  readOnlyCtx.effectivePerms = PERM_READ_TELEMETRY;

  // Read-only client rejected
  CommandResult roRes = CoreCommandService::instance().execute(FeedNudgeCmd{1}, readOnlyCtx);
  assert(!roRes.isOk());
  assert(roRes.payload == "PERMISSION_DENIED");

  // Idle mode cannot nudge
  CommandResult idleRes = CoreCommandService::instance().execute(FeedNudgeCmd{1}, ctx);
  assert(!idleRes.isOk());
  assert(idleRes.payload == "ENGINE_REJECTED");

  // Start feed and nudge +1 / -1
  StartFeedCmd feedCmd;
  feedCmd.axis = AXIS_Z;
  feedCmd.direction = -1;
  feedCmd.syncSpindle = true;
  feedCmd.feedRate = 5000;
  assert(CoreCommandService::instance().execute(feedCmd, ctx).isOk());

  assert(CoreCommandService::instance().execute(FeedNudgeCmd{1}, ctx).isOk());
  float feedVal = 0.0f;
  assert(s_gearboxMode.getParam("feed", feedVal));
  assert(feedVal == 6000.0f);

  assert(CoreCommandService::instance().execute(FeedNudgeCmd{-1}, ctx).isOk());
  assert(s_gearboxMode.getParam("feed", feedVal));
  assert(feedVal == 5000.0f);

  // Dead idle-axis CMD_JOG exception removed: RUNNING mode denies CMD_JOG on idle axis X
  assert(!s_gearboxMode.canExecuteCommand(CommandId::CMD_JOG, ModeState::RUNNING, AXIS_X));

  // Verify ModeIds::CONE == 57 and GEARBOX_PARAMS canonical keys
  static_assert(ModeIds::CONE == 57, "ModeIds::CONE must be 57");
  const ModeManifest& manifest = s_gearboxMode.getManifest();
  assert(manifest.paramCount == 7);
  const char* expectedKeys[] = { "axis", "sync", "dir", "feed", "dist", "xdir", "taper" };
  for (uint8_t i = 0; i < manifest.paramCount; ++i) {
    assert(std::strcmp(manifest.params[i].key, expectedKeys[i]) == 0);
  }

  engine.stopAll(true);
  s_gearboxMode.onExit();
  engine.setStepperHAL(nullptr);
  engine.setEncoderHAL(nullptr);

  std::cout << "  [PASS] CMD.feedNudge, ModeIds::CONE=57, and GEARBOX_PARAMS alignment verified.\n";
}

int main() {
  std::cout << "\n======================================================\n";
  std::cout << " RUNNING CORE COMMAND SERVICE & SAFETY GATE UNIT TESTS\n";
  std::cout << "======================================================\n";

  testCommandValidationAndInvariants();
  testSafe01MotionSafetyGate();
  testZeroGhostLimitsPersistence();
  testHoldAndResumeLifecycle();
  testSimulationCommandsAndPermissions();
  testDualAxisHoldAndFeedHoldMotionGate();
  testFeedNudgeAndModesContract();

  std::cout << "\n>>> ALL CORE COMMAND SERVICE TESTS PASSED SUCCESSFULLY! <<<\n\n";
  return 0;
}
