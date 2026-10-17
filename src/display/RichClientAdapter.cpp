#include "RichClientAdapter.h"
#include "MachineModel.h"
#include "modes/ModeRegistry.h"
#include "modes/composite/UnifiedGearboxMode.h"
#include "motion/MotionConversions.h"

static unsigned long lastTelemetryHash = 0;

static void serializeKinematics(String* out, const KinematicsState& k) {
  *out += "K.zDu=" + String(k.z.posDu) + "\n";
  *out += "K.xDu=" + String(k.x.posDu) + "\n";
  *out += "K.zLeftDu=" + (k.z.limits.leftOrUpDu.has_value() ? String(*k.z.limits.leftOrUpDu) : "undefined") + "\n";
  *out += "K.zRightDu=" + (k.z.limits.rightOrDownDu.has_value() ? String(*k.z.limits.rightOrDownDu) : "undefined") + "\n";
  *out += "K.xUpDu=" + (k.x.limits.leftOrUpDu.has_value() ? String(*k.x.limits.leftOrUpDu) : "undefined") + "\n";
  *out += "K.xDownDu=" + (k.x.limits.rightOrDownDu.has_value() ? String(*k.x.limits.rightOrDownDu) : "undefined") + "\n";
  *out += "K.zDis=" + String(k.z.disabled ? 1 : 0) + "\n";
  *out += "K.xDis=" + String(k.x.disabled ? 1 : 0) + "\n";
  *out += "K.rpm=" + String(k.spindle.rpm) + "\n";
  *out += "K.turns=" + String(k.spindle.turns, 2) + "\n";
  *out += "K.angle=" + String(k.spindle.angle, 2) + "\n";
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
