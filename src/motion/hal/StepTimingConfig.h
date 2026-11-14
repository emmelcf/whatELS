#pragma once

/**
 * @file StepTimingConfig.h
 * @brief Electrical signal timing and logic polarity constraints for stepper/servo drives.
 * @see docs/motion/HAL_SPECIFICATION.md
 */
#include <cstdint>

/**
 * @brief Electrical signal timing and logic polarity constraints for stepper/servo drives.
 * @details Conforms to HAL_SPECIFICATION.md Section 2.1.
 */
struct StepTimingConfig {
  uint32_t dirSetupDelayNs  = 5000;  // t_setup: Min time DIR pin must be stable before STEP rising edge (ns)
  uint32_t dirHoldTimeNs    = 5000;  // t_hold:  Min time DIR pin must be held after STEP active edge (ns)
  uint32_t stepPulseWidthNs = 2500;  // t_high:  Active STEP pulse duration (ns; typically 2.5 - 5.0 µs)
  uint16_t enableDelayMs    = 50;    // t_ena:   Driver settling time after ENA active before first step pulse
  bool     invertStep       = false; // True = Active LOW pulse; False = Active HIGH pulse
  bool     invertDir        = false; // True = Invert physical direction pin logic
  bool     invertEnable     = false; // True = Active HIGH enable; False = Active LOW enable
};
