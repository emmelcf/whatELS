#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <iostream>

#include "motion/kernel/MotionEngine.h"
#include "motion/hal/MockStepperHAL.h"
#include "motion/hal/MockSpindleEncoderHAL.h"
#include "motion/contract/MotionCommands.h"
#include "core/SettingsService.h"

// Minimal test double for SettingsService::getActiveProfile() used as fallback in MotionEngine
static LatheConfiguration s_mockConfig;
const LatheConfiguration& SettingsService::getActiveProfile() {
  return s_mockConfig;
}

// Test harness listener to record callbacks from MotionEngine
class TestMotionEventListener : public IMotionEventListener {
public:
  bool             halted           = false;
  AxisId           haltedAxis       = AXIS_Z;
  MotionHaltReason haltReason       = MotionHaltReason::NORMAL_STOP;
  int32_t          haltedPosSteps   = 0;
  int32_t          haltedCoordDu    = 0;
  const SoftLimit* triggeredLimit   = nullptr;
  uint32_t         telemetryCount   = 0;

  void reset() {
    halted         = false;
    haltedAxis     = AXIS_Z;
    haltReason     = MotionHaltReason::NORMAL_STOP;
    haltedPosSteps = 0;
    haltedCoordDu  = 0;
    triggeredLimit = nullptr;
    telemetryCount = 0;
  }

  void onMotionHalted(
      AxisId axis,
      MotionHaltReason reason,
      int32_t posSteps,
      int32_t coordDu,
      const SoftLimit* limit) override {
    halted         = true;
    haltedAxis     = axis;
    haltReason     = reason;
    haltedPosSteps = posSteps;
    haltedCoordDu  = coordDu;
    triggeredLimit = limit;
  }

  void onMotionTelemetry(
      int32_t zCoordDu,
      int32_t xCoordDu,
      uint32_t currentRpm,
      bool isMoving) override {
    telemetryCount++;
  }
};

static void printTestHeader(const char* name) {
  std::cout << "\n==================================================\n";
  std::cout << " RUNNING TEST: " << name << "\n";
  std::cout << "==================================================\n";
}

