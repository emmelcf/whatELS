#include "Display.h"
#include "Config.h"
#include "Axis.h"
#include "Input.h"
#include "Motion.h"
#include "WebServerModule.h"
#include <Preferences.h>

// LCD Hash Tracking
long lcdHashLine0 = 0;
long lcdHashLine1 = 0;
long lcdHashLine2 = 0;
long lcdHashLine3 = 0;
bool splashScreen = false;

unsigned long saveTime = 0;
const int COLOR_NUMPAD_DEFAULT = 65535;

static unsigned long lastDisplayUpdateTime = 0;
static const unsigned long NEXTION_UPDATE_INTERVAL_MS = 100;
static const unsigned long NEXTION_WEB_UPDATE_INTERVAL_MS = 200;

void writeNextionCommandRaw(const String &command) {
  Serial1.print(command);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}

void toScreen(const String &command) {
  if (tftUploadActive) return;
  writeNextionCommandRaw(command);
}

void setText(const String &id, const String &text) {
  toScreen(id + ".txt=\"" + text + "\"");
}

void beep() {
  toScreen("play 0,0,0");
}

unsigned long getDisplayUpdateIntervalMs() {
  return webUiIsConnected() ? NEXTION_WEB_UPDATE_INTERVAL_MS : NEXTION_UPDATE_INTERVAL_MS;
}

