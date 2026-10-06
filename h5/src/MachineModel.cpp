#include "MachineModel.h"
#include "Axis.h"
#include "Config.h"
#include "Motion.h"
#include "Input.h"
#include "Display.h"
#include "DisplayModel.h"
#include <limits.h>

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

KinematicsState MachineModel::captureKinematics() {
  KinematicsState k = {};
  k.zDu = getAxisCanonicalPosDu(&z);
  k.xDu = getAxisCanonicalPosDu(&x);
  k.yDu = getAxisCanonicalPosDu(&y);
  k.zLeftDu = getAxisCanonicalStopDu(&z, true);
  k.zRightDu = getAxisCanonicalStopDu(&z, false);
  k.xUpDu = getAxisCanonicalStopDu(&x, true);
  k.xDownDu = getAxisCanonicalStopDu(&x, false);
  k.zDisabled = z.disabled;
  k.xDisabled = x.disabled;
  k.yDisabled = y.disabled;
  k.rpm = getApproxRpm();
  k.turns = (ENCODER_PPR > 0) ? (spindlePos / ENCODER_PPR) : 0;
  k.angle = (ENCODER_PPR > 0) ? (abs(spindlePos) % ENCODER_PPR) * 3600 / ENCODER_PPR : 0;
  k.mode = mode;
  k.isOn = isOn;
  k.estop = emergencyStop;
  k.dupr = dupr;
  k.nextDupr = nextDuprFlag ? nextDupr : dupr;
  return k;
}

IntentState MachineModel::captureIntent() {
  IntentState i = {};
  i.thread = currentThreadSetting;
  i.taper = currentTaperSetting;
  i.feedStepDu = feedStepDu;
  i.jogStepDu = moveStep;
  long activeDupr = nextDuprFlag ? nextDupr : dupr;
  i.feedReverse = (activeDupr < 0);
  return i;
}

CycleState MachineModel::captureCycle() {
  CycleState c = {};
  c.turnPasses = turnPasses;
  c.currentPass = opIndex;
  c.setupIndex = setupIndex;
  c.starts = starts;
  return c;
}

MachineProperties MachineModel::captureProperties() {
  MachineProperties p = {};
  p.measure = measure;
  p.isDiameterMode = isDiameterMode;
  p.isAbsoluteMode = isAbsoluteMode;
  p.zDroActive = zDisplayModeDRO;
  p.xDroActive = xDisplayModeDRO;
  p.screwZDu = (long)round(z.screwPitch * 10000.0f);
  p.screwXDu = (long)round(x.screwPitch * 10000.0f);
  p.motorStepsZ = (long)round(z.motorSteps);
  p.motorStepsX = (long)round(x.motorSteps);
  p.encoderPpr = ENCODER_PPR;
  return p;
}

MachineModel MachineModel::captureAll() {
  MachineModel m;
  m.kinematics = captureKinematics();
  m.intent = captureIntent();
  m.cycle = captureCycle();
  m.properties = captureProperties();
  return m;
}

