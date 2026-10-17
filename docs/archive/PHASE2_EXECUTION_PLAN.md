# NanoELS Phase 2 — End-to-End Motion Integration, Stepper HAL & Legacy Decommissioning Plan

> **Status:** Completed & Archived Historical Runbook  
> **Target Subsystems:** Subsystem 1 (HAL Drivers), Subsystem 2 (Core 1 Real-Time Wiring), Subsystem 3 (Mode Migration & Legacy Retirement)  
> **Location:** `docs/archive/PHASE2_EXECUTION_PLAN.md`  
> **Platform:** ESP32-S3 (PlatformIO `pio run -e esp32s3`)

---

## 1. Overview & Strategy

With **Phase 1** complete, the modular kinematic kernel, thread synchronizer, multi-stop soft limit tables, and lock-free concurrency structures are fully implemented and verified at the unit math level.

**Phase 2** bridges this new motion core to physical reality and retires legacy technical debt across five sequentially dependent implementation slices:

```
┌────────────────────────────────────────────────────────────────────────┐
│                        PHASE 2 EXECUTION TIMELINE                      │
├─────────────────────┬─────────────────────┬────────────────────────────┤
│ Slice               │ Domain & Scope      │ Key Deliverables           │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 1 (Conv 1)    │ Doc Hygiene & E2E   │ Clean docs/motion,         │
│                     │ Simulation Harness  │ MockStepperHAL, MockEncoder,│
│                     │                     │ Automated E2E Test Suite   │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 2 (Conv 2)    │ Hardware Stepper    │ Esp32StepperHAL.h/.cpp,    │
│                     │ Driver (HAL Layer 1)│ GPTimer/RMT Pulse Trains,  │
│                     │                     │ Setup/Hold Timing Guard    │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 3 (Conv 3)    │ Core 1 Task Wiring  │ MotionEngine in main.cpp,  │
│                     │ & Live Telemetry    │ TelemetryMailbox -> WS,    │
│                     │                     │ Touch DRO Web Streaming    │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 4 (Conv 4)    │ Mode Primitives     │ AsyncPrimitive, Cone,      │
│                     │ Migration (Layer 3) │ GearX, GearZ refactored to │
│                     │                     │ IELSMode orchestrators     │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 5 (Conv 5)    │ Legacy Motion Core  │ Motion.cpp & Axis.cpp      │
│                     │ Decommissioning     │ Deletion, Zero Legacy      │
│                     │                     │ Globals, 100% Clean Build  │
└─────────────────────┴─────────────────────┴────────────────────────────┘
```

---

## 2. Slice Specifications & Acceptance Criteria

### Slice 1: Documentation Hygiene & Automated E2E Simulation Test Harness
- **Primary Goal:** Clean up documentation duplicates and build an end-to-end integration test harness that simulates real spindle rotation and asserts exact carriage stepper output with zero physical hardware required.
- **Governing Specs:** [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md), [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md)
- **Target Files:**
  - `docs/motion/`: Remove duplicate `phase1_argon_review.md` and `:Zone.Identifier`.
  - `docs/archive/reviews/argon/README.md`: Update archive index documenting that all Argon findings were resolved in Phase 1 commits `ec2753b` $\to$ `0fb6fb8`.
  - `src/motion/hal/MockStepperHAL.h`: Test double recording pulse counts, direction changes, setup delay compliance, and step intervals.
  - `src/motion/hal/MockSpindleEncoderHAL.h`: Test double injecting simulated quadrature pulses, index pulses, and variable RPM profiles.
  - `test/test_motion_e2e.cpp` (or runnable integration test):
    1. **Spindle Sync Gearing E2E:** Engage 1.5 mm pitch, feed 4,096 mock pulses (1 rev), assert exact resulting step count on `MockStepperHAL` with 0 step error.
    2. **Multi-Stop Shoulder Lookahead E2E:** Command feed towards a soft stop at +50,000 du; assert carriage halts cleanly at the shoulder with `onMotionHalted(REASON_SOFT_LIMIT)`.
    3. **Live Pitch Trim E2E:** Trigger `updatePitchLive()` during active pulse feeding; assert position continuity with zero step jumps.
    4. **Inter-Core Command Queue E2E:** Verify `postCommand(MotionCommandPacket)` correctly queues and transitions motion state.
