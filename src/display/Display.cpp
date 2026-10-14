#include "Display.h"
#include "Config.h"
#include "Axis.h"
#include "Input.h"
#include "Motion.h"
#include "WebServerModule.h"
#include <Preferences.h>
#include "SettingsService.h"

// LCD Hash Tracking
long lcdHashLine0 = LCD_HASH_INITIAL;
long lcdHashLine1 = LCD_HASH_INITIAL;
long lcdHashLine2 = LCD_HASH_INITIAL;
long lcdHashLine3 = LCD_HASH_INITIAL;

unsigned long saveTime = 0;
const int COLOR_NUMPAD_DEFAULT = 65535;

static unsigned long lastDisplayUpdateTime = 0;
static const unsigned long NEXTION_UPDATE_INTERVAL_MS = 100;
static const unsigned long NEXTION_WEB_UPDATE_INTERVAL_MS = 200;

void writeNextionCommandRaw(const String &command) {
  Serial1.print(command);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}

void toScreen(const String &command) {
  if (tftUploadActive) return;
  writeNextionCommandRaw(command);
}

void setText(const String &id, const String &text) {
  toScreen(id + ".txt=\"" + text + "\"");
}

void beep() {
  toScreen("play 0,0,0");
}

unsigned long getDisplayUpdateIntervalMs() {
  return webUiIsConnected() ? NEXTION_WEB_UPDATE_INTERVAL_MS : NEXTION_UPDATE_INTERVAL_MS;
}

