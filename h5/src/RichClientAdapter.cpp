#include "RichClientAdapter.h"
#include "MachineModel.h"

static unsigned long lastTelemetryHash = 0;

static void serializeKinematics(String* out, const KinematicsState& k) {
  *out += "K.zDu=" + String(k.zDu) + "\n";
  *out += "K.xDu=" + String(k.xDu) + "\n";
  *out += "K.yDu=" + String(k.yDu) + "\n";
  *out += "K.zLeftDu=" + String(k.zLeftDu) + "\n";
  *out += "K.zRightDu=" + String(k.zRightDu) + "\n";
  *out += "K.xUpDu=" + String(k.xUpDu) + "\n";
  *out += "K.xDownDu=" + String(k.xDownDu) + "\n";
  *out += "K.zDis=" + String(k.zDisabled ? 1 : 0) + "\n";
  *out += "K.xDis=" + String(k.xDisabled ? 1 : 0) + "\n";
  *out += "K.yDis=" + String(k.yDisabled ? 1 : 0) + "\n";
  *out += "K.rpm=" + String(k.rpm) + "\n";
  *out += "K.turns=" + String(k.turns) + "\n";
  *out += "K.angle=" + String(k.angle) + "\n";
  *out += "K.mode=" + String(k.mode) + "\n";
  *out += "K.on=" + String(k.isOn ? 1 : 0) + "\n";
  *out += "K.estop=" + String(k.estop ? 1 : 0) + "\n";
  *out += "K.dupr=" + String(k.dupr) + "\n";
  *out += "K.nextDupr=" + String(k.nextDupr) + "\n";
}

static void serializeIntent(String* out, const IntentState& i) {
  *out += "I.thFmt=" + String((int)i.thread.format) + "\n";
  *out += "I.thPitch=" + String(i.thread.authorPitch, 4) + "\n";
  *out += "I.dupr=" + String(i.thread.dupr) + "\n";
  *out += "I.tpMode=" + String((int)i.taper.modeType) + "\n";
  *out += "I.tpVal=" + String(i.taper.authorVal, 4) + "\n";
  *out += "I.coneRatio=" + String(i.taper.coneRatio, 6) + "\n";
  *out += "I.tpPreset=" + String(i.taper.presetId) + "\n";
  *out += "I.feedStepDu=" + String(i.feedStepDu) + "\n";
  *out += "I.jogStepDu=" + String(i.jogStepDu) + "\n";
  *out += "I.feedRev=" + String(i.feedReverse ? 1 : 0) + "\n";
}

static void serializeCycle(String* out, const CycleState& c) {
  *out += "C.passes=" + String(c.turnPasses) + "\n";
  *out += "C.opIdx=" + String(c.currentPass) + "\n";
  *out += "C.setupIdx=" + String(c.setupIndex) + "\n";
  *out += "C.starts=" + String(c.starts) + "\n";
}

static void serializeProperties(String* out, const MachineProperties& p) {
  *out += "P.measure=" + String(p.measure) + "\n";
  *out += "P.dia=" + String(p.isDiameterMode ? 1 : 0) + "\n";
  *out += "P.abs=" + String(p.isAbsoluteMode ? 1 : 0) + "\n";
  *out += "P.zDro=" + String(p.zDroActive ? 1 : 0) + "\n";
  *out += "P.xDro=" + String(p.xDroActive ? 1 : 0) + "\n";
  *out += "P.screwZDu=" + String(p.screwZDu) + "\n";
  *out += "P.screwXDu=" + String(p.screwXDu) + "\n";
  *out += "P.motorStepsZ=" + String(p.motorStepsZ) + "\n";
  *out += "P.motorStepsX=" + String(p.motorStepsX) + "\n";
  *out += "P.encoderPpr=" + String(p.encoderPpr) + "\n";
}

