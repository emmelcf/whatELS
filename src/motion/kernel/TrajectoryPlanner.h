#pragma once
#include <cstdint>
#include <cmath>
#if defined(ESP32) || defined(ARDUINO)
#include <esp_attr.h>
#endif
#ifndef IRAM_ATTR
#define IRAM_ATTR
#endif

#include "AxisGeometry.h"

/**
 * @brief Real-time acceleration profiler and deceleration window calculator.
 * @details Conforms to MOTION_ENGINE_SPECIFICATION.md Section 4.
 *          Implements real-time stopping distance calculation (d_stop = v^2 / 2a + margin),
 *          pre-cut runway entry RPM verification, and trapezoidal profile velocity updates.
 */
class TrajectoryPlanner {
public:
  enum class State : uint8_t {
    IDLE = 0,
    ACCELERATING,
    CRUISING,
    DECELERATING,
    HOLDING,
    STOPPED
  };

  TrajectoryPlanner() = default;

  /**
   * @brief Configures profile parameters in raw step units.
   * @param startVelStepsSec Minimum starting step rate (e.g. 50 steps/sec).
   * @param cruiseVelStepsSec Commanded cruising step rate.
   * @param endVelStepsSec Target final step rate when reaching target/stop (0 or startVel).
   * @param accelStepsSec2 Acceleration / Deceleration in steps/sec^2.
   */
  void configureProfile(
      int32_t startVelStepsSec,
      int32_t cruiseVelStepsSec,
      int32_t endVelStepsSec,
      int32_t accelStepsSec2);

  /**
   * @brief Resets planner state to IDLE.
   */
  void reset();

  /**
   * @brief Calculates theoretical and discrete-margin stopping distance in motor steps.
   * @details d_stop = ceil((v_i^2 - v_end^2) / 2a) + ceil(0.5 * v_i * dt).
   * @param currentVelocityStepsSec Current axis velocity in steps/second.
   * @param endVelocityStepsSec Target final velocity in steps/second (typically 0).
   * @param accelStepsSec2 Acceleration in steps/second^2.
   * @param sampleIntervalSec Loop sampling interval dt in seconds (e.g. 0.00025s for 4 kHz).
   * @return Required stopping distance in motor steps.
   */
  static int32_t IRAM_ATTR computeStoppingDistanceSteps(
      int32_t currentVelocityStepsSec,
      int32_t endVelocityStepsSec,
      int32_t accelStepsSec2,
      float sampleIntervalSec);

  /**
   * @brief Converts stopping distance in steps to canonical deci-microns (du).
   */
  static int32_t IRAM_ATTR computeStoppingDistanceDu(
      const AxisGeometry& geom,
      int32_t currentVelocityStepsSec,
      int32_t endVelocityStepsSec,
      int32_t accelStepsSec2,
      float sampleIntervalSec);

  /**
   * @brief Verifies whether the available runway distance allows safe stopping from entry velocity.
   * @details v_entry_max = sqrt(2 * a * D_remaining).
   * @param remainingDistanceSteps Steps to soft limit or end of stroke.
   * @param entryVelocityStepsSec Proposed or current velocity entering the cut.
   * @param accelStepsSec2 Acceleration in steps/sec^2.
   * @param outMaxSafeVelocity Optional pointer to receive computed v_entry_max.
   * @return True if remaining distance is sufficient to decelerate safely.
   */
  static bool IRAM_ATTR verifyRunway(
      int32_t remainingDistanceSteps,
      int32_t entryVelocityStepsSec,
      int32_t accelStepsSec2,
      int32_t* outMaxSafeVelocity = nullptr);

  /**
   * @brief Validates runway clearance against spindle RPM and thread lead prior to synchronization.
   * @param geom Axis geometry (pitch and steps/rev).
   * @param remainingDistanceDu Distance to next soft limit in deci-microns.
   * @param spindleRpm Spindle rotational speed.
   * @param pitchDupr Commanded thread lead in deci-microns per rev.
   * @param outMaxSafeRpm Optional pointer to receive max allowable RPM for this runway.
   * @return True if carriage can stop safely before the limit at the given RPM.
   */
  static bool verifyRunwaySpindle(
      const AxisGeometry& geom,
      int32_t remainingDistanceDu,
      uint32_t spindleRpm,
      int32_t pitchDupr,
      uint32_t* outMaxSafeRpm = nullptr);

  /**
   * @brief Advances the velocity profile for a single loop tick.
   * @param remainingDistanceSteps Current distance to target or soft limit in steps.
   * @param dt Sampling interval in seconds.
   * @return Next commanded step velocity in steps/second.
   */
  int32_t IRAM_ATTR updateVelocity(int32_t remainingDistanceSteps, float dt);

  // Getters
  State getState() const { return m_state; }
  int32_t getCurrentVelocity() const { return m_currentVelocity; }
  int32_t getCruiseVelocity() const { return m_cruiseVelocity; }
  int32_t getAccel() const { return m_accel; }
  bool isDecelerating() const { return m_state == State::DECELERATING; }
  bool isFinished() const { return m_state == State::STOPPED || m_state == State::IDLE; }

private:
  State   m_state           = State::IDLE;
  int32_t m_currentVelocity = 0;
  int32_t m_startVelocity   = 0;
  int32_t m_cruiseVelocity  = 0;
  int32_t m_endVelocity     = 0;
  int32_t m_accel           = 0;
};
