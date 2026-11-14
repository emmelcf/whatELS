#include "Esp32StepperHAL.h"
#include <algorithm>

Esp32StepperHAL::Esp32StepperHAL() {
  for (size_t i = 0; i < MAX_AXES; ++i) {
    m_timerContexts[i].hal = this;
    m_timerContexts[i].axis = static_cast<AxisId>(i);
  }
}

Esp32StepperHAL::~Esp32StepperHAL() {
  for (size_t i = 0; i < MAX_AXES; ++i) {
    stopPulseTrain(static_cast<AxisId>(i));
    cleanupTimer(i);
    cleanupRmt(i);
  }
}

bool Esp32StepperHAL::configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;

  stopPulseTrain(axis);
  cleanupTimer(idx);
  cleanupRmt(idx);

  m_axes[idx].stepPin = stepPin;
  m_axes[idx].dirPin = dirPin;
  m_axes[idx].enablePin = enablePin;
  m_axes[idx].timing = timing;
  m_axes[idx].currentDir = 1;
  m_axes[idx].stepPulsePhase.store(PULSE_IDLE, std::memory_order_relaxed);
  m_axes[idx].pendingPulses.store(0, std::memory_order_relaxed);

#if defined(ESP32) || defined(ARDUINO)
  if (stepPin >= 0) {
    if (!initRmt(idx)) {
      return false;
    }
    restoreStepGpio(idx);
  }
  if (dirPin >= 0) {
    gpio_reset_pin(static_cast<gpio_num_t>(dirPin));
    gpio_set_direction(static_cast<gpio_num_t>(dirPin), GPIO_MODE_OUTPUT);
    int initialDirLevel = timing.invertDir ? 0 : 1;
    gpio_set_level(static_cast<gpio_num_t>(dirPin), initialDirLevel);
  }
  if (enablePin >= 0) {
    gpio_reset_pin(static_cast<gpio_num_t>(enablePin));
    gpio_set_direction(static_cast<gpio_num_t>(enablePin), GPIO_MODE_OUTPUT);
    // Inactive level: if invertEnable, active is 1 so inactive is 0; if normal, active is 0 so inactive is 1.
    gpio_set_level(static_cast<gpio_num_t>(enablePin), timing.invertEnable ? 0 : 1);
  }

  if (!initTimer(idx)) {
    return false;
  }
#endif

  m_axes[idx].configured = true;
  m_axes[idx].enabled = false;
  m_axes[idx].enableTimestampUs = 0;
  m_axes[idx].lastDirChangeTimestampUs = 0;
  m_axes[idx].lastStepTimestampUs = 0;
  return true;
}

bool Esp32StepperHAL::configureFaultPin(AxisId axis, int faultPin, bool activeLow) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;
  m_axes[idx].faultPin = faultPin;
  m_axes[idx].faultActiveLow = activeLow;

#if defined(ESP32) || defined(ARDUINO)
  if (faultPin >= 0) {
    gpio_num_t gpio = static_cast<gpio_num_t>(faultPin);
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    if (activeLow) {
      gpio_pulldown_dis(gpio);
      gpio_pullup_en(gpio);
    } else {
      gpio_pullup_dis(gpio);
      gpio_pulldown_en(gpio);
    }
  }
#endif
  return true;
}

void IRAM_ATTR Esp32StepperHAL::setDirection(AxisId axis, int8_t dir) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured) return;

  int8_t normDir = (dir >= 0) ? 1 : -1;
  if (m_axes[idx].currentDir != normDir) {
#if defined(ESP32) || defined(ARDUINO)
    int64_t nowUs = esp_timer_get_time();
    int64_t elapsedSinceStepUs = nowUs - m_axes[idx].lastStepTimestampUs;
    int64_t requiredHoldUs = (m_axes[idx].timing.dirHoldTimeNs + 999) / 1000;
    if (elapsedSinceStepUs < requiredHoldUs && m_axes[idx].lastStepTimestampUs > 0) {
      // Non-blocking hold guard: defer direction toggle until dirHoldTimeNs elapses
      return;
    }

    int pinLevel = (normDir > 0) ? 1 : 0;
    if (m_axes[idx].timing.invertDir) {
      pinLevel = !pinLevel;
    }
    if (m_axes[idx].dirPin >= 0) {
      gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].dirPin), pinLevel);
    }
    m_axes[idx].lastDirChangeTimestampUs = nowUs;
