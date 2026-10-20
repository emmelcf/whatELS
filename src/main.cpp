#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "Config.h"
#include "WebServerModule.h"
#include "DRO_Scale.h"
#include "Debug.h"
#include "UsbKeyboard.h"
#include "SettingsService.h"
#include "core/OperatorIntent.h"
#include "modes/ModeRegistry.h"
#include "motion/hal/Esp32StepperHAL.h"
#include "motion/hal/Esp32SpindleEncoderHAL.h"
#include "motion/kernel/MotionEngine.h"

static Esp32StepperHAL stepperHal;
static Esp32SpindleEncoderHAL encoderHal;

void setup() {
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  Serial.begin(DEBUG_UART_BAUD);
#else
  Serial.begin(DEBUG_UART_BAUD, SERIAL_8N1, DEBUG_UART_RX_PIN, DEBUG_UART_TX_PIN);
#endif
  delay(100);
  DEBUG_PRINTLN("--- whatELS Controller Starting ---");
  DEBUG_PRINTLN("Mounting LittleFS...");
  Serial.flush();
  if (LittleFS.begin(true)) {
    gcodeProgramCount = getGcodeProgramCount();
  }
  DEBUG_PRINTLN("Loading configuration...");
  DEBUG_PRINTLN("Loading machine config...");
  loadMachineConfig();
  DEBUG_PRINTLN("Initializing SettingsService & Machine Profile...");
  SettingsService::init();
  DEBUG_PRINTLN("Loading wifi config...");
  loadWifiConfig();
  DEBUG_PRINTLN("Loading keyboard config...");
  loadKeyboardConfig();

  if (activeMachineProfile.xAxis.hasDroScale) {
    DEBUG_PRINTLN("xScale.attach()...");
    Serial.flush();
    xScale.attach();
  }
  if (activeMachineProfile.zAxis.hasDroScale) {
    DEBUG_PRINTLN("zScale.attach()...");
    zScale.attach();
  }

  DEBUG_PRINTLN("pinMode setup...");
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);

  pinMode(Z_DIR, OUTPUT);
  pinMode(Z_STEP, OUTPUT);
  pinMode(Z_ENA, OUTPUT);
  DHIGH(Z_STEP);

  pinMode(X_DIR, OUTPUT);
  pinMode(X_STEP, OUTPUT);
  pinMode(X_ENA, OUTPUT);
  DHIGH(X_STEP);





  // Always boot cleanly and safely into standard manual feed / DRO mode
  DEBUG_PRINTLN("Initializing ModeRegistry & standard modes...");
  ModeRegistry::initStandardModes();

  DEBUG_PRINTLN("Setting default mode LatheMode::NORMAL...");
  Serial.flush();
  SettingsService::requestMode(static_cast<int>(LatheMode::NORMAL));
  DEBUG_PRINTLN("Default mode configured.");
  Serial.flush();

  DEBUG_PRINTLN("LittleFS mounted.");
  Serial.flush();




  // Initialize and wire MotionEngine to Core 1 ([CONC-01], Slice 3)
  DEBUG_PRINTLN("Initializing MotionEngine HAL and Core 1 Task...");
  Serial.flush();
  StepTimingConfig defaultTiming;
  stepperHal.configureAxis(AXIS_Z, Z_STEP, Z_DIR, Z_ENA, defaultTiming);
  stepperHal.configureAxis(AXIS_X, X_STEP, X_DIR, X_ENA, defaultTiming);

  int indexPin = activeMachineProfile.spindle.encoder.hasIndex ? activeMachineProfile.spindle.encoder.indexPin : -1;
  uint16_t spindlePpr = activeMachineProfile.spindle.encoder.ppr;
  float pulleyRatio = activeMachineProfile.spindle.pulleyRatio > 0.0f ? activeMachineProfile.spindle.pulleyRatio : 1.0f;
  encoderHal.init(ENC_A, ENC_B, indexPin, spindlePpr, pulleyRatio);

  MotionEngine& engine = MotionEngine::instance();
  engine.setStepperHAL(&stepperHal);
  engine.setEncoderHAL(&encoderHal);

  AxisGeometry zGeom;
  zGeom.screwPitchDu = activeMachineProfile.zAxis.leadscrewPitchDu();
  zGeom.motorStepsPerRev = activeMachineProfile.zAxis.motorSteps();
  zGeom.backlashSteps = duToSteps(zGeom, activeMachineProfile.zAxis.backlashDu());
  zGeom.maxVelocityMmSec = activeMachineProfile.zAxis.maxVelocityMmMin / 60.0f;
  zGeom.maxAccelMmSec2 = activeMachineProfile.zAxis.maxAccelMmSec2;
  engine.configureAxis(AXIS_Z, zGeom, defaultTiming);

  AxisGeometry xGeom;
  xGeom.screwPitchDu = activeMachineProfile.xAxis.leadscrewPitchDu();
  xGeom.motorStepsPerRev = activeMachineProfile.xAxis.motorSteps();
  xGeom.backlashSteps = duToSteps(xGeom, activeMachineProfile.xAxis.backlashDu());
  xGeom.maxVelocityMmSec = activeMachineProfile.xAxis.maxVelocityMmMin / 60.0f;
  xGeom.maxAccelMmSec2 = activeMachineProfile.xAxis.maxAccelMmSec2;
  engine.configureAxis(AXIS_X, xGeom, defaultTiming);

  engine.startTask(22, 1);
  DEBUG_PRINTLN("MotionEngine task started on Core 1 (priority 22).");
  Serial.flush();
  if (sysConfig.wifi.enabled) {
    DEBUG_PRINTLN("Creating taskWiFi...");
    Serial.flush();
    xTaskCreatePinnedToCore(taskWiFi, "taskWiFi", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  }
  DEBUG_PRINTLN("setup() completed successfully!");
  Serial.flush();
}

void loop() {
  if (emergencyStop != EstopCode::NONE) {
    vTaskDelay(pdMS_TO_TICKS(10));
    return;
  }

  SettingsService::applyCore1PendingLatches();

  LatheMode currentMode = static_cast<LatheMode>(SettingsService::getMode());
  IELSMode* activeMode = ModeRegistry::getActiveMode();
  if (activeMode) {
    bool isEngaged = SettingsService::isOn();
    long duprVal = SettingsService::getThreadSetting().dupr;
    if (!isEngaged || (duprVal == 0 && currentMode != LatheMode::SLOT && currentMode != LatheMode::ASYNC)) {
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
