#pragma once
#include <cstdint>
#if defined(ESP32) || defined(ARDUINO)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

/**
 * @brief Spindle synchronous gearing engine (Electronic Leadscrew).
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 5.
 *          Implements [KIN-01] continuous spindle phase accumulation and
 *          [KIN-02] anchored 64-bit Bresenham accumulator with safe live trims.
 */
class SpindleSynchronizer {
public:
  SpindleSynchronizer() = default;

  /**
   * @brief Engages synchronous electronic gearing at the current spindle and axis positions.
   * @param currentSpindlePulses Current accumulated spindle encoder edge count.
   * @param currentAxisSteps Current axis position in raw motor steps.
   * @param pitchDupr Commanded pitch/lead in canonical deci-microns per spindle rev.
   */
  void IRAM_ATTR engage(int64_t currentSpindlePulses, int32_t currentAxisSteps, int32_t pitchDupr);

  /**
   * @brief Disengages synchronous electronic gearing.
   */
  void disengage();

  /**
   * @brief Live pitch trim (CMD_FEED_NUDGE) without historical jumps [KIN-02].
   * @details Shifts the anchor to the current target step and current spindle pulse,
   *          changing the slope incrementally from this point forward.
   * @param currentSpindlePulses Current accumulated spindle encoder edge count.
   * @param newPitchDupr New pitch/lead in canonical deci-microns per spindle rev.
   */
  void IRAM_ATTR updatePitchLive(int64_t currentSpindlePulses, int32_t newPitchDupr);

  /**
   * @brief Computes the ideal synchronous axis target step position [KIN-02].
   * @details Executed on real-time Core 1 ticks, including during deceleration and dwell.
   * @param accumulatedSpindlePulses Current accumulated spindle pulses from encoder HAL.
   * @param motorStepsPerRev Leadscrew motor steps per full revolution.
   * @param screwPitchDu Leadscrew pitch in canonical deci-microns.
   * @param effectiveEncoderEdgesPerRev Effective encoder edges per complete spindle revolution.
   * @return Ideal synchronized carriage target position in raw motor steps.
   */
  int32_t IRAM_ATTR computeIdealSyncSteps(
      int64_t accumulatedSpindlePulses,
      int32_t motorStepsPerRev,
      int32_t screwPitchDu,
      int32_t effectiveEncoderEdgesPerRev);

  /**
   * @brief Updates continuous spindle phase tracking [KIN-01].
   * @details Must be called across deceleration, pause, dwell, and reversal so phase is preserved.
   * @param deltaPulses Incremental spindle encoder edges since last sample.
   */
  void IRAM_ATTR updateSpindlePhase(int32_t deltaPulses);

  /**
   * @brief Returns continuous spindle phase pulse accumulator [KIN-01].
   */
  int64_t getSpindlePhasePulses() const { return m_spindlePhasePulses; }

  /**
   * @brief Sets or resets the continuous spindle phase pulse accumulator.
   */
  void resetSpindlePhase(int64_t phase = 0) { m_spindlePhasePulses = phase; }

  /**
   * @brief Calculates modulo thread phase within one spindle revolution.
   * @param effectiveEncoderEdgesPerRev Spindle pulses per revolution.
   * @return Modulo spindle pulse phase [0, effectiveEncoderEdgesPerRev - 1].
   */
  int32_t calculateModuloPhasePulses(int32_t effectiveEncoderEdgesPerRev) const;

  bool isEngaged() const { return m_engaged; }
  int64_t getAnchorPulses() const { return m_anchorPulses; }
  int32_t getAnchorSteps() const { return m_anchorSteps; }
  int32_t getCurrentTargetSteps() const { return m_currentTargetSteps; }
  int32_t getPitchDupr() const { return m_pitchDupr; }

private:
  int64_t m_anchorPulses       = 0;
  int32_t m_anchorSteps        = 0;
  int32_t m_currentTargetSteps = 0;
  int32_t m_pitchDupr          = 0;

  int64_t m_spindlePhasePulses = 0; // Continuous phase tracking across dwell & decel [KIN-01]
  bool    m_engaged            = false;
};
