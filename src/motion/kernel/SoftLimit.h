#pragma once
#include <cstdint>

/**
 * @brief Represents an operator-clamped virtual soft limit or shoulder stop.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 3.1.
 *          Models limit state explicitly without magic sentinels.
 */
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