uint8_t MachineModel::detectChangedDomains(const MachineModel& prev, const MachineModel& current) {
  uint8_t changed = DOMAIN_NONE;

  if (prev.kinematics.zDu != current.kinematics.zDu ||
      prev.kinematics.xDu != current.kinematics.xDu ||
      prev.kinematics.yDu != current.kinematics.yDu ||
      prev.kinematics.zLeftDu != current.kinematics.zLeftDu ||
      prev.kinematics.zRightDu != current.kinematics.zRightDu ||
      prev.kinematics.xUpDu != current.kinematics.xUpDu ||
      prev.kinematics.xDownDu != current.kinematics.xDownDu ||
      prev.kinematics.zDisabled != current.kinematics.zDisabled ||
      prev.kinematics.xDisabled != current.kinematics.xDisabled ||
      prev.kinematics.yDisabled != current.kinematics.yDisabled ||
      prev.kinematics.rpm != current.kinematics.rpm ||
      prev.kinematics.turns != current.kinematics.turns ||
      prev.kinematics.angle != current.kinematics.angle ||
      prev.kinematics.mode != current.kinematics.mode ||
      prev.kinematics.isOn != current.kinematics.isOn ||
      prev.kinematics.estop != current.kinematics.estop ||
      prev.kinematics.dupr != current.kinematics.dupr ||
      prev.kinematics.nextDupr != current.kinematics.nextDupr) {
    changed |= DOMAIN_KINEMATICS;
  }

  if (prev.intent.thread.format != current.intent.thread.format ||
      abs(prev.intent.thread.authorPitch - current.intent.thread.authorPitch) > 0.0001f ||
      prev.intent.thread.dupr != current.intent.thread.dupr ||
      prev.intent.taper.modeType != current.intent.taper.modeType ||
      abs(prev.intent.taper.authorVal - current.intent.taper.authorVal) > 0.0001f ||
      abs(prev.intent.taper.coneRatio - current.intent.taper.coneRatio) > 0.000001f ||
      prev.intent.taper.presetId != current.intent.taper.presetId ||
      prev.intent.feedStepDu != current.intent.feedStepDu ||
      prev.intent.jogStepDu != current.intent.jogStepDu ||
      prev.intent.feedReverse != current.intent.feedReverse) {
    changed |= DOMAIN_INTENT;
  }

  if (prev.cycle.turnPasses != current.cycle.turnPasses ||
      prev.cycle.currentPass != current.cycle.currentPass ||
      prev.cycle.setupIndex != current.cycle.setupIndex ||
      prev.cycle.starts != current.cycle.starts) {
    changed |= DOMAIN_CYCLE;
  }

  if (prev.properties.measure != current.properties.measure ||
      prev.properties.isDiameterMode != current.properties.isDiameterMode ||
      prev.properties.isAbsoluteMode != current.properties.isAbsoluteMode ||
      prev.properties.zDroActive != current.properties.zDroActive ||
      prev.properties.xDroActive != current.properties.xDroActive ||
      prev.properties.screwZDu != current.properties.screwZDu ||
      prev.properties.screwXDu != current.properties.screwXDu ||
      prev.properties.motorStepsZ != current.properties.motorStepsZ ||
      prev.properties.motorStepsX != current.properties.motorStepsX ||
      prev.properties.encoderPpr != current.properties.encoderPpr) {
    changed |= DOMAIN_PROPERTIES;
  }

  return changed;
}

bool MachineModel::hasChanged(const MachineModel& prev, const MachineModel& current, uint8_t domainMask) {
  return (detectChangedDomains(prev, current) & domainMask) != 0;
}

uint8_t parseDomainQuery(const char* query) {
  if (!query || *query == '\0') return DOMAIN_ALL;

  while (*query == ' ' || *query == '\t') query++;
  if (strncmp(query, "?get=", 5) == 0) query += 5;
  else if (strncmp(query, "get=", 4) == 0) query += 4;
  else if (strncmp(query, "?state", 6) == 0) return DOMAIN_ALL;

  while (*query == ' ' || *query == '\t') query++;
  if (*query == '\0') return DOMAIN_ALL;

  uint8_t mask = DOMAIN_NONE;
  char buf[64];
  strncpy(buf, query, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = '\0';

  char* token = strtok(buf, ",;| \r\n");
  while (token) {
    if (strcasecmp(token, "K") == 0 || strcasecmp(token, "kinematics") == 0 || strcasecmp(token, "kin") == 0) {
      mask |= DOMAIN_KINEMATICS;
    } else if (strcasecmp(token, "I") == 0 || strcasecmp(token, "intent") == 0) {
      mask |= DOMAIN_INTENT;
    } else if (strcasecmp(token, "C") == 0 || strcasecmp(token, "cycle") == 0) {
      mask |= DOMAIN_CYCLE;
    } else if (strcasecmp(token, "P") == 0 || strcasecmp(token, "properties") == 0 || strcasecmp(token, "prop") == 0 || strcasecmp(token, "props") == 0) {
      mask |= DOMAIN_PROPERTIES;
    } else if (strcasecmp(token, "ALL") == 0 || strcmp(token, "*") == 0) {
      mask |= DOMAIN_ALL;
    }
    token = strtok(nullptr, ",;| \r\n");
  }

  return (mask == DOMAIN_NONE) ? DOMAIN_ALL : mask;
}