#endif
    m_axes[idx].currentDir = normDir;
  }
}

int8_t Esp32StepperHAL::getDirection(AxisId axis) const {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return 1;
  return m_axes[idx].currentDir;
}

void Esp32StepperHAL::setEnabled(AxisId axis, bool enable) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured) return;

  if (enable) {
    if (!m_axes[idx].enabled) {
      m_axes[idx].enabled = true;
#if defined(ESP32) || defined(ARDUINO)
      if (m_axes[idx].enablePin >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].enablePin), m_axes[idx].timing.invertEnable ? 1 : 0);
      }
      m_axes[idx].enableTimestampUs = esp_timer_get_time();
#endif
    }
  } else {
    if (m_axes[idx].enabled) {
      stopPulseTrain(axis);
      m_axes[idx].enabled = false;
#if defined(ESP32) || defined(ARDUINO)
      if (m_axes[idx].enablePin >= 0) {
        gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].enablePin), m_axes[idx].timing.invertEnable ? 0 : 1);
      }
#endif
    }
  }
}

bool Esp32StepperHAL::isEnabled(AxisId axis) const {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;
  return m_axes[idx].enabled;
}

void IRAM_ATTR Esp32StepperHAL::pulseStep(AxisId axis) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured || !m_axes[idx].enabled) return;

#if defined(ESP32) || defined(ARDUINO)
  int64_t nowUs = esp_timer_get_time();

  // Non-blocking enable settling guard: gate pulses until driver power stage settles
  int64_t elapsedEnaMs = (nowUs - m_axes[idx].enableTimestampUs) / 1000;
  if (elapsedEnaMs < static_cast<int64_t>(m_axes[idx].timing.enableDelayMs)) {
    return;
  }

  // Non-blocking direction setup timing guard: ensure DIR is stable before STEP active edge
  int64_t elapsedSinceDirChangeUs = nowUs - m_axes[idx].lastDirChangeTimestampUs;
  int64_t requiredSetupUs = (m_axes[idx].timing.dirSetupDelayNs + 999) / 1000;
  if (elapsedSinceDirChangeUs < requiredSetupUs && m_axes[idx].lastDirChangeTimestampUs > 0) {
    return;
  }

  if (m_axes[idx].stepPin >= 0) {
    // If a hardware-timed pulse is already in flight, queue without dropping or busy-waiting
    if (m_axes[idx].stepPulsePhase.load(std::memory_order_acquire) != PULSE_IDLE) {
      m_axes[idx].pendingPulses.fetch_add(1, std::memory_order_relaxed);
      return;
    }

    int activeLevel = m_axes[idx].timing.invertStep ? 0 : 1;
    m_axes[idx].stepPulsePhase.store(PULSE_HIGH, std::memory_order_release);
    gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].stepPin), activeLevel);
    m_axes[idx].lastStepTimestampUs = nowUs;

    if (m_axes[idx].timerHandle) {
      gptimer_set_raw_count(m_axes[idx].timerHandle, 0);
      gptimer_start(m_axes[idx].timerHandle);
    } else {
      int inactiveLevel = m_axes[idx].timing.invertStep ? 1 : 0;
      gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].stepPin), inactiveLevel);
      m_axes[idx].stepPulsePhase.store(PULSE_IDLE, std::memory_order_release);
    }
  }
#endif
}

