# whatELS — Architectural Peer Review & Safety Audit

> **Document Type:** Principal Embedded Systems & Motion Control Peer Review  
> **Target System:** whatELS Modular Architecture (ESP32-S3)[cite: 1]  
> **Reviewed Compendium:** `UNABRIDGED_ARCHITECTURE_COMPENDIUM.md`[cite: 1]  
> **Date:** October 15, 2026  

---

## 1. Executive Verdict & Readiness Scorecard

**Overall Architectural Readiness Score: 86% Ready for Physical Lathe Iron**

The **whatELS** architecture is an exceptionally well-structured embedded systems specification[cite: 1]. The decomposition of a monolithic procedural sketch into a 6-subsystem architecture, the strict core isolation between Core 0 (supervisory/network) and Core 1 (hard real-time kinematics), the canonical integer deci-micron (`du`) coordinate system, the lossless operator intent state model (`K`, `I`, `C`, `P`), and the sparse NVS override persistence engine represent top-tier CNC controller design[cite: 1].

However, before commissioning this firmware on physical lathe iron with high-torque servos or closed-loop steppers, **three physical-safety `BLOCKER` issues and five `CRITICAL` concurrency/timing hazards must be resolved:**

1. **Kinematic Deceleration Desynchronization (`BLOCKER` — `[KIN-01]`):** Decelerating the Z-axis stepper along a time-based velocity profile ($d_{\text{stop}} = v_i^2 / 2a$) while the spindle is still rotating in `SPINDLE_SYNC_GEARING` mode breaks the geometric phase lock between the spindle and carriage, guaranteeing ruined threads or tool crashes on reversal unless the phase offset is tracked throughout deceleration and rest[cite: 1].
2. **Dynamic Live Pitch Trim Phase Jump (`BLOCKER` — `[KIN-02]`):** Multiplying total `AccumulatedSpindlePulses` by a newly nudged `PitchDupr` recalculates position from the origin of the cut rather than incrementally from the current coordinate, causing an instantaneous multi-millimeter step jump under chip load[cite: 1].
3. **Soft Limit Bypass When Resting on Stop (`BLOCKER` — `[KIN-03]`):** Because `findNextEnforcedLimit()` filters for `stepPos < currentSteps - ADVANCE_DEADBAND_STEPS`, once the carriage halts directly on a soft limit (`currentSteps == stepPos`), that limit is excluded from the next search[cite: 1]. Pressing feed again in the same direction skips the stop and drives the carriage into the chuck[cite: 1].
4. **Seqlock Memory Reordering Bug (`CRITICAL` — `[CONC-01]`):** In `TelemetryMailbox`, C++ `m_data = snapshot` is a non-atomic struct copy that the Xtensa compiler can reorder across `memory_order_release` stores, and `read()` lacks an acquire fence before `seq2` is sampled, permitting torn telemetry reads[cite: 1].
5. **ESP32-S3 SPI Flash Cache Freeze (`CRITICAL` — `[CONC-02]`):** Writing to NVS or LittleFS on Core 0 automatically suspends the shared SPI flash instruction/data cache on **both cores** for 1 to 40 ms[cite: 1]. Unless Core 1 ISR handlers and motion loops are strictly placed in `IRAM_ATTR` / `DRAM_ATTR` and flash writes are gated by `isOn == false`, a background setting save on Core 0 will freeze Core 1 mid-cut[cite: 1].

---

## 2. Domain-by-Domain Analysis

### Domain A: Real-Time Determinism, Timing & Concurrency (Core 0 vs. Core 1)

