#pragma once

#include <Arduino.h>
#include "Config.h"
#include "../core/SettingsService.h"

// --- Unit Conversion Helpers ---
// Pure conversion from internal metric units to active display units (Metric vs Imperial)
inline float mmToDisplay(float mm) {
  return (SettingsService::getMeasure() == MeasureUnit::INCH) ? (mm / 25.4f) : mm;
}

inline float duToDisplay(long deciMicrons) {
  return (SettingsService::getMeasure() == MeasureUnit::INCH) ? (deciMicrons / 254000.0f) : (deciMicrons / 10000.0f);
}

// --- Position & Stop Distance Resolution in Millimeters ---
// Resolves true position in mm (DRO scale vs. Stepper, plus Diameter mode for X)
float getAxisPositionMm(AxisId a);
float getAxisStopDistanceMm(AxisId a, bool left);

// --- Precision Formatting Helpers ---
const int POSITION_DECIMAL_PLACES = 3;
const int PITCH_INCH_DECIMAL_PLACES = 4;

// Always fixed to 3 decimal points (0.000) for coordinates
String formatPosition(float mm, int decimals = POSITION_DECIMAL_PLACES);

// Format stop distance (fixed 3 decimals, or "" if stop not set)
String formatStopDistance(AxisId a, bool left);

// Formats jog step increments, trimming redundant trailing zeros
String formatJogStep(long stepDu);

// Formats thread pitch / lead (metric mm or TPI)
String formatPitch(long valueDu);

// Formats degrees with degree symbol
String formatDegrees(long degrees10000);

// Formats floats omitting trailing zeros
String formatNoTrailing0(float value);

// --- State and Mode Display Strings ---
String getDisplayStatusText();
String getDisplayModeText();
String getDisplayMeasureText();

// --- Spindle Formatted Values ---
int getApproxRpm();
String getDisplayRpmText(int rpm);
String getDisplayTurnsText();
String getDisplayAngleText();
String getDisplaySpindleValue(int rpm);

// --- Contextual Message Helper ---
String buildDisplayMessage(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection);
String joystickLatheDirectionText(int zDirection, int xDirection);

// Legacy print functions for backward compatibility
String printDeciMicrons(long deciMicrons, int precisionPointsMax);
String printDegrees(long degrees10000);
String printDupr(long value);
String printMode();
String printNoTrailing0(float value);

// --- Complete Display Snapshot Model ---
struct DisplaySnapshot {
  // 1. Primary Coordinate Readouts (Fixed 3 decimal places)
  String xPos;
  String yPos;
  String zPos;

  // 2. Soft Stop Distances (Fixed 3 decimal places, or "" if not active)
  String xStopUp;
  String xStopDown;
  String yStopUp;
  String yStopDown;
  String zStopLeft;
  String zStopRight;

  // 3. Machine State Labels
  String status;
  String mode;
  String measure;
  String pitch;
  String step;

  // 4. Spindle Readouts
  int rpm = 0;
  String rpmText;
  String turnsText;
  String angleText;
  String spindleLabel; // "RPM", "Turns", "Angle"
  String spindleValue; // Value corresponding to active toggle

  // 5. Contextual Message
  String message;

  // 6. UI Indicators / Visual State Flags
  bool xIsDroActive = false;
  bool zIsDroActive = false;
  bool isDiameterMode = false;
  bool isAbsoluteMode = false;
  bool spindleValColorGreen = false;
};

extern bool spindleValColorGreen;

// Generates a complete snapshot of all displayable information
DisplaySnapshot getDisplaySnapshot(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection);
