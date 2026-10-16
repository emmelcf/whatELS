# NanoELS Motion Engine to Mode Integration Contract

> **Status:** Authoritative Engineering Specification  
> **Target Version:** NanoELS H5 Modular Architecture  
> **Audience:** Firmware Engineers, Motion Control Specialists, Mode Authors  
> **Location:** `docs/motion/MODE_INTEGRATION_CONTRACT.md`  
> **Predecessor Docs:** [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md), [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md)

---

## 1. Executive Summary & Design Rationale

This specification defines the boundary between **Layer 2 (Kinematic Motion Engine)** and **Layer 3 (Mode Orchestration Layer)**.

### 1.1 Inversion of Responsibility
In legacy firmware and early draft architectures, modes were burdened with low-level motion responsibilities:
- Modes received high-frequency spindle encoder ISR pulses (up to 10 kHz) and directly ran step math.
- Modes implemented their own ad-hoc limit switch evaluations with binary "high/low" sentinels.
- Modes implemented ad-hoc `delay()` or busy-wait loops to handle rapid returns and tool clearance.

**First-Principles Motion Principle:**
The `MotionEngine` is the sole authority for physical kinematics, hardware pulse generation, acceleration/deceleration profiling, and soft limit enforcement.  
An `IELSMode` (whether `UnifiedGearboxMode`, `TurnCycle`, `FaceCycle`, or `ThreadCycle`) is strictly an **orchestrator of machining intent**. It commands the `MotionEngine` using clean, high-level primitives and responds to motion lifecycle events.

```
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 3: MODE ORCHESTRATION LAYER                        │
│                                                                        │
│   UnifiedGearboxMode     TurnCycle      ThreadCycle      FaceCycle    │
│   (State, Passes, Infeed, Retract Sequences, User Parameter Checks)    │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                         Commands   │   Events / Telemetry
                      (Start, Stop, │ (Limit Reached, Stroke Done,
                       Sync, Rapid) │  Sync Acquired, Position)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 2: KINEMATIC MOTION ENGINE                         │
│                                                                        │
│   ┌────────────────────┐  ┌────────────────────┐  ┌─────────────────┐  │
│   │   SoftLimitTable   │  │ Trajectory Engine  │  │   Bresenham     │  │
│   │ (Multi-Stop Drum)  │  │(Accel/Decel Window)│  │  Synchronizer   │  │
│   └────────────────────┘  └────────────────────┘  └─────────────────┘  │
│   • Enforces stopping distance: d_stop = v^2 / 2a                     │
│   • Glides into stops with zero mechanical shock                       │
│   • Maintains continuous feed when no limit is active                  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 1: HARDWARE ABSTRACTION (HAL)                      │
│                  IStepperHAL, ISpindleEncoderHAL                       │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Motion Engine Command Interface

Modes communicate with the `MotionEngine` strictly through typed command structures and methods. No global variables, no pin manipulation, and no sentinels.

### 2.1 Motion Primitives

A mode commands one of four fundamental physical primitives:

| Primitive | Engine Method | Description | Primary Use Case |
|---|---|---|---|
| **Spindle-Synchronous Feed** | `engageSpindleSync()` | Carriage motion locked to spindle encoder pulses via 64-bit Bresenham accumulator | Threading, Single-point turning/facing |
| **Continuous Velocity Feed** | `engageVelocityFeed()` | Asynchronous timed pulse train with controlled acceleration/deceleration | Electronic Power Feed, Facing cuts |
| **Controlled Stroke Move** | `executeStroke()` | Absolute or relative move of fixed length with trapezoidal profiling | Retract moves, multi-pass stepover, clearance |
| **Manual Jog** | `executeJog()` | Incremental jog step or MPG handwheel tracking | Touch-off, workpiece setup |

---

### 2.2 Data Structures & Parameter Types

```cpp
#pragma once
#include <cstdint>
#include "HAL_SPECIFICATION.md"
#include "MOTION_ENGINE_SPECIFICATION.md"

