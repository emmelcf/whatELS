#include <cassert>
#include <cstdio>
#include <cstdlib>
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

UnifiedGearboxMode* ModeRegistry::getGearboxMode() {
  return nullptr;
}

bool ModeRegistry::activateMode(ModeId /*id*/) {
  return true;
}

bool UnifiedGearboxMode::setActiveAxis(const char* /*axisStr*/) {
  return true;
}

bool UnifiedGearboxMode::startFeed(AxisId /*axis*/, bool /*sync*/, int32_t /*feedRateDu*/, int8_t /*direction*/, int32_t /*targetDistDu*/) {
  return false;
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
    cmd.limitBound = 3; // Only bound 1 or 2 valid
    cmd.coordDu = 1000;
    CommandResult res = CoreCommandService::instance().execute(cmd, ctx);
    assert(!res.isOk());
    assert(res.payload == "INVALID_PARAM:bound");
  }

  std::cout << "  [PASS] Domain validation correctly rejects invalid/omitted physical parameters.\n";
}

void testSafe01MotionSafetyGate() {
  printHeader("2. [SAFE-01] Motion Safety Interlock & Permissions");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

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
}

void testZeroGhostLimitsPersistence() {
  printHeader("3. Zero Ghost Limits (ADR-15) Shoulder Limit Persistence");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  ClientContext ctx;
  ctx.effectivePerms = PERM_ALL;

  // Set shoulder limit directly via canonical AxisLimitCmd
  AxisLimitCmd limitCmd;
  limitCmd.axis = AXIS_Z;
  limitCmd.limitBound = 1; // Bound 1 = Lower / Chuck shoulder
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
  MotionEngine engine(&stepperHal, &encoderHal);

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

  std::cout << "  [PASS] Simulation commands enforce PERM_SIMULATION and execute cleanly.\n";
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

  std::cout << "\n>>> ALL CORE COMMAND SERVICE TESTS PASSED SUCCESSFULLY! <<<\n\n";
  return 0;
}
