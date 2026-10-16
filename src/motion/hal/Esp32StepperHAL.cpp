#include "Esp32StepperHAL.h"

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
  }
}

bool Esp32StepperHAL::configureAxis(AxisId axis, int stepPin, int dirPin, int enablePin, const StepTimingConfig& timing) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;

  stopPulseTrain(axis);

  m_axes[idx].stepPin = stepPin;
  m_axes[idx].dirPin = dirPin;
  m_axes[idx].enablePin = enablePin;
  m_axes[idx].timing = timing;
  m_axes[idx].currentDir = 1;

#if defined(ESP32) || defined(ARDUINO)
  if (stepPin >= 0) {
    gpio_reset_pin(static_cast<gpio_num_t>(stepPin));
    gpio_set_direction(static_cast<gpio_num_t>(stepPin), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(stepPin), timing.invertStep ? 1 : 0);
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

bool Esp32StepperHAL::configureFaultPin(AxisId axis, int faultPin) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) return false;
  m_axes[idx].faultPin = faultPin;

#if defined(ESP32) || defined(ARDUINO)
  if (faultPin >= 0) {
    gpio_reset_pin(static_cast<gpio_num_t>(faultPin));
    gpio_set_direction(static_cast<gpio_num_t>(faultPin), GPIO_MODE_INPUT);
    gpio_pullup_en(static_cast<gpio_num_t>(faultPin));
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
      esp_rom_delay_us(static_cast<uint32_t>(requiredHoldUs - elapsedSinceStepUs));
    }

    int pinLevel = (normDir > 0) ? 1 : 0;
    if (m_axes[idx].timing.invertDir) {
      pinLevel = !pinLevel;
    }
    if (m_axes[idx].dirPin >= 0) {
      gpio_set_level(static_cast<gpio_num_t>(m_axes[idx].dirPin), pinLevel);
    }
    m_axes[idx].lastDirChangeTimestampUs = esp_timer_get_time();
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

  // Direction setup timing guard: ensure DIR is stable before STEP active edge
  int64_t elapsedSinceDirChangeUs = nowUs - m_axes[idx].lastDirChangeTimestampUs;
  int64_t requiredSetupUs = (m_axes[idx].timing.dirSetupDelayNs + 999) / 1000;
  if (elapsedSinceDirChangeUs < requiredSetupUs && m_axes[idx].lastDirChangeTimestampUs > 0) {
    esp_rom_delay_us(static_cast<uint32_t>(requiredSetupUs - elapsedSinceDirChangeUs));
  }

  // Generate calibrated active step pulse
  if (m_axes[idx].stepPin >= 0) {
    gpio_num_t stepGpio = static_cast<gpio_num_t>(m_axes[idx].stepPin);
    int activeLevel = m_axes[idx].timing.invertStep ? 0 : 1;
    int inactiveLevel = m_axes[idx].timing.invertStep ? 1 : 0;

    gpio_set_level(stepGpio, activeLevel);
    uint32_t pulseWidthUs = (m_axes[idx].timing.stepPulseWidthNs + 999) / 1000;
    if (pulseWidthUs == 0) pulseWidthUs = 1;
    esp_rom_delay_us(pulseWidthUs);
    gpio_set_level(stepGpio, inactiveLevel);

    m_axes[idx].lastStepTimestampUs = esp_timer_get_time();
  }
#endif
}

void Esp32StepperHAL::startPulseTrain(AxisId axis, uint32_t frequencyHz, int8_t direction) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured || frequencyHz == 0) return;

  setEnabled(axis, true);
  setDirection(axis, direction);

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].timerHandle) {
    uint64_t periodUs = 1000000ULL / frequencyHz;
    if (periodUs < 10) periodUs = 10; // 100 kHz safety ceiling

    gptimer_alarm_config_t alarm_config = {
      .alarm_count = periodUs,
      .reload_count = 0,
      .flags = {
        .auto_reload_on_alarm = true,
      },
    };
    gptimer_set_alarm_action(m_axes[idx].timerHandle, &alarm_config);
    gptimer_set_raw_count(m_axes[idx].timerHandle, 0);
    gptimer_start(m_axes[idx].timerHandle);
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

  m_axes[idx].pulseFrequencyHz = frequencyHz;

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].timerHandle && m_axes[idx].pulseTrainActive) {
    uint64_t periodUs = 1000000ULL / frequencyHz;
    if (periodUs < 10) periodUs = 10;

    gptimer_alarm_config_t alarm_config = {
      .alarm_count = periodUs,
      .reload_count = 0,
      .flags = {
        .auto_reload_on_alarm = true,
      },
    };
    gptimer_set_alarm_action(m_axes[idx].timerHandle, &alarm_config);
  }
#endif
}

void Esp32StepperHAL::stopPulseTrain(AxisId axis) {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES || !m_axes[idx].configured) return;

#if defined(ESP32) || defined(ARDUINO)
  if (m_axes[idx].timerHandle && m_axes[idx].pulseTrainActive) {
    gptimer_stop(m_axes[idx].timerHandle);
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
    // Leadshine / Stepper drivers typically assert fault LOW
    return gpio_get_level(static_cast<gpio_num_t>(m_axes[idx].faultPin)) == 0;
  }
#endif
  return false;
}

const AxisHardwareState& Esp32StepperHAL::getAxisState(AxisId axis) const {
  size_t idx = static_cast<size_t>(axis);
  if (idx >= MAX_AXES) {
    return m_axes[0];
  }
  return m_axes[idx];
}

#if defined(ESP32) || defined(ARDUINO)
bool IRAM_ATTR Esp32StepperHAL::onTimerAlarm(gptimer_handle_t timer, const gptimer_alarm_event_data_t* edata, void* user_ctx) {
  auto* ctx = static_cast<StepperTimerContext*>(user_ctx);
  if (ctx && ctx->hal) {
    ctx->hal->pulseStep(ctx->axis);
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
#else
bool Esp32StepperHAL::initTimer(size_t) { return true; }
void Esp32StepperHAL::cleanupTimer(size_t) {}
#endif
