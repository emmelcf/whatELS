#include "DisplayModel.h"
#include "Config.h"
#include "Axis.h"
#include "Input.h"
#include "Motion.h"
#include "WebServerModule.h"
#include "SettingsService.h"

bool spindleValColorGreen = false;

// --- Position & Stop Distance Resolution in Millimeters ---
float getAxisPositionMm(Axis* a) {
  float mm = 0.0f;
  if (a == &x && SettingsService::isXDroMode()) {
    mm = xScale.getPosition();
  } else if (a == &z && SettingsService::isZDroMode()) {
    mm = zScale.getPosition();
  } else {
    mm = getAxisPosDu(a) / 10000.0f;
  }

  if (a == &x && SettingsService::isDiameterMode()) {
    mm *= 2.0f;
  }
  return mm;
}

float getAxisStopDistanceMm(Axis* a, bool left) {
  long du = left ? getAxisLeftStopDistanceDu(a) : getAxisRightStopDistanceDu(a);
  return du / 10000.0f;
}

// --- Precision Formatting Helpers ---
String formatPosition(float mm, int decimals) {
  return String(mmToDisplay(mm), decimals);
}

String formatStopDistance(Axis* a, bool left) {
  if (left && a->leftStop == LONG_MAX) return "";
  if (!left && a->rightStop == LONG_MIN) return "";
  if (a->rotational) {
    long du = left ? getAxisLeftStopDistanceDu(a) : getAxisRightStopDistanceDu(a);
    return formatDegrees(du);
  }
  return formatPosition(getAxisStopDistanceMm(a, left), POSITION_DECIMAL_PLACES);
}

String formatJogStep(long stepDu) {
  return printDeciMicrons(stepDu, 5);
}

String formatPitch(long valueDu) {
  return printDupr(valueDu);
}

String formatDegrees(long degrees10000) {
  return printDegrees(degrees10000);
}

String formatNoTrailing0(float value) {
  return printNoTrailing0(value);
}

// --- Legacy Print Functions (Centralized) ---
String printDeciMicrons(long deciMicrons, int precisionPointsMax) {
  if (deciMicrons == 0) {
    return "0";
  }
  bool imperial = measure != MeasureUnit::METRIC;
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
  if (measure == MeasureUnit::INCH) {
    return String(value / 254000.0, PITCH_INCH_DECIMAL_PLACES);
  }
  if (measure != MeasureUnit::TPI || value == 0) return printDeciMicrons(value, 5);

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
  if (mode == LatheMode::NORMAL) return "GEAR";
  if (mode == LatheMode::XGEAR) return "XGEAR";
  if (mode == LatheMode::ASYNC) return "ASYNC";
  if (mode == LatheMode::CONE) return "CONE";
  if (mode == LatheMode::TURN) return "TURN";
  if (mode == LatheMode::FACE) return "FACE";
  if (mode == LatheMode::CUT) return "CUT";
  if (mode == LatheMode::SLOT) return "SLOT";
  if (mode == LatheMode::THREAD) return "THREAD";
  if (mode == LatheMode::ELLIPSE) return "ELLIP";
  if (mode == LatheMode::GCODE) return "GCODE";
  if (mode == LatheMode::JOYSTICK) return "JOY";
  return "";
}

String printDistanceToLeftStop(Axis* a) {
  return formatStopDistance(a, true);
}

