#include "RichClientAdapter.h"
#include "MachineModel.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include "motion/MotionConversions.h"

static unsigned long lastTelemetryHash = 0;

static void serializeKinematics(String* out, const KinematicsState& k) {
  *out += "K.zDu=" + String(k.zDu) + "\n";
  *out += "K.xDu=" + String(k.xDu) + "\n";
  *out += "K.zLeftDu=" + (k.zLeftDefined && k.zLeftEnabled ? String(k.zLeftDu) : "undefined") + "\n";
  *out += "K.zRightDu=" + (k.zRightDefined && k.zRightEnabled ? String(k.zRightDu) : "undefined") + "\n";
  *out += "K.xUpDu=" + (k.xUpDefined && k.xUpEnabled ? String(k.xUpDu) : "undefined") + "\n";
  *out += "K.xDownDu=" + (k.xDownDefined && k.xDownEnabled ? String(k.xDownDu) : "undefined") + "\n";
  *out += "K.zDis=" + String(k.zDisabled ? 1 : 0) + "\n";
  *out += "K.xDis=" + String(k.xDisabled ? 1 : 0) + "\n";
  *out += "K.rpm=" + String(k.rpm) + "\n";
  *out += "K.turns=" + String(k.turns, 2) + "\n";
  *out += "K.angle=" + String(k.angle, 2) + "\n";
  IELSMode* activeMode = ModeRegistry::getActiveMode();
  int modeId = activeMode ? activeMode->getId() : k.mode;
  *out += "K.mode=" + String(modeId) + "\n";
  *out += "K.on=" + String(k.isOn ? 1 : 0) + "\n";
  *out += "K.estop=" + String(k.estop ? 1 : 0) + "\n";
  *out += "K.dupr=" + String(k.dupr) + "\n";
  *out += "K.nextDupr=" + String(k.nextDupr) + "\n";

  uint32_t perms = activeMode ? activeMode->getLivePermissions() : 0;
  char permBuf[16];
  snprintf(permBuf, sizeof(permBuf), "0x%08X", (unsigned int)perms);
  *out += "K.perms=" + String(permBuf) + "\n";

  UnifiedGearboxMode* gb = ModeRegistry::getGearboxMode();
  if (gb) {
    *out += "K.gearAxis=" + String(gb->getActiveAxis()) + "\n";
    *out += "K.gearSync=" + String(gb->isSync() ? 1 : 0) + "\n";
    *out += "K.gearDir=" + String(MotionConversions::directionToSemantic(gb->getAxis(), gb->getDirection())) + "\n";
  }
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
  char hexBuf[16];
  snprintf(hexBuf, sizeof(hexBuf), "0x%08X", (unsigned int)p.capabilities);
  *out += "P.caps=" + String(hexBuf) + "\n";
  *out += "P.preset=" + String(p.presetId) + "\n";
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
  *out += "P.dbAngle=" + String(p.deadbandAngle, 3) + "\n";
  *out += "P.dbAngleAuto=" + String(p.deadbandAngleAuto ? 1 : 0) + "\n";
  *out += "P.dbTurns=" + String(p.deadbandTurns, 4) + "\n";
  *out += "P.dbTurnsAuto=" + String(p.deadbandTurnsAuto ? 1 : 0) + "\n";
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
  *out += "M.zLeftDu=" + (m.kinematics.zLeftDefined && m.kinematics.zLeftEnabled ? String(m.kinematics.zLeftDu) : "undefined") + "\n";
  *out += "M.zRightDu=" + (m.kinematics.zRightDefined && m.kinematics.zRightEnabled ? String(m.kinematics.zRightDu) : "undefined") + "\n";
  *out += "M.xUpDu=" + (m.kinematics.xUpDefined && m.kinematics.xUpEnabled ? String(m.kinematics.xUpDu) : "undefined") + "\n";
  *out += "M.xDownDu=" + (m.kinematics.xDownDefined && m.kinematics.xDownEnabled ? String(m.kinematics.xDownDu) : "undefined") + "\n";
  *out += "M.zDis=" + String(m.kinematics.zDisabled ? 1 : 0) + "\n";
  *out += "M.xDis=" + String(m.kinematics.xDisabled ? 1 : 0) + "\n";
  *out += "M.dia=" + String(m.properties.isDiameterMode ? 1 : 0) + "\n";
  *out += "M.abs=" + String(m.properties.isAbsoluteMode ? 1 : 0) + "\n";
  *out += "M.zDro=" + String(m.properties.zDroActive ? 1 : 0) + "\n";
  *out += "M.xDro=" + String(m.properties.xDroActive ? 1 : 0) + "\n";
  *out += "M.rpm=" + String(m.kinematics.rpm) + "\n";
  *out += "M.turns=" + String(m.kinematics.turns, 2) + "\n";
  *out += "M.angle=" + String(m.kinematics.angle, 2) + "\n";
  *out += "M.starts=" + String(m.cycle.starts) + "\n";
  *out += "M.passes=" + String(m.cycle.turnPasses) + "\n";
  *out += "M.opIdx=" + String(m.cycle.currentPass) + "\n";
  *out += "M.setupIdx=" + String(m.cycle.setupIndex) + "\n";
}

String RichClientAdapter::buildTelemetry(const MachineModel& m, uint8_t domainMask) {
  String out = "";
  out.reserve(768);

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

  return out;
}

static MachineModel lastCapturedModel = {};
static bool hasCapturedInitial = false;

bool RichClientAdapter::hasStateChanged() {
  MachineModel m = MachineModel::captureAll();
  if (!hasCapturedInitial) {
    hasCapturedInitial = true;
    lastCapturedModel = m;
    return true;
  }
  uint8_t changed = MachineModel::detectChangedDomains(lastCapturedModel, m);
  if (changed != DOMAIN_NONE) {
    lastCapturedModel = m;
    return true;
  }
  return false;
}
