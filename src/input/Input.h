#pragma once

#include <Arduino.h>
#include <PS2KeyAdvanced.h>
#include "Config.h"
#include "Axis.h"

// Keypad & button states
extern bool buttonLeftPressed;
extern bool buttonRightPressed;
extern bool buttonUpPressed;
extern bool buttonDownPressed;
extern bool buttonOffPressed;
extern bool buttonBackPressed;
extern bool buttonForwardPressed;

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

// Spindle timing
extern unsigned long spindleEncTime;
extern unsigned long spindleEncTimeDiffBulk;
extern unsigned long spindleEncTimeAtIndex0;
extern int spindleEncTimeIndex;

long spindleModulo(long value);
void processSpindleCounter();

// Dev spindle encoder simulation (set to 0 to compile it out of release builds).
#ifndef ENABLE_SPINDLE_SIM
#define ENABLE_SPINDLE_SIM 1
#endif
#ifndef SPINDLE_SIM_TIMEOUT_MS
#define SPINDLE_SIM_TIMEOUT_MS 2000  // heartbeat watchdog: revert to real encoder if silent
#endif
#if ENABLE_SPINDLE_SIM
void spindleSimSet(bool active, float rpm);
void spindleSimTurnDeg(float deg);
bool spindleSimIsActive();
float spindleSimGetRpm();
#endif

// Joystick State
struct JoystickAxisState {
  Axis* axis;
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

struct JoystickConfig {
  bool enabled;
  bool zEnabled;
  bool xEnabled;
  bool buttonTogglesOnOff;
  int centerSamples;
  int oversamples;
  int sampleIntervalMs;
  int adcMax;
  int deadband;
  int pulseQueueLimit;
  float normalRps;
  float rapidRps;
  bool invertZ;
  bool invertX;
  bool invertButton;
};

extern JoystickConfig joystickConfig;
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
void cancelJoystickLatheSync();
void resetJoystickLatheFeedPosition();
void resetJoystickLatheFeed();
void prepareJoystickLatheManualMove();
bool isJoystickManualMove(Axis* a);
bool isGearboxManualMove(Axis* a);
bool isContinuousStep();
bool isRapidManualMove();
bool manualMovesAllowedWhenOn();
bool manualMovesIgnoredWhenOn();
int getJoystickManualDirection(Axis* a);
long getJoystickManualSpeed(Axis* a);
JoystickAxisState* getJoystickAxisState(Axis* a);
volatile int* getJoystickPulseQueue(Axis* a);
int getAndResetJoystickPulses(Axis* a);
void addJoystickPulses(Axis* a, int delta);
int getJoystickPitchStatusDirection();

void adjustPitch(bool plus);
void buttonPlusMinusPress(bool plus);
void buttonOnOffPress(bool on);
void processKeypadEvent();
void taskKeypad(void *param);
void taskJoystick(void *param);
