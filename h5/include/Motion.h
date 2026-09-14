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
void reset();
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

void setModeFromLoop(int m);
void setModeFromTask(int m);
void setModeFromUi(int m, bool fromNextion);

void discountFullSpindleTurns();

void modeGearbox(Axis* a);
void modeTurn(Axis* main, Axis* aux);
void modeFace(Axis* main, Axis* aux);
void modeCut();
void modeSlot();
void modeCone();
void modeEllipse(Axis* main, Axis* aux);
void modeJoystick();

bool handleGcodeCommand(String command);
void taskGcode(void *param);