- **Acceptance Criteria (DoD):**
  1. `docs/motion/` contains only clean, authoritative specification files.
  2. Automated E2E integration test runs and passes 100% of test cases.
  3. `pio run -e esp32s3` compiles cleanly with zero warnings or errors.

---

### Slice 2: Hardware Stepper Driver (`Esp32StepperHAL`)
- **Primary Goal:** Implement the concrete ESP32-S3 hardware stepper driver satisfying nanosecond timing rules, non-blocking enable delays, and coordinated dual-axis stepping.
- **Governing Specs:** [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md), [`StepTimingConfig.h`](../src/motion/hal/StepTimingConfig.h)
- **Target Files:**
  - `src/motion/hal/Esp32StepperHAL.h`: Concrete `IStepperHAL` declaration.
  - `src/motion/hal/Esp32StepperHAL.cpp`: ESP32-S3 timer-driven pulse generator (using ESP-IDF `gptimer` or dedicated high-resolution timer).
  - Enforces `dirSetupDelayNs`, `stepPulseWidthNs`, and `dirHoldTimeNs` from `StepTimingConfig`.
  - Enforces non-blocking driver enable settling (`enableDelayMs`) via timestamp gate before emitting pulses.
- **Acceptance Criteria (DoD):**
  1. Direction pin changes strictly obey `dirSetupDelayNs` before step pulse transition.
  2. Step pulses maintain calibrated `stepPulseWidthNs` duration.
  3. All driver methods and ISR routines marked `IRAM_ATTR`.
  4. Compiles with zero errors or warnings via `pio run -e esp32s3`.

---

### Slice 3: Core 1 Motion Task Wiring & Live Telemetry Streaming
- **Primary Goal:** Wire `MotionEngine` into `src/main.cpp` on Core 1 and stream `TelemetryMailbox` snapshots out to the WebSocket and Web UI.
- **Governing Specs:** [`CONCURRENCY_AND_TIMING.md`](./CONCURRENCY_AND_TIMING.md), [`../clients/ELS_CLIENT_ARCHITECTURE.md`](../clients/ELS_CLIENT_ARCHITECTURE.md)
- **Target Files:**
  - `src/main.cpp`: Initialize `MotionEngine` with `Esp32StepperHAL` and `Esp32SpindleEncoderHAL`. Start `MotionEngine::startTask()` pinned to Core 1 at priority 22.
  - `src/core/MachineModel.cpp`: Bridge `MachineModel` to sample kinematic position and state directly from `MotionEngine::getTelemetryMailbox()`.
  - `src/web/WebServerModule.cpp`: Stream tear-free telemetry snapshots over WebSocket (`UI.*`) at 50 Hz.
- **Acceptance Criteria (DoD):**
  1. Core 1 motion task runs deterministically without triggering FreeRTOS task watchdog.
  2. Web UI (`clients/web_ui/`) receives live carriage coordinates streamed from `TelemetryMailbox`.
  3. `pio run -e esp32s3` compiles cleanly with zero errors.

---

### Slice 4: Mode Primitives Migration to `IELSMode` Orchestration
- **Primary Goal:** Decouple remaining legacy primitives (`AsyncPrimitive`, `GearXPrimitive`, `GearZPrimitive`, `ConePrimitive`) from raw global step loops, migrating them to command `MotionEngine` primitives.
- **Governing Specs:** [`MODE_INTEGRATION_CONTRACT.md`](./MODE_INTEGRATION_CONTRACT.md), [`../modes/ELS_MODE_ARCHITECTURE.md`](../modes/ELS_MODE_ARCHITECTURE.md)
- **Target Files:**
  - `src/motion/modes/primitives/AsyncPrimitive.cpp/.h`: Command `MotionEngine::engageVelocityFeed()`.
  - `src/motion/modes/primitives/GearZPrimitive.cpp/.h`: Command `MotionEngine::engageSpindleSync()` for Z axis.
  - `src/motion/modes/primitives/GearXPrimitive.cpp/.h`: Command `MotionEngine::engageSpindleSync()` for X axis.
  - `src/motion/modes/primitives/ConePrimitive.cpp/.h`: Command `MotionEngine::engageSpindleSync()` with secondary axis ratio parameters (`enableSecondaryAxis = true`).
- **Acceptance Criteria (DoD):**
  1. All 4 mode primitives inherit from `IELSMode` and command `MotionEngine` with zero direct step generation loops.
  2. Mode switches via `ModeRegistry` successfully start and stop motion primitives.
  3. Compiles cleanly via `pio run -e esp32s3`.