// Reason why a motion move came to a halt
enum class MotionHaltReason : uint8_t {
  NORMAL_STOP = 0,          // Operator commanded stop, completed deceleration ramp
  SOFT_LIMIT_REACHED,       // Decelerated smoothly and landed exactly on an active soft limit
  TARGET_DISTANCE_REACHED,  // Stroke move reached commanded coordinate
  FEED_HOLD,                // Paused by operator; position latched, sync preserved
  HARD_ENDSTOP_TRIPPED,     // Physical machine switch hit (Emergency hard stop)
  DRIVER_FAULT,             // Stepper driver alarm line asserted
  ESTOP                     // Emergency stop button hit or supervisory abort
};

// Motion command parameters for Spindle-Synchronous Gearing
struct SpindleSyncParams {
  AxisId  axis;             // AXIS_Z or AXIS_X
  int8_t  direction;        // -1 (Toward Chuck / Centerline), +1 (Toward Tail / Retract)
  int32_t pitchDupr;        // Pitch / Lead in deci-microns per revolution (e.g. 20000 = 2.0 mm/rev)
  int32_t maxRapidSpeedDuMin;// Maximum rapid velocity clamp during spindle sync
  bool    respectSoftLimits;// True = halt automatically at nearest enforced limit
  bool    syncIndex;        // True = wait for 1/rev index marker before initiating step pulses (Threading)
  int32_t targetDistDu;     // Optional stroke limit (0 = continuous until stopped or limit reached)
};

// Motion command parameters for Continuous / Profiled Velocity Feed
struct VelocityFeedParams {
  AxisId  axis;             // AXIS_Z or AXIS_X
  int8_t  direction;        // -1 (Toward Chuck / Centerline), +1 (Toward Tail / Retract)
  int32_t feedRateDuPerMin; // Velocity in deci-microns per minute (e.g. 1000000 = 100 mm/min)
  float   accelMmSec2;      // Acceleration ramp rate (0.0 = use axis default)
  bool    respectSoftLimits;// True = halt automatically at nearest enforced limit
  int32_t targetDistDu;     // Optional stroke limit (0 = continuous until stopped or limit reached)
};

// Motion command parameters for Rapid Positioning & Multi-Pass Clearance
struct StrokeParams {
  AxisId  axis;
  int32_t targetCoordDu;    // Absolute workpiece coordinate target in deci-microns
  int32_t rapidSpeedDuMin;  // Rapid traverse rate (e.g. 2500000 = 250 mm/min)
  float   accelMmSec2;      // Acceleration ramp rate
  bool    respectSoftLimits;// True = enforce limits during stroke
};
```

---

## 3. The Motion Event Listener Contract (`IMotionEventListener`)

When a motion completes, encounters a soft limit, or changes state, `MotionEngine` dispatches events asynchronously to registered listeners (the active mode and the telemetry dispatcher).

```cpp
class IMotionEventListener {
public:
  virtual ~IMotionEventListener() = default;

  /**
   * @brief Fired when motion on an axis halts for any reason.
   * @param axis Axis that halted.
   * @param reason The specific reason motion ceased (limit, target, stop, estop).
   * @param haltedPosSteps Physical leadscrew position where motor stopped.
   * @param haltedCoordDu Active workpiece coordinate in deci-microns.
   * @param triggeredLimit Pointer to the limit that caused the stop (or nullptr).
   */
  virtual void onMotionHalted(
      AxisId axis,
      MotionHaltReason reason,
      int32_t haltedPosSteps,
      int32_t haltedCoordDu,
      const SoftLimit* triggeredLimit) = 0;

  /**
   * @brief Fired in threading mode when the spindle 1/rev index marker is acquired
   *        and carriage motion actively engages.
   */
  virtual void onSpindleSyncAcquired(AxisId axis) = 0;

  /**
   * @brief Fired periodically (e.g. 50 Hz - 100 Hz) to deliver filtered telemetry.
   * @param zCoordDu Current Z workpiece coordinate.
   * @param xCoordDu Current X workpiece coordinate.
   * @param currentRpm Filtered spindle rotational speed.
   * @param isMoving True if either axis has active step generation.
   */
  virtual void onMotionTelemetry(
      int32_t zCoordDu,
      int32_t xCoordDu,
      uint32_t currentRpm,
      bool isMoving) = 0;
};
```

---

## 4. Refined Mode Architecture Interface (`IELSMode`)

To decouple `IELSMode` from legacy hardware-level ISRs, the updated `IELSMode` interface interacts with motion exclusively via high-level engine services.

```cpp
#pragma once
#include <cstdint>
#include "docs/motion/HAL_SPECIFICATION.md"
#include "docs/motion/MOTION_ENGINE_SPECIFICATION.md"
#include "docs/motion/MODE_INTEGRATION_CONTRACT.md"

