#pragma once
#include "IStepperHAL.h"
#include <cstdint>
#include <cstddef>

/**
 * @brief Test double for IStepperHAL.
 * @details Records step pulses, direction changes, driver enabling, and pulse train states.
 *          Used in automated E2E simulation tests and verification harnesses.
 */
class MockStepperHAL : public IStepperHAL {
public:
  static constexpr size_t MAX_AXES = 2;

  MockStepperHAL() {
    reset();
  }

  virtual ~MockStepperHAL() = default;

  bool configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return false;
    m_stepPin[idx] = stepPin;
    m_dirPin[idx] = dirPin;
    m_enablePin[idx] = enablePin;
    m_timing[idx] = timing;
    m_configured[idx] = true;
    return true;
  }

  void setDirection(AxisId axis, int8_t dir) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    int8_t normDir = (dir >= 0) ? 1 : -1;
    if (m_direction[idx] != normDir) {
      m_directionChanges[idx]++;
      m_direction[idx] = normDir;
    }
  }

  int8_t getDirection(AxisId axis) const override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return 0;
    return m_direction[idx];
  }

  void setEnabled(AxisId axis, bool enable) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    m_enabled[idx] = enable;
  }

  bool isEnabled(AxisId axis) const override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return false;
    return m_enabled[idx];
  }

  void pulseStep(AxisId axis) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    m_totalSteps[idx]++;
    if (m_direction[idx] > 0) {
      m_forwardSteps[idx]++;
    } else {
      m_reverseSteps[idx]++;
    }
  }

  void startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    setDirection(axis, direction);
    m_pulseTrainActive[idx] = true;
    m_pulseFrequencyHz[idx] = frequencyHz;
  }

  void updatePulseFrequency(AxisId axis, uint32_t frequencyHz) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    m_pulseFrequencyHz[idx] = frequencyHz;
  }

  void stopPulseTrain(AxisId axis) override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return;
    m_pulseTrainActive[idx] = false;
    m_pulseFrequencyHz[idx] = 0;
  }

  bool isPulseTrainActive(AxisId axis) const override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return false;
    return m_pulseTrainActive[idx];
  }

  bool hasDriverFault(AxisId axis) const override {
    size_t idx = static_cast<size_t>(axis);
    if (idx >= MAX_AXES) return false;
    return m_fault[idx];
  }

  // --- Mock Inspection & Injection Helpers ---

  void reset() {
    for (size_t i = 0; i < MAX_AXES; ++i) {
      m_configured[i] = false;
      m_enabled[i] = false;
      m_direction[i] = 1;
      m_directionChanges[i] = 0;
      m_totalSteps[i] = 0;
      m_forwardSteps[i] = 0;
      m_reverseSteps[i] = 0;
      m_pulseTrainActive[i] = false;
      m_pulseFrequencyHz[i] = 0;
      m_fault[i] = false;
      m_stepPin[i] = -1;
      m_dirPin[i] = -1;
      m_enablePin[i] = -1;
    }
  }

  int64_t getTotalSteps(AxisId axis) const {
    size_t idx = static_cast<size_t>(axis);
    return (idx < MAX_AXES) ? m_totalSteps[idx] : 0;
  }

  int64_t getForwardSteps(AxisId axis) const {
    size_t idx = static_cast<size_t>(axis);
    return (idx < MAX_AXES) ? m_forwardSteps[idx] : 0;
  }

  int64_t getReverseSteps(AxisId axis) const {
    size_t idx = static_cast<size_t>(axis);
    return (idx < MAX_AXES) ? m_reverseSteps[idx] : 0;
  }

  uint32_t getDirectionChanges(AxisId axis) const {
    size_t idx = static_cast<size_t>(axis);
    return (idx < MAX_AXES) ? m_directionChanges[idx] : 0;
  }

  uint32_t getPulseFrequencyHz(AxisId axis) const {
    size_t idx = static_cast<size_t>(axis);
    return (idx < MAX_AXES) ? m_pulseFrequencyHz[idx] : 0;
  }

  void setDriverFault(AxisId axis, bool fault) {
    size_t idx = static_cast<size_t>(axis);
    if (idx < MAX_AXES) m_fault[idx] = fault;
  }

private:
  bool             m_configured[MAX_AXES]{false, false};
  bool             m_enabled[MAX_AXES]{false, false};
  int8_t           m_direction[MAX_AXES]{1, 1};
  uint32_t         m_directionChanges[MAX_AXES]{0, 0};
  int64_t          m_totalSteps[MAX_AXES]{0, 0};
  int64_t          m_forwardSteps[MAX_AXES]{0, 0};
  int64_t          m_reverseSteps[MAX_AXES]{0, 0};
  bool             m_pulseTrainActive[MAX_AXES]{false, false};
  uint32_t         m_pulseFrequencyHz[MAX_AXES]{0, 0};
  bool             m_fault[MAX_AXES]{false, false};
  int              m_stepPin[MAX_AXES]{-1, -1};
  int              m_dirPin[MAX_AXES]{-1, -1};
  int              m_enablePin[MAX_AXES]{-1, -1};
  StepTimingConfig m_timing[MAX_AXES];
};
