#include "Input.h"
#include "Config.h"
#include "Axis.h"
#include "Debug.h"
#include "Display.h"
#include "Motion.h"
#include "WebServerModule.h"
#include "UsbKeyboard.h"
#include "SettingsService.h"

// Keypad & button states
bool buttonLeftPressed = false;
bool buttonRightPressed = false;
bool buttonUpPressed = false;
bool buttonDownPressed = false;
bool buttonOffPressed = false;
bool buttonBackPressed = false;
bool buttonForwardPressed = false;

unsigned long keypadTimeUs = 0;
PS2KeyAdvanced keyboard;

// Numpad state
bool inNumpad = false;
int numpadDigits[20] = {0};
int numpadIndex = 0;

// Spindle timing
unsigned long spindleEncTime = 0;
unsigned long spindleEncTimeDiffBulk = 0;
unsigned long spindleEncTimeAtIndex0 = 0;
int spindleEncTimeIndex = 0;

// Joystick State
JoystickAxisState joystickAxes[] = {
  {&z, JOY_Z, &JOYSTICK_Z_ENABLED, &INVERT_JOYSTICK_Z, 0, 0, 0, 2048, 2048, 0, 0, 0},
  {&x, JOY_X, &JOYSTICK_X_ENABLED, &INVERT_JOYSTICK_X, 0, 0, 0, 2048, 2048, 0, 0, 0},
  {&y, JOY_Y, &JOYSTICK_Y_ENABLED, &INVERT_JOYSTICK_Y, 0, 0, 0, 2048, 2048, 0, 0, 0},
};
const int JOYSTICK_AXIS_COUNT = sizeof(joystickAxes) / sizeof(joystickAxes[0]);

portMUX_TYPE joystickPulseMux = portMUX_INITIALIZER_UNLOCKED;
volatile bool joystickRapidPressed = false;
volatile int joystickLatheDirectionZ = 0;
volatile int joystickLatheDirectionX = 0;
volatile bool joystickLatheRapid = false;
volatile int joystickPitchAdjustDirection = 0;
volatile int joystickPitchStatusDirection = 0;
volatile unsigned long joystickPitchStatusMillis = 0;
bool joystickAvailable = false;
float joystickPitchChangeFraction = 0;
unsigned long joystickSampleTimeUs = 0;
int joystickLatheFeedSignZ = 0;
int joystickLatheFeedSignX = 0;
long joystickLathePitchZ = 0;
long joystickLathePitchX = 0;
bool joystickLatheThreadLocked = false;
int joystickLatheSyncAxis = 0;
long joystickLatheSyncPitch = 0;
bool joystickLatheRebaseZAfterSync = false;
bool joystickLatheRebaseXAfterSync = false;
bool joystickButtonRawPressed = false;
bool joystickButtonPressed = false;
bool joystickButtonToggleBlocked = false;
unsigned long joystickButtonRawChangeMillis = 0;
unsigned long joystickButtonPressMillis = 0;
String joystickStartupWarning = "";

static unsigned long multistartPressMillis = 0;

// Numpad implementation
bool numpadNegative = false;

long getNumpadResult() {
  long result = 0;
  for (int i = 0; i < numpadIndex; i++) {
    result = result * 10 + numpadDigits[i];
  }
  if (numpadNegative) {
    result = -result;
  }
  return result;
}

float numpadToConeRatio() {
  return getNumpadResult() / 100000.0;
}

long numpadToDeciMicrons() {
  long result = getNumpadResult();
  if (result == 0) {
    return 0;
  }
  if (measure == MEASURE_INCH) {
    result = result * 254;
  } else if (measure == MEASURE_TPI) {
    result = round(254000.0 / result);
  } else { // Metric
    result = result * 10;
  }
  return result;
}

void resetNumpad() {
  numpadIndex = 0;
  numpadNegative = false;
}

void trimLeadingNumpadZeros() {
  while (numpadIndex > 1 && numpadDigits[0] == 0) {
    for (int i = 1; i < numpadIndex; i++) {
      numpadDigits[i - 1] = numpadDigits[i];
    }
    numpadIndex--;
  }
}

void numpadPress(int digit) {
  if (!inNumpad) {
    numpadIndex = 0;
  }
  numpadDigits[numpadIndex] = digit;
  if (numpadIndex < 7) {
    numpadIndex++;
  } else {
    numpadIndex = 0;
  }
}

void numpadBackspace() {
  if (inNumpad && numpadIndex > 0) {
    numpadIndex--;
  }
}

void numpadPlusMinus(bool plus) {
  if (numpadIndex == 0) {
    return;
  }
  if (plus) {
    for (int i = numpadIndex - 1; i >= 0; i--) {
      if (numpadDigits[i] < 9) {
        numpadDigits[i]++;
        trimLeadingNumpadZeros();
        return;
      }
      numpadDigits[i] = 0;
    }
    if (numpadIndex < 7) {
      numpadDigits[numpadIndex] = 0;
      numpadDigits[0] = 1;
      numpadIndex++;
    } else {
      for (int i = 0; i < numpadIndex; i++) {
        numpadDigits[i] = 9;
      }
    }
    return;
  }
  if (getNumpadResult() <= 1) {
    return;
  }
  for (int i = numpadIndex - 1; i >= 0; i--) {
    if (numpadDigits[i] > 0) {
      numpadDigits[i]--;
      trimLeadingNumpadZeros();
      return;
    }
    numpadDigits[i] = 9;
  }
}