class MotionEngine; // Forward declaration

class IELSMode : public IMotionEventListener {
public:
  virtual ~IELSMode() = default;

  // --- 1. Identity & Manifest ---
  virtual ModeId getId() const = 0;
  virtual const char* getSlug() const = 0;
  virtual const char* getName() const = 0;
  virtual const ModeManifest& getManifest() const = 0;

  // --- 2. Lifecycle & Engine Binding ---
  /**
   * @brief Called when mode is activated. Injects authoritative MotionEngine pointer.
   */
  virtual void onEnter(MotionEngine* engine) = 0;

  /**
   * @brief Called before switching away to another mode. Must stop all motion safely.
   */
  virtual void onExit() = 0;

  // --- 3. Operator Execution Controls ---
  virtual bool start() = 0;
  virtual void stop(StopType stopType) = 0;
  virtual bool pause() = 0;
  virtual bool resume() = 0;

  virtual ModeState getState() const = 0;
  virtual bool isRunning() const = 0;

  // --- 4. Live Runtime Permission Gatekeeper ---
  virtual uint32_t getLivePermissions() const = 0;
  virtual bool canExecuteCommand(CommandId cmd, ModeState state) const = 0;

  // --- 5. Mode Parameters ---
  virtual bool setParam(const char* key, float value) = 0;
  virtual bool setParam(const char* key, const char* strValue) = 0;
  virtual bool getParam(const char* key, float& outVal) const = 0;
  virtual bool validateParams(char* errBuf, size_t errBufLen) const = 0;

  // --- 6. Telemetry & Progress Serialization ---
  virtual void writeTelemetry(TelemetryWriter& writer) const = 0;
};
```

---

## 5. Typical Execution Flows

### 5.1 Scenario A: Continuous Electronic Gearbox Feed to Shoulder Stop
This scenario describes how `UnifiedGearboxMode` interacts with `MotionEngine` when the operator engages feed toward a chuck shoulder stop:

```
Operator / UI               UnifiedGearboxMode               MotionEngine              IStepperHAL
     │                              │                             │                         │
     │── [FEED_LEFT] ──────────────>│                             │                         │
     │                              │── engageSpindleSync() ─────>│                         │
     │                              │   (Z, Dir=-1, Pitch=0.2mm,  │                         │
     │                              │    respectSoftLimits=true)  │                         │
     │                              │                             │── findNextEnforcedLimit()
     │                              │                             │   (Found Slot 1 at -25mm)
     │                              │                             │                         │
     │                              │                             │── Bresenham Gearing ───>│ pulseStep(Z)
     │                              │                             │── Bresenham Gearing ───>│ pulseStep(Z)
     │                              │                             │                         │
     │                              │                             │ [At D_remain <= d_stop] │
     │                              │                             │── Deceleration Ramp ───>│ updatePulseFreq
     │                              │                             │                         │
     │                              │                             │ [Landed on Step -5000]  │
     │                              │                             │── stopPulseTrain() ────>│ stop()
     │                              │<── onMotionHalted() ────────│                         │
     │                              │    (SOFT_LIMIT_REACHED,     │                         │
     │                              │     Slot 1, -25.000 mm)     │                         │
     │<── Telemetry (STOPPED, ──────│                             │                         │
     │     at Limit Slot 1)         │                             │                         │
