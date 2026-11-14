#pragma once

/**
 * @file SoftLimit.h
 * @brief Operator-clamped virtual soft limit and shoulder stop model.
 * @see docs/motion/MOTION_ENGINE_SPECIFICATION.md
 */
#include <cstdint>

enum class LimitBound : uint8_t {
  AUTO  = 0,
  LOWER = 1, // Stops motion in negative direction (-1 / Chuck / Centerline)
  UPPER = 2, // Stops motion in positive direction (+1 / Tailstock / Retract)
  BOTH  = 3  // Stops motion in both directions
};

/**
 * @brief Represents an operator-clamped virtual soft limit or shoulder stop.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 3.1.
 *          Models limit state explicitly without magic sentinels.
 */
struct SoftLimit {
  uint8_t    id        = 0;       // Slot ID (1..MAX_LIMITS)
  bool       defined   = false;   // True if coordinate exists (saved in memory/recipe)
  bool       enabled   = false;   // True if actively enforced during physical motion
  char       label[16] = {0};     // Semantic tag (e.g. "CHUCK", "SHLDR_1", "GROOVE")
  int32_t    coordDu   = 0;       // Workpiece coordinate in canonical deci-microns
  int32_t    stepPos   = 0;       // Raw leadscrew machine position in motor steps
  LimitBound bound     = LimitBound::AUTO; // Bound enforcement polarity

  inline bool isEnforced() const {
    return defined && enabled;
  }
};
