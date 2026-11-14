#pragma once

/**
 * @file MotionCommandPacket.h
 * @brief Lock-free command queue packet consumed by Core 1 MotionEngine.
 * @see docs/motion/CONCURRENCY_AND_TIMING.md
 * @see docs/adr/0002-command-surface-rationalization-and-dumb-client-decoupling.md
 */
#include <cstdint>
#include "hal/IStepperHAL.h"

/**
 * @brief Lock-free command queue packet consumed by Core 1 MotionEngine.
 * @details Conforms to CONCURRENCY_AND_TIMING.md Section 3.2.
 */
struct MotionCommandPacket {
  enum class Type : uint8_t {
    START_SPINDLE_SYNC,
    START_VELOCITY_FEED,
    EXECUTE_STROKE,
    EXECUTE_JOG,
    STOP_NORMAL,
    STOP_EMERGENCY,
    PAUSE_FEED_HOLD,
    RESUME_FEED_HOLD,
    UPDATE_FEED_PITCH,
    UPDATE_VELOCITY,
    UPDATE_SOFT_LIMITS,
    ZERO_AXIS
  } type;

  AxisId  axis                = AXIS_Z;
  int8_t  direction           = 0;     // -1 (Chuck/Centerline), +1 (Tailstock/Retract)
  int32_t pitchDupr           = 0;     // Pitch in canonical deci-microns per spindle rev
  int32_t speedDuMin          = 0;     // Velocity in deci-microns per minute
  int32_t targetCoordDu       = 0;     // Target coordinate in deci-microns
  bool    respectSoftLimits   = true;  // Enforce soft limits drum

  // Secondary axis extension for compound tapers / cones [KIN-04]
  bool    enableSecondaryAxis = false;
  AxisId  secondaryAxis       = AXIS_X;
  int32_t secondaryRatioNum   = 0;
  int32_t secondaryRatioDen   = 1;
};
