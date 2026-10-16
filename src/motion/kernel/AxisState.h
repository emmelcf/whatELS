#pragma once
#include <cstdint>
#if defined(ESP32) || defined(ARDUINO)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#include "AxisGeometry.h"
#include "hal/IStepperHAL.h"

/**
 * @brief Pure kinematic state and position tracking for a machine axis.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 2.3.
 *          Positions are tracked in pure motor steps and canonical deci-microns (du).
 */
struct AxisState {
  AxisId       id              = AXIS_Z;
  AxisGeometry geometry;

  // Pure Kinematic Positions
  int32_t posSteps             = 0;     // Physical carriage position in raw motor steps
  int32_t originSteps          = 0;     // Coordinate offset between physical bed zero and operator zero
  int32_t motorPosSteps        = 0;     // Motor rotor position before backlash compensation
  int32_t backlashAccum        = 0;     // Active backlash hysteresis steps [0, backlashSteps]

  // Power & Motion State
  bool    movingManually       = false; // True during handwheel / MPG jogging
  bool    disabled             = false; // True if motor driver is de-energized
};

/**
 * @brief Converts raw motor steps to canonical deci-microns (du) with symmetric integer rounding [KIN-05].
 * @details 1 du = 0.0001 mm = 0.1 um.
 */
inline __attribute__((always_inline)) int32_t stepsToDu(const AxisGeometry& geom, int32_t steps) {
  if (geom.motorStepsPerRev == 0) return 0;
  int64_t num = static_cast<int64_t>(steps) * geom.screwPitchDu;
  int64_t den = geom.motorStepsPerRev;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}

/**
 * @brief Converts canonical deci-microns (du) to raw motor steps with symmetric integer rounding [KIN-05].
 */
inline __attribute__((always_inline)) int32_t duToSteps(const AxisGeometry& geom, int32_t du) {
  if (geom.screwPitchDu == 0) return 0;
  int64_t num = static_cast<int64_t>(du) * geom.motorStepsPerRev;
  int64_t den = geom.screwPitchDu;
  return static_cast<int32_t>((num >= 0 ? (num + den / 2) : (num - den / 2)) / den);
}

/**
 * @brief Calculates the workpiece coordinate in canonical deci-microns (du) taking origin offset into account.
 */
inline __attribute__((always_inline)) int32_t getWorkpieceCoordDu(const AxisState& axis) {
  return stepsToDu(axis.geometry, axis.posSteps + axis.originSteps);
}
