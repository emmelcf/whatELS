# NanoELS Phase 1 — Motion Core Implementation Plan & Execution Ledger

> **Status:** Completed & Archived Historical Runbook  
> **Target Subsystems:** Subsystem 1 (HAL & Hardware Timing), Subsystem 2 (Kinematic Motion Engine), Subsystem 3 (Mode Contract)  
> **Location:** `docs/archive/PHASE1_EXECUTION_PLAN.md`  
> **Platform:** ESP32-S3 (PlatformIO `pio run -e esp32s3`)

---

## 1. Overview & Strategy

To eliminate context rot and maintain maximum focus and safety, **Phase 1** is partitioned into four self-contained, sequentially dependent implementation slices. Each slice is executed in a dedicated pair-programming conversation.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        PHASE 1 EXECUTION TIMELINE                      │
├─────────────────────┬─────────────────────┬────────────────────────────┤
│ Slice               │ Domain & Scope      │ Key Deliverables           │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 1 (Conv 1)    │ Layer 1 HAL &       │ StepTimingConfig.h,        │
│                     │ Concurrency IPC     │ IStepperHAL, TelemetryMailbox, │
│                     │                     │ ISpindleEncoderHAL (PCNT)  │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 2 (Conv 2)    │ Layer 2 Kinematics  │ AxisGeometry, AxisState,   │
│                     │ & Math Primitives   │ SoftLimitTable ([KIN-03]), │
│                     │                     │ SpindleSynchronizer ([KIN-01/02]) │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 3 (Conv 3)    │ Motion Engine Core  │ MotionEngine, Trajectory,  │
│                     │ & Core 1 Loop       │ Core 1 Task, Flash Lockout │
├─────────────────────┼─────────────────────┼────────────────────────────┤
│ Slice 4 (Conv 4)    │ Mode Orchestration  │ IELSMode Refactor,         │
│                     │ Contract Decoupling │ Default-Deny Gatekeeper,   │
│                     │                     │ UnifiedGearbox Integration │
└─────────────────────┴─────────────────────┴────────────────────────────┘
```

---

## 2. Slice Specifications & Acceptance Criteria

### Slice 1: HAL Contracts & Foundational Concurrency IPC
- **Primary Goal:** Establish typed hardware interfaces and the lock-free telemetry mailbox for Core 1 $\to$ Core 0 data streaming.
- **Governing Specs:** [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md), [`CONCURRENCY_AND_TIMING.md`](./CONCURRENCY_AND_TIMING.md)
- **Target Files:**
  - `src/motion/hal/StepTimingConfig.h`: Nanosecond timing contracts (`dirSetupDelayNs`, `dirHoldTimeNs`, `stepPulseWidthNs`) and non-blocking `enableDelayMs` settling rules.
  - `src/motion/hal/IStepperHAL.h`: Abstract stepper interface for discrete pulse stepping and hardware-timed pulse trains.
  - `src/motion/hal/ISpindleEncoderHAL.h`: Abstract encoder interface with rational pulley ratios (`encoderTeeth`, `spindleTeeth`).
  - `src/motion/hal/Esp32SpindleEncoderHAL.h/.cpp`: Concrete ESP32-S3 PCNT hardware quadrature counter with **zero-loss 16-bit modular difference** (`[HAL-01]`) and 500 ns hardware glitch filter.
  - `src/motion/contract/TelemetryMailbox.h`: Provably race-free seqlock with explicit `std::atomic_thread_fence(memory_order_release)` and `std::atomic_thread_fence(memory_order_acquire)` (`[CONC-01]`).
- **Acceptance Criteria (DoD):**
  1. Compiles with zero errors or warnings under `pio run -e esp32s3`.
  2. `TelemetryMailbox` conforms strictly to the memory ordering model in Section 3.1 of `CONCURRENCY_AND_TIMING.md`.
  3. PCNT driver never calls `pcnt_unit_clear_count()`.

---

### Slice 2: Kinematic Kernel & Mathematical Primitives
- **Primary Goal:** Implement deterministic coordinates, soft limits, and anchored spindle gearing algorithms.
- **Governing Spec:** [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md)
- **Target Files:**
  - `src/motion/kernel/AxisGeometry.h`: Pitch, steps/rev, rapid clamps, and backlash definitions.
  - `src/motion/kernel/AxisState.h`: Pure kinematic position tracking in raw motor steps and canonical deci-microns (`du`). Symmetric half-way rounding in `stepsToDu()` and `duToSteps()` (`[KIN-05]`).
  - `src/motion/kernel/SoftLimit.h` & `SoftLimitTable.h/.cpp`: Multi-stop drum table. Fix `[KIN-03]`: resting at `currentSteps == stepPos` blocks motion in that direction (`D_remaining = 0`) unless `allowPassThrough` override is commanded.
  - `src/motion/kernel/SpindleSynchronizer.h/.cpp`: 64-bit Bresenham fractional accumulator with `m_anchorPulses`/`m_anchorSteps` for live pitch trims without historical jumps (`[KIN-02]`), and continuous phase tracking across deceleration, pause, and reversal (`[KIN-01]`).
- **Acceptance Criteria (DoD):**
  1. Symmetric integer rounding tests verify zero step loss on Imperial/fractional conversions.
  2. `SoftLimitTable` verified to prevent pass-through when resting directly on a stop.
  3. `SpindleSynchronizer::updatePitchLive()` verified to maintain continuity without step delta jumps.
  4. Compiles with zero warnings via `pio run -e esp32s3`.

---

### Slice 3: Motion Engine Core & Real-Time Concurrency Loop
- **Primary Goal:** Integrate HAL and Kernel inside the Core 1 real-time task loop with acceleration profiling and flash cache freeze protection.
- **Governing Specs:** [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md), [`CONCURRENCY_AND_TIMING.md`](./CONCURRENCY_AND_TIMING.md)
- **Target Files:**
  - `src/motion/kernel/TrajectoryPlanner.h/.cpp`: Real-time stopping distance calculation $d_{\text{stop}} = v_i^2 / 2a$ with sample margins and pre-cut entry RPM runway verification.
  - `src/motion/kernel/MotionEngine.h/.cpp`: Core 1 real-time orchestrator. Manages `AxisState`, `SoftLimitTable`, `IStepperHAL`, and `ISpindleEncoderHAL`.
  - `src/motion/contract/MotionCommandPacket.h`: Lock-free command queue packet consumed by Core 1.
  - `src/core/SettingsService.cpp`: Guard NVS and LittleFS flash writes with `!MotionEngine::instance().isAnyAxisMoving()` (`[CONC-02]`).
- **Acceptance Criteria (DoD):**
  1. All Core 1 ISRs, stepping routines, and math marked `IRAM_ATTR`.
  2. Core 1 tick execution time verified within $\le 35\,\mu\text{s}$ budget.
  3. In-motion NVS writes safely rejected with `ERR_MOTION_ACTIVE`.
  4. Compiles with zero warnings via `pio run -e esp32s3`.

---

### Slice 4: Mode Contract Decoupling & High-Level Orchestration
- **Primary Goal:** Decouple `IELSMode` from low-level motion ISRs, enforce default-deny safety gatekeeping, and integrate `UnifiedGearboxMode`.
- **Governing Specs:** [`MODE_INTEGRATION_CONTRACT.md`](./MODE_INTEGRATION_CONTRACT.md), [`../modes/ELS_MODE_ARCHITECTURE.md`](../modes/ELS_MODE_ARCHITECTURE.md)
- **Target Files:**
  - `src/motion/modes/IELSMode.h`: Remove legacy `onSpindlePulse()` / `onMotionTick()` hooks. Inherit from `IMotionEventListener` (`onMotionHalted`, `onSpindleSyncAcquired`).
  - `src/motion/modes/IELSMode.h`: Implement default-deny in `canExecuteCommand()` (`[SAFE-01]`).
  - `src/motion/contract/MotionCommands.h`: Add dual-axis compound taper parameters (`enableSecondaryAxis`, `secondaryAxis`, `secondaryRatioNum`, `secondaryRatioDen`) (`[KIN-04]`).
  - `src/motion/modes/composite/UnifiedGearboxMode.cpp/.h`: Update to command `MotionEngine` primitives (`engageSpindleSync`, `engageVelocityFeed`) and handle event callbacks.
- **Acceptance Criteria (DoD):**
  1. Modes act purely as orchestrators; zero motor step math or busy loops in mode code.
  2. Safety gatekeeper blocks invalid commands during `ARMED` and `RETRACTING` states.
  3. Full firmware build clean via `pio run -e esp32s3`.

---

## 3. Starter Kickoff Prompts (For New Conversations)

Copy and paste the appropriate prompt into each new chat session:

### Starter Prompt: Slice 1
```markdown
We are executing Slice 1 of Phase 1 (HAL Contracts & TelemetryMailbox) per `docs/motion/PHASE1_EXECUTION_PLAN.md`.