```

1. **Engagement:** Operator presses feed left in Web UI.
2. **Intent Command:** `UnifiedGearboxMode` calls `engine->engageSpindleSync(AXIS_Z, -1, pitch, true)`.
3. **Limit Lookahead:** `MotionEngine` queries `SoftLimitTable::findNextEnforcedLimit()`. It locates the chuck limit at $-25.000\text{ mm}$ (step $-5000$).
4. **Motion:** Motion runs synchronously with spindle rotation.
5. **Deceleration Window:** As the carriage approaches $-25.000\text{ mm}$, the deceleration window triggers. Stepper frequency ramps down smoothly to prevent missed steps or overshoot.
6. **Stop & Notification:** Motion lands exactly on step $-5000$. `MotionEngine` dispatches `onMotionHalted(AXIS_Z, SOFT_LIMIT_REACHED, -5000, -250000, &limit)`.
7. **Mode State:** `UnifiedGearboxMode` transitions to `PAUSED` or `IDLE` and updates UI telemetry.

---

### 5.2 Scenario B: Multi-Pass Turning Cycle Orchestration
This demonstrates how higher-level canned cycles compose simple motion primitives:

1. **Cycle Start:** Operator configures:
   - Initial diameter: $30\text{ mm}$, Target diameter: $26\text{ mm}$
   - Cut depth per pass: $1\text{ mm}$ on diameter ($0.5\text{ mm}$ on radius)
   - Cut length: $40\text{ mm}$ (from $Z = 0$ to $Z = -40\text{ mm}$)
2. **Pass 1 Infeed:**
   - Mode calls `engine->executeStroke(AXIS_X, targetRadius, infeedRate)`.
   - Engine executes stroke and fires `onMotionHalted(AXIS_X, TARGET_DISTANCE_REACHED)`.
3. **Pass 1 Cut:**
   - Mode calls `engine->engageSpindleSync(AXIS_Z, -1, feedPitch, true, targetZ = -400000)`.
   - Engine cuts to $-40\text{ mm}$ and fires `onMotionHalted(AXIS_Z, TARGET_DISTANCE_REACHED)`.
4. **Pass 1 Retract:**
   - Mode calls `engine->executeStroke(AXIS_X, clearanceRadius, rapidRate)`.
   - Tool pulls away from workpiece.
5. **Pass 1 Return:**
   - Mode calls `engine->executeStroke(AXIS_Z, 0, rapidRate)`.
   - Carriage rapids back to start.
6. **Pass 2 Execution:**
   - Mode increments pass counter, recalculates infeed depth, and repeats steps 2–5 until target diameter is reached.

**Key Architectural Takeaway:** The canned cycle contains **zero** motor pulse math, **zero** hardware timer code, and **zero** encoder quadrature counters. It is a pure, clean state machine commanding the `MotionEngine`.

---

## 6. Live Parameter Trim & In-Motion Safety Protocol

Modern CNC operation requires trimming feed rates or adjusting chip loads while the tool is under active cut.

### 6.1 Safe vs. Prohibited In-Motion Adjustments

| Command | Permitted In-Motion? | Enforcement Action |
|---|---|---|
| **Feed Trim ([−] / [+])** | **YES** (if mode declares `PERM_LIVE_FEED_TRIM`) | `MotionEngine::updatePitch()` or `updateFeedRate()` updates Bresenham ratio or timer frequency dynamically without stopping or losing step count |
| **Feed Hold / Pause** | **YES** (if mode declares `PERM_FEED_HOLD`) | `MotionEngine::pause()` decelerates smoothly to stop; spindle angle & thread registration preserved |
| **Emergency Stop** | **YES** (Universally permitted at all times) | `MotionEngine::stopAll(emergency = true)` immediately kills step trains and asserts motor disable |
| **Axis Swap (Z $\leftrightarrow$ X)** | **NO** (Strictly locked) | Command rejected with `ERR_MOTION_ACTIVE` |
| **Direction Flip** | **NO** (Strictly locked) | Must come to full stop before direction pin toggle to prevent mechanical shock |
| **Change Cut Dimensions** | **NO** (Strictly locked) | Major coordinate rewrites rejected until cycle is completed or paused |

### 6.2 Atomic Live Trim Execution
When an operator nudges feed rate via Web UI:
1. Web UI sends `{"cmd":"feed_nudge", "delta_percent": 10}`.
2. Mode verifies `getLivePermissions() & PERM_LIVE_FEED_TRIM`.
3. Mode calculates new pitch/feed and calls `engine->updateFeedParameters(...)`.
4. `MotionEngine` recalculates the fractional ratio and applies it to the active accumulator on the very next spindle pulse or timer tick without discontinuity.
