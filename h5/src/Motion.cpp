#include "Motion.h"
#include "Config.h"
#include "Axis.h"
#include "Debug.h"
#include "Input.h"
#include "Display.h"
#include "WebServerModule.h"

// Automation and Operation State
long setupIndex = 0;
long opIndex = 0;
bool opIndexAdvanceFlag = false;
long opSubIndex = 0;
int opDuprSign = 0;
long opDupr = 0;

// GCode state
String gcodeCommand = "";
long gcodeFeedDuPerSec = GCODE_FEED_DEFAULT_DU_SEC;
bool gcodeInitialized = false;
bool gcodeAbsolutePositioning = true;
bool gcodeInBrace = false;
bool gcodeInSemicolon = false;
bool wsInKeycode = false;
int wsKeycode = 0;
String keycodeCommand = "";
bool gcodeInSave = false;
bool gcodeInSaveFirstLine = false;
String gcodeSaveName = "";
String gcodeSaveValue = "";
int gcodeProgramIndex = 0;
int gcodeProgramCount = 0;
String gcodeProgram = "";
int gcodeProgramCharIndex = 0;

static long auxSafeDistance = 0;
static long startOffset = 0;
static bool timerAttached = false;

// Axis helpers
Axis* getPitchAxis() {
  return (mode == MODE_FACE || mode == MODE_XGEAR) ? &x : &z;
}

Axis* getAsyncAxis() {
  return mode == MODE_Y ? &y : &z;
}

bool needZStops() {
  return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD || mode == MODE_ELLIPSE || mode == MODE_SLOT;
}

bool isPassMode() {
  return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_CUT || mode == MODE_THREAD || mode == MODE_ELLIPSE || mode == MODE_SLOT;
}

bool isGearboxMode() {
  return mode == MODE_NORMAL || mode == MODE_XGEAR;
}

int getLastSetupIndex() {
  if (mode == MODE_CONE || mode == MODE_GCODE) return 2;
  if (mode == MODE_THREAD) return 4;
  if (mode == MODE_TURN || mode == MODE_FACE || mode == MODE_CUT || mode == MODE_ELLIPSE || mode == MODE_SLOT) return 3;
  return 0;
}

long getPassModeZStart() {
  if (mode == MODE_TURN || mode == MODE_THREAD) return dupr > 0 ? z.rightStop : z.leftStop;
  if (mode == MODE_SLOT) return z.rightStop;
  if (mode == MODE_FACE) return auxForward ? z.rightStop : z.leftStop;
  if (mode == MODE_ELLIPSE) return dupr > 0 ? z.leftStop : z.rightStop;
  return z.pos;
}

long getPassModeXStart() {
  if (mode == MODE_TURN || mode == MODE_THREAD) return auxForward ? x.rightStop : x.leftStop;
  if (mode == MODE_SLOT) return auxForward ? x.rightStop : x.leftStop;
  if (mode == MODE_FACE || mode == MODE_CUT) return dupr > 0 ? x.rightStop : x.leftStop;
  if (mode == MODE_ELLIPSE) return x.rightStop;
  return x.pos;
}

void resetGcodeState() {
  gcodeCommand = "";
  gcodeFeedDuPerSec = GCODE_FEED_DEFAULT_DU_SEC;
  gcodeAbsolutePositioning = true;
  gcodeInBrace = false;
  gcodeInSemicolon = false;
  setMeasure(MEASURE_METRIC);
}

static unsigned int getTimerLimit() {
  if (dupr == 0) {
    return 65535;
  }
  return min((long)65535, (long)(TIMER_FREQ / (z.motorSteps * abs(dupr) / z.screwPitch)) - 1);
}

void updateAsyncTimerSettings() {
  setDir(getAsyncAxis(), dupr > 0);
  timerAlarmWrite(async_timer, getTimerLimit(), true);
  timerWrite(async_timer, 0);
}

void IRAM_ATTR onAsyncTimer() {
  Axis* a = getAsyncAxis();
  if (!isOn || a->movingManually || (mode != MODE_ASYNC && mode != MODE_Y)) {
    return;
  } else if (dupr > 0 && a->pos < a->leftStop) {
    if (a->pos <= a->motorPos) {
      a->pos++;
    }
    a->motorPos++;
    a->posGlobal++;
  } else if (dupr < 0 && a->pos > a->rightStop) {
    if (a->pos >= a->motorPos + a->backlashSteps) {
      a->pos--;
    }
    a->motorPos--;
    a->posGlobal--;
  } else {
    return;
  }

  DLOW(a->step);
  a->stepStartUs = micros();
  delayMicroseconds(10);
  DHIGH(a->step);
}

void setDupr(long value) {
  nextDupr = value;
  nextDuprFlag = true;
}

void setStarts(int value) {
  nextStarts = value;
  nextStartsFlag = true;
}

void setTurnPasses(int value) {
  if (isOn) {
    beep();
  } else {
    turnPasses = value;
  }
}

void setConeRatio(float value) {
  nextConeRatio = value;
  nextConeRatioFlag = true;
}

void applyConeRatio() {
  if (nextConeRatio == coneRatio) {
    return;
  }
  coneRatio = nextConeRatio;
  markOrigin();
}

void reset() {
  z.leftStop = LONG_MAX;
  z.nextLeftStopFlag = false;
  z.rightStop = LONG_MIN;
  z.nextRightStopFlag = false;
  z.originPos = 0;
  z.posGlobal = 0;
  z.motorPos = 0;
  z.pendingPos = 0;
  z.disabled = false;
  x.leftStop = LONG_MAX;
  x.nextLeftStopFlag = false;
  x.rightStop = LONG_MIN;
  x.nextRightStopFlag = false;
  x.originPos = 0;
  x.posGlobal = 0;
  x.motorPos = 0;
  x.pendingPos = 0;
  x.disabled = false;
  y.leftStop = LONG_MAX;
  y.nextLeftStopFlag = false;
  y.rightStop = LONG_MIN;
  y.nextRightStopFlag = false;
  y.originPos = 0;
  y.posGlobal = 0;
  y.motorPos = 0;
  y.pendingPos = 0;
  y.disabled = false;
  setDupr(0);
  setStarts(1);
  moveStep = MOVE_STEP_1;
  setModeFromTask(MODE_NORMAL);
  measure = MEASURE_METRIC;
  showTacho = false;
  showAngle = false;
  setConeRatio(1);
  auxForward = true;
}