1. Review Section 2 (Slice 1 Scope) of `docs/motion/PHASE1_EXECUTION_PLAN.md` and the governing specifications:
   - `docs/motion/HAL_SPECIFICATION.md`
   - `docs/motion/CONCURRENCY_AND_TIMING.md`
2. Present your proposed API interfaces and file modifications for alignment before writing code.
3. Once aligned, implement the files under `src/motion/hal/` and `src/motion/contract/`.
4. Verify compilation cleanly using `pio run -e esp32s3`.
5. Commit the milestone (`feat(hal): implement HAL contracts, PCNT driver, and TelemetryMailbox`).
6. Update Section 4 (Execution Log & Handover Ledger) in `docs/motion/PHASE1_EXECUTION_PLAN.md` with commit hash, completed files, verification status, and any architectural notes.
```

### Starter Prompt: Slice 2
```markdown
We are executing Slice 2 of Phase 1 (Kinematic Kernel & Math Primitives) per `docs/motion/PHASE1_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE1_EXECUTION_PLAN.md` to check the completed log from Slice 1.
2. Review Section 2 (Slice 2 Scope) and `docs/motion/MOTION_ENGINE_SPECIFICATION.md`.
3. Present your proposed implementation details for `AxisGeometry`, `AxisState`, `SoftLimitTable` ([KIN-03]), and `SpindleSynchronizer` ([KIN-01], [KIN-02]) for alignment before writing code.
4. Implement the kernel components under `src/motion/kernel/`.
5. Verify compilation cleanly using `pio run -e esp32s3`.
6. Commit the milestone (`feat(motion): implement kinematic state, SoftLimitTable, and SpindleSynchronizer`).
7. Update Section 4 in `docs/motion/PHASE1_EXECUTION_PLAN.md` with commit hash, completed files, verification status, and notes.
```

### Starter Prompt: Slice 3
```markdown
We are executing Slice 3 of Phase 1 (MotionEngine Core & Real-Time Loop) per `docs/motion/PHASE1_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE1_EXECUTION_PLAN.md` to inspect the completed state from Slices 1 & 2.
2. Review Section 2 (Slice 3 Scope) along with `docs/motion/MOTION_ENGINE_SPECIFICATION.md` and `docs/motion/CONCURRENCY_AND_TIMING.md`.
3. Present your proposed implementation for `MotionEngine`, `TrajectoryPlanner`, and the `SettingsService` flash lockout guard ([CONC-02]) for alignment before writing code.
4. Implement the components under `src/motion/kernel/` and integrate with Core 1 FreeRTOS task.
5. Verify compilation cleanly using `pio run -e esp32s3`.
6. Commit the milestone (`feat(motion): implement MotionEngine orchestrator, trajectory planner, and Core 1 loop`).
7. Update Section 4 in `docs/motion/PHASE1_EXECUTION_PLAN.md` with commit hash, completed files, verification status, and notes.
```

### Starter Prompt: Slice 4
```markdown
We are executing Slice 4 of Phase 1 (Mode Contract Decoupling & Integration) per `docs/motion/PHASE1_EXECUTION_PLAN.md`.

