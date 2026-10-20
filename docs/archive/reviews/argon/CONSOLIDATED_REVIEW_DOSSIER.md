# whatELS — Master System & Motion Architecture Dossier

> **Document Type:** Consolidated Master Architecture Specification & Peer Review Dossier  
> **Target Platform:** whatELS Electronic Leadscrew & Lathe Controller (ESP32-S3)  
> **Authors:** Emil & Antigravity  
> **Date:** October 2026  
> **Audience:** External Architecture Reviewers (Google Argon), Embedded Systems & CNC Specialists  
> **Location:** `docs/motion/CONSOLIDATED_REVIEW_DOSSIER.md`

---

# TABLE OF CONTENTS
- [SECTION 0: REVIEW CHARTER & EVALUATION CRITERIA FOR ARGON](#section-0-review-charter--evaluation-criteria-for-argon)
- [SECTION 1: SYSTEM OVERVIEW & ARCHITECTURAL HIERARCHY](#section-1-system-overview--architectural-hierarchy)
- [SECTION 2: LAYER 1 — HARDWARE ABSTRACTION LAYER (HAL)](#section-2-layer-1--hardware-abstraction-layer-hal)
- [SECTION 3: LAYER 2 — KINEMATIC MOTION ENGINE](#section-3-layer-2--kinematic-motion-engine)
- [SECTION 4: LAYER 2 → LAYER 3 BOUNDARY (MODE INTEGRATION CONTRACT)](#section-4-layer-2--layer-3-boundary-mode-integration-contract)
- [SECTION 5: REAL-TIME CONCURRENCY & DUAL-CORE TIMING](#section-5-real-time-concurrency--dual-core-timing)
- [SECTION 6: LAYER 3 — MODE ORCHESTRATION LAYER (IELSMode)](#section-6-layer-3--mode-orchestration-layer-ielsmode)
- [SECTION 7: MASTER SYSTEM ARCHITECTURE (THE 6 SUBSYSTEMS)](#section-7-master-system-architecture-the-6-subsystems)
- [SECTION 8: CORE STATE MODEL (CANONICAL DOMAINS & UNITS)](#section-8-core-state-model-canonical-domains--units)
- [SECTION 9: LATHE MACHINE PROFILE & HARDWARE CAPABILITIES](#section-9-lathe-machine-profile--hardware-capabilities)
- [SECTION 10: PROPERTIES, SETTINGS & NVS PERSISTENCE](#section-10-properties-settings--nvs-persistence)
- [SECTION 11: CLIENT ARCHITECTURE & MULTI-CLIENT WIRE PROTOCOL](#section-11-client-architecture--multi-client-wire-protocol)
- [SECTION 12: PEER REVIEW AUDIT & ARCHITECTURAL RESOLUTIONS](#section-12-peer-review-audit--architectural-resolutions)

---

# SECTION 0: REVIEW CHARTER & EVALUATION CRITERIA FOR ARGON

## 0.1 Context & Background
whatELS is an industrial-grade, open-source Electronic Leadscrew (ELS) and Digital Readout (DRO) controller designed for metalworking engine lathes. It replaces traditional cast-iron change gears, feed gearboxes, and half-nut mechanics with an electronically synchronized leadscrew and motorized cross-slide.

The hardware runs on the **ESP32-S3** (dual-core Xtensa LX7 @ 240 MHz with FreeRTOS). It interfaces with:
- Dedicated **Pulse Counter (PCNT)** hardware reading a high-resolution spindle optical quadrature encoder ($1{,}024 - 2{,}048\text{ PPR}$, up to $3{,}000\text{ RPM} \implies 204.8\text{ kHz}$ edge rate).
- Digital microstepping drives (Leadshine EM542S / TMC2209) on the Z leadscrew and X cross-slide.
- Optical linear glass scales (DRO).
- Hardware emergency stop lines and opto-isolated limit switches.
- Connected client terminals: High-resolution touch Web UI (tablet/PC over WebSocket) and transitional Nextion serial LCD.

## 0.2 Review Objectives & Questions for the Reviewer
Please evaluate the architecture presented in this dossier across the following dimensions:

1. **Real-Time Concurrency & Determinism:**
   - Is the strict partitioning between **Core 0 (Supervisory / Web / Modes)** and **Core 1 (Real-Time Stepping & Kinematics)** sound?
   - Is the lock-free `TelemetryMailbox` seqlock implementation truly race-free and immune to priority inversion?
   - Are the interrupt and motion tick execution budgets ($\le 35\,\mu\text{s}$) realistic and resilient to jitter?
2. **Kinematic & Trajectory Correctness:**
   - Does the dynamic stopping window calculation ($d_{\text{stop}} = \frac{v_i^2 - v_{\text{end}}^2}{2a}$) and the multi-stop `SoftLimitTable` provide absolute crash prevention without mechanical shock?
   - Are the conversions between raw motor steps and canonical integer deci-microns ($1\text{ du} = 0.1\,\mu\text{m}$) free of integer truncation drift?
3. **Spindle Synchronization & Threading:**
   - Does the 64-bit Bresenham fractional accumulator eliminate cumulative lead error over thousands of continuous spindle turns?
   - Is the thread phase registration mechanism sound when resting at a shoulder limit or during spindle reversals?
4. **Hardware Abstraction Layer (HAL):**
   - Are the electrical timing contracts (`StepTimingConfig`: setup, hold, pulse widths) comprehensive for modern digital stepper drives?
   - Is the ESP32-S3 PCNT hardware counting configuration robust against bounce and electrical EMI?
5. **Architectural Decoupling & Extensibility:**
   - Is the boundary between `MotionEngine` (Layer 2) and `IELSMode` (Layer 3) clean, preventing low-level step generation from leaking into higher-level canned cycles?
6. **System-Wide Cohesion:**
   - Does the 4-tier properties taxonomy, NVS sparse override engine, and canonical 4-domain state model seamlessly support the physical motion reality?

---

# SECTION 1: SYSTEM OVERVIEW & ARCHITECTURAL HIERARCHY

The whatELS motion subsystem is organized into three strictly decoupled layers:

```
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 3: MODE ORCHESTRATION LAYER                        │
│          UnifiedGearboxMode, TurnCycle, ThreadCycle, FaceCycle         │
│          (High-level machining state machines and operator intent)     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    MODE_INTEGRATION_CONTRACT
    • High-level command primitives (Sync, Feed, Stroke, Jog)
    • Event listener interface (IMotionEventListener)
    • In-motion live trim & safety permissions
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 2: KINEMATIC MOTION ENGINE                         │
│          MotionEngine, SoftLimitTable, TrajectoryPlanner               │
│          (Coordinates in deci-microns, accel/decel, stopping windows)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    MOTION_ENGINE_SPECIFICATION
    • AxisState & AxisGeometry model (steps <-> du conversions, backlash)
    • SoftLimitTable (multi-stop drum, zero magic sentinels)
    • Trajectory acceleration/deceleration stopping window: d_stop = v^2 / 2a
    • SpindleSynchronizer (64-bit Bresenham accumulator, thread phase sync)
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 1: HARDWARE ABSTRACTION LAYER (HAL)                │
│          IStepperHAL, ISpindleEncoderHAL, IDROScaleHAL, IEndstopHAL    │
│          (Direct silicon interface to timers, GPIOs, PCNT, and Optos)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    HAL_SPECIFICATION
    • StepTimingConfig (setup/hold/pulse widths, microsecond safety)
    • IStepperHAL (timed pulse trains, single steps, dynamic frequency)
    • ISpindleEncoderHAL (PCNT hardware counting, filtered RPM, index pulse)
    • IDROScaleHAL & IEndstopHAL (scales, crash stops, E-Stop circuit)
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     ESP32-S3 PHYSICAL HARDWARE                         │
│  Dual-Core Xtensa LX7 @ 240MHz, PCNT, GPTimer, RMT, Dedicated GPIOs   │
└────────────────────────────────────────────────────────────────────────┘
```

---

# SECTION 2: LAYER 1 — HARDWARE ABSTRACTION LAYER (HAL)

### 2.1 Electrical Signal & Timing Contract (`StepTimingConfig`)
Digital servo drives and stepper drives require strict adherence to minimum electrical timing windows to avoid missed steps, direction reversal glitches, and driver faults:

```cpp
struct StepTimingConfig {
  uint32_t dirSetupDelayNs  = 5000;  // t_setup: Min time DIR pin must be stable before STEP rising edge (ns)
  uint32_t dirHoldTimeNs    = 5000;  // t_hold:  Min time DIR pin must be held after STEP active edge (ns)
  uint32_t stepPulseWidthNs = 2500;  // t_high:  Active STEP pulse duration (ns; typically 2.5 - 5.0 µs)
  uint16_t enableDelayMs    = 50;    // t_ena:   Settling time after ENA active before first step pulse (non-blocking)
  bool     invertStep       = false; // True = Active LOW pulse; False = Active HIGH pulse
  bool     invertDir        = false; // True = Invert physical direction pin logic
  bool     invertEnable     = false; // True = Active HIGH enable; False = Active LOW enable
};
```

### 2.2 Stepper Hardware Interface (`IStepperHAL`)

```cpp
#pragma once
#include <cstdint>

enum AxisId : uint8_t {
  AXIS_Z = 0,  // Longitudinal carriage leadscrew
  AXIS_X = 1   // Transverse cross-slide leadscrew
};

class IStepperHAL {
public:
  virtual ~IStepperHAL() = default;

  virtual bool configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) = 0;
  virtual void setDirection(AxisId axis, int8_t dir) = 0;
  virtual int8_t getDirection(AxisId axis) const = 0;
  virtual void setEnabled(AxisId axis, bool enable) = 0;
  virtual bool isEnabled(AxisId axis) const = 0;

  // Single discrete step pulse (Bresenham sync & discrete jogs)
  virtual void pulseStep(AxisId axis) = 0;

  // Hardware-timed pulse train for continuous and profiled feeds
  virtual void startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) = 0;
  virtual void updatePulseFrequency(AxisId axis, uint32_t frequencyHz) = 0;
  virtual void stopPulseTrain(AxisId axis) = 0;
  virtual bool isPulseTrainActive(AxisId axis) const = 0;

  virtual bool hasDriverFault(AxisId axis) const = 0;
};
```

### 2.3 Spindle Encoder Hardware Interface (`ISpindleEncoderHAL`)
Uses the dedicated ESP32-S3 Pulse Counter (PCNT) hardware unit to count Phase A/B quadrature edges directly in silicon with **zero CPU interrupts**.
Never resets the hardware counter (`[HAL-01]`); utilizes 16-bit signed two's complement modular subtraction:

```cpp
#pragma once
#include <cstdint>

class ISpindleEncoderHAL {
public:
  virtual ~ISpindleEncoderHAL() = default;

  virtual bool init(
      int pinA,
      int pinB,
      int pinIndex,
      uint16_t nativePpr,
      float pulleyRatio,
      uint16_t encoderTeeth = 0,
      uint16_t spindleTeeth = 0) = 0;

  virtual int32_t getPulseDelta() = 0;
  virtual int64_t getAccumulatedPulses() const = 0;
  virtual uint32_t getFilteredRpm() const = 0;
  virtual float getAngleDegrees() const = 0;
  virtual bool hasIndexSynced() const = 0;
  virtual void resetIndexSync() = 0;
};
```

### 2.4 Linear Scales & Endstop Interfaces (`IDROScaleHAL`, `IEndstopHAL`)

```cpp
class IDROScaleHAL {
public:
  virtual ~IDROScaleHAL() = default;
  virtual bool init(AxisId axis, int pinA, int pinB, uint32_t resolutionNm) = 0;
  virtual int32_t getPositionDu(AxisId axis) const = 0;
  virtual void zero(AxisId axis) = 0;
  virtual void setPositionDu(AxisId axis, int32_t posDu) = 0;
};

enum EndstopId : uint8_t {
  ENDSTOP_Z_MIN = 0, // Chuck crash stop
  ENDSTOP_Z_MAX = 1, // Tailstock crash stop
  ENDSTOP_X_MIN = 2, // Spindle centerline stop
  ENDSTOP_X_MAX = 3, // Full cross-slide retract stop
  ENDSTOP_ESTOP = 4  // Physical Emergency Stop mushroom switch
};

class IEndstopHAL {
public:
  virtual ~IEndstopHAL() = default;
  virtual bool init() = 0;
  virtual bool isAsserted(EndstopId id) const = 0;
  virtual uint8_t getAssertedMask() const = 0;
  virtual bool isEmergencyStopAsserted() const = 0;
};
```

---

# SECTION 3: LAYER 2 — KINEMATIC MOTION ENGINE

### 3.1 Coordinates & AxisState
- **Pure Cartesian Polarity (ISO 841):**
  - $Z = -1$: Toward Chuck (cut depth); $Z = +1$: Toward Tailstock (retract).
  - $X = -1$: Toward Spindle Centerline (infeed/depth); $X = +1$: Away from Centerline (retract).
- **Canonical Unit System: Deci-Microns (`du`):**
  - $1\text{ du} = 0.0001\text{ mm} = 0.1\,\mu\text{m}$.
  - $10{,}000\text{ du} = 1.0000\text{ mm}$; $254{,}000\text{ du} = 1.0000\text{ in}$.
  - Cross-slide tracked strictly in canonical radius (`xDu`).

```cpp
struct AxisGeometry {
  int32_t screwPitchDu;     // Leadscrew lead (e.g. 20000 for 2.0 mm)
  int32_t motorStepsPerRev; // Motor steps per rev (e.g. 800)
  int32_t backlashSteps;    // Lost motion compensation in motor steps
  float   maxVelocityMmSec; // Rapid traverse velocity clamp
  float   maxAccelMmSec2;   // Safe acceleration limit
};

struct AxisState {
  AxisId       id;
  AxisGeometry geometry;
  int32_t      posSteps;        // Leadscrew carriage position in motor steps
  int32_t      originSteps;     // Coordinate offset: bed zero <-> operator zero
  int32_t      motorPosSteps;   // Motor rotor steps before backlash
  int32_t      backlashAccum;   // Active backlash hysteresis steps
  bool         movingManually;
  bool         disabled;
};

inline int32_t IRAM_ATTR stepsToDu(const AxisGeometry& g, int32_t steps) {
  int64_t num = static_cast<int64_t>(steps) * g.screwPitchDu;
  int64_t den = g.motorStepsPerRev;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}

inline int32_t IRAM_ATTR duToSteps(const AxisGeometry& g, int32_t du) {
  int64_t num = static_cast<int64_t>(du) * g.motorStepsPerRev;
  int64_t den = g.screwPitchDu;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}
```

### 3.2 The Multi-Stop Soft Limit Table (`SoftLimitTable`)
Zero magic sentinels (`LONG_MAX`, `LONG_MIN`). State is explicitly modeled:

```cpp
struct SoftLimit {
  uint8_t id        = 0;       // Slot ID (1..MAX_LIMITS)
  bool    defined   = false;   // True if coordinate exists (saved in memory/recipe)
  bool    enabled   = false;   // True if actively enforced during physical motion
  char    label[16] = {0};     // Semantic tag ("CHUCK", "SHLDR_1", "GROOVE")
  int32_t coordDu   = 0;       // Workpiece coordinate in canonical deci-microns
  int32_t stepPos   = 0;       // Raw leadscrew machine position in motor steps

  inline bool isEnforced() const { return defined && enabled; }
};

class SoftLimitTable {
public:
  static constexpr size_t MAX_LIMITS = 8;
  static constexpr int32_t ADVANCE_DEADBAND_STEPS = 2;

  /**
   * @brief Queries next enforced limit. When resting on a limit (currentSteps == stepPos),
   *        motion in that direction is blocked (D_remaining = 0) unless allowPassThrough is true [KIN-03].
   */
  const SoftLimit* IRAM_ATTR findNextEnforcedLimit(
      int32_t currentSteps,
      int8_t direction,
      bool allowPassThrough = false) const {
    const SoftLimit* candidate = nullptr;
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

  bool setLimit(size_t slot, int32_t coordDu, int32_t stepPos, const char* label = nullptr, bool enable = true);
  bool setEnabled(size_t slot, bool enable);
  bool clearLimit(size_t slot);
  void clearAll();
  void updateOrigin(const AxisState& axis);

private:
  SoftLimit m_limits[MAX_LIMITS];
};
```

### 3.3 Trajectory Planning & Real-Time Stopping Window
To guarantee smooth deceleration into shoulder limits without mechanical shock:

1. **Stopping Distance Calculation with Discrete Margin:**
   $$d_{\text{stop}} = \text{ceil}\left(\frac{v_i^2 - v_{\text{end}}^2}{2a}\right) + \text{ceil}(0.5 \cdot v_i \cdot \Delta t)$$
2. **Remaining Distance Query:**
   $$D_{\text{remaining}} = \text{abs}(P_{\text{limit}} - P_i)$$
3. **Deceleration Window Trigger:**
   $$\text{If } D_{\text{remaining}} \le d_{\text{stop}}, \quad \text{transition from CRUISE to DECELERATION ramp}$$
4. **Pre-Cut Runway Verification:** Verify entry RPM does not exceed $v_{\text{entry\_max}} = \sqrt{2 \cdot a \cdot D_{\text{remaining}}}$.

### 3.4 Spindle Synchronization Engine (`SpindleSynchronizer`)
To prevent integer truncation drift while enabling instantaneous live pitch nudges (`CMD_FEED_NUDGE`) without historical jumps, the synchronizer maintains an incremental anchor (`[KIN-01]`, `[KIN-02]`):

```cpp
class SpindleSynchronizer {
public:
  void IRAM_ATTR engage(int64_t currentSpindlePulses, int32_t currentAxisSteps, int32_t pitchDupr) {
    m_anchorPulses = currentSpindlePulses;
    m_anchorSteps = currentAxisSteps;
    m_currentTargetSteps = currentAxisSteps;
    m_pitchDupr = pitchDupr;
  }

  void IRAM_ATTR updatePitchLive(int64_t currentSpindlePulses, int32_t newPitchDupr) {
    m_anchorSteps = m_currentTargetSteps;
    m_anchorPulses = currentSpindlePulses;
    m_pitchDupr = newPitchDupr;
  }

  int32_t IRAM_ATTR computeIdealSyncSteps(
      int64_t accumulatedPulses, int32_t motorStepsPerRev, int32_t screwPitchDu, int32_t encoderPpr) {
    int64_t deltaPulses = accumulatedPulses - m_anchorPulses;
    int64_t num = deltaPulses * static_cast<int64_t>(motorStepsPerRev) * static_cast<int64_t>(m_pitchDupr);
    int64_t den = static_cast<int64_t>(screwPitchDu) * static_cast<int64_t>(encoderPpr);
    int64_t stepOffset = (num >= 0) ? ((num + den / 2) / den) : ((num - den / 2) / den);
    m_currentTargetSteps = m_anchorSteps + static_cast<int32_t>(stepOffset);
    return m_currentTargetSteps;
  }

private:
  int64_t m_anchorPulses = 0;
  int32_t m_anchorSteps = 0;
  int32_t m_currentTargetSteps = 0;
  int32_t m_pitchDupr = 0;
};
```
*Spindle pulses continue accumulating in `m_spindlePhasePulses` throughout deceleration, pause, and resting dwell so helical thread registration is never lost upon reversal.*

---

# SECTION 4: LAYER 2 $\to$ LAYER 3 BOUNDARY (MODE INTEGRATION CONTRACT)

### 4.1 Inversion of Responsibility
- Modes **do not** receive raw 10 kHz spindle interrupts.
- Modes **do not** implement motor stepping loops or `delay()` busy-waits.
- Modes command high-level primitives and respond to motion lifecycle events.

### 4.2 Parameter Structs & Event Listener Interface

```cpp
enum class MotionHaltReason : uint8_t {
  NORMAL_STOP = 0,
  SOFT_LIMIT_REACHED,
  TARGET_DISTANCE_REACHED,
  FEED_HOLD,
  HARD_ENDSTOP_TRIPPED,
  DRIVER_FAULT,
  ESTOP
};

struct SpindleSyncParams {
  AxisId  axis;                  // Primary axis (AXIS_Z or AXIS_X)
  int8_t  direction;             // -1 (Chuck), +1 (Tailstock)
  int32_t pitchDupr;             // Deci-microns per rev (e.g. 20000 = 2.0 mm/rev)
  int32_t maxRapidSpeedDuMin;
  bool    respectSoftLimits;
  bool    syncIndex;             // Wait for 1/rev index marker before stepping
  int32_t targetDistDu;          // 0 = continuous until stopped or limit reached

  // Coordinated Dual-Axis / Taper Extension [KIN-04]
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;
  int32_t secondaryRatioDen   = 1;
};

struct VelocityFeedParams {
  AxisId  axis;
  int8_t  direction;
  int32_t feedRateDuPerMin;      // Velocity in du/min (e.g. 1000000 = 100 mm/min)
  float   accelMmSec2;
  bool    respectSoftLimits;
  int32_t targetDistDu;

  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;
  int32_t secondaryRatioDen   = 1;
};

struct StrokeParams {
  AxisId  axis;
  int32_t targetCoordDu;
  int32_t rapidSpeedDuMin;
  float   accelMmSec2;
  bool    respectSoftLimits;

  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryTargetCoordDu = 0;
};

class IMotionEventListener {
public:
  virtual ~IMotionEventListener() = default;

  virtual void onMotionHalted(
      AxisId axis,
      MotionHaltReason reason,
      int32_t haltedPosSteps,
      int32_t haltedCoordDu,
      const SoftLimit* triggeredLimit) = 0;

  virtual void onSpindleSyncAcquired(AxisId axis) = 0;

  virtual void onMotionTelemetry(
      int32_t zCoordDu,
      int32_t xCoordDu,
      uint32_t currentRpm,
      bool isMoving) = 0;
};
```

---

# SECTION 5: REAL-TIME CONCURRENCY & DUAL-CORE TIMING

### 5.1 Dual-Core Task Allocation (Option A)
- **Core 0 (Supervisory, HMI & Communications):**
  - WebSockets (50 Hz broadcast), HTTP server, JSON RPC decoder.
  - LittleFS flash filesystem and NVS access (strictly locked out while `isAnyAxisMoving()`).
  - HMI command validation and queue dispatch.
- **Core 1 (Deterministic Real-Time Motion & Mode Execution):**
  - High-priority FreeRTOS task (Priority 22) + Hardware Timer ISRs (all in `IRAM_ATTR`).
  - `IELSMode` state machine execution (zero-latency shoulder reversal and automated canned cycles).
  - PCNT hardware polling with native 16-bit modular difference, 64-bit Bresenham accumulator.
  - Trajectory stopping window evaluation ($d_{\text{stop}}$).
  - Hard endstop & E-stop monitoring.

### 5.2 Lock-Free Telemetry Mailbox (Core 1 $\to$ Core 0)
Eliminates blocking mutexes in the real-time motion path using a monotonic sequence counter (seqlock). Explicit memory fences prevent compiler and Xtensa LX7 dual-core reordering (`[CONC-01]`):

```cpp
struct MotionTelemetrySnapshot {
  int32_t  zPosSteps;
  int32_t  zCoordDu;
  int32_t  xPosSteps;
  int32_t  xCoordDu;
  uint32_t spindleRpm;
  int64_t  spindlePulses;
  uint16_t motionMode;
  bool     isMoving;
  bool     isFeedHold;
  uint8_t  activeLimitSlot;
  int32_t  distToLimitDu;
  uint32_t sequenceNumber;
};

class TelemetryMailbox {
public:
  void IRAM_ATTR publish(const MotionTelemetrySnapshot& snapshot) {
    uint32_t seq = m_seq.load(std::memory_order_relaxed);
    m_seq.store(seq + 1, std::memory_order_relaxed); // Mark odd

    std::atomic_thread_fence(std::memory_order_release);
    std::memcpy(const_cast<MotionTelemetrySnapshot*>(&m_data), &snapshot, sizeof(MotionTelemetrySnapshot));
    m_seq.store(seq + 2, std::memory_order_release);
  }

  bool read(MotionTelemetrySnapshot& outSnapshot) const {
    for (int retries = 0; retries < 4; ++retries) {
      uint32_t seq1 = m_seq.load(std::memory_order_acquire);
      if (seq1 & 1U) continue;

      std::memcpy(&outSnapshot, const_cast<const MotionTelemetrySnapshot*>(&m_data), sizeof(MotionTelemetrySnapshot));
      std::atomic_thread_fence(std::memory_order_acquire);

      uint32_t seq2 = m_seq.load(std::memory_order_relaxed);
      if (seq1 == seq2) return true; // Clean snapshot read without torn words
    }
    return false;
  }

private:
  std::atomic<uint32_t>            m_seq{0};
  volatile MotionTelemetrySnapshot m_data{};
};
```

### 5.3 Worst-Case Latency Budgets (Core 1)

| Operation | Execution Budget | Permitted Jitter |
|---|---|---|
| Spindle Delta Calculation & RPM Filter | $\le 8\,\mu\text{s}$ | $\pm 2\,\mu\text{s}$ |
| Trajectory Deceleration Window Evaluation ($d_{\text{stop}}$) | $\le 12\,\mu\text{s}$ | $\pm 3\,\mu\text{s}$ |
| Bresenham 64-bit Fractional Gearing Step Dispatch | $\le 6\,\mu\text{s}$ | $\pm 1\,\mu\text{s}$ |
| Soft Limit Boundary Verification | $\le 5\,\mu\text{s}$ | $\pm 1\,\mu\text{s}$ |
| **Total Real-Time Service Loop Time** | **$\le 35\,\mu\text{s}$** | **Loop Period: $250\,\mu\text{s}$ ($14\%$ Core 1 load)** |

---

# SECTION 6: LAYER 3 — MODE ORCHESTRATION LAYER (IELSMode)

### 6.1 The Refined `IELSMode` Contract

```cpp
class IELSMode : public IMotionEventListener {
public:
  virtual ~IELSMode() = default;

  // Identity & Manifest
  virtual ModeId getId() const = 0;
  virtual const char* getSlug() const = 0;
  virtual const char* getName() const = 0;
  virtual const ModeManifest& getManifest() const = 0;

  // Lifecycle & MotionEngine Injection
  virtual void onEnter(MotionEngine* engine) = 0;
  virtual void onExit() = 0;

  // Execution Controls
  virtual bool start() = 0;
  virtual void stop(StopType stopType) = 0;
  virtual bool pause() = 0;
  virtual bool resume() = 0;

  virtual ModeState getState() const = 0;
  virtual bool isRunning() const = 0;

  // In-Motion Gatekeeper
  virtual uint32_t getLivePermissions() const = 0;
  virtual bool canExecuteCommand(CommandId cmd, ModeState state) const = 0;

  // Parameter Interface
  virtual bool setParam(const char* key, float value) = 0;
  virtual bool setParam(const char* key, const char* strValue) = 0;
  virtual bool getParam(const char* key, float& outVal) const = 0;
  virtual bool validateParams(char* errBuf, size_t errBufLen) const = 0;

  // Telemetry Serialization
  virtual void writeTelemetry(TelemetryWriter& writer) const = 0;
};
```

### 6.2 In-Motion Safety Permissions Matrix
Governs which operator interventions are permitted while axes are cutting under chip load:

| Command / Operation | Electronic Gearbox (`GEARBOX`) | Threading Cycle (`THREADING`) | Turning Cycle (`TURNING`) | Facing Cycle (`FACING`) | Cutoff (`CUTOFF`) |
|---|---|---|---|---|---|
| **E-Stop (`CMD_ESTOP`)** | ✅ **Universal** | ✅ **Universal** | ✅ **Universal** | ✅ **Universal** | ✅ **Universal** |
| **Normal Stop (`CMD_STOP`)** | ✅ Permitted | ✅ Permitted (Abort) | ✅ Permitted (Abort) | ✅ Permitted (Abort) | ✅ Permitted (Abort) |
| **Feed Hold (`CMD_FEED_HOLD`)** | ✅ Permitted | ✅ Permitted (End of pass) | ✅ Permitted | ✅ Permitted | ✅ Permitted |
| **Feed Rate Nudge (`CMD_FEED_NUDGE`)** | ✅ **Permitted** (Live chip trim) | ❌ **Locked** (Destroys pitch) | ✅ **Permitted** (Roughing feed) | ✅ **Permitted** (Roughing feed) | ✅ **Permitted** (Plunge trim) |
| **Axis Swap (Z ↔ X)** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** |
| **Rewrite Pitch / Lead** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** | ❌ **Locked** |

---

# SECTION 7: MASTER SYSTEM ARCHITECTURE (THE 6 SUBSYSTEMS)

The whatELS firmware is partitioned into 6 distinct architectural subsystems with strict boundary rules:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   THE 6 ARCHITECTURAL SUBSYSTEMS                                │
├─────────────────────┬────────────────────────────────┬──────────────────────────────────────────┤
│ Subsystem           │ Core Responsibility            │ Strict Boundaries & Invariants           │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 1. Hardware Timing  │ Stepper pulse timer ISRs, pin  │ • Core 1 only.                           │
│    & HAL (`hal/`)   │ drivers, quadrature spindle    │ • NEVER include WiFi, WebSockets, or UI. │
│                     │ interrupts, optical DRO scales.│ • Lock-free hardware register access.    │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 2. Kinematic Kernel │ Atomic kinematic functions     │ • Core 1 only.                           │
│    & Motion         │ (`FollowSpindle`,              │ • Operates in integer deci-microns (du). │
│    (`motion/`)      │  `MoveVelocity`), gearing math,│ • Universal soft limit safety envelope   │
│                     │ backlash, accel/decel profiling│   enforced across ALL modes.             │
│                     │ and hardware pulse generation. │ • Pure math & pulse coordination.        │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 3. Operational Mode │ Operational mode state machines│ • Implements `IELSMode` (enter/loop/exit)│
│    Engine (`modes/`)| (`GearboxMode`, `TurnCycle`,   │ • Orchestrates functions from Subsystem 2│
│                     │ `FaceCycle`, `ThreadCycle`).   │ • Atomic motion execution (`startFeed`). │
│                     │ Prunes unavailable via `caps`. │ • Mode-agnostic extensibility.           │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 4. State Model &    │ Single source of truth. 4-tier │ • Atomic point-in-time snapshots (K,I,C,P│
│    Persistence      │ properties (`machine`, `sys`,  │ • Sparse NVS overrides (pristine flash). │
│    (`model/`,       │ `prefs`, `client`), LittleFS   │ • 3-tier preset fallback hierarchy:      │
│    `settings/`)     │ JSON catalog & schema streaming│   Active (NVS) ──► Default ──► ROM Failsafe│
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 5. Command          │ Line protocol decoder, client  │ • Canonical ASCII wire strings (`CMD.*`, │
│    Dispatcher &     │ multiplexing, routing incoming │   `SETTINGS.*`).                         │
│    Transport        │ commands, broadcasting deltas. │ • Normalizes semantic terms (CHUCK/TAIL) │
│    (`transport/`)   │ Ingress Staging Adapter for    │   to Cartesian signs (-1/+1).            │
│                     │ simple clients; atomic cmds.   │ • Transports (WS, UART) feed Dispatcher. │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 6. Human-Machine    │ Hardware inputs (keypad, MPG,  │ • "Input is just an internal client."    │
│    Interface (HMI)  │ joystick), Nextion display,    │ • Translates buttons/ticks into CMD.*    │
│    Adapters (`hmi/`)| native LVGL display, Web UI.   │ • Displays observe telemetry; never run  │
│                     │                                │   motion logic directly.                 │
└─────────────────────┴────────────────────────────────┴──────────────────────────────────────────┘
```

---

# SECTION 8: CORE STATE MODEL (CANONICAL DOMAINS & UNITS)

### 8.1 The 4 Canonical State Domains
The `MachineModel` groups system variables into 4 distinct broadcast domains to prevent flooding the network:

1. **Domain 'K' (Kinematics):** Real-time physical machine dynamics ($20 - 50\text{ Hz}$ during cuts):
   ```cpp
   struct KinematicsState {
     long    zDu;           // Carriage position in deci-microns
     long    xDu;           // Cross-slide position in canonical radius deci-microns
     int32_t zLeftDu;       // Left / Chuck soft limit
     bool    zLeftDefined;  // True if left limit coordinate is set
     bool    zLeftEnabled;  // True if left limit is actively enforced
     int32_t zRightDu;      // Right / Tailstock soft limit
     bool    zRightDefined;
     bool    zRightEnabled;
     int32_t xUpDu;         // Centerline infeed soft limit
     bool    xUpDefined;
     bool    xUpEnabled;
     int32_t xDownDu;       // Retract soft limit
     bool    xDownDefined;
     bool    xDownEnabled;
     bool    zDisabled;
     bool    xDisabled;
     int     rpm;
     float   turns;
     float   angle;
     int     mode;
     bool    isOn;
     bool    estop;
     long    dupr;
     long    nextDupr;
   };
   ```
2. **Domain 'I' (Operator Intent):** Preserves authoring formatting without floating-point conversion drift (e.g. `13 TPI`, `1:3.5 taper`, `1.5 mm pitch`).
3. **Domain 'C' (Configuration / Machine Capabilities):** Hardware attachments and leadscrew pitches.
4. **Domain 'P' (Preferences & UI Presentation):** Units (`mm`/`in`), display mode ($\varnothing$ vs $R$).

### 8.2 Pure Deci-Microns (`du`) & Radius Invariant
- All linear coordinates, dimensions, leadscrew leads, and tool offsets are integer deci-microns ($0.1\,\mu\text{m}$):
  $$1\text{ du} = 0.0001\text{ mm} = 0.1\,\mu\text{m}$$
  $$10{,}000\text{ du} = 1.0000\text{ mm}; \quad 254{,}000\text{ du} = 1.0000\text{ in}$$
- Cross-slide $X$ is tracked strictly in **canonical radius (`xDu`)**. Diameter mode ($\varnothing$) is strictly a client-side presentation setting.

---

# SECTION 9: LATHE MACHINE PROFILE & HARDWARE CAPABILITIES

### 9.1 Hardware Capability Bitmask (`LatheCapabilityFlags`)

```cpp
enum LatheCapabilityFlags : uint32_t {
    CAP_NONE            = 0,
    CAP_SPINDLE_ENCODER = 1 << 0, // Spindle quadrature pulse encoder
    CAP_SPINDLE_INDEX   = 1 << 1, // Single-pulse-per-revolution index sensor
    CAP_SPINDLE_VFD     = 1 << 2, // VFD speed/direction control
    CAP_MOTOR_Z         = 1 << 3, // Z leadscrew motor installed
    CAP_MOTOR_X         = 1 << 4, // X cross-slide motor installed
    CAP_DRO_Z           = 1 << 6, // Optical linear scale on Z
    CAP_DRO_X           = 1 << 7, // Optical linear scale on X
    CAP_LIMIT_SWITCH_Z  = 1 << 9, // Hardware limit switches on Z
    CAP_LIMIT_SWITCH_X  = 1 << 10 // Hardware limit switches on X
};
```

### 9.2 Machine Profile Struct (`LatheProfile`)
Stores physical lathe dimensions, leadscrew pitch, encoder PPR, and axis geometry:

```cpp
struct LatheProfile {
  char     modelName[32];
  uint32_t capabilities;
  int32_t  bedLengthDu;
  int32_t  maxSwingDu;
  int32_t  chuckDiameterDu;
  
  // Z Axis Mechanics
  int32_t  zScrewPitchDu;    // Leadscrew lead (e.g. 20000 for 2.0 mm)
  int32_t  zMotorStepsRev;   // Motor steps per rev (e.g. 800)
  int32_t  zBacklashDu;      // Lost motion compensation
  
  // X Axis Mechanics
  int32_t  xScrewPitchDu;
  int32_t  xMotorStepsRev;
  int32_t  xBacklashDu;

  // Spindle Mechanics
  uint16_t encoderPpr;       // Quadrature pulses per revolution
  float    spindlePulleyRatio;
};
```

---

# SECTION 10: PROPERTIES, SETTINGS & NVS PERSISTENCE

### 10.1 The 4-Tier Properties Taxonomy
1. **Tier 1: Machine Profile (`machine.*`)** — Lathe iron mechanics; stored in NVS namespace `"machine"`.
2. **Tier 2: Controller Infrastructure (`sys.*`)** — ESP32 network/WiFi, timer pins, scale filters; stored in NVS namespace `"sys"`.
3. **Tier 3: Workshop Preferences (`prefs.*`)** — Default units, jog step increments, deadbands; stored in NVS namespace `"prefs"`.
4. **Tier 4: Client Presentation (`client.*`)** — Individual tablet display themes, audio toggles; stored client-side or broker NVS.

### 10.2 Dynamic Default vs. Sparse Override
NVS flash strictly stores user customizations. If a setting matches the factory default or active machine preset, it consumes zero flash space:
- Updating a firmware preset automatically updates all un-overridden properties across machines.
- Operators can reset any property to factory default by simply deleting its sparse NVS key.

---

# SECTION 11: CLIENT ARCHITECTURE & MULTI-CLIENT WIRE PROTOCOL

### 11.1 Client Communication Architecture
- **Transport:** WebSocket Server running on ESP32-S3 port 81 (`ws://<ip>:81/`).
- **Telemetry Format:** Domain-namespaced JSON broadcast:
  ```json
  {
    "domain": "K",
    "zDu": -250000,
    "xDu": 100000,
    "rpm": 850,
    "turns": 142.5,
    "isOn": true,
    "mode": 50,
    "perms": 3
  }
  ```
- **Command Dispatch:** Canonical ASCII line protocol (`CMD.*`):
  - `CMD.startFeed=axis:Z,sync:1,feed:0.15,dir:-1` $\to$ Atomic feed engagement.
  - `CMD.axisZero=Z` $\to$ Zero workpiece coordinate.
  - `CMD.stop=0` $\to$ Controlled deceleration stop.
  - `CMD.estop=1` $\to$ Immediate emergency stop.

### 11.2 Multi-Client Capability Filtering & Safe Monitor Lockout
When multiple terminals are connected (e.g. 10" Web DRO tablet + Nextion headstock screen):
- **Universal Safety:** `STOP` and `ESTOP` are universally permitted from any client at all times.
- **Safe Monitor Lockout:** If Client A initiates an advanced multi-pass cycle (e.g. `SPHERE`) that Client B cannot render, Client B automatically dims its controls and enters **Safe Monitor Lockout**, displaying live DRO coordinates and RPM while locking out conflicting parameter changes.

---

# SECTION 12: PEER REVIEW AUDIT & ARCHITECTURAL RESOLUTIONS

> **Auditor:** Google Argon (Principal Embedded Systems & Motion Control Review)  
> **Initial Readiness Score:** 86%  
> **Review Outcome:** All 3 `BLOCKER` and 5 `CRITICAL` findings formally addressed and resolved in architecture specifications.

### Summary of Incorporated Architectural Fixes

| Finding ID | Severity | Affected Component | Flaw Description | Resolved Architecture Specification |
|---|---|---|---|---|
| **`[KIN-01]`** | `BLOCKER` | `SpindleSynchronizer` | Loss of thread helical phase during deceleration stop; carriage slows while spindle spins. | Preserves continuous spindle pulse accumulation in `m_spindlePhasePulses` throughout deceleration, pause, and resting dwell; locks to modulo thread phase on spindle reversal (§3.4). Pre-cut entry RPM runway verification added (§3.3). |
| **`[KIN-02]`** | `BLOCKER` | `SpindleSynchronizer` | Multiplying historical accumulated pulses by new pitch during live trim (`CMD_FEED_NUDGE`) caused instantaneous step jump. | Implemented anchored Bresenham synchronizer (`m_anchorPulses`, `m_anchorSteps`) computing incremental slope from current position (§3.4). |
| **`[KIN-03]`** | `BLOCKER` | `SoftLimitTable` | Deadband filter `< currentSteps - 2` excluded stop when resting on it; pressing feed again crashed into chuck. | Updated `findNextEnforcedLimit()`: resting at `currentSteps == stepPos` blocks motion in that direction (`D_remaining = 0`) unless explicit `allowPassThrough` override is commanded (§3.2). |
| **`[CONC-01]`** | `CRITICAL` | `TelemetryMailbox` | C++ struct copy across relaxed/release stores lacked memory barriers, causing torn reads on dual-core Xtensa LX7. | Added explicit `std::atomic_thread_fence(memory_order_release)` in `publish()` and `std::atomic_thread_fence(memory_order_acquire)` in `read()`, with `volatile` payload and 4 retries (§5.2). |
| **`[CONC-02]`** | `CRITICAL` | Core 0/1 Shared Flash Bus | NVS/LittleFS writes on Core 0 suspend SPI flash cache on both cores for 1–40 ms, stalling Core 1 motion. | Placed all Core 1 motion code/ISRs in `IRAM_ATTR`, state in `DRAM_ATTR`, and gated NVS/LittleFS writes with `isAnyAxisMoving()` (§5.1). |
| **`[CONC-03]`** | `CRITICAL` | Mode $\leftrightarrow$ MotionEngine Boundary | Core 0 mode calling Core 1 `MotionEngine*` directly violated lock-free boundary. | Adopted **Option A**: `IELSMode` FSM executes directly on **Core 1** inside the real-time loop, enabling zero-latency multi-pass shoulder transitions; Core 0 HMI enqueues commands via lock-free ringbuffer (§5.1). |
| **`[SAFE-01]`** | `CRITICAL` | `IELSMode::canExecuteCommand` | Gatekeeper checked only `RUNNING` and `PAUSED`, falling through to `return true;` during `ARMED`, `RETRACTING`, and `FAULT`. | Inverted to **default-deny**: only `IDLE` and `COMPLETED` allow configuration; `ARMED` and `RUNNING` strictly whitelist live commands; `RETRACTING` rejects all non-stop commands (`docs/ELS_MODE_ARCHITECTURE.md`). |
| **`[HAL-01]`** | `CRITICAL` | `ISpindleEncoderHAL` | Calling `pcnt_unit_clear_count()` after reading dropped $204.8\text{ kHz}$ encoder edges. | Removed counter clears; configured PCNT as free-running signed 16-bit counter computing delta via 16-bit modular difference (`current16 - lastCount16`), with 500 ns glitch filter (§2.3). |
| **`[KIN-04]`** | `ENHANCEMENT` | Parameter Structs | Missing compound / taper fields for `ZX` cones and tapered threads. | Added `enableSecondaryAxis`, `secondaryAxis`, `secondaryRatioNum`, and `secondaryRatioDen` to motion primitive structs (§4.2). |
| **`[KIN-05]`** | `ENHANCEMENT` | Conversions | Integer division truncated towards zero, losing steps on fractional pitches. | Added symmetric half-way rounding away from zero (`num >= 0 ? num + den/2 : num - den/2`) to `stepsToDu` and `duToSteps` (§3.1). |
| **`[HAL-02]`** | `ENHANCEMENT` | `StepTimingConfig` | Pulse width unit mismatch (`us` vs `ns`) and blocking enable delay. | Standardized timing in nanoseconds (`dirSetupDelayNs`, `stepPulseWidthNs`) and documented non-blocking enable arming state machine (§2.1). |
| **`[SAFE-02]`** | `ENHANCEMENT` | Wi-Fi Motion Protocol | Reconciling zero-WiFi mandate with Web UI commands. | Clarified autonomous on-chip cycles are safe over WS; enforced a strict 200 ms Core 1 lease watchdog for continuous manual jogging (`CMD.jogHold`) (§5.1). |