bool processNumpadResult(int keyCode) {
  long newDu = numpadToDeciMicrons();
  float newConeRatio = numpadToConeRatio();
  long numpadResult = getNumpadResult();
  resetNumpad();
  // Ignore numpad input unless confirmed with ON.
  if (keyCode == B_ON) {
    if (numpadEntryMode == DIAMETER) {
      float divisor = (measure == MEASURE_METRIC) ? 100.0f : 1000.0f;
      float desiredValue = numpadResult / divisor;

      if (xDisplayModeDRO) {
        desiredValue /= 2.0f;
        if (measure != MEASURE_METRIC) desiredValue *= 25.4f;
        xScale.setPosition(desiredValue);
      } else {
        float targetRadiusMm = desiredValue / 2.0f;
        if (measure != MEASURE_METRIC) targetRadiusMm = (desiredValue * 25.4f) / 2.0f;
        
        long targetSteps = round((targetRadiusMm * 10000.0f) * x.motorSteps / x.screwPitch);
        
        // Fix for wrong direction bug: Motorized X-axis should set origin such that getAxisPosDu aligns with target
        // getAxisPosDu = stepsToDu(x.pos + x.originPos)
        // targetRadiusMm * 10000.0f is the du value.
        // stepsToDu(x.pos + originPos) = targetRadiusMm * 10000.0f
        // x.pos + originPos = targetSteps
        x.originPos = targetSteps - x.pos;
      }
      numpadEntryMode = PITCH;
      return true;
    } else if (numpadEntryMode == LENGTH) {
      float divisor = (measure == MEASURE_METRIC) ? 100.0f : 1000.0f;
      float desiredValue = numpadResult / divisor;

      if (zDisplayModeDRO) {
        if (measure != MEASURE_METRIC) desiredValue *= 25.4f;
        zScale.setPosition(desiredValue);
      } else {
        float targetMm = desiredValue;
        if (measure != MEASURE_METRIC) targetMm = desiredValue * 25.4f;
        long targetSteps = round((targetMm * 10000.0f) * z.motorSteps / z.screwPitch);
        z.originPos = targetSteps - z.pos;
      }
      numpadEntryMode = PITCH;
      return true;
    }
    
    if (isPassMode() && setupIndex == 1) {
      setTurnPasses(int(min((long)PASSES_MAX, numpadResult)));
      setupIndex++;
    } else if (mode == MODE_THREAD && setupIndex == 3) {
      setConeRatio(newConeRatio);
      setupIndex++;
    } else if (mode == MODE_CONE && setupIndex == 1) {
      setConeRatio(newConeRatio);
      setupIndex++;
    } else {
      if (abs(newDu) <= DUPR_MAX) {
        setDupr(newDu);
      }
    }
    // Don't use this ON press for starting the motion.
    return true;
  }

  // Shared piece for stops and moves.
  Axis* a = (keyCode == B_STOPL || keyCode == B_STOPR || keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_Z) ? &z : &x;
  int sign = ((keyCode == B_STOPL || keyCode == B_STOPU || keyCode == B_LEFT || keyCode == B_UP || keyCode == B_Z || keyCode == B_X || keyCode == B_X_ENA) ? 1 : -1);
  if (keyCode == B_STOPF || keyCode == B_STOPB || keyCode == B_FORWARD || keyCode == B_BACK || keyCode == B_Y) {
    a = &y;
    sign = (keyCode == B_BACK || keyCode == B_STOPB) ? -1 : 1;
  }
  long posDiffAbs = (a->rotational ? numpadResult * 10 : newDu) / a->screwPitch * a->motorSteps;
  long pos = a->pos + posDiffAbs * sign;

  // Potentially assign a new value to a limit. Treat newDu as a relative distance from current position.
  if (keyCode == B_STOPL) {
    setLeftStop(&z, pos);
    return true;
  } else if (keyCode == B_STOPR) {
    setRightStop(&z, pos);
    return true;
  } else if (keyCode == B_STOPU) {
    setLeftStop(&x, pos);
    return true;
  } else if (keyCode == B_STOPD) {
    setRightStop(&x, pos);
    return true;
  } else if (keyCode == B_STOPF && ACTIVE_Y) {
    setLeftStop(&y, pos);
    return true;
  } else if (keyCode == B_STOPB && ACTIVE_Y) {
    setRightStop(&y, pos);
    return true;
  }

  // Potentially move by newDu in the given direction.
  // We don't support precision manual moves when ON yet. Can't stay in the thread for most modes.
  if (!isOn && (keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_UP || keyCode == B_DOWN || keyCode == B_FORWARD || keyCode == B_BACK)) {
    if (pos < a->rightStop) {
      pos = a->rightStop;
      beep();
    } else if (pos > a->leftStop) {
      pos = a->leftStop;
      beep();
    } else if (abs(pos - a->pos) > a->estopSteps) {
      beep();
      return true;
    }
    a->speedMax = a->speedManualMove;
    stepToFinal(a, pos);
    return true;
  }

  // Set axis 0 newDu ahead.
  if (keyCode == B_Z || keyCode == B_X || keyCode == B_Y) {
    a->originPos = -pos;
    return true;
  }

  // Set X axis 0 from diameter.
  if (keyCode == B_DIAMETER || keyCode == B_X_ENA) {
    a->originPos = -a->pos - posDiffAbs / 2;
    return true;
  }

  if (keyCode == B_STEP) {
    if (newDu > 0) {
      SettingsService::setJogStepDu(newDu);
    } else {
      beep();
    }
    return true;
  }

  return false;
}

bool processNumpad(int keyCode) {
  if (keyCode == B_0) {
    numpadPress(0);
    inNumpad = true;
  } else if (keyCode == B_1) {
    numpadPress(1);
    inNumpad = true;
  } else if (keyCode == B_2) {
    numpadPress(2);
    inNumpad = true;
  } else if (keyCode == B_3) {
    numpadPress(3);
    inNumpad = true;
  } else if (keyCode == B_4) {
    numpadPress(4);
    inNumpad = true;
  } else if (keyCode == B_5) {
    numpadPress(5);
    inNumpad = true;
  } else if (keyCode == B_6) {
    numpadPress(6);
    inNumpad = true;
  } else if (keyCode == B_7) {
    numpadPress(7);
    inNumpad = true;
  } else if (keyCode == B_8) {
    numpadPress(8);
    inNumpad = true;
  } else if (keyCode == B_9) {
    numpadPress(9);
    inNumpad = true;
  } else if (keyCode == B_BACKSPACE) {
    numpadBackspace();
    inNumpad = true;
  } else if (inNumpad && (keyCode == B_PLUS || keyCode == B_MINUS)) {
    if (numpadIndex == 0 && keyCode == B_MINUS) {
      numpadNegative = !numpadNegative;
      return true;
    }
    numpadPlusMinus(keyCode == B_PLUS);
    return true;
  } else if (inNumpad) {
    inNumpad = false;
    return processNumpadResult(keyCode);
  }
  return inNumpad;
}

// Spindle functions
long spindleModulo(long value) {
  value = value % ENCODER_STEPS_INT;
  if (value < 0) {
    value += ENCODER_STEPS_INT;
  }
  return value;
}

#if ENABLE_SPINDLE_SIM
// ---------------------------------------------------------------------------
// Dev spindle simulation (driven by CMD.spindleSim / CMD.spindleSimTurn).
// Replaces the real encoder count source while active; everything downstream
// of the count delta (spindlePos, RPM timing, sync) is untouched.
// ---------------------------------------------------------------------------
static volatile bool spindleSimActive = false;
static volatile float spindleSimRpm = 0.0f;          // signed; + = same direction as encoder counts up
static volatile long spindleSimManualCounts = 0;     // pending hand-rotation counts
static volatile unsigned long spindleSimHeartbeatMs = 0;
static bool spindleSimResync = false;                // re-sync the PCNT baseline when leaving override
static unsigned long spindleSimLastMicros = 0;
static float spindleSimFraction = 0.0f;

void spindleSimSet(bool active, float rpm) {
  spindleSimRpm = rpm;
  spindleSimHeartbeatMs = millis();
  if (active && !spindleSimActive) {
    spindleSimLastMicros = micros();
    spindleSimFraction = 0.0f;
    spindleSimManualCounts = 0;
  }
  if (!active && spindleSimActive) {
    spindleSimResync = true;
  }
  spindleSimActive = active;
}

void spindleSimTurnDeg(float deg) {
  if (!spindleSimActive) return;
  long counts = lroundf(deg / 360.0f * (float) ENCODER_STEPS_INT);
  __atomic_fetch_add(&spindleSimManualCounts, counts, __ATOMIC_RELAXED);
  spindleSimHeartbeatMs = millis();
}