void updateDisplay() {
  if (tftUploadActive) return;
  if (millis() - lastDisplayUpdateTime < getDisplayUpdateIntervalMs()) return;
  lastDisplayUpdateTime = millis();
  bool publishStatus = webBuffersReady && webUiIsConnected();

  int rpm = getApproxRpm();
  long numpadResult = getNumpadResult();
  bool spindleStopped = micros() > spindleEncTime + 100000;
  int pitchStatusDirection = getJoystickPitchStatusDirection();

  DisplaySnapshot snapshot = getDisplaySnapshot(rpm, numpadResult, spindleStopped, pitchStatusDirection);

  long newHashLine0 = isOn + spindlePosSync + mode + measure + dupr + starts;
  bool updateLine0 = lcdHashLine0 != newHashLine0;
  if (updateLine0) {
    lcdHashLine0 = newHashLine0;
    setText("bStatus", snapshot.status);
    setText("bMode", snapshot.mode);
    setText("tPitch", snapshot.pitch);
    setText("bMeasure", snapshot.measure);
  }

  long newHashLine1 = moveStep + rpm + spindlePos + measure + spindleDisplayMode * 17 + (spindleValColorGreen ? 300 : 0);
  bool updateLine1 = lcdHashLine1 != newHashLine1;
  if (updateLine1) {
    lcdHashLine1 = newHashLine1;
    setText("tStepVal", snapshot.step);
    setText("tRPMVal", snapshot.rpmText);
    setText("tTurnsVal", snapshot.turnsText);
    setText("tAngleVal", snapshot.angleText);

    setText("tSpindleTog", snapshot.spindleLabel);
    setText("tSpindleVal", snapshot.spindleValue);

    if (snapshot.spindleValColorGreen) {
      toScreen("tSpindleVal.pco=" + String(COLOR_DRO_ON));
    } else {
      toScreen("tSpindleVal.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
  }

  long newHashLine2 =
    x.pos + x.originPos + x.disabled + x.leftStop - x.rightStop +
    z.pos + z.originPos + z.disabled + z.leftStop - z.rightStop +
    y.pos + y.originPos + y.disabled + y.leftStop - y.rightStop + measure + x.pos % 100 +
    (xDisplayModeDRO ? (long)(xScale.getRawPosition() * 1000) : 0) +
    (zDisplayModeDRO ? (long)(zScale.getRawPosition() * 1000) : 0) +
    isDiameterMode + isAbsoluteMode + xDisplayModeDRO + zDisplayModeDRO;
  bool updateLine2 = lcdHashLine2 != newHashLine2;
  if (updateLine2) {
    lcdHashLine2 = newHashLine2;
    setText("tX", snapshot.xPos);
    if (snapshot.xIsDroActive) {
      toScreen("tX.pco=" + String(COLOR_DRO_ON));
    } else {
      toScreen("tX.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tXUp", snapshot.xStopUp);
    setText("tXDown", snapshot.xStopDown);
    setText("tY", snapshot.yPos);
    setText("tYUp", snapshot.yStopUp);
    setText("tYDown", snapshot.yStopDown);
    setText("tZ", snapshot.zPos);
    if (snapshot.zIsDroActive) {
      toScreen("tZ.pco=" + String(COLOR_DRO_ON));
    } else {
      toScreen("tZ.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tZLeft", snapshot.zStopLeft);
    setText("tZRight", snapshot.zStopRight);

    if (snapshot.isDiameterMode) {
      toScreen("tDia.pco=" + String(COLOR_DRO_ON));
      toScreen("tDia.font=3");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(COLOR_DRO_OFF));
      toScreen("tRad.font=2");
      setText("tRad", "rad");
    } else {
      toScreen("tDia.pco=" + String(COLOR_DRO_OFF));
      toScreen("tDia.font=2");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(COLOR_DRO_ON));
      toScreen("tRad.font=3");
      setText("tRad", "rad");
    }

    if (!snapshot.isAbsoluteMode) {
      toScreen("tInc.pco=" + String(COLOR_DRO_ON));
      toScreen("tInc.font=3");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(COLOR_DRO_OFF));
      toScreen("tAbs.font=2");
      setText("tAbs", "abs");
    } else {
      toScreen("tInc.pco=" + String(COLOR_DRO_OFF));
      toScreen("tInc.font=2");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(COLOR_DRO_ON));
      toScreen("tAbs.font=3");
      setText("tAbs", "abs");
    }
  }

  long gcodeCommandHash = 0;
  for (int i = 0; i < gcodeCommand.length(); i++) {
    gcodeCommandHash += gcodeCommand.charAt(i);
  }
  for (int i = 0; i < wifiStatus.length(); i++) {
    gcodeCommandHash += wifiStatus.charAt(i);
  }
  long newHashLine3 = z.pos + (showAngle ? spindlePos : -1) + (showTacho ? rpm : -2) + measure + (numpadResult > 0 ? numpadResult : -1) + mode * 5 + dupr +
      (mode == MODE_CONE ? round(coneRatio * 10000) : 0) + turnPasses + opIndex + setupIndex + gcodeProgramIndex + gcodeProgramCount + spindleStopped * 3 + (isOn ? 139 : -117) + (inNumpad ? 10 : 0) + (auxForward ? 17 : -31) +
      (z.leftStop == LONG_MAX ? 123 : z.leftStop) + (z.rightStop == LONG_MIN ? 1234 : z.rightStop) +
      (x.leftStop == LONG_MAX ? 1235 : x.leftStop) + (x.rightStop == LONG_MIN ? 123456 : x.rightStop) + gcodeCommandHash +
      (mode == MODE_Y ? y.pos + y.originPos + (y.leftStop == LONG_MAX ? 123 : y.leftStop) + (y.rightStop == LONG_MIN ? 1234 : y.rightStop) + y.disabled : 0) +
      pitchStatusDirection * 149 +
      (mode == MODE_JOYSTICK ? joystickLatheDirectionZ * 97 + joystickLatheDirectionX * 101 + joystickLatheFeedSignZ * 131 + joystickLatheFeedSignX * 137 + joystickLatheRapid * 17 + JOYSTICK_ENABLED * 19 + spindlePosSync * 151 : 0) + x.pos + x.originPos + z.pos;
  bool updateLine3 = lcdHashLine3 != newHashLine3;
  if (updateLine3) {
    lcdHashLine3 = newHashLine3;
    setText("t3", snapshot.message);
  }

  if (publishStatus) {
    publishMachineStatus(snapshot.status, snapshot.mode, snapshot.pitch, snapshot.measure, snapshot.step, snapshot.rpmText, snapshot.turnsText, snapshot.angleText,
      snapshot.xPos, snapshot.xStopUp, snapshot.xStopDown, snapshot.yPos, snapshot.yStopUp, snapshot.yStopDown, snapshot.zPos, snapshot.zStopLeft, snapshot.zStopRight, snapshot.message);
  }
}