void Esp32StepperHAL::startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured || frequencyHz == 0) return;

  setEnabled(axis, true);
  setDirection(axis, direction);

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].rmtChannel && m_axes[idx].rmtEncoder && m_axes[idx].stepPin >= 0) {
    size_t symbolCount = buildPulseTrainSymbols(idx, frequencyHz);
    if (symbolCount == 0) return;

    if (m_axes[idx].pulseTrainActive) {
      rmt_disable(m_axes[idx].rmtChannel);
    }
    rmt_tx_switch_gpio(m_axes[idx].rmtChannel, static_cast<gpio_num_t>(m_axes[idx].stepPin), false);
    rmt_enable(m_axes[idx].rmtChannel);

    rmt_transmit_config_t txConfig = {
      .loop_count = -1,
      .flags = {
        .eot_level = static_cast<uint32_t>(m_axes[idx].timing.invertStep ? 1 : 0),
        .queue_nonblocking = 1,
      },
    };
    rmt_transmit(
        m_axes[idx].rmtChannel,
        m_axes[idx].rmtEncoder,
        m_axes[idx].rmtSymbols,
        symbolCount * sizeof(rmt_symbol_word_t),
        &txConfig);
  }
#endif

  m_axes[idx].pulseTrainActive = true;
  m_axes[idx].pulseFrequencyHz = frequencyHz;
}

void Esp32StepperHAL::updatePulseFrequency(AxisId axis, uint32_t frequencyHz) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured) return;

  if (frequencyHz == 0) {
    stopPulseTrain(axis);
    return;
  }

  if (!m_axes[idx].pulseTrainActive || frequencyHz == m_axes[idx].pulseFrequencyHz) {
    m_axes[idx].pulseFrequencyHz = frequencyHz;
    return;
  }

  m_axes[idx].pulseFrequencyHz = frequencyHz;

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].rmtChannel && m_axes[idx].rmtEncoder) {
    size_t symbolCount = buildPulseTrainSymbols(idx, frequencyHz);
    if (symbolCount == 0) return;

    rmt_disable(m_axes[idx].rmtChannel);
    rmt_enable(m_axes[idx].rmtChannel);

    rmt_transmit_config_t txConfig = {
      .loop_count = -1,
      .flags = {
        .eot_level = static_cast<uint32_t>(m_axes[idx].timing.invertStep ? 1 : 0),
        .queue_nonblocking = 1,
      },
    };
    rmt_transmit(
        m_axes[idx].rmtChannel,
        m_axes[idx].rmtEncoder,
        m_axes[idx].rmtSymbols,
        symbolCount * sizeof(rmt_symbol_word_t),
        &txConfig);
  }
#endif
}

void Esp32StepperHAL::stopPulseTrain(AxisId axis) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured) return;

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].rmtChannel && m_axes[idx].pulseTrainActive) {
    rmt_disable(m_axes[idx].rmtChannel);
    restoreStepGpio(idx);
  }
#endif

  m_axes[idx].pulseTrainActive = false;
  m_axes[idx].pulseFrequencyHz = 0;
}

bool Esp32StepperHAL::isPulseTrainActive(AxisId axis) const {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;
  return m_axes[idx].pulseTrainActive;
}

bool Esp32StepperHAL::hasDriverFault(AxisId axis) const {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].faultPin >= 0) {
    int level = gpio_get_level(static_cast<gpio_num_t>(m_axes[idx].faultPin));
    return m_axes[idx].faultActiveLow ? (level == 0) : (level != 0);
  }
#endif
  return false;
}

#if defined(ESP32) || defined(ARDUINO)
bool IRAM_ATTR Esp32StepperHAL::onTimerAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
  (void)edata;
  auto* ctx = static_cast<StepperTimerContext*>(user_ctx);
  if (!ctx || !ctx->hal) return false;

  size_t idx = static_cast<size_t>(ctx->axis);
  if (idx >= MAX_AXES) return false;

  AxisHardwareState& ax = ctx->hal->m_axes[idx];
  if (ax.stepPin < 0) {
    ax.stepPulsePhase.store(PULSE_IDLE, std::memory_order_release);
    gptimer_stop(timer);
    return false;
  }

  gpio_num_t stepGpio = static_cast<gpio_num_t>(ax.stepPin);
  int activeLevel = ax.timing.invertStep ? 0 : 1;
  int inactiveLevel = ax.timing.invertStep ? 1 : 0;

  uint8_t phase = ax.stepPulsePhase.load(std::memory_order_acquire);
  if (phase == PULSE_HIGH) {
    gpio_set_level(stepGpio, inactiveLevel);
    if (ax.pendingPulses.load(std::memory_order_relaxed) > 0) {
      ax.stepPulsePhase.store(PULSE_LOW_COOLDOWN, std::memory_order_release);
    } else {
      ax.stepPulsePhase.store(PULSE_IDLE, std::memory_order_release);
      gptimer_stop(timer);
    }
  } else if (phase == PULSE_LOW_COOLDOWN) {
    uint32_t pending = ax.pendingPulses.load(std::memory_order_relaxed);
    if (pending > 0) {
      ax.pendingPulses.fetch_sub(1, std::memory_order_relaxed);
      ax.stepPulsePhase.store(PULSE_HIGH, std::memory_order_release);
      gpio_set_level(stepGpio, activeLevel);
      ax.lastStepTimestampUs = esp_timer_get_time();
    } else {
      ax.stepPulsePhase.store(PULSE_IDLE, std::memory_order_release);
      gptimer_stop(timer);
    }
  } else {
    gptimer_stop(timer);
  }
  return false;
}

