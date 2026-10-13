#pragma once

#include <Arduino.h>
#include "Config.h"
#include "Axis.h"
#include "DisplayModel.h"

// LCD Hash Tracking
extern long lcdHashLine0;
extern long lcdHashLine1;
extern long lcdHashLine2;
extern long lcdHashLine3;
extern unsigned long saveTime;

void writeNextionCommandRaw(const String &command);
void toScreen(const String &command);
void setText(const String &id, const String &text);
void beep();

String printDeciMicrons(long deciMicrons, int precisionPointsMax);
String printDegrees(long degrees10000);
String printDupr(long value);
String printMode();
// String printAxisPos(Axis* a);
String printDistanceToLeftStop(Axis* a);
String printDistanceToRightStop(Axis* a);
String printNoTrailing0(float value);
String joystickLatheDirectionText(int zDirection, int xDirection);

int getApproxRpm();
unsigned long getDisplayUpdateIntervalMs();
String buildDisplayMessage(int rpm, long numpadResult, bool spindleStopped, int pitchStatusDirection);

void updateDisplay();
bool saveIfChanged();
void taskDisplay(void *param);
