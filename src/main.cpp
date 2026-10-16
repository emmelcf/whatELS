#include <Arduino.h>
#include <Preferences.h>
#include <LittleFS.h>
#include "Config.h"
#include "Axis.h"
#include "Input.h"
#include "Display.h"
#include "Motion.h"
#include "WebServerModule.h"
#include "DRO_Scale.h"
#include "Debug.h"
#include "UsbKeyboard.h"
#include "SettingsService.h"
#include "MachineModel.h"
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
  async_timer = timerBegin(TIMER_FREQ);
  initVelocityTimer();
  DEBUG_PRINTLN("--- NanoEls H5 Starting ---");
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

  DEBUG_PRINTLN("initJoystick...");
  initJoystick();



  DEBUG_PRINTLN("initAxis Z/X...");
  Serial.flush();
  AxisPins zPins = { Z_ENA, Z_DIR, Z_STEP, Z_PULSE_A, Z_PULSE_B, PULSE_COUNTER_Z };
  initAxis(&z, NAME_Z, true, false, activeMachineProfile.zAxis, sysConfig.mpg.zHandwheel.ppr, zPins);

  AxisPins xPins = { X_ENA, X_DIR, X_STEP, X_PULSE_A, X_PULSE_B, PULSE_COUNTER_X };
  initAxis(&x, NAME_X, true, false, activeMachineProfile.xAxis, sysConfig.mpg.xHandwheel.ppr, xPins);
  DEBUG_PRINTLN("initAxis done.");
  Serial.flush();

  isOn = false;
  dupr = 0;
  motionMutex = xSemaphoreCreateMutex();
  starts = 1;
  turnPasses = 1;
  coneRatio = 0.0f;
  auxForward = true;

  // Clean, deterministic boot state for physical axes (unreferenced / zero origin, no soft limits, enabled)
  z.savedPos = z.pos = 0;
  z.savedPosGlobal = z.posGlobal = 0;
  z.savedOriginPos = z.originPos = 0;
  z.savedMotorPos = z.motorPos = 0;
  z.savedLeftStop = z.leftStop = LONG_MAX;
  z.savedRightStop = z.rightStop = LONG_MIN;
  z.savedDisabled = z.disabled = false;

  x.savedPos = x.pos = 0;
  x.savedPosGlobal = x.posGlobal = 0;
  x.savedOriginPos = x.originPos = 0;
  x.savedMotorPos = x.motorPos = 0;
  x.savedLeftStop = x.leftStop = LONG_MAX;
  x.savedRightStop = x.rightStop = LONG_MIN;
  x.savedDisabled = x.disabled = false;

  // Spindle phase & angle unreferenced until index/tick sync
  savedSpindlePos = spindlePos = 0;
  savedSpindlePosAvg = spindlePosAvg = 0;
  savedSpindlePosSync = spindlePosSync = 0;
  savedSpindlePosGlobal = spindlePosGlobal = 0;

  // Always boot cleanly and safely into standard manual feed / DRO mode
  DEBUG_PRINTLN("Initializing ModeRegistry & standard modes...");
  ModeRegistry::initStandardModes();

  DEBUG_PRINTLN("setModeFromLoop LatheMode::NORMAL...");
  Serial.flush();
  setModeFromLoop(LatheMode::NORMAL);
  DEBUG_PRINTLN("setModeFromLoop done.");
  Serial.flush();

  // Initialize settings service and load operator preferences (Tiers 3 & 4)
  SettingsService::init();

  DEBUG_PRINTLN("Enabling stepper pins...");
  Serial.flush();
  if (!z.needsRest && !z.disabled) digitalWrite(z.ena, z.invertEnable ? LOW : HIGH);
  if (!x.needsRest && !x.disabled) digitalWrite(x.ena, x.invertEnable ? LOW : HIGH);

  DEBUG_PRINTLN("LittleFS mounted.");
  Serial.flush();

  motionSnapshotQueue = xQueueCreate(1, sizeof(StatusSnapshot));
  publishMotionSnapshot();

  if (sysConfig.wifi.enabled) {
    webUiEventQueue = xQueueCreate(WEB_UI_EVENT_QUEUE_LENGTH, sizeof(WebUiEvent));
  }

  DEBUG_PRINTLN("Serial1.begin()...");
  Serial.flush();
  Serial1.begin(NEXTION_NORMAL_BAUD, SERIAL_8N1, 44, 43);

#if ENABLE_USB_KEYBOARD
  initUsbKeyboard();
