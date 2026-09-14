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

// Returns number of letters printed.
String printDeciMicrons(long deciMicrons, int precisionPointsMax) {
  if (deciMicrons == 0) {
    return "0";
  }
  bool imperial = measure != MEASURE_METRIC;
  long v = imperial ? round(deciMicrons / 25.4) : deciMicrons;
  int points = 0;
  if (v == 0 && precisionPointsMax >= 5) {
    points = 5;
  } else if ((v % 10) != 0 && precisionPointsMax >= 4) {
    points = 4;
  } else if ((v % 100) != 0 && precisionPointsMax >= 3) {
    points = 3;
  } else if ((v % 1000) != 0 && precisionPointsMax >= 2) {
    points = 2;
  } else if ((v % 10000) != 0 && precisionPointsMax >= 1) {
    points = 1;
  }
  return String(deciMicrons / (imperial ? 254000.0 : 10000.0), points);
}

String printDegrees(long degrees10000) {
  int points = 0;
  if ((degrees10000 % 100) != 0) {
    points = 3;
  } else if ((degrees10000 % 1000) != 0) {
    points = 2;
  } else if ((degrees10000 % 10000) != 0) {
    points = 1;
  }
  return String(degrees10000 / 10000.0, points) + char(223); // degree symbol
}

String printDupr(long value) {
  if (measure != MEASURE_TPI || value == 0) return printDeciMicrons(value, 5);

  float tpi = 254000.0 / value;
  String result = "";
  if (abs(tpi - round(tpi)) < TPI_ROUND_EPSILON) {
    result = String(int(round(tpi)));
  } else {
    int tpi100 = round(tpi * 100);
    int points = 0;
    if ((tpi100 % 10) != 0) {
      points = 2;
    } else if ((tpi100 % 100) != 0) {
      points = 1;
    }
    result = String(tpi, points);
  }
  return result;
}

String printMode() {
  if (mode == MODE_NORMAL) return "GEAR";
  if (mode == MODE_XGEAR) return "XGEAR";
  if (mode == MODE_ASYNC) return "ASYNC";
  if (mode == MODE_CONE) return "CONE";
  if (mode == MODE_TURN) return "TURN";
  if (mode == MODE_FACE) return "FACE";
  if (mode == MODE_CUT) return "CUT";
  if (mode == MODE_SLOT) return "SLOT";
  if (mode == MODE_THREAD) return "THREAD";
  if (mode == MODE_ELLIPSE) return "ELLIP";
  if (mode == MODE_GCODE) return "GCODE";
  if (mode == MODE_Y) return "Y";
  if (mode == MODE_JOYSTICK) return "JOY";
  return "";
}

// String printAxisPos(Axis* a) {
//   if (a->rotational) return printDegrees(getAxisPosDu(a));
//   return printDeciMicrons(getAxisPosDu(a), 3);
// }

String printDistanceToLeftStop(Axis* a) {
  if (a->leftStop == LONG_MAX) return "";
  if (a->rotational) return printDegrees(getAxisLeftStopDistanceDu(a));
  return printDeciMicrons(getAxisLeftStopDistanceDu(a), 3);
}

String printDistanceToRightStop(Axis* a) {
  if (a->rightStop == LONG_MIN) return "";
  if (a->rotational) return printDegrees(getAxisRightStopDistanceDu(a));
  return printDeciMicrons(getAxisRightStopDistanceDu(a), 3);
}

String printNoTrailing0(float value) {
  long v = round(value * 100000);
  int points = 0;
  if ((v % 10) != 0) {
    points = 5;
  } else if ((v % 100) != 0) {
    points = 4;
  } else if ((v % 1000) != 0) {
    points = 3;
  } else if ((v % 10000) != 0) {
    points = 2;
  } else if ((v % 100000) != 0) {
    points = 1;
  }
  return String(value, points);
}

String joystickLatheDirectionText(int zDirection, int xDirection) {
  String result = "";
  if (zDirection > 0) result += "Carriage left";
  else if (zDirection < 0) result += "Carriage right";
  if (xDirection != 0) {
    if (result != "") result += " ";
    result += (xDirection > 0 ? "Cross in" : "Cross out");
  }
  return result;
}

