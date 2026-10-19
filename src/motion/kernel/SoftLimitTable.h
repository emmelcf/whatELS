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
    const int32_t deadband = allowPassThrough ? ADVANCE_DEADBAND_STEPS : 0;

    if (direction < 0) {
      // Slot 0 is designated Left/Inward limit
      if (m_limits[0].isEnforced()) {
        int32_t threshold = currentSteps - deadband;
        if (m_limits[0].stepPos <= threshold) {
          return &m_limits[0];
        }
      }
    } else if (direction > 0) {
      // Slot 1 is designated Right/Outward limit
      if (m_limits[1].isEnforced()) {
        int32_t threshold = currentSteps + deadband;
        if (m_limits[1].stepPos >= threshold) {
          return &m_limits[1];
        }
      }
    }

    return nullptr;
  }

  // Backwards-compatible convenience getters for single-limit displays
  const SoftLimit* getLeftLimit() const { return m_limits[0].isEnforced() ? &m_limits[0] : nullptr; }
  const SoftLimit* getRightLimit() const { return m_limits[1].isEnforced() ? &m_limits[1] : nullptr; }

  bool setLimit(size_t slot, int32_t coordDu, int32_t stepPos, const char* label = nullptr, bool enable = true);
  bool setEnabled(size_t slot, bool enable);
  bool clearLimit(size_t slot);
  void clearAll();
  void updateOrigin(const AxisState& axis);

  const SoftLimit& getSlot(size_t slot) const;

private:
  SoftLimit m_limits[MAX_LIMITS];
  static const SoftLimit s_emptyLimit;
};