void updateDisplay() {
  if (tftUploadActive) return;
  if (millis() - lastDisplayUpdateTime < getDisplayUpdateIntervalMs()) return;
  lastDisplayUpdateTime = millis();
  bool publishStatus = webBuffersReady && webUiIsConnected();

  static bool forceNextionFullRefresh = true;

  int rpm = getApproxRpm();
  long numpadResult = getNumpadResult();
  bool spindleStopped = micros() > spindleEncTime + 100000;
  int pitchStatusDirection = getJoystickPitchStatusDirection();

  DisplaySnapshot snapshot = getDisplaySnapshot(rpm, numpadResult, spindleStopped, pitchStatusDirection);

  long newHashLine0 = isOn + spindlePosSync + static_cast<int>(mode) + static_cast<int>(measure) + dupr + starts;
  bool updateLine0 = forceNextionFullRefresh || (lcdHashLine0 != newHashLine0);
  if (updateLine0) {
    lcdHashLine0 = newHashLine0;
    setText("bStatus", snapshot.status);
    setText("bMode", snapshot.mode);
    setText("tPitch", snapshot.pitch);
    setText("bMeasure", snapshot.measure);
  }

  long newHashLine1 = moveStep + rpm + spindlePos + static_cast<int>(measure) + SettingsService::getSpindleDisplayMode() * 17 + (snapshot.spindleValColorGreen ? 300 : 0);
  bool updateLine1 = forceNextionFullRefresh || (lcdHashLine1 != newHashLine1);
  if (updateLine1) {
    lcdHashLine1 = newHashLine1;
    setText("tStepVal", snapshot.step);
    setText("tRPMVal", snapshot.rpmText);
    setText("tTurnsVal", snapshot.turnsText);
    setText("tAngleVal", snapshot.angleText);

    setText("tSpindleTog", snapshot.spindleLabel);
    setText("tSpindleVal", snapshot.spindleValue);

    if (snapshot.spindleValColorGreen) {
      toScreen("tSpindleVal.pco=" + String(sysConfig.theme.activeHighlight));
    } else {
      toScreen("tSpindleVal.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
  }

  long newHashLine2 =
    x.pos + x.originPos + x.disabled +
    (x.leftStop == LONG_MAX ? 10007 : x.leftStop) - (x.rightStop == LONG_MIN ? -10007 : x.rightStop) +
    z.pos + z.originPos + z.disabled +
    (z.leftStop == LONG_MAX ? 20011 : z.leftStop) - (z.rightStop == LONG_MIN ? -20011 : z.rightStop) +
    static_cast<int>(measure) + x.pos % 100 +
    (snapshot.xIsDroActive ? (long)(xScale.getRawPosition() * 1000) : 0) +
    (snapshot.zIsDroActive ? (long)(zScale.getRawPosition() * 1000) : 0) +
    snapshot.isDiameterMode + snapshot.isAbsoluteMode + snapshot.xIsDroActive + snapshot.zIsDroActive;
  bool updateLine2 = forceNextionFullRefresh || (lcdHashLine2 != newHashLine2);
  if (updateLine2) {
    lcdHashLine2 = newHashLine2;
    setText("tX", snapshot.xPos);
    if (snapshot.xIsDroActive) {
      toScreen("tX.pco=" + String(sysConfig.theme.activeHighlight));
    } else {
      toScreen("tX.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tXUp", snapshot.xStopUp);
    setText("tXDown", snapshot.xStopDown);
    setText("tY", snapshot.yPos);
    setText("tYUp", snapshot.yStopUp);
    setText("tYDown", snapshot.yStopDown);
    setText("tZ", snapshot.zPos);
    if (snapshot.zIsDroActive) {
      toScreen("tZ.pco=" + String(sysConfig.theme.activeHighlight));
    } else {
      toScreen("tZ.pco=" + String(COLOR_NUMPAD_DEFAULT));
    }
    setText("tZLeft", snapshot.zStopLeft);
    setText("tZRight", snapshot.zStopRight);

    if (snapshot.isDiameterMode) {
      toScreen("tDia.pco=" + String(sysConfig.theme.activeHighlight));
      toScreen("tDia.font=3");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(sysConfig.theme.inactiveDim));
      toScreen("tRad.font=2");
      setText("tRad", "rad");
    } else {
      toScreen("tDia.pco=" + String(sysConfig.theme.inactiveDim));
      toScreen("tDia.font=2");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(sysConfig.theme.activeHighlight));
      toScreen("tRad.font=3");
      setText("tRad", "rad");
    }

    if (!snapshot.isAbsoluteMode) {
      toScreen("tInc.pco=" + String(sysConfig.theme.activeHighlight));
      toScreen("tInc.font=3");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(sysConfig.theme.inactiveDim));
      toScreen("tAbs.font=2");
      setText("tAbs", "abs");
    } else {
      toScreen("tInc.pco=" + String(sysConfig.theme.inactiveDim));
      toScreen("tInc.font=2");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(sysConfig.theme.activeHighlight));
      toScreen("tAbs.font=3");
      setText("tAbs", "abs");
    }
  }

  long gcodeCommandHash = 0;
  for (int i = 0; i < gcodeCommand.length(); i++) {
    gcodeCommandHash += gcodeCommand.charAt(i);
  }
  for (int i = 0; i < wifiStatus.length(); i++) {
    gcodeCommandHash += wifiStatus.charAt(i);
  }
  long newHashLine3 = z.pos + (showAngle ? spindlePos : -1) + (showTacho ? rpm : -2) + static_cast<int>(measure) + (numpadResult > 0 ? numpadResult : -1) + static_cast<int>(mode) * 5 + dupr +
      (mode == LatheMode::CONE ? round(coneRatio * 10000) : 0) + turnPasses + opIndex + setupIndex + gcodeProgramIndex + gcodeProgramCount + spindleStopped * 3 + (isOn ? 139 : -117) + (inNumpad ? 10 : 0) + (auxForward ? 17 : -31) +
      (z.leftStop == LONG_MAX ? 123 : z.leftStop) + (z.rightStop == LONG_MIN ? 1234 : z.rightStop) +
      (x.leftStop == LONG_MAX ? 1235 : x.leftStop) + (x.rightStop == LONG_MIN ? 123456 : x.rightStop) + gcodeCommandHash +
      pitchStatusDirection * 149 +
      (mode == LatheMode::JOYSTICK ? joystickLatheDirectionZ * 97 + joystickLatheDirectionX * 101 + joystickLatheFeedSignZ * 131 + joystickLatheFeedSignX * 137 + joystickLatheRapid * 17 + sysConfig.joystick.enabled * 19 + spindlePosSync * 151 : 0) + x.pos + x.originPos + z.pos;
  bool updateLine3 = forceNextionFullRefresh || (lcdHashLine3 != newHashLine3);
  if (updateLine3) {
    lcdHashLine3 = newHashLine3;
    setText("t3", snapshot.message);
  }

  forceNextionFullRefresh = false;

  if (publishStatus) {
    publishMachineStatus(snapshot.status, snapshot.mode, snapshot.pitch, snapshot.measure, snapshot.step, snapshot.rpmText, snapshot.turnsText, snapshot.angleText,
      snapshot.xPos, snapshot.xStopUp, snapshot.xStopDown, snapshot.yPos, snapshot.yStopUp, snapshot.yStopDown, snapshot.zPos, snapshot.zStopLeft, snapshot.zStopRight, snapshot.message,
      snapshot.isDiameterMode, snapshot.isAbsoluteMode, snapshot.xIsDroActive, snapshot.zIsDroActive);
  }
}

// Periodic NVS flash sweeping has been retired in favor of event-driven sparse persistence in SettingsService.
bool saveIfChanged() {
  return false;
}

void taskDisplay(void *param) {
  toScreen("page 0");
  delay(50);
  while (emergencyStop == EstopCode::NONE) {
    updateDisplay();
    // Periodic NVS flash sweeping has been retired in favor of event-driven sparse persistence in SettingsService.
    if (beepFlag) {
      beepFlag = false;
      beep();
    }
    if (abs(z.pendingPos) > z.estopSteps || abs(x.pendingPos) > x.estopSteps) {
      setEmergencyStop(EstopCode::POS);
    }
    taskYIELD();
  }
  setText("bMode", "ESTOP");
  if (emergencyStop == EstopCode::POS) {
    setText("t3", "Requested position outside machine");
  } else if (emergencyStop == EstopCode::MARK_ORIGIN) {
    setText("t3", "Unable to mark origin");
  } else if (emergencyStop == EstopCode::ON_OFF) {
    setText("t3", "Unable to turn on/off");
  } else if (emergencyStop == EstopCode::OFF_MANUAL_MOVE) {
    setText("t3", "Off during manual move");
  }
  vTaskDelete(NULL);
}
