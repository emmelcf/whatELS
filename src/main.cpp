#include <Arduino.h>
#include <LittleFS.h>
#include <esp_log.h>
#include "Config.h"
#include "WebServerModule.h"
#include "DRO_Scale.h"
#include "SettingsService.h"
#include "modes/ModeRegistry.h"
#include "motion/hal/Esp32StepperHAL.h"
#include "motion/hal/Esp32SpindleEncoderHAL.h"
#include "motion/kernel/MotionEngine.h"
#include "core/Esp32SystemInfoHAL.h"
#include "core_client/StandardCommandParser.h"

static const char* TAG = "MAIN";

static Esp32StepperHAL stepperHal;
static Esp32SpindleEncoderHAL encoderHal;
static whatels::core::Esp32SystemInfoHAL systemInfoHal;

void setup() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(DEBUG_UART_BAUD);
#else
  Serial.begin(DEBUG_UART_BAUD, SERIAL_8N1, DEBUG_UART_RX_PIN, DEBUG_UART_TX_PIN);
#endif
  delay(100);
  ESP_LOGI(TAG, "--- whatELS Controller Starting ---");
  ESP_LOGI(TAG, "Mounting LittleFS...");
  if (LittleFS.begin(true)) {
    int gcodeCount = getGcodeProgramCount();
    ESP_LOGI(TAG, "LittleFS mounted successfully (%d programs)", gcodeCount);
  } else {
    ESP_LOGE(TAG, "Failed to mount LittleFS!");
  }
  ESP_LOGI(TAG, "Initializing SettingsService & Machine Profile...");
  SettingsService::init();
  ESP_LOGI(TAG, "Loading WiFi config...");
  loadWifiConfig();
  ESP_LOGI(TAG, "Loading keyboard config...");
  loadKeyboardConfig();
  ESP_LOGI(TAG, "Loading MPG handwheel config...");
  loadMpgConfig();

  if (SettingsService::hasCapability(CAP_DRO_X)) {
    ESP_LOGI(TAG, "Attaching X DRO scale...");
    xScale.attach();
  }
  if (SettingsService::hasCapability(CAP_DRO_Z)) {
    ESP_LOGI(TAG, "Attaching Z DRO scale...");
    zScale.attach();
  }

  // Always boot cleanly and safely into standard manual feed / DRO mode
  ESP_LOGI(TAG, "Initializing ModeRegistry & standard modes...");
  ModeRegistry::initStandardModes();

  ESP_LOGI(TAG, "Setting default mode LatheMode::NORMAL...");
  SettingsService::requestMode(static_cast<int>(LatheMode::NORMAL));

  // Initialize and wire MotionEngine to Core 1 ([CONC-01], Slice 3)
  ESP_LOGI(TAG, "Initializing MotionEngine HAL and Core 1 Task...");
  StepTimingConfig defaultTiming;
  stepperHal.configureAxis(AXIS_Z, Z_STEP, Z_DIR, Z_ENA, defaultTiming);
  stepperHal.configureAxis(AXIS_X, X_STEP, X_DIR, X_ENA, defaultTiming);

  const LatheConfiguration& profile = SettingsService::getActiveProfile();
  int indexPin = profile.spindle.encoder.hasIndex ? profile.spindle.encoder.indexPin : -1;
  uint16_t spindlePpr = profile.spindle.encoder.ppr;
  float pulleyRatio = profile.spindle.effectivePulleyRatio();
  encoderHal.init(ENC_A, ENC_B, indexPin, spindlePpr, pulleyRatio);

  MotionEngine& engine = MotionEngine::instance();
  engine.setStepperHAL(&stepperHal);
  engine.setEncoderHAL(&encoderHal);

  whatels::core_client::StandardCommandParser::instance().setSystemInfoHAL(&systemInfoHal);

  SettingsService::applyDerivedMachineConfig();

  engine.startTask(22, 1);
  ESP_LOGI(TAG, "MotionEngine task started on Core 1 (priority 22)");
  if (sysConfig.wifi.enabled) {
    ESP_LOGI(TAG, "Creating taskWiFi on Core 0...");
    xTaskCreatePinnedToCore(taskWiFi, "taskWiFi", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  }
  ESP_LOGI(TAG, "setup() completed successfully!");
}

void loop() {
  if (emergencyStop != EstopCode::NONE) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }

  LatheMode currentMode = static_cast<LatheMode>(SettingsService::getMode());
  IELSMode* activeMode = ModeRegistry::getActiveMode();
  if (activeMode) {
    bool isEngaged = SettingsService::isOn();
    long duprVal = SettingsService::getThreadSetting().dupr;
    if (!isEngaged || (duprVal == 0 && currentMode != LatheMode::ASYNC)) {
      if (activeMode->isRunning()) {
        activeMode->stop(StopType::NORMAL_STOP);
      }
    } else {
      if (!activeMode->isRunning()) {
        activeMode->start();
      }
    }
  }

  vTaskDelay(pdMS_TO_TICKS(1));
}