* **1. Core Partitioning (Sound, with Cross-Core Boundary Leak):**  
  The conceptual boundary between Core 0 and Core 1 is rock-solid[cite: 1]. However, Sections 4 & 6 show `IELSMode` (running on Core 0's HMI/Mode task) receiving `MotionEngine*` directly (`onEnter(MotionEngine* engine)`) and calling `m_engine->engageSpindleSync(params)` synchronously, while `MotionEngine` synchronously fires `onMotionHalted()` callbacks back into `IELSMode`[cite: 1]. If `MotionEngine` lives on Core 1 and `IELSMode` lives on Core 0, direct method invocation violates the lock-free boundary[cite: 1]. `IELSMode` must either execute inside the Core 1 motion task loop (consuming `MotionCommandPacket` from Core 0) or interact with `MotionEngine` strictly via an `IMotionCommandQueue` proxy and an `IMotionEventQueue`[cite: 1].

* **2. Lock-Free State Synchronization (`TelemetryMailbox`):**  
  Using a seqlock for single-producer (Core 1) to multi-consumer (Core 0) telemetry is the exact right pattern—Core 0 readers can never block or starve Core 1[cite: 1]. However, the C++11 atomic memory ordering in `TelemetryMailbox` (Section 5.3.1) is defective[cite: 1]:
  * In `publish()`, `m_seq.store(seq, std::memory_order_release)` prevents *prior* writes from moving *down*, but **does not prevent subsequent writes (`m_data = snapshot`) from moving *up* before the odd sequence counter store!**
  * In `read()`, `uint32_t seq2 = m_seq.load(std::memory_order_acquire)` prevents *subsequent* reads from moving *up*, but **does not prevent prior reads (`outSnapshot = m_data`) from moving *down* after `seq2` is sampled!**
  * Because `m_data` is not an atomic type, concurrent read/write must be guarded by explicit `std::atomic_thread_fence(std::memory_order_release)` and `std::atomic_thread_fence(std::memory_order_acquire)` barriers.

* **3. Interrupt Latency & Jitter Budget ($\le 35\,\mu\text{s}$ at $250\,\mu\text{s}$ period):**  
  On a 240 MHz Xtensa LX7, $35\,\mu\text{s}$ equals 8,400 clock cycles—more than enough for 64-bit integer math and GPIO writes **if and only if** the code and data reside in internal SRAM (`IRAM_ATTR` and `DRAM_ATTR`)[cite: 1]. On the ESP32-S3, Core 0 and Core 1 share a single SPI/Octal Flash bus. Whenever Core 0 writes to NVS (`Preferences`) or LittleFS, ESP-IDF disables the flash cache on *both* cores for the duration of the flash page program/sector erase ($1\text{ ms}$ to $40\text{ ms}$)[cite: 1]. Any Core 1 task or ISR not marked `IRAM_ATTR` (or reading `const` tables/vtables from flash) will stall completely, causing catastrophic stepper stalls.

* **4. Hardware Offload (PCNT):**  
  At 3,000 RPM and 4,096 edges/rev ($204.8\text{ kHz}$), a $250\,\mu\text{s}$ poll interval accumulates $\approx 51.2$ counts per tick (or $\approx 205$ counts at $1\text{ ms}$)[cite: 1]. This easily fits inside the 16-bit signed hardware counter (`[-32768, +32767]`). However, **you must not call `pcnt_unit_clear_count()` after reading `pcnt_unit_get_count()`**. Between reading the register and clearing it ($\approx 0.5\,\mu\text{s}$), an incoming $204.8\text{ kHz}$ encoder edge will be wiped out, causing cumulative spindle angle drift over hundreds of revolutions! Instead, let the 16-bit PCNT hardware counter free-run and wrap natively, computing the delta via 16-bit signed modular subtraction (`int16_t delta = currentCount - lastCount;`). Additionally, configure the ESP32-S3 PCNT hardware glitch filter to $\approx 500\text{ ns}$ (40 APB clock cycles at 80 MHz) to reject VFD PWM switching noise without attenuating the $4.88\,\mu\text{s}$ valid quadrature edges[cite: 1].

---

### Domain B: Kinematics, Trajectory & Stepper Safety

* **1. Stopping Distance Window Lookahead ($d_{\text{stop}} = \frac{v_i^2 - v_{\text{end}}^2}{2a}$):**
  * **In `VELOCITY_FEED` and `StrokeParams`:** The kinematic lookahead works cleanly, provided you account for discrete-time quantization (`ceil` of stopping steps plus a $0.5 \cdot v_i \cdot \Delta t$ sample-period margin) so the axis never overshoots by 1 step before triggering deceleration[cite: 1].
  * **In `SPINDLE_SYNC_GEARING` (`BLOCKER`):** While synchronous gearing is engaged, carriage velocity $v_i$ is strictly a function of spindle RPM ($v_i = \frac{\text{RPM}}{60} \times \text{pitch}$)[cite: 1]. When $D_{\text{remaining}} \le d_{\text{stop}}$ triggers at a shoulder stop, the controller breaks direct 1:1 pulse gearing to decelerate the motor along its maximum safe acceleration ramp $a$[cite: 1]. **During this deceleration ramp, the spindle continues turning at full speed while the carriage slows down**, creating a phase lag ($\Delta\phi_{\text{decel}}$) between the leadscrew and the spindle thread helix! Worse, if the spindle accelerates mid-cut or is engaged too close to a shoulder at high RPM, $d_{\text{stop}}$ can exceed $D_{\text{remaining}}$ instantly[cite: 1]. You must:
    1. Verify before starting a synchronous cut that $\text{RPM}_{\text{current}}$ does not exceed the maximum permissible entry RPM for the available runway.
    2. Continue accumulating all spindle pulses into the phase tracker during the deceleration ramp and while resting at the shoulder so thread registration is never lost[cite: 1].

* **2. Multi-Stop Soft Limits (`SoftLimitTable`):**  
  Eliminating `LONG_MAX`/`LONG_MIN` sentinels in favor of explicit `defined` and `enabled` booleans is a great design upgrade[cite: 1]. However, `findNextEnforcedLimit()` (Section 3.3.2) has a dangerous deadband flaw:
  `if (m_limits[i].isEnforced() && (m_limits[i].stepPos < (currentSteps - ADVANCE_DEADBAND_STEPS)))`[cite: 1].  
  When the carriage decelerates and halts *exactly* on a soft limit (`currentSteps == m_limits[i].stepPos`), that limit now fails the `< currentSteps - 2` check[cite: 1]! If the operator accidentally presses `[FEED_LEFT]` again while resting on the chuck stop, `findNextEnforcedLimit()` will **skip the chuck stop entirely** and drive the carriage straight into the spinning chuck jaws[cite: 1]! A limit must only be bypassed if the operator explicitly commands a "Pass-Through / Override" action; otherwise, if `currentSteps == limit.stepPos` and the commanded direction moves into or beyond that limit, motion must be blocked (`D_remaining = 0`)[cite: 1].

* **3. Canonical Units (Deci-Microns):**  
  Storing coordinates in integer `du` ($0.1\,\mu\text{m}$) with 64-bit intermediate products completely prevents overflow[cite: 1]. However, C++ integer division truncates toward zero[cite: 1]. For non-integer step pitches (e.g., an Imperial 12 TPI leadscrew = $211{,}667\text{ du}$, or 3200 microsteps where $1\text{ step} = 6.25\text{ du}$), truncation in `duToSteps(stepsToDu(s))` loses 1 step[cite: 1]. Both `stepsToDu` and `duToSteps` must use **symmetric half-way rounding away from zero**, and internal axis position must always treat `posSteps` as the primary kinematic accumulator[cite: 1].

* **4. Spindle Synchronization & Threading:**  
  The 64-bit closed-form target position equation:
  $$\text{TargetSteps} = \text{round}\left( \frac{\text{AccumulatedSpindlePulses} \times \text{MotorSteps} \times \text{PitchDupr}}{\text{ScrewPitchDu} \times \text{EncoderPPR}} \right)$$
  is mathematically immune to cumulative drift over millions of turns[cite: 1]. However, **if the operator uses Live Feed Trim (`CMD_FEED_NUDGE`) while `AccumulatedSpindlePulses` is non-zero** (e.g., 500 turns into a cut), changing `PitchDupr` in that formula multiplies all 500 historical turns by the new pitch, causing an instantaneous step jump of $\Delta\text{Pitch} \times 500\text{ turns}$[cite: 1]! The synchronizer must maintain a phase anchor (`anchorPulses`, `anchorSteps`) that latches the current position whenever `PitchDupr` is dynamically modified[cite: 1]. Additionally, in `SpindleHardwareConfig`, replace `float pulleyRatio` with rational gear tooth integers (`uint16_t encoderTeeth`, `uint16_t spindleTeeth`) to keep the denominator 100% integer-exact[cite: 1].

---

### Domain C: Layer Boundaries & Decoupling

* **1. HAL (`IStepperHAL`, `ISpindleEncoderHAL`, `IDROScaleHAL`, `IEndstopHAL`):**
  * **Unit Mismatch:** `StepTimingConfig` in `HAL_SPECIFICATION.md` specifies `uint16_t stepPulseWidthUs = 4`, whereas `AxisHardwareConfig` in `LATHE_MACHINE_PROFILE.md` specifies `uint16_t stepPulseWidthNs = 500`[cite: 1]. Standardize on nanoseconds across both layers[cite: 1].
  * **Non-Blocking Enable Settling (`enableDelayMs = 50`):** `IStepperHAL::setEnabled(axis, true)` cannot block for $50\text{ ms}$ (Section 2.1.1 invariant)[cite: 1]. If `setEnabled(true)` returns immediately and `MotionEngine` immediately calls `startPulseTrain()`, the first 50 ms of steps will be ignored by the optocoupler/driver while its power stage settles! `IStepperHAL` or `MotionEngine` must enforce an `ARMING_ENABLE_DELAY` state machine or timestamp guard before emitting pulses[cite: 1].
  * **Coordinated Multi-Axis Stepping (`ZX` Tapers & Cones):** Ensure `IStepperHAL` allows preparing direction pins on both Z and X before firing simultaneous GPIO port register writes (`GPIO.out_w1ts` / `GPIO.out1_w1ts`) so Z and X step edges occur with zero inter-axis skew[cite: 1].

* **2. MotionEngine $\leftrightarrow$ Mode Integration Contract:**  
  Making modes pure event-driven FSMs (`onMotionHalted`, `onSpindleSyncAcquired`) that orchestrate high-level primitives (`engageSpindleSync`, `engageVelocityFeed`, `executeStroke`) is a massive improvement over legacy procedural loops[cite: 1]. Two gaps exist in `MODE_INTEGRATION_CONTRACT.md`:
  * **Missing Compound/Diagonal Primitive:** `SpindleSyncParams`, `VelocityFeedParams`, and `StrokeParams` only accept a single `AxisId axis` (`AXIS_Z` or `AXIS_X`), and have no field for `taperRatio` or dual-axis target coordinates[cite: 1]! Yet `UnifiedGearboxMode` supports `axis = "ZX"` with `m_taperRatio`, and `TaperedThreadingMode` requires coordinated X+Z threading[cite: 1]. Add `secondaryAxis`, `secondaryRatioNum`, and `secondaryRatioDen` to those parameter structs[cite: 1].
  * **Backlash Takeup on Cycle Retract/Infeed:** In Scenario B (Multi-Pass Turning Cycle, Section 4.5.2), `MotionEngine::executeStroke()` must transparently inject `backlashSteps` motor pulses without incrementing `posSteps` whenever an axis reverses direction so backlash is taken up *before* the tool enters the cut[cite: 1].

---

### Domain D: State Model, Properties & Persistence

* **1. Canonical 4-Domain State Model (`K`, `I`, `C`, `P`):**  
  Partitioning state into `Kinematics`, `Intent`, `Cycle`, and `Properties` with change-driven bitmask broadcasting and dynamic encoder deadbands (`360.0f / ENCODER_STEPS_FLOAT`) is a standout feature of this design[cite: 1]. Storing both the authoring intent (`13 TPI`) and canonical `dupr` eliminates float round-trip drift[cite: 1].
  * *Critique:* There is a struct divergence between `MotionTelemetrySnapshot` (Section 5.3.1), `StatusSnapshot` (Section 7.5.2 / 10.4.2), and `KinematicsState` (Section 7.4.2)[cite: 1]. Unify these so `StatusSnapshot` is a direct wrapper around `KinematicsState` (or produced directly from `MotionTelemetrySnapshot`) without maintaining three separate kinematic structs[cite: 1].

* **2. 4-Tier Properties & NVS Sparse Overrides:**  
  The "Dynamic Default vs. Sparse Override" model (`std::optional<T>` in RAM backed by NVS only when modified) and the 3-tier profile fallback (Active NVS $\to$ LittleFS `/presets/profiles.json` $\to$ ROM `PROGMEM`) are rock-solid[cite: 1].
  * *Critique:* Because NVS and LittleFS writes erase/program SPI flash pages and disable the ESP32-S3 cache on both cores, `SettingsService::set()`, `reset()`, and `applyPreset()` **must reject execution with `ERR_MOTION_ACTIVE` whenever `MotionEngine::isMoving()` is true**[cite: 1]. Furthermore, replace Arduino `String` fields inside `WifiConfig` and `CachedProperty` with fixed-size `char[N]` arrays to eliminate heap fragmentation[cite: 1].

---

### Domain E: Multi-Client Architecture & Wire Protocol

* **1. Safety Gatekeeper & In-Motion Lockout:**  
  The `canExecuteCommand(CommandId cmd, ModeState state)` gatekeeper and the In-Motion Safety Permissions matrix (Section 6.3.4) cleanly prevent hazardous mid-cut mutations[cite: 1].
  * *Logic Bug Found:* In `IELSMode::canExecuteCommand()` (Section 6.3.3), only `if (state == ModeState::RUNNING)` and `if (state == ModeState::PAUSED)` are checked before falling through to `return true;`[cite: 1]. When `state` is `ModeState::ARMED` (waiting for spindle index pulse to start threading) or `ModeState::RETRACTING` (executing automatic tool clearance move at the end of a pass), **`canExecuteCommand()` falls through to `return true;`!** An operator or client could swap axes, zero the DRO, or rewrite pitch while the carriage is actively retracting at rapid speed or armed for an index pulse[cite: 1].

* **2. Safe Monitor Lockout & Zero-WiFi Mandate Conflict:**  
  * The **Safe Monitor Lockout** (Tier 3 capability handshake) is an excellent pattern for multi-client safety[cite: 1].
  * However, there is an **architectural contradiction** between Section 1.1.3 / Section 10.1 ("**Zero-WiFi Control Plane Mandate:** Automated threading cycles, electronic gearing, and emergency stops execute strictly via on-chip memory or wired serial UART. Wi-Fi and WebSockets are dedicated to supervisory telemetric observation and setup") and Section 6.4.1 / Section 10.10.1 ("**Atomic Motion Invocation (Rich Clients / Web UI):** `CMD.startFeed=...`")[cite: 1].
  * *Reconciliation:* Clarify in the spec that:
    1. Autonomous cycles (`startFeed`, `startCycle`) are safe to initiate via WebSocket because Core 1 executes the entire trajectory and soft-limit stop autonomously on-chip even if Wi-Fi drops 1 millisecond later[cite: 1].
    2. Continuous manual jogging (`CMD.jogHold`) over Wi-Fi requires a strict **200 ms hardware lease watchdog on Core 1** so a dropped `CMD.jogRelease` packet halts jogging immediately[cite: 1].
    3. Physical E-Stop must always be hardwired to GPIO/driver enable, with Web UI `STOP` acting as a secondary software stop[cite: 1].

---

## 3. Actionable Findings & Recommendations Summary Table

*(See Section 3.1 below for complete, copy-ready C++ code fixes corresponding to each Finding ID).*

| Finding ID | Severity | Affected Component / Spec | Issue Description & Failure Mode | Summary of Recommended Fix |
| :--- | :--- | :--- | :--- | :--- |
| **`[KIN-01]`** | **`BLOCKER`** | `SpindleSynchronizer` & `TrajectoryPlanner` (`MOTION_ENGINE_SPECIFICATION.md` §4.2, §5.2) | **Loss of Thread Phase & Overshoot During Sync Deceleration:** Time-based decel ($d_{\text{stop}} = v_i^2 / 2a$) while spindle turns breaks 1:1 thread phase lock and risks overshoot if spindle RPM rises[cite: 1]. | Continuously accumulate spindle pulses into `m_spindlePhasePulses` during decel, pause, and rest[cite: 1]. Add a $10\%$ velocity margin to $d_{\text{stop}}$ and re-lock on modulo phase upon reversal (See **Fix `[KIN-01]`**). |
| **`[KIN-02]`** | **`BLOCKER`** | `SpindleSynchronizer` (`MOTION_ENGINE_SPECIFICATION.md` §5.1) | **Catastrophic Step Jump on Live Pitch Trim (`CMD_FEED_NUDGE`):** Multiplying total historical `AccumulatedSpindlePulses` by a modified `PitchDupr` causes an instantaneous step jump of $N_{\text{turns}} \times \Delta\text{Pitch}$[cite: 1]. | Latch `m_anchorSteps` and `m_anchorPulses` whenever `PitchDupr` changes dynamically, computing incremental steps from the anchor (See **Fix `[KIN-02]`**). |
| **`[KIN-03]`** | **`BLOCKER`** | `SoftLimitTable` (`MOTION_ENGINE_SPECIFICATION.md` §3.2) | **Soft Limit Bypass When Resting on Stop:** `findNextEnforcedLimit()` filters for `stepPos < currentSteps - 2`[cite: 1]. Once halted at `currentSteps == stepPos`, pressing feed again skips the stop and crashes into the chuck[cite: 1]. | Remove blind `ADVANCE_DEADBAND_STEPS` subtraction; block motion (`D_remaining = 0`) when resting on an active limit unless `allowPassThrough` is explicitly requested (See **Fix `[KIN-03]`**). |
| **`[CONC-01]`** | **`CRITICAL`** | `TelemetryMailbox` (`CONCURRENCY_AND_TIMING.md` §3.1) | **Seqlock Memory Reordering & Torn Reads:** `m_data = snapshot` can be reordered before the odd `m_seq` store in `publish()` or after `seq2` load in `read()`, causing torn reads on Xtensa LX7[cite: 1]. | Insert explicit `std::atomic_thread_fence(memory_order_release)` and `std::atomic_thread_fence(memory_order_acquire)` barriers around payload copies (See **Fix `[CONC-01]`**). |
| **`[CONC-02]`** | **`CRITICAL`** | ESP32-S3 Flash Cache & Core 1 (`CONCURRENCY_AND_TIMING.md` §4.3, `ELS_PROPERTIES_AND_SETTINGS.md` §6.1) | **Core 1 Freeze During Core 0 NVS/LittleFS Writes:** Flash erase/write on Core 0 suspends SPI flash cache on *both* cores for $1\text{–}40\text{ ms}$, stalling Core 1 motion if code/data is in Flash[cite: 1]. | Place all Core 1 motion functions/ISRs in `IRAM_ATTR`, state in `DRAM_ATTR`, stack in internal SRAM, and reject NVS/LittleFS writes when `isOn == true`[cite: 1] (See **Fix `[CONC-02]`**). |
| **`[CONC-03]`** | **`CRITICAL`** | `IELSMode` $\leftrightarrow$ `MotionEngine` (`MODE_INTEGRATION_CONTRACT.md` §3, §4) | **Cross-Core Thread-Safety Violation:** `IELSMode` (Core 0) invokes methods directly on `MotionEngine*` (Core 1) and receives synchronous `onMotionHalted()` callbacks across cores without synchronization[cite: 1]. | Either run `IELSMode` FSMs inside the Core 1 Motion Task loop (recommended) or decouple Core 0 `IELSMode` via `IMotionCommandQueue` and `IMotionEventQueue` ringbuffers[cite: 1]. |
| **`[SAFE-01]`** | **`CRITICAL`** | `IELSMode::canExecuteCommand()` (`ELS_MODE_ARCHITECTURE.md` §3.3) | **Gatekeeper Bypass in `ARMED`, `RETRACTING`, and `FAULT` States:** `canExecuteCommand()` only checks `RUNNING` and `PAUSED`, falling through to `return true;` during `ARMED` and `RETRACTING` rapid moves[cite: 1]. | Invert logic to **default-deny**: only allow full configuration in `IDLE` and `COMPLETED`, and explicitly whitelist safe live commands in active states (See **Fix `[SAFE-01]`**). |
| **`[HAL-01]`** | **`CRITICAL`** | `ISpindleEncoderHAL` (`HAL_SPECIFICATION.md` §3.1, `CONCURRENCY_AND_TIMING.md` §4.1) | **PCNT Register Clear Race & Counter Wrap Hazard:** Clearing PCNT after reading loses $204.8\text{ kHz}$ edges; free-running without 16-bit modular math wraps every $0.32\text{ s}$ at 3,000 RPM[cite: 1]. | Never reset PCNT count during operation; let the 16-bit counter wrap natively and compute `int16_t delta = current16 - m_lastCount16` (See **Fix `[HAL-01]`**). |
| **`[KIN-04]`** | **`ENHANCEMENT`** | `SpindleSyncParams`, `VelocityFeedParams`, `StrokeParams` (`MODE_INTEGRATION_CONTRACT.md` §2.2) | **Missing Dual-Axis / Taper Primitive Parameters:** Motion primitive structs only specify a single `AxisId` with no ratio fields for `ZX` cones or tapered threads[cite: 1]. | Add `enableSecondaryAxis`, `secondaryAxis`, `secondaryRatioNum`, and `secondaryRatioDen` to motion primitive structs (See **Fix `[KIN-04]`**). |
| **`[KIN-05]`** | **`ENHANCEMENT`** | `AxisState` Conversions (`MOTION_ENGINE_SPECIFICATION.md` §2.3) | **Truncation Drift in `stepsToDu` and `duToSteps`:** C++ integer division truncates toward zero, losing 1 step on round-trip conversion for fractional `du/step` pitches[cite: 1]. | Use symmetric half-way rounding (`(num >= 0 ? num + den/2 : num - den/2) / den`) in both conversion functions (See **Fix `[KIN-05]`**). |
| **`[HAL-02]`** | **`ENHANCEMENT`** | `StepTimingConfig` (`HAL_SPECIFICATION.md` §2.1 & `LATHE_MACHINE_PROFILE.md` §3.3) | **Pulse Width Unit Mismatch (`us` vs `ns`) & Non-Blocking `enableDelayMs`:** `StepTimingConfig` uses `uint16_t` $\mu\text{s}$ while `AxisHardwareConfig` uses $\text{ns}$; `enableDelayMs` needs non-blocking arming[cite: 1]. | Standardize on nanoseconds (`stepPulseWidthNs`) and track `m_enableTimestampUs` to gate step output asynchronously until `enableDelayMs` elapses[cite: 1]. |
| **`[SAFE-02]`** | **`ENHANCEMENT`** | Zero-WiFi Control Plane Mandate (`WHATELS_MASTER_ARCHITECTURE.md` §1.3 & `ELS_CLIENT_ARCHITECTURE.md` §5.3) | **Specification Contradiction on Wi-Fi Motion Commands:** §1.3 forbids motion commands over Wi-Fi while §10.1 defines `CMD.startFeed` and `CMD.jogHold` over WebSockets[cite: 1]. | Clarify that autonomous on-chip cycles may be started/stopped over WS, and enforce a strict $200\text{ ms}$ Core 1 heartbeat lease timer for `CMD.jogHold`[cite: 1]. |
| **`[STATE-01]`** | **`NOTE`** | `MotionTelemetrySnapshot`, `StatusSnapshot`, `KinematicsState` (`ELS_CORE_STATE_MODEL.md` §4.2, §5.2) | **Triple Struct Redundancy for Kinematics:** Three separate structs define Core 1 $\to$ Core 0 kinematic state with slightly different field names[cite: 1]. | Unify into a single canonical `KinematicsState` struct used directly by `TelemetryMailbox` and `MachineModel`[cite: 1]. |

---

### 3.1 Detailed Code Fixes for Actionable Findings

#### Fix `[KIN-01]` & `[KIN-02]`: Anchored Bresenham Synchronizer with Continuous Phase Tracking
```cpp
class SpindleSynchronizer {
public:
  // Called when engaging spindle sync or re-locking phase
  void IRAM_ATTR engage(int64_t currentSpindlePulses, int32_t currentAxisSteps, int32_t pitchDupr) {
    m_anchorPulses      = currentSpindlePulses;
    m_anchorSteps       = currentAxisSteps;
    m_currentTargetSteps = currentAxisSteps;
    m_pitchDupr         = pitchDupr;
  }

  // Safe live pitch trim (CMD_FEED_NUDGE) without historical multiplication jump
  void IRAM_ATTR updatePitchLive(int64_t currentSpindlePulses, int32_t newPitchDupr) {
    m_anchorSteps  = m_currentTargetSteps;
    m_anchorPulses = currentSpindlePulses;
    m_pitchDupr    = newPitchDupr;
  }

  // Computed on every Core 1 tick (even during decel ramp or shoulder dwell!)
  int32_t IRAM_ATTR computeIdealSyncSteps(
      int64_t accumulatedSpindlePulses,
      int32_t motorStepsPerRev,
      int32_t screwPitchDu,
      int32_t effectiveEncoderEdgesPerRev) {
    int64_t deltaPulses = accumulatedSpindlePulses - m_anchorPulses;
    int64_t num = deltaPulses * static_cast<int64_t>(motorStepsPerRev) * static_cast<int64_t>(m_pitchDupr);
    int64_t den = static_cast<int64_t>(screwPitchDu) * static_cast<int64_t>(effectiveEncoderEdgesPerRev);
    
    int64_t stepOffset = (num >= 0) ? ((num + den / 2) / den) : ((num - den / 2) / den);
    m_currentTargetSteps = m_anchorSteps + static_cast<int32_t>(stepOffset);
    return m_currentTargetSteps;
  }

private:
  int64_t m_anchorPulses       = 0;
  int32_t m_anchorSteps        = 0;
  int32_t m_currentTargetSteps = 0;
  int32_t m_pitchDupr          = 0;
};
```

#### Fix `[KIN-03]`: Safe Soft-Limit Lookahead Without Resting Bypass
```cpp
const SoftLimit* IRAM_ATTR findNextEnforcedLimit(
    int32_t currentSteps,
    int8_t direction,
    bool allowPassThrough = false) const {
  const SoftLimit* candidate = nullptr;
  // Only apply deadband skip if operator explicitly commanded pass-through override!
  const int32_t deadband = allowPassThrough ? ADVANCE_DEADBAND_STEPS : 0;

  if (direction < 0) {
    int32_t highestPosBelow = INT32_MIN;
    int32_t threshold = currentSteps - deadband;
    for (size_t i = 0; i < MAX_LIMITS; ++i) {
      if (m_limits[i].isEnforced() && (m_limits[i].stepPos <= threshold)) {
        if (m_limits[i].stepPos > highestPosBelow) {
          highestPosBelow = m_limits[i].stepPos;
          candidate = &m_limits[i];
        }
      }
    }
  } else if (direction > 0) {
    int32_t lowestPosAbove = INT32_MAX;
    int32_t threshold = currentSteps + deadband;
    for (size_t i = 0; i < MAX_LIMITS; ++i) {
      if (m_limits[i].isEnforced() && (m_limits[i].stepPos >= threshold)) {
        if (m_limits[i].stepPos < lowestPosAbove) {
          lowestPosAbove = m_limits[i].stepPos;
          candidate = &m_limits[i];
        }
      }
    }
  }
  return candidate;
}
```

#### Fix `[CONC-01]`: Provably Race-Free Seqlock (`TelemetryMailbox`) for Dual-Core Xtensa LX7
```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

class TelemetryMailbox {
public:
  void IRAM_ATTR publish(const MotionTelemetrySnapshot& snapshot) {
    uint32_t seq = m_seq.load(std::memory_order_relaxed);
    m_seq.store(seq + 1, std::memory_order_relaxed); // Mark odd (write in progress)
    
    // Prevent compiler or CPU from reordering payload writes before (seq + 1) store
    std::atomic_thread_fence(std::memory_order_release);
    
    std::memcpy(const_cast<MotionTelemetrySnapshot*>(&m_data), &snapshot, sizeof(MotionTelemetrySnapshot));
    
    // Ensure all payload bytes are visible before marking sequence even
    m_seq.store(seq + 2, std::memory_order_release);
  }

  bool read(MotionTelemetrySnapshot& outSnapshot) const {
    for (int retries = 0; retries < 4; ++retries) {
      uint32_t seq1 = m_seq.load(std::memory_order_acquire);
      if (seq1 & 1U) {
        continue; // Writer active; spin retry
      }

      std::memcpy(&outSnapshot, const_cast<const MotionTelemetrySnapshot*>(&m_data), sizeof(MotionTelemetrySnapshot));

      // Prevent payload reads from sinking below seq2 load
      std::atomic_thread_fence(std::memory_order_acquire);

      uint32_t seq2 = m_seq.load(std::memory_order_relaxed);
      if (seq1 == seq2) {
        return true; // Guaranteed tear-free snapshot
      }
    }
    return false;
  }

private:
  std::atomic<uint32_t>            m_seq{0};
  volatile MotionTelemetrySnapshot m_data{};
};
```

#### Fix `[CONC-02]`: Flash Write Lockout Guard in `SettingsService`
```cpp
bool SettingsService::set(const char* keyVal, String* reply) {
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS=cannot write to NVS flash while motion is active\n";
    return false;
  }
  // Proceed with validation, RAM cache update, and NVS write...
}
```

#### Fix `[SAFE-01]`: Default-Deny In-Motion Safety Gatekeeper (`IELSMode::canExecuteCommand`)
```cpp
virtual bool canExecuteCommand(CommandId cmd, ModeState state) const {
  // 1. Stop and E-Stop are inviolable and universally permitted in ALL states
  if (cmd == CommandId::CMD_STOP || cmd == CommandId::CMD_ESTOP) {
    return true;
  }

  // 2. Full configuration is ONLY permitted when stationary in IDLE or COMPLETED
  if (state == ModeState::IDLE || state == ModeState::COMPLETED) {
    return true;
  }

  // 3. During active cutting (RUNNING) or waiting for sync trigger (ARMED)
  if (state == ModeState::RUNNING || state == ModeState::ARMED) {
    if (cmd == CommandId::CMD_FEED_HOLD)    return (getLivePermissions() & PERM_FEED_HOLD) != 0;
    if (cmd == CommandId::CMD_FEED_NUDGE)   return (getLivePermissions() & PERM_LIVE_FEED_TRIM) != 0;
    if (cmd == CommandId::CMD_STEP_CYCLE)   return (getLivePermissions() & PERM_LIVE_STEP_CYCLE) != 0;
    if (cmd == CommandId::CMD_RPM_OVERRIDE) return (getLivePermissions() & PERM_LIVE_RPM_OVERRIDE) != 0;
    return false; // Lock out all other commands
  }

  // 4. During Feed Hold (PAUSED)
  if (state == ModeState::PAUSED) {
    if (cmd == CommandId::CMD_FEED_RESUME)  return true;
    if (cmd == CommandId::CMD_FEED_NUDGE)   return (getLivePermissions() & PERM_LIVE_FEED_TRIM) != 0;
    return false;
  }

  // 5. During RETRACTING or FAULT: strictly reject all non-stop commands
  return false;
}
```

#### Fix `[HAL-01]`: Zero-Loss Free-Running 16-Bit Modular PCNT Sampling
```cpp
int32_t IRAM_ATTR Esp32SpindleEncoderHAL::getPulseDelta() {
  int rawCount = 0;
  // Read hardware counter without clearing (prevents edge loss at 204.8 kHz)
  pcnt_unit_get_count(m_pcntUnit, &rawCount);
  
  int16_t current16 = static_cast<int16_t>(rawCount);
  int16_t delta16   = static_cast<int16_t>(current16 - m_lastCount16);
  m_lastCount16     = current16;
  
  m_accumulatedPulses += delta16;
  return static_cast<int32_t>(delta16);
}
```

#### Fix `[KIN-04]` & `[KIN-05]`: Multi-Axis Taper Parameters & Symmetric Rounding Conversions
```cpp
struct SpindleSyncParams {
  AxisId  axis;                  // Primary axis (AXIS_Z or AXIS_X)
  int8_t  direction;             // -1 or +1
  int32_t pitchDupr;             // Primary axis lead in du/rev
  int32_t maxRapidSpeedDuMin;    // Velocity clamp
  bool    respectSoftLimits;     // Enforce soft stops
  bool    syncIndex;             // Wait for 1/rev index pulse
  int32_t targetDistDu;          // Optional stroke distance (0 = continuous)

  // Coordinated Dual-Axis / Taper Extension (for ZX Cones & NPT Taper Threads)
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0; // Radial delta in du (signed for taper direction)
  int32_t secondaryRatioDen   = 1; // Axial delta in du (must be > 0)
};

inline int32_t IRAM_ATTR stepsToDu(const AxisGeometry& geom, int32_t steps) {
  int64_t num = static_cast<int64_t>(steps) * geom.screwPitchDu;
  int64_t den = geom.motorStepsPerRev;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}

inline int32_t IRAM_ATTR duToSteps(const AxisGeometry& geom, int32_t du) {
  int64_t num = static_cast<int64_t>(du) * geom.motorStepsPerRev;
  int64_t den = geom.screwPitchDu;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}
```

---

## 4. Implementation Readiness Checklist

Before writing the C++ implementation for Phase 1 (`src/motion/`), complete the following specification updates:

- [ ] **1. Fix `TelemetryMailbox` Memory Barriers (`CONCURRENCY_AND_TIMING.md`):** Add `std::atomic_thread_fence(std::memory_order_release)` before copying `m_data` in `publish()`, and `std::atomic_thread_fence(std::memory_order_acquire)` after reading `m_data` in `read()`[cite: 1].
- [ ] **2. Resolve Core 0 / Core 1 Mode Execution Boundary (`MODE_INTEGRATION_CONTRACT.md`):** Run `IELSMode` state machines inside the Core 1 motion loop (recommended for zero-latency multi-pass transitions at shoulder stops) or communicate with `MotionEngine` strictly through lock-free command/event queues[cite: 1]. Remove raw cross-core `MotionEngine*` method calls if `IELSMode` remains on Core 0[cite: 1].
- [ ] **3. Anchor the Bresenham Accumulator for Live Trim (`MOTION_ENGINE_SPECIFICATION.md`):** Add `m_anchorPulses` and `m_anchorSteps` to `SpindleSynchronizer` so `CMD_FEED_NUDGE` updates incremental slope from the current tool position rather than multiplying total historical spindle turns[cite: 1].
- [ ] **4. Fix Soft Limit Resting Deadband (`MOTION_ENGINE_SPECIFICATION.md`):** Update `SoftLimitTable::findNextEnforcedLimit()` so resting at `currentSteps == limit.stepPos` blocks further motion in that direction (`D_remaining = 0`) unless an explicit `allowPassThrough` override is commanded[cite: 1].
- [ ] **5. Preserve Spindle Phase During Sync Deceleration (`MOTION_ENGINE_SPECIFICATION.md`):** Specify that `SpindleSynchronizer` continues tracking spindle encoder pulses during deceleration ramps, feed hold, and shoulder dwell so reversing the spindle re-engages the leadscrew at the exact helical phase angle[cite: 1].
- [ ] **6. Default-Deny in `IELSMode::canExecuteCommand()` (`ELS_MODE_ARCHITECTURE.md`):** Lock out structural/configuration commands during `ARMED`, `RETRACTING`, and `FAULT` states, permitting full configuration only in `IDLE` and `COMPLETED`[cite: 1].
- [ ] **7. Add Compound Axis Ratio to Motion Primitives (`MODE_INTEGRATION_CONTRACT.md`):** Extend `SpindleSyncParams`, `VelocityFeedParams`, and `StrokeParams` with secondary axis ratio parameters (`enableSecondaryAxis`, `secondaryAxis`, `secondaryRatioNum`, `secondaryRatioDen`) to support `ZX` tapers and tapered threading[cite: 1].
- [ ] **8. Enforce `IRAM_ATTR` & In-Motion Flash Write Lockout (`CONCURRENCY_AND_TIMING.md` & `ELS_PROPERTIES_AND_SETTINGS.md`):** Mandate `IRAM_ATTR` / `DRAM_ATTR` for all Core 1 code/data, use modular 16-bit subtraction without counter resets for ESP32-S3 PCNT (`HAL_SPECIFICATION.md`), and reject all NVS/LittleFS writes while `isOn == true`[cite: 1].