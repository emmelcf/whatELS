#pragma once
#include <cstdint>
#include <cstddef>
#if defined(ESP32) || defined(ARDUINO)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#include "SoftLimit.h"
#include "AxisState.h"

/**
 * @brief Multi-stop drum and soft limit lookahead subsystem.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 3.2.
 *          Fixes [KIN-03]: resting directly at an active limit blocks further travel
 *          in that direction unless allowPassThrough is explicitly commanded.
 */
class SoftLimitTable {
public:
  static constexpr size_t MAX_LIMITS = 8;
  static constexpr int32_t ADVANCE_DEADBAND_STEPS = 2; // Steps past stop when overriding

  SoftLimitTable();

  /**
   * @brief Queries the immediate next enforced soft limit strictly ahead in the motion path.
   * @details If resting directly on an active limit (currentSteps == stepPos), motion in that
   *          direction is BLOCKED (returns that limit with D_remaining = 0) unless allowPassThrough is true.
   * @param currentSteps Current axis position in raw motor steps.
   * @param direction Cartesian motion polarity (-1 toward Chuck/Center, +1 toward Tail/Retract).
   * @param allowPassThrough Set true ONLY when operator explicitly commands a pass-through override.
   * @return Pointer to next active limit, or nullptr if path is completely unbounded.
   */
  const SoftLimit* IRAM_ATTR findNextEnforcedLimit(
      int32_t currentSteps,
      int8_t direction,
      bool allowPassThrough = false) const {
    const SoftLimit* candidate = nullptr;
    const int32_t deadband = allowPassThrough ? ADVANCE_DEADBAND_STEPS : 0;

    if (direction < 0) {
      // Negative travel: find closest enforced lower bound at or below threshold
      int32_t highestPosBelow = INT32_MIN;
      int32_t threshold = currentSteps - deadband;
      for (size_t i = 0; i < MAX_LIMITS; ++i) {
        if (!m_limits[i].isEnforced()) continue;
        if (m_limits[i].bound == LimitBound::UPPER) continue; // Upper bounds do not block negative travel
        if (m_limits[i].stepPos <= threshold) {
          if (m_limits[i].stepPos > highestPosBelow) {
            highestPosBelow = m_limits[i].stepPos;
            candidate = &m_limits[i];
          }
        }
      }
    } else if (direction > 0) {
      // Positive travel: find closest enforced upper bound at or above threshold
      int32_t lowestPosAbove = INT32_MAX;
      int32_t threshold = currentSteps + deadband;
      for (size_t i = 0; i < MAX_LIMITS; ++i) {
        if (!m_limits[i].isEnforced()) continue;
        if (m_limits[i].bound == LimitBound::LOWER) continue; // Lower bounds do not block positive travel
        if (m_limits[i].stepPos >= threshold) {
          if (m_limits[i].stepPos < lowestPosAbove) {
            lowestPosAbove = m_limits[i].stepPos;
            candidate = &m_limits[i];
          }
        }
      }
    }

    return candidate;
  }

  // Backwards-compatible convenience getters for single-limit displays
  const SoftLimit* getLeftLimit() const {
    if (m_limits[0].isEnforced()) return &m_limits[0];
    for (size_t i = 0; i < MAX_LIMITS; ++i) {
      if (m_limits[i].isEnforced() && m_limits[i].bound == LimitBound::LOWER) return &m_limits[i];
    }
    return nullptr;
  }

  const SoftLimit* getRightLimit() const {
    if (m_limits[1].isEnforced()) return &m_limits[1];
    for (size_t i = 0; i < MAX_LIMITS; ++i) {
      if (m_limits[i].isEnforced() && m_limits[i].bound == LimitBound::UPPER) return &m_limits[i];
    }
    return nullptr;
  }

  bool setLimit(size_t slot, int32_t coordDu, int32_t stepPos, const char* label = nullptr, bool enable = true, LimitBound bound = LimitBound::AUTO);
  bool setEnabled(size_t slot, bool enable);
  bool clearLimit(size_t slot);
  void clearAll();
  void updateOrigin(const AxisState& axis);

  const SoftLimit& getSlot(size_t slot) const;

private:
  SoftLimit m_limits[MAX_LIMITS];
  static const SoftLimit s_emptyLimit;
};