long normalizePitch(long pitch) {
  float scale = 1.0f;
  if (measure == MEASURE_METRIC) {
    scale = 100.0f;
  } else if (measure == MEASURE_INCH) {
    scale = 25.4f;
  }
  return round(pitch / scale) * scale;
}

void applyDuprSignReverse() {
  spindlePos = -spindlePos;
  spindlePosAvg = -spindlePosAvg;
  if (spindlePosSync != 0) {
    Axis* a = getPitchAxis();
    spindlePosSync = spindleModulo(spindlePos - spindleFromPos(a, a->pos));
  }
}

void applyDupr() {
  if (nextDupr == dupr) {
    return;
  }
  bool signReverse = isOn && (isGearboxMode() || mode == MODE_CONE) && dupr != 0 && nextDupr == -dupr;
  bool joystickSamePitchMagnitude = mode == MODE_JOYSTICK && abs(nextDupr) == abs(dupr);
  dupr = nextDupr;
  if (mode == MODE_SLOT) {
    return;
  } else if (joystickSamePitchMagnitude) {
    return;
  } else if (signReverse) {
    applyDuprSignReverse();
  } else {
    markOrigin();
    if (mode == MODE_JOYSTICK) {
      resetJoystickLatheFeed();
    }
  }
  if (mode == MODE_ASYNC || mode == MODE_Y) {
    updateAsyncTimerSettings();
  }
}

void applyStarts() {
  if (starts == nextStarts) {
    return;
  }
  starts = nextStarts;
  markOrigin();
}

void setLeftStop(Axis* a, long value) {
  a->nextLeftStop = value;
  a->nextLeftStopFlag = true;
}

void setRightStop(Axis* a, long value) {
  a->nextRightStop = value;
  a->nextRightStopFlag = true;
}

static void leaveStop(Axis* a, long oldStop) {
  if (mode == MODE_CONE) {
    markOrigin();
  } else if (isGearboxMode() && a == getPitchAxis() && a->pos == oldStop) {
    spindlePosSync = spindleModulo(spindlePos - spindleFromPos(a, a->pos));
  }
}

void applyLeftStop(Axis* a) {
  long oldStop = a->leftStop;
  a->leftStop = a->nextLeftStop;
  leaveStop(a, oldStop);
}

void applyRightStop(Axis* a) {
  long oldStop = a->rightStop;
  a->rightStop = a->nextRightStop;
  leaveStop(a, oldStop);
}

void setModeFromTask(int value) {
  nextMode = value;
  nextModeFlag = true;
}

void setModeFromLoop(int value) {
  if (mode == value) {
    return;
  }
  if (isOn) {
    setIsOnFromLoop(false);
  }
  if (mode == MODE_THREAD) {
    setStarts(1);
  } else if (mode == MODE_ASYNC || mode == MODE_Y) {
    setAsyncTimerEnable(false);
  } else if (mode == MODE_JOYSTICK) {
    resetJoystickLatheFeed();
  }
  mode = value;
  setupIndex = 0;
  if (mode == MODE_JOYSTICK) {
    resetJoystickLatheFeed();
  }
  if (mode == MODE_ASYNC || mode == MODE_Y) {
    if (!timerAttached) {
      timerAttached = true;
      timerAttachInterrupt(async_timer, &onAsyncTimer, true);
    }
    updateAsyncTimerSettings();
    setAsyncTimerEnable(true);
  }
}

void setModeFromUi(int modeToSet, bool eventFromNextion) {
  setModeFromTask(modeToSet);
  if (eventFromNextion && lastNextionPageId == 1) toScreen("page 0");
}

void modeGearbox(Axis* a) {
  if (a->movingManually) {
    return;
  }
  a->speedMax = LONG_MAX;
  stepToContinuous(a, posFromSpindle(a, spindlePosAvg, true));
}

static long getJoystickLathePitchAbs() {
  return abs(dupr);
}

static bool startJoystickLatheFeedSync(Axis* a, long pitch) {
  joystickLatheSyncAxis = a == &x ? 1 : 0;
  joystickLatheSyncPitch = pitch;
  long spindleTarget = spindleFromPosWithModePitch(a, a->pos, pitch);
  spindlePosSync = spindleModulo(spindlePos - spindleTarget);
  if (spindlePosSync == 0) {
    spindlePosAvg = spindlePos = spindleTarget;
    joystickLatheSyncPitch = 0;
    return true;
  }
  return false;
}

static bool rebaseJoystickLatheAxis(Axis* a, long pitch) {
  if (pitch == 0) return true;
  if (xSemaphoreTake(a->mutex, 10) != pdTRUE) {
    beepFlag = true;
    return false;
  }

  long target = posFromSpindleWithModePitch(a, spindlePosAvg, false, pitch);
  long delta = target - a->pos;
  if (a->leftStop != LONG_MAX) {
    a->leftStop += delta;
  }
  if (a->rightStop != LONG_MIN) {
    a->rightStop += delta;
  }
  a->pos += delta;
  a->motorPos += delta;
  a->originPos -= delta;
  a->fractionalPos = 0;
  a->pendingPos = 0;
  a->continuous = false;
  a->speed = a->speedStart;

  xSemaphoreGive(a->mutex);
  return true;
}

static bool applyPendingJoystickLatheRebases(long zPitch, long xPitch) {
  if (joystickLatheRebaseZAfterSync) {
    if (zPitch != 0 && !rebaseJoystickLatheAxis(&z, zPitch)) return false;
    joystickLatheRebaseZAfterSync = false;
  }
  if (joystickLatheRebaseXAfterSync) {
    if (xPitch != 0 && !rebaseJoystickLatheAxis(&x, xPitch)) return false;
    joystickLatheRebaseXAfterSync = false;
  }
  return true;
}

static Axis* getJoystickLatheSyncAxis(long previousZPitch, long previousXPitch, long zPitch, long xPitch) {
  if (previousZPitch != 0 && zPitch != 0) return &z;
  if (previousXPitch != 0 && xPitch != 0) return &x;
  return zPitch != 0 ? &z : &x;
}