---

### Slice 5: Legacy Motion Core Decommissioning & Cleanup
- **Primary Goal:** Eliminate legacy technical debt by safely deleting monolithic procedural files and cutting over remaining consumers.
- **Governing Specs:** [`NANOELS_MASTER_ARCHITECTURE.md`](../NANOELS_MASTER_ARCHITECTURE.md)
- **Target Files:**
  - Delete `src/motion/Motion.cpp` and `src/motion/Motion.h`.
  - Delete `src/motion/Axis.cpp` and `src/motion/Axis.h`.
  - `src/main.cpp`: Remove legacy `taskMoveZ`, `taskMoveX`, `async_timer`, and `motionMutex`.
  - Cut over any remaining callers in `Display.cpp`, `Input.cpp`, and `CommandDispatcher.cpp` to `MotionEngine` / `ModeRegistry`.
- **Acceptance Criteria (DoD):**
  1. Legacy `Motion.cpp`, `Motion.h`, `Axis.cpp`, `Axis.h` completely removed from filesystem.
  2. Zero legacy global motion variables (`z`, `x`, `motionMutex`, `isOn`, `dupr`) remaining.
  3. Complete firmware builds cleanly via `pio run -e esp32s3` with zero errors or warnings.

---

## 3. Starter Kickoff Prompts (For New Conversations)

Copy and paste the appropriate prompt into each new chat session:

### Starter Prompt: Slice 1
```markdown
We are executing Slice 1 of Phase 2 (Doc Hygiene & Automated E2E Simulation Test Harness) per `docs/motion/PHASE2_EXECUTION_PLAN.md`.

1. Review Section 2 (Slice 1 Scope) of `docs/motion/PHASE2_EXECUTION_PLAN.md`.
2. Clean up duplicate docs (`docs/motion/phase1_argon_review.md` and `:Zone.Identifier`) and update `docs/archive/reviews/argon/README.md`.
3. Present the proposed implementation for `MockStepperHAL`, `MockSpindleEncoderHAL`, and the automated E2E test suite for alignment before writing code.
4. Implement the mock HALs and E2E integration test verifying:
   - Spindle sync gearing (1 rev = exact target steps, zero drift).
   - Soft limit shoulder stop (halting cleanly on stop with REASON_SOFT_LIMIT).
   - Live pitch trim continuity without jumps.
   - Inter-core command queue dispatch.
5. Verify clean build and test execution via `pio run -e esp32s3`.
6. Commit the milestone (`test(motion): implement automated E2E simulation harness and clean doc archive`).
7. Update Section 4 in `docs/motion/PHASE2_EXECUTION_PLAN.md` with commit hash, verified tests, and notes.
```

### Starter Prompt: Slice 2
```markdown
We are executing Slice 2 of Phase 2 (Hardware Stepper Driver Esp32StepperHAL) per `docs/motion/PHASE2_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE2_EXECUTION_PLAN.md` to check completed state from Slice 1.
2. Review Section 2 (Slice 2 Scope) along with `docs/motion/HAL_SPECIFICATION.md` and `src/motion/hal/StepTimingConfig.h`.
3. Present your proposed implementation for `Esp32StepperHAL.h/.cpp` (GPTimer configuration, direction setup timing guard, non-blocking enable settling) for alignment before writing code.
4. Implement `Esp32StepperHAL` under `src/motion/hal/`.
5. Verify clean compilation via `pio run -e esp32s3`.
6. Commit the milestone (`feat(hal): implement concrete Esp32StepperHAL with hardware timer and timing guards`).
7. Update Section 4 in `docs/motion/PHASE2_EXECUTION_PLAN.md` with commit hash, verification results, and notes.
```

### Starter Prompt: Slice 3
```markdown
We are executing Slice 3 of Phase 2 (Core 1 Motion Task Wiring & Live Telemetry Streaming) per `docs/motion/PHASE2_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE2_EXECUTION_PLAN.md` to check completed state from Slices 1 & 2.
2. Review Section 2 (Slice 3 Scope) along with `docs/motion/CONCURRENCY_AND_TIMING.md`.
3. Present your proposed integration in `src/main.cpp`, `MachineModel.cpp`, and `WebServerModule.cpp` for alignment before editing code.
4. Launch `MotionEngine::startTask()` pinned to Core 1 and connect `TelemetryMailbox` snapshots to WebSocket streaming.
5. Verify clean compilation via `pio run -e esp32s3` and preview Web UI.
6. Commit the milestone (`feat(motion): wire MotionEngine to Core 1 and stream TelemetryMailbox to Web UI`).
7. Update Section 4 in `docs/motion/PHASE2_EXECUTION_PLAN.md` with commit hash, verification results, and notes.
```