static void serializeLegacyM(String* out, const MachineModel& m) {
  *out += "M.mode=" + String(m.kinematics.mode) + "\n";
  *out += "M.on=" + String(m.kinematics.isOn ? 1 : 0) + "\n";
  *out += "M.estop=" + String(m.kinematics.estop ? 1 : 0) + "\n";
  *out += "M.dupr=" + String(m.kinematics.dupr) + "\n";
  *out += "M.nextDupr=" + String(m.kinematics.nextDupr) + "\n";
  *out += "M.thFmt=" + String((int)m.intent.thread.format) + "\n";
  *out += "M.thPitch=" + String(m.intent.thread.authorPitch, 4) + "\n";
  *out += "M.tpMode=" + String((int)m.intent.taper.modeType) + "\n";
  *out += "M.tpVal=" + String(m.intent.taper.authorVal, 4) + "\n";
  *out += "M.coneRatio=" + String(m.intent.taper.coneRatio, 6) + "\n";
  *out += "M.tpPreset=" + String(m.intent.taper.presetId) + "\n";
  *out += "M.feedStepDu=" + String(m.intent.feedStepDu) + "\n";
  *out += "M.jogStepDu=" + String(m.intent.jogStepDu) + "\n";
  *out += "M.measure=" + String(m.properties.measure) + "\n";
  *out += "M.zDu=" + String(m.kinematics.zDu) + "\n";
  *out += "M.xDu=" + String(m.kinematics.xDu) + "\n";
  *out += "M.yDu=" + String(m.kinematics.yDu) + "\n";
  *out += "M.zLeftDu=" + String(m.kinematics.zLeftDu) + "\n";
  *out += "M.zRightDu=" + String(m.kinematics.zRightDu) + "\n";
  *out += "M.xUpDu=" + String(m.kinematics.xUpDu) + "\n";
  *out += "M.xDownDu=" + String(m.kinematics.xDownDu) + "\n";
  *out += "M.zDis=" + String(m.kinematics.zDisabled ? 1 : 0) + "\n";
  *out += "M.xDis=" + String(m.kinematics.xDisabled ? 1 : 0) + "\n";
  *out += "M.yDis=" + String(m.kinematics.yDisabled ? 1 : 0) + "\n";
  *out += "M.dia=" + String(m.properties.isDiameterMode ? 1 : 0) + "\n";
  *out += "M.abs=" + String(m.properties.isAbsoluteMode ? 1 : 0) + "\n";
  *out += "M.zDro=" + String(m.properties.zDroActive ? 1 : 0) + "\n";
  *out += "M.xDro=" + String(m.properties.xDroActive ? 1 : 0) + "\n";
  *out += "M.rpm=" + String(m.kinematics.rpm) + "\n";
  *out += "M.turns=" + String(m.kinematics.turns) + "\n";
  *out += "M.angle=" + String(m.kinematics.angle) + "\n";
  *out += "M.starts=" + String(m.cycle.starts) + "\n";
  *out += "M.passes=" + String(m.cycle.turnPasses) + "\n";
  *out += "M.opIdx=" + String(m.cycle.currentPass) + "\n";
  *out += "M.setupIdx=" + String(m.cycle.setupIndex) + "\n";
}

String RichClientAdapter::buildTelemetry(uint8_t domainMask) {
  String out = "";
  out.reserve(768);

  MachineModel m = MachineModel::captureAll();

  if (domainMask & DOMAIN_KINEMATICS) {
    serializeKinematics(&out, m.kinematics);
  }
  if (domainMask & DOMAIN_INTENT) {
    serializeIntent(&out, m.intent);
  }
  if (domainMask & DOMAIN_CYCLE) {
    serializeCycle(&out, m.cycle);
  }
  if (domainMask & DOMAIN_PROPERTIES) {
    serializeProperties(&out, m.properties);
  }

  // Include legacy M.* namespace for backward compatibility whenever ALL or KINEMATICS is requested
  if ((domainMask == DOMAIN_ALL) || (domainMask & DOMAIN_KINEMATICS)) {
    serializeLegacyM(&out, m);
  }

  return out;
}

bool RichClientAdapter::hasStateChanged() {
  MachineModel m = MachineModel::captureAll();

  unsigned long hash = 0;
  hash += (unsigned long)m.kinematics.mode * 31;
  hash += (unsigned long)(m.kinematics.isOn ? 1 : 0) * 17;
  hash += (unsigned long)m.kinematics.dupr;
  hash += (unsigned long)round(m.intent.thread.authorPitch * 1000.0f);
  hash += (unsigned long)round(m.intent.taper.authorVal * 1000.0f);
  hash += (unsigned long)m.intent.feedStepDu;
  hash += (unsigned long)m.intent.jogStepDu;
  hash += (unsigned long)m.kinematics.zDu;
  hash += (unsigned long)m.kinematics.xDu * 3;
  hash += (unsigned long)m.kinematics.zLeftDu;
  hash += (unsigned long)m.kinematics.zRightDu;
  hash += (unsigned long)m.kinematics.xUpDu;
  hash += (unsigned long)m.kinematics.xDownDu;
  hash += (unsigned long)m.kinematics.rpm * 7;
  hash += (unsigned long)(m.kinematics.estop ? 1 : 0) * 13;
  hash += (unsigned long)(m.kinematics.zDisabled ? 1 : 0);
  hash += (unsigned long)(m.kinematics.xDisabled ? 1 : 0);
  hash += (unsigned long)(m.properties.isDiameterMode ? 1 : 0);
  hash += (unsigned long)(m.properties.isAbsoluteMode ? 1 : 0);

  if (hash != lastTelemetryHash) {
    lastTelemetryHash = hash;
    return true;
  }
  return false;
}
