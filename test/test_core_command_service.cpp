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

// Minimal test double for SettingsService active profile
static LatheConfiguration s_mockConfig;
const LatheConfiguration& SettingsService::getActiveProfile() {
  return s_mockConfig;
}

using namespace whatels::core;
using namespace whatels::core_client;

static void printHeader(const char* name) {
  std::cout << "\n==================================================\n";
  std::cout << " TEST: " << name << "\n";
  std::cout << "==================================================\n";
}

void testCommandStructsAndVariants() {
  printHeader("1. Command Structs & C++17 std::variant");

  StartCycleCmd cycleCmd;

  // Test Threading
  ThreadingCycleParams threadParams;
  threadParams.pitchDupr = 15000;
  threadParams.passes = 8;
  threadParams.depthDu = 9200;
  threadParams.zStartDu = 0;
  threadParams.zEndDu = -25000;
  threadParams.xClearDu = 20000;
  cycleCmd.params = threadParams;

  assert(std::holds_alternative<ThreadingCycleParams>(cycleCmd.params));
  auto& retrievedThread = std::get<ThreadingCycleParams>(cycleCmd.params);
  assert(retrievedThread.pitchDupr == 15000);
  assert(retrievedThread.passes == 8);

  // Test Turning
  TurningCycleParams turnParams;
  turnParams.feedDupr = 1500;
  turnParams.passes = 4;
  turnParams.docDu = 5000;
  turnParams.zEndDu = -50000;
  cycleCmd.params = turnParams;

  assert(std::holds_alternative<TurningCycleParams>(cycleCmd.params));
  auto& retrievedTurn = std::get<TurningCycleParams>(cycleCmd.params);
  assert(retrievedTurn.feedDupr == 1500);
  assert(retrievedTurn.docDu == 5000);

  std::cout << "  [PASS] Variant extraction and typed parameter integrity verified.\n";
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

  StartCycleCmd cycleCmd;
  ThreadingCycleParams threadParams;
  threadParams.pitchDupr = 15000;
  threadParams.passes = 8;
  threadParams.depthDu = 9200;
  threadParams.zStartDu = 0;
  threadParams.zEndDu = -35000; // Shoulder at -3.5 mm
  threadParams.xClearDu = 20000;
  cycleCmd.params = threadParams;

  CommandResult res = CoreCommandService::instance().execute(cycleCmd, ctx);
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
  assert(stepRes.isOk());
  std::cout << "  [PASS] CMD.step executed cleanly.\n";
}

int main() {
  std::cout << "\n======================================================\n";
  std::cout << " RUNNING CORE COMMAND SERVICE & SAFETY GATE UNIT TESTS\n";
  std::cout << "======================================================\n";

  testCommandStructsAndVariants();
  testSafe01MotionSafetyGate();
  testZeroGhostLimitsPersistence();
  testHoldAndResumeLifecycle();

  std::cout << "\n>>> ALL CORE COMMAND SERVICE TESTS PASSED SUCCESSFULLY! <<<\n\n";
  return 0;
}