### Starter Prompt: Slice 4
```markdown
We are executing Slice 4 of Phase 2 (Mode Primitives Migration to IELSMode Orchestration) per `docs/motion/PHASE2_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE2_EXECUTION_PLAN.md` to check completed state from Slices 1, 2, & 3.
2. Review Section 2 (Slice 4 Scope) along with `docs/motion/MODE_INTEGRATION_CONTRACT.md`.
3. Present your proposed refactoring for `AsyncPrimitive`, `GearXPrimitive`, `GearZPrimitive`, and `ConePrimitive` for alignment before editing code.
4. Refactor the primitives to command `MotionEngine` primitives (`engageSpindleSync`, `engageVelocityFeed`) without legacy busy loops.
5. Verify clean compilation via `pio run -e esp32s3`.
6. Commit the milestone (`feat(modes): migrate remaining mode primitives to MotionEngine orchestration`).
7. Update Section 4 in `docs/motion/PHASE2_EXECUTION_PLAN.md` with commit hash, verification results, and notes.
```

### Starter Prompt: Slice 5
```markdown
We are executing Slice 5 of Phase 2 (Legacy Motion Core Decommissioning) per `docs/motion/PHASE2_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE2_EXECUTION_PLAN.md` to verify all previous slices are complete.
2. Review Section 2 (Slice 5 Scope).
3. Present your audit of all remaining references to `Motion.h` and `Axis.h` across `src/` for alignment before deleting files.
4. Remove `src/motion/Motion.cpp`, `src/motion/Motion.h`, `src/motion/Axis.cpp`, and `src/motion/Axis.h`.
5. Remove legacy `taskMoveZ`, `taskMoveX`, `async_timer`, and `motionMutex` from `src/main.cpp`.
6. Verify full firmware compilation cleanly via `pio run -e esp32s3`.
7. Commit the milestone (`refactor(motion): decommission legacy Motion.cpp, Axis.cpp, and obsolete motion globals`).
8. Update Section 4 in `docs/motion/PHASE2_EXECUTION_PLAN.md` with final Phase 2 completion summary.
```

---

## 4. Living Execution Log & Handover Ledger

### Status Tracker

| Slice | Status | Target Date | Commit Hash | Verified By | Notes / Deviations |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Slice 1: Doc Hygiene & E2E Simulation Harness** | `COMPLETE` | 2026-10-16 | `11dd2f6` | PlatformIO + E2E Harness | Zero warnings, zero errors. 100% pass on all 4 E2E test suites. |
| **Slice 2: Hardware Stepper Driver (Esp32StepperHAL)** | `COMPLETE` | 2026-10-16 | `ce21bce` | `pio run -e esp32s3` | Concrete GPTimer pulse train, nanosecond timing guards, non-blocking enable settling. |
| **Slice 3: Core 1 Task Wiring & Live Telemetry** | `COMPLETE` | 2026-10-16 | `4a0e289` | `pio run -e esp32s3` + Web UI Preview | MotionEngine on Core 1 (prio 22), TelemetryMailbox seqlock bridge, 50 Hz WebSocket stream. |
| **Slice 4: Mode Primitives Migration** | `COMPLETE` | 2026-10-16 | `54ba844` | `pio run -e esp32s3` + E2E Harness | Async, GearZ, GearX, Cone migrated to MotionEngine orchestration; zero legacy busy loops. |
| **Slice 5: Legacy Motion Core Decommissioning** | `COMPLETE` | 2026-10-16 | `3719431` | PlatformIO + E2E Harness | Fully deleted Motion.cpp, Motion.h, Axis.cpp, Axis.h; removed legacy tasks, globals, and procedural loops. |

---

### Detailed Session Logs

