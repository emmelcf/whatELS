# NanoELS Motion Engine & Kinematic Core Specification

> **Status:** Authoritative Engineering Specification  
> **Target Version:** NanoELS H5 Modular Architecture  
> **Audience:** Firmware Engineers, Motion Control Specialists  
> **Location:** `docs/motion/MOTION_ENGINE_SPECIFICATION.md`  
> **Layer Dependency:** Built on `docs/motion/HAL_SPECIFICATION.md`

---

## 1. Executive Summary & Architectural Scope

The **Motion Engine (`MotionEngine`)** is the mathematical and kinematic core of NanoELS. It translates operator intent (commanded feed rates, leadscrew thread leads, target distances, virtual soft limits) into precisely timed, acceleration-profiled step pulse sequences executed via `IStepperHAL`.

```
┌─────────────────────────────────────────────────────────────┐
│             LAYER 3: MODE ORCHESTRATION LAYER               │
│          (UnifiedGearboxMode, Canned Cycles, HMI)           │
└──────────────────────────────┬──────────────────────────────┘
                               │ Commands: startFeed, jog, setLimit
                               ▼
┌─────────────────────────────────────────────────────────────┐
│             LAYER 2: KINEMATIC MOTION ENGINE                │
│                                                             │
│  ┌───────────────────────┐       ┌───────────────────────┐  │
│  │     AxisState (Z,X)   │       │    SoftLimitTable     │  │
│  │  (Pos, Origin, Steps) │       │(Multi-Stop Drum/Stops)│  │
│  └───────────┬───────────┘       └───────────┬───────────┘  │
│              │                               │              │
│              ▼                               ▼              │
│  ┌───────────────────────────────────────────────────────┐  │
│  │       CNC-Grade Trajectory & Accel/Decel Engine       │  │
│  │   (Trapezoidal/S-Curve Profiler, Soft Limit Guard)    │  │
│  └───────────────────────────┬───────────────────────────┘  │
│                              │                              │
│              ┌───────────────┴───────────────┐              │
│              ▼                               ▼              │
│  ┌───────────────────────┐       ┌───────────────────────┐  │
│  │  SpindleSynchronizer  │       │     VelocityFeed      │  │
│  │ (ELS Electronic Gear) │       │ (Continuous PowerFeed)│  │
│  └───────────┬───────────┘       └───────────┬───────────┘  │
└──────────────┼───────────────────────────────┼──────────────┘
               │                               │
               ▼                               ▼
┌─────────────────────────────────────────────────────────────┐
│             LAYER 1: HARDWARE ABSTRACTION (HAL)             │
│                 IStepperHAL, ISpindleEncoder                │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Core Domain Entities & Coordinates

### 2.1 Pure Cartesian Machine Polarity (ISO 841)
- **$Z$ (Longitudinal Carriage):**
  - $-1$ (Negative): Toward Chuck / Spindle Nose ($-Z$, cut depth).
  - $+1$ (Positive): Toward Tailstock ($+Z$, retract).
- **$X$ (Transverse Cross-Slide):**
  - $-1$ (Negative): Toward Spindle Centerline ($-X$, infeed/plunge).
  - $+1$ (Positive): Away from Centerline ($+X$, retract).

### 2.2 Canonical Unit System: Deci-Microns (`du`)
- All user-facing linear positions and travel dimensions are stored in **canonical integer deci-microns** ($0.1\,\mu\text{m}$):
  $$1\text{ du} = 0.0001\text{ mm} = 0.1\,\mu\text{m}$$
  $$10{,}000\text{ du} = 1.0000\text{ mm}$$
  $$254{,}000\text{ du} = 1.0000\text{ in}$$
- Cross-slide ($X$) is tracked strictly in **canonical radius** (`xDu`).

### 2.3 The `AxisState` Model
The `AxisState` struct isolates kinematic position tracking from physical GPIO pins:

```cpp
#pragma once
#include <cstdint>
#include "docs/motion/HAL_SPECIFICATION.md"

struct AxisGeometry {
  int32_t screwPitchDu;    // Leadscrew lead in deci-microns (e.g. 20000 for 2.0mm)
  int32_t motorStepsPerRev;// Steps per complete leadscrew revolution (e.g. 800)
  int32_t backlashSteps;   // Lost motion compensation in motor steps
  float   maxVelocityMmSec;// Physical rapid limit
  float   maxAccelMmSec2;  // Safe acceleration limit
};

