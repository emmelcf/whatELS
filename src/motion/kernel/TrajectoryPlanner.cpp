#include "TrajectoryPlanner.h"
#include "AxisState.h"
#include <algorithm>

void TrajectoryPlanner::configureProfile(
    int32_t startVelStepsSec,
    int32_t cruiseVelStepsSec,
    int32_t endVelStepsSec,
    int32_t accelStepsSec2) {
  m_startVelocity   = std::max<int32_t>(1, std::abs(startVelStepsSec));
  m_cruiseVelocity  = std::max<int32_t>(m_startVelocity, std::abs(cruiseVelStepsSec));
  m_endVelocity     = std::max<int32_t>(0, std::abs(endVelStepsSec));
  m_accel           = std::max<int32_t>(1, std::abs(accelStepsSec2));
  m_currentVelocity = m_startVelocity;
  m_state           = State::ACCELERATING;
}

void TrajectoryPlanner::reset() {
  m_state           = State::IDLE;
  m_currentVelocity = 0;
  m_startVelocity   = 0;
  m_cruiseVelocity  = 0;
  m_endVelocity     = 0;
  m_accel           = 0;
}

int32_t TrajectoryPlanner::computeStoppingDistanceSteps(
    int32_t currentVelocityStepsSec,
    int32_t endVelocityStepsSec,
    int32_t accelStepsSec2,
    float sampleIntervalSec) {
  int64_t v = std::abs(currentVelocityStepsSec);
  int64_t vEnd = std::abs(endVelocityStepsSec);
  int64_t a = std::abs(accelStepsSec2);

  if (a == 0) return 0;
  if (v <= vEnd) {
    // Already below end velocity; only discrete sample margin applies
    int32_t sampleMargin = static_cast<int32_t>(std::ceil(0.5f * static_cast<float>(v) * sampleIntervalSec));
    return std::max<int32_t>(0, sampleMargin);
  }

  // d_stop = ceil((v^2 - v_end^2) / 2a) + ceil(0.5 * v * dt)
  int64_t num = (v * v) - (vEnd * vEnd);
  int64_t den = 2 * a;
  int64_t decelSteps = (num + den - 1) / den; // Ceiling integer division

  int32_t sampleMargin = static_cast<int32_t>(std::ceil(0.5f * static_cast<float>(v) * sampleIntervalSec));
  return static_cast<int32_t>(decelSteps + sampleMargin);
}

int32_t TrajectoryPlanner::computeStoppingDistanceDu(
    const AxisGeometry& geom,
    int32_t currentVelocityStepsSec,
    int32_t endVelocityStepsSec,
    int32_t accelStepsSec2,
    float sampleIntervalSec) {
  int32_t stopSteps = computeStoppingDistanceSteps(
      currentVelocityStepsSec, endVelocityStepsSec, accelStepsSec2, sampleIntervalSec);
  return stepsToDu(geom, stopSteps);
}

bool TrajectoryPlanner::verifyRunway(
    int32_t remainingDistanceSteps,
    int32_t entryVelocityStepsSec,
    int32_t accelStepsSec2,
    int32_t* outMaxSafeVelocity) {
  if (remainingDistanceSteps <= 0 || accelStepsSec2 <= 0) {
    if (outMaxSafeVelocity) *outMaxSafeVelocity = 0;
    return false;
  }

  // v_entry_max = sqrt(2 * a * D_remaining)
  double maxVelD = std::sqrt(2.0 * static_cast<double>(accelStepsSec2) * static_cast<double>(remainingDistanceSteps));
  int32_t maxVel = static_cast<int32_t>(std::floor(maxVelD));

  if (outMaxSafeVelocity) {
    *outMaxSafeVelocity = maxVel;
  }

  return std::abs(entryVelocityStepsSec) <= maxVel;
}

bool TrajectoryPlanner::verifyRunwaySpindle(
    const AxisGeometry& geom,
    int32_t remainingDistanceDu,
    uint32_t spindleRpm,
    int32_t pitchDupr,
    uint32_t* outMaxSafeRpm) {
  if (remainingDistanceDu <= 0 || pitchDupr == 0 || geom.motorStepsPerRev == 0 || geom.screwPitchDu == 0) {
    if (outMaxSafeRpm) *outMaxSafeRpm = 0;
    return false;
  }

  // Convert acceleration from mm/s^2 to steps/s^2
  int32_t accelDuSec2 = static_cast<int32_t>(geom.maxAccelMmSec2 * 10000.0f);
  int32_t accelStepsSec2 = duToSteps(geom, accelDuSec2);
  if (accelStepsSec2 <= 0) accelStepsSec2 = 1000;

  int32_t remainingSteps = duToSteps(geom, remainingDistanceDu);

  // Commanded linear velocity: v_du_sec = (RPM / 60) * pitchDupr
  float rps = static_cast<float>(spindleRpm) / 60.0f;
  int32_t vDuSec = static_cast<int32_t>(rps * static_cast<float>(std::abs(pitchDupr)));
  int32_t vStepsSec = duToSteps(geom, vDuSec);

  int32_t maxSafeVelSteps = 0;
  bool isSafe = verifyRunway(remainingSteps, vStepsSec, accelStepsSec2, &maxSafeVelSteps);

  if (outMaxSafeRpm) {
    int32_t maxSafeDuSec = stepsToDu(geom, maxSafeVelSteps);
    if (pitchDupr != 0) {
      uint32_t maxRpm = static_cast<uint32_t>((static_cast<uint64_t>(maxSafeDuSec) * 60ULL) / std::abs(pitchDupr));
      *outMaxSafeRpm = maxRpm;
    } else {
      *outMaxSafeRpm = 0;
    }
  }

  return isSafe;
}

int32_t TrajectoryPlanner::updateVelocity(int32_t remainingDistanceSteps, float dt) {
  if (m_state == State::IDLE || m_state == State::STOPPED) {
    return 0;
  }

  int32_t dStop = computeStoppingDistanceSteps(m_currentVelocity, m_endVelocity, m_accel, dt);

  if (remainingDistanceSteps <= dStop) {
    m_state = State::DECELERATING;
  }

  int32_t velDelta = static_cast<int32_t>(std::round(static_cast<float>(m_accel) * dt));
  if (velDelta < 1) velDelta = 1;

  switch (m_state) {
    case State::ACCELERATING:
      m_currentVelocity += velDelta;
      if (m_currentVelocity >= m_cruiseVelocity) {
        m_currentVelocity = m_cruiseVelocity;
        m_state = State::CRUISING;
      }
      break;

    case State::CRUISING:
      m_currentVelocity = m_cruiseVelocity;
      break;

    case State::DECELERATING:
      if (m_currentVelocity > m_endVelocity + velDelta) {
        m_currentVelocity -= velDelta;
      } else {
        m_currentVelocity = m_endVelocity;
        if (remainingDistanceSteps <= 0 || m_currentVelocity <= 0) {
          m_state = State::STOPPED;
        }
      }
      break;

    case State::HOLDING:
      // Velocity held at 0 for pause / feed hold
      m_currentVelocity = 0;
      break;

    default:
      break;
  }

  return m_currentVelocity;
}
