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

void setup() {
  Serial.begin(115200);
  delay(100);
  DEBUG_PRINTLN("--- NanoEls H5 Starting ---");
  DEBUG_PRINTLN("Loading configuration...");
  DEBUG_PRINTLN("Loading machine config...");
  loadMachineConfig();
  DEBUG_PRINTLN("Loading wifi config...");
  loadWifiConfig();
  DEBUG_PRINTLN("Loading keyboard config...");
  loadKeyboardConfig();

  DEBUG_PRINTLN("xScale.attach()...");
  Serial.flush();
  xScale.attach();
  DEBUG_PRINTLN("zScale.attach()...");
  zScale.attach();

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

  if (ACTIVE_Y) {
    pinMode(Y_DIR, OUTPUT);
    pinMode(Y_STEP, OUTPUT);
    pinMode(Y_ENA, OUTPUT);
    DHIGH(Y_STEP);
  }
  DEBUG_PRINTLN("initJoystick...");
  initJoystick();

  DEBUG_PRINTLN("main Preferences...");
  Preferences pref;
  DEBUG_PRINTLN("pref.begin...");
  pref.begin(PREF_NAMESPACE);
  if (pref.getInt(PREF_VERSION) != PREFERENCES_VERSION) {
    DEBUG_PRINTLN("pref.clear()...");
    pref.clear();
    pref.putInt(PREF_VERSION, PREFERENCES_VERSION);
  }
  DEBUG_PRINTLN("Loading main preferences...");

  initAxis(&z, NAME_Z, true, false, MOTOR_STEPS_Z, SCREW_Z_DU, PULSE_PER_REVOLUTION_Z, SPEED_START_Z, SPEED_MANUAL_MOVE_Z, ACCELERATION_Z, INVERT_Z, INVERT_Z_ENABLE, NEEDS_REST_Z, MAX_TRAVEL_MM_Z, BACKLASH_DU_Z, Z_ENA, Z_DIR, Z_STEP, Z_PULSE_A, Z_PULSE_B, PULSE_COUNTER_Z);
  initAxis(&x, NAME_X, true, false, MOTOR_STEPS_X, SCREW_X_DU, PULSE_PER_REVOLUTION_X, SPEED_START_X, SPEED_MANUAL_MOVE_X, ACCELERATION_X, INVERT_X, INVERT_X_ENABLE, NEEDS_REST_X, MAX_TRAVEL_MM_X, BACKLASH_DU_X, X_ENA, X_DIR, X_STEP, X_PULSE_A, X_PULSE_B, PULSE_COUNTER_X);
  initAxis(&y, NAME_Y, ACTIVE_Y, ROTARY_Y, MOTOR_STEPS_Y, SCREW_Y_DU, PULSE_PER_REVOLUTION_Y, SPEED_START_Y, SPEED_MANUAL_MOVE_Y, ACCELERATION_Y, INVERT_Y, INVERT_Y_ENABLE, NEEDS_REST_Y, MAX_TRAVEL_MM_Y, BACKLASH_DU_Y, Y_ENA, Y_DIR, Y_STEP, Y_PULSE_A, Y_PULSE_B, PULSE_COUNTER_Y);

  isOn = false;
  savedDupr = dupr = pref.getLong(PREF_DUPR);
  motionMutex = xSemaphoreCreateMutex();
  savedStarts = starts = min((int32_t)STARTS_MAX, max(static_cast<int32_t>(1), (int32_t)pref.getInt(PREF_STARTS)));
  z.savedPos = z.pos = pref.getLong(PREF_POS_Z);
  z.savedPosGlobal = z.posGlobal = pref.getLong(PREF_POS_GLOBAL_Z);
  z.savedOriginPos = z.originPos = pref.getLong(PREF_ORIGIN_POS_Z);
  z.savedMotorPos = z.motorPos = pref.getLong(PREF_MOTOR_POS_Z);
  z.savedLeftStop = z.leftStop = pref.getLong(PREF_LEFT_STOP_Z, LONG_MAX);
  z.savedRightStop = z.rightStop = pref.getLong(PREF_RIGHT_STOP_Z, LONG_MIN);
  z.savedDisabled = z.disabled = pref.getBool(PREF_DISABLED_Z, false);
  x.savedPos = x.pos = pref.getLong(PREF_POS_X);
  x.savedPosGlobal = x.posGlobal = pref.getLong(PREF_POS_GLOBAL_X);
  x.savedOriginPos = x.originPos = pref.getLong(PREF_ORIGIN_POS_X);
  x.savedMotorPos = x.motorPos = pref.getLong(PREF_MOTOR_POS_X);
  x.savedLeftStop = x.leftStop = pref.getLong(PREF_LEFT_STOP_X, LONG_MAX);
  x.savedRightStop = x.rightStop = pref.getLong(PREF_RIGHT_STOP_X, LONG_MIN);
  x.savedDisabled = x.disabled = pref.getBool(PREF_DISABLED_X, false);
  y.savedPos = y.pos = pref.getLong(PREF_POS_Y);
  y.savedPosGlobal = y.posGlobal = pref.getLong(PREF_POS_GLOBAL_Y);
  y.savedOriginPos = y.originPos = pref.getLong(PREF_ORIGIN_POS_Y);
  y.savedMotorPos = y.motorPos = pref.getLong(PREF_MOTOR_POS_Y);
  y.savedLeftStop = y.leftStop = pref.getLong(PREF_LEFT_STOP_Y, LONG_MAX);
  y.savedRightStop = y.rightStop = pref.getLong(PREF_RIGHT_STOP_Y, LONG_MIN);
  y.savedDisabled = y.disabled = pref.getBool(PREF_DISABLED_Y, false);
  savedSpindlePos = spindlePos = pref.getLong(PREF_SPINDLE_POS);
  savedSpindlePosAvg = spindlePosAvg = pref.getLong(PREF_SPINDLE_POS_AVG);
  savedSpindlePosSync = spindlePosSync = pref.getInt(PREF_OUT_OF_SYNC);
  savedSpindlePosGlobal = spindlePosGlobal = pref.getLong(PREF_SPINDLE_POS_GLOBAL);
  savedShowAngle = showAngle = pref.getBool(PREF_SHOW_ANGLE);
  savedShowTacho = showTacho = pref.getBool(PREF_SHOW_TACHO);
  savedMoveStep = moveStep = pref.getLong(PREF_MOVE_STEP, MOVE_STEP_1);
  savedMode = pref.getInt(PREF_MODE);
  if (savedMode < MODE_NORMAL || savedMode > MODE_SLOT || (savedMode == MODE_Y && !ACTIVE_Y)) savedMode = MODE_NORMAL;
  setModeFromLoop(savedMode);
  savedMeasure = measure = pref.getInt(PREF_MEASURE);
  savedConeRatio = coneRatio = pref.getFloat(PREF_CONE_RATIO, coneRatio);
  savedTurnPasses = turnPasses = pref.getInt(PREF_TURN_PASSES, turnPasses);
  savedAuxForward = auxForward = pref.getBool(PREF_AUX_FORWARD, true);
  savedZDisplayModeDRO = zDisplayModeDRO = pref.getBool(PREF_Z_DRO_MODE, false);
  savedXDisplayModeDRO = xDisplayModeDRO = pref.getBool(PREF_X_DRO_MODE, false);
  savedIsDiameterMode = isDiameterMode = pref.getBool(PREF_DIAMETER_MODE, true);
  savedIsAbsoluteMode = isAbsoluteMode = pref.getBool(PREF_ABSOLUTE_MODE, true);
  pref.end();

  if (!z.needsRest && !z.disabled) digitalWrite(z.ena, z.invertEnable ? LOW : HIGH);
  if (!x.needsRest && !x.disabled) digitalWrite(x.ena, x.invertEnable ? LOW : HIGH);
  if (y.active && !y.needsRest && !y.disabled) digitalWrite(y.ena, y.invertEnable ? LOW : HIGH);

  if (LittleFS.begin(true)) {
    gcodeProgramCount = getGcodeProgramCount();
  }

  if (WIFI_ENABLED) {
    webUiEventQueue = xQueueCreate(WEB_UI_EVENT_QUEUE_LENGTH, sizeof(WebUiEvent));
  }

  Serial1.begin(NEXTION_NORMAL_BAUD, SERIAL_8N1, 44, 43);

  keyboard.begin(KEY_DATA, KEY_CLOCK);
  xTaskCreatePinnedToCore(taskKeypad, "taskKeypad", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  if (joystickUsable()) xTaskCreatePinnedToCore(taskJoystick, "taskJoystick", 4000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);

  delay(1300);
  if (xTaskCreatePinnedToCore(taskAttachInterrupts, "taskAttachInterrupts", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */) == pdPASS) {
    while (!pulseCountersReady) {
      delay(1);
    }
  }
  xTaskCreatePinnedToCore(taskDisplay, "taskDisplay", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveZ, "taskMoveZ", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveX, "taskMoveX", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  if (ACTIVE_Y) xTaskCreatePinnedToCore(taskMoveY, "taskMoveY", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskGcode, "taskGcode", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
  if (WIFI_ENABLED) xTaskCreatePinnedToCore(taskWiFi, "taskWiFi", 10000 /* stack size */, NULL, 0 /* priority */, NULL, 0 /* core */);
}

void loop() {
  if (emergencyStop != ESTOP_NONE) {
    return;
  }
  if (xSemaphoreTake(motionMutex, 1) != pdTRUE) {
    return;
  }
  applySettings();
  processSpindleCounter();
  discountFullSpindleTurns();
  int currentMode = mode;
  if (currentMode == MODE_JOYSTICK && spindlePosSync != 0 && !joystickLatheFeedCommandActive()) {
    cancelJoystickLatheSync();
    resetJoystickLatheFeedPosition();
  }
  if (!isOn || (dupr == 0 && currentMode != MODE_SLOT) || spindlePosSync != 0) {
    if (currentMode == MODE_JOYSTICK && (!isOn || dupr == 0)) {
      resetJoystickLatheFeedPosition();
    }
  } else if (currentMode == MODE_NORMAL) {
    modeGearbox(&z);
  } else if (currentMode == MODE_XGEAR) {
    modeGearbox(&x);
  } else if (currentMode == MODE_JOYSTICK) {
    modeJoystick();
  } else if (currentMode == MODE_TURN) {
    modeTurn(&z, &x);
  } else if (currentMode == MODE_FACE) {
    modeTurn(&x, &z);
  } else if (currentMode == MODE_CUT) {
    modeCut();
  } else if (currentMode == MODE_SLOT) {
    modeSlot();
  } else if (currentMode == MODE_CONE) {
    modeCone();
  } else if (currentMode == MODE_THREAD) {
    modeTurn(&z, &x);
  } else if (currentMode == MODE_ELLIPSE) {
    modeEllipse(&z, &x);
  }
  moveAxis(&z);
  moveAxis(&x);
  if (ACTIVE_Y) moveAxis(&y);
  xSemaphoreGive(motionMutex);
}