// -----------------------------------------------------------------------------
// Test 1: Spindle Sync Gearing (Zero Drift)
// -----------------------------------------------------------------------------
void testSpindleSyncGearing() {
  printTestHeader("1. Spindle Sync Gearing E2E (1 Rev = Exact Target Steps, Zero Drift)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  // Configure Axis Z: 4.0 mm screw lead (40,000 du), 1600 steps/rev motor
  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000;
  geomZ.motorStepsPerRev = 1600;
  geomZ.maxAccelMmSec2   = 200.0f;
  geomZ.maxVelocityMmSec = 50.0f;
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  // Initialize Spindle Encoder: 1024 PPR (4096 4x quadrature edges/rev)
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  // Commanded thread lead: 1.50 mm (15,000 dupr)
  // Expected steps per spindle revolution:
  // (1600 steps/rev * 15000 dupr) / 40000 du/rev = 600 steps
  FeedParameters feed;
  feed.axis          = AXIS_Z;
  feed.direction     = 1;
  feed.pitchDupr     = 15000;
  feed.targetDistDu  = -1; // Continuous
  bool started = engine.startSpindleSync(feed);
  assert(started);
  assert(engine.getActiveMode(AXIS_Z) == MotionMode::SPINDLE_SYNC_GEARING);
  assert(stepperHal.isEnabled(AXIS_Z));

  // Simulate 1 full spindle revolution (4,096 pulses) across 1,024 ticks (4 pulses/tick)
  for (int tick = 0; tick < 1024; ++tick) {
    encoderHal.injectPulses(4);
    encoderHal.setFilteredRpm(600);
    engine.onMotionTick();
  }

  int64_t totalSteps1 = stepperHal.getTotalSteps(AXIS_Z);
  int32_t posSteps1   = engine.getAxisPositionSteps(AXIS_Z);

  std::cout << "  -> After 1 full rev (4,096 pulses):" << std::endl;
  std::cout << "     Expected Stepper Pulses: 600" << std::endl;
  std::cout << "     Actual Stepper Pulses:   " << totalSteps1 << std::endl;
  std::cout << "     Axis Position (steps):   " << posSteps1 << std::endl;

  assert(totalSteps1 == 600);
  assert(posSteps1 == 600);
  assert(stepperHal.getForwardSteps(AXIS_Z) == 600);
  assert(stepperHal.getReverseSteps(AXIS_Z) == 0);

  // Simulate a 2nd full revolution (another 4,096 pulses)
  for (int tick = 0; tick < 1024; ++tick) {
    encoderHal.injectPulses(4);
    engine.onMotionTick();
  }

  int64_t totalSteps2 = stepperHal.getTotalSteps(AXIS_Z);
  int32_t posSteps2   = engine.getAxisPositionSteps(AXIS_Z);

  std::cout << "  -> After 2 full revs (8,192 pulses):" << std::endl;
  std::cout << "     Expected Stepper Pulses: 1200" << std::endl;
  std::cout << "     Actual Stepper Pulses:   " << totalSteps2 << std::endl;

  assert(totalSteps2 == 1200);
  assert(posSteps2 == 1200);

  std::cout << "  [PASS] Spindle Sync Gearing verified with ZERO step drift.\n";
}

// -----------------------------------------------------------------------------
// Test 2: Multi-Stop Shoulder Lookahead E2E
// -----------------------------------------------------------------------------
void testSoftLimitShoulderStop() {
  printTestHeader("2. Multi-Stop Shoulder Lookahead E2E (Clean Halt on Soft Limit)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);
  TestMotionEventListener listener;
  engine.setEventListener(&listener);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000; // 4 mm leadscrew
  geomZ.motorStepsPerRev = 1600;  // 1600 steps/rev -> 400 steps/mm
  geomZ.maxAccelMmSec2   = 200.0f;
  geomZ.maxVelocityMmSec = 50.0f;
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  // Set Soft Limit at +50,000 du (+5.0 mm = +2,000 steps)
  // 50,000 du * 1600 / 40000 = 2,000 steps
  SoftLimitTable& limits = engine.getSoftLimits(AXIS_Z);
  bool setOk = limits.setLimit(0, 50000, 2000, "SHOULDER_1", true, LimitBound::UPPER);
  assert(setOk);

  // Engage spindle sync feed toward tailstock (+1) with 1.0 mm pitch (10,000 dupr = 400 steps/rev)
  FeedParameters feed;
  feed.axis          = AXIS_Z;
  feed.direction     = 1;
  feed.pitchDupr     = 10000;
  feed.targetDistDu  = -1; // Unbounded, must halt at soft limit
  engine.startSpindleSync(feed);
  assert(engine.isMoving(AXIS_Z));

  // Run motion toward limit: 2000 steps requires 5 full revolutions = 20,480 pulses
  // Simulate pulses until halt or max ticks
  int ticks = 0;
  while (engine.isMoving(AXIS_Z) && ticks < 10000) {
    encoderHal.injectPulses(4);
    engine.onMotionTick();
    ticks++;
  }

  std::cout << "  -> Motion halted after " << ticks << " ticks." << std::endl;
  std::cout << "     Is Moving: " << (engine.isMoving(AXIS_Z) ? "true" : "false") << std::endl;
  std::cout << "     Final Position Steps: " << engine.getAxisPositionSteps(AXIS_Z) << std::endl;
  std::cout << "     Final Position du:    " << engine.getAxisPositionDu(AXIS_Z) << std::endl;

  assert(!engine.isMoving(AXIS_Z));
  assert(engine.getActiveMode(AXIS_Z) == MotionMode::STOPPED);
  assert(listener.halted == true);
  assert(listener.haltReason == MotionHaltReason::SOFT_LIMIT_REACHED);
  assert(listener.haltedPosSteps == 2000);
  assert(listener.haltedCoordDu == 50000);
  assert(listener.triggeredLimit != nullptr);
  assert(listener.triggeredLimit->id == 1);

  // Assert carriage NEVER advances past limit even if spindle continues turning
  int64_t stepsAtStop = stepperHal.getTotalSteps(AXIS_Z);
  for (int extra = 0; extra < 500; ++extra) {
    encoderHal.injectPulses(10);
    engine.onMotionTick();
  }
  assert(stepperHal.getTotalSteps(AXIS_Z) == stepsAtStop);
  assert(engine.getAxisPositionSteps(AXIS_Z) == 2000);

  std::cout << "  [PASS] Soft limit shoulder stop halted cleanly with REASON_SOFT_LIMIT at boundary.\n";
}

// -----------------------------------------------------------------------------
// Test 3: Live Pitch Trim Continuity E2E
// -----------------------------------------------------------------------------
void testLivePitchTrimContinuity() {
  printTestHeader("3. Live Pitch Trim Continuity E2E (CMD_FEED_NUDGE Without Jumps)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000; // 4.0 mm lead
  geomZ.motorStepsPerRev = 1600;  // 400 steps/mm
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  // Initial pitch: 1.0 mm (10,000 dupr -> 400 steps/rev)
  FeedParameters feed;
  feed.axis          = AXIS_Z;
  feed.direction     = 1;
  feed.pitchDupr     = 10000;
  feed.targetDistDu  = -1;
  engine.startSpindleSync(feed);

  // Feed 0.5 rev (2,048 pulses -> 200 steps)
  for (int i = 0; i < 512; ++i) {
    encoderHal.injectPulses(4);
    engine.onMotionTick();
  }

  int32_t stepsBeforeTrim = engine.getAxisPositionSteps(AXIS_Z);
  std::cout << "  -> Position before live trim: " << stepsBeforeTrim << " steps (expected 200)" << std::endl;
  assert(stepsBeforeTrim == 200);

  // Dispatch live pitch trim to 2.0 mm (20,000 dupr -> 800 steps/rev) via packet
  MotionCommandPacket pkt;
  pkt.type      = MotionCommandPacket::Type::UPDATE_FEED_PITCH;
  pkt.axis      = AXIS_Z;
  pkt.pitchDupr = 20000;
  bool queued = engine.postCommand(pkt);
  assert(queued);

  // Process tick to drain command packet and test continuity
  engine.onMotionTick();
  int32_t stepsImmediatelyAfter = engine.getAxisPositionSteps(AXIS_Z);
  std::cout << "  -> Position immediately after trim dispatch: " << stepsImmediatelyAfter << " steps" << std::endl;

  // Crucial invariant: zero position discontinuity at instant of trim!
  assert(stepsImmediatelyAfter == stepsBeforeTrim);

  // Feed another 0.5 rev (2,048 pulses) under new 2.0 mm lead (expected 400 additional steps)
  for (int i = 0; i < 512; ++i) {
    encoderHal.injectPulses(4);
    engine.onMotionTick();
  }

  int32_t finalSteps = engine.getAxisPositionSteps(AXIS_Z);
  std::cout << "  -> Position after post-trim 0.5 rev: " << finalSteps << " steps (expected 600)" << std::endl;

  // Total = 200 (first 0.5 rev @ 1.0mm) + 400 (second 0.5 rev @ 2.0mm) = 600 steps
  assert(finalSteps == 600);
  assert(stepperHal.getTotalSteps(AXIS_Z) == 600);

  std::cout << "  [PASS] Live pitch trim continuity verified without jumps or cumulative error.\n";
}

// -----------------------------------------------------------------------------
// Test 4: Inter-Core Command Queue Dispatch E2E
// -----------------------------------------------------------------------------
void testInterCoreCommandQueue() {
  printTestHeader("4. Inter-Core Command Queue E2E (Queue Buffering & Core 1 Drain)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000;
  geomZ.motorStepsPerRev = 1600;
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  assert(!engine.isMoving(AXIS_Z));

  // 1. Post START_SPINDLE_SYNC command
  MotionCommandPacket startPkt;
  startPkt.type          = MotionCommandPacket::Type::START_SPINDLE_SYNC;
  startPkt.axis          = AXIS_Z;
  startPkt.direction     = 1;
  startPkt.pitchDupr     = 15000;
  startPkt.targetCoordDu = -1;

  bool queued = engine.postCommand(startPkt);
  assert(queued);

  // Before onMotionTick() runs, motion must NOT have started yet (inter-core decoupling)
  assert(engine.getActiveMode(AXIS_Z) == MotionMode::STOPPED);
  assert(!engine.isMoving(AXIS_Z));

  // Core 1 onMotionTick() drains packet
  engine.onMotionTick();
  assert(engine.getActiveMode(AXIS_Z) == MotionMode::SPINDLE_SYNC_GEARING);
  assert(engine.isMoving(AXIS_Z));

  // 2. Post PAUSE_FEED_HOLD command
  MotionCommandPacket holdPkt;
  holdPkt.type = MotionCommandPacket::Type::PAUSE_FEED_HOLD;
  holdPkt.axis = AXIS_Z;
  engine.postCommand(holdPkt);

  assert(!engine.isFeedHold(AXIS_Z)); // Not active until tick
  engine.onMotionTick();
  assert(engine.isFeedHold(AXIS_Z));

  // 3. Post RESUME_FEED_HOLD command
  MotionCommandPacket resumePkt;
  resumePkt.type = MotionCommandPacket::Type::RESUME_FEED_HOLD;
  resumePkt.axis = AXIS_Z;
  engine.postCommand(resumePkt);

  assert(engine.isFeedHold(AXIS_Z)); // Still on hold until tick
  engine.onMotionTick();
  assert(!engine.isFeedHold(AXIS_Z));

  // 4. Post STOP_NORMAL command
  MotionCommandPacket stopPkt;
  stopPkt.type = MotionCommandPacket::Type::STOP_NORMAL;
  stopPkt.axis = AXIS_Z;
  engine.postCommand(stopPkt);

  assert(engine.isMoving(AXIS_Z)); // Still moving until tick
  engine.onMotionTick();
  assert(!engine.isMoving(AXIS_Z));
  assert(engine.getActiveMode(AXIS_Z) == MotionMode::STOPPED);

  std::cout << "  [PASS] Inter-core command queue safely buffers and Core 1 loop dispatches correctly.\n";
}

// -----------------------------------------------------------------------------
// Test 5: Hybrid Hardware Offload (VELOCITY_FEED & Continuous MANUAL_JOG)
// -----------------------------------------------------------------------------
void testHybridPulseTrainOffload() {
  printTestHeader("5. Hybrid Hardware Offload (VELOCITY_FEED & Continuous MANUAL_JOG)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000;
  geomZ.motorStepsPerRev = 1600; // 400 steps/mm
  geomZ.maxAccelMmSec2   = 100.0f;
  geomZ.maxVelocityMmSec = 25.0f;
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  VelocityFeedParams vf;
  vf.axis             = AXIS_Z;
  vf.direction        = 1;
  vf.feedRateDuPerMin = 600000; // 60 mm/min = 1 mm/s = 400 steps/sec
  vf.accelMmSec2      = 50.0f;
  vf.targetDistDu     = 10000;  // 1.0 mm = 400 steps
  assert(engine.engageVelocityFeed(vf));
  assert(stepperHal.isPulseTrainActive(AXIS_Z));
  assert(stepperHal.getTotalSteps(AXIS_Z) == 0); // Hardware pulse train, NOT software pulseStep()

  // Update velocity mid-flight via UPDATE_VELOCITY packet
  MotionCommandPacket velPkt;
  velPkt.type       = MotionCommandPacket::Type::UPDATE_VELOCITY;
  velPkt.axis       = AXIS_Z;
  velPkt.speedDuMin = 1200000; // 120 mm/min = 800 steps/sec
  assert(engine.postCommand(velPkt));

  for (int i = 0; i < 100; ++i) {
    engine.onMotionTick();
  }
  assert(engine.isMoving(AXIS_Z));
  assert(stepperHal.isPulseTrainActive(AXIS_Z));
  assert(stepperHal.getPulseFrequencyHz(AXIS_Z) > 50);
  assert(stepperHal.getTotalSteps(AXIS_Z) == 0); // Hardware pulse train, NOT software pulseStep()

  for (int i = 0; i < 2000 && engine.isMoving(AXIS_Z); ++i) {
    engine.onMotionTick();
  }
  assert(!engine.isMoving(AXIS_Z));
  assert(!stepperHal.isPulseTrainActive(AXIS_Z));

  std::cout << "  [PASS] VELOCITY_FEED offloaded to startPulseTrain/updatePulseFrequency/stopPulseTrain.\n";
}

// -----------------------------------------------------------------------------
// Test 6: 200 ms Jog Lease Watchdog
// -----------------------------------------------------------------------------
void testJogLeaseWatchdog() {
  printTestHeader("6. 200 ms Jog Lease Watchdog (Continuous MANUAL_JOG)");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000;
  geomZ.motorStepsPerRev = 1600;
  geomZ.maxVelocityMmSec = 10.0f;
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);

  assert(engine.startJog(AXIS_Z, 1, 0));
  assert(stepperHal.isPulseTrainActive(AXIS_Z));

  // Run 150 ms (< 200 ms) -> still active
  for (int i = 0; i < 150; ++i) {
    engine.onMotionTick();
  }
  assert(engine.isMoving(AXIS_Z));
  assert(stepperHal.isPulseTrainActive(AXIS_Z));

  // Renew lease via keepalive startJog(..., 0)
  assert(engine.startJog(AXIS_Z, 1, 0));
  for (int i = 0; i < 150; ++i) {
    engine.onMotionTick();
  }
  assert(engine.isMoving(AXIS_Z));

  // Expire 200 ms lease without keepalive
  for (int i = 0; i < 205; ++i) {
    engine.onMotionTick();
  }
  assert(!engine.isMoving(AXIS_Z));
  assert(!stepperHal.isPulseTrainActive(AXIS_Z));

  std::cout << "  [PASS] 200 ms Jog Lease Watchdog safely halts runaway continuous jog.\n";
}

// -----------------------------------------------------------------------------
// Test 7: Runway Verification, syncIndex, UPDATE_SOFT_LIMITS
// -----------------------------------------------------------------------------
void testRunwayAndSyncIndex() {
  printTestHeader("7. Runway Verification, syncIndex & UPDATE_SOFT_LIMITS");

  MockStepperHAL stepperHal;
  MockSpindleEncoderHAL encoderHal;
  MotionEngine engine(&stepperHal, &encoderHal);
  TestMotionEventListener listener;
  engine.setEventListener(&listener);

  AxisGeometry geomZ;
  geomZ.screwPitchDu     = 40000;
  geomZ.motorStepsPerRev = 1600;
  geomZ.maxAccelMmSec2   = 10.0f; // Low accel so high RPM runway check triggers
  StepTimingConfig timing;
  engine.configureAxis(AXIS_Z, geomZ, timing);
  engine.zeroAxis(AXIS_Z);
  encoderHal.init(1, 2, 3, 1024, 1.0f);
  encoderHal.init(1, 2, 3, 1024, 1.0f);

  // Set upper limit at +5,000 du (0.5 mm) via UPDATE_SOFT_LIMITS
  MotionCommandPacket limPkt;
  limPkt.type              = MotionCommandPacket::Type::UPDATE_SOFT_LIMITS;
  limPkt.axis              = AXIS_Z;
  limPkt.direction         = 1;
  limPkt.targetCoordDu     = 5000;
  limPkt.respectSoftLimits = true;
  assert(engine.postCommand(limPkt));
  engine.onMotionTick();

  // Feed high RPM (3000 RPM = 50 RPS; 2.0 mm pitch -> 100 mm/s; d_stop = 500 mm >> 0.5 mm)
  encoderHal.setFilteredRpm(3000);
  engine.onMotionTick();

  SpindleSyncParams sync;
  sync.axis              = AXIS_Z;
  sync.direction         = 1;
  sync.pitchDupr         = 20000;
  sync.respectSoftLimits = true;
  sync.syncIndex         = true;
  assert(!engine.engageSpindleSync(sync)); // Rejected by verifyRunwaySpindle!

  // Now test syncIndex waiting for index pulse at safe RPM
  encoderHal.setFilteredRpm(60);
  engine.onMotionTick();
  limPkt.targetCoordDu = 500000; // 50 mm runway
  assert(engine.postCommand(limPkt));
  engine.onMotionTick();

  assert(engine.engageSpindleSync(sync));
  encoderHal.injectPulses(100);
  engine.onMotionTick();
  assert(stepperHal.getTotalSteps(AXIS_Z) == 0); // Still waiting for index

  encoderHal.setIndexSynced(true);
  engine.onMotionTick(); // Acquires index sync
  encoderHal.injectPulses(40);
  engine.onMotionTick();
  assert(stepperHal.getTotalSteps(AXIS_Z) > 0); // Synced and stepping!

  std::cout << "  [PASS] Runway verification, syncIndex, and UPDATE_SOFT_LIMITS verified.\n";
}

int main() {
  std::cout << "========================================================\n";
  std::cout << " whatELS Phase 2 — Automated E2E Motion Simulation Suite\n";
  std::cout << "========================================================\n";

  testSpindleSyncGearing();
  testSoftLimitShoulderStop();
  testLivePitchTrimContinuity();
  testInterCoreCommandQueue();
  testHybridPulseTrainOffload();
  testJogLeaseWatchdog();
  testRunwayAndSyncIndex();

  std::cout << "\n========================================================\n";
  std::cout << " ALL 7 E2E INTEGRATION TESTS PASSED (100% SUCCESS)\n";
  std::cout << "========================================================\n";
  return 0;
}
