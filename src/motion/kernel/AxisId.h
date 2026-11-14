#pragma once

/**
 * @file AxisId.h
 * @brief Canonical axis identifier enum (AXIS_Z, AXIS_X).
 * @see docs/motion/MOTION_ENGINE_SPECIFICATION.md
 * @see docs/motion/HAL_SPECIFICATION.md
 */
#include <cstdint>

#ifndef HAVE_AXIS_ID
#define HAVE_AXIS_ID
enum AxisId : uint8_t {
  AXIS_Z = 0,  // Longitudinal carriage leadscrew
  AXIS_X = 1   // Transverse cross-slide leadscrew
};
#endif