bool spindleSimIsActive() { return spindleSimActive; }
float spindleSimGetRpm() { return spindleSimActive ? spindleSimRpm : 0.0f; }

static int spindleSimNextDelta() {
  unsigned long nowUs = micros();
  float dt = (float)(nowUs - spindleSimLastMicros) * 1e-6f;
  spindleSimLastMicros = nowUs;
  if (dt > 0.1f) dt = 0.1f;
  if (millis() - spindleSimHeartbeatMs > SPINDLE_SIM_TIMEOUT_MS) {
    // Visualizer went away: hand control back to the real encoder.
    spindleSimActive = false;
    spindleSimRpm = 0.0f;
    spindleSimResync = true;
    return 0;
  }
  float counts = spindleSimRpm / 60.0f * (float) ENCODER_STEPS_INT * dt + spindleSimFraction;
  int delta = (int) counts;            // truncate toward zero, carry the remainder
  spindleSimFraction = counts - (float) delta;
  long pending = __atomic_load_n(&spindleSimManualCounts, __ATOMIC_RELAXED);
  if (pending != 0) {
    // Spread hand rotation over ~20 ms so ~30-50 Hz updates look smooth.
    float k = dt / 0.02f;
    if (k > 1.0f) k = 1.0f;
    long take = lroundf((float) pending * k);
    if (take == 0) take = pending > 0 ? 1 : -1;
    if (labs(take) > labs(pending)) take = pending;
    __atomic_fetch_sub(&spindleSimManualCounts, take, __ATOMIC_RELAXED);
    delta += (int) take;
  }
  return delta;
}
#endif

void processSpindleCounter() {
  int delta = 0;
#if ENABLE_SPINDLE_SIM
  if (spindleSimActive) {
    delta = spindleSimNextDelta();
    if (delta == 0) {
      return;
    }
  } else
#endif
  {
    if (!pulseCountersReady) {
      return;
    }
    pcnt_unit_handle_t unit = pulseUnits[PULSE_COUNTER_SPINDLE];
    if (!unit) {
      return;
    }
#if ENABLE_SPINDLE_SIM
    if (spindleSimResync) {
      pcnt_unit_clear_count(unit);
      spindleCount = 0;
      spindleSimResync = false;
      return;
    }
#endif
    int count = 0;
    pcnt_unit_get_count(unit, &count);
    delta = count - spindleCount;
    if (delta == 0) {
      return;
    }
    if (count >= PCNT_CLEAR || count <= -PCNT_CLEAR) {
      pcnt_unit_clear_count(unit);
      spindleCount = 0;
    } else {
      spindleCount = count;
    }
  }

  unsigned long microsNow = micros();
  if (spindleEncTimeIndex >= RPM_BULK) {
    spindleEncTimeDiffBulk = microsNow - spindleEncTimeAtIndex0;
    spindleEncTimeAtIndex0 = microsNow;
    spindleEncTimeIndex = 0;
  }
  spindleEncTimeIndex += abs(delta);

  spindlePos += delta;
  spindlePosGlobal += delta;
  if (spindlePosGlobal > ENCODER_STEPS_INT) {
    spindlePosGlobal -= ENCODER_STEPS_INT;
  } else if (spindlePosGlobal < 0) {
    spindlePosGlobal += ENCODER_STEPS_INT;
  }
  if (spindlePos > spindlePosAvg) {
    spindlePosAvg = spindlePos;
  } else if (spindlePos < spindlePosAvg - ENCODER_BACKLASH) {
    spindlePosAvg = spindlePos + ENCODER_BACKLASH;
  }
  spindleEncTime = microsNow;

  if (spindlePosSync != 0) {
    spindlePosSync += delta;
    if (spindlePosSync % ENCODER_STEPS_INT == 0) {
      spindlePosSync = 0;
      if (mode == MODE_JOYSTICK && joystickLatheSyncPitch != 0) {
        Axis* a = joystickLatheSyncAxis == 1 ? &x : &z;
        spindlePosAvg = spindlePos = spindleFromPosWithModePitch(a, a->pos, joystickLatheSyncPitch);
        joystickLatheSyncPitch = 0;
      } else {
        Axis* a = getPitchAxis();
        spindlePosAvg = spindlePos = spindleFromPos(a, a->pos);
      }
    }
  }
}

// Joystick implementation
bool joystickAxisEnabled(JoystickAxisState* joystickAxis) {
  return joystickAxis->enabled == nullptr || *joystickAxis->enabled;
}

bool anyJoystickAxisEnabled() {
  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (joystickAxisEnabled(&joystickAxes[i])) return true;
  }
  return false;
}

void resetJoystickLatheFeedPosition() {
  joystickLatheFeedSignZ = 0;
  joystickLatheFeedSignX = 0;
  joystickLathePitchZ = 0;
  joystickLathePitchX = 0;
  joystickLatheRebaseZAfterSync = false;
  joystickLatheRebaseXAfterSync = false;
}

void cancelJoystickLatheSync() {
  if (mode == MODE_JOYSTICK) {
    spindlePosSync = 0;
    joystickLatheSyncPitch = 0;
    joystickLatheRebaseZAfterSync = false;
    joystickLatheRebaseXAfterSync = false;
  }
}

bool joystickUsable() {
  return JOYSTICK_ENABLED && joystickAvailable;
}

void prepareJoystickLatheManualMove() {
  if (isOn && mode == MODE_JOYSTICK) {
    cancelJoystickLatheSync();
    resetJoystickLatheFeedPosition();
  }
}

bool isContinuousStep() {
  if (!ENABLE_CONTINUOUS_MOVE) return false;
  return moveStep == (measure == MEASURE_METRIC ? MOVE_STEP_1 : MOVE_STEP_IMP_1);
}

bool isRapidManualMove() {
  return joystickUsable() && joystickRapidPressed;
}

static int joystickDirectionFromDeflection(float deflection) {
  return deflection > 0 ? 1 : (deflection < 0 ? -1 : 0);
}

JoystickAxisState* getJoystickAxisState(Axis* a) {
  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (joystickAxes[i].axis == a) return &joystickAxes[i];
  }
  return nullptr;
}

int getJoystickManualDirection(Axis* a) {
  JoystickAxisState* joystickAxis = getJoystickAxisState(a);
  return joystickAxis == nullptr ? 0 : joystickAxis->manualDirection;
}

long getJoystickManualSpeed(Axis* a) {
  JoystickAxisState* joystickAxis = getJoystickAxisState(a);
  return joystickAxis == nullptr ? 0 : joystickAxis->manualSpeed;
}

bool isJoystickManualMove(Axis* a) {
  return joystickUsable() && getJoystickManualDirection(a) != 0;
}

volatile int* getJoystickPulseQueue(Axis* a) {
  JoystickAxisState* joystickAxis = getJoystickAxisState(a);
  return joystickAxis == nullptr ? nullptr : &joystickAxis->queuedPulses;
}

