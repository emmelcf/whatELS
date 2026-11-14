#pragma once

/**
 * @file IStepperHAL.h
 * @brief Abstract stepper/servo hardware abstraction layer.
 * @see docs/motion/HAL_SPECIFICATION.md
 */
#include <cstdint>
#include "StepTimingConfig.h"

#ifndef HAVE_AXIS_ID
#define HAVE_AXIS_ID
enum AxisId : uint8_t {
  AXIS_Z = 0,  // Longitudinal carriage leadscrew
  AXIS_X = 1   // Transverse cross-slide leadscrew
};
#endif

/**
 * @brief Abstract stepper/servo hardware abstraction layer.
 * @details Conforms to HAL_SPECIFICATION.md Section 2.2.
 */
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