#endif

  DEBUG_PRINTLN("keyboard.begin()...");
  Serial.flush();
  keyboard.begin(KEY_DATA, KEY_CLOCK);
  DEBUG_PRINTLN("Creating keypad task...");
  Serial.flush();
  xTaskCreatePinnedToCore(taskKeypad, "taskKeypad", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  if (joystickUsable()) xTaskCreatePinnedToCore(taskJoystick, "taskJoystick", 4000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);

  DEBUG_PRINTLN("Starting delay before attachInterrupts...");
  Serial.flush();
  delay(1300);
  DEBUG_PRINTLN("Creating taskAttachInterrupts...");
  Serial.flush();
  if (xTaskCreatePinnedToCore(taskAttachInterrupts, "taskAttachInterrupts", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */) == pdPASS) {
    while (!pulseCountersReady) {
      delay(1);
    }
  }
  DEBUG_PRINTLN("pulseCountersReady! Creating display and motion tasks...");
  Serial.flush();
  xTaskCreatePinnedToCore(taskDisplay, "taskDisplay", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveZ, "taskMoveZ", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveX, "taskMoveX", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskGcode, "taskGcode", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);

  // Initialize and wire MotionEngine to Core 1 ([CONC-01], Slice 3)
  DEBUG_PRINTLN("Initializing MotionEngine HAL and Core 1 Task...");
  Serial.flush();
  StepTimingConfig defaultTiming;
  stepperHal.configureAxis(AXIS_Z, Z_STEP, Z_DIR, Z_ENA, defaultTiming);
  stepperHal.configureAxis(AXIS_X, X_STEP, X_DIR, X_ENA, defaultTiming);

  int indexPin = activeMachineProfile.spindle.encoder.hasIndex ? activeMachineProfile.spindle.encoder.indexPin : -1;
  uint16_t spindlePpr = activeMachineProfile.spindle.encoder.ppr > 0 ? activeMachineProfile.spindle.encoder.ppr : 1024;
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
    return;
  }
  if (xSemaphoreTake(motionMutex, 1) != pdTRUE) {
    return;
  }
  applySettings();
  processSpindleCounter();
  discountFullSpindleTurns();
  LatheMode currentMode = mode;
  if (currentMode == LatheMode::JOYSTICK && spindlePosSync != 0 && !joystickLatheFeedCommandActive()) {
    cancelJoystickLatheSync();
    resetJoystickLatheFeedPosition();
  }
  IELSMode* activeMode = ModeRegistry::getActiveMode();
  bool isAsyncRunning = (currentMode == LatheMode::ASYNC) || isVelocityFeedActive();
  if (!isOn || (dupr == 0 && currentMode != LatheMode::SLOT && !isAsyncRunning) || spindlePosSync != 0) {
    if (currentMode == LatheMode::JOYSTICK && (!isOn || dupr == 0)) {
      resetJoystickLatheFeedPosition();
    }
    if (activeMode && activeMode->isRunning()) {
      activeMode->stop(StopType::NORMAL_STOP);
    }
  } else if (activeMode && activeMode->getId() == ModeIds::GEARBOX &&
             (currentMode == LatheMode::NORMAL || currentMode == LatheMode::XGEAR || currentMode == LatheMode::CONE || currentMode == LatheMode::ASYNC)) {
    if (!activeMode->isRunning()) {
      activeMode->start();
    }
  } else if (currentMode == LatheMode::ASYNC) {
    // Stepping driven by async_timer interrupt
  } else if (currentMode == LatheMode::NORMAL) {
    modeGearbox(&z);
  } else if (currentMode == LatheMode::XGEAR) {
    modeGearbox(&x);
  } else if (currentMode == LatheMode::JOYSTICK) {
    modeJoystick();
  } else if (currentMode == LatheMode::TURN) {
    modeTurn(&z, &x);
  } else if (currentMode == LatheMode::FACE) {
    modeTurn(&x, &z);
  } else if (currentMode == LatheMode::CUT) {
    modeCut();
  } else if (currentMode == LatheMode::SLOT) {
    modeSlot();
  } else if (currentMode == LatheMode::CONE) {
    modeCone();
  } else if (currentMode == LatheMode::THREAD) {
    modeTurn(&z, &x);
  } else if (currentMode == LatheMode::ELLIPSE) {
    modeEllipse(&z, &x);
  }
  moveAxis(&z);
  moveAxis(&x);
  xSemaphoreGive(motionMutex);

  static unsigned long lastSnapshotPublish = 0;
  unsigned long now = millis();
  if (now - lastSnapshotPublish >= 20) { // 50 Hz inter-core snapshot cadence
    lastSnapshotPublish = now;
    publishMotionSnapshot();
  }
}