void addJoystickPulses(Axis* a, int delta) {
  if (delta == 0) return;
  volatile int* pulseQueue = getJoystickPulseQueue(a);
  if (pulseQueue == nullptr) return;
  portENTER_CRITICAL(&joystickPulseMux);
  int queued = *pulseQueue + delta;
  if (queued > JOYSTICK_PULSE_QUEUE_LIMIT) {
    queued = JOYSTICK_PULSE_QUEUE_LIMIT;
  } else if (queued < -JOYSTICK_PULSE_QUEUE_LIMIT) {
    queued = -JOYSTICK_PULSE_QUEUE_LIMIT;
  }
  *pulseQueue = queued;
  portEXIT_CRITICAL(&joystickPulseMux);
}

static void clearJoystickPulses(JoystickAxisState* joystickAxis) {
  portENTER_CRITICAL(&joystickPulseMux);
  joystickAxis->queuedPulses = 0;
  portEXIT_CRITICAL(&joystickPulseMux);
}

static void cancelPendingJoystickMove(JoystickAxisState* joystickAxis, int previousDirection) {
  if (previousDirection == 0) return;
  clearJoystickPulses(joystickAxis);
  joystickAxis->pulseFraction = 0;

  Axis* axis = joystickAxis->axis;
  if (xSemaphoreTake(axis->mutex, 10) != pdTRUE) return;
  int pendingDirection = axis->pendingPos > 0 ? 1 : (axis->pendingPos < 0 ? -1 : 0);
  if (pendingDirection == previousDirection) {
    axis->pendingPos = 0;
    axis->fractionalPos = 0;
    axis->continuous = false;
    axis->speed = axis->speedStart;
  }
  xSemaphoreGive(axis->mutex);
}

int getAndResetJoystickPulses(Axis* a) {
  volatile int* pulseQueue = getJoystickPulseQueue(a);
  if (pulseQueue == nullptr) return 0;
  portENTER_CRITICAL(&joystickPulseMux);
  int delta = *pulseQueue;
  *pulseQueue = 0;
  portEXIT_CRITICAL(&joystickPulseMux);
  return delta;
}

static int readJoystickPin(int pin) {
  long total = 0;
  for (int i = 0; i < JOYSTICK_OVERSAMPLES; i++) {
    total += analogRead(pin);
  }
  return total / JOYSTICK_OVERSAMPLES;
}

static int calibrateJoystickCenter(int pin) {
  long total = 0;
  for (int i = 0; i < JOYSTICK_CENTER_SAMPLES; i++) {
    total += readJoystickPin(pin);
    DELAY(2);
  }
  return total / JOYSTICK_CENTER_SAMPLES;
}

static float getJoystickDeflection(JoystickAxisState* joystickAxis) {
  int raw = readJoystickPin(joystickAxis->pin);
  int rawDelta = raw - joystickAxis->center;
  int filteredDelta = joystickAxis->filtered - joystickAxis->center;
  if (*joystickAxis->invert) {
    rawDelta = -rawDelta;
    filteredDelta = -filteredDelta;
  }

  bool rawNeutral = abs(rawDelta) <= JOYSTICK_DEADBAND;
  bool filteredNeutral = abs(filteredDelta) <= JOYSTICK_DEADBAND;
  bool rawReversed = !rawNeutral && !filteredNeutral && ((rawDelta > 0) != (filteredDelta > 0));
  if (rawNeutral) {
    joystickAxis->filtered = joystickAxis->center;
  } else if (filteredNeutral || rawReversed) {
    joystickAxis->filtered = raw;
  } else {
    joystickAxis->filtered = (joystickAxis->filtered * 3 + raw) / 4;
  }

  int delta = joystickAxis->filtered - joystickAxis->center;
  if (*joystickAxis->invert) delta = -delta;
  int absDelta = abs(delta);
  if (absDelta <= JOYSTICK_DEADBAND) return 0;

  int range = delta > 0 ? JOYSTICK_ADC_MAX - joystickAxis->center : joystickAxis->center;
  range = max(1, range - JOYSTICK_DEADBAND);
  float normalized = (absDelta - JOYSTICK_DEADBAND) / float(range);
  if (normalized > 1.0) normalized = 1.0;
  float curved = normalized * normalized;
  return delta > 0 ? curved : -curved;
}

static int getJoystickPulseDelta(JoystickAxisState* joystickAxis, unsigned long elapsedUs) {
  float revolutionsPerSecond = joystickRapidPressed ? JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND : JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND;
  joystickAxis->pulseFraction += joystickAxis->deflection * joystickAxis->axis->pulsePerRevolution * revolutionsPerSecond * elapsedUs / 1000000.0;
  int delta = int(joystickAxis->pulseFraction);
  joystickAxis->pulseFraction -= delta;
  return delta;
}

static long getJoystickSpeedFromDeflection(JoystickAxisState* joystickAxis) {
  if (joystickAxis->deflection == 0) return 0;
  float revolutionsPerSecond = joystickRapidPressed ? JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND : JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND;
  long speed = round(abs(joystickAxis->deflection) * revolutionsPerSecond * joystickAxis->axis->motorSteps);
  if (speed < 1) speed = 1;
  return min((long)joystickAxis->axis->speedManualMove, speed);
}

static void readJoystickAxis(JoystickAxisState* joystickAxis) {
  if (!joystickAxisEnabled(joystickAxis)) {
    joystickAxis->deflection = 0;
    joystickAxis->direction = 0;
    return;
  }
  joystickAxis->deflection = getJoystickDeflection(joystickAxis);
  joystickAxis->direction = joystickDirectionFromDeflection(joystickAxis->deflection);
}

static void updateJoystickManualAxis(JoystickAxisState* joystickAxis, bool enabled, bool queueWhenNeutral, unsigned long elapsedUs) {
  int previousDirection = joystickAxis->manualDirection;
  joystickAxis->manualDirection = enabled ? joystickAxis->direction : 0;
  joystickAxis->manualSpeed = joystickAxis->manualDirection == 0 ? 0 : getJoystickSpeedFromDeflection(joystickAxis);
  if (previousDirection != 0 && previousDirection != joystickAxis->manualDirection) {
    cancelPendingJoystickMove(joystickAxis, previousDirection);
  }
  if (enabled && (queueWhenNeutral || joystickAxis->manualDirection != 0)) {
    addJoystickPulses(joystickAxis->axis, getJoystickPulseDelta(joystickAxis, elapsedUs));
  } else {
    joystickAxis->pulseFraction = 0;
  }
}

static void resetJoystickAxisMotion(JoystickAxisState* joystickAxis) {
  int previousDirection = joystickAxis->manualDirection;
  joystickAxis->manualDirection = 0;
  joystickAxis->manualSpeed = 0;
  cancelPendingJoystickMove(joystickAxis, previousDirection);
  clearJoystickPulses(joystickAxis);
  joystickAxis->pulseFraction = 0;
}

static bool joystickStartupCenterLooksNeutral(int center) {
  int midpoint = JOYSTICK_ADC_MAX / 2;
  int tolerance = max(JOYSTICK_DEADBAND, JOYSTICK_STARTUP_CENTER_TOLERANCE_MIN);
  return abs(center - midpoint) <= tolerance;
}

