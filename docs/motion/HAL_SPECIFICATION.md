# whatELS Hardware Abstraction Layer (HAL) Specification

> **Status:** Authoritative Engineering Specification  
> **Target Version:** whatELS Modular Architecture  
> **Audience:** Firmware Engineers, Hardware Integrators, Motion Core  
> **Location:** `docs/motion/HAL_SPECIFICATION.md`

---

## 1. Executive Summary & Architectural Role

The **Hardware Abstraction Layer (HAL)** is the lowest software layer in the whatELS motion controller. It provides deterministic, typed, non-blocking hardware interfaces to physical pins, timer peripherals, and hardware pulse counters on the ESP32-S3 microcontroller.

### 1.1 Strict Layer Boundaries & Invariants
1. **Zero Domain Logic:** The HAL knows **nothing** about millimeters, inches, threads per inch, conical tapers, workpiece coordinates, or cutting cycles.
2. **Deterministic Time & Pin Signaling:** The HAL is the sole component permitted to write to motor `STEP`/`DIR`/`ENA` pins or read hardware quadrature counters.
3. **Non-Blocking Guarantee:** HAL functions executed from Core 0 or Core 1 tasks never execute blocking delay loops (`delay()` or busy `micros()` spins). Step pulses and timing windows are handled strictly by hardware peripherals (RMT, GPTimer) or short, bounded microsecond pin toggles.
4. **Hardware Driver Agnostic:** Whether driven by basic step/direction stepper drivers (TB6600, DM542), smart digital drives (Leadshine EM542S), or SPI/UART smart drivers (TMC2209), the interface contract remains identical.

```
┌─────────────────────────────────────────────────────────────┐
│             LAYER 2: KINEMATIC MOTION ENGINE                │
│    (Coordinates, Trajectory Planner, Gearing, Limits)       │
└──────────────────────────────┬──────────────────────────────┘
                               │ Step pulses, frequencies, directions
                               ▼
┌─────────────────────────────────────────────────────────────┐
│             LAYER 1: HARDWARE ABSTRACTION LAYER             │
│  ┌────────────────┐ ┌────────────────┐ ┌────────────────┐   │
│  │  IStepperHAL   │ │ISpindleEncoder │ │  IDROScaleHAL  │   │
│  │ (Step/Dir/Ena) │ │(PCNT Counter)  │ │(Linear Scales) │   │
│  └───────┬────────┘ └───────┬────────┘ └───────┬────────┘   │
└──────────┼──────────────────┼──────────────────┼────────────┘
           │                  │                  │
           ▼                  ▼                  ▼
┌─────────────────────────────────────────────────────────────┐
│                 ESP32-S3 PHYSICAL HARDWARE                  │
│   GPIO / RMT / GPTimer   PCNT Peripherals     Opto Inputs   │
└─────────────────────────────────────────────────────────────┘
```

---

## 2. Stepper Hardware Abstraction (`IStepperHAL`)

### 2.1 Electrical Signal & Timing Contract (`StepTimingConfig`)
Steppers and digital servo drives require strict adherence to minimum electrical timing windows to avoid missed steps, direction reversal glitches, and driver faults:

```cpp
struct StepTimingConfig {
  uint32_t dirSetupDelayNs  = 5000;  // t_setup: Min time DIR pin must be stable before STEP rising edge (ns)
  uint32_t dirHoldTimeNs    = 5000;  // t_hold:  Min time DIR pin must be held after STEP active edge (ns)
  uint32_t stepPulseWidthNs = 2500;  // t_high:  Active STEP pulse duration (ns; typically 2.5 - 5.0 µs)
  uint16_t enableDelayMs    = 50;    // t_ena:   Driver settling time after ENA active before first step pulse
  bool     invertStep       = false; // True = Active LOW pulse; False = Active HIGH pulse
  bool     invertDir        = false; // True = Invert physical direction pin logic
  bool     invertEnable     = false; // True = Active HIGH enable; False = Active LOW enable
};
```

> **Non-Blocking Enable Arming Invariant:** `IStepperHAL::setEnabled(axis, true)` must never block the calling task for $50\text{ ms}$. Instead, the HAL latches `m_enableTimestampUs = esp_timer_get_time()`, and step pulse generation is gated until `enableDelayMs` has elapsed. Any pulse requests before the timeout are safely deferred.

