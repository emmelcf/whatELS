#pragma once

/**
 * @file ISpindleEncoderHAL.h
 * @brief Abstract spindle encoder hardware abstraction layer with simulation hooks.
 * @see docs/motion/HAL_SPECIFICATION.md
 * @see docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md
 */
#include <cstdint>

/**
 * @brief Abstract spindle encoder hardware abstraction layer.
 * @details Conforms to HAL_SPECIFICATION.md Section 3.2.
 */
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
   * @brief Updates encoder geometry parameters on the fly without hardware re-init.
   */
  virtual void updateProfile(
      uint16_t nativePpr,
      float pulleyRatio,
      uint16_t encoderTeeth = 0,
      uint16_t spindleTeeth = 0) {}

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
   * @brief Returns total 4x quadrature counts per physical spindle revolution.
   */
  virtual int32_t getCountsPerSpindleRev() const { return 0; }

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

  // --- Simulation & Test Injection Hooks ---

  /**
   * @brief Toggles virtual injection mode.
   * @param active When true, getPulseDelta() serves injected virtual pulses instead of PCNT pins.
   */
  virtual void setSimulationActive(bool active) = 0;

  /**
   * @brief Checks if virtual injection mode is currently active.
   */
  virtual bool isSimulationActive() const = 0;

  /**
   * @brief Injects an exact discrete pulse delta.
   * @param delta Signed pulse count.
   */
  virtual void injectSimulatedPulses(int32_t delta) = 0;

  /**
   * @brief Sets a constant simulated spindle RPM.
   * @param rpm Signed rotational speed in RPM (+ forward / - reverse). 0.0 = stopped.
   */
  virtual void setSimulatedRpm(float rpm) = 0;
};