static bool updateJoystickButton(bool rawPressed, bool moveNeutral) {
  unsigned long now = millis();
  if (rawPressed != joystickButtonRawPressed) {
    joystickButtonRawPressed = rawPressed;
    joystickButtonRawChangeMillis = now;
  }

  if (rawPressed != joystickButtonPressed && now - joystickButtonRawChangeMillis >= JOYSTICK_BUTTON_DEBOUNCE_MS) {
    joystickButtonPressed = rawPressed;
    if (joystickButtonPressed) {
      joystickButtonPressMillis = now;
      joystickButtonToggleBlocked = mode != MODE_JOYSTICK || !moveNeutral;
    } else {
      bool shortClick = now - joystickButtonPressMillis <= JOYSTICK_BUTTON_TOGGLE_MAX_MS;
      if (JOYSTICK_BUTTON_TOGGLES_ON_OFF && mode == MODE_JOYSTICK && moveNeutral && shortClick && !joystickButtonToggleBlocked) {
        buttonOnOffPress(!isOn);
      }
      joystickButtonToggleBlocked = false;
    }
  }

  if (joystickButtonPressed && (mode != MODE_JOYSTICK || !moveNeutral || now - joystickButtonPressMillis > JOYSTICK_BUTTON_TOGGLE_MAX_MS)) {
    joystickButtonToggleBlocked = true;
  }

  return joystickButtonPressed;
}

void initJoystick() {
  joystickAvailable = false;
  joystickStartupWarning = "";
  if (!JOYSTICK_ENABLED) return;
  if (!anyJoystickAxisEnabled()) return;
  analogReadResolution(12);
  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (!joystickAxisEnabled(&joystickAxes[i])) continue;
    pinMode(joystickAxes[i].pin, INPUT_PULLDOWN);
  }
  pinMode(JOY_BUTTON, INPUT_PULLUP);

  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (!joystickAxisEnabled(&joystickAxes[i])) continue;
    joystickAxes[i].center = calibrateJoystickCenter(joystickAxes[i].pin);
    if (!joystickStartupCenterLooksNeutral(joystickAxes[i].center)) {
      int offset = joystickAxes[i].center - JOYSTICK_ADC_MAX / 2;
      char axisName = i == JOYSTICK_AXIS_Z ? NAME_Z : (i == JOYSTICK_AXIS_X ? NAME_X : NAME_Y);
      joystickStartupWarning = "Joy ";
      joystickStartupWarning += axisName;
      joystickStartupWarning += (offset >= 0 ? " +" : " -");
      joystickStartupWarning += String(max(0, abs(offset) - JOYSTICK_DEADBAND));
      joystickStartupWarning += " past deadband";
      return;
    }
  }

  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (!joystickAxisEnabled(&joystickAxes[i])) {
      joystickAxes[i].center = JOYSTICK_ADC_MAX / 2;
      joystickAxes[i].filtered = joystickAxes[i].center;
      joystickAxes[i].deflection = 0;
      joystickAxes[i].direction = 0;
      joystickAxes[i].manualDirection = 0;
      joystickAxes[i].manualSpeed = 0;
      joystickAxes[i].queuedPulses = 0;
      joystickAxes[i].pulseFraction = 0;
      continue;
    }
    joystickAxes[i].filtered = joystickAxes[i].center;
  }
  joystickSampleTimeUs = micros();
  joystickAvailable = true;
}

void resetJoystickLatheFeed() {
  resetJoystickLatheFeedPosition();
  joystickLatheDirectionZ = 0;
  joystickLatheDirectionX = 0;
  joystickLatheRapid = false;
  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    resetJoystickAxisMotion(&joystickAxes[i]);
  }
  joystickPitchAdjustDirection = 0;
  joystickPitchStatusDirection = 0;
  joystickPitchStatusMillis = 0;
  joystickLatheThreadLocked = false;
  joystickLatheSyncPitch = 0;
  joystickPitchChangeFraction = 0;
}

bool joystickLatheFeedCommandActive() {
  return isOn && dupr != 0 && !joystickLatheRapid && (joystickLatheDirectionZ != 0 || joystickLatheDirectionX != 0);
}

bool manualMovesAllowedWhenOn() {
  return isGearboxMode() || mode == MODE_ASYNC || mode == MODE_CONE || mode == MODE_Y || mode == MODE_JOYSTICK;
}

bool manualMovesIgnoredWhenOn() {
  return mode == MODE_GCODE || isPassMode();
}

static bool joystickPitchAdjustmentAllowed() {
  if (mode == MODE_GCODE) return false;
  if (!isOn) return true;
  return mode != MODE_THREAD && mode != MODE_ELLIPSE;
}

int getJoystickPitchStatusDirection() {
  int direction = joystickPitchAdjustDirection;
  if (direction != 0) return direction;
  if (millis() - joystickPitchStatusMillis < JOYSTICK_PITCH_STATUS_HOLD_MS) {
    return joystickPitchStatusDirection;
  }
  return 0;
}

bool isGearboxManualMove(Axis* a) {
  return isGearboxMode() && a == getPitchAxis();
}

// Nextion byte conversion and message processing
static bool checkForTerminator() {
  if (nextionBufferIndex < 3) return false;
  return nextionBuffer[nextionBufferIndex - 3] == 0xFF &&
      nextionBuffer[nextionBufferIndex - 2] == 0xFF &&
      nextionBuffer[nextionBufferIndex - 1] == 0xFF;
}

static const byte HEX_TO_KEYCODE[256] = {
  [0] = 0,
  [1] = 0,
  [2] = 0,
  [3] = B_OFF,
  [4] = B_MODE,
  [5] = B_REVERSE,
  [6] = B_MEASURE,
  [7] = B_STEP,
  [8] = B_DISPL,
  [9] = B_OFF, // tTurns
  [10] = B_OFF, // tAngle
  [11] = B_X_ENA,
  [12] = B_X,
  [13] = B_X_TOGGLE_DRO,
  [14] = 0,
  [15] = B_Y_ENA,
  [16] = 0,
  [17] = B_Y,
  [18] = 0,
  [19] = B_Z_ENA,
  [20] = 0,
  [21] = B_Z,
  [22] = B_Z_TOGGLE_DRO,
  [23] = B_OFF,
  [24] = B_BACKSPACE,
  [25] = B_ON,
  [26] = B_0,
  [27] = B_1,
  [28] = B_2,
  [29] = B_3,
  [30] = B_4,
  [31] = B_5,
  [32] = B_6,
  [33] = B_7,
  [34] = B_8,
  [35] = B_9,
  [36] = B_STOPU,
  [37] = B_STOPD,
  [38] = B_STOPF,
  [39] = B_STOPB,
  [40] = B_STOPL,
  [41] = B_STOPR,
  [42] = B_PLUS,
  [43] = B_MINUS,
  [44] = B_UP,
  [45] = B_DOWN,
  [46] = B_FORWARD,
  [47] = B_BACK,
  [48] = B_LEFT,
  [49] = B_RIGHT,
  [50] = B_MULTISTART,
  [51] = B_KNOWN_DIA,
  [52] = B_KNOWN_LENGTH,
  [53] = B_DIA_RAD,
  [54] = B_ABS_INC,
  [55] = B_VERSION,
  [56] = B_SPINDLE_UNITS,
  [57] = B_PITCH,
};

