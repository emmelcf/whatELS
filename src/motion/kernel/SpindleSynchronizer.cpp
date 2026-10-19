#include "SpindleSynchronizer.h"

void SpindleSynchronizer::engage(int64_t currentSpindlePulses, int32_t currentAxisSteps, int32_t pitchDupr) {
  m_anchorPulses       = currentSpindlePulses;
  m_anchorSteps        = currentAxisSteps;
  m_currentTargetSteps = currentAxisSteps;
  m_pitchDupr          = pitchDupr;
  m_engaged            = true;
}

void SpindleSynchronizer::disengage() {
  m_engaged = false;
}

void SpindleSynchronizer::updatePitchLive(int64_t currentSpindlePulses, int32_t newPitchDupr) {
  m_anchorSteps  = m_currentTargetSteps;
  m_anchorPulses = currentSpindlePulses;
  m_pitchDupr    = newPitchDupr;
}

int32_t SpindleSynchronizer::computeIdealSyncSteps(
    int64_t accumulatedSpindlePulses,
    int32_t motorStepsPerRev,
    int32_t screwPitchDu,
    int32_t effectiveEncoderEdgesPerRev) {
  int64_t deltaPulses = accumulatedSpindlePulses - m_anchorPulses;
  int64_t num = deltaPulses * static_cast<int64_t>(motorStepsPerRev) * static_cast<int64_t>(m_pitchDupr);
  int64_t den = static_cast<int64_t>(screwPitchDu) * static_cast<int64_t>(effectiveEncoderEdgesPerRev);

  if (den == 0) {
    return m_currentTargetSteps;
  }

  // Symmetric integer rounding [KIN-05]
  int64_t stepOffset = (num >= 0) ? ((num + den / 2) / den) : ((num - den / 2) / den);
  m_currentTargetSteps = m_anchorSteps + static_cast<int32_t>(stepOffset);
  return m_currentTargetSteps;
}

int64_t SpindleSynchronizer::calculatePulsesForStep(
    int32_t targetStep,
    int32_t motorStepsPerRev,
    int32_t screwPitchDu,
    int32_t effectiveEncoderEdgesPerRev) const {
  if (motorStepsPerRev == 0 || m_pitchDupr == 0) {
    return m_anchorPulses;
  }
  int64_t stepOffset = targetStep - m_anchorSteps;
  int64_t num = stepOffset * static_cast<int64_t>(screwPitchDu) * static_cast<int64_t>(effectiveEncoderEdgesPerRev);
  int64_t den = static_cast<int64_t>(motorStepsPerRev) * static_cast<int64_t>(m_pitchDupr);
  if (den == 0) {
    return m_anchorPulses;
  }
  return m_anchorPulses + (num / den);
}

void SpindleSynchronizer::updateSpindlePhase(int32_t deltaPulses) {
  m_spindlePhasePulses += deltaPulses;
}

int32_t SpindleSynchronizer::calculateModuloPhasePulses(int32_t effectiveEncoderEdgesPerRev) const {
  if (effectiveEncoderEdgesPerRev <= 0) {
    return 0;
  }
  int64_t mod = m_spindlePhasePulses % effectiveEncoderEdgesPerRev;
  if (mod < 0) {
    mod += effectiveEncoderEdgesPerRev;
  }
  return static_cast<int32_t>(mod);
}
