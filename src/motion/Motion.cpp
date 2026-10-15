#include "Motion.h"
#include "Config.h"
#include "Axis.h"
#include "Debug.h"
#include "Input.h"
#include "Display.h"
#include "WebServerModule.h"
#include "SettingsService.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"

// Automation and Operation State
long setupIndex = 0;
long opIndex = 0;
bool opIndexAdvanceFlag = false;
long opSubIndex = 0;
int opDuprSign = 0;
long opDupr = 0;

// GCode state
String gcodeCommand = "";
long gcodeFeedDuPerSec = 20000;
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

ThreadSetting currentThreadSetting = { THREAD_METRIC, 0.0f, 0 };
TaperSetting currentTaperSetting = { TAPER_RATIO, 1.0f, 1.0f, 0 };
long feedStepDu = FEED_STEPS_METRIC[1];

static long auxSafeDistance = 0;
static long startOffset = 0;
static bool timerAttached = false;

VelocityFeedState velocityFeed;

// Axis helpers
Axis* getPitchAxis() {
  return (mode == LatheMode::FACE || mode == LatheMode::XGEAR) ? &x : &z;
}

Axis* getAsyncAxis() {
  if (velocityFeed.axis != nullptr) {
    return velocityFeed.axis;
  }
  return &z;
}

bool needZStops() {
  return mode == LatheMode::TURN || mode == LatheMode::FACE || mode == LatheMode::THREAD || mode == LatheMode::ELLIPSE || mode == LatheMode::SLOT;
}

bool isPassMode() {
  return mode == LatheMode::TURN || mode == LatheMode::FACE || mode == LatheMode::CUT || mode == LatheMode::THREAD || mode == LatheMode::ELLIPSE || mode == LatheMode::SLOT;
}

bool isGearboxMode() {
  return mode == LatheMode::NORMAL || mode == LatheMode::XGEAR;
}

int getLastSetupIndex() {
  if (mode == LatheMode::CONE || mode == LatheMode::GCODE) return 2;
  if (mode == LatheMode::THREAD) return 4;
  if (mode == LatheMode::TURN || mode == LatheMode::FACE || mode == LatheMode::CUT || mode == LatheMode::ELLIPSE || mode == LatheMode::SLOT) return 3;
  return 0;
}

long getPassModeZStart() {
  if (mode == LatheMode::TURN || mode == LatheMode::THREAD) return dupr > 0 ? z.rightStop : z.leftStop;
  if (mode == LatheMode::SLOT) return z.rightStop;
  if (mode == LatheMode::FACE) return auxForward ? z.rightStop : z.leftStop;
  if (mode == LatheMode::ELLIPSE) return dupr > 0 ? z.leftStop : z.rightStop;
  return z.pos;
}

long getPassModeXStart() {
  if (mode == LatheMode::TURN || mode == LatheMode::THREAD) return auxForward ? x.rightStop : x.leftStop;
  if (mode == LatheMode::SLOT) return auxForward ? x.rightStop : x.leftStop;
  if (mode == LatheMode::FACE || mode == LatheMode::CUT) return dupr > 0 ? x.rightStop : x.leftStop;
  if (mode == LatheMode::ELLIPSE) return x.rightStop;
  return x.pos;
}

void resetGcodeState() {
  gcodeCommand = "";
  gcodeFeedDuPerSec = sysConfig.gcode.defaultFeedDuPerSec;
  gcodeAbsolutePositioning = true;
  gcodeInBrace = false;
  gcodeInSemicolon = false;
  SettingsService::setMeasure(MeasureUnit::METRIC);
}

void onAsyncTimer();

void initVelocityTimer() {
  if (!timerAttached && async_timer != nullptr) {
    timerAttachInterrupt(async_timer, &onAsyncTimer);
    timerAttached = true;
  }
}