bool Esp32StepperHAL::initTimer(size_t axisIdx) {
  if (axisIdx >= MAX_AXES) return false;

  if (!m_axes[axisIdx].timerHandle) {
    gptimer_config_t timer_config = {
      .clk_src = GPTIMER_CLK_SRC_DEFAULT,
      .direction = GPTIMER_COUNT_UP,
      .resolution_hz = 1000000, // 1MHz = 1us resolution
      .flags = {
        .intr_shared = false,
      },
    };

    esp_err_t err = gptimer_new_timer(&timer_config, &m_axes[axisIdx].timerHandle);
    if (err != ESP_OK) {
      return false;
    }

    m_timerContexts[axisIdx].hal = this;
    m_timerContexts[axisIdx].axis = static_cast<AxisId>(axisIdx);

    gptimer_event_callbacks_t cbs = {
      .on_alarm = &Esp32StepperHAL::onTimerAlarm,
    };
    gptimer_register_event_callbacks(m_axes[axisIdx].timerHandle, &cbs, &m_timerContexts[axisIdx]);

    uint64_t pulseWidthUs = (m_axes[axisIdx].timing.stepPulseWidthNs + 999) / 1000;
    if (pulseWidthUs == 0) pulseWidthUs = 1;

    gptimer_alarm_config_t alarm_config = {
      .alarm_count = pulseWidthUs,
      .reload_count = 0,
      .flags = {
        .auto_reload_on_alarm = true,
      },
    };
    gptimer_set_alarm_action(m_axes[axisIdx].timerHandle, &alarm_config);
    gptimer_enable(m_axes[axisIdx].timerHandle);
  }
  return true;
}

void Esp32StepperHAL::cleanupTimer(size_t axisIdx) {
  if (axisIdx >= MAX_AXES) return;

  if (m_axes[axisIdx].timerHandle) {
    gptimer_stop(m_axes[axisIdx].timerHandle);
    gptimer_disable(m_axes[axisIdx].timerHandle);
    gptimer_del_timer(m_axes[axisIdx].timerHandle);
    m_axes[axisIdx].timerHandle = nullptr;
  }
}

bool Esp32StepperHAL::initRmt(size_t axisIdx) {
  if (axisIdx >= MAX_AXES || m_axes[axisIdx].stepPin < 0) return false;

  if (!m_axes[axisIdx].rmtChannel) {
    rmt_tx_channel_config_t txConfig = {
      .gpio_num = static_cast<gpio_num_t>(m_axes[axisIdx].stepPin),
      .clk_src = RMT_CLK_SRC_DEFAULT,
      .resolution_hz = 1000000, // 1 MHz (1 us per tick)
      .mem_block_symbols = 48,  // ESP32-S3 hardware block size
      .trans_queue_depth = 2,
      .intr_priority = 0,
      .flags = {
        .invert_out = 0,
        .with_dma = 0,
        .io_loop_back = 0,
        .io_od_mode = 0,
        .allow_pd = 0,
        .init_level = static_cast<uint32_t>(m_axes[axisIdx].timing.invertStep ? 1 : 0),
      },
    };
    if (rmt_new_tx_channel(&txConfig, &m_axes[axisIdx].rmtChannel) != ESP_OK) {
      return false;
    }
  }

  if (!m_axes[axisIdx].rmtEncoder) {
    rmt_copy_encoder_config_t copyConfig = {};
    if (rmt_new_copy_encoder(&copyConfig, &m_axes[axisIdx].rmtEncoder) != ESP_OK) {
      cleanupRmt(axisIdx);
      return false;
    }
  }

  return true;
}