#### Slice 1 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `11dd2f6`
- **Files Created / Modified:**
  - `docs/motion/` (removed duplicate `phase1_argon_review.md` and `:Zone.Identifier`)
  - `docs/archive/reviews/argon/README.md` (created review index documenting resolution of all findings in Phase 1)
  - `src/motion/hal/MockStepperHAL.h` (test double recording pulse counts, directions, and pulse trains)
  - `src/motion/hal/MockSpindleEncoderHAL.h` (test double simulating quadrature pulses, angular degrees, and RPM)
  - `src/motion/contract/TelemetryMailbox.h` (guarded `esp_attr.h` for portable host compilation)
  - `src/motion/kernel/MotionEngine.h` (added host command queue fallback and FreeRTOS type aliases)
  - `src/motion/kernel/MotionEngine.cpp` (buffered command queue and step tracking loop in `SPINDLE_SYNC_GEARING`)
  - `test/test_motion_e2e.cpp` (automated 4-suite integration test harness)
- **Verification Results:**
  - **Automated E2E Simulation Test:** Passed 100% across all 4 test suites:
    1. *Spindle Sync Gearing:* 1 rev = 600 steps, 2 revs = 1200 steps with 0 step error / zero drift.
    2. *Multi-Stop Shoulder Lookahead:* Halted cleanly at +50,000 du with `REASON_SOFT_LIMIT` (`SOFT_LIMIT_REACHED`).
    3. *Live Pitch Trim:* Smooth slope change from 1.0 mm to 2.0 mm pitch without step jumps or position discontinuity.
    4. *Inter-Core Command Queue:* Verified Core 0 queue buffering and Core 1 `onMotionTick()` drain and state transitions.
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.9% (61,920 bytes), Flash: 20.2% (1,325,997 bytes).
- **Key Architectural Decisions & Deviations:**
  - Created reusable `MockStepperHAL` and `MockSpindleEncoderHAL` test doubles under `src/motion/hal/` conforming to `IStepperHAL` and `ISpindleEncoderHAL`.
  - Added clean fallback queue buffering in `MotionEngine` when running on host platforms without FreeRTOS (`std::queue<MotionCommandPacket>`), ensuring identical queuing semantics between host unit simulation and dual-core ESP32-S3 firmware.
  - Updated `SPINDLE_SYNC_GEARING` step emission in `MotionEngine::onMotionTick()` to dispatch multi-pulse steps within each tick without lag.

#### Slice 2 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `ce21bce`
- **Files Created / Modified:**
  - `src/motion/hal/Esp32StepperHAL.h` (concrete `IStepperHAL` implementation with dual-axis hardware descriptors and `gptimer` callbacks)
  - `src/motion/hal/Esp32StepperHAL.cpp` (hardware pulse generator with `gptimer` allocation, nanosecond timing guards, and non-blocking enable settling)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 18.9% (used 61,920 bytes from 327,680 bytes)
    - Flash: 20.2% (used 1,325,997 bytes from 6,553,600 bytes)
- **Key Architectural Decisions & Deviations:**
  - Configured independent ESP-IDF `gptimer` hardware peripherals per axis (1 MHz resolution, 1 tick = 1 µs) for hardware-timed asynchronous pulse trains.
  - Enforced `dirHoldTimeNs` in `setDirection()` and `dirSetupDelayNs` in `pulseStep()` using microsecond bounded delays (`esp_rom_delay_us`) to prevent drive edge miscounts during reversals.
  - Enforced non-blocking driver enable settling (`enableDelayMs`) via timestamp gate (`esp_timer_get_time()`) without blocking the caller.
  - Placed critical pulse emission paths and ISR callbacks in `IRAM_ATTR` for deterministic real-time execution.

#### Slice 3 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `4a0e289`
- **Files Created / Modified:**
  - `src/main.cpp` (initialized `Esp32StepperHAL` and `Esp32SpindleEncoderHAL`, injected into `MotionEngine`, configured axis geometries from `activeMachineProfile`, and launched `MotionEngine::startTask(22, 1)` on Core 1)
  - `src/core/MachineModel.cpp` (bridged `MachineModel::captureKinematics()` and `publishMotionSnapshot()` to sample `TelemetryMailbox` seqlock directly, guaranteeing tear-free real-time position reads)
  - `src/web/WebServerModule.cpp` (tuned telemetry push ceiling `MIN_PUSH_INTERVAL_MS` to 20 ms for 50 Hz push cadence)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 19.0% (used 62,184 bytes from 327,680 bytes)
    - Flash: 20.4% (used 1,334,197 bytes from 6,553,600 bytes)
  - **Automated E2E Simulation Test:** Passed 100% across all 4 test suites with zero drift.
  - **Web UI Verification:** 800×480 touch display layout previewed cleanly with zero console errors.
