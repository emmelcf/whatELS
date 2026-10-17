#include "Input.h"
#include "Config.h"
#include "Debug.h"
#include "WebServerModule.h"
#include "UsbKeyboard.h"
#include "SettingsService.h"
#include "web/CommandDispatcher.h"
#include "core/OperatorIntent.h"
#include "motion/kernel/MotionEngine.h"
#include "motion/DRO_Scale.h"

static inline LatheMode currentLatheMode() {
  return static_cast<LatheMode>(SettingsService::getMode());
}
static inline MeasureUnit currentMeasure() {
  return SettingsService::getMeasure();
}

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

static bool isPassMode(LatheMode m) {
  return m == LatheMode::TURN || m == LatheMode::FACE || m == LatheMode::CUT || m == LatheMode::THREAD || m == LatheMode::ELLIPSE || m == LatheMode::SLOT;
}

static bool isPassMode() {
  return isPassMode(currentLatheMode());
}

static int getLastSetupIndex(LatheMode m) {
  if (m == LatheMode::CONE || m == LatheMode::GCODE) return 2;
  if (m == LatheMode::THREAD) return 4;
  return 3;
}

static int getLastSetupIndex() {
  return getLastSetupIndex(currentLatheMode());
}

// Joystick State
JoystickAxisState joystickAxes[] = {
  {AXIS_Z, JOY_Z, &sysConfig.joystick.zEnabled, &sysConfig.joystick.invertZ, 0, 0, 0, 2048, 2048, 0, 0, 0},
  {AXIS_X, JOY_X, &sysConfig.joystick.xEnabled, &sysConfig.joystick.invertX, 0, 0, 0, 2048, 2048, 0, 0, 0},
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
  if (currentMeasure() == MeasureUnit::INCH) {
    result = result * 254;
  } else if (currentMeasure() == MeasureUnit::TPI) {
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
      float divisor = (currentMeasure() == MeasureUnit::METRIC) ? 100.0f : 1000.0f;
      float desiredValue = numpadResult / divisor;

      if (SettingsService::isXDroMode()) {
        desiredValue /= 2.0f;
        if (currentMeasure() != MeasureUnit::METRIC) desiredValue *= 25.4f;
        xScale.setPosition(desiredValue);
      } else {
        float targetRadiusMm = desiredValue / 2.0f;
        if (currentMeasure() != MeasureUnit::METRIC) targetRadiusMm = (desiredValue * 25.4f) / 2.0f;
        MotionEngine::instance().setAxisPositionDu(AXIS_X, round(targetRadiusMm * 10000.0f));
      }
      numpadEntryMode = PITCH;
      return true;
    } else if (numpadEntryMode == LENGTH) {
      float divisor = (currentMeasure() == MeasureUnit::METRIC) ? 100.0f : 1000.0f;
      float desiredValue = numpadResult / divisor;

      if (SettingsService::isZDroMode()) {
        if (currentMeasure() != MeasureUnit::METRIC) desiredValue *= 25.4f;
        zScale.setPosition(desiredValue);
      } else {
        float targetMm = desiredValue;
        if (currentMeasure() != MeasureUnit::METRIC) targetMm = desiredValue * 25.4f;
        MotionEngine::instance().setAxisPositionDu(AXIS_Z, round(targetMm * 10000.0f));
      }
      numpadEntryMode = PITCH;
      return true;
    }
    
    if (isPassMode() && setupIndex == 1) {
      SettingsService::setTurnPasses(int(min((long)PASSES_MAX, numpadResult)));
      setupIndex++;
    } else if (currentLatheMode() == LatheMode::THREAD && setupIndex == 3) {
      SettingsService::setTaperRatio(newConeRatio);
      setupIndex++;
    } else if (currentLatheMode() == LatheMode::CONE && setupIndex == 1) {
      SettingsService::setTaperRatio(newConeRatio);
      setupIndex++;
    } else {
      if (abs(newDu) <= DUPR_MAX) {
        SettingsService::setFeedrateDupr(newDu);
      }
    }
    // Don't use this ON press for starting the motion.
    return true;
  }

  // Shared piece for stops and moves.
  AxisId a = (keyCode == B_STOPL || keyCode == B_STOPR || keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_Z) ? AXIS_Z : AXIS_X;
  int sign = ((keyCode == B_STOPL || keyCode == B_STOPU || keyCode == B_LEFT || keyCode == B_UP || keyCode == B_Z || keyCode == B_X || keyCode == B_X_ENA) ? 1 : -1);
  long currentPosDu = MotionEngine::instance().getAxisPositionDu(a);
  long posDu = currentPosDu + newDu * sign;

  // Potentially assign a new value to a limit. Treat newDu as a relative distance from current position.
  if (keyCode == B_STOPL) {
    SettingsService::setAxisLimitDu(AXIS_Z, LimitDirection::LIMIT_LEFT_OR_UP, posDu);
    return true;
  } else if (keyCode == B_STOPR) {
    SettingsService::setAxisLimitDu(AXIS_Z, LimitDirection::LIMIT_RIGHT_OR_DOWN, posDu);
    return true;
  } else if (keyCode == B_STOPU) {
    SettingsService::setAxisLimitDu(AXIS_X, LimitDirection::LIMIT_LEFT_OR_UP, posDu);
    return true;
  } else if (keyCode == B_STOPD) {
    SettingsService::setAxisLimitDu(AXIS_X, LimitDirection::LIMIT_RIGHT_OR_DOWN, posDu);
    return true;
  }

  // Potentially move by newDu in the given direction.
  if (!SettingsService::isOn() && (keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_UP || keyCode == B_DOWN)) {
    const auto& limits = MotionEngine::instance().getSoftLimits(a);
    const SoftLimit* minLimit = limits.getRightLimit();
    const SoftLimit* maxLimit = limits.getLeftLimit();
    if (minLimit && posDu < minLimit->coordDu) {
      posDu = minLimit->coordDu;
    } else if (maxLimit && posDu > maxLimit->coordDu) {
      posDu = maxLimit->coordDu;
    }
    int8_t dir = (sign > 0) ? 1 : -1;
    MotionEngine::instance().startJog(a, dir, abs(newDu));
    return true;
  }

  // Set axis 0 newDu ahead.
  if (keyCode == B_Z || keyCode == B_X) {
    MotionEngine::instance().setAxisPositionDu(a, -posDu);
    return true;
  }

  // Set X axis 0 from diameter.
  if (keyCode == B_DIAMETER || keyCode == B_X_ENA) {
    MotionEngine::instance().setAxisPositionDu(AXIS_X, -(currentPosDu + newDu / 2));
    return true;
  }

  if (keyCode == B_STEP) {
    if (newDu > 0) {
      SettingsService::setJogStepDu(newDu);
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

bool joystickUsable() {
  return sysConfig.joystick.enabled && joystickAvailable;
}

void prepareJoystickLatheManualMove() {
  if (SettingsService::isOn() && currentLatheMode() == LatheMode::JOYSTICK) {
    resetJoystickLatheFeedPosition();
  }
}

bool isContinuousStep() {
  if (!sysConfig.timing.enableContinuousMove) return false;
  return SettingsService::getJogStepDu() == (currentMeasure() == MeasureUnit::METRIC ? JOG_STEPS_METRIC[0] : JOG_STEPS_IMPERIAL[0]);
}

bool isRapidManualMove() {
  return joystickUsable() && joystickRapidPressed;
}

static int joystickDirectionFromDeflection(float deflection) {
  return deflection > 0 ? 1 : (deflection < 0 ? -1 : 0);
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
  MotionEngine::instance().stop(joystickAxis->axisId);
}

static int readJoystickPin(int pin) {
  long total = 0;
  for (int i = 0; i < sysConfig.joystick.oversamples; i++) {
    total += analogRead(pin);
  }
  return total / sysConfig.joystick.oversamples;
}

static int calibrateJoystickCenter(int pin) {
  long total = 0;
  for (int i = 0; i < sysConfig.joystick.centerSamples; i++) {
    total += readJoystickPin(pin);
    DELAY(2);
  }
  return total / sysConfig.joystick.centerSamples;
}

static float getJoystickDeflection(JoystickAxisState* joystickAxis) {
  int raw = readJoystickPin(joystickAxis->pin);
  int rawDelta = raw - joystickAxis->center;
  int filteredDelta = joystickAxis->filtered - joystickAxis->center;
  if (*joystickAxis->invert) {
    rawDelta = -rawDelta;
    filteredDelta = -filteredDelta;
  }

  bool rawNeutral = abs(rawDelta) <= sysConfig.joystick.deadband;
  bool filteredNeutral = abs(filteredDelta) <= sysConfig.joystick.deadband;
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
  if (absDelta <= sysConfig.joystick.deadband) return 0;

  int range = delta > 0 ? sysConfig.joystick.adcMax - joystickAxis->center : joystickAxis->center;
  range = max(1, range - sysConfig.joystick.deadband);
  float normalized = (absDelta - sysConfig.joystick.deadband) / float(range);
  if (normalized > 1.0) normalized = 1.0;
  float curved = normalized * normalized;
  return delta > 0 ? curved : -curved;
}

static int getJoystickPulseDelta(JoystickAxisState* joystickAxis, unsigned long elapsedUs) {
  float revolutionsPerSecond = joystickRapidPressed ? sysConfig.joystick.rapidRps : sysConfig.joystick.normalRps;
  joystickAxis->pulseFraction += joystickAxis->deflection * 400.0f * revolutionsPerSecond * elapsedUs / 1000000.0;
  int delta = int(joystickAxis->pulseFraction);
  joystickAxis->pulseFraction -= delta;
  return delta;
}

static long getJoystickSpeedFromDeflection(JoystickAxisState* joystickAxis) {
  if (joystickAxis->deflection == 0) return 0;
  float revolutionsPerSecond = joystickRapidPressed ? sysConfig.joystick.rapidRps : sysConfig.joystick.normalRps;
  long speed = round(abs(joystickAxis->deflection) * revolutionsPerSecond * 400.0f);
  if (speed < 1) speed = 1;
  return speed;
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
  if (enabled && joystickAxis->manualDirection != 0) {
    int8_t dir = (joystickAxis->manualDirection > 0) ? 1 : -1;
    MotionEngine::instance().startJog(joystickAxis->axisId, dir, 1000);
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
  int midpoint = sysConfig.joystick.adcMax / 2;
  int tolerance = max(sysConfig.joystick.deadband, sysConfig.joystick.startupCenterToleranceMin);
  return abs(center - midpoint) <= tolerance;
}

static bool updateJoystickButton(bool rawPressed, bool moveNeutral) {
  unsigned long now = millis();
  if (rawPressed != joystickButtonRawPressed) {
    joystickButtonRawPressed = rawPressed;
    joystickButtonRawChangeMillis = now;
  }

  if (rawPressed != joystickButtonPressed && now - joystickButtonRawChangeMillis >= sysConfig.joystick.buttonDebounceMs) {
    joystickButtonPressed = rawPressed;
    if (joystickButtonPressed) {
      joystickButtonPressMillis = now;
      joystickButtonToggleBlocked = currentLatheMode() != LatheMode::JOYSTICK || !moveNeutral;
    } else {
      bool shortClick = now - joystickButtonPressMillis <= sysConfig.joystick.buttonToggleMaxMs;
      if (sysConfig.joystick.buttonTogglesOnOff && currentLatheMode() == LatheMode::JOYSTICK && moveNeutral && shortClick && !joystickButtonToggleBlocked) {
        buttonOnOffPress(!SettingsService::isOn());
      }
      joystickButtonToggleBlocked = false;
    }
  }

  if (joystickButtonPressed && (currentLatheMode() != LatheMode::JOYSTICK || !moveNeutral || now - joystickButtonPressMillis > sysConfig.joystick.buttonToggleMaxMs)) {
    joystickButtonToggleBlocked = true;
  }

  return joystickButtonPressed;
}

void initJoystick() {
  joystickAvailable = false;
  joystickStartupWarning = "";
  if (!sysConfig.joystick.enabled) return;
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
      int offset = joystickAxes[i].center - sysConfig.joystick.adcMax / 2;
      char axisName = i == JOYSTICK_AXIS_Z ? NAME_Z : NAME_X;
      joystickStartupWarning = "Joy ";
      joystickStartupWarning += axisName;
      joystickStartupWarning += (offset >= 0 ? " +" : " -");
      joystickStartupWarning += String(max(0, abs(offset) - sysConfig.joystick.deadband));
      joystickStartupWarning += " past deadband";
      return;
    }
  }

  for (int i = 0; i < JOYSTICK_AXIS_COUNT; i++) {
    if (!joystickAxisEnabled(&joystickAxes[i])) {
      joystickAxes[i].center = sysConfig.joystick.adcMax / 2;
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
  return SettingsService::isOn() && SettingsService::getThreadSetting().dupr != 0 && !joystickLatheRapid && (joystickLatheDirectionZ != 0 || joystickLatheDirectionX != 0);
}

bool manualMovesAllowedWhenOn() {
  LatheMode m = currentLatheMode();
  return m == LatheMode::NORMAL || m == LatheMode::XGEAR || m == LatheMode::ASYNC || m == LatheMode::CONE || m == LatheMode::JOYSTICK;
}

bool manualMovesIgnoredWhenOn() {
  return currentLatheMode() == LatheMode::GCODE || isPassMode();
}

static bool joystickPitchAdjustmentAllowed() {
  LatheMode m = currentLatheMode();
  if (m == LatheMode::GCODE) return false;
  if (!SettingsService::isOn()) return true;
  return m != LatheMode::THREAD && m != LatheMode::ELLIPSE;
}

int getJoystickPitchStatusDirection() {
  int direction = joystickPitchAdjustDirection;
  if (direction != 0) return direction;
  if (millis() - joystickPitchStatusMillis < sysConfig.joystick.pitchStatusHoldMs) {
    return joystickPitchStatusDirection;
  }
  return 0;
}

static void setModeFromUi(LatheMode modeToSet) {
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "CMD.mode=%d", static_cast<int>(modeToSet));
  CommandDispatcher::dispatch(cmd, strlen(cmd));
}

static long normalizePitch(long pitch) {
  long delta = SettingsService::getPreferences().feedStepDu > 0 ? SettingsService::getPreferences().feedStepDu : (currentMeasure() == MeasureUnit::METRIC ? FEED_STEPS_METRIC[1] : FEED_STEPS_IMPERIAL[2]);
  return (delta > 0) ? (round((float)pitch / delta) * delta) : pitch;
}

void adjustPitch(bool plus) {
  bool minus = !plus;
  long currentDupr = SettingsService::getThreadSetting().dupr;
  if (currentMeasure() != MeasureUnit::TPI) {
    long delta = SettingsService::getPreferences().feedStepDu > 0 ? SettingsService::getPreferences().feedStepDu : (currentMeasure() == MeasureUnit::METRIC ? FEED_STEPS_METRIC[1] : FEED_STEPS_IMPERIAL[2]);
    long normalizedDupr = normalizePitch(currentDupr);
    if (minus && currentDupr > -DUPR_MAX) {
      SettingsService::setFeedrateDupr(max(-DUPR_MAX, normalizedDupr - delta), false);
    } else if (plus && currentDupr < DUPR_MAX) {
      SettingsService::setFeedrateDupr(min(DUPR_MAX, normalizedDupr + delta), false);
    }
  } else { // TPI
    if (currentDupr == 0) {
      SettingsService::setFeedrateDupr(plus ? 1 : -1, false);
    } else {
      long currentTpi = round(254000.0 / currentDupr);
      long tpi = currentTpi + (plus ? 1 : -1);
      long newDupr = tpi == 0 ? (plus ? DUPR_MAX : -DUPR_MAX) : round(254000.0 / tpi);
      if (newDupr == currentDupr) {
        newDupr += plus ? -1 : 1;
      }
      if (newDupr != currentDupr && newDupr < DUPR_MAX && newDupr > -DUPR_MAX) {
        SettingsService::setFeedrateTpi((float)abs(tpi), false);
        if (newDupr < 0) {
          SettingsService::setFeedrateDupr(newDupr, false);
        }
      }
    }
  }
}

void buttonPlusMinusPress(bool plus) {
  bool minus = !plus;
  if (currentLatheMode() == LatheMode::THREAD && setupIndex == 2) {
    int curStarts = SettingsService::getStarts();
    if (minus && curStarts > 1) {
      SettingsService::setStarts(curStarts - 1);
    } else if (plus && curStarts < STARTS_MAX) {
      SettingsService::setStarts(curStarts + 1);
    }
  } else if (isPassMode() && setupIndex == 1 && getNumpadResult() == 0) {
    int curPasses = SettingsService::getTurnPasses();
    if (minus && curPasses > 1) {
      SettingsService::setTurnPasses(curPasses - 1);
    } else if (plus && curPasses < PASSES_MAX) {
      SettingsService::setTurnPasses(curPasses + 1);
    }
  } else {
    adjustPitch(plus);
  }
}

void buttonOnOffPress(bool on) {
  const auto& zLimits = MotionEngine::instance().getSoftLimits(AXIS_Z);
  const auto& xLimits = MotionEngine::instance().getSoftLimits(AXIS_X);
  bool missingZStops = (zLimits.getLeftLimit() == nullptr || zLimits.getRightLimit() == nullptr);
  bool missingXStops = (xLimits.getLeftLimit() == nullptr || xLimits.getRightLimit() == nullptr);
  if (on && isPassMode() && (missingZStops || missingXStops)) {
    // Cannot engage motion without set travel stops
    return;
  } else if (!SettingsService::isOn() && on && currentLatheMode() == LatheMode::GCODE && gcodeProgramIndex >= gcodeProgramCount && setupIndex == 1) {
    return;
  } else if (!SettingsService::isOn() && on && setupIndex < getLastSetupIndex()) {
    if (currentLatheMode() == LatheMode::THREAD && setupIndex == 3) SettingsService::setTaperRatio(0);
    setupIndex++;
  } else if (SettingsService::isOn() && on && (currentLatheMode() == LatheMode::TURN || currentLatheMode() == LatheMode::FACE || currentLatheMode() == LatheMode::THREAD)) {
    opIndexAdvanceFlag = true;
  } else if (!on && MotionEngine::instance().isAnyAxisMoving()) {
    setEmergencyStop(EstopCode::OFF_MANUAL_MOVE);
  } else if (!SettingsService::isOn() && on && currentLatheMode() == LatheMode::GCODE && gcodeProgramIndex >= gcodeProgramCount) {
    return;
  } else if (!SettingsService::isOn() && on && currentLatheMode() == LatheMode::GCODE) {
    String name = getCurrentGcodeProgramName();
    if (name.length() > 0) {
      gcodeProgramCharIndex = 0;
      gcodeProgram = readGcodeProgram(name);
      if (gcodeProgram.length() > 0) {
        gcodeProgram += '\n';
        CommandDispatcher::dispatch("CMD.on=1", 8);
      }
    }
  } else {
    if (on) {
      CommandDispatcher::dispatch("CMD.on=1", 8);
    } else {
      CommandDispatcher::dispatch("CMD.stop", 8);
    }
  }
}

static void buttonMultistartPress() {
  int curStarts = SettingsService::getStarts();
  if (millis() - multistartPressMillis > 3000 && curStarts > 1) {
    SettingsService::setStarts(1);
  } else {
    SettingsService::setStarts(curStarts + 1);
  }
  multistartPressMillis = millis();
}

static void buttonLeftStopPress(AxisId a) {
  const auto& limits = MotionEngine::instance().getSoftLimits(a);
  if (limits.getLeftLimit()) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD.axisClearLimit=%c,LEFT", (a == AXIS_X) ? 'X' : 'Z');
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  } else {
    int32_t posDu = MotionEngine::instance().getAxisPositionDu(a);
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "CMD.axisLimitDu=%c,LEFT,%ld", (a == AXIS_X) ? 'X' : 'Z', (long)posDu);
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  }
}