static int processNextionMessage() {
  lastNextionPageId = 255;
  if (nextionBufferIndex < 6) return 0;
  if (nextionBuffer[0] == 0x65) {
    byte pageId = nextionBuffer[1];
    int code = 0;
    if (pageId == 0x00) {
      code = HEX_TO_KEYCODE[nextionBuffer[2]];
    } else if (pageId == 0x01) {
      switch (nextionBuffer[2]) {
        case 12: code = B_MODE_GEARS; break;
        case 13: code = B_MODE_TURN; break;
        case 14: code = B_MODE_FACE; break;
        case 15: code = B_MODE_CONE; break;
        case 16: code = B_MODE_CUT; break;
        case 17: code = B_MODE_THREAD; break;
        case 18: code = B_MODE_ELLIPSE; break;
        case 19: code = B_MODE_GCODE; break;
        case 20: code = B_MODE_ASYNC; break;
        case 21: code = B_MODE_Y; break;
        case 23: code = B_MODE_XGEAR; break;
        case 25: code = B_MODE_JOYSTICK; break;
        case 27: code = B_MODE_SLOT; break;
      }
    }
    if (code != 0) {
      lastNextionPageId = pageId;
      if (nextionBuffer[3] == 0) code |= PS2_BREAK;
    }
    return code;
  }
  return 0;
}

// static void setModeFromUi(int modeToSet, bool eventFromNextion) {
//   setModeFromTask(modeToSet);
//   if (eventFromNextion && lastNextionPageId == 1) toScreen("page 0");
// }

void adjustPitch(bool plus) {
  bool minus = !plus;
  long currentDupr = nextDuprFlag ? nextDupr : dupr;
  if (measure != MEASURE_TPI) {
    long delta = feedStepDu > 0 ? feedStepDu : (measure == MEASURE_METRIC ? FEED_STEP_2 : FEED_STEP_IMP_3);
    long normalizedDupr = normalizePitch(currentDupr);
    if (minus && currentDupr > -DUPR_MAX) {
      setFeedrateDupr(max(-DUPR_MAX, normalizedDupr - delta), false);
    } else if (plus && currentDupr < DUPR_MAX) {
      setFeedrateDupr(min(DUPR_MAX, normalizedDupr + delta), false);
    }
  } else { // TPI
    if (currentDupr == 0) {
      setFeedrateDupr(plus ? 1 : -1, false);
    } else {
      long currentTpi = round(254000.0 / currentDupr);
      long tpi = currentTpi + (plus ? 1 : -1);
      long newDupr = tpi == 0 ? (plus ? DUPR_MAX : -DUPR_MAX) : round(254000.0 / tpi);
      if (newDupr == currentDupr) {
        newDupr += plus ? -1 : 1;
      }
      if (newDupr != currentDupr && newDupr < DUPR_MAX && newDupr > -DUPR_MAX) {
        setFeedrateTpi((float)abs(tpi), false);
        if (newDupr < 0) {
          setFeedrateDupr(newDupr, false);
        }
      }
    }
  }
}

void buttonPlusMinusPress(bool plus) {
  bool minus = !plus;
  if (mode == MODE_THREAD && setupIndex == 2) {
    if (minus && starts > 1) {
      setStarts(starts - 1);
    } else if (plus && starts < STARTS_MAX) {
      setStarts(starts + 1);
    }
  } else if (isPassMode() && setupIndex == 1 && getNumpadResult() == 0) {
    if (minus && turnPasses > 1) {
      setTurnPasses(turnPasses - 1);
    } else if (plus && turnPasses < PASSES_MAX) {
      setTurnPasses(turnPasses + 1);
    }
  } else {
    adjustPitch(plus);
  }
}

static unsigned long resetMillis = 0;

static void buttonOffRelease() {
  if (millis() - resetMillis > 3000) {
    reset();
    splashScreen = true;
  }
}

void buttonOnOffPress(bool on) {
  resetMillis = millis();
  bool missingZStops = needZStops() && (z.leftStop == LONG_MAX || z.rightStop == LONG_MIN);
  if (on && isPassMode() && (missingZStops || x.leftStop == LONG_MAX || x.rightStop == LONG_MIN)) {
    beep();
  } else if (!isOn && on && mode == MODE_GCODE && gcodeProgramIndex >= gcodeProgramCount && setupIndex == 1) {
    beep();
  } else if (!isOn && on && setupIndex < getLastSetupIndex()) {
    if (mode == MODE_THREAD && setupIndex == 3) setConeRatio(0);
    setupIndex++;
  } else if (isOn && on && (mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD)) {
    opIndexAdvanceFlag = true;
  } else if (!on && (z.movingManually || x.movingManually || y.movingManually)) {
    setEmergencyStop(ESTOP_OFF_MANUAL_MOVE);
  } else if (!isOn && on && mode == MODE_GCODE && gcodeProgramIndex >= gcodeProgramCount) {
    beep();
  } else if (!isOn && on && mode == MODE_GCODE) {
    String name = getCurrentGcodeProgramName();
    if (name.length() == 0) {
      beep();
    } else {
      gcodeProgramCharIndex = 0;
      gcodeProgram = readGcodeProgram(name);
      if (gcodeProgram.length() == 0) {
        beep();
      } else {
        gcodeProgram += '\n';
        resetGcodeState();
        SettingsService::requestIsOn(true);
      }
    }
  } else {
    SettingsService::requestIsOn(on);
  }
}

static void buttonMultistartPress() {
  if (millis() - multistartPressMillis > 3000 && starts > 1) {
    SettingsService::setStarts(1);
  } else {
    SettingsService::setStarts(starts + 1);
  }
  multistartPressMillis = millis();
}

static void buttonLeftStopPress(Axis* a) {
  setLeftStop(a, a->leftStop == LONG_MAX ? a->pos : LONG_MAX);
}

static void buttonRightStopPress(Axis* a) {
  setRightStop(a, a->rightStop == LONG_MIN ? a->pos : LONG_MIN);
}

static void buttonDisplayPress() {
  int next = spindleDisplayMode + 1;
  if (next > 2) next = 0;
  SettingsService::setSpindleDisplayMode(next);
}

static void buttonMoveStepPress() {
  SettingsService::cycleJogStep();
}

static void buttonMeasurePress() {
  if (measure == MEASURE_METRIC) {
    SettingsService::setMeasure(MEASURE_INCH);
  } else if (measure == MEASURE_INCH) {
    SettingsService::setMeasure(MEASURE_TPI);
  } else {
    SettingsService::setMeasure(MEASURE_METRIC);
  }
}

static void buttonReversePress() {
  setDupr(-dupr);
}

static int heldRepeatKey = 0;
static unsigned long heldRepeatPressTime = 0;
static unsigned long heldRepeatLastStepTime = 0;

static void processHeldRepeatKey() {
  if (heldRepeatKey == 0) return;
  if (isOn || inNumpad || emergencyStop != ESTOP_NONE) {
    heldRepeatKey = 0;
    return;
  }
  unsigned long now = millis();
  if (now - heldRepeatPressTime >= (unsigned long)JOG_HOLD_DELAY_MS) {
    if (now - heldRepeatLastStepTime >= 200) {
      heldRepeatLastStepTime = now;
      buttonPlusMinusPress(heldRepeatKey == B_PLUS);
    }
  }
}