static long getJoystickLathePitchForAxis(Axis* a, long zPitch, long xPitch) {
  return a == &z ? zPitch : xPitch;
}

void modeJoystick() {
  int zDirection = z.movingManually ? 0 : joystickLatheDirectionZ;
  int xDirection = x.movingManually ? 0 : joystickLatheDirectionX;
  if (joystickLatheRapid) {
    zDirection = 0;
    xDirection = 0;
  }
  long pitchAbs = getJoystickLathePitchAbs();
  if (pitchAbs == 0) {
    zDirection = 0;
    xDirection = 0;
  }

  long zPitch = zDirection == 0 ? 0 : pitchAbs * zDirection;
  long xPitch = xDirection == 0 ? 0 : pitchAbs * xDirection;
  if (!applyPendingJoystickLatheRebases(zPitch, xPitch)) {
    return;
  }

  long previousZPitch = joystickLathePitchZ;
  long previousXPitch = joystickLathePitchX;
  bool feedChanged = zPitch != joystickLathePitchZ || xPitch != joystickLathePitchX ||
      zDirection != joystickLatheFeedSignZ || xDirection != joystickLatheFeedSignX;
  if (feedChanged && (joystickLathePitchZ != 0 || joystickLathePitchX != 0 || zPitch != 0 || xPitch != 0)) {
    if (zPitch != 0 || xPitch != 0) {
      if (joystickLatheThreadLocked) {
        bool zContinues = previousZPitch != 0 && zPitch == previousZPitch;
        bool xContinues = previousXPitch != 0 && xPitch == previousXPitch;
        if (zContinues || xContinues) {
          if (zPitch != 0 && !zContinues && !rebaseJoystickLatheAxis(&z, zPitch)) return;
          if (xPitch != 0 && !xContinues && !rebaseJoystickLatheAxis(&x, xPitch)) return;
        } else {
          Axis* syncAxis = getJoystickLatheSyncAxis(previousZPitch, previousXPitch, zPitch, xPitch);
          long syncPitch = getJoystickLathePitchForAxis(syncAxis, zPitch, xPitch);
          joystickLatheRebaseZAfterSync = zPitch != 0 && syncAxis != &z;
          joystickLatheRebaseXAfterSync = xPitch != 0 && syncAxis != &x;
          if (!startJoystickLatheFeedSync(syncAxis, syncPitch)) {
            joystickLatheFeedSignZ = zDirection;
            joystickLatheFeedSignX = xDirection;
            joystickLathePitchZ = zPitch;
            joystickLathePitchX = xPitch;
            return;
          }
          if (!applyPendingJoystickLatheRebases(zPitch, xPitch)) return;
        }
      } else {
        markOrigin();
        joystickLatheThreadLocked = true;
      }
    }
  }
  joystickLatheFeedSignZ = zDirection;
  joystickLatheFeedSignX = xDirection;
  joystickLathePitchZ = zPitch;
  joystickLathePitchX = xPitch;

  if (zPitch == 0 && xPitch == 0) {
    return;
  }
  if (zPitch != 0) {
    z.speedMax = LONG_MAX;
    stepToContinuous(&z, posFromSpindleWithModePitch(&z, spindlePosAvg, true, zPitch));
  }
  if (xPitch != 0) {
    x.speedMax = LONG_MAX;
    stepToContinuous(&x, posFromSpindleWithModePitch(&x, spindlePosAvg, true, xPitch));
  }
}

void modeTurn(Axis* main, Axis* aux) {
  if (main->movingManually || aux->movingManually || turnPasses <= 0 ||
      main->leftStop == LONG_MAX || main->rightStop == LONG_MIN ||
      aux->leftStop == LONG_MAX || aux->rightStop == LONG_MIN ||
      dupr == 0 || (dupr * opDuprSign < 0) || starts < 1) {
    setIsOnFromLoop(false);
    return;
  }

  long mainStartStop = opDuprSign > 0 ? main->rightStop : main->leftStop;
  long mainEndStop = opDuprSign > 0 ? main->leftStop : main->rightStop;
  long auxStartStop = auxForward ? aux->rightStop : aux->leftStop;
  long auxEndStop = auxForward ? aux->leftStop : aux->rightStop;

  auxSafeDistance = (auxForward ? -1 : 1) * SAFE_DISTANCE_DU * aux->motorSteps / aux->screwPitch;
  if (opIndex == 0) {
    startOffset = starts == 1 ? 0 : round(ENCODER_STEPS_FLOAT / starts);

    main->speedMax = main->speedManualMove;
    aux->speedMax = aux->speedManualMove;
    long auxPos = auxStartStop;
    long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      stepToFinal(main, mainStartStop);
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses * starts) {
    if (opIndexAdvanceFlag && (opIndex + starts) < turnPasses * starts) {
      opIndexAdvanceFlag = false;
      opIndex += starts;
    }
    float fraction = (turnPasses - ceil(opIndex / float(starts))) / (float)turnPasses;
    if (mode == MODE_THREAD) fraction = fraction * fraction;
    long auxPos = auxEndStop - (auxEndStop - auxStartStop) * fraction;
    if (opSubIndex == 0) {
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 1;
        spindlePosSync = spindleModulo(spindlePosGlobal - spindleFromPos(main, main->posGlobal) + startOffset * (opIndex - 1));
        return;
      }
    }
    if (opSubIndex == 1) {
      markOrigin();
      main->speedMax = LONG_MAX;
      opSubIndex = 2;
      return;
    }
    if (opSubIndex == 2) {
      long mainTargetPos = posFromSpindle(main, spindlePosAvg, true);
      long auxTargetPos = auxPos;
      if (mode == MODE_THREAD && coneRatio != 0) {
        float coneEffectRatio = -coneRatio / 2.0 / main->motorSteps * aux->motorSteps / aux->screwPitch * main->screwPitch * (auxForward ? 1.0 : -1.0);
        auxTargetPos = auxPos + round(mainTargetPos * coneEffectRatio);
      }

      if (auxTargetPos > aux->leftStop) auxTargetPos = aux->leftStop;
      if (auxTargetPos < aux->rightStop) auxTargetPos = aux->rightStop;

      stepToContinuous(main, mainTargetPos);
      stepToContinuous(aux, auxTargetPos);
      if (main->pos == mainEndStop || (coneRatio != 0 && aux->pos == (opDuprSign > 0 ? auxStartStop : auxEndStop))) {
        opSubIndex = 3;
      }
    }
    if (opSubIndex == 3) {
      long auxTargetPos = (mode == MODE_THREAD ? auxStartStop : auxPos) + auxSafeDistance;
      stepToFinal(aux, auxTargetPos);
      if (aux->pos == auxTargetPos) {
        opSubIndex = 4;
      }
    }
    if (opSubIndex == 4) {
      main->speedMax = main->speedManualMove;
      long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
      stepToFinal(main, mainPos);
      if (main->pos == mainPos) {
        stepToFinal(main, mainStartStop);
        opSubIndex = 0;
        opIndex++;
      }
    }
  } else {
    main->speedMax = main->speedManualMove;
    long auxPos = auxStartStop;
    long mainPos = mainStartStop;
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      setIsOnFromLoop(false);
      beep();
    }
  }
}

