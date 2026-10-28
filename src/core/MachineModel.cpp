#include "MachineModel.h"
#include "SettingsService.h"
#include "motion/DRO_Scale.h"
#include "motion/kernel/MotionEngine.h"

static long getAxisCanonicalPosDu(AxisId a) {
  if (a == AXIS_X && SettingsService::isXDroMode()) {
    return (long)round(xScale.getPosition() * 10000.0f);
  } else if (a == AXIS_Z && SettingsService::isZDroMode()) {
    return (long)round(zScale.getPosition() * 10000.0f);
  }
  return MotionEngine::instance().getAxisPositionDu(a);
}

static AxisLimits getAxisLimits(AxisId axis) {
  AxisLimits lims;
  const SoftLimitTable& limits = MotionEngine::instance().getSoftLimits(axis);
  const SoftLimit* leftOrUp = limits.getLeftLimit();
  if (leftOrUp && leftOrUp->isEnforced()) {
    lims.leftOrUpDu = leftOrUp->coordDu;
  }
  const SoftLimit* rightOrDown = limits.getRightLimit();
  if (rightOrDown && rightOrDown->isEnforced()) {
    lims.rightOrDownDu = rightOrDown->coordDu;
  }
  return lims;
}

KinematicsState MachineModel::captureKinematics() {
  KinematicsState k;
  MotionEngine& engine = MotionEngine::instance();

  // 1. Physical Axis Dynamics
  k.z.posDu = getAxisCanonicalPosDu(AXIS_Z);
  k.z.limits = getAxisLimits(AXIS_Z);
  k.z.disabled = false;

  k.x.posDu = getAxisCanonicalPosDu(AXIS_X);
  k.x.limits = getAxisLimits(AXIS_X);
  k.x.disabled = false;

  // 2. Real-Time Core 1 Overlay from Lock-Free TelemetryMailbox ([CONC-01])
  MotionTelemetrySnapshot tSnap;
  bool hasMailbox = engine.getTelemetryMailbox().read(tSnap);
  if (hasMailbox) {
    if (!SettingsService::isZDroMode()) {
      k.z.posDu = tSnap.zCoordDu;
    }
    if (!SettingsService::isXDroMode()) {
      k.x.posDu = tSnap.xCoordDu;
    }
    k.spindle.rpm = tSnap.spindleRpm;

    const LatheConfiguration& profile = SettingsService::getActiveProfile();
    int32_t cpr = (engine.getEncoderHAL() && engine.getEncoderHAL()->getCountsPerSpindleRev() > 0)
                      ? engine.getEncoderHAL()->getCountsPerSpindleRev()
                      : static_cast<int32_t>(round(profile.spindle.countsPerRev()));
    if (cpr > 0) {
      k.spindle.turns = (float)tSnap.spindlePulses / (float)cpr;
      int64_t moduloPulses = tSnap.spindlePulses % cpr;
      if (moduloPulses < 0) moduloPulses += cpr;
      k.spindle.angle = (float)moduloPulses * 360.0f / (float)cpr;
    }
  }

  // 3. Motion State & Mode
  k.mode = static_cast<uint8_t>(SettingsService::getMode());
  k.isOn = SettingsService::isOn() || (hasMailbox && tSnap.isMoving);
  k.estop = SettingsService::getEstopCode() != 0;
  k.dupr = SettingsService::getThreadSetting().dupr;
  k.nextDupr = k.dupr;

  return k;
}

IntentState MachineModel::captureIntent() {
  IntentState i = {};
  i.thread = SettingsService::getThreadSetting();
  i.taper = SettingsService::getTaperSetting();
  i.feedStepDu = SettingsService::getPreferences().feedStepDu;
  i.jogStepDu = SettingsService::getJogStepDu();
  i.feedReverse = (i.thread.dupr < 0);
  return i;
}

CycleState MachineModel::captureCycle() {
  CycleState c = {};
  c.turnPasses = SettingsService::getTurnPasses();
  c.currentPass = 0;
  c.setupIndex = 0;
  c.starts = SettingsService::getStarts();
  return c;
}

MachineProperties MachineModel::captureProperties() {
  MachineProperties p = {};
  const LatheConfiguration& profile = SettingsService::getActiveProfile();
  p.capabilities = profile.capabilities;
  strncpy(p.presetId, profile.profileId, sizeof(p.presetId) - 1);
  p.presetId[sizeof(p.presetId) - 1] = '\0';
  p.measure = static_cast<int>(SettingsService::getMeasure());
  p.isDiameterMode = SettingsService::isDiameterMode();
  p.isAbsoluteMode = SettingsService::isAbsoluteMode();
  p.zDroActive = SettingsService::isZDroMode();
  p.xDroActive = SettingsService::isXDroMode();
  p.screwZDu = profile.zAxis.leadscrewPitchDu();
  p.screwXDu = profile.xAxis.leadscrewPitchDu();
  p.motorStepsZ = profile.zAxis.motorSteps();
  p.motorStepsX = profile.xAxis.motorSteps();
  p.encoderPpr = (long)round(profile.spindle.effectivePpr());
  p.deadbandAngle = SettingsService::getDeadbandAngle();
  p.deadbandAngleAuto = SettingsService::isDeadbandAngleAuto();
  p.deadbandTurns = SettingsService::getDeadbandTurns();
  p.deadbandTurnsAuto = SettingsService::isDeadbandTurnsAuto();
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

  float angleThreshold = SettingsService::getDeadbandAngle();
  float turnsThreshold = SettingsService::getDeadbandTurns();

  if (prev.kinematics.z != current.kinematics.z ||
      prev.kinematics.x != current.kinematics.x ||
      prev.kinematics.spindle.rpm != current.kinematics.spindle.rpm ||
      fabsf(prev.kinematics.spindle.turns - current.kinematics.spindle.turns) > turnsThreshold ||
      fabsf(prev.kinematics.spindle.angle - current.kinematics.spindle.angle) > angleThreshold ||
      prev.kinematics.mode != current.kinematics.mode ||
      prev.kinematics.isOn != current.kinematics.isOn ||
      prev.kinematics.estop != current.kinematics.estop ||
      prev.kinematics.dupr != current.kinematics.dupr ||
      prev.kinematics.nextDupr != current.kinematics.nextDupr) {
    changed |= DOMAIN_KINEMATICS;
  }

  if (prev.intent.thread.format != current.intent.thread.format ||
      fabsf(prev.intent.thread.authorPitch - current.intent.thread.authorPitch) > 0.0001f ||
      prev.intent.thread.dupr != current.intent.thread.dupr ||
      prev.intent.taper.modeType != current.intent.taper.modeType ||
      fabsf(prev.intent.taper.authorVal - current.intent.taper.authorVal) > 0.0001f ||
      fabsf(prev.intent.taper.coneRatio - current.intent.taper.coneRatio) > 0.000001f ||
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
      prev.properties.encoderPpr != current.properties.encoderPpr ||
      prev.properties.deadbandAngle != current.properties.deadbandAngle ||
      prev.properties.deadbandAngleAuto != current.properties.deadbandAngleAuto ||
      prev.properties.deadbandTurns != current.properties.deadbandTurns ||
      prev.properties.deadbandTurnsAuto != current.properties.deadbandTurnsAuto) {
    changed |= DOMAIN_PROPERTIES;
  }

  return changed;
}

bool MachineModel::hasChanged(const MachineModel& prev, const MachineModel& current, uint8_t domainMask) {
  return (detectChangedDomains(prev, current) & domainMask) != 0;
}