- **Key Architectural Decisions & Deviations:**
  - Initialized concrete HAL instances (`s_stepperHal`, `s_encoderHal`) in `main.cpp` using pins from `Config.h` and parameters from `activeMachineProfile`.
  - Started `MotionEngine` pinned to Core 1 at priority 22 with 12 KB stack.
  - Integrated lock-free seqlock read in `captureKinematics()` preserving precedence for physical DRO scale modes when enabled (`isZDroMode()` / `isXDroMode()`).
  - Increased WebSocket telemetry dispatch cadence ceiling to 50 Hz (20 ms interval) while maintaining quiescence during machine idle.

#### Slice 4 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `54ba844`
- **Files Created / Modified:**
  - `src/motion/kernel/MotionEngine.cpp` (enhanced `startSpindleSync()` to engage secondary axis synchronizer and step generator when `enableSecondaryAxis` is true)
  - `src/motion/modes/primitives/AsyncPrimitive.h/.cpp` (refactored to command `MotionEngine::engageVelocityFeed()` without `async_timer` or legacy procedural loops)
  - `src/motion/modes/primitives/GearZPrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` for AXIS_Z without `Motion.h` globals)
  - `src/motion/modes/primitives/GearXPrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` for AXIS_X without `Motion.h` globals)
  - `src/motion/modes/primitives/ConePrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` with secondary axis taper ratio parameters)
  - `src/motion/modes/ModeRegistry.cpp` (registered all 4 primitives in `ModeRegistry::initStandardModes()`)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 19.0% (used 62,272 bytes from 327,680 bytes)
    - Flash: 20.5% (used 1,341,249 bytes from 6,553,600 bytes)
  - **Automated E2E Simulation Test:** All 4 test suites passed with 100% success.
- **Key Architectural Decisions & Deviations:**
  - Completely severed all 4 primitives from `Motion.h`, `Axis.h`, and procedural globals (`dupr`, `z`, `x`, `spindlePosAvg`, `stepToContinuous`).
  - Reused `MotionConversions.h` for direction mapping and axis token parsing across all primitives.
  - Implemented `onMotionHalted()` in all 4 primitives to handle automatic state transitions on soft limits (`SOFT_LIMIT_REACHED`) or stroke completions (`TARGET_DISTANCE_REACHED`).

#### Slice 5 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `3719431`
- **Files Deleted:**
  - `src/motion/Motion.cpp` (monolithic legacy motion file, 1,475 lines deleted)
  - `src/motion/Motion.h` (legacy procedural headers and externs deleted)
  - `src/motion/Axis.cpp` (legacy procedural stepper and PCNT driver, 858 lines deleted)
  - `src/motion/Axis.h` (legacy struct Axis and globals deleted)