bool startVelocityFeed(Axis* a, int32_t speedDuPerMin, int8_t direction, int32_t targetDistDu) {
  if (!a || speedDuPerMin <= 0 || direction == 0) return false;

  initVelocityTimer();

  // Convert du/min to timer ticks
  long stepsPerMin = (static_cast<int64_t>(speedDuPerMin) * a->motorSteps) / a->screwPitch;
  long stepsPerSec = stepsPerMin / 60;
  if (stepsPerSec <= 0) stepsPerSec = 1;

  unsigned int timerLimit = min((long)65535, (long)(TIMER_FREQ / stepsPerSec) - 1);
  if (timerLimit < 10) timerLimit = 10;

  // Set hardware direction pin: positive coordinate travel -> true, negative -> false
  setDir(a, direction > 0);

  // Compute target position bounded by soft limits
  if (targetDistDu > 0) {
    long distSteps = (static_cast<int64_t>(targetDistDu) * a->motorSteps) / a->screwPitch;
    if (direction > 0) {
      long t = a->pos + distSteps;
      velocityFeed.targetPos = (a->leftStop != LONG_MAX && t > a->leftStop) ? a->leftStop : t;
    } else {
      long t = a->pos - distSteps;
      velocityFeed.targetPos = (a->rightStop != LONG_MIN && t < a->rightStop) ? a->rightStop : t;
    }
  } else {
    // Continuous feed bounded by active soft limit
    velocityFeed.targetPos = (direction > 0) ? a->leftStop : a->rightStop;
  }

  velocityFeed.axis = a;
  velocityFeed.speedDuPerMin = speedDuPerMin;
  velocityFeed.direction = direction;
  velocityFeed.targetDistDu = targetDistDu;
  velocityFeed.active = true;

  timerAlarm(async_timer, timerLimit, true, 0);
  timerWrite(async_timer, 0);
  setAsyncTimerEnable(true);
  return true;
}

void stopVelocityFeed() {
  velocityFeed.active = false;
  setAsyncTimerEnable(false);
}

bool isVelocityFeedActive() {
  return velocityFeed.active;
}

static unsigned int getTimerLimit() {
  if (dupr == 0) {
    return 65535;
  }
  return min((long)65535, (long)(TIMER_FREQ / (z.motorSteps * abs(dupr) / z.screwPitch)) - 1);
}

void updateAsyncTimerSettings() {
  setDir(getAsyncAxis(), dupr > 0);
  timerAlarm(async_timer, getTimerLimit(), true, 0);
  timerWrite(async_timer, 0);
}