void modeFace(Axis* main, Axis* aux) {
  modeTurn(main, aux);
}

void modeCone() {
  if (z.movingManually || x.movingManually || coneRatio == 0) {
    return;
  }

  float zToXRatio = -coneRatio / 2.0 / z.motorSteps * x.motorSteps / x.screwPitch * z.screwPitch * (auxForward ? 1.0 : -1.0);
  if (zToXRatio == 0) {
    return;
  }

  x.speedMax = LONG_MAX;
  z.speedMax = LONG_MAX;

  long spindle = spindlePosAvg;
  long spindleMin = LONG_MIN;
  long spindleMax = LONG_MAX;
  if (z.leftStop != LONG_MAX) {
    (dupr > 0 ? spindleMax : spindleMin) = spindleFromPos(&z, z.leftStop);
  }
  if (z.rightStop != LONG_MIN) {
    (dupr > 0 ? spindleMin: spindleMax) = spindleFromPos(&z, z.rightStop);
  }
  if (x.leftStop != LONG_MAX) {
    long lim = spindleFromPos(&z, round(x.leftStop / zToXRatio));
    if (zToXRatio < 0) {
      (dupr > 0 ? spindleMin: spindleMax) = lim;
    } else {
      (dupr > 0 ? spindleMax : spindleMin) = lim;
    }
  }
  if (x.rightStop != LONG_MIN) {
    long lim = spindleFromPos(&z, round(x.rightStop / zToXRatio));
    if (zToXRatio < 0) {
      (dupr > 0 ? spindleMax : spindleMin) = lim;
    } else {
      (dupr > 0 ? spindleMin: spindleMax) = lim;
    }
  }
  if (spindle > spindleMax) {
    spindle = spindleMax;
  } else if (spindle < spindleMin) {
    spindle = spindleMin;
  }

  stepToContinuous(&z, posFromSpindle(&z, spindle, true));
  stepToContinuous(&x, round(z.pos * zToXRatio));
}