### 2.2 The `IStepperHAL` Interface

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

  /**
   * @brief Configures physical GPIO pins and timing constraints for an axis.
   * @param axis Target axis (AXIS_Z or AXIS_X).
   * @param stepPin GPIO pin connected to driver STEP/PUL.
   * @param dirPin GPIO pin connected to driver DIR.
   * @param enablePin GPIO pin connected to driver ENA (-1 if hardwired).
   * @param timing Electrical timing parameters for the drive.
   * @return True if GPIO configuration succeeded.
   */
  virtual bool configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) = 0;

  /**
   * @brief Sets the physical motion direction for an axis.
   * @details Enforces dirSetupDelayUs if direction changes while stationary.
   * @param axis Target axis.
   * @param dir Cartesian polarity: -1 (Chuck / Centerline) or +1 (Tailstock / Retract).
   */
  virtual void setDirection(AxisId axis, int8_t dir) = 0;

  /**
   * @brief Returns the currently latched direction pin polarity.
   */
  virtual int8_t getDirection(AxisId axis) const = 0;

  /**
   * @brief Enables or disables power to the stepper motor windings.
   * @param axis Target axis.
   * @param enable True to energize motor; false to allow free manual movement.
   */
  virtual void setEnabled(AxisId axis, bool enable) = 0;

  /**
   * @brief Returns whether the axis stepper is energized.
   */
  virtual bool isEnabled(AxisId axis) const = 0;

  /**
   * @brief Fires a single non-blocking step pulse on the specified axis.
   * @details Used by synchronous spindle gearing (Bresenham step dispatch) and discrete jogs.
   *          Enforces stepPulseWidthUs.
   */
  virtual void pulseStep(AxisId axis) = 0;

  /**
   * @brief Starts an asynchronous hardware-timed pulse train for continuous or profiled feed.
   * @param axis Target axis.
   * @param frequencyHz Step pulse rate in steps per second (f = steps/sec).
   * @param direction Cartesian polarity (-1 or +1).
   */
  virtual void startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) = 0;

  /**
   * @brief Dynamically updates the step pulse rate without stopping or interrupting motion.
   * @details Essential for modern CNC trapezoidal and S-curve acceleration/deceleration profiling.
   * @param axis Target axis.
   * @param frequencyHz New target frequency in steps per second.
   */
  virtual void updatePulseFrequency(AxisId axis, uint32_t frequencyHz) = 0;

  /**
   * @brief Halts the asynchronous pulse train immediately.
   */
  virtual void stopPulseTrain(AxisId axis) = 0;

  /**
   * @brief Returns true if an asynchronous pulse train is actively generating steps.
   */
  virtual bool isPulseTrainActive(AxisId axis) const = 0;

  /**
   * @brief Returns true if hardware driver fault pin is asserted (if wired).
   */
  virtual bool hasDriverFault(AxisId axis) const = 0;
};
```

---

## 3. Spindle Encoder Abstraction (`ISpindleEncoderHAL`)

The spindle encoder is the primary timing and synchronization source for the entire electronic leadscrew.

### 3.1 Pulse Counter Hardware Contract (ESP32-S3 PCNT)
- Uses ESP32-S3 dedicated **Pulse Counter (PCNT)** hardware module to sample Phase A and Phase B quadrature signals without CPU interrupt overhead.
- Supports $4\times$ quadrature decoding ($2\times$ or $4\times$ edge evaluation).
- Optional Index pulse (Phase Z) hardware reset for single-point thread threading synchronization.
- **Zero-Loss 16-Bit Native Modular Wrapping (`[HAL-01]`):**
  - **Never clear the hardware PCNT counter** (`pcnt_unit_clear_count()`) during operation. Clearing the register introduces a $\approx 0.5\,\mu\text{s}$ blind window that permanently wipes out high-frequency ($204.8\text{ kHz}$) incoming encoder edges.
  - The PCNT unit runs as a free-running 16-bit signed counter (`[-32768, +32767]`).
  - Sampling computes the incremental delta using 16-bit two's complement modular subtraction:
    ```cpp
    int16_t current16 = static_cast<int16_t>(rawCount);
    int16_t delta16   = static_cast<int16_t>(current16 - m_lastCount16);
    m_lastCount16     = current16;
    m_accumulatedPulses += delta16;
    ```
- **Hardware Glitch Filter:** Configured to $\approx 500\text{ ns}$ (40 APB clock cycles at 80 MHz) to reject industrial VFD PWM electrical interference without filtering valid $4.88\,\mu\text{s}$ edges.

### 3.2 The `ISpindleEncoderHAL` Interface

```cpp
#pragma once
#include <cstdint>