void processKeypadEvent() {
  processWebUiMoveFailsafe();
  int event = 0;
  bool eventFromNextion = false;
  bool eventUsesKeyboardMap = false;
  bool eventFromWebUi = false;
  lastNextionPageId = 255;
  WebUiEvent webUiEvent = {};
  if (readWebUiEvent(&webUiEvent)) {
    event = webUiEvent.actionCode;
    eventFromWebUi = true;
    if (!webUiEvent.isPress) event |= PS2_BREAK;
  } else if (wsKeycode != 0) {
    event = wsKeycode;
    wsKeycode = 0;
    eventUsesKeyboardMap = true;
#if ENABLE_USB_KEYBOARD
  } else if (usbKeyboardAvailable()) {
    event = usbKeyboardRead();
    eventUsesKeyboardMap = true;
#endif
  } else if (keyboard.available()) {
    event = keyboard.read();
    eventUsesKeyboardMap = true;
  } else if (!tftUploadActive && Serial1.available() > 0) {
    while (Serial1.available() > 0) {
      byte incomingByte = Serial1.read();
      if (nextionBufferIndex < NEXTION_BUFFER_LENGTH) {
        nextionBuffer[nextionBufferIndex] = incomingByte;
        nextionBufferIndex++;
      } else {
        nextionBufferIndex = 0;
      }
      if (checkForTerminator()) {
        event = processNextionMessage();
        eventFromNextion = event != 0 && lastNextionPageId != 255;
        nextionBufferIndex = 0;
        break;
      }
    }
  }
  if (event == 0) {
    processHeldRepeatKey();
    return;
  }
  int physicalKeyCode = event & 0xFF;
  bool isPress = !(event & PS2_BREAK);
  int keyCode = eventUsesKeyboardMap ? keyboardActionForCode(byte(physicalKeyCode)) : physicalKeyCode;
  keypadTimeUs = micros();

  if (eventUsesKeyboardMap) {
    publishKeyboardEvent(byte(physicalKeyCode), byte(keyCode), isPress);
  }

  // Some keyboards send this code and expect an answer to initialize.
  if (physicalKeyCode == 170) {
    keyboard.echo();
    return;
  }

  if (eventUsesKeyboardMap && shouldConsumeKeyboardCapture(byte(physicalKeyCode), isPress)) {
    return;
  }

  if (keyCode == 0) return;

  // Off button always gets handled.
  if (keyCode == B_OFF) {
    buttonOffPressed = isPress;
    isPress ? buttonOnOffPress(false) : buttonOffRelease();
  }

  if (mode == MODE_GCODE && isOn) {
    // Not allowed to interfere other than turn off.
    if (isPress && keyCode != B_OFF) beep();
    return;
  }

  // Releases don't matter in numpad but it has to run before LRUD since it might handle those keys.
  if (isPress && processNumpad(keyCode)) {
    return;
  }

  if (eventFromWebUi) {
    updateWebUiMoveWatchdog(byte(keyCode), isPress);
  }

  // Keyboard may not send release event if another button is pressed before first one is released.
  buttonLeftPressed = false;
  buttonRightPressed = false;
  buttonUpPressed = false;
  buttonDownPressed = false;
  buttonBackPressed = false;
  buttonForwardPressed = false;

  // Setup wizard navigation.
  if (isPress && setupIndex == 2 && (keyCode == B_LEFT || keyCode == B_RIGHT)) {
    SettingsService::setAuxForward(!SettingsService::getAuxForward());
  } else if (isPress && mode == MODE_GCODE && setupIndex == 1 && (keyCode == B_UP || keyCode == B_DOWN)) {
    if (gcodeProgramIndex > 0 && keyCode == B_UP) gcodeProgramIndex--;
    else if (gcodeProgramIndex == 0 && gcodeProgramCount > 0 && keyCode == B_UP) gcodeProgramIndex = gcodeProgramCount - 1;
    else if ((gcodeProgramIndex < gcodeProgramCount - 1) && keyCode == B_DOWN) gcodeProgramIndex++;
    else if (keyCode == B_DOWN) gcodeProgramIndex = 0;
  } else if (isPress && mode == MODE_GCODE && setupIndex == 1 && keyCode == B_MINUS) {
    removeGcodeByName(getCurrentGcodeProgramName());
    return;
  } else if (isWebUiMoveAction(byte(keyCode))) {
    setMoveButtonPressed(byte(keyCode), isPress);
  }

  // For all other keys we have no "release" logic, except resetting held repeat.
  if (!isPress) {
    if (keyCode == heldRepeatKey) {
      heldRepeatKey = 0;
    }
    return;
  }

  if (keyCode != heldRepeatKey) {
    heldRepeatKey = 0;
  }

  // Rest of the buttons.
  if (keyCode == B_PLUS) {
    if (!isOn && !inNumpad) {
      heldRepeatKey = B_PLUS;
      heldRepeatPressTime = millis();
      heldRepeatLastStepTime = millis();
    }
    buttonPlusMinusPress(true);
  } else if (keyCode == B_MINUS) {
    if (!isOn && !inNumpad) {
      heldRepeatKey = B_MINUS;
      heldRepeatPressTime = millis();
      heldRepeatLastStepTime = millis();
    }
    buttonPlusMinusPress(false);
  } else if (keyCode == B_ON) {
    buttonOnOffPress(true);
  } else if (keyCode == B_STOPL) {
    buttonLeftStopPress(&z);
  } else if (keyCode == B_STOPR) {
    buttonRightStopPress(&z);
  } else if (keyCode == B_STOPU) {
    buttonLeftStopPress(&x);
  } else if (keyCode == B_STOPD) {
    buttonRightStopPress(&x);
  } else if (keyCode == B_STOPF && ACTIVE_Y) {
    buttonLeftStopPress(&y);
  } else if (keyCode == B_STOPB && ACTIVE_Y) {
    buttonRightStopPress(&y);
  } else if (keyCode == B_MODE_Y && ACTIVE_Y) {
    setModeFromUi(MODE_Y, eventFromNextion);
  } else if (keyCode == B_MODE_ELLIPSE) {
    setModeFromUi(MODE_ELLIPSE, eventFromNextion);
  } else if (keyCode == B_MODE_GCODE) {
    setModeFromUi(MODE_GCODE, eventFromNextion);
  } else if (keyCode == B_MODE_ASYNC) {
    setModeFromUi(MODE_ASYNC, eventFromNextion);
  } else if (keyCode == B_MULTISTART) {
    buttonMultistartPress();
  } else if (keyCode == B_DISPL) {
    buttonDisplayPress();
  } else if (keyCode == B_X) {
    markAxis0(&x);
  } else if (keyCode == B_Z) {
    markAxis0(&z);
  } else if (keyCode == B_Y && ACTIVE_Y) {
    markAxis0(&y);
  } else if (keyCode == B_X_ENA) {
    x.disabled = !x.disabled;
    updateEnable(&x);
  } else if (keyCode == B_Z_ENA) {
    z.disabled = !z.disabled;
    updateEnable(&z);
  } else if (keyCode == B_Y_ENA && ACTIVE_Y) {
    y.disabled = !y.disabled;
    updateEnable(&y);
  } else if (keyCode == B_STEP) {
    buttonMoveStepPress();
  } else if (keyCode == B_REVERSE) {
    buttonReversePress();
  } else if (keyCode == B_MEASURE) {
    buttonMeasurePress();
  } else if (keyCode == B_MODE_GEARS) {
    setModeFromUi(MODE_NORMAL, eventFromNextion);
  } else if (keyCode == B_MODE_TURN) {
    setModeFromUi(MODE_TURN, eventFromNextion);
  } else if (keyCode == B_MODE) {
    // Note for multi-page screens (e.g. stock Nextion or future LVGL mode selection screen):
    // Stock Nextion opened a mode selection screen (page 1) when tapping the mode button:
    //   if (eventFromNextion) toScreen("page 1");
    // For single-page screens like GeckoSub's screenset, tapping the Mode button cycles
    // through the modes sequentially directly on the main screen:
    if (mode == MODE_NORMAL) setModeFromTask(MODE_XGEAR);
    else if (mode == MODE_XGEAR) setModeFromTask(MODE_JOYSTICK);
    else if (mode == MODE_JOYSTICK) setModeFromTask(MODE_TURN);
    else if (mode == MODE_TURN) setModeFromTask(MODE_FACE);
    else if (mode == MODE_FACE) setModeFromTask(MODE_CONE);
    else if (mode == MODE_CONE) setModeFromTask(MODE_CUT);
    else if (mode == MODE_CUT) setModeFromTask(MODE_SLOT);
    else if (mode == MODE_SLOT) setModeFromTask(MODE_THREAD);
    else if (mode == MODE_THREAD) setModeFromTask(MODE_ELLIPSE);
    else if (mode == MODE_ELLIPSE) setModeFromTask(MODE_GCODE);
    else if (mode == MODE_GCODE) setModeFromTask(MODE_ASYNC);
    else if (mode == MODE_ASYNC) setModeFromTask(y.active ? MODE_Y : MODE_NORMAL);
    else if (mode == MODE_Y) setModeFromTask(MODE_NORMAL);
    else setModeFromTask(MODE_NORMAL);
  } else if (keyCode == B_MODE_XGEAR) {
    setModeFromUi(MODE_XGEAR, eventFromNextion);
  } else if (keyCode == B_MODE_JOYSTICK) {
    setModeFromUi(MODE_JOYSTICK, eventFromNextion);
  } else if (keyCode == B_MODE_FACE) {
    setModeFromUi(MODE_FACE, eventFromNextion);
  } else if (keyCode == B_MODE_CONE) {
    setModeFromUi(MODE_CONE, eventFromNextion);
  } else if (keyCode == B_MODE_CUT) {
    setModeFromUi(MODE_CUT, eventFromNextion);
  } else if (keyCode == B_MODE_SLOT) {
    setModeFromUi(MODE_SLOT, eventFromNextion);
  } else if (keyCode == B_MODE_THREAD) {
    setModeFromUi(MODE_THREAD, eventFromNextion);
  } else if (keyCode == B_X_TOGGLE_DRO) {
#if HAS_DRO_X
    SettingsService::setXDroMode(!SettingsService::isXDroMode());
    beep();
#endif
  } else if (keyCode == B_Z_TOGGLE_DRO) {
#if HAS_DRO_Z
    SettingsService::setZDroMode(!SettingsService::isZDroMode());
    beep();
#endif
  } else if (keyCode == B_DIA_RAD) {
    SettingsService::setDiameterMode(!SettingsService::isDiameterMode());
    beep();
  } else if (keyCode == B_ABS_INC) {
    SettingsService::setAbsoluteMode(!SettingsService::isAbsoluteMode());
    beep();
  } else if (keyCode == B_SPINDLE_UNITS) {
    spindleValColorGreen = !spindleValColorGreen;
    beep();
  }
}

