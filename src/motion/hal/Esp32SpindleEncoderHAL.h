#pragma once
#include "ISpindleEncoderHAL.h"
#include <driver/pulse_cnt.h>
#include <esp_attr.h>
#include <atomic>

/**
 * @brief Concrete ESP32-S3 PCNT hardware quadrature counter implementation.
 * @details Conforms to HAL_SPECIFICATION.md Section 3.1 & 3.2.
 *          Implements [HAL-01] Zero-Loss 16-bit modular difference.
 */
class Esp32SpindleEncoderHAL : public ISpindleEncoderHAL {
public:
  Esp32SpindleEncoderHAL();
  virtual ~Esp32SpindleEncoderHAL();

  bool init(
      int pinA,
      int pinB,
      int pinIndex,
      uint16_t nativePpr,
      float pulleyRatio,
      uint16_t encoderTeeth = 0,
      uint16_t spindleTeeth = 0) override;

  int32_t getPulseDelta() override;
  int64_t getAccumulatedPulses() const override;
  int32_t getCountsPerSpindleRev() const override { return static_cast<int32_t>(m_countsPerSpindleRev); }
  uint32_t getFilteredRpm() const override;
  float getAngleDegrees() const override;
  bool hasIndexSynced() const override;
  void resetIndexSync() override;

  // --- Simulation & Test Injection Hooks ---
  void setSimulationActive(bool active) override;
  bool isSimulationActive() const override;
  void injectSimulatedPulses(int32_t delta) override;
  void setSimulatedRpm(float rpm) override;

private:
  static void IRAM_ATTR indexIsrHandler(void* arg);
  void updateRpmEstimate(int32_t delta, int64_t nowUs);

  // Simulation mode state
  std::atomic<bool>     m_simActive{false};
  std::atomic<float>    m_simRpm{0.0f};
  std::atomic<int32_t>  m_pendingSimPulses{0};
  bool                  m_wasSimActive{false};
  int32_t               m_maxSimPulseChunk{96}; // Max pulses consumed per tick (~1200 RPM at 4800 CPR)
  int64_t               m_lastSimTimeUs{0};
  float                 m_simFraction{0.0f};

  pcnt_unit_handle_t    m_unit{nullptr};
  pcnt_channel_handle_t m_chanA{nullptr};
  pcnt_channel_handle_t m_chanB{nullptr};

  int                   m_pinIndex{-1};
  uint16_t              m_nativePpr{1024};
  float                 m_pulleyRatio{1.0f};
  uint16_t              m_encoderTeeth{0};
  uint16_t              m_spindleTeeth{0};
  float                 m_countsPerSpindleRev{4096.0f};

  int16_t               m_lastCount16{0};
  int64_t               m_accumulatedPulses{0};

  std::atomic<bool>     m_hasIndexSynced{false};
  int64_t               m_lastIndexPulseTimeUs{0};

  // Speed estimation
  int64_t               m_lastRpmCalcTimeUs{0};
  float                 m_filteredRpm{0.0f};
  int32_t               m_rpmDeltaAccumulator{0};
};