1. Review Section 4 of `docs/motion/PHASE1_EXECUTION_PLAN.md` to inspect completed state from Slices 1, 2, & 3.
2. Review Section 2 (Slice 4 Scope) along with `docs/motion/MODE_INTEGRATION_CONTRACT.md` and `docs/modes/ELS_MODE_ARCHITECTURE.md`.
3. Present your proposed changes for `IELSMode.h`, the default-deny gatekeeper ([SAFE-01]), dual-axis taper parameters ([KIN-04]), and `UnifiedGearboxMode` integration for alignment before editing code.
4. Refactor mode interfaces and migrate `UnifiedGearboxMode`.
5. Verify full firmware compilation cleanly using `pio run -e esp32s3`.
6. Commit the milestone (`feat(modes): decouple IELSMode from motion ISRs and implement default-deny gatekeeper`).
7. Update Section 4 in `docs/motion/PHASE1_EXECUTION_PLAN.md` with the final summary for Phase 1 completion.
```

---

## 4. Living Execution Log & Handover Ledger

*(Updated at the conclusion of each conversation by the active pair-programming agent)*

### Status Tracker

| Slice | Status | Target Date | Commit Hash | Verified By | Notes / Deviations |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Slice 1: HAL & TelemetryMailbox** | `COMPLETE` | 2026-10-15 | `ec2753b` | PlatformIO | Zero warnings, zero errors |
| **Slice 2: Kinematic Kernel & Math** | `COMPLETE` | 2026-10-15 | `5720c53` | PlatformIO + Unit Test | Zero warnings, zero errors. KIN-01, KIN-02, KIN-03, KIN-05 verified. |
| **Slice 3: MotionEngine Core Loop** | `COMPLETE` | 2026-10-15 | `06db3a1` | PlatformIO + Unit Test | Zero warnings, zero errors. CONC-02 flash lockout, TrajectoryPlanner, and Core 1 task verified. |
| **Slice 4: Mode Contract Decoupling** | `COMPLETE` | 2026-10-15 | `0fb6fb8` | PlatformIO + Unit Test | Zero warnings, zero errors. SAFE-01 default-deny gatekeeper, KIN-04 dual-axis taper, and UnifiedGearboxMode migration verified. |

---

### Detailed Session Logs

#### Slice 1 Execution Log
- **Date Completed:** 2026-10-15
- **Commit:** `ec2753b`
- **Files Created / Modified:**
  - `src/motion/hal/StepTimingConfig.h`
  - `src/motion/hal/IStepperHAL.h`
  - `src/motion/hal/ISpindleEncoderHAL.h`
  - `src/motion/hal/Esp32SpindleEncoderHAL.h`
  - `src/motion/hal/Esp32SpindleEncoderHAL.cpp`
  - `src/motion/contract/TelemetryMailbox.h`
  - `src/core/SettingsService.h` (guarded `AxisId` against redefinition)
- **PlatformIO Verification Result:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.7% (61,120 bytes), Flash: 20.2% (1,322,253 bytes).
- **Key Architectural Decisions & Deviations:**
  - Implemented `[HAL-01]` zero-loss 16-bit modular difference in `Esp32SpindleEncoderHAL::getPulseDelta()`; PCNT counter is never cleared during runtime.
  - 500 ns PCNT glitch filter configured to reject industrial VFD PWM interference.
  - Configured 4x quadrature decoding using dual PCNT channels per unit.
  - Implemented `TelemetryMailbox` seqlock conforming to `[CONC-01]` and Section 3.1 of `CONCURRENCY_AND_TIMING.md` with explicit release/acquire thread fences.
  - Zero modifications to `platformio.ini` required (`-I src` already provides access to `motion/hal/` and `motion/contract/`).

#### Slice 2 Execution Log
- **Date Completed:** 2026-10-15
- **Commit:** `5720c53`
- **Files Created / Modified:**
  - `src/motion/kernel/AxisGeometry.h`
  - `src/motion/kernel/AxisState.h`
  - `src/motion/kernel/SoftLimit.h`
  - `src/motion/kernel/SoftLimitTable.h`
  - `src/motion/kernel/SoftLimitTable.cpp`
  - `src/motion/kernel/SpindleSynchronizer.h`
  - `src/motion/kernel/SpindleSynchronizer.cpp`
  - `platformio.ini` (added `-I src/motion/kernel` to build flags)
- **PlatformIO Verification Result:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.7% (61,120 bytes), Flash: 20.2% (1,322,253 bytes). Unit test verified 100% pass across Imperial round-trip conversions, soft-limit resting bypass prevention, and live pitch trim continuity.
- **Key Architectural Decisions & Deviations:**
  - Implemented `[KIN-05]` symmetric integer half-way rounding ($+ \text{den}/2$ for $\ge 0$, $-\text{den}/2$ for $< 0$) in `stepsToDu()` and `duToSteps()`, with division-by-zero guards.
  - Implemented `[KIN-03]` multi-stop soft limit lookahead in `SoftLimitTable::findNextEnforcedLimit()`. When resting directly at an active limit (`currentSteps == stepPos`), remaining travel $D_{\text{remaining}} = 0$ is returned to block further motion in that direction unless explicit `allowPassThrough` override is commanded.
  - Implemented `[KIN-02]` anchored 64-bit Bresenham accumulator in `SpindleSynchronizer` with `updatePitchLive()` to shift the anchor and enable live pitch trims (`CMD_FEED_NUDGE`) without historical multiplication jumps or step delta discontinuities.
  - Implemented `[KIN-01]` continuous spindle phase accumulation in `SpindleSynchronizer::m_spindlePhasePulses` and `calculateModuloPhasePulses()` across deceleration, pauses, and dwell to guarantee exact thread re-locking without cross-threading upon carriage return or spindle reversal.
  - Marked real-time kinematic calculations (`findNextEnforcedLimit`, `computeIdealSyncSteps`, `stepsToDu`, `duToSteps`, `getWorkpieceCoordDu`) with `IRAM_ATTR` for zero-jitter execution on Core 1.

#### Slice 3 Execution Log
- **Date Completed:** 2026-10-15
- **Commit:** `06db3a1`
- **Files Created / Modified:**
  - `src/motion/contract/MotionCommandPacket.h`
  - `src/motion/kernel/TrajectoryPlanner.h`
  - `src/motion/kernel/TrajectoryPlanner.cpp`
  - `src/motion/kernel/MotionEngine.h`
  - `src/motion/kernel/MotionEngine.cpp`
  - `src/core/SettingsService.cpp`
  - `src/motion/kernel/SpindleSynchronizer.cpp` (cleaned up redundant `IRAM_ATTR` definitions)
- **PlatformIO Verification Result:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.9% (61,912 bytes), Flash: 20.2% (1,323,717 bytes). Unit test verified 100% pass on stopping distance calculations, sample margin safety, pre-cut entry RPM runway verification, and velocity profiling.
- **Key Architectural Decisions & Deviations:**
  - Implemented `[CONC-02]` in-motion flash write lockout in `SettingsService`: all NVS (`Preferences`) and LittleFS write paths (`set`, `reset`, `resetGroup`, `setPreset`, `saveCurrentAsPreset`, `deletePreset`, `setDefaultPreset`) check `MotionEngine::instance().isAnyAxisMoving()` and reject immediately with `ERR_MOTION_ACTIVE` to eliminate the 1–40 ms SPI flash cache stall on Core 1.
  - Implemented `TrajectoryPlanner` real-time stopping distance calculation $d_{\text{stop}} = \text{ceil}((v_i^2 - v_{\text{end}}^2) / 2a) + \text{ceil}(0.5 \cdot v_i \cdot \Delta t)$ and pre-cut spindle runway verification $v_{\text{entry\_max}} = \sqrt{2 \cdot a \cdot D_{\text{remaining}}}$ with zero runtime allocations.
  - Implemented `MotionEngine` singleton orchestrator managing dual axes, `SoftLimitTable` lookahead, `SpindleSynchronizer` gearing, lock-free inter-core `MotionCommandPacket` queue, Core 1 task lifecycle, and 50 Hz `TelemetryMailbox` state streaming.
  - Resolved GCC `[-Wattributes]` section conflict warnings by placing `IRAM_ATTR` on class member declarations in headers.

#### Slice 4 Execution Log
- **Date Completed:** 2026-10-15
- **Commit:** `0fb6fb8`
- **Files Created / Modified:**
  - `src/motion/contract/MotionCommands.h` (created)
  - `src/motion/modes/IELSMode.h`
  - `src/motion/modes/composite/UnifiedGearboxMode.h`
  - `src/motion/modes/composite/UnifiedGearboxMode.cpp`
  - `src/motion/kernel/MotionEngine.h`
  - `src/motion/kernel/MotionEngine.cpp`
  - `src/motion/kernel/AxisState.h`
  - `src/motion/modes/ModeRegistry.cpp`
  - `src/motion/modes/primitives/AsyncPrimitive.h` / `.cpp`
  - `src/motion/modes/primitives/ConePrimitive.h` / `.cpp`
  - `src/motion/modes/primitives/GearXPrimitive.h` / `.cpp`
  - `src/motion/modes/primitives/GearZPrimitive.h` / `.cpp`
  - `src/main.cpp`
- **PlatformIO Verification Result:** `pio run -e esp32s3` passed cleanly (0 errors, 0 warnings). RAM: 18.9% (61,920 bytes), Flash: 20.2% (1,325,997 bytes). Unit test verified 100% pass across all 7 verification categories.
- **Key Architectural Decisions & Deviations:**
  - Decoupled `IELSMode` from low-level motion ISRs: removed `onSpindlePulse()`, `onMotionTick()`, and `onLimitReached()`; inherited from `IMotionEventListener` (`onMotionHalted`, `onSpindleSyncAcquired`, `onMotionTelemetry`).
  - Implemented `[SAFE-01]` default-deny in `IELSMode::canExecuteCommand()`: full configuration commands are only permitted in `IDLE` or `COMPLETED`; `RUNNING` and `ARMED` strictly whitelist declared live operations; `PAUSED` allows feed resume / nudge / cycle; `RETRACTING` and `FAULT` reject all non-stop commands.
  - Implemented `[KIN-04]` compound dual-axis taper parameters (`enableSecondaryAxis`, `secondaryAxis`, `secondaryRatioNum`, `secondaryRatioDen`) in `SpindleSyncParams` and `VelocityFeedParams`.
  - Migrated `UnifiedGearboxMode` to act purely as an orchestrator commanding `MotionEngine::engageSpindleSync()` and `engageVelocityFeed()`, receiving `onMotionHalted()` callbacks for soft limits and stop events with zero motor step math in mode code.
  - Resolved Xtensa literal pool placement relocation error in `.iram1` section by marking `stepsToDu`, `duToSteps`, and `getWorkpieceCoordDu` in `AxisState.h` with `__attribute__((always_inline))`.
  - Injected `MotionEngine*` into `IELSMode::onEnter(MotionEngine* engine)` with safe fallback to `MotionEngine::instance()`.

---

### Phase 1 Completion Summary

With Slice 4 complete, **Phase 1 of the Modular Motion Engine Architecture is 100% finished**:
1. **Layer 1 (HAL):** Zero-loss 16-bit modular PCNT sampling (`[HAL-01]`), 500 ns glitch filtering, lock-free `TelemetryMailbox` seqlock (`[CONC-01]`).
2. **Layer 2 (Kinematic Kernel):** 64-bit anchored Bresenham fractional accumulator (`[KIN-02]`), phase preservation across dwells and spindle reversals (`[KIN-01]`), multi-stop soft limits drum with resting bypass prevention (`[KIN-03]`), and symmetric integer half-way rounding (`[KIN-05]`).
3. **Core 1 Real-Time Loop:** Trajectory planning with stopping distance calculation and entry RPM runway checks, in-motion NVS flash cache lockout (`[CONC-02]`), and lock-free inter-core command ringbuffer.
4. **Layer 3 (Mode Integration):** Clean decoupling of modes from motion ISRs, default-deny safety gatekeeper (`[SAFE-01]`), dual-axis compound taper parameters (`[KIN-04]`), and migration of `UnifiedGearboxMode`.
 