String printDistanceToRightStop(Axis* a) {
  return formatStopDistance(a, false);
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

// --- State and Mode Display Strings ---
String getDisplayStatusText() {
  return spindlePosSync ? "SYN" : (isOn ? "ON" : "OFF");
}

String getDisplayModeText() {
  return printMode();
}

String getDisplayMeasureText() {
  return measure == MeasureUnit::INCH ? "IN" : measure == MeasureUnit::METRIC ? "MM" : "TPI";
}

// --- Spindle Formatted Values ---
int getApproxRpm() {
  unsigned long t = micros();
  unsigned long elapsedTime = t - spindleEncTime;
  if (elapsedTime > 50000) {
    // RPM less than 10.
    spindleEncTimeDiffBulk = 0;
    shownRpm = 0;
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

String getDisplayRpmText(int rpm) {
  return String(rpm);
}

String getDisplayTurnsText() {
  float turns = (float) abs(spindlePos) / ENCODER_STEPS_INT;
  return String(turns, turns < 100 ? 2 : (turns < 1000 ? 1 : 0));
}

String getDisplayAngleText() {
  return String(spindleModulo(spindlePos) * 360 / ENCODER_STEPS_FLOAT, 2) + String(char(223));
}

String getDisplaySpindleValue(int rpm) {
  switch (SettingsService::getSpindleDisplayMode()) {
    case 0: return String(rpm);
    case 1: {
      float turns = (float) abs(spindlePos) / ENCODER_STEPS_INT;
      return String(turns, turns < 100 ? 2 : (turns < 1000 ? 1 : 0));
    }
    case 2:
      return String(spindleModulo(spindlePos) * 360 / ENCODER_STEPS_FLOAT, 1);
    default:
      return "";
  }
}

// --- Contextual Message Helper ---
String buildDisplayMessage(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection) {
  String result = "";
  if (mode != LatheMode::JOYSTICK && pitchStatusDirection > 0) {
    result = "Pitch +";
  } else if (mode != LatheMode::JOYSTICK && pitchStatusDirection < 0) {
    result = "Pitch -";
  } else if (mode == LatheMode::GCODE) {
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
      if (mode == LatheMode::FACE) {
        result = auxForward ? "Right to left?" : "Left to right?";
      } else if (mode == LatheMode::CUT) {
        result = dupr >= 0 ? "Pitch > 0, external" : "Pitch < 0, internal";
      } else {
        result = auxForward ? "External?" : "Internal?";
      }
    } else if (mode == LatheMode::THREAD && !isOn && setupIndex == 3) {
      result = "Cone ratio " + String(numpadToConeRatio(), 5) + "?";
    } else if (!isOn && setupIndex == getLastSetupIndex()) {
      long zOffset = getPassModeZStart() - z.pos;
      long xOffset = getPassModeXStart() - x.pos;
      result = "Go";
      if (zOffset != 0) {
        result += " ";
        result += z.name;
        result += formatPosition(stepsToDu(&z, zOffset) / 10000.0f, 2);
      }
      if (xOffset != 0) {
        result += " ";
        result += x.name;
        result += formatPosition(stepsToDu(&x, xOffset) / 10000.0f, 2);
      }
      result += "?";
    } else if (mode == LatheMode::SLOT && isOn && dupr == 0 && !inNumpad) {
      result = "Set pitch";
    } else if (isOn && numpadResult == 0) {
      result = "Pass " + String(opIndex) + " of " + String(max(opIndex, long(turnPasses * starts)));
    }
  } else if (mode == LatheMode::CONE) {
    if (numpadResult != 0 && setupIndex == 1) result = "Use ratio " + String(numpadToConeRatio(), 5) + "?";
    else if (!isOn && setupIndex == 1) result = "Use ratio " + printNoTrailing0(coneRatio) + "?";
    else if (!isOn && setupIndex == 2) result = auxForward ? "External?" : "Internal?";
    else if (!isOn && setupIndex == 3) result = "Go?";
    else if (isOn && numpadResult == 0) result = "Cone ratio " + printNoTrailing0(coneRatio);
  } else if (mode == LatheMode::JOYSTICK && !inNumpad) {
    String directionText = joystickLatheDirectionText(joystickLatheDirectionZ, joystickLatheDirectionX);
    if (!sysConfig.joystick.enabled) result = "Joystick disabled";
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

  if (result == "" && x.active && !x.disabled) {
    float xPosMm = abs(getAxisPositionMm(&x));
    // If not in diameter mode, double it for diameter display
    if (!SettingsService::isDiameterMode()) xPosMm *= 2.0f;
    result = "Diameter " + formatPosition(xPosMm, POSITION_DECIMAL_PLACES);
  }

  return result;
}

// --- Complete Display Snapshot Generator ---
DisplaySnapshot getDisplaySnapshot(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection) {
  DisplaySnapshot s = {};

  // Primary Coordinates (Fixed 3 decimals)
  s.xPos = (!x.active || x.disabled) ? "" : formatPosition(getAxisPositionMm(&x));
  s.yPos = "";
  s.zPos = (!z.active || z.disabled) ? "" : formatPosition(getAxisPositionMm(&z));

  // Soft Stop Distances (Fixed 3 decimals, or "" if not active)
  s.xStopUp = (!x.active || x.disabled) ? "" : formatStopDistance(&x, true);
  s.xStopDown = (!x.active || x.disabled) ? "" : formatStopDistance(&x, false);
  s.yStopUp = "";
  s.yStopDown = "";
  s.zStopLeft = (!z.active || z.disabled) ? "" : formatStopDistance(&z, true);
  s.zStopRight = (!z.active || z.disabled) ? "" : formatStopDistance(&z, false);

  // Status & Mode Labels
  s.status = getDisplayStatusText();
  s.mode = getDisplayModeText();
  s.measure = getDisplayMeasureText();
  s.pitch = formatPitch(dupr);
  if (starts != 1) s.pitch += " x" + String(starts);
  s.step = formatJogStep(moveStep);

  // Spindle Readouts
  s.rpm = rpm;
  s.rpmText = getDisplayRpmText(rpm);
  s.turnsText = getDisplayTurnsText();
  s.angleText = getDisplayAngleText();
  switch (SettingsService::getSpindleDisplayMode()) {
    case 0: s.spindleLabel = "RPM"; break;
    case 1: s.spindleLabel = "Turns"; break;
    case 2: s.spindleLabel = "Angle"; break;
    default: s.spindleLabel = ""; break;
  }
  s.spindleValue = getDisplaySpindleValue(rpm);

  // Contextual Message
  s.message = buildDisplayMessage(rpm, numpadResult, spindleStopped, pitchStatusDirection);

  // UI Indicators
  s.xIsDroActive = SettingsService::isXDroMode();
  s.zIsDroActive = SettingsService::isZDroMode();
  s.isDiameterMode = SettingsService::isDiameterMode();
  s.isAbsoluteMode = SettingsService::isAbsoluteMode();
  s.spindleValColorGreen = spindleValColorGreen;

  return s;
}
