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


extern JoystickAxisState joystickAxes[];
extern const int JOYSTICK_AXIS_COUNT;

extern portMUX_TYPE joystickPulseMux;
extern volatile bool joystickRapidPressed;
extern volatile int joystickLatheDirectionZ;
extern volatile int joystickLatheDirectionX;
extern volatile bool joystickLatheRapid;
extern volatile int joystickPitchAdjustDirection;
extern volatile int joystickPitchStatusDirection;
extern volatile unsigned long joystickPitchStatusMillis;
extern bool joystickAvailable;
extern float joystickPitchChangeFraction;
extern unsigned long joystickSampleTimeUs;
extern int joystickLatheFeedSignZ;
extern int joystickLatheFeedSignX;
extern long joystickLathePitchZ;
extern long joystickLathePitchX;
extern bool joystickLatheThreadLocked;
extern int joystickLatheSyncAxis;
extern long joystickLatheSyncPitch;
extern bool joystickLatheRebaseZAfterSync;
extern bool joystickLatheRebaseXAfterSync;
extern bool joystickButtonRawPressed;
extern bool joystickButtonPressed;
extern bool joystickButtonToggleBlocked;
extern unsigned long joystickButtonRawChangeMillis;
extern unsigned long joystickButtonPressMillis;
extern String joystickStartupWarning;

void initJoystick();
bool joystickUsable();
bool joystickAxisEnabled(JoystickAxisState* joystickAxis);
bool anyJoystickAxisEnabled();
bool joystickLatheFeedCommandActive();
void resetJoystickLatheFeedPosition();
void resetJoystickLatheFeed();
void prepareJoystickLatheManualMove();
bool isContinuousStep();
bool isRapidManualMove();
bool manualMovesAllowedWhenOn();
bool manualMovesIgnoredWhenOn();
int getJoystickPitchStatusDirection();

void adjustPitch(bool plus);
void buttonPlusMinusPress(bool plus);
void buttonOnOffPress(bool on);
void processKeypadEvent();
void taskKeypad(void *param);
void taskJoystick(void *param);
