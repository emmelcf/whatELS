#pragma once

#include <Arduino.h>
#include <driver/pulse_cnt.h>
#include "Config.h"
#include "DRO_Scale.h"

enum PulseCounter {
  PULSE_COUNTER_SPINDLE,
  PULSE_COUNTER_Z,
  PULSE_COUNTER_X,
  PULSE_COUNTER_COUNT
};

struct Axis {
  SemaphoreHandle_t mutex = nullptr;

  char name = '\0';
  bool active = false;
  bool rotational = false;
  float motorSteps = 0.0f;
  float screwPitch = 0.0f;
  float pulsePerRevolution = 0.0f;

  long pos = 0;
  long savedPos = 0;
  float fractionalPos = 0.0f;
  long originPos = 0;
  long savedOriginPos = 0;
  long posGlobal = 0;
  long savedPosGlobal = 0;
  int pendingPos = 0;
  long motorPos = 0;
  long savedMotorPos = 0;
  bool continuous = false;

  long leftStop = 0;
  long savedLeftStop = 0;
  long nextLeftStop = 0;
  bool nextLeftStopFlag = false;

  long rightStop = 0;
  long savedRightStop = 0;
  long nextRightStop = 0;
  bool nextRightStopFlag = false;

  long speed = 0;
  long speedStart = 0;
  long speedMax = 0;
  long speedManualMove = 0;
  long acceleration = 0;
  long decelerateSteps = 0;

  bool direction = false;
  bool directionInitialized = false;
  unsigned long stepStartUs = 0;
  int stepperEnableCounter = 0;
  bool disabled = false;
  bool savedDisabled = false;

  bool invertStepper = false;
  bool invertEnable = false;
  bool needsRest = false;
  bool movingManually = false;
  long estopSteps = 0;
  long backlashSteps = 0;
  long gcodeRelativePos = 0;

  int ena = 0;
  int dir = 0;
  int step = 0;

  int pulseA = 0;
  int pulseB = 0;
  int pulseCount = 0;
  long pulsePos = 0;
  long pulsePosAvg = 0;
  PulseCounter pulseCounter = PULSE_COUNTER_SPINDLE;
};

// Global Axes
extern Axis z;
extern Axis x;

// DRO Scales
extern DRO_Scale xScale;
extern DRO_Scale zScale;

// Pulse counter handles
extern pcnt_unit_handle_t pulseUnits[PULSE_COUNTER_COUNT];
extern volatile bool pulseCountersReady;

// Core motion and machine state
extern SemaphoreHandle_t motionMutex;
extern hw_timer_t *async_timer;
extern EstopCode emergencyStop;

extern bool isOn;
extern bool nextIsOn;
extern bool nextIsOnFlag;

extern long dupr;
extern long nextDupr;
extern bool nextDuprFlag;

extern int starts;
extern int nextStarts;
extern bool nextStartsFlag;

extern volatile LatheMode mode;
extern LatheMode nextMode;
extern bool nextModeFlag;

extern MeasureUnit measure;

extern long moveStep;

extern float coneRatio;
extern float nextConeRatio;
extern bool nextConeRatioFlag;

extern int turnPasses;

extern bool auxForward;

extern long spindlePos;
extern long spindlePosAvg;
extern long savedSpindlePos;
extern long savedSpindlePosAvg;
extern int spindlePosSync;
extern int savedSpindlePosSync;
extern long spindlePosGlobal;
extern long savedSpindlePosGlobal;
extern int spindleCount;

extern bool showAngle;
extern bool showTacho;
extern bool savedShowAngle;
extern bool savedShowTacho;
extern int shownRpm;
extern unsigned long shownRpmTime;

extern bool beepFlag;

struct AxisPins {
  int ena;
  int dir;
  int step;
  int pulseA;
  int pulseB;
  PulseCounter pulseCounter;
};

// Axis Functions
void initAxis(Axis* a, char name, bool active, bool rotational,
              const AxisHardwareConfig& config, float pulsePerRevolution,
              const AxisPins& pins);

void initAxis(Axis* a, char name, bool active, bool rotational, float motorSteps, float screwPitch, float pulsePerRevolution, long speedStart, long speedManualMove,
    long acceleration, bool invertStepper, bool invertEnable, bool needsRest, long maxTravelMm, long backlashDu, int ena, int dir, int step, int pulseA, int pulseB, PulseCounter pulseCounter);

void updateEnable(Axis* a);
void stepperEnable(Axis* a, bool value);
bool stepperIsRunning(Axis* a);

long stepsToDu(Axis* a, long steps);
long duToSteps(Axis* a, long du);
long getAxisPosDu(Axis* a);
long getAxisStopDiffDu(Axis* a);
String printAxisPos(Axis* a);
long getAxisLeftStopDistanceDu(Axis* a);
long getAxisRightStopDistanceDu(Axis* a);

void markAxisOrigin(Axis* a);
void markAxis0(Axis* a);
void markOrigin();
bool markOriginOrEmergencyStop();
void zeroSpindlePos();

void setEmergencyStop(EstopCode kind);
void setIsOnFromTask(bool on);
void setIsOnFromLoop(bool on);

void setDir(Axis* a, bool dir);
bool stepTo(Axis* a, long newPos, bool continuous);
bool stepToContinuous(Axis* a, long newPos);
bool stepToFinal(Axis* a, long newPos);
void moveAxis(Axis* a);

long posFromSpindleWithModePitch(Axis* a, long s, bool respectStops, long pitch);
long posFromSpindle(Axis* a, long s, bool respectStops);
long spindleFromPosWithModePitch(Axis* a, long p, long pitch);
long spindleFromPos(Axis* a, long p);

void waitForPendingPosNear0(Axis* a);
void waitForPendingPos0(Axis* a);
long getManualMoveTargetPos(Axis* a, int sign);
long getMoveStepForAxis(Axis* a);
long getStepMaxSpeed(Axis* a);
long getManualMoveSpeedMax(Axis* a, bool pulseMove);
void waitForStep(Axis* a);
void waitForManualMoveStep(Axis* a, bool pulseMove);
int waitForNextPulseMoveDelta(Axis* a, int sign);

int applyAxisEncoderBacklash(Axis* a, int delta);
int getAndResetPulses(Axis* a);
bool moveGearboxAxisManually(Axis* a, int pulseDelta, int sign);

void setAsyncTimerEnable(bool value);
void updateAsyncTimerSettings();

void startPulseCounter(PulseCounter pulseCounter, int gpioA, int gpioB);
void taskAttachInterrupts(void *param);
void taskMoveZ(void *param);
void taskMoveX(void *param);
