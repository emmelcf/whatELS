#pragma once

#include <Arduino.h>
#include "Config.h"
#include "Axis.h"

// Automation and Operation State
extern long setupIndex;
extern long opIndex;
extern bool opIndexAdvanceFlag;
extern long opSubIndex;
extern int opDuprSign;
extern long opDupr;

// GCode state
extern String gcodeCommand;
extern long gcodeFeedDuPerSec;
extern bool gcodeInitialized;
extern bool gcodeAbsolutePositioning;
extern bool gcodeInBrace;
extern bool gcodeInSemicolon;
extern bool wsInKeycode;
extern int wsKeycode;
extern String keycodeCommand;
extern bool gcodeInSave;
extern bool gcodeInSaveFirstLine;
extern String gcodeSaveName;
extern String gcodeSaveValue;
extern int gcodeProgramIndex;
extern int gcodeProgramCount;
extern String gcodeProgram;
extern int gcodeProgramCharIndex;

Axis* getPitchAxis();
Axis* getAsyncAxis();

bool isPassMode();
bool isGearboxMode();
bool needZStops();
int getLastSetupIndex();
long getPassModeZStart();
long getPassModeXStart();
void resetGcodeState();

void setDupr(long value);
void setStarts(int value);
void setTurnPasses(int value);
void setConeRatio(float value);
long normalizePitch(long pitch);
void applyDuprSignReverse();
void applyDupr();
void applyStarts();
void setLeftStop(Axis* a, long value);
void setRightStop(Axis* a, long value);
void applyLeftStop(Axis* a);
void applyRightStop(Axis* a);
void applyConeRatio();
void applySettings();
void updateAsyncTimerSettings();

void setModeFromLoop(LatheMode m);
void setModeFromTask(LatheMode m);
void setModeFromUi(LatheMode m, bool fromNextion);

void discountFullSpindleTurns();

void modeGearbox(Axis* a);
void modeTurn(Axis* main, Axis* aux);
void modeFace(Axis* main, Axis* aux);
void modeCut();
void modeSlot();
void modeCone();
void modeEllipse(Axis* main, Axis* aux);
void modeJoystick();

// --- Authoritative Operator Intent Models ---
enum ThreadFormat {
  THREAD_METRIC = 0,
  THREAD_TPI    = 1,
  THREAD_INCH   = 2
};

struct ThreadSetting {
  ThreadFormat format;
  float authorPitch; // Exact operator input: e.g. 1.5 (mm), 13.0 (tpi), or 0.050 (in)
  long dupr;         // Kinematic integer pitch in deci-microns per rev
};

enum TaperModeType {
  TAPER_RATIO  = 0, // 1:k (authorVal = k, e.g. 3.5)
  TAPER_ANGLE  = 1, // Half-angle in degrees (authorVal = angleDeg, e.g. 1.432)
  TAPER_PRESET = 2  // Standard preset (Morse, etc.)
};

struct TaperSetting {
  TaperModeType modeType;
  float authorVal;   // The exact value entered
  float coneRatio;   // Derived kinematic diametral ratio: tan(angle) or 1.0/(2k)
  int presetId;      // Preset index if applicable
};

extern ThreadSetting currentThreadSetting;
extern TaperSetting currentTaperSetting;
extern long feedStepDu;

// Kinematic Core Domain APIs
bool setFeedrateDupr(long dupr, bool preserveDirection = true);
bool setFeedrateMm(float pitchMm, bool preserveDirection = true);
bool setFeedrateTpi(float tpi, bool preserveDirection = true);
bool setFeedrateInch(float pitchInch, bool preserveDirection = true);
bool reverseFeedDirection();
ThreadSetting getThreadSetting();

bool setTaperRatio(float ratioK);
bool setTaperAngle(float angleDeg);
bool setTaperPreset(int presetIdx, float angleDeg, float ratioVal);
TaperSetting getTaperSetting();

bool setAxisZero(Axis* a);
bool setAxisPositionDu(Axis* a, long posDu);
bool setAxisLimitDu(Axis* a, bool isLeftOrUp, long limitDu);
bool clearAxisLimit(Axis* a, bool isLeftOrUp);

bool setOperatingMode(int mode);
bool setFeedStepDu(long stepDu);
long getFeedStepDu();
void cycleFeedStep();

bool handleGcodeCommand(String command);
void taskGcode(void *param);
