#include "Esp32SpindleEncoderHAL.h"
#include <Arduino.h>
#include <esp_timer.h>
#include <cmath>

Esp32SpindleEncoderHAL::Esp32SpindleEncoderHAL() = default;

Esp32SpindleEncoderHAL::~Esp32SpindleEncoderHAL() {
  if (m_pinIndex >= 0) {
    detachInterrupt(digitalPinToInterrupt(m_pinIndex));
    m_pinIndex = -1;
  }
  if (m_chanA) {
    pcnt_del_channel(m_chanA);
    m_chanA = nullptr;
  }
  if (m_chanB) {
    pcnt_del_channel(m_chanB);
    m_chanB = nullptr;
  }
  if (m_unit) {
    pcnt_unit_stop(m_unit);
    pcnt_unit_disable(m_unit);
    pcnt_del_unit(m_unit);
    m_unit = nullptr;
  }
}

void IRAM_ATTR Esp32SpindleEncoderHAL::indexIsrHandler(void* arg) {
  auto* self = static_cast<Esp32SpindleEncoderHAL*>(arg);
  if (self) {
    self->m_hasIndexSynced.store(true, std::memory_order_relaxed);
    self->m_lastIndexPulseTimeUs = esp_timer_get_time();
  }
}

bool Esp32SpindleEncoderHAL::init(
    int pinA,
    int pinB,
    int pinIndex,
    uint16_t nativePpr,
    float pulleyRatio,
    uint16_t encoderTeeth,
    uint16_t spindleTeeth) {
  m_nativePpr = (nativePpr > 0) ? nativePpr : 1024;
  m_pulleyRatio = (pulleyRatio > 0.0f) ? pulleyRatio : 1.0f;
  m_encoderTeeth = encoderTeeth;
  m_spindleTeeth = spindleTeeth;

  // Calculate 4x quadrature counts per physical spindle turn
  float ratio = 1.0f;
  if (m_spindleTeeth > 0 && m_encoderTeeth > 0) {
    ratio = static_cast<float>(m_spindleTeeth) / static_cast<float>(m_encoderTeeth);
  } else if (m_pulleyRatio > 0.0f) {
    ratio = m_pulleyRatio;
  }
  m_countsPerSpindleRev = static_cast<float>(m_nativePpr) * 4.0f * ratio;
  if (m_countsPerSpindleRev <= 0.0f) {
    m_countsPerSpindleRev = 4096.0f;
  }

  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);

  // Configure free-running 16-bit signed PCNT unit [-32768, +32767]
  pcnt_unit_config_t unitConfig = {
      .low_limit = -32768,
      .high_limit = 32767,
  };
  esp_err_t err = pcnt_new_unit(&unitConfig, &m_unit);
  if (err != ESP_OK || !m_unit) {
    return false;
  }

  // Hardware glitch filter: 500 ns to reject VFD PWM noise ([HAL-01])
  pcnt_glitch_filter_config_t filterConfig = {
      .max_glitch_ns = 500,
  };
  pcnt_unit_set_glitch_filter(m_unit, &filterConfig);

  // 4x Quadrature decoding via dual channels on the single PCNT unit
  pcnt_chan_config_t chanAConfig = {
      .edge_gpio_num = pinA,
      .level_gpio_num = pinB,
  };
  err = pcnt_new_channel(m_unit, &chanAConfig, &m_chanA);
  if (err != ESP_OK) {
    return false;
  }
  pcnt_channel_set_edge_action(m_chanA, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
  pcnt_channel_set_level_action(m_chanA, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  pcnt_chan_config_t chanBConfig = {
      .edge_gpio_num = pinB,
      .level_gpio_num = pinA,
  };
  err = pcnt_new_channel(m_unit, &chanBConfig, &m_chanB);
  if (err != ESP_OK) {
    return false;
  }
  pcnt_channel_set_edge_action(m_chanB, PCNT_CHANNEL_EDGE_ACTION_DECREASE, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  pcnt_channel_set_level_action(m_chanB, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);

  // Optional Index Pulse (Phase Z)
  if (pinIndex >= 0) {
    m_pinIndex = pinIndex;
    pinMode(m_pinIndex, INPUT_PULLUP);
    attachInterruptArg(digitalPinToInterrupt(m_pinIndex), indexIsrHandler, this, RISING);
  }

  pcnt_unit_enable(m_unit);
  pcnt_unit_clear_count(m_unit); // Initial boot-time zero ONLY
  pcnt_unit_start(m_unit);

  m_lastCount16 = 0;
  m_accumulatedPulses = 0;
  m_filteredRpm = 0.0f;
  m_rpmDeltaAccumulator = 0;
  m_lastRpmCalcTimeUs = esp_timer_get_time();

  return true;
}

int32_t IRAM_ATTR Esp32SpindleEncoderHAL::getPulseDelta() {
  if (!m_unit) {
    return 0;
  }

  // [HAL-01] Sample 16-bit hardware register without clearing the counter
  int rawCount = 0;
  pcnt_unit_get_count(m_unit, &rawCount);

  int16_t current16 = static_cast<int16_t>(rawCount);
  int16_t delta16   = static_cast<int16_t>(current16 - m_lastCount16);
  m_lastCount16     = current16;
  m_accumulatedPulses += delta16;

  int64_t nowUs = esp_timer_get_time();
  updateRpmEstimate(delta16, nowUs);

  return delta16;
}

int64_t Esp32SpindleEncoderHAL::getAccumulatedPulses() const {
  return m_accumulatedPulses;
}

void Esp32SpindleEncoderHAL::updateRpmEstimate(int32_t delta, int64_t nowUs) {
  m_rpmDeltaAccumulator += delta;
  int64_t elapsedUs = nowUs - m_lastRpmCalcTimeUs;

  // Update filtered RPM at ~50 Hz (20 ms window)
  if (elapsedUs >= 20000) {
    float dtMinutes = static_cast<float>(elapsedUs) / 60000000.0f;
    float instRpm = (std::abs(m_rpmDeltaAccumulator) / m_countsPerSpindleRev) / dtMinutes;

    // Exponential moving average filter (alpha = 0.25)
    m_filteredRpm = (0.75f * m_filteredRpm) + (0.25f * instRpm);

    m_rpmDeltaAccumulator = 0;
    m_lastRpmCalcTimeUs = nowUs;
  } else if (elapsedUs > 200000 && m_rpmDeltaAccumulator == 0) {
    // Spindle has come to a complete stop
    m_filteredRpm = 0.0f;
  }
}

uint32_t Esp32SpindleEncoderHAL::getFilteredRpm() const {
  return static_cast<uint32_t>(std::round(m_filteredRpm));
}

float Esp32SpindleEncoderHAL::getAngleDegrees() const {
  if (m_countsPerSpindleRev <= 0.0f) {
    return 0.0f;
  }
  double pos = static_cast<double>(m_accumulatedPulses);
  double revs = pos / static_cast<double>(m_countsPerSpindleRev);
  double frac = revs - std::floor(revs);
  return static_cast<float>(frac * 360.0);
}

bool Esp32SpindleEncoderHAL::hasIndexSynced() const {
  return m_hasIndexSynced.load(std::memory_order_relaxed);
}

void Esp32SpindleEncoderHAL::resetIndexSync() {
  m_hasIndexSynced.store(false, std::memory_order_relaxed);
}
