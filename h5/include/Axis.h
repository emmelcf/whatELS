#pragma once

#include <Arduino.h>
#include <driver/pcnt.h>
#include "Config.h"
#include "DRO_Scale.h"

enum PulseCounter {
  PULSE_COUNTER_SPINDLE,
  PULSE_COUNTER_Z,
  PULSE_COUNTER_X,
  PULSE_COUNTER_Y,
  PULSE_COUNTER_COUNT
};

struct Axis {
  SemaphoreHandle_t mutex;

  char name;
  bool active;
  bool rotational;
  float motorSteps;
  float screwPitch;
  float pulsePerRevolution;

  long pos;
  long savedPos;
  float fractionalPos;
  long originPos;
  long savedOriginPos;
  long posGlobal;
  long savedPosGlobal;
  int pendingPos;
  long motorPos;
  long savedMotorPos;
  bool continuous;

  long leftStop;
  long savedLeftStop;
  long nextLeftStop;
  bool nextLeftStopFlag;

  long rightStop;
  long savedRightStop;
  long nextRightStop;
  bool nextRightStopFlag;

  long speed;
  long speedStart;
  long speedMax;
  long speedManualMove;
  long acceleration;
  long decelerateSteps;

  bool direction;
  bool directionInitialized;
  unsigned long stepStartUs;
  int stepperEnableCounter;
  bool disabled;
  bool savedDisabled;

  bool invertStepper;
  bool invertEnable;
  bool needsRest;
  bool movingManually;
  long estopSteps;
  long backlashSteps;
  long gcodeRelativePos;

  int ena;
  int dir;
  int step;

  int pulseA;
  int pulseB;
  int pulseCount;
  long pulsePos;
  long pulsePosAvg;
  PulseCounter pulseCounter;
};

// Global Axes
extern Axis z;
extern Axis x;
extern Axis y;

// DRO Scales
extern DRO_Scale xScale;
extern DRO_Scale zScale;

// Pulse counter handles
extern pcnt_unit_t pulseUnits[PULSE_COUNTER_COUNT];
extern volatile bool pulseCountersReady;

// Core motion and machine state
extern SemaphoreHandle_t motionMutex;
extern hw_timer_t *async_timer;
extern int emergencyStop;

extern bool isOn;
extern bool nextIsOn;
extern bool nextIsOnFlag;

extern long dupr;
extern long savedDupr;
extern long nextDupr;
extern bool nextDuprFlag;

extern int starts;
extern int savedStarts;
extern int nextStarts;
extern bool nextStartsFlag;

extern volatile int mode;
extern int nextMode;
extern bool nextModeFlag;
extern int savedMode;

extern int measure;
extern int savedMeasure;

extern long moveStep;
extern long savedMoveStep;

extern float coneRatio;
extern float savedConeRatio;
extern float nextConeRatio;
extern bool nextConeRatioFlag;

extern int turnPasses;
extern int savedTurnPasses;

extern bool auxForward;
extern bool savedAuxForward;

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

// Axis Functions
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

void setEmergencyStop(int kind);
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
void taskMoveY(void *param);
