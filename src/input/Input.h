#pragma once

#include <Arduino.h>
#include <PS2KeyAdvanced.h>
#include "Config.h"
#include "motion/kernel/AxisId.h"

// Keypad state
extern unsigned long keypadTimeUs;
extern PS2KeyAdvanced keyboard;

// Numpad state
extern bool inNumpad;
extern int numpadDigits[20];
extern int numpadIndex;

long getNumpadResult();
void resetNumpad();
long numpadToDeciMicrons();
float numpadToConeRatio();

// Joystick State
struct JoystickAxisState {
  AxisId axisId;
  int pin;
  bool* enabled;
  bool* invert;
  volatile int manualDirection;
  volatile long manualSpeed;
  volatile int queuedPulses;
  int center;
  int filtered;
  float pulseFraction;
  float deflection;
  int direction;
};

enum JoystickAxisIndex {
  JOYSTICK_AXIS_Z = 0,
  JOYSTICK_AXIS_X = 1
};


void initJoystick();
bool joystickUsable();

void adjustPitch(bool plus);
void buttonPlusMinusPress(bool plus);
void buttonOnOffPress(bool on);
void processKeypadEvent();
void taskKeypad(void *param);
void taskJoystick(void *param);