void IRAM_ATTR onAsyncTimer() {
  if (!isOn) return;

  // Modern Subsystem 2 Kinematic Velocity Feed
  if (velocityFeed.active) {
    Axis* a = velocityFeed.axis;
    if (!a || a->movingManually) return;

    int8_t dir = velocityFeed.direction;
    if (dir > 0 && a->pos < velocityFeed.targetPos) {
      if (a->pos <= a->motorPos) {
        a->pos++;
      }
      a->motorPos++;
      a->posGlobal++;
    } else if (dir < 0 && a->pos > velocityFeed.targetPos) {
      if (a->pos >= a->motorPos + a->backlashSteps) {
        a->pos--;
      }
      a->motorPos--;
      a->posGlobal--;
    } else {
      // Limit or target stroke reached
      velocityFeed.active = false;
      return;
    }

    DLOW(a->step);
    a->stepStartUs = micros();
    delayMicroseconds(10);
    DHIGH(a->step);
    return;
  }

  // Legacy fallback for LatheMode::ASYNC
  Axis* a = getAsyncAxis();
  if (a->movingManually || mode != LatheMode::ASYNC) {
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
  SettingsService::setStarts(value);
}

void setTurnPasses(int value) {
  SettingsService::setTurnPasses(value);
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

long normalizePitch(long pitch) {
  float scale = 1.0f;
  if (measure == MeasureUnit::METRIC) {
    scale = 100.0f;
  } else if (measure == MeasureUnit::INCH) {
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
  bool signReverse = isOn && (isGearboxMode() || mode == LatheMode::CONE) && dupr != 0 && nextDupr == -dupr;
  bool joystickSamePitchMagnitude = mode == LatheMode::JOYSTICK && abs(nextDupr) == abs(dupr);
  dupr = nextDupr;
  if (mode == LatheMode::SLOT) {
    return;
  } else if (joystickSamePitchMagnitude) {
    return;
  } else if (signReverse) {
    applyDuprSignReverse();
  } else {
    markOrigin();
    if (mode == LatheMode::JOYSTICK) {
      resetJoystickLatheFeed();
    }
  }
  if (mode == LatheMode::ASYNC) {
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
  if (mode == LatheMode::CONE) {
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

void setModeFromTask(LatheMode value) {
  SettingsService::requestMode(static_cast<int>(value));
}

void setModeFromLoop(LatheMode value) {
  if (mode == value) {
    return;
  }
  if (isOn) {
    setIsOnFromLoop(false);
  }
  if (mode == LatheMode::THREAD) {
    setStarts(1);
  } else if (mode == LatheMode::ASYNC) {
    setAsyncTimerEnable(false);
  } else if (mode == LatheMode::JOYSTICK) {
    resetJoystickLatheFeed();
  }
  mode = value;
  setupIndex = 0;

  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    if (value == LatheMode::NORMAL) {
      gb->setActiveAxis("Z");
      gb->setSync(true);
    } else if (value == LatheMode::XGEAR) {
      gb->setActiveAxis("X");
      gb->setSync(true);
    } else if (value == LatheMode::ASYNC) {
      gb->setActiveAxis("Z");
      gb->setSync(false);
    } else if (value == LatheMode::CONE) {
      gb->setActiveAxis("ZX");
      gb->setSync(true);
    }
  }

  if (mode == LatheMode::JOYSTICK) {
    resetJoystickLatheFeed();
  }
  if (mode == LatheMode::ASYNC) {
    if (!timerAttached) {
      timerAttached = true;
      timerAttachInterrupt(async_timer, &onAsyncTimer);
    }
    updateAsyncTimerSettings();
    setAsyncTimerEnable(true);
  }
}

void setModeFromUi(LatheMode modeToSet, bool eventFromNextion) {
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
    if (mode == LatheMode::THREAD) fraction = fraction * fraction;
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
      if (mode == LatheMode::THREAD && coneRatio != 0) {
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
      long auxTargetPos = (mode == LatheMode::THREAD ? auxStartStop : auxPos) + auxSafeDistance;
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
  if (dupr != 0 && (isGearboxMode() || mode == LatheMode::CONE)) {
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
  return measure == MeasureUnit::METRIC ? 10000.0 : 254000.0;
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

static void updateAxisSpeeds(long diffX, long diffZ) {
  if (diffX == 0 && diffZ == 0) return;
  long absX = abs(diffX);
  long absZ = abs(diffZ);
  float stepsPerSecX = gcodeFeedDuPerSec * x.motorSteps / x.screwPitch;
  float minStepsPerSecX = sysConfig.gcode.minFeedDuPerSec * x.motorSteps / x.screwPitch;
  if (stepsPerSecX > x.speedManualMove) stepsPerSecX = x.speedManualMove;
  else if (stepsPerSecX < minStepsPerSecX) stepsPerSecX = minStepsPerSecX;
  float stepsPerSecZ = gcodeFeedDuPerSec * z.motorSteps / z.screwPitch;
  float minStepsPerSecZ = sysConfig.gcode.minFeedDuPerSec * z.motorSteps / z.screwPitch;
  if (stepsPerSecZ > z.speedManualMove) stepsPerSecZ = z.speedManualMove;
  else if (stepsPerSecZ < minStepsPerSecZ) stepsPerSecZ = minStepsPerSecZ;
  float secX = absX / stepsPerSecX;
  float secZ = absZ / stepsPerSecZ;
  float sec = max(secX, secZ);
  x.speedMax = sec > 0 ? absX / sec : x.speedManualMove;
  z.speedMax = sec > 0 ? absZ / sec : z.speedManualMove;
  if (x.speedMax < minStepsPerSecX) x.speedMax = minStepsPerSecX;
  if (z.speedMax < minStepsPerSecZ) z.speedMax = minStepsPerSecZ;
}

static void gcodeWaitEpsilon(int epsilon) {
  while (isOn && (abs(x.pendingPos) > epsilon || abs(z.pendingPos) > epsilon || (sysConfig.gcode.spindlePausesGcode && getApproxRpm() < sysConfig.gcode.minRpm))) {
    taskYIELD();
  }
}

static void gcodeWaitNear() {
  gcodeWaitEpsilon(sysConfig.gcode.waitEpsilonSteps);
}

static void gcodeWaitStop() {
  gcodeWaitEpsilon(0);
}

static void G00_01(const String& command) {
  long xStart = x.pos;
  long zStart = z.pos;
  long xEnd = command.indexOf(x.name) >= 0 ? mmOrInchToAbsolutePos(&x, getFloat(command, x.name)) : xStart;
  long zEnd = command.indexOf(z.name) >= 0 ? mmOrInchToAbsolutePos(&z, getFloat(command, z.name)) : zStart;
  long xDiff = xEnd - xStart;
  long zDiff = zEnd - zStart;
  updateAxisSpeeds(xDiff, zDiff);
  long chunks = round(max(abs(xDiff), abs(zDiff)) * sysConfig.gcode.linearInterpolationPrecision);
  for (long i = 0; i < chunks; i++) {
    if (!isOn) return;
    float scale = i / float(chunks);
    stepToContinuous(&x, xStart + xDiff * scale);
    stepToContinuous(&z, zStart + zDiff * scale);
    gcodeWaitNear();
  }
  stepToFinal(&x, xEnd);
  stepToFinal(&z, zEnd);
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
  while (isOn && getApproxRpm() < sysConfig.gcode.minRpm) {
    taskYIELD();
  }
  return isOn;
}

static long gcodeWaitForThreadPhase(Axis* lead, long pitchDu) {
  if (!gcodeWaitForSpindle()) return -1;

  long lastPhase = spindleModulo(spindlePosGlobal - spindleFromPosWithPitch(lead, lead->posGlobal, pitchDu));
  while (isOn) {
    if (getApproxRpm() < sysConfig.gcode.minRpm) {
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
  long xEnd = command.indexOf(x.name) >= 0 ? mmOrInchToAbsolutePos(&x, getFloat(command, x.name)) : xStart;
  long zEnd = command.indexOf(z.name) >= 0 ? mmOrInchToAbsolutePos(&z, getFloat(command, z.name)) : zStart;
  long xDiff = xEnd - xStart;
  long zDiff = zEnd - zStart;

  Axis* lead = NULL;
  long leadDiff = 0;
  if (zDiff != 0) {
    lead = &z;
    leadDiff = zDiff;
  } else if (xDiff != 0) {
    lead = &x;
    leadDiff = xDiff;
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

  while (isOn) {
    long spindle = spindlePosAvg;
    bool finished = spindle >= spindleTarget;
    float progress = finished ? 1.0 : spindle / float(spindleTarget);
    if (progress < 0) progress = 0;
    else if (progress > 1) progress = 1;

    stepToContinuous(&x, round(xDiff * progress));
    stepToContinuous(&z, round(zDiff * progress));

    if (finished) break;
    taskYIELD();
  }

  if (!isOn) return true;
  stepToFinal(&x, xDiff);
  stepToFinal(&z, zDiff);
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
    SettingsService::setMeasure(op == 20 ? MeasureUnit::INCH : MeasureUnit::METRIC);
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

  if (!(code == 'G' && getInt(command, 'G') == 32)) {
    setFeedRate(command);
  }
  switch (code) {
    case 'G':
    case NAME_Z:
    case NAME_X: return handleGcode(command);
    case 'F': return true;
    case 'M': return handleMcode(command);
    case 'T': return true;
    default: writeBuffer(&outBuffer, "error: unsupported command "); writeBuffer(&outBuffer, code); writeBuffer(&outBuffer, "\n"); return false;
  }
  return false;
}

void taskGcode(void *param) {
  while (emergencyStop == EstopCode::NONE) {
    if (mode != LatheMode::GCODE) {
      gcodeInitialized = false;
    } else if (!gcodeInitialized) {
      gcodeInitialized = true;
      resetGcodeState();
    }
    char receivedChar = '\0';
    bool isWebSocket = false;
    if (mode == LatheMode::GCODE && isOn && gcodeProgramCharIndex < gcodeProgram.length()) {
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
    if (mode == LatheMode::GCODE && isOn && gcodeProgramCharIndex > 0 && gcodeProgramCharIndex == gcodeProgram.length()) {
      setIsOnFromTask(false);
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}

// --- Kinematic Core Domain APIs ---

bool setFeedrateDupr(long duprVal, bool preserveDirection) {
  return SettingsService::setFeedrateDupr(duprVal, preserveDirection);
}

bool setFeedrateMm(float pitchMm, bool preserveDirection) {
  return SettingsService::setFeedrateMm(pitchMm, preserveDirection);
}

bool setFeedrateTpi(float tpi, bool preserveDirection) {
  return SettingsService::setFeedrateTpi(tpi, preserveDirection);
}

bool setFeedrateInch(float pitchInch, bool preserveDirection) {
  return SettingsService::setFeedrateInch(pitchInch, preserveDirection);
}

bool reverseFeedDirection() {
  return SettingsService::reverseFeedDirection();
}

ThreadSetting getThreadSetting() {
  return SettingsService::getThreadSetting();
}

bool setTaperRatio(float ratioK) {
  return SettingsService::setTaperRatio(ratioK);
}

bool setTaperAngle(float angleDeg) {
  return SettingsService::setTaperAngle(angleDeg);
}

bool setTaperPreset(int presetIdx, float angleDeg, float ratioVal) {
  return SettingsService::setTaperPreset(presetIdx, angleDeg, ratioVal);
}

TaperSetting getTaperSetting() {
  return SettingsService::getTaperSetting();
}

bool setAxisZero(Axis* a) {
  if (!a) return false;
  markAxis0(a);
  return true;
}

bool setAxisPositionDu(Axis* a, long posDu) {
  if (!a) return false;
  long targetSteps = duToSteps(a, posDu);
  a->originPos = targetSteps - a->pos;
  return true;
}

bool setAxisLimitDu(Axis* a, bool isLeftOrUp, long limitDu) {
  if (!a) return false;
  AxisId axisId = (a == &x) ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeftOrUp ? LIMIT_LEFT_OR_UP : LIMIT_RIGHT_OR_DOWN;
  return SettingsService::setAxisLimitDu(axisId, dir, limitDu);
}

bool clearAxisLimit(Axis* a, bool isLeftOrUp) {
  if (!a) return false;
  AxisId axisId = (a == &x) ? AXIS_X : AXIS_Z;
  LimitDirection dir = isLeftOrUp ? LIMIT_LEFT_OR_UP : LIMIT_RIGHT_OR_DOWN;
  return SettingsService::clearAxisLimit(axisId, dir);
}

bool setOperatingMode(LatheMode newMode) {
  return SettingsService::requestMode(static_cast<int>(newMode));
}

bool setOperatingMode(int newMode) {
  return SettingsService::requestMode(newMode);
}

bool setFeedStepDu(long stepDu) {
  if (stepDu <= 0) return false;
  feedStepDu = stepDu;
  operatorPrefs.feedStepDu = stepDu;
  return true;
}

long getFeedStepDu() {
  return feedStepDu;
}

void cycleFeedStep() {
  if (measure == MeasureUnit::METRIC) {
    if (feedStepDu == FEED_STEPS_METRIC[0]) feedStepDu = FEED_STEPS_METRIC[1];
    else if (feedStepDu == FEED_STEPS_METRIC[1]) feedStepDu = FEED_STEPS_METRIC[2];
    else if (feedStepDu == FEED_STEPS_METRIC[2]) feedStepDu = FEED_STEPS_METRIC[3];
    else feedStepDu = FEED_STEPS_METRIC[0];
  } else {
    if (feedStepDu == FEED_STEPS_IMPERIAL[0]) feedStepDu = FEED_STEPS_IMPERIAL[1];
    else if (feedStepDu == FEED_STEPS_IMPERIAL[1]) feedStepDu = FEED_STEPS_IMPERIAL[2];
    else if (feedStepDu == FEED_STEPS_IMPERIAL[2]) feedStepDu = FEED_STEPS_IMPERIAL[3];
    else feedStepDu = FEED_STEPS_IMPERIAL[0];
  }
  operatorPrefs.feedStepDu = feedStepDu;
}