void taskKeypad(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    processKeypadEvent();
    taskYIELD();
  }
  vTaskDelete(NULL);
}

void taskJoystick(void *param) {
  JoystickAxisState* joystickZ = &joystickAxes[JOYSTICK_AXIS_Z];
  JoystickAxisState* joystickX = &joystickAxes[JOYSTICK_AXIS_X];
  JoystickAxisState* joystickY = &joystickAxes[JOYSTICK_AXIS_Y];
  while (emergencyStop == ESTOP_NONE && joystickUsable()) {
    bool rawButtonPressed = digitalRead(JOY_BUTTON) == (INVERT_JOYSTICK_BUTTON ? HIGH : LOW);
    unsigned long now = micros();
    unsigned long elapsedUs = now - joystickSampleTimeUs;
    joystickSampleTimeUs = now;

    readJoystickAxis(joystickZ);
    readJoystickAxis(joystickX);
    readJoystickAxis(joystickY);

    bool moveNeutral = joystickZ->direction == 0 && joystickX->direction == 0;
    bool buttonPressed = updateJoystickButton(rawButtonPressed, moveNeutral);
    joystickRapidPressed = buttonPressed;

    bool yAdjustsPitch = !buttonPressed && joystickPitchAdjustmentAllowed();
    int pitchDirection = yAdjustsPitch ? joystickY->direction : 0;
    joystickPitchAdjustDirection = pitchDirection;
    if (pitchDirection != 0) {
      joystickPitchStatusDirection = pitchDirection;
      joystickPitchStatusMillis = millis();
      joystickPitchChangeFraction += abs(joystickY->deflection) * JOYSTICK_PITCH_CHANGES_PER_SECOND * elapsedUs / 1000000.0;
      int changes = min(10, int(joystickPitchChangeFraction));
      joystickPitchChangeFraction -= changes;
      for (int i = 0; i < changes; i++) {
        adjustPitch(pitchDirection > 0);
      }
    } else {
      joystickPitchChangeFraction = 0;
    }

    if (mode == MODE_JOYSTICK) {
      bool manualJoystickMove = buttonPressed || !isOn;
      joystickLatheDirectionZ = joystickZ->direction;
      joystickLatheDirectionX = joystickX->direction;
      joystickLatheRapid = buttonPressed && !moveNeutral;
      updateJoystickManualAxis(joystickZ, manualJoystickMove, false, elapsedUs);
      updateJoystickManualAxis(joystickX, manualJoystickMove, false, elapsedUs);
      updateJoystickManualAxis(joystickY, false, false, elapsedUs);
    } else {
      joystickLatheDirectionZ = 0;
      joystickLatheDirectionX = 0;
      joystickLatheRapid = false;
      updateJoystickManualAxis(joystickZ, true, true, elapsedUs);
      updateJoystickManualAxis(joystickX, true, true, elapsedUs);
      updateJoystickManualAxis(joystickY, y.active && !yAdjustsPitch, true, elapsedUs);
    }

    DELAY(JOYSTICK_SAMPLE_INTERVAL_MS);
  }
  vTaskDelete(NULL);
}