void modeCut() {
  if (x.movingManually || turnPasses <= 0 || x.leftStop == LONG_MAX || x.rightStop == LONG_MIN || dupr == 0 || dupr * opDuprSign < 0) {
    setIsOnFromLoop(false);
    return;
  }

  long startStop = opDuprSign > 0 ? x.rightStop : x.leftStop;
  long endStop = opDuprSign > 0 ? x.leftStop : x.rightStop;

  if (opIndex == 0) {
    x.speedMax = x.speedManualMove;
    long xPos = startStop;
    stepToFinal(&x, xPos);
    if (x.pos == xPos) {
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses) {
    if (opSubIndex == 0) {
      spindlePosAvg = spindlePos = spindleFromPos(&x, x.pos);
      opSubIndex = 1;
    }
    if (opSubIndex == 1) {
      x.speedMax = LONG_MAX;
      long endPos = endStop - (endStop - startStop) / turnPasses * (turnPasses - opIndex);
      long xPos = posFromSpindle(&x, spindlePosAvg, true);
      if (dupr > 0 && xPos > endPos) xPos = endPos;
      else if (dupr < 0 && xPos < endPos) xPos = endPos;
      stepToContinuous(&x, xPos);
      if (x.pos == endPos) {
        opSubIndex = 2;
      }
    }
    if (opSubIndex == 2) {
      x.speedMax = x.speedManualMove;
      stepToFinal(&x, startStop);
      if (x.pos == startStop) {
        opSubIndex = 0;
        opIndex++;
      }
    }
  } else {
    setIsOnFromLoop(false);
    beep();
  }
}

static long duPerSecondToStepsPerSecond(Axis* a, long duPerSecond) {
  long speed = round(abs(duPerSecond) * a->motorSteps / a->screwPitch);
  return speed < 1 ? 1 : speed;
}

void modeSlot() {
  if (z.movingManually || x.movingManually || turnPasses <= 0 ||
      z.leftStop == LONG_MAX || z.rightStop == LONG_MIN ||
      x.leftStop == LONG_MAX || x.rightStop == LONG_MIN) {
    setIsOnFromLoop(false);
    return;
  }

  long zStartStop = z.rightStop;
  long zEndStop = z.leftStop;
  long xStartStop = auxForward ? x.rightStop : x.leftStop;
  long xEndStop = auxForward ? x.leftStop : x.rightStop;

  if (opIndex == 0) {
    z.speedMax = z.speedManualMove;
    x.speedMax = x.speedManualMove;
    stepToFinal(&z, zStartStop);
    stepToFinal(&x, xStartStop);
    if (z.pos == zStartStop && x.pos == xStartStop) {
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses) {
    long xPos = xEndStop - round((xEndStop - xStartStop) * (turnPasses - opIndex) / float(turnPasses));
    long zReduction = duToSteps(&z, SLOT_LEFT_REDUCTION_DU) * (opIndex - 1);
    long zEndPos = zEndStop - zReduction;
    if (zEndPos < zStartStop) zEndPos = zStartStop;

    if (opSubIndex == 0) {
      x.speedMax = x.speedManualMove;
      stepToFinal(&x, xPos);
      if (x.pos == xPos) {
        opSubIndex = 1;
      }
    } else if (opSubIndex == 1) {
      if (dupr == 0) {
        stepToFinal(&z, z.pos);
        return;
      }
      z.speedMax = duPerSecondToStepsPerSecond(&z, dupr);
      stepToContinuous(&z, zEndPos);
      if (z.pos == zEndPos) {
        opSubIndex = 2;
      }
    } else if (opSubIndex == 2) {
      x.speedMax = x.speedManualMove;
      stepToFinal(&x, xStartStop);
      if (x.pos == xStartStop) {
        opSubIndex = 3;
      }
    } else if (opSubIndex == 3) {
      z.speedMax = z.speedManualMove;
      stepToFinal(&z, zStartStop);
      if (z.pos == zStartStop) {
        opSubIndex = 0;
        opIndex++;
      }
    }
  } else {
    setIsOnFromLoop(false);
    beep();
  }
}

void modeEllipse(Axis* main, Axis* aux) {
  if (main->movingManually || aux->movingManually || turnPasses <= 0 ||
      main->leftStop == LONG_MAX || main->rightStop == LONG_MIN ||
      aux->leftStop == LONG_MAX || aux->rightStop == LONG_MIN ||
      main->leftStop == main->rightStop ||
      aux->leftStop == aux->rightStop ||
      dupr == 0 || dupr != opDupr) {
    setIsOnFromLoop(false);
    return;
  }

  long mainStartStop = opDuprSign > 0 ? main->rightStop : main->leftStop;
  long mainEndStop = opDuprSign > 0 ? main->leftStop : main->rightStop;
  long auxStartStop = aux->rightStop;
  long auxEndStop = aux->leftStop;

  main->speedMax = main->speedManualMove;
  aux->speedMax = aux->speedManualMove;

  if (opIndex == 0) {
    opIndex = 1;
    opSubIndex = 0;
    spindlePos = 0;
    spindlePosAvg = 0;
  } else if (opIndex <= turnPasses) {
    float pass0to1 = opIndex / float(turnPasses);
    long mainDelta = round(pass0to1 * (mainEndStop - mainStartStop));
    long auxDelta = round(pass0to1 * (auxEndStop - auxStartStop));
    long spindleDelta = spindleFromPos(main, mainDelta);

    if (opSubIndex == 0) {
      long auxPos = auxStartStop;
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 1;
      }
    } else if (opSubIndex == 1) {
      long mainPos = mainEndStop - mainDelta;
      stepToFinal(main, mainPos);
      if (main->pos == mainPos) {
        opSubIndex = 2;
        spindlePos = 0;
        spindlePosAvg = 0;
      }
    } else if (opSubIndex == 2) {
      float progress0to1 = 0;
      if ((spindleDelta > 0 && spindlePosAvg >= spindleDelta) || (spindleDelta < 0 && spindlePosAvg <= spindleDelta)) {
        progress0to1 = 1;
      } else {
        progress0to1 = spindlePosAvg / float(spindleDelta);
      }
      float mainCoeff = auxForward ? cos(HALF_PI * (3 + progress0to1)) : (1 + sin(HALF_PI * (progress0to1 - 1)));
      long mainPos = mainEndStop - mainDelta + round(mainDelta * mainCoeff);
      float auxCoeff = auxForward ? (1 + sin(HALF_PI * (3 + progress0to1))) : sin(HALF_PI * progress0to1);
      long auxPos = auxStartStop + round(auxDelta * auxCoeff);
      stepToContinuous(main, mainPos);
      stepToContinuous(aux, auxPos);
      if (progress0to1 == 1 && main->pos == mainPos && aux->pos == auxPos) {
        opIndex++;
        opSubIndex = 0;
      }
    }
  } else if (opIndex == turnPasses + 1) {
    stepToFinal(aux, auxStartStop);
    if (aux->pos == auxStartStop) {
      setIsOnFromLoop(false);
      beep();
    }
  }
}

void discountFullSpindleTurns() {
  if (dupr != 0 && (isGearboxMode() || mode == MODE_CONE)) {
    Axis* a = getPitchAxis();
    if (stepperIsRunning(a)) return;
    int spindlePosDiff = 0;
    if (a->pos == a->rightStop) {
      long stopSpindlePos = spindleFromPos(a, a->rightStop);
      if (dupr > 0) {
        if (spindlePos < stopSpindlePos - ENCODER_STEPS_INT) {
          spindlePosDiff = ENCODER_STEPS_INT;
        }
      } else {
        if (spindlePos > stopSpindlePos + ENCODER_STEPS_INT) {
          spindlePosDiff = -ENCODER_STEPS_INT;
        }
      }
    } else if (a->pos == a->leftStop) {
      long stopSpindlePos = spindleFromPos(a, a->leftStop);
      if (dupr > 0) {
        if (spindlePos > stopSpindlePos + ENCODER_STEPS_INT) {
          spindlePosDiff = -ENCODER_STEPS_INT;
        }
      } else {
        if (spindlePos < stopSpindlePos - ENCODER_STEPS_INT) {
          spindlePosDiff = ENCODER_STEPS_INT;
        }
      }
    }
    if (spindlePosDiff != 0) {
      spindlePos += spindlePosDiff;
      spindlePosAvg += spindlePosDiff;
    }
  }
}

void applySettings() {
  if (nextDuprFlag) {
    applyDupr();
    nextDuprFlag = false;
  }
  if (nextStartsFlag) {
    applyStarts();
    nextStartsFlag = false;
  }
  if (z.nextLeftStopFlag) {
    applyLeftStop(&z);
    z.nextLeftStopFlag = false;
  }
  if (z.nextRightStopFlag) {
    applyRightStop(&z);
    z.nextRightStopFlag = false;
  }
  if (x.nextLeftStopFlag) {
    applyLeftStop(&x);
    x.nextLeftStopFlag = false;
  }
  if (x.nextRightStopFlag) {
    applyRightStop(&x);
    x.nextRightStopFlag = false;
  }
  if (y.nextLeftStopFlag) {
    applyLeftStop(&y);
    y.nextLeftStopFlag = false;
  }
  if (y.nextRightStopFlag) {
    applyRightStop(&y);
    y.nextRightStopFlag = false;
  }
  if (nextConeRatioFlag) {
    applyConeRatio();
    nextConeRatioFlag = false;
  }
  if (nextIsOnFlag) {
    setIsOnFromLoop(nextIsOn);
    nextIsOnFlag = false;
  }
  if (nextModeFlag) {
    setModeFromLoop(nextMode);
    nextModeFlag = false;
  }
}

// GCode Parsing and Execution
static String getValueString(const String& command, char letter) {
  int index = command.indexOf(letter);
  if (index == -1) {
    return "";
  }
  String valueString;
  for (int i = index + 1; i < command.length(); i++) {
    char c = command.charAt(i);
    if (isDigit(c) || c == '.' || c == '-') {
      valueString += c;
    } else {
      break;
    }
  }
  return valueString;
}

static float getFloat(const String& command, char letter) {
  return getValueString(command, letter).toFloat();
}

static int getInt(const String& command, char letter) {
  return getValueString(command, letter).toInt();
}

static float gcodeUnitScaleDu() {
  return measure == MEASURE_METRIC ? 10000.0 : 254000.0;
}

static long gcodeUnitToDu(float value) {
  return round(value * gcodeUnitScaleDu());
}

static void setFeedRate(const String& command) {
  float feed = getFloat(command, 'F');
  if (feed <= 0) return;
  gcodeFeedDuPerSec = round(feed * gcodeUnitScaleDu() / 60.0);
}

static long mmOrInchToAbsolutePos(Axis* a, float mmOrInch) {
  long part1 = a->gcodeRelativePos;
  long part2 = round(mmOrInch * gcodeUnitScaleDu() / a->screwPitch * a->motorSteps);
  return part1 + part2;
}

static void updateAxisSpeeds(long diffX, long diffZ, long diffY) {
  if (diffX == 0 && diffZ == 0 && diffY == 0) return;
  long absX = abs(diffX);
  long absZ = abs(diffZ);
  long absC = abs(diffY);
  float stepsPerSecX = gcodeFeedDuPerSec * x.motorSteps / x.screwPitch;
  float minStepsPerSecX = GCODE_FEED_MIN_DU_SEC * x.motorSteps / x.screwPitch;
  if (stepsPerSecX > x.speedManualMove) stepsPerSecX = x.speedManualMove;
  else if (stepsPerSecX < minStepsPerSecX) stepsPerSecX = minStepsPerSecX;
  float stepsPerSecZ = gcodeFeedDuPerSec * z.motorSteps / z.screwPitch;
  float minStepsPerSecZ = GCODE_FEED_MIN_DU_SEC * z.motorSteps / z.screwPitch;
  if (stepsPerSecZ > z.speedManualMove) stepsPerSecZ = z.speedManualMove;
  else if (stepsPerSecZ < minStepsPerSecZ) stepsPerSecZ = minStepsPerSecZ;
  float stepsPerSecY = gcodeFeedDuPerSec * y.motorSteps / y.screwPitch;
  float minStepsPerSecY = GCODE_FEED_MIN_DU_SEC * y.motorSteps / y.screwPitch;
  if (stepsPerSecY > y.speedManualMove) stepsPerSecY = y.speedManualMove;
  else if (stepsPerSecY < minStepsPerSecY) stepsPerSecY = minStepsPerSecY;
  float secX = absX / stepsPerSecX;
  float secZ = absZ / stepsPerSecZ;
  float secY = absC / stepsPerSecY;
  float sec = ACTIVE_Y ? max(max(secX, secZ), secY) : max(secX, secZ);
  x.speedMax = sec > 0 ? absX / sec : x.speedManualMove;
  z.speedMax = sec > 0 ? absZ / sec : z.speedManualMove;
  y.speedMax = sec > 0 ? absC / sec : y.speedManualMove;
  if (x.speedMax < minStepsPerSecX) x.speedMax = minStepsPerSecX;
  if (z.speedMax < minStepsPerSecZ) z.speedMax = minStepsPerSecZ;
  if (y.speedMax < minStepsPerSecY) y.speedMax = minStepsPerSecY;
}

static void gcodeWaitEpsilon(int epsilon) {
  while (isOn && (abs(x.pendingPos) > epsilon || abs(z.pendingPos) > epsilon || abs(y.pendingPos) > epsilon || (SPINDLE_PAUSES_GCODE && getApproxRpm() < GCODE_MIN_RPM))) {
    taskYIELD();
  }
}

static void gcodeWaitNear() {
  gcodeWaitEpsilon(GCODE_WAIT_EPSILON_STEPS);
}

static void gcodeWaitStop() {
  gcodeWaitEpsilon(0);
}

static void G00_01(const String& command) {
  long xStart = x.pos;
  long zStart = z.pos;
  long yStart = y.pos;
  long xEnd = command.indexOf(x.name) >= 0 ? mmOrInchToAbsolutePos(&x, getFloat(command, x.name)) : xStart;
  long zEnd = command.indexOf(z.name) >= 0 ? mmOrInchToAbsolutePos(&z, getFloat(command, z.name)) : zStart;
  long yEnd = command.indexOf(y.name) >= 0 ? mmOrInchToAbsolutePos(&y, getFloat(command, y.name)) : yStart;
  long xDiff = xEnd - xStart;
  long zDiff = zEnd - zStart;
  long yDiff = yEnd - yStart;
  updateAxisSpeeds(xDiff, zDiff, yDiff);
  long chunks = round(max(max(abs(xDiff), abs(zDiff)), abs(yDiff)) * LINEAR_INTERPOLATION_PRECISION);
  for (long i = 0; i < chunks; i++) {
    if (!isOn) return;
    float scale = i / float(chunks);
    stepToContinuous(&x, xStart + xDiff * scale);
    stepToContinuous(&z, zStart + zDiff * scale);
    if (ACTIVE_Y) stepToContinuous(&y, yStart + yDiff * scale);
    gcodeWaitNear();
  }
  stepToFinal(&x, xEnd);
  stepToFinal(&z, zEnd);
  if (ACTIVE_Y) stepToFinal(&y, yEnd);
  gcodeWaitStop();
}

static bool gcodeError(const String& message, const String& command) {
  setIsOnFromTask(false);
  writeBuffer(&outBuffer, "error: ");
  writeBuffer(&outBuffer, message);
  writeBuffer(&outBuffer, " ");
  writeBuffer(&outBuffer, command);
  writeBuffer(&outBuffer, "\n");
  return false;
}

static long spindleFromPosWithPitch(Axis* a, long pos, long pitchDu) {
  return round(pos * a->screwPitch * ENCODER_STEPS_FLOAT / a->motorSteps / pitchDu);
}

static bool gcodeWaitForSpindle() {
  while (isOn && getApproxRpm() < GCODE_MIN_RPM) {
    taskYIELD();
  }
  return isOn;
}

static long gcodeWaitForThreadPhase(Axis* lead, long pitchDu) {
  if (!gcodeWaitForSpindle()) return -1;

  long lastPhase = spindleModulo(spindlePosGlobal - spindleFromPosWithPitch(lead, lead->posGlobal, pitchDu));
  while (isOn) {
    if (getApproxRpm() < GCODE_MIN_RPM) {
      if (!gcodeWaitForSpindle()) return -1;
      lastPhase = spindleModulo(spindlePosGlobal - spindleFromPosWithPitch(lead, lead->posGlobal, pitchDu));
    }

    long phase = spindleModulo(spindlePosGlobal - spindleFromPosWithPitch(lead, lead->posGlobal, pitchDu));
    if (phase == 0) {
      return 0;
    }
    if (phase - lastPhase < -ENCODER_STEPS_INT / 2) {
      return phase;
    }
    lastPhase = phase;
    taskYIELD();
  }
  return -1;
}

static bool gcodeMarkThreadOrigin(long spindleAtOrigin) {
  if (xSemaphoreTake(motionMutex, 100) != pdTRUE) {
    setIsOnFromTask(false);
    writeBuffer(&outBuffer, "error: failed to synchronize G32\n");
    return false;
  }
  markOrigin();
  spindlePos = spindleAtOrigin;
  spindlePosAvg = spindleAtOrigin;
  xSemaphoreGive(motionMutex);
  return true;
}

static bool G32(const String& command) {
  float feed = getFloat(command, 'F');
  if (feed <= 0) {
    return gcodeError("G32 requires positive F pitch", command);
  }

  long pitchDuAbs = abs(gcodeUnitToDu(feed));
  if (pitchDuAbs == 0 || pitchDuAbs > DUPR_MAX) {
    return gcodeError("G32 F pitch out of range", command);
  }

  long xStart = x.pos;
  long zStart = z.pos;
  long yStart = y.pos;
  long xEnd = command.indexOf(x.name) >= 0 ? mmOrInchToAbsolutePos(&x, getFloat(command, x.name)) : xStart;
  long zEnd = command.indexOf(z.name) >= 0 ? mmOrInchToAbsolutePos(&z, getFloat(command, z.name)) : zStart;
  long yEnd = command.indexOf(y.name) >= 0 ? mmOrInchToAbsolutePos(&y, getFloat(command, y.name)) : yStart;
  long xDiff = xEnd - xStart;
  long zDiff = zEnd - zStart;
  long yDiff = yEnd - yStart;

  Axis* lead = NULL;
  long leadDiff = 0;
  if (zDiff != 0) {
    lead = &z;
    leadDiff = zDiff;
  } else if (xDiff != 0) {
    lead = &x;
    leadDiff = xDiff;
  } else if (ACTIVE_Y && yDiff != 0) {
    lead = &y;
    leadDiff = yDiff;
  } else {
    return gcodeError("G32 requires an axis move", command);
  }

  long pitchDu = leadDiff > 0 ? pitchDuAbs : -pitchDuAbs;
  long spindleTarget = spindleFromPosWithPitch(lead, leadDiff, pitchDu);
  if (spindleTarget <= 0) {
    return gcodeError("G32 move too short for pitch", command);
  }

  long spindleAtOrigin = gcodeWaitForThreadPhase(lead, pitchDu);
  if (spindleAtOrigin < 0) return true;
  if (!gcodeMarkThreadOrigin(spindleAtOrigin)) return false;

  x.speedMax = LONG_MAX;
  z.speedMax = LONG_MAX;
  y.speedMax = LONG_MAX;

  while (isOn) {
    long spindle = spindlePosAvg;
    bool finished = spindle >= spindleTarget;
    float progress = finished ? 1.0 : spindle / float(spindleTarget);
    if (progress < 0) progress = 0;
    else if (progress > 1) progress = 1;

    stepToContinuous(&x, round(xDiff * progress));
    stepToContinuous(&z, round(zDiff * progress));
    if (ACTIVE_Y) stepToContinuous(&y, round(yDiff * progress));

    if (finished) break;
    taskYIELD();
  }

  if (!isOn) return true;
  stepToFinal(&x, xDiff);
  stepToFinal(&z, zDiff);
  if (ACTIVE_Y) stepToFinal(&y, yDiff);
  gcodeWaitStop();
  return true;
}

static bool handleGcode(const String& command) {
  int op = getInt(command, 'G');
  if (op == 0 || op == 1) {
    G00_01(command);
  } else if (op == 32) {
    return G32(command);
  } else if (op == 20 || op == 21) {
    setMeasure(op == 20 ? MEASURE_INCH : MEASURE_METRIC);
  } else if (op == 90 || op == 91) {
    gcodeAbsolutePositioning = op == 90;
  } else if (op == 94) {
    /* no-op feed per minute */
  } else if (op == 18) {
    /* no-op ZX plane selection */
  } else {
    writeBuffer(&outBuffer, "error: unsupported command ");
    writeBuffer(&outBuffer, command);
    writeBuffer(&outBuffer, "\n");
    return false;
  }
  return true;
}

static bool handleMcode(const String& command) {
  int op = getInt(command, 'M');
  if (op == 0 || op == 1 || op == 2 || op == 30) {
    setIsOnFromTask(false);
  } else {
    setIsOnFromTask(false);
    writeBuffer(&outBuffer, "error: unsupported command ");
    writeBuffer(&outBuffer, command);
    writeBuffer(&outBuffer, "\n");
    return false;
  }
  return true;
}

bool handleGcodeCommand(String command) {
  command.trim();
  if (command.length() == 0) return false;

  char code = command.charAt(0);
  int spaceIndex = command.indexOf(' ');
  if (code == 'N' && spaceIndex > 0) {
    command = command.substring(spaceIndex + 1);
    code = command.charAt(0);
  }

  z.gcodeRelativePos = gcodeAbsolutePositioning ? -z.originPos : z.pos;
  x.gcodeRelativePos = gcodeAbsolutePositioning ? -x.originPos : x.pos;
  y.gcodeRelativePos = gcodeAbsolutePositioning ? -y.originPos : y.pos;

  if (!(code == 'G' && getInt(command, 'G') == 32)) {
    setFeedRate(command);
  }
  switch (code) {
    case 'G':
    case NAME_Z:
    case NAME_X:
    case NAME_Y: return handleGcode(command);
    case 'F': return true;
    case 'M': return handleMcode(command);
    case 'T': return true;
    default: writeBuffer(&outBuffer, "error: unsupported command "); writeBuffer(&outBuffer, code); writeBuffer(&outBuffer, "\n"); return false;
  }
  return false;
}

void taskGcode(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    if (mode != MODE_GCODE) {
      gcodeInitialized = false;
    } else if (!gcodeInitialized) {
      gcodeInitialized = true;
      resetGcodeState();
    }
    char receivedChar = '\0';
    bool isWebSocket = false;
    if (mode == MODE_GCODE && isOn && gcodeProgramCharIndex < gcodeProgram.length()) {
      receivedChar = gcodeProgram.charAt(gcodeProgramCharIndex);
      gcodeProgramCharIndex++;
    } else if (bufferAvailable(&inBuffer)) {
      isWebSocket = true;
      receivedChar = shiftBuffer(&inBuffer);
    }
    int charCode = int(receivedChar);
    if (charCode > 0) {
      if (gcodeInBrace) {
        if (receivedChar == ')') gcodeInBrace = false;
      } else if (wsInKeycode) {
        if (charCode < 32) {
          if (wsKeycode == 0) {
            wsKeycode = keycodeCommand.toInt();
            writeBuffer(&outBuffer, String(wsKeycode));
            writeBuffer(&outBuffer, "\n");
          } else {
            writeBuffer(&outBuffer, "slower\n");
          }
          wsInKeycode = false;
          keycodeCommand = "";
        } else {
          keycodeCommand += receivedChar;
        }
      } else if (receivedChar == '(') {
        gcodeInBrace = true;
      } else if (receivedChar == ';') {
        gcodeInSemicolon = true;
      } else if (gcodeInSemicolon && charCode >= 32) {
        // Ignoring comment.
      } else if (receivedChar == '!') {
        setIsOnFromTask(false);
      } else if (receivedChar == '~') {
        setIsOnFromTask(true);
      } else if (receivedChar == '%') {
        resetGcodeState();
      } else if (receivedChar == '?') {
        writeBuffer(&outBuffer, "<");
        writeBuffer(&outBuffer, isOn ? "Run" : "Idle");
        writeBuffer(&outBuffer, "|WPos:");
        writeBuffer(&outBuffer, formatPosition(getAxisPositionMm(&x)));
        writeBuffer(&outBuffer, ",");
        writeBuffer(&outBuffer, formatPosition(getAxisPositionMm(&y)));
        writeBuffer(&outBuffer, ",");
        writeBuffer(&outBuffer, formatPosition(getAxisPositionMm(&z)));
        writeBuffer(&outBuffer, "|FS:");
        writeBuffer(&outBuffer, round(gcodeFeedDuPerSec * 60 / 10000.0));
        writeBuffer(&outBuffer, ",");
        writeBuffer(&outBuffer, String(getApproxRpm()));
        writeBuffer(&outBuffer, "|Id:");
        writeBuffer(&outBuffer, CONTROLLER_VERSION_TEXT);
        writeBuffer(&outBuffer, ">");
      } else if (gcodeInSave && receivedChar == '"') {
        gcodeInSave = false;
        if (gcodeSaveName.length() == 0) {
          if (removeAllGcode()) writeBuffer(&outBuffer, "ok\n");
        } else if (gcodeSaveValue.length() > 1) {
          if (saveGcode()) writeBuffer(&outBuffer, "ok\n");
        } else if (gcodeSaveName.length() == 1) {
          writeBuffer(&outBuffer, "error: name must be at least 2 chars\n");
        } else {
          removeGcodeByName(gcodeSaveName);
        }
        gcodeSaveName = "";
        gcodeSaveValue = "";
      } else if (!gcodeInSave && receivedChar == '"') {
        gcodeInSave = true;
        gcodeInSaveFirstLine = true;
      } else if (gcodeInSaveFirstLine && receivedChar >= 32) {
        gcodeSaveName += receivedChar;
      } else if (gcodeInSaveFirstLine && receivedChar < 32) {
        gcodeInSaveFirstLine = false;
        writeBuffer(&outBuffer, "ok\n");
      } else if (gcodeInSave) {
        gcodeSaveValue += receivedChar;
        if (receivedChar < 32) {
          gcodeInBrace = false;
          gcodeInSemicolon = false;
          writeBuffer(&outBuffer, "ok\n");
        }
      } else if (isOn) {
        if (gcodeInBrace && charCode < 32) {
          writeBuffer(&outBuffer, "error: comment not closed\n");
          setIsOnFromTask(false);
        } else if (charCode < 32 && gcodeCommand.length() > 1) {
          if (handleGcodeCommand(gcodeCommand)) {
            if (isWebSocket) writeBuffer(&outBuffer, "ok\n");
          }
          gcodeCommand = "";
          gcodeInSemicolon = false;
        } else if (charCode < 32) {
          if (isWebSocket) writeBuffer(&outBuffer, "ok\n");
          gcodeCommand = "";
          gcodeInSemicolon = false;
        } else if (charCode >= 32 && (charCode == 'G' || charCode == 'M')) {
          handleGcodeCommand(gcodeCommand);
          gcodeCommand = receivedChar;
        } else if (charCode >= 32) {
          gcodeCommand += receivedChar;
        }
      } else if (receivedChar == '=') {
        wsInKeycode = true;
        keycodeCommand = "";
      }
    }
    if (mode == MODE_GCODE && isOn && gcodeProgramCharIndex > 0 && gcodeProgramCharIndex == gcodeProgram.length()) {
      setIsOnFromTask(false);
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}