static void buttonRightStopPress(AxisId a) {
  const auto& limits = MotionEngine::instance().getSoftLimits(a);
  if (limits.getRightLimit()) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "CMD.axisClearLimit=%c,RIGHT", (a == AXIS_X) ? 'X' : 'Z');
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  } else {
    int32_t posDu = MotionEngine::instance().getAxisPositionDu(a);
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "CMD.axisLimitDu=%c,RIGHT,%ld", (a == AXIS_X) ? 'X' : 'Z', (long)posDu);
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  }
}

static void buttonDisplayPress() {
  int next = SettingsService::getSpindleDisplayMode() + 1;
  if (next > 2) next = 0;
  SettingsService::setSpindleDisplayMode(next);
}

static void buttonMoveStepPress() {
  CommandDispatcher::dispatch("CMD.feedStepCycle", 17);
}

static void buttonMeasurePress() {
  int next = (static_cast<int>(currentMeasure()) + 1) % 3;
  char cmd[32];
  snprintf(cmd, sizeof(cmd), "CMD.measure=%d", next);
  CommandDispatcher::dispatch(cmd, strlen(cmd));
}

static void buttonReversePress() {
  CommandDispatcher::dispatch("CMD.feedRev", 11);
}

static int wsKeycode = 0;

static int heldRepeatKey = 0;
static unsigned long heldRepeatPressTime = 0;
static unsigned long heldRepeatLastStepTime = 0;