int getApproxRpm() {
  unsigned long t = micros();
  unsigned long elapsedTime = t - spindleEncTime;
  if (elapsedTime > 50000) {
    // RPM less than 10.
    spindleEncTimeDiffBulk = 0;
    return 0;
  }
  int rpm = 0;
  if (spindleEncTimeDiffBulk > 0) {
    rpm = 60000000 / spindleEncTimeDiffBulk;
    if (abs(rpm - shownRpm) > (rpm < 1000 ? 3 : 5)) {
      // Don't update RPM with insignificant differences.
      shownRpm = rpm;
      shownRpmTime = t;
    }
  }
  return rpm;
}

unsigned long getDisplayUpdateIntervalMs() {
  return webUiIsConnected() ? NEXTION_WEB_UPDATE_INTERVAL_MS : NEXTION_UPDATE_INTERVAL_MS;
}

String buildDisplayMessage(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection) {
  String result = "";
  if (mode != MODE_JOYSTICK && pitchStatusDirection > 0) {
    result = "Pitch +";
  } else if (mode != MODE_JOYSTICK && pitchStatusDirection < 0) {
    result = "Pitch -";
  } else if (mode == MODE_GCODE) {
    if (setupIndex == 1 && gcodeProgramCount == 0) {
      result = "No stored programs";
    } else if (setupIndex == 1) {
      if (gcodeProgramIndex >= gcodeProgramCount) {
        result = "Program deleted";
      } else {
        result = getCurrentGcodeProgramName();
      }
    } else if (setupIndex == 2) {
      if (spindleStopped) result = "Turn on the spindle!";
      else result = "Spindle on. Go?";
    } else if (isOn) {
      result = gcodeCommand.substring(0, 20);
    }
  } else if (isPassMode()) {
    bool missingZStops = needZStops() && (z.leftStop == LONG_MAX || z.rightStop == LONG_MIN);
    bool missingStops = missingZStops || x.leftStop == LONG_MAX || x.rightStop == LONG_MIN;
    if (!inNumpad && missingStops) {
      result = needZStops() ? "Set all stops" : "Set X stops";
    } else if (numpadResult != 0 && setupIndex == 1) {
      long passes = min((long)PASSES_MAX, numpadResult);
      result = String(passes);
      if (passes == 1) result += " pass?";
      else result += " passes?";
    } else if (!isOn && setupIndex == 1) {
      result = String(turnPasses);
      if (turnPasses == 1) result += " pass?";
      else result += " passes?";
    } else if (!isOn && setupIndex == 2) {
      if (mode == MODE_FACE) {
        result = auxForward ? "Right to left?" : "Left to right?";
      } else if (mode == MODE_CUT) {
        result = dupr >= 0 ? "Pitch > 0, external" : "Pitch < 0, internal";
      } else {
        result = auxForward ? "External?" : "Internal?";
      }
    } else if (mode == MODE_THREAD && !isOn && setupIndex == 3) {
      result = "Cone ratio " + String(numpadToConeRatio(), 5) + "?";
    } else if (!isOn && setupIndex == getLastSetupIndex()) {
      long zOffset = getPassModeZStart() - z.pos;
      long xOffset = getPassModeXStart() - x.pos;
      result = "Go";
      if (zOffset != 0) {
        result += " ";
        result += z.name;
        result += printDeciMicrons(stepsToDu(&z, zOffset), 2);
      }
      if (xOffset != 0) {
        result += " ";
        result += x.name;
        result += printDeciMicrons(stepsToDu(&x, xOffset), 2);
      }
      result += "?";
    } else if (mode == MODE_SLOT && isOn && dupr == 0 && !inNumpad) {
      result = "Set pitch";
    } else if (isOn && numpadResult == 0) {
      result = "Pass " + String(opIndex) + " of " + String(max(opIndex, long(turnPasses * starts)));
    }
  } else if (mode == MODE_CONE) {
    if (numpadResult != 0 && setupIndex == 1) result = "Use ratio " + String(numpadToConeRatio(), 5) + "?";
    else if (!isOn && setupIndex == 1) result = "Use ratio " + printNoTrailing0(coneRatio) + "?";
    else if (!isOn && setupIndex == 2) result = auxForward ? "External?" : "Internal?";
    else if (!isOn && setupIndex == 3) result = "Go?";
    else if (isOn && numpadResult == 0) result = "Cone ratio " + printNoTrailing0(coneRatio);
  } else if (mode == MODE_JOYSTICK && !inNumpad) {
    String directionText = joystickLatheDirectionText(joystickLatheDirectionZ, joystickLatheDirectionX);
    if (!JOYSTICK_ENABLED) result = "Joystick disabled";
    else if (!joystickAvailable) result = joystickStartupWarning.length() > 0 ? joystickStartupWarning : "Joystick not connected";
    else if (joystickLatheRapid && directionText != "") {
      result = "Rapid ";
      result += directionText;
    } else if (spindlePosSync != 0 && directionText != "") {
      result = "Sync ";
      result += directionText;
    } else if (pitchStatusDirection > 0) result = "Pitch +";
    else if (pitchStatusDirection < 0) result = "Pitch -";
    else if (!isOn && directionText != "") {
      result = "Jog ";
      result += directionText;
    } else if (dupr == 0) result = "Set pitch";
    else if (!isOn) result = "Feed off";
    else if (directionText != "") {
      result = "Feed ";
      result += directionText;
    } else result = "Feed neutral";
  }

  if (inNumpad && result == "") result = "Use " + printDupr(numpadToDeciMicrons()) + "?";

  if (result == "" && (millis() - wifiStatusMillis < 7000 || !x.active || x.disabled)) result = wifiStatus;

  if (result == "" && x.active && !x.disabled) result = "Diameter " + printDeciMicrons(abs(2 * getAxisPosDu(&x)), 2);

  return result;
}

