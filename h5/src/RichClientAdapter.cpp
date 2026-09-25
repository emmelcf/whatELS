#include "RichClientAdapter.h"
#include "Config.h"
#include "Axis.h"
#include "Motion.h"
#include "Input.h"
#include "Display.h"

static unsigned long lastTelemetryHash = 0;

static long getAxisCanonicalPosDu(Axis* a) {
  if (a == &x && xDisplayModeDRO) {
    return (long)round(xScale.getPosition() * 10000.0f);
  } else if (a == &z && zDisplayModeDRO) {
    return (long)round(zScale.getPosition() * 10000.0f);
  }
  return getAxisPosDu(a);
}

static long getAxisCanonicalStopDu(Axis* a, bool isLeft) {
  if (isLeft) {
    if (a->leftStop == LONG_MAX) return LONG_MAX;
    return stepsToDu(a, a->leftStop + a->originPos);
  } else {
    if (a->rightStop == LONG_MIN) return LONG_MIN;
    return stepsToDu(a, a->rightStop + a->originPos);
  }
}

String RichClientAdapter::buildTelemetry() {
  String out = "";
  out.reserve(512);

  out += "M.mode=" + String(mode) + "\n";
  out += "M.on=" + String(isOn ? 1 : 0) + "\n";
  out += "M.estop=" + String(emergencyStop) + "\n";
  out += "M.dupr=" + String(dupr) + "\n";
  out += "M.nextDupr=" + String(nextDuprFlag ? nextDupr : dupr) + "\n";
  
  // Authoritative Intent: Thread
  out += "M.thFmt=" + String((int)currentThreadSetting.format) + "\n";
  out += "M.thPitch=" + String(currentThreadSetting.authorPitch, 4) + "\n";

  // Authoritative Intent: Taper
  out += "M.tpMode=" + String((int)currentTaperSetting.modeType) + "\n";
  out += "M.tpVal=" + String(currentTaperSetting.authorVal, 4) + "\n";
  out += "M.coneRatio=" + String(coneRatio, 6) + "\n";
  out += "M.tpPreset=" + String(currentTaperSetting.presetId) + "\n";

  // Feed & Jog Steps
  out += "M.feedStepDu=" + String(feedStepDu) + "\n";
  out += "M.jogStepDu=" + String(moveStep) + "\n";
  out += "M.measure=" + String(measure) + "\n";

  // Axes in Deci-microns (du)
  long zPosDu = getAxisCanonicalPosDu(&z);
  long xPosDu = getAxisCanonicalPosDu(&x);
  long yPosDu = getAxisCanonicalPosDu(&y);
  out += "M.zDu=" + String(zPosDu) + "\n";
  out += "M.xDu=" + String(xPosDu) + "\n";
  out += "M.yDu=" + String(yPosDu) + "\n";

  // Limits in Deci-microns (du)
  long zLeftDu = getAxisCanonicalStopDu(&z, true);
  long zRightDu = getAxisCanonicalStopDu(&z, false);
  long xUpDu = getAxisCanonicalStopDu(&x, true);
  long xDownDu = getAxisCanonicalStopDu(&x, false);
  out += "M.zLeftDu=" + String(zLeftDu) + "\n";
  out += "M.zRightDu=" + String(zRightDu) + "\n";
  out += "M.xUpDu=" + String(xUpDu) + "\n";
  out += "M.xDownDu=" + String(xDownDu) + "\n";

  // Axis States
  out += "M.zDis=" + String(z.disabled ? 1 : 0) + "\n";
  out += "M.xDis=" + String(x.disabled ? 1 : 0) + "\n";
  out += "M.yDis=" + String(y.disabled ? 1 : 0) + "\n";
  out += "M.dia=" + String(isDiameterMode ? 1 : 0) + "\n";
  out += "M.abs=" + String(isAbsoluteMode ? 1 : 0) + "\n";
  out += "M.zDro=" + String(zDisplayModeDRO ? 1 : 0) + "\n";
  out += "M.xDro=" + String(xDisplayModeDRO ? 1 : 0) + "\n";

  // Spindle & Pass Operations
  long turns = ENCODER_PPR > 0 ? (spindlePos / ENCODER_PPR) : 0;
  long angle = ENCODER_PPR > 0 ? (abs(spindlePos) % ENCODER_PPR) * 3600 / ENCODER_PPR : 0;
  out += "M.rpm=" + String(shownRpm) + "\n";
  out += "M.turns=" + String(turns) + "\n";
  out += "M.angle=" + String(angle) + "\n";
  out += "M.starts=" + String(starts) + "\n";
  out += "M.passes=" + String(turnPasses) + "\n";
  out += "M.opIdx=" + String(opIndex) + "\n";
  out += "M.setupIdx=" + String(setupIndex) + "\n";

  return out;
}

bool RichClientAdapter::hasStateChanged() {
  unsigned long hash = 0;
  hash += (unsigned long)mode * 31;
  hash += (unsigned long)(isOn ? 1 : 0) * 17;
  hash += (unsigned long)dupr;
  hash += (unsigned long)round(currentThreadSetting.authorPitch * 1000.0f);
  hash += (unsigned long)round(currentTaperSetting.authorVal * 1000.0f);
  hash += (unsigned long)feedStepDu;
  hash += (unsigned long)moveStep;
  hash += (unsigned long)getAxisCanonicalPosDu(&z);
  hash += (unsigned long)getAxisCanonicalPosDu(&x) * 3;
  hash += (unsigned long)getAxisCanonicalStopDu(&z, true);
  hash += (unsigned long)getAxisCanonicalStopDu(&z, false);
  hash += (unsigned long)getAxisCanonicalStopDu(&x, true);
  hash += (unsigned long)getAxisCanonicalStopDu(&x, false);
  hash += (unsigned long)shownRpm * 7;
  hash += (unsigned long)emergencyStop * 13;
  hash += (unsigned long)(z.disabled ? 1 : 0);
  hash += (unsigned long)(x.disabled ? 1 : 0);
  hash += (unsigned long)(isDiameterMode ? 1 : 0);
  hash += (unsigned long)(isAbsoluteMode ? 1 : 0);

  if (hash != lastTelemetryHash) {
    lastTelemetryHash = hash;
    return true;
  }
  return false;
}