static void processHeldRepeatKey() {
  if (heldRepeatKey == 0) return;
  if (SettingsService::isOn() || inNumpad || emergencyStop != EstopCode::NONE) {
    heldRepeatKey = 0;
    return;
  }
  unsigned long now = millis();
  if (now - heldRepeatPressTime >= (unsigned long)sysConfig.timing.jogHoldDelayMs) {
    if (now - heldRepeatLastStepTime >= 200) {
      heldRepeatLastStepTime = now;
      buttonPlusMinusPress(heldRepeatKey == B_PLUS);
    }
  }
}

void processKeypadEvent() {
  processWebUiMoveFailsafe();
  int event = 0;
  bool eventUsesKeyboardMap = false;
  bool eventFromWebUi = false;
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
    if (isPress) buttonOnOffPress(false);
  }

  if (currentLatheMode() == LatheMode::GCODE && SettingsService::isOn()) {
    // Not allowed to interfere other than turn off.
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
  } else if (isPress && currentLatheMode() == LatheMode::GCODE && setupIndex == 1 && (keyCode == B_UP || keyCode == B_DOWN)) {
    if (gcodeProgramIndex > 0 && keyCode == B_UP) gcodeProgramIndex--;
    else if (gcodeProgramIndex == 0 && gcodeProgramCount > 0 && keyCode == B_UP) gcodeProgramIndex = gcodeProgramCount - 1;
    else if ((gcodeProgramIndex < gcodeProgramCount - 1) && keyCode == B_DOWN) gcodeProgramIndex++;
    else if (keyCode == B_DOWN) gcodeProgramIndex = 0;
  } else if (isPress && currentLatheMode() == LatheMode::GCODE && setupIndex == 1 && keyCode == B_MINUS) {
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
    if (!SettingsService::isOn() && !inNumpad) {
      heldRepeatKey = B_PLUS;
      heldRepeatPressTime = millis();
      heldRepeatLastStepTime = millis();
    }
    buttonPlusMinusPress(true);
  } else if (keyCode == B_MINUS) {
    if (!SettingsService::isOn() && !inNumpad) {
      heldRepeatKey = B_MINUS;
      heldRepeatPressTime = millis();
      heldRepeatLastStepTime = millis();
    }
    buttonPlusMinusPress(false);
  } else if (keyCode == B_ON) {
    buttonOnOffPress(true);
  } else if (keyCode == B_STOPL) {
    buttonLeftStopPress(AXIS_Z);
  } else if (keyCode == B_STOPR) {
    buttonRightStopPress(AXIS_Z);
  } else if (keyCode == B_STOPU) {
    buttonLeftStopPress(AXIS_X);
  } else if (keyCode == B_STOPD) {
    buttonRightStopPress(AXIS_X);
  } else if (keyCode == B_MODE_ELLIPSE) {
    setModeFromUi(LatheMode::ELLIPSE);
  } else if (keyCode == B_MODE_GCODE) {
    setModeFromUi(LatheMode::GCODE);
  } else if (keyCode == B_MODE_ASYNC) {
    setModeFromUi(LatheMode::ASYNC);
  } else if (keyCode == B_MULTISTART) {
    buttonMultistartPress();
  } else if (keyCode == B_DISPL) {
    buttonDisplayPress();
  } else if (keyCode == B_X) {
    CommandDispatcher::dispatch("CMD.axisZero=X", 14);
  } else if (keyCode == B_Z) {
    CommandDispatcher::dispatch("CMD.axisZero=Z", 14);
  } else if (keyCode == B_X_ENA) {
    // Motor enable toggle
  } else if (keyCode == B_Z_ENA) {
    // Motor enable toggle
  } else if (keyCode == B_STEP) {
    buttonMoveStepPress();
  } else if (keyCode == B_REVERSE) {
    buttonReversePress();
  } else if (keyCode == B_MEASURE) {
    buttonMeasurePress();
  } else if (keyCode == B_MODE_GEARS) {
    setModeFromUi(LatheMode::NORMAL);
  } else if (keyCode == B_MODE_TURN) {
    setModeFromUi(LatheMode::TURN);
  } else if (keyCode == B_MODE) {
    LatheMode curMode = currentLatheMode();
    LatheMode nextMode = LatheMode::NORMAL;
    if (curMode == LatheMode::NORMAL) nextMode = LatheMode::XGEAR;
    else if (curMode == LatheMode::XGEAR) nextMode = LatheMode::JOYSTICK;
    else if (curMode == LatheMode::JOYSTICK) nextMode = LatheMode::TURN;
    else if (curMode == LatheMode::TURN) nextMode = LatheMode::FACE;
    else if (curMode == LatheMode::FACE) nextMode = LatheMode::CONE;
    else if (curMode == LatheMode::CONE) nextMode = LatheMode::CUT;
    else if (curMode == LatheMode::CUT) nextMode = LatheMode::SLOT;
    else if (curMode == LatheMode::SLOT) nextMode = LatheMode::THREAD;
    else if (curMode == LatheMode::THREAD) nextMode = LatheMode::ELLIPSE;
    else if (curMode == LatheMode::ELLIPSE) nextMode = LatheMode::GCODE;
    else if (curMode == LatheMode::GCODE) nextMode = LatheMode::ASYNC;
    else if (curMode == LatheMode::ASYNC) nextMode = LatheMode::NORMAL;
    setModeFromUi(nextMode);
  } else if (keyCode == B_MODE_XGEAR) {
    setModeFromUi(LatheMode::XGEAR);
  } else if (keyCode == B_MODE_JOYSTICK) {
    setModeFromUi(LatheMode::JOYSTICK);
  } else if (keyCode == B_MODE_FACE) {
    setModeFromUi(LatheMode::FACE);
  } else if (keyCode == B_MODE_CONE) {
    setModeFromUi(LatheMode::CONE);
  } else if (keyCode == B_MODE_CUT) {
    setModeFromUi(LatheMode::CUT);
  } else if (keyCode == B_MODE_SLOT) {
    setModeFromUi(LatheMode::SLOT);
  } else if (keyCode == B_MODE_THREAD) {
    setModeFromUi(LatheMode::THREAD);
  } else if (keyCode == B_X_TOGGLE_DRO) {
    if (SettingsService::getActiveProfile().xAxis.hasDroScale) {
      char cmd[32];
      snprintf(cmd, sizeof(cmd), "SETTINGS.set=prefs.xDroActive=%d", SettingsService::isXDroMode() ? 0 : 1);
      CommandDispatcher::dispatch(cmd, strlen(cmd));
    }
  } else if (keyCode == B_Z_TOGGLE_DRO) {
    if (SettingsService::getActiveProfile().zAxis.hasDroScale) {
      char cmd[32];
      snprintf(cmd, sizeof(cmd), "SETTINGS.set=prefs.zDroActive=%d", SettingsService::isZDroMode() ? 0 : 1);
      CommandDispatcher::dispatch(cmd, strlen(cmd));
    }
  } else if (keyCode == B_DIA_RAD) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "SETTINGS.set=prefs.diaRad=%d", SettingsService::isDiameterMode() ? 0 : 1);
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  } else if (keyCode == B_ABS_INC) {
    char cmd[32];
    snprintf(cmd, sizeof(cmd), "SETTINGS.set=prefs.absInc=%d", SettingsService::isAbsoluteMode() ? 0 : 1);
    CommandDispatcher::dispatch(cmd, strlen(cmd));
  } else if (keyCode == B_SPINDLE_UNITS) {
    buttonDisplayPress();
  }
}