void updateDisplay() {
  if (tftUploadActive) return;
  if (millis() - lastDisplayUpdateTime < getDisplayUpdateIntervalMs()) return;
  lastDisplayUpdateTime = millis();
  bool publishStatus = webBuffersReady && webUiIsConnected();

  long newHashLine0 = isOn + spindlePosSync + mode + measure + dupr + starts;
  bool updateLine0 = lcdHashLine0 != newHashLine0;
  String statusText = "";
  String modeText = "";
  String pitchText = "";
  String measureText = "";
  if (updateLine0 || publishStatus) {
    statusText = spindlePosSync ? "SYN" : (isOn ? "ON" : "OFF");
    modeText = printMode();
    pitchText = printDupr(dupr);
    if (starts != 1) pitchText += " x" + String(starts);
    measureText = measure == MEASURE_INCH ? "IN" : measure == MEASURE_METRIC ? "MM" : "TPI";
  }
  if (updateLine0) {
    lcdHashLine0 = newHashLine0;
    setText("bStatus", statusText);
    setText("bMode", modeText);
    setText("tPitch", pitchText);
    setText("bMeasure", measureText);
  }

  int rpm = getApproxRpm();
  long newHashLine1 = moveStep + rpm + spindlePos + measure;
  bool updateLine1 = lcdHashLine1 != newHashLine1;
  String stepText = "";
  String rpmText = "";
  String turnsText = "";
  String angleText = "";
  if (updateLine1 || publishStatus) {
    stepText = printDeciMicrons(moveStep, 5);
    rpmText = String(rpm);
    float turns = (float) abs(spindlePos) / ENCODER_STEPS_INT;
    turnsText = String(turns, turns < 100 ? 2 : (turns < 1000 ? 1 : 0));
    angleText = String(spindleModulo(spindlePos) * 360 / ENCODER_STEPS_FLOAT, 2) + String(char(223));
  }
  if (updateLine1) {
    lcdHashLine1 = newHashLine1;
    setText("tStepVal", stepText);
    setText("tRPMVal", rpmText);
    setText("tTurnsVal", turnsText);
    setText("tAngleVal", angleText);
  }

  long newHashLine2 =
    x.pos + x.originPos + x.disabled + x.leftStop - x.rightStop +
    z.pos + z.originPos + z.disabled + z.leftStop - z.rightStop +
    y.pos + y.originPos + y.disabled + y.leftStop - y.rightStop + measure + x.pos % 100 +
    (xDisplayModeDRO ? (long)(xScale.getRawPosition() * 1000) : 0) +
    (zDisplayModeDRO ? (long)(zScale.getRawPosition() * 1000) : 0) +
    isDiameterMode + isAbsoluteMode + xDisplayModeDRO + zDisplayModeDRO;
  bool updateLine2 = lcdHashLine2 != newHashLine2;
  String xText = "";
  String xLeftText = "";
  String xRightText = "";
  String yText = "";
  String yLeftText = "";
  String yRightText = "";
  String zText = "";
  String zLeftText = "";
  String zRightText = "";
  if (updateLine2 || publishStatus) {
    if (xDisplayModeDRO) {
      float xVal = xScale.getPosition();
      if (isDiameterMode) xVal *= 2.0;
      xText = !x.active || x.disabled ? "" : (measure != MEASURE_METRIC ? String(xVal / 25.4, 3) : String(xVal, 2));
    } else {
      xText = !x.active || x.disabled ? "" : printAxisPos(&x);
    }
    xLeftText = !x.active || x.disabled ? "" : printDistanceToLeftStop(&x);
    xRightText = !x.active || x.disabled ? "" : printDistanceToRightStop(&x);
    yText = !y.active || y.disabled ? "" : printAxisPos(&y);
    yLeftText = !y.active || y.disabled ? "" : printDistanceToLeftStop(&y);
    yRightText = !y.active || y.disabled ? "" : printDistanceToRightStop(&y);
    if (zDisplayModeDRO) {
      float zVal = zScale.getPosition();
      zText = !z.active || z.disabled ? "" : (measure != MEASURE_METRIC ? String(zVal / 25.4, 3) : String(zVal, 2));
    } else {
      zText = !z.active || z.disabled ? "" : printAxisPos(&z);
    }
    zLeftText = !z.active || z.disabled ? "" : printDistanceToLeftStop(&z);
    zRightText = !z.active || z.disabled ? "" : printDistanceToRightStop(&z);
  }
  if (updateLine2) {
    lcdHashLine2 = newHashLine2;
    setText("tX", xText);
    if (xDisplayModeDRO) {
      toScreen("tX.pco=" + String(COLOR_DRO_ON));
    } else {
      toScreen("tX.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tXUp", xLeftText);
    setText("tXDown", xRightText);
    setText("tY", yText);
    setText("tYUp", yLeftText);
    setText("tYDown", yRightText);
    setText("tZ", zText);
    if (zDisplayModeDRO) {
      toScreen("tZ.pco=" + String(COLOR_DRO_ON));
    } else {
      toScreen("tZ.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tZLeft", zLeftText);
    setText("tZRight", zRightText);

    if (isDiameterMode) {
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

    if (!isAbsoluteMode) {
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

  long numpadResult = getNumpadResult();
  long gcodeCommandHash = 0;
  for (int i = 0; i < gcodeCommand.length(); i++) {
    gcodeCommandHash += gcodeCommand.charAt(i);
  }
  for (int i = 0; i < wifiStatus.length(); i++) {
    gcodeCommandHash += wifiStatus.charAt(i);
  }
  bool spindleStopped = micros() > spindleEncTime + 100000;
  int pitchStatusDirection = getJoystickPitchStatusDirection();
  long newHashLine3 = z.pos + (showAngle ? spindlePos : -1) + (showTacho ? rpm : -2) + measure + (numpadResult > 0 ? numpadResult : -1) + mode * 5 + dupr +
      (mode == MODE_CONE ? round(coneRatio * 10000) : 0) + turnPasses + opIndex + setupIndex + gcodeProgramIndex + gcodeProgramCount + spindleStopped * 3 + (isOn ? 139 : -117) + (inNumpad ? 10 : 0) + (auxForward ? 17 : -31) +
      (z.leftStop == LONG_MAX ? 123 : z.leftStop) + (z.rightStop == LONG_MIN ? 1234 : z.rightStop) +
      (x.leftStop == LONG_MAX ? 1235 : x.leftStop) + (x.rightStop == LONG_MIN ? 123456 : x.rightStop) + gcodeCommandHash +
      (mode == MODE_Y ? y.pos + y.originPos + (y.leftStop == LONG_MAX ? 123 : y.leftStop) + (y.rightStop == LONG_MIN ? 1234 : y.rightStop) + y.disabled : 0) +
      pitchStatusDirection * 149 +
      (mode == MODE_JOYSTICK ? joystickLatheDirectionZ * 97 + joystickLatheDirectionX * 101 + joystickLatheFeedSignZ * 131 + joystickLatheFeedSignX * 137 + joystickLatheRapid * 17 + JOYSTICK_ENABLED * 19 + spindlePosSync * 151 : 0) + x.pos + x.originPos + z.pos;
  bool updateLine3 = lcdHashLine3 != newHashLine3;
  String messageText = "";
  if (updateLine3 || publishStatus) {
    messageText = buildDisplayMessage(rpm, numpadResult, spindleStopped, pitchStatusDirection);
  }
  if (updateLine3) {
    lcdHashLine3 = newHashLine3;
    setText("t3", messageText);
  }

  if (publishStatus) {
    publishMachineStatus(statusText, modeText, pitchText, measureText, stepText, rpmText, turnsText, angleText,
      xText, xLeftText, xRightText, yText, yLeftText, yRightText, zText, zLeftText, zRightText, messageText);
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