class ISpindleEncoderHAL {
public:
  virtual ~ISpindleEncoderHAL() = default;

  /**
   * @brief Initializes the hardware PCNT unit for spindle encoder tracking.
   * @param pinA GPIO for Phase A.
   * @param pinB GPIO for Phase B.
   * @param pinIndex GPIO for Phase Z / Index pulse (-1 if not wired).
   * @param nativePpr Native pulses per revolution (e.g. 1024, 1200, 2048).
   * @param pulleyRatio Spindle-to-encoder pulley mechanical ratio (typically 1.0).
   * @param encoderTeeth Rational gear teeth on encoder pulley (0 = use pulleyRatio float).
   * @param spindleTeeth Rational gear teeth on spindle pulley (0 = use pulleyRatio float).
   */
  virtual bool init(
      int pinA,
      int pinB,
      int pinIndex,
      uint16_t nativePpr,
      float pulleyRatio,
      uint16_t encoderTeeth = 0,
      uint16_t spindleTeeth = 0) = 0;

  /**
   * @brief Samples the delta pulses elapsed since the last read.
   * @details Called at high frequency (1 kHz - 10 kHz) in the motion control loop.
   *          Uses native 16-bit signed modular subtraction without resetting the PCNT hardware.
   * @return Signed pulse delta (positive for forward/CCW, negative for reverse/CW).
   */
  virtual int32_t getPulseDelta() = 0;

  /**
   * @brief Returns total continuous signed pulses accumulated since machine boot/origin.
   */
  virtual int64_t getAccumulatedPulses() const = 0;

  /**
   * @brief Returns filtered real-time spindle rotational speed in RPM.
   * @details Computed via hardware timer delta with low-pass jitter filtering.
   */
  virtual uint32_t getFilteredRpm() const = 0;

  /**
   * @brief Returns exact spindle angular position within the current turn [0.0°, 360.0°).
   */
  virtual float getAngleDegrees() const = 0;

  /**
   * @brief Returns true if the physical 1/rev index pulse has been observed and synchronized.
   */
  virtual bool hasIndexSynced() const = 0;

  /**
   * @brief Resets index synchronization latch, forcing re-sync on next passing marker.
   */
  virtual void resetIndexSync() = 0;
};
```

---

## 4. Optical DRO Linear Scales Abstraction (`IDROScaleHAL`)

For lathes equipped with optical linear glass scales or magnetic encoders on the carriage and cross-slide:

```cpp
#pragma once
#include <cstdint>
#include "IStepperHAL.h"

class IDROScaleHAL {
public:
  virtual ~IDROScaleHAL() = default;

  virtual bool init(AxisId axis, int pinA, int pinB, uint32_t resolutionNm) = 0;
  
  /**
   * @brief Returns real physical linear carriage position in deci-microns (du).
   */
  virtual int32_t getPositionDu(AxisId axis) const = 0;

  /**
   * @brief Zeros the scale hardware accumulator at current position.
   */
  virtual void zero(AxisId axis) = 0;

  /**
   * @brief Sets absolute position preset on the physical scale reader.
   */
  virtual void setPositionDu(AxisId axis, int32_t posDu) = 0;
};
```

---

## 5. Hardware Endstops & Emergency Stop Abstraction (`IEndstopHAL`)

For physical machine protection switches:

```cpp
#pragma once
#include <cstdint>
#include "IStepperHAL.h"

enum EndstopId : uint8_t {
  ENDSTOP_Z_MIN = 0, // Physical limit switch near chuck
  ENDSTOP_Z_MAX = 1, // Physical limit switch near tailstock
  ENDSTOP_X_MIN = 2, // Physical limit switch at spindle centerline
  ENDSTOP_X_MAX = 3, // Physical limit switch at full retract
  ENDSTOP_ESTOP = 4  // Physical E-stop mushroom button line
};

class IEndstopHAL {
public:
  virtual ~IEndstopHAL() = default;

  virtual bool init() = 0;

  /**
   * @brief Returns true if a physical endstop switch is currently asserted.
   */
  virtual bool isAsserted(EndstopId id) const = 0;

  /**
   * @brief Returns bitmask of all currently asserted hardware endstops.
   */
  virtual uint8_t getAssertedMask() const = 0;

  /**
   * @brief Returns true if the hardwired Emergency Stop line is broken/tripped.
   */
  virtual bool isEmergencyStopAsserted() const = 0;
};
```
