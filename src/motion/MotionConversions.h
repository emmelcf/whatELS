#pragma once

#include <Arduino.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include "core/SettingsService.h"

/**
 * @file MotionConversions.h
 * @brief Subsystem 2 pure conversion utilities: canonical units, coordinate signs, and semantic intents.
 * @details Implements ADR 0001 & ADR 0002 standards for lossless conversion between human semantics,
 *          metric/imperial engineering units, and canonical integer deci-microns (du).
 */

namespace MotionConversions {

  // --- Canonical Linear Unit Conversions (1 du = 0.0001 mm = 0.1 µm) ---

  constexpr int32_t DU_PER_MM   = 10000;
  constexpr int32_t DU_PER_INCH = 254000;

  inline int32_t mmToDu(float mm) {
    return static_cast<int32_t>(roundf(mm * static_cast<float>(DU_PER_MM)));
  }

  inline float duToMm(int32_t du) {
    return static_cast<float>(du) / static_cast<float>(DU_PER_MM);
  }

  inline int32_t inchToDu(float inch) {
    return static_cast<int32_t>(roundf(inch * static_cast<float>(DU_PER_INCH)));
  }

  inline float duToInch(int32_t du) {
    return static_cast<float>(du) / static_cast<float>(DU_PER_INCH);
  }

  inline int32_t tpiToDupr(float tpi) {
    if (tpi <= 0.0001f) return 0;
    return static_cast<int32_t>(roundf(static_cast<float>(DU_PER_INCH) / tpi));
  }

  inline float duprToTpi(int32_t dupr) {
    if (dupr <= 0) return 0.0f;
    return static_cast<float>(DU_PER_INCH) / static_cast<float>(dupr);
  }

  inline int32_t feedMmPerMinToDu(float mmPerMin) {
    return mmToDu(mmPerMin);
  }

  // --- Axis Parsing ---

  inline AxisId parseAxisId(const char* str, AxisId fallback = AXIS_Z) {
    if (!str || !str[0]) return fallback;
    if (str[0] == 'X' || str[0] == 'x') return AXIS_X;
    return AXIS_Z;
  }

  // --- Semantic Intent to Cartesian Polarity (-1 / +1) ---

  struct DirectionMapping {
    const char* token;
    uint8_t     axisMask;  // 0x01 = Z, 0x02 = X, 0x03 = Any
    int8_t      direction; // -1 (Chuck/Center) or +1 (Tail/Retract)
  };

  static const DirectionMapping DIRECTION_TABLE[] = {
    // Z-Axis (Carriage)
    { "CHUCK",   0x01, -1 },
    { "LEFT",    0x01, -1 },
    { "TAIL",    0x01, +1 },
    { "RIGHT",   0x01, +1 },

    // X-Axis (Cross-Slide)
    { "CENTER",  0x02, -1 },
    { "INFEED",  0x02, -1 },
    { "IN",      0x02, -1 },
    { "RETRACT", 0x02, +1 },
    { "OUT",     0x02, +1 },
  };

  /**
   * @brief Converts semantic human direction strings into Cartesian coordinate polarity.
   * - Negative (-1): Coordinate decreasing (-Z towards chuck, -X towards spindle centerline).
   * - Positive (+1): Coordinate increasing (+Z towards tailstock, +X retract away from cut).
   * Note: Spindle terms FWD/REV are strictly rejected for axis feed motion.
   */
  inline int8_t semanticToDirection(AxisId axis, const char* str) {
    if (!str || !str[0]) return 0;

    // 1. Fast Path: Numeric coordinate sign (-1 or +1)
    if (str[0] == '-' || str[0] == '+' || (str[0] >= '0' && str[0] <= '9')) {
      int val = atoi(str);
      if (val < 0) return -1;
      if (val > 0) return 1;
      return 0;
    }

    // 2. Data-driven Table Lookup for semantic terms
    uint8_t mask = (axis == AXIS_X) ? 0x02 : 0x01;
    for (const auto& entry : DIRECTION_TABLE) {
      if ((entry.axisMask & mask) && strcasecmp(entry.token, str) == 0) {
        return entry.direction;
      }
    }

    return 0; // Unrecognized token (including banned FWD/REV)
  }

  /**
   * @brief Converts motion polarity into semantic human string for telemetry and logging.
   */
  inline const char* directionToSemantic(AxisId axis, int8_t dir) {
    if (axis == AXIS_X) {
      return (dir < 0) ? "CENTER" : "RETRACT";
    }
    return (dir < 0) ? "CHUCK" : "TAIL";
  }

} // namespace MotionConversions