void taskKeypad(void *param) {
  while (emergencyStop == EstopCode::NONE) {
    processKeypadEvent();
    taskYIELD();
  }
  vTaskDelete(NULL);
}

void taskJoystick(void *param) {
  JoystickAxisState* joystickZ = &joystickAxes[JOYSTICK_AXIS_Z];
  JoystickAxisState* joystickX = &joystickAxes[JOYSTICK_AXIS_X];
  while (emergencyStop == EstopCode::NONE && joystickUsable()) {
    bool rawButtonPressed = digitalRead(JOY_BUTTON) == (sysConfig.joystick.invertButton ? HIGH : LOW);
    unsigned long now = micros();
    unsigned long elapsedUs = now - joystickSampleTimeUs;
    joystickSampleTimeUs = now;

    readJoystickAxis(joystickZ);
    readJoystickAxis(joystickX);

    bool moveNeutral = joystickZ->direction == 0 && joystickX->direction == 0;
    bool buttonPressed = updateJoystickButton(rawButtonPressed, moveNeutral);
    joystickRapidPressed = buttonPressed;

    if (currentLatheMode() == LatheMode::JOYSTICK) {
      bool manualJoystickMove = buttonPressed || !SettingsService::isOn();
      joystickLatheDirectionZ = joystickZ->direction;
      joystickLatheDirectionX = joystickX->direction;
      joystickLatheRapid = buttonPressed && !moveNeutral;
      updateJoystickManualAxis(joystickZ, manualJoystickMove, false, elapsedUs);
      updateJoystickManualAxis(joystickX, manualJoystickMove, false, elapsedUs);
    } else {
      joystickLatheDirectionZ = 0;
      joystickLatheDirectionX = 0;
      joystickLatheRapid = false;
      updateJoystickManualAxis(joystickZ, true, true, elapsedUs);
      updateJoystickManualAxis(joystickX, true, true, elapsedUs);
    }

    DELAY(sysConfig.joystick.sampleIntervalMs);
  }
  vTaskDelete(NULL);
}
