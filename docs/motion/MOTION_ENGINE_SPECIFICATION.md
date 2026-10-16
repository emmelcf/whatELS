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
inline int32_t stepsToDu(const AxisGeometry& geom, int32_t steps) {
  return static_cast<int32_t>((static_cast<int64_t>(steps) * geom.screwPitchDu) / geom.motorStepsPerRev);
}

inline int32_t duToSteps(const AxisGeometry& geom, int32_t du) {
  return static_cast<int32_t>((static_cast<int64_t>(du) * geom.motorStepsPerRev) / geom.screwPitchDu);
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

### 3.2 The `SoftLimitTable` Class

```cpp
class SoftLimitTable {
public:
  static constexpr size_t MAX_LIMITS = 8;
  static constexpr int32_t ADVANCE_DEADBAND_STEPS = 2; // Steps past stop to advance to next

  /**
   * @brief Queries the immediate next enforced soft limit strictly ahead in the motion path.
   * @param currentSteps Current axis position in raw motor steps.
   * @param direction Cartesian motion polarity (-1 toward Chuck/Center, +1 toward Tail/Retract).
   * @return Pointer to next active limit, or nullptr if path is completely unbounded.
   */
  const SoftLimit* findNextEnforcedLimit(int32_t currentSteps, int8_t direction) const {
    const SoftLimit* candidate = nullptr;

    if (direction < 0) {
      // Negative travel: find closest enforced limit strictly below current position
      int32_t highestPosBelow = INT32_MIN;
      for (size_t i = 0; i < MAX_LIMITS; ++i) {
        if (m_limits[i].isEnforced() && (m_limits[i].stepPos < (currentSteps - ADVANCE_DEADBAND_STEPS))) {
          if (m_limits[i].stepPos > highestPosBelow) {
            highestPosBelow = m_limits[i].stepPos;
            candidate = &m_limits[i];
          }
        }
      }
    } else if (direction > 0) {
      // Positive travel: find closest enforced limit strictly above current position
      int32_t lowestPosAbove = INT32_MAX;
      for (size_t i = 0; i < MAX_LIMITS; ++i) {
        if (m_limits[i].isEnforced() && (m_limits[i].stepPos > (currentSteps + ADVANCE_DEADBAND_STEPS))) {
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

### 4.2 Real-Time Deceleration Window Calculation
At every step position $P_i$ during motion at velocity $v_i$:

1. **Calculate stopping distance:**
   $$d_{\text{stop}} = \frac{v_i^2 - v_{\text{end}}^2}{2a}$$
2. **Query path clearance to the next soft limit:**
   $$D_{\text{remaining}} = \text{abs}(P_{\text{limit}} - P_i)$$
3. **Deceleration Condition:**
   $$\text{If } D_{\text{remaining}} \le d_{\text{stop}}, \quad \text{transition from CRUISE to DECELERATION ramp!}$$

This guarantees that the carriage glides smoothly into the shoulder stop, landing on the exact step coordinate at zero velocity without mechanical shock.

---

## 5. Spindle Synchronous Gearing Engine (`SpindleSynchronizer`)

In synchronized electronic leadscrew mode, carriage motion is geometrically locked to the rotating spindle.

### 5.1 Fractional Bresenham Gearing Accumulator
To prevent integer truncation drift over thousands of spindle revolutions, the gearing engine uses a 64-bit fractional accumulator:

$$\text{Target Position (Steps)} = \text{round}\left( \frac{\text{AccumulatedSpindlePulses} \times \text{MotorSteps} \times \text{PitchDupr}}{\text{ScrewPitchDu} \times \text{EncoderPPR}} \right)$$

### 5.2 Spindle Slip & Thread Phase Registration
When the carriage reaches a soft limit shoulder during high-speed threading or turning:
1. Stepping halts cleanly at the shoulder limit (`p = limit.stepPos`).
2. Spindle pulses continue accumulating in the spindle encoder HAL.
3. The synchronizer tracks **spindle modulo turns** relative to the resting position.
4. When the operator reverses the spindle or disengages, the carriage retraces the exact thread helical path without jumping teeth or stripping threads.

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