void Esp32StepperHAL::cleanupRmt(size_t axisIdx) {
  if (axisIdx >= MAX_AXES) return;

  if (m_axes[axisIdx].rmtChannel) {
    rmt_disable(m_axes[axisIdx].rmtChannel);
    rmt_del_channel(m_axes[axisIdx].rmtChannel);
    m_axes[axisIdx].rmtChannel = nullptr;
  }
  if (m_axes[axisIdx].rmtEncoder) {
    rmt_del_encoder(m_axes[axisIdx].rmtEncoder);
    m_axes[axisIdx].rmtEncoder = nullptr;
  }
}

size_t Esp32StepperHAL::buildPulseTrainSymbols(size_t axisIdx, uint32_t frequencyHz) {
  if (axisIdx >= MAX_AXES || frequencyHz == 0) return 0;

  uint32_t pulseWidthUs = (m_axes[axisIdx].timing.stepPulseWidthNs + 999) / 1000;
  if (pulseWidthUs == 0) pulseWidthUs = 1;

  uint32_t periodUs = 1000000UL / frequencyHz;
  if (periodUs <= pulseWidthUs) {
    periodUs = pulseWidthUs + 1;
  }
  uint32_t offUs = periodUs - pulseWidthUs;

  uint16_t activeLevel = m_axes[axisIdx].timing.invertStep ? 0 : 1;
  uint16_t inactiveLevel = m_axes[axisIdx].timing.invertStep ? 1 : 0;

  size_t count = 0;
  uint32_t firstOff = std::min<uint32_t>(offUs, 32767U);
  offUs -= firstOff;

  m_axes[axisIdx].rmtSymbols[count++] = rmt_symbol_word_t{
    .duration0 = static_cast<uint16_t>(std::min<uint32_t>(pulseWidthUs, 32767U)),
    .level0 = activeLevel,
    .duration1 = static_cast<uint16_t>(firstOff),
    .level1 = inactiveLevel,
  };

  while (offUs > 0 && count < AxisHardwareState::MAX_RMT_SYMBOLS) {
    uint32_t d0 = std::min<uint32_t>(offUs, 32767U);
    offUs -= d0;
    uint32_t d1 = std::min<uint32_t>(offUs, 32767U);
    offUs -= d1;

    m_axes[axisIdx].rmtSymbols[count++] = rmt_symbol_word_t{
      .duration0 = static_cast<uint16_t>(d0),
      .level0 = inactiveLevel,
      .duration1 = static_cast<uint16_t>(d1),
      .level1 = inactiveLevel,
    };
  }

  return count;
}

void Esp32StepperHAL::restoreStepGpio(size_t axisIdx) {
  if (axisIdx >= MAX_AXES || m_axes[axisIdx].stepPin < 0) return;
  gpio_num_t stepGpio = static_cast<gpio_num_t>(m_axes[axisIdx].stepPin);
  gpio_reset_pin(stepGpio);
  gpio_set_direction(stepGpio, GPIO_MODE_OUTPUT);
  gpio_set_level(stepGpio, m_axes[axisIdx].timing.invertStep ? 1 : 0);
}
#else
bool Esp32StepperHAL::initTimer(size_t) { return true; }
void Esp32StepperHAL::cleanupTimer(size_t) {}
bool Esp32StepperHAL::initRmt(size_t) { return true; }
void Esp32StepperHAL::cleanupRmt(size_t) {}
size_t Esp32StepperHAL::buildPulseTrainSymbols(size_t, uint32_t) { return 0; }
void Esp32StepperHAL::restoreStepGpio(size_t) {}
#endif