bool saveIfChanged() {
  if (dupr == savedDupr && starts == savedStarts && z.pos == z.savedPos && z.originPos == z.savedOriginPos && z.posGlobal == z.savedPosGlobal && z.motorPos == z.savedMotorPos && z.leftStop == z.savedLeftStop && z.rightStop == z.savedRightStop && z.disabled == z.savedDisabled &&
      spindlePos == savedSpindlePos && spindlePosAvg == savedSpindlePosAvg && spindlePosSync == savedSpindlePosSync && savedSpindlePosGlobal == spindlePosGlobal && showAngle == savedShowAngle && showTacho == savedShowTacho && moveStep == savedMoveStep &&
      mode == savedMode && measure == savedMeasure && x.pos == x.savedPos && x.originPos == x.savedOriginPos && x.posGlobal == x.savedPosGlobal && x.motorPos == x.savedMotorPos && x.leftStop == x.savedLeftStop && x.rightStop == x.savedRightStop && x.disabled == x.savedDisabled &&
      y.pos == y.savedPos && y.originPos == y.savedOriginPos && y.posGlobal == y.savedPosGlobal && y.motorPos == y.savedMotorPos && y.leftStop == y.savedLeftStop && y.rightStop == y.savedRightStop && y.disabled == y.savedDisabled &&
      coneRatio == savedConeRatio && turnPasses == savedTurnPasses && savedAuxForward == auxForward &&
      zDisplayModeDRO == savedZDisplayModeDRO && xDisplayModeDRO == savedXDisplayModeDRO &&
      isDiameterMode == savedIsDiameterMode && isAbsoluteMode == savedIsAbsoluteMode) return false;

  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  if (dupr != savedDupr) pref.putLong(PREF_DUPR, savedDupr = dupr);
  if (starts != savedStarts) pref.putInt(PREF_STARTS, savedStarts = starts);
  if (z.pos != z.savedPos) pref.putLong(PREF_POS_Z, z.savedPos = z.pos);
  if (z.posGlobal != z.savedPosGlobal) pref.putLong(PREF_POS_GLOBAL_Z, z.savedPosGlobal = z.posGlobal);
  if (z.originPos != z.savedOriginPos) pref.putLong(PREF_ORIGIN_POS_Z, z.savedOriginPos = z.originPos);
  if (z.motorPos != z.savedMotorPos) pref.putLong(PREF_MOTOR_POS_Z, z.savedMotorPos = z.motorPos);
  if (z.leftStop != z.savedLeftStop) pref.putLong(PREF_LEFT_STOP_Z, z.savedLeftStop = z.leftStop);
  if (z.rightStop != z.savedRightStop) pref.putLong(PREF_RIGHT_STOP_Z, z.savedRightStop = z.rightStop);
  if (z.disabled != z.savedDisabled) pref.putBool(PREF_DISABLED_Z, z.savedDisabled = z.disabled);
  if (spindlePos != savedSpindlePos) pref.putLong(PREF_SPINDLE_POS, savedSpindlePos = spindlePos);
  if (spindlePosAvg != savedSpindlePosAvg) pref.putLong(PREF_SPINDLE_POS_AVG, savedSpindlePosAvg = spindlePosAvg);
  if (spindlePosSync != savedSpindlePosSync) pref.putInt(PREF_OUT_OF_SYNC, savedSpindlePosSync = spindlePosSync);
  if (spindlePosGlobal != savedSpindlePosGlobal) pref.putLong(PREF_SPINDLE_POS_GLOBAL, savedSpindlePosGlobal = spindlePosGlobal);
  if (showAngle != savedShowAngle) pref.putBool(PREF_SHOW_ANGLE, savedShowAngle = showAngle);
  if (showTacho != savedShowTacho) pref.putBool(PREF_SHOW_TACHO, savedShowTacho = showTacho);
  if (moveStep != savedMoveStep) pref.putLong(PREF_MOVE_STEP, savedMoveStep = moveStep);
  if (mode != savedMode) pref.putInt(PREF_MODE, savedMode = mode);
  if (measure != savedMeasure) pref.putInt(PREF_MEASURE, savedMeasure = measure);
  if (x.pos != x.savedPos) pref.putLong(PREF_POS_X, x.savedPos = x.pos);
  if (x.posGlobal != x.savedPosGlobal) pref.putLong(PREF_POS_GLOBAL_X, x.savedPosGlobal = x.posGlobal);
  if (x.originPos != x.savedOriginPos) pref.putLong(PREF_ORIGIN_POS_X, x.savedOriginPos = x.originPos);
  if (x.motorPos != x.savedMotorPos) pref.putLong(PREF_MOTOR_POS_X, x.savedMotorPos = x.motorPos);
  if (x.leftStop != x.savedLeftStop) pref.putLong(PREF_LEFT_STOP_X, x.savedLeftStop = x.leftStop);
  if (x.rightStop != x.savedRightStop) pref.putLong(PREF_RIGHT_STOP_X, x.savedRightStop = x.rightStop);
  if (x.disabled != x.savedDisabled) pref.putBool(PREF_DISABLED_X, x.savedDisabled = x.disabled);
  if (y.pos != y.savedPos) pref.putLong(PREF_POS_Y, y.savedPos = y.pos);
  if (y.posGlobal != y.savedPosGlobal) pref.putLong(PREF_POS_GLOBAL_Y, y.savedPosGlobal = y.posGlobal);
  if (y.originPos != y.savedOriginPos) pref.putLong(PREF_ORIGIN_POS_Y, y.savedOriginPos = y.originPos);
  if (y.motorPos != y.savedMotorPos) pref.putLong(PREF_MOTOR_POS_Y, y.savedMotorPos = y.motorPos);
  if (y.leftStop != y.savedLeftStop) pref.putLong(PREF_LEFT_STOP_Y, y.savedLeftStop = y.leftStop);
  if (y.rightStop != y.savedRightStop) pref.putLong(PREF_RIGHT_STOP_Y, y.savedRightStop = y.rightStop);
  if (y.disabled != y.savedDisabled) pref.putBool(PREF_DISABLED_Y, y.savedDisabled = y.disabled);
  if (coneRatio != savedConeRatio) pref.putFloat(PREF_CONE_RATIO, savedConeRatio = coneRatio);
  if (turnPasses != savedTurnPasses) pref.putInt(PREF_TURN_PASSES, savedTurnPasses = turnPasses);
  if (auxForward != savedAuxForward) pref.putBool(PREF_AUX_FORWARD, savedAuxForward = auxForward);
  if (zDisplayModeDRO != savedZDisplayModeDRO) pref.putBool(PREF_Z_DRO_MODE, savedZDisplayModeDRO = zDisplayModeDRO);
  if (xDisplayModeDRO != savedXDisplayModeDRO) pref.putBool(PREF_X_DRO_MODE, savedXDisplayModeDRO = xDisplayModeDRO);
  if (isDiameterMode != savedIsDiameterMode) pref.putBool(PREF_DIAMETER_MODE, savedIsDiameterMode = isDiameterMode);
  if (isAbsoluteMode != savedIsAbsoluteMode) pref.putBool(PREF_ABSOLUTE_MODE, savedIsAbsoluteMode = isAbsoluteMode);
  pref.end();
  return true;
}

void taskDisplay(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    updateDisplay();
    unsigned long now = micros();
    if (!stepperIsRunning(&z) && !stepperIsRunning(&x) && (now > spindleEncTime + SAVE_DELAY_US) && (now < saveTime || now > saveTime + SAVE_DELAY_US) && (now < keypadTimeUs || now > keypadTimeUs + SAVE_DELAY_US)) {
      if (saveIfChanged()) {
        saveTime = now;
      }
    }
    if (beepFlag) {
      beepFlag = false;
      beep();
    }
    if (abs(z.pendingPos) > z.estopSteps || abs(x.pendingPos) > x.estopSteps) {
      setEmergencyStop(ESTOP_POS);
    }
    taskYIELD();
  }
  setText("bMode", "ESTOP");
  if (emergencyStop == ESTOP_POS) {
    setText("t3", "Requested position outside machine");
  } else if (emergencyStop == ESTOP_MARK_ORIGIN) {
    setText("t3", "Unable to mark origin");
  } else if (emergencyStop == ESTOP_ON_OFF) {
    setText("t3", "Unable to turn on/off");
  } else if (emergencyStop == ESTOP_OFF_MANUAL_MOVE) {
    setText("t3", "Off during manual move");
  }
  vTaskDelete(NULL);
}