struct AxisState {
  AxisId       id;
  AxisGeometry geometry;

  // Pure Kinematic Positions
  int32_t posSteps;        // Physical carriage position in raw motor steps
  int32_t originSteps;     // Coordinate offset between physical bed zero and operator zero
  int32_t motorPosSteps;   // Motor rotor position before backlash compensation
  int32_t backlashAccum;   // Active backlash hysteresis steps [0, backlashSteps]
  
  // Power state
  bool    movingManually;  // True during handwheel/MPG jogging
  bool    disabled;        // True if motor driver is de-energized
};
```

#### Canonical Conversion Primitives:
```cpp
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

inline int32_t getWorkpieceCoordDu(const AxisState& axis) {
  return stepsToDu(axis.geometry, axis.posSteps + axis.originSteps);
}
```

---

## 3. The Multi-Stop Soft Limit Subsystem (`SoftLimitTable`)

A soft limit represents an operator-clamped virtual stop (such as a chuck shoulder stop or thread relief groove).

### 3.1 The `SoftLimit` Entity
Zero magic sentinels (`LONG_MAX`, `LONG_MIN`). State is explicitly modeled:

```cpp
struct SoftLimit {
  uint8_t id        = 0;       // Slot ID (1..MAX_LIMITS)
  bool    defined   = false;   // True if coordinate exists (saved in memory/recipe)
  bool    enabled   = false;   // True if actively enforced during physical motion
  char    label[16] = {0};     // Semantic tag (e.g. "CHUCK", "SHLDR_1", "GROOVE")
  int32_t coordDu   = 0;       // Workpiece coordinate in canonical deci-microns
  int32_t stepPos   = 0;       // Raw leadscrew machine position in motor steps

  inline bool isEnforced() const {
    return defined && enabled;
  }
};
```

### 3.2 The `SoftLimitTable` Class & Safe Lookahead (`[KIN-03]`)

```cpp
class SoftLimitTable {
public:
  static constexpr size_t MAX_LIMITS = 8;
  static constexpr int32_t ADVANCE_DEADBAND_STEPS = 2; // Steps past stop to advance when overriding

