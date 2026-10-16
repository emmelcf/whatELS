#pragma once
#include "ISpindleEncoderHAL.h"
#include <cstdint>

/**
 * @brief Test double for ISpindleEncoderHAL.
 * @details Simulates spindle quadrature pulses, continuous edge accumulation,
 *          filtered RPM, angular position, and index marker synchronization.
 */
class MockSpindleEncoderHAL : public ISpindleEncoderHAL {
public:
  MockSpindleEncoderHAL() {
    reset();
  }

  virtual ~MockSpindleEncoderHAL() = default;

  bool init(
      int pinA,
      int pinB,
      int pinIndex,
      uint16_t nativePpr,
      float pulleyRatio,
      uint16_t encoderTeeth = 0,
      uint16_t spindleTeeth = 0) override {
    m_pinA = pinA;
    m_pinB = pinB;
    m_pinIndex = pinIndex;
    m_nativePpr = nativePpr;
    m_pulleyRatio = pulleyRatio;
    m_encoderTeeth = encoderTeeth;
    m_spindleTeeth = spindleTeeth;
    m_initialized = true;
    return true;
  }

  int32_t getPulseDelta() override {
    int32_t d = m_pendingDelta;
    m_pendingDelta = 0;
    return d;
  }

  int64_t getAccumulatedPulses() const override {
    return m_accumulatedPulses;
  }

  int32_t getCountsPerSpindleRev() const override {
    return static_cast<int32_t>(m_nativePpr) * 4;
  }

  uint32_t getFilteredRpm() const override {
    return m_filteredRpm;
  }

  float getAngleDegrees() const override {
    return m_angleDegrees;
  }

  bool hasIndexSynced() const override {
    return m_indexSynced;
  }

  void resetIndexSync() override {
    m_indexSynced = false;
  }

  // --- Simulation & Test Injection Hooks ---

  void setSimulationActive(bool active) override {
    m_simulationActive = active;
  }

  bool isSimulationActive() const override {
    return m_simulationActive;
  }

  void injectSimulatedPulses(int32_t delta) override {
    injectPulses(delta);
  }

  void setSimulatedRpm(float rpm) override {
    setFilteredRpm(rpm > 0.0f ? static_cast<uint32_t>(rpm) : 0);
  }

  // --- Mock Injection & Control Helpers ---

  void reset() {
    m_initialized = false;
    m_accumulatedPulses = 0;
    m_pendingDelta = 0;
    m_filteredRpm = 0;
    m_angleDegrees = 0.0f;
    m_indexSynced = false;
    m_nativePpr = 1024;
    m_pulleyRatio = 1.0f;
    m_encoderTeeth = 0;
    m_spindleTeeth = 0;
    m_pinA = -1;
    m_pinB = -1;
    m_pinIndex = -1;
    m_simulationActive = false;
  }

  /**
   * @brief Injects incremental spindle encoder pulses into the test double.
   * @param delta Signed pulses to accumulate and buffer for getPulseDelta().
   */
  void injectPulses(int32_t delta) {
    m_accumulatedPulses += delta;
    m_pendingDelta += delta;

    // Update angular position if PPR is known
    int32_t effectivePpr = m_nativePpr * 4;
    if (effectivePpr > 0) {
      int64_t mod = m_accumulatedPulses % effectivePpr;
      if (mod < 0) mod += effectivePpr;
      m_angleDegrees = (static_cast<float>(mod) / static_cast<float>(effectivePpr)) * 360.0f;
    }
  }

  void setFilteredRpm(uint32_t rpm) {
    m_filteredRpm = rpm;
  }

  void setAngleDegrees(float degrees) {
    m_angleDegrees = degrees;
  }

  void setIndexSynced(bool synced) {
    m_indexSynced = synced;
  }

  bool isInitialized() const {
    return m_initialized;
  }

private:
  bool     m_initialized{false};
  int64_t  m_accumulatedPulses{0};
  int32_t  m_pendingDelta{0};
  uint32_t m_filteredRpm{0};
  float    m_angleDegrees{0.0f};
  bool     m_indexSynced{false};
  uint16_t m_nativePpr{1024};
  float    m_pulleyRatio{1.0f};
  uint16_t m_encoderTeeth{0};
  uint16_t m_spindleTeeth{0};
  int      m_pinA{-1};
  int      m_pinB{-1};
  int      m_pinIndex{-1};
  bool     m_simulationActive{false};
};
