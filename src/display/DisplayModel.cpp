#include "DisplayModel.h"
#include "Config.h"
#include "Input.h"
#include "WebServerModule.h"
#include "SettingsService.h"
#include "motion/DRO_Scale.h"
#include "motion/kernel/MotionEngine.h"

bool spindleValColorGreen = false;
int shownRpm = 0;
unsigned long shownRpmTime = 0;

static bool isPassMode(LatheMode mode) {
  return mode == LatheMode::TURN || mode == LatheMode::FACE || mode == LatheMode::CUT || mode == LatheMode::THREAD || mode == LatheMode::ELLIPSE || mode == LatheMode::SLOT;
}

static int getLastSetupIndex(LatheMode mode) {
  if (mode == LatheMode::CONE || mode == LatheMode::GCODE) return 2;
  if (mode == LatheMode::THREAD) return 4;
  return 3;
}

// --- Position & Stop Distance Resolution in Millimeters ---
float getAxisPositionMm(AxisId a) {
  float mm = 0.0f;
  if (a == AXIS_X && SettingsService::isXDroMode()) {
    mm = xScale.getPosition();
  } else if (a == AXIS_Z && SettingsService::isZDroMode()) {
    mm = zScale.getPosition();
  } else {
    mm = MotionEngine::instance().getAxisPositionDu(a) / 10000.0f;
  }

  if (a == AXIS_X && SettingsService::isDiameterMode()) {
    mm *= 2.0f;
  }
  return mm;
}

float getAxisStopDistanceMm(AxisId a, bool left) {
  const SoftLimitTable& limits = MotionEngine::instance().getSoftLimits(a);
  const SoftLimit* lim = left ? limits.getLeftLimit() : limits.getRightLimit();
  if (!lim || !lim->isEnforced()) return 0.0f;
  long currentDu = MotionEngine::instance().getAxisPositionDu(a);
  long distDu = left ? (lim->coordDu - currentDu) : (currentDu - lim->coordDu);
  return distDu / 10000.0f;
}

// --- Precision Formatting Helpers ---
String formatPosition(float mm, int decimals) {
  return String(mmToDisplay(mm), decimals);
}

String formatStopDistance(AxisId a, bool left) {
  const SoftLimitTable& limits = MotionEngine::instance().getSoftLimits(a);
  const SoftLimit* lim = left ? limits.getLeftLimit() : limits.getRightLimit();
  if (!lim || !lim->isEnforced()) return "";
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
  bool imperial = SettingsService::getMeasure() != MeasureUnit::METRIC;
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
  if (SettingsService::getMeasure() == MeasureUnit::INCH) {
    return String(value / 254000.0, PITCH_INCH_DECIMAL_PLACES);
  }
  if (SettingsService::getMeasure() != MeasureUnit::TPI || value == 0) return printDeciMicrons(value, 5);

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
  LatheMode m = static_cast<LatheMode>(SettingsService::getMode());
  if (m == LatheMode::NORMAL) return "GEAR";
  if (m == LatheMode::XGEAR) return "XGEAR";
  if (m == LatheMode::ASYNC) return "ASYNC";
  if (m == LatheMode::CONE) return "CONE";
  if (m == LatheMode::TURN) return "TURN";
  if (m == LatheMode::FACE) return "FACE";
  if (m == LatheMode::CUT) return "CUT";
  if (m == LatheMode::SLOT) return "SLOT";
  if (m == LatheMode::THREAD) return "THREAD";
  if (m == LatheMode::ELLIPSE) return "ELLIP";
  if (m == LatheMode::GCODE) return "GCODE";
  if (m == LatheMode::JOYSTICK) return "JOY";
  return "";
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
  return spindlePosSync ? "SYN" : (SettingsService::isOn() ? "ON" : "OFF");
}

String getDisplayModeText() {
  return printMode();
}

String getDisplayMeasureText() {
  MeasureUnit m = SettingsService::getMeasure();
  return m == MeasureUnit::INCH ? "IN" : m == MeasureUnit::METRIC ? "MM" : "TPI";
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
// --- Contextual Message Helper ---
String buildDisplayMessage(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection) {
  String result = "";
  LatheMode m = static_cast<LatheMode>(SettingsService::getMode());
  long duprVal = SettingsService::getThreadSetting().dupr;
  bool isMachineOn = SettingsService::isOn();

  if (m != LatheMode::JOYSTICK && pitchStatusDirection > 0) {
    result = "Pitch +";
  } else if (m != LatheMode::JOYSTICK && pitchStatusDirection < 0) {
    result = "Pitch -";
  } else if (m == LatheMode::JOYSTICK && !inNumpad) {
    String directionText = joystickLatheDirectionText(joystickLatheDirectionZ, joystickLatheDirectionX);
    if (!sysConfig.joystick.enabled) result = "Joystick disabled";
    else if (!joystickAvailable) result = joystickStartupWarning.length() > 0 ? joystickStartupWarning : "Joystick not connected";
    else if (joystickLatheRapid && directionText != "") {
      result = "Rapid " + directionText;
    } else if (spindlePosSync != 0 && directionText != "") {
      result = "Sync " + directionText;
    } else if (pitchStatusDirection > 0) result = "Pitch +";
    else if (pitchStatusDirection < 0) result = "Pitch -";
    else if (!isMachineOn && directionText != "") {
      result = "Jog " + directionText;
    } else if (duprVal == 0) result = "Set pitch";
    else if (!isMachineOn) result = "Feed off";
    else if (directionText != "") {
      result = "Feed " + directionText;
    } else result = "Feed neutral";
  } else {
    if (duprVal == 0) result = "Set pitch";
    else if (!isMachineOn) result = "Feed off";
    else result = "Feed on";
  }

  if (inNumpad && result == "") result = "Use " + printDupr(numpadToDeciMicrons()) + "?";

  if (result == "" && (millis() - wifiStatusMillis < 7000)) result = wifiStatus;

  if (result == "") {
    float xPosMm = abs(getAxisPositionMm(AXIS_X));
    if (!SettingsService::isDiameterMode()) xPosMm *= 2.0f;
    result = "Diameter " + formatPosition(xPosMm, POSITION_DECIMAL_PLACES);
  }

  return result;
}

// --- Complete Display Snapshot Generator ---
DisplaySnapshot getDisplaySnapshot(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection) {
  DisplaySnapshot s = {};

  // Primary Coordinates (Fixed 3 decimals)
  s.xPos = formatPosition(getAxisPositionMm(AXIS_X));
  s.yPos = "";
  s.zPos = formatPosition(getAxisPositionMm(AXIS_Z));

  // Soft Stop Distances (Fixed 3 decimals, or "" if not active)
  s.xStopUp = formatStopDistance(AXIS_X, true);
  s.xStopDown = formatStopDistance(AXIS_X, false);
  s.yStopUp = "";
  s.yStopDown = "";
  s.zStopLeft = formatStopDistance(AXIS_Z, true);
  s.zStopRight = formatStopDistance(AXIS_Z, false);

  // Status & Mode Labels
  s.status = getDisplayStatusText();
  s.mode = getDisplayModeText();
  s.measure = getDisplayMeasureText();
  s.pitch = formatPitch(SettingsService::getThreadSetting().dupr);
  if (SettingsService::getStarts() != 1) s.pitch += " x" + String(SettingsService::getStarts());
  s.step = formatJogStep(SettingsService::getJogStepDu());

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