  /**
   * @brief Queries the immediate next enforced soft limit strictly ahead in the motion path.
   * @details If resting directly on an active limit (currentSteps == stepPos), motion in that
   *          direction is BLOCKED (returns that limit with D_remaining = 0) unless allowPassThrough is true.
   * @param currentSteps Current axis position in raw motor steps.
   * @param direction Cartesian motion polarity (-1 toward Chuck/Center, +1 toward Tail/Retract).
   * @param allowPassThrough Set true ONLY when operator explicitly commands a pass-through override.
   * @return Pointer to next active limit, or nullptr if path is completely unbounded.
   */
  const SoftLimit* IRAM_ATTR findNextEnforcedLimit(
      int32_t currentSteps,
      int8_t direction,
      bool allowPassThrough = false) const {
    const SoftLimit* candidate = nullptr;
    const int32_t deadband = allowPassThrough ? ADVANCE_DEADBAND_STEPS : 0;

    if (direction < 0) {
      // Negative travel: find closest enforced limit at or below current position threshold
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
      // Positive travel: find closest enforced limit at or above current position threshold
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

  // Backwards-compatible convenience getters for single-limit displays
  const SoftLimit* getLeftLimit() const { return m_limits[0].isEnforced() ? &m_limits[0] : nullptr; }
  const SoftLimit* getRightLimit() const { return m_limits[1].isEnforced() ? &m_limits[1] : nullptr; }

  bool setLimit(size_t slot, int32_t coordDu, int32_t stepPos, const char* label = nullptr, bool enable = true);
  bool setEnabled(size_t slot, bool enable);
  bool clearLimit(size_t slot);
  void clearAll();
  void updateOrigin(const AxisState& axis);

  const SoftLimit& getSlot(size_t slot) const;

private:
  SoftLimit m_limits[MAX_LIMITS];
};
```

---

## 4. Modern CNC Acceleration & Trajectory Engine

### 4.1 Trajectory Mathematical Model
To prevent tool chatter, carriage vibration, and missed steps, motion is governed by acceleration and velocity constraints:

$$\text{Profile Parameters}: \quad v_{\text{start}}, \quad v_{\text{cruise}}, \quad a_{\text{max}}, \quad j_{\text{max}}$$

```
Velocity (v)
     ^
v_max|          ┌───────────────────────────┐  (Cruising)
     |         /                             \
     |        /                               \
v_min|  ─────┘                                 └─────  (Target / Soft Limit Halt)
     |  (Acceleration Ramp)                    (Deceleration Ramp)
     +───────────────────────────────────────────────────> Time (t)
```

### 4.2 Real-Time Deceleration Window Calculation & Directional Boundary Safety
At every step position $P_i$ during motion at velocity $v_i$ with direction $\text{dir} \in \{-1, +1\}$:

1. **Calculate stopping distance with discrete sample margin:**
   $$d_{\text{stop}} = \text{ceil}\left(\frac{v_i^2 - v_{\text{end}}^2}{2a}\right) + \text{ceil}(0.5 \cdot v_i \cdot \Delta t)$$
2. **Directional path clearance to the next soft limit or target coordinate:**
   $$D_{\text{remaining}} = (P_{\text{limit}} - P_i) \cdot \text{dir}$$
   *Boundary Hit Condition*: If $D_{\text{remaining}} \le 0$, the boundary has been met or crossed. The axis halts immediately with zero runaway, avoiding naive `abs() == 0` equality traps.
3. **Deceleration Condition:**
   $$\text{If } D_{\text{remaining}} \le d_{\text{stop}}, \quad \text{transition from CRUISE to DECELERATION ramp!}$$
4. **Discrete Residual Step Completion:**
   When the trapezoidal profile decelerates to $v = 0$, any residual steps remaining to the exact target step ($N \le 4$ steps caused by discrete sample margin) are stepped out at baseline rate to ensure the cut lands on the exact commanded coordinate without leaving an unfinished $0.05\text{–}0.10\text{ mm}$ stub.
5. **Pre-Cut Runway Verification:** Before engaging synchronous gearing or feed, verify that current spindle RPM does not exceed the maximum entry velocity allowable for the runway:
   $$v_{\text{entry\_max}} = \sqrt{2 \cdot a \cdot D_{\text{remaining}}}$$

---

## 5. Spindle Synchronous Gearing Engine (`SpindleSynchronizer`)

In synchronized electronic leadscrew mode, carriage motion is geometrically locked to the rotating spindle.

### 5.1 Anchored Bresenham Accumulator & Safe Live Trim (`[KIN-02]`)
To prevent integer truncation drift over millions of revolutions while allowing instantaneous live pitch trims (`CMD_FEED_NUDGE`) without historical jumps, the synchronizer maintains an **anchor point** (`m_anchorPulses`, `m_anchorSteps`):

```cpp
class SpindleSynchronizer {
public:
  void IRAM_ATTR engage(int64_t currentSpindlePulses, int32_t currentAxisSteps, int32_t pitchDupr) {
    m_anchorPulses       = currentSpindlePulses;
    m_anchorSteps        = currentAxisSteps;
    m_currentTargetSteps = currentAxisSteps;
    m_pitchDupr          = pitchDupr;
  }

  // Safe live pitch trim (CMD_FEED_NUDGE) - updates incremental slope from current position
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

  // Exact closed-form inverse calculation for cross-axis limit clamping [KIN-04]
  int64_t IRAM_ATTR calculatePulsesForStep(
      int32_t targetStep,
      int32_t motorStepsPerRev,
      int32_t screwPitchDu,
      int32_t effectiveEncoderEdgesPerRev) const {
    if (motorStepsPerRev == 0 || m_pitchDupr == 0) return m_anchorPulses;
    int64_t stepOffset = targetStep - m_anchorSteps;
    int64_t num = stepOffset * static_cast<int64_t>(screwPitchDu) * static_cast<int64_t>(effectiveEncoderEdgesPerRev);
    int64_t den = static_cast<int64_t>(motorStepsPerRev) * static_cast<int64_t>(m_pitchDupr);
    if (den == 0) return m_anchorPulses;
    return m_anchorPulses + (num / den);
  }

private:
  int64_t m_anchorPulses       = 0;
  int32_t m_anchorSteps        = 0;
  int32_t m_currentTargetSteps = 0;
  int32_t m_pitchDupr          = 0;
};
```

### 5.2 Continuous Spindle Phase Accumulation & Shoulder Reversal (`[KIN-01]`)
When the carriage decelerates into a soft limit shoulder or pauses in Feed Hold:
1. Stepping decelerates along the safe acceleration envelope and halts at `p = limit.stepPos`.
2. **Spindle encoder pulses continue accumulating uninterrupted in `m_spindlePhasePulses` throughout deceleration, pause, and resting dwell**.
3. When the operator or canned cycle reverses spindle rotation or commands carriage return, the synchronizer tracks modulo thread phase so the tool locks back onto the exact original thread helix without stripped threads or cross-threading.

### 5.3 Coordinated Multi-Axis Limit Coupling & Pulse Clamping (`[KIN-04]`)
In coordinated dual-axis operations ($Z+X$ taper and cone cuts):
1. **The Partner-Axis Race Condition**: If $X$ hits its $-5{,}000\text{ du}$ soft limit at pulse 2400, but a 1 ms tick arrives with 12 pulses (advancing spindle accumulator to 2412), evaluating $Z$ before $X$ would cause $Z$ to overstep by 4 steps before $X$'s limit halts the engine.
2. **Analytical Master Pulse Clamping**: Before evaluating either axis, `MotionEngine` inverts the electronic gearing for all active soft limits and stroke targets using `calculatePulsesForStep()`:
   $$\text{effectivePulses} = \min(\text{accumulatedPulses}, \text{boundPulse}_Z, \text{boundPulse}_X)$$
3. **Synchronous Position Landing**: Both axes compute their ideal steps using `effectivePulses`. $X$ lands on its exact limit step, $Z$ lands on its exact proportional taper coordinate, and `stopWithReason()` halts both axes synchronously with zero partner-axis overshoot.

---

## 6. Complete `MotionEngine` Interface Contract

```cpp
#pragma once
#include <cstdint>
#include "AxisState.h"
#include "SoftLimitTable.h"
#include "docs/motion/HAL_SPECIFICATION.md"

enum class MotionMode : uint8_t {
  STOPPED,
  VELOCITY_FEED,
  SPINDLE_SYNC_GEARING,
  MANUAL_JOG
};

struct FeedParameters {
  AxisId  axis;
  int8_t  direction;       // -1 (Chuck / Centerline), +1 (Tailstock / Retract)
  int32_t speedDuPerMin;   // Commanded velocity (for async power feed)
  int32_t pitchDupr;       // Commanded thread lead (for spindle sync gearing)
  int32_t targetDistDu;    // Optional stroke distance (-1 = continuous to soft limit)

  // Coordinated Dual-Axis / Taper Extension (for ZX Cones & NPT Taper Threads [KIN-04])
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0; // Radial delta in du (signed for taper direction)
  int32_t secondaryRatioDen   = 1; // Axial delta in du (must be > 0)
};

class MotionEngine {
public:
  MotionEngine(IStepperHAL* stepperHal, ISpindleEncoderHAL* encoderHal, IDROScaleHAL* droHal, IEndstopHAL* endstopHal);

  // Axis Configuration
  bool configureAxis(AxisId axis, const AxisGeometry& geometry, const StepTimingConfig& timing);
  
  // Coordinate & Zeroing APIs
  void zeroAxis(AxisId axis);
  void setAxisPositionDu(AxisId axis, int32_t posDu);
  int32_t getAxisPositionDu(AxisId axis) const;

  // Soft Limit Table Management
  SoftLimitTable& getSoftLimits(AxisId axis);
  const SoftLimitTable& getSoftLimits(AxisId axis) const;

  // Motion Execution APIs
  bool startVelocityFeed(const FeedParameters& params);
  bool startSpindleSync(const FeedParameters& params);
  bool startJog(AxisId axis, int8_t direction, int32_t stepDu);
  void stop(AxisId axis, bool emergency = false);
  void stopAll(bool emergency = false);

  // Status & Telemetry
  bool isMoving(AxisId axis) const;
  MotionMode getActiveMode(AxisId axis) const;
  int32_t getDistanceToNextLimitDu(AxisId axis) const;

  // Real-Time Service Hooks (Called from Core 1 ISR / Loop)
  void onMotionTick();
  void onSpindlePulse(int32_t deltaPulses, uint32_t currentRpm);

private:
  AxisState        m_axes[2];
  SoftLimitTable   m_limits[2];
  IStepperHAL*     m_stepperHal;
  ISpindleEncoderHAL* m_encoderHal;
  IDROScaleHAL*    m_droHal;
  IEndstopHAL*     m_endstopHal;
};
```