- **Files Created / Decoupled:**
  - `src/core/OperatorIntent.h` (created authoritative container for thread format, pitch intent, and cycle variables)
  - `src/motion/kernel/AxisId.h` (created pure identifier header `enum AxisId : uint8_t { AXIS_Z = 0, AXIS_X = 1 }`)
  - `src/main.cpp` (removed `async_timer`, `motionMutex`, `initAxis`, `taskMoveZ`, `taskMoveX`, `taskGcode`, `taskAttachInterrupts`, and all legacy stepping loops; streamlined `loop()` to supervisory latch application and 50 Hz telemetry snapshot publishing)
  - `src/display/Display.cpp/.h` & `src/display/DisplayModel.cpp/.h` (completely decoupled from `Motion.h` / `Axis.h`; hash computation converted to `MotionEngine` and `SettingsService` getters)
  - `src/input/Input.cpp/.h` (decoupled from `Axis.h` / `Motion.h`, routed all jog, origin, and mode toggles through `MotionEngine` and `SettingsService`, bridged spindle encoder delta to `TelemetryMailbox`)
  - `src/web/CommandDispatcher.cpp` & `src/web/WebServerModule.cpp/.h` (routed all wire commands directly to `SettingsService` and `MotionEngine`)
  - `src/core/SettingsService.cpp/.h`, `src/core/Config.cpp/.h`, `src/core/MachineModel.cpp/.h` (cleared legacy coupling)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` passed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 18.8% (used 61,736 bytes from 327,680 bytes)
    - Flash: 20.1% (used 1,314,113 bytes from 6,553,600 bytes)
  - **Automated E2E Simulation Test:** Passed 100% across all 4 test suites:
    1. Spindle Sync Gearing: 0 step drift across 2 revs.
    2. Multi-Stop Shoulder Lookahead: Clean halt at soft limit.
    3. Live Pitch Trim Continuity: Clean trim without discontinuities.
    4. Inter-Core Command Queue: Deterministic buffering and Core 1 dispatch.
- **Key Architectural Decisions & Deviations:**
  - Deleted 3,468 lines of technical debt and retired all legacy global motion variables (`z`, `x`, `motionMutex`, `isOn`, `dupr`, `starts`, `turnPasses`, `coneRatio`, `auxForward`).
  - Converted Core 0 `loop()` into a lean supervisory loop running `SettingsService::applyCore1PendingLatches()`, `processSpindleCounter()`, and 50 Hz snapshot publication, letting Core 1 `MotionEngine` handle 100% of real-time kinematics.
  - Successfully achieved full completion of Phase 2 (Legacy Motion Core Decommissioning).


---

### Detailed Session Logs

#### Slice 1 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `11dd2f6`
- **Files Created / Modified:**
  - `docs/motion/` (removed duplicate `phase1_argon_review.md` and `:Zone.Identifier`)
  - `docs/archive/reviews/argon/README.md` (created review index documenting resolution of all findings in Phase 1)
  - `src/motion/hal/MockStepperHAL.h` (test double recording pulse counts, directions, and pulse trains)
  - `src/motion/hal/MockSpindleEncoderHAL.h` (test double simulating quadrature pulses, angular degrees, and RPM)
  - `src/motion/contract/TelemetryMailbox.h` (guarded `esp_attr.h` for portable host compilation)
  - `src/motion/kernel/MotionEngine.h` (added host command queue fallback and FreeRTOS type aliases)
  - `src/motion/kernel/MotionEngine.cpp` (buffered command queue and step tracking loop in `SPINDLE_SYNC_GEARING`)
  - `test/test_motion_e2e.cpp` (automated 4-suite integration test harness)
- **Verification Results:**
  - **Automated E2E Simulation Test:** Passed 100% across all 4 test suites:
    1. *Spindle Sync Gearing:* 1 rev = 600 steps, 2 revs = 1200 steps with 0 step error / zero drift.
    2. *Multi-Stop Shoulder Lookahead:* Halted cleanly at +50,000 du with `REASON_SOFT_LIMIT` (`SOFT_LIMIT_REACHED`).
    3. *Live Pitch Trim:* Smooth slope change from 1.0 mm to 2.0 mm pitch without step jumps or position discontinuity.
    4. *Inter-Core Command Queue:* Verified Core 0 queue buffering and Core 1 `onMotionTick()` drain and state transitions.
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.9% (61,920 bytes), Flash: 20.2% (1,325,997 bytes).
- **Key Architectural Decisions & Deviations:**
  - Created reusable `MockStepperHAL` and `MockSpindleEncoderHAL` test doubles under `src/motion/hal/` conforming to `IStepperHAL` and `ISpindleEncoderHAL`.
  - Added clean fallback queue buffering in `MotionEngine` when running on host platforms without FreeRTOS (`std::queue<MotionCommandPacket>`), ensuring identical queuing semantics between host unit simulation and dual-core ESP32-S3 firmware.
  - Updated `SPINDLE_SYNC_GEARING` step emission in `MotionEngine::onMotionTick()` to dispatch multi-pulse steps within each tick without lag.

#### Slice 2 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `ce21bce`
- **Files Created / Modified:**
  - `src/motion/hal/Esp32StepperHAL.h` (concrete `IStepperHAL` implementation with dual-axis hardware descriptors and `gptimer` callbacks)
  - `src/motion/hal/Esp32StepperHAL.cpp` (hardware pulse generator with `gptimer` allocation, nanosecond timing guards, and non-blocking enable settling)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 18.9% (used 61,920 bytes from 327,680 bytes)
    - Flash: 20.2% (used 1,325,997 bytes from 6,553,600 bytes)
- **Key Architectural Decisions & Deviations:**
  - Configured independent ESP-IDF `gptimer` hardware peripherals per axis (1 MHz resolution, 1 tick = 1 µs) for hardware-timed asynchronous pulse trains.
  - Enforced `dirHoldTimeNs` in `setDirection()` and `dirSetupDelayNs` in `pulseStep()` using microsecond bounded delays (`esp_rom_delay_us`) to prevent drive edge miscounts during reversals.
  - Enforced non-blocking driver enable settling (`enableDelayMs`) via timestamp gate (`esp_timer_get_time()`) without blocking the caller.
  - Placed critical pulse emission paths and ISR callbacks in `IRAM_ATTR` for deterministic real-time execution.

#### Slice 3 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `4a0e289`
- **Files Created / Modified:**
  - `src/main.cpp` (initialized `Esp32StepperHAL` and `Esp32SpindleEncoderHAL`, injected into `MotionEngine`, configured axis geometries from `activeMachineProfile`, and launched `MotionEngine::startTask(22, 1)` on Core 1)
  - `src/core/MachineModel.cpp` (bridged `MachineModel::captureKinematics()` and `publishMotionSnapshot()` to sample `TelemetryMailbox` seqlock directly, guaranteeing tear-free real-time position reads)
  - `src/web/WebServerModule.cpp` (tuned telemetry push ceiling `MIN_PUSH_INTERVAL_MS` to 20 ms for 50 Hz push cadence)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 19.0% (used 62,184 bytes from 327,680 bytes)
    - Flash: 20.4% (used 1,334,197 bytes from 6,553,600 bytes)
  - **Automated E2E Simulation Test:** Passed 100% across all 4 test suites with zero drift.
  - **Web UI Verification:** 800×480 touch display layout previewed cleanly with zero console errors.
- **Key Architectural Decisions & Deviations:**
  - Initialized concrete HAL instances (`s_stepperHal`, `s_encoderHal`) in `main.cpp` using pins from `Config.h` and parameters from `activeMachineProfile`.
  - Started `MotionEngine` pinned to Core 1 at priority 22 with 12 KB stack.
  - Integrated lock-free seqlock read in `captureKinematics()` preserving precedence for physical DRO scale modes when enabled (`isZDroMode()` / `isXDroMode()`).
  - Increased WebSocket telemetry dispatch cadence ceiling to 50 Hz (20 ms interval) while maintaining quiescence during machine idle.

#### Slice 4 Execution Log
- **Date Completed:** 2026-10-16
- **Commit:** `54ba844`
- **Files Created / Modified:**
  - `src/motion/kernel/MotionEngine.cpp` (enhanced `startSpindleSync()` to engage secondary axis synchronizer and step generator when `enableSecondaryAxis` is true)
  - `src/motion/modes/primitives/AsyncPrimitive.h/.cpp` (refactored to command `MotionEngine::engageVelocityFeed()` without `async_timer` or legacy procedural loops)
  - `src/motion/modes/primitives/GearZPrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` for AXIS_Z without `Motion.h` globals)
  - `src/motion/modes/primitives/GearXPrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` for AXIS_X without `Motion.h` globals)
  - `src/motion/modes/primitives/ConePrimitive.h/.cpp` (refactored to command `MotionEngine::engageSpindleSync()` with secondary axis taper ratio parameters)
  - `src/motion/modes/ModeRegistry.cpp` (registered all 4 primitives in `ModeRegistry::initStandardModes()`)
- **Verification Results:**
  - **PlatformIO Target Compilation:** `pio run -e esp32s3` completed with `[SUCCESS]` (0 errors, 0 warnings).
    - RAM: 19.0% (used 62,272 bytes from 327,680 bytes)
    - Flash: 20.5% (used 1,341,249 bytes from 6,553,600 bytes)
  - **Automated E2E Simulation Test:** All 4 test suites passed with 100% success.
- **Key Architectural Decisions & Deviations:**
  - Completely severed all 4 primitives from `Motion.h`, `Axis.h`, and procedural globals (`dupr`, `z`, `x`, `spindlePosAvg`, `stepToContinuous`).
  - Reused `MotionConversions.h` for direction mapping and axis token parsing across all primitives.
  - Implemented `onMotionHalted()` in all 4 primitives to handle automatic state transitions on soft limits (`SOFT_LIMIT_REACHED`) or stroke completions (`TARGET_DISTANCE_REACHED`).



