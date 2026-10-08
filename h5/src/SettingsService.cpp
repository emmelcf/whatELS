#include "SettingsService.h"
#include "Config.h"
#include "Axis.h"
#include "Motion.h"
#include "Input.h"
#include "WebServerModule.h"
#include "Debug.h"
#include <Preferences.h>

static LatheConfiguration _activeProfile = {};
static char _activePresetSlug[24] = "wm210v_dual";
static std::optional<float> _deadbandAngle;
static std::optional<float> _deadbandTurns;

void SettingsService::init() {
  DEBUG_PRINTLN("SettingsService::init...");
  Preferences pref;
  pref.begin("machine", false);

  // 1. Determine active preset slug (default: wm210v_dual)
  String savedSlug = pref.getString("p_preset", "wm210v_dual");
  strncpy(_activePresetSlug, savedSlug.c_str(), sizeof(_activePresetSlug) - 1);
  _activePresetSlug[sizeof(_activePresetSlug) - 1] = '\0';

  // 2. Load preset baseline into RAM
  const LatheConfiguration* pPreset = findPresetBySlug(_activePresetSlug);
  if (pPreset) {
    _activeProfile = *pPreset;
  } else {
    _activeProfile = getDefaultProfile();
    strncpy(_activePresetSlug, _activeProfile.profileId, sizeof(_activePresetSlug) - 1);
    _activePresetSlug[sizeof(_activePresetSlug) - 1] = '\0';
  }

  // 3. Scan for sparse user overrides in NVS namespace "machine"
  // If a key is NOT present in NVS, the preset default in RAM remains untouched. Zero flash writes!
  if (pref.isKey("z_pitch")) {
    _activeProfile.zAxis.leadscrewPitchMm = pref.getFloat("z_pitch");
  }
  if (pref.isKey("z_steps")) {
    _activeProfile.zAxis.stepsPerMm = pref.getFloat("z_steps");
  }
  if (pref.isKey("z_backlash")) {
    _activeProfile.zAxis.backlashMm = pref.getFloat("z_backlash");
  }
  if (pref.isKey("z_maxvel")) {
    _activeProfile.zAxis.maxVelocityMmMin = pref.getFloat("z_maxvel");
  }
  if (pref.isKey("z_accel")) {
    _activeProfile.zAxis.maxAccelMmSec2 = pref.getFloat("z_accel");
  }
  if (pref.isKey("x_pitch")) {
    _activeProfile.xAxis.leadscrewPitchMm = pref.getFloat("x_pitch");
  }
  if (pref.isKey("x_steps")) {
    _activeProfile.xAxis.stepsPerMm = pref.getFloat("x_steps");
  }
  if (pref.isKey("x_backlash")) {
    _activeProfile.xAxis.backlashMm = pref.getFloat("x_backlash");
  }
  if (pref.isKey("x_maxvel")) {
    _activeProfile.xAxis.maxVelocityMmMin = pref.getFloat("x_maxvel");
  }
  if (pref.isKey("x_accel")) {
    _activeProfile.xAxis.maxAccelMmSec2 = pref.getFloat("x_accel");
  }
  if (pref.isKey("sp_ppr")) {
    _activeProfile.spindle.encoderPpr = pref.getUShort("sp_ppr");
  }
  if (pref.isKey("p_caps")) {
    _activeProfile.capabilities = pref.getUInt("p_caps");
  }
  if (pref.isKey("dim_bed")) {
    _activeProfile.dimensions.bedLengthMm = pref.getFloat("dim_bed");
  }
  if (pref.isKey("dim_swg")) {
    _activeProfile.dimensions.maxSwingDiameterMm = pref.getFloat("dim_swg");
  }
  if (pref.isKey("dim_cs")) {
    _activeProfile.dimensions.swingOverCrossSlideMm = pref.getFloat("dim_cs");
  }
  if (pref.isKey("dim_zt")) {
    _activeProfile.dimensions.maxZTravelMm = pref.getFloat("dim_zt");
  }
  if (pref.isKey("dim_xt")) {
    _activeProfile.dimensions.maxXTravelMm = pref.getFloat("dim_xt");
  }
  if (pref.isKey("dim_chk")) {
    _activeProfile.dimensions.chuckDiameterMm = pref.getFloat("dim_chk");
  }
  if (pref.isKey("dim_bore")) {
    _activeProfile.dimensions.spindleBoreDiameterMm = pref.getFloat("dim_bore");
  }
  pref.end();

  // 4. Load deadband overrides from PREF_NAMESPACE ("h5")
  pref.begin(PREF_NAMESPACE, false);
  if (pref.isKey("db_angle")) {
    _deadbandAngle = pref.getFloat("db_angle");
  } else {
    _deadbandAngle = std::nullopt;
  }
  if (pref.isKey("db_turns")) {
    _deadbandTurns = pref.getFloat("db_turns");
  } else {
    _deadbandTurns = std::nullopt;
  }
  pref.end();

  // 5. Synchronize legacy globals for Core 1 compatibility
  syncLegacyGlobals();
  DEBUG_PRINTF("Active profile initialized: %s (%s), caps=0x%08X\n", _activeProfile.profileId, _activeProfile.modelName, _activeProfile.capabilities);
}

const LatheConfiguration& SettingsService::getActiveProfile() {
  return _activeProfile;
}

bool SettingsService::hasCapability(LatheCapabilityFlags flag) {
  return _activeProfile.hasCapability(flag);
}

const char* SettingsService::getActivePresetSlug() {
  return _activePresetSlug;
}

void SettingsService::syncLegacyGlobals() {
  SCREW_Z_DU = _activeProfile.zAxis.leadscrewPitchDu();
  SCREW_X_DU = _activeProfile.xAxis.leadscrewPitchDu();
  z.screwPitch = _activeProfile.zAxis.leadscrewPitchMm;
  x.screwPitch = _activeProfile.xAxis.leadscrewPitchMm;

  if (_activeProfile.zAxis.stepsPerMm > 0 && _activeProfile.zAxis.leadscrewPitchMm > 0) {
    MOTOR_STEPS_Z = (long)round(_activeProfile.zAxis.stepsPerMm * _activeProfile.zAxis.leadscrewPitchMm);
    z.motorSteps = MOTOR_STEPS_Z;
  }
  if (_activeProfile.xAxis.stepsPerMm > 0 && _activeProfile.xAxis.leadscrewPitchMm > 0) {
    MOTOR_STEPS_X = (long)round(_activeProfile.xAxis.stepsPerMm * _activeProfile.xAxis.leadscrewPitchMm);
    x.motorSteps = MOTOR_STEPS_X;
  }

  ENCODER_PPR = _activeProfile.spindle.encoderPpr;
  ENCODER_STEPS_INT = ENCODER_PPR * 2;
  ENCODER_STEPS_FLOAT = ENCODER_PPR * 2;
  RPM_BULK = ENCODER_PPR * 2;
}

bool SettingsService::setPreset(const char* slug, String* reply) {
  if (!slug || *slug == '\0') {
    if (reply) *reply = "ERR:SETTINGS.preset=missing slug\n";
    return false;
  }
  const LatheConfiguration* pPreset = findPresetBySlug(slug);
  if (!pPreset) {
    if (reply) *reply = "ERR:SETTINGS.preset=unknown preset slug: " + String(slug) + "\n";
    return false;
  }

  Preferences pref;
  pref.begin("machine", false);
  pref.clear(); // Wipes sparse overrides in "machine", resetting to clean preset
  pref.putString("p_preset", pPreset->profileId);
  pref.end();

  strncpy(_activePresetSlug, pPreset->profileId, sizeof(_activePresetSlug) - 1);
  _activePresetSlug[sizeof(_activePresetSlug) - 1] = '\0';
  _activeProfile = *pPreset;

  syncLegacyGlobals();
  saveMachineConfig();
  machineStatusForcePublish = true;

  if (reply) *reply = "OK:SETTINGS.preset=" + String(_activeProfile.profileId) + "\n";
  return true;
}

bool SettingsService::reset(const char* key, String* reply) {
  if (!key || *key == '\0') return false;

  auto reportOk = [&](const char* name) {
    if (reply) *reply = "OK:SETTINGS.reset=" + String(name) + "\n";
    machineStatusForcePublish = true;
  };

  const LatheConfiguration* pBaseline = findPresetBySlug(_activePresetSlug);
  if (!pBaseline) {
    static LatheConfiguration def = getDefaultProfile();
    pBaseline = &def;
  }

  Preferences pref;
  if (strcasecmp(key, "machine.z.pitchDu") == 0 || strcasecmp(key, "screwZDu") == 0) {
    pref.begin("machine", false);
    pref.remove("z_pitch");
    pref.end();
    _activeProfile.zAxis.leadscrewPitchMm = pBaseline->zAxis.leadscrewPitchMm;
    syncLegacyGlobals();
    saveMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.pitchDu") == 0 || strcasecmp(key, "screwXDu") == 0) {
    pref.begin("machine", false);
    pref.remove("x_pitch");
    pref.end();
    _activeProfile.xAxis.leadscrewPitchMm = pBaseline->xAxis.leadscrewPitchMm;
    syncLegacyGlobals();
    saveMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.z.motorSteps") == 0 || strcasecmp(key, "motorStepsZ") == 0) {
    pref.begin("machine", false);
    pref.remove("z_steps");
    pref.end();
    _activeProfile.zAxis.stepsPerMm = pBaseline->zAxis.stepsPerMm;
    syncLegacyGlobals();
    saveMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.motorSteps") == 0 || strcasecmp(key, "motorStepsX") == 0) {
    pref.begin("machine", false);
    pref.remove("x_steps");
    pref.end();
    _activeProfile.xAxis.stepsPerMm = pBaseline->xAxis.stepsPerMm;
    syncLegacyGlobals();
    saveMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.spindle.ppr") == 0 || strcasecmp(key, "encoderPpr") == 0) {
    pref.begin("machine", false);
    pref.remove("sp_ppr");
    pref.end();
    _activeProfile.spindle.encoderPpr = pBaseline->spindle.encoderPpr;
    syncLegacyGlobals();
    saveMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "deadbandAngle") == 0) {
    setDeadbandAngle("auto");
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "deadbandTurns") == 0) {
    setDeadbandTurns("auto");
    reportOk(key);
    return true;
  }

  if (reply) *reply = "ERR:SETTINGS.reset=unknown key: " + String(key) + "\n";
  return false;
}

bool SettingsService::resetGroup(const char* group, String* reply) {
  if (!group || *group == '\0') return false;

  if (strcasecmp(group, "machine") == 0) {
    return setPreset(_activePresetSlug, reply);
  }

  if (reply) *reply = "ERR:SETTINGS.resetGroup=unknown group: " + String(group) + "\n";
  return false;
}

float SettingsService::getDeadbandAngle() {
  float derived = (ENCODER_STEPS_FLOAT > 0) ? (360.0f / ENCODER_STEPS_FLOAT) : 0.05f;
  return _deadbandAngle.value_or(derived);
}

bool SettingsService::isDeadbandAngleAuto() {
  return !_deadbandAngle.has_value();
}

float SettingsService::getDeadbandTurns() {
  float derived = (ENCODER_STEPS_FLOAT > 0) ? (2.0f / ENCODER_STEPS_FLOAT) : 0.002f;
  return _deadbandTurns.value_or(derived);
}

bool SettingsService::isDeadbandTurnsAuto() {
  return !_deadbandTurns.has_value();
}

bool SettingsService::setScrewZDu(long du) {
  if (du < 100 || du > 10000000) return false;
  float pitchMm = (float)du / 10000.0f;
  _activeProfile.zAxis.leadscrewPitchMm = pitchMm;
  Preferences pref;
  pref.begin("machine", false);
  pref.putFloat("z_pitch", pitchMm);
  pref.end();
  syncLegacyGlobals();
  saveMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setScrewXDu(long du) {
  if (du < 100 || du > 10000000) return false;
  float pitchMm = (float)du / 10000.0f;
  _activeProfile.xAxis.leadscrewPitchMm = pitchMm;
  Preferences pref;
  pref.begin("machine", false);
  pref.putFloat("x_pitch", pitchMm);
  pref.end();
  syncLegacyGlobals();
  saveMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMotorStepsZ(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  float pitchMm = _activeProfile.zAxis.leadscrewPitchMm;
  if (pitchMm > 0) {
    _activeProfile.zAxis.stepsPerMm = (float)steps / pitchMm;
  }
  Preferences pref;
  pref.begin("machine", false);
  pref.putFloat("z_steps", _activeProfile.zAxis.stepsPerMm);
  pref.end();
  syncLegacyGlobals();
  saveMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMotorStepsX(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  float pitchMm = _activeProfile.xAxis.leadscrewPitchMm;
  if (pitchMm > 0) {
    _activeProfile.xAxis.stepsPerMm = (float)steps / pitchMm;
  }
  Preferences pref;
  pref.begin("machine", false);
  pref.putFloat("x_steps", _activeProfile.xAxis.stepsPerMm);
  pref.end();
  syncLegacyGlobals();
  saveMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setEncoderPpr(int ppr) {
  if (ppr < 10 || ppr > 30000) return false;
  _activeProfile.spindle.encoderPpr = ppr;
  Preferences pref;
  pref.begin("machine", false);
  pref.putUShort("sp_ppr", (uint16_t)ppr);
  pref.end();
  syncLegacyGlobals();
  applyDerivedMachineConfig();
  saveMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setDiameterMode(bool dia) {
  isDiameterMode = dia;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool("dia", isDiameterMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setAbsoluteMode(bool abs) {
  isAbsoluteMode = abs;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool("abs", isAbsoluteMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setZDroMode(bool zdro) {
  zDisplayModeDRO = zdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool("zdro", zDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setXDroMode(bool xdro) {
  xDisplayModeDRO = xdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool("xdro", xDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMeasure(int m) {
  if (m < 0 || m > 2) return false;
  ::setMeasure(m);
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putInt(PREF_MEASURE, m);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setJogStepDu(long du) {
  if (du < 1 || du > 1000000) return false;
  moveStep = du;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_MOVE_STEP, du);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setDeadbandAngle(const char* val) {
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  if (!val || *val == '\0' || strcasecmp(val, "auto") == 0 || strcasecmp(val, "default") == 0) {
    _deadbandAngle = std::nullopt;
    pref.remove("db_angle");
  } else {
    float deg = atof(val);
    if (deg < 0.0f || deg > 360.0f) { pref.end(); return false; }
    _deadbandAngle = deg;
    pref.putFloat("db_angle", deg);
  }
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setDeadbandTurns(const char* val) {
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  if (!val || *val == '\0' || strcasecmp(val, "auto") == 0 || strcasecmp(val, "default") == 0) {
    _deadbandTurns = std::nullopt;
    pref.remove("db_turns");
  } else {
    float turns = atof(val);
    if (turns < 0.0f || turns > 100.0f) { pref.end(); return false; }
    _deadbandTurns = turns;
    pref.putFloat("db_turns", turns);
  }
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::get(const char* key, String* reply) {
  if (!reply) return false;

  bool getAll = (!key || *key == '\0' || strcasecmp(key, "ALL") == 0 || strcmp(key, "*") == 0);

  auto appendSetting = [&](const char* name, const String& val) {
    *reply += "SETTING." + String(name) + "=" + val + "\n";
  };

  // --- Tier 1: Machine Profile ---
  if (getAll || strcasecmp(key, "machine.preset") == 0 || strcasecmp(key, "preset") == 0) {
    appendSetting("machine.preset", String(_activePresetSlug));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.name") == 0) {
    appendSetting("machine.name", String(_activeProfile.modelName));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.caps") == 0 || strcasecmp(key, "caps") == 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)_activeProfile.capabilities);
    appendSetting("machine.caps", String(buf));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.bedLength") == 0) {
    appendSetting("machine.dim.bedLength", String(_activeProfile.dimensions.bedLengthMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.swingDia") == 0) {
    appendSetting("machine.dim.swingDia", String(_activeProfile.dimensions.maxSwingDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.chuckDia") == 0) {
    appendSetting("machine.dim.chuckDia", String(_activeProfile.dimensions.chuckDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.spindleBore") == 0) {
    appendSetting("machine.dim.spindleBore", String(_activeProfile.dimensions.spindleBoreDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.z.pitchDu") == 0 || strcasecmp(key, "screwZDu") == 0) {
    appendSetting("machine.z.pitchDu", String(SCREW_Z_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.x.pitchDu") == 0 || strcasecmp(key, "screwXDu") == 0) {
    appendSetting("machine.x.pitchDu", String(SCREW_X_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.z.motorSteps") == 0 || strcasecmp(key, "motorStepsZ") == 0) {
    appendSetting("machine.z.motorSteps", String(MOTOR_STEPS_Z));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.x.motorSteps") == 0 || strcasecmp(key, "motorStepsX") == 0) {
    appendSetting("machine.x.motorSteps", String(MOTOR_STEPS_X));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.spindle.ppr") == 0 || strcasecmp(key, "encoderPpr") == 0) {
    appendSetting("machine.spindle.ppr", String(ENCODER_PPR));
    if (!getAll) return true;
  }

  // --- Tier 2: Controller Infrastructure ---
  if (getAll || strcasecmp(key, "screwYDu") == 0) {
    appendSetting("screwYDu", String(SCREW_Y_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "motorStepsY") == 0) {
    appendSetting("motorStepsY", String(MOTOR_STEPS_Y));
    if (!getAll) return true;
  }

  // --- Tier 3: Operator Preferences ---
  if (getAll || strcasecmp(key, "measure") == 0) {
    appendSetting("measure", String(measure));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "dia") == 0 || strcasecmp(key, "isDiameterMode") == 0) {
    appendSetting("dia", isDiameterMode ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "abs") == 0 || strcasecmp(key, "isAbsoluteMode") == 0) {
    appendSetting("abs", isAbsoluteMode ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "zDro") == 0 || strcasecmp(key, "zDroActive") == 0) {
    appendSetting("zDro", zDisplayModeDRO ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "xDro") == 0 || strcasecmp(key, "xDroActive") == 0) {
    appendSetting("xDro", xDisplayModeDRO ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "feedStepDu") == 0) {
    appendSetting("feedStepDu", String(feedStepDu));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "jogStepDu") == 0) {
    appendSetting("jogStepDu", String(moveStep));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "backlashZDu") == 0) {
    appendSetting("backlashZDu", String(BACKLASH_DU_Z));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "backlashXDu") == 0) {
    appendSetting("backlashXDu", String(BACKLASH_DU_X));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "deadbandAngle") == 0) {
    appendSetting("deadbandAngle", String(getDeadbandAngle(), 3));
    appendSetting("deadbandAngle.isAuto", isDeadbandAngleAuto() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "deadbandTurns") == 0) {
    appendSetting("deadbandTurns", String(getDeadbandTurns(), 4));
    appendSetting("deadbandTurns.isAuto", isDeadbandTurnsAuto() ? "1" : "0");
    if (!getAll) return true;
  }

  if (getAll) {
    return true;
  }

  *reply = "ERR:SETTINGS." + String(key) + "=unknown key\n";
  return true;
}

bool SettingsService::set(const char* keyVal, String* reply) {
  if (!keyVal || *keyVal == '\0') return false;

  const char* sep = strpbrk(keyVal, ",=");
  if (!sep) {
    if (reply) *reply = "ERR:SETTINGS=expected key,value\n";
    return true;
  }

  char keyBuf[48];
  size_t keyLen = sep - keyVal;
  if (keyLen >= sizeof(keyBuf)) keyLen = sizeof(keyBuf) - 1;
  strncpy(keyBuf, keyVal, keyLen);
  keyBuf[keyLen] = '\0';

  const char* valStr = sep + 1;
  while (*valStr == ' ' || *valStr == '\t') valStr++;

  auto reportOk = [&](const char* name, const String& val) {
    if (reply) *reply = "OK:SETTINGS." + String(name) + "=" + val + "\n";
    machineStatusForcePublish = true;
  };

  auto reportErr = [&](const char* name, const char* reason) {
    if (reply) *reply = "ERR:SETTINGS." + String(name) + "=" + String(reason) + "\n";
  };

  // --- Preset Switch ---
  if (strcasecmp(keyBuf, "machine.preset") == 0 || strcasecmp(keyBuf, "preset") == 0) {
    return setPreset(valStr, reply);
  }

  // --- Tier 1: Machine Profile ---
  if (strcasecmp(keyBuf, "machine.z.pitchDu") == 0 || strcasecmp(keyBuf, "screwZDu") == 0) {
    long val = atol(valStr);
    if (!setScrewZDu(val)) { reportErr(keyBuf, "out of bounds (100..10000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "machine.x.pitchDu") == 0 || strcasecmp(keyBuf, "screwXDu") == 0) {
    long val = atol(valStr);
    if (!setScrewXDu(val)) { reportErr(keyBuf, "out of bounds (100..10000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "machine.z.motorSteps") == 0 || strcasecmp(keyBuf, "motorStepsZ") == 0) {
    long val = atol(valStr);
    if (!setMotorStepsZ(val)) { reportErr(keyBuf, "out of bounds (10..1000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "machine.x.motorSteps") == 0 || strcasecmp(keyBuf, "motorStepsX") == 0) {
    long val = atol(valStr);
    if (!setMotorStepsX(val)) { reportErr(keyBuf, "out of bounds (10..1000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "machine.spindle.ppr") == 0 || strcasecmp(keyBuf, "encoderPpr") == 0) {
    int val = atoi(valStr);
    if (!setEncoderPpr(val)) { reportErr(keyBuf, "out of bounds (10..30000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  // --- Legacy Y axis ---
  if (strcasecmp(keyBuf, "screwYDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 10000000) { reportErr(keyBuf, "out of bounds (1..10000000)"); return true; }
    SCREW_Y_DU = val;
    y.screwPitch = val / 10000.0f;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "motorStepsY") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 1000000) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    MOTOR_STEPS_Y = val;
    y.motorSteps = val;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  // --- Tier 3: Operator Preferences ---
  if (strcasecmp(keyBuf, "measure") == 0) {
    int val = atoi(valStr);
    if (!setMeasure(val)) { reportErr(keyBuf, "must be 0 (MM), 1 (TPI), or 2 (IN)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "dia") == 0 || strcasecmp(keyBuf, "isDiameterMode") == 0) {
    int val = atoi(valStr);
    setDiameterMode(val != 0);
    reportOk(keyBuf, isDiameterMode ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "abs") == 0 || strcasecmp(keyBuf, "isAbsoluteMode") == 0) {
    int val = atoi(valStr);
    setAbsoluteMode(val != 0);
    reportOk(keyBuf, isAbsoluteMode ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "zDro") == 0 || strcasecmp(keyBuf, "zDroActive") == 0) {
    int val = atoi(valStr);
    setZDroMode(val != 0);
    reportOk(keyBuf, zDisplayModeDRO ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "xDro") == 0 || strcasecmp(keyBuf, "xDroActive") == 0) {
    int val = atoi(valStr);
    setXDroMode(val != 0);
    reportOk(keyBuf, xDisplayModeDRO ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "jogStepDu") == 0) {
    long val = atol(valStr);
    if (!setJogStepDu(val)) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "deadbandAngle") == 0) {
    if (!setDeadbandAngle(valStr)) { reportErr(keyBuf, "invalid angle (0..360 or auto)"); return true; }
    reportOk(keyBuf, isDeadbandAngleAuto() ? "auto" : String(getDeadbandAngle(), 3));
    return true;
  }

  if (strcasecmp(keyBuf, "deadbandTurns") == 0) {
    if (!setDeadbandTurns(valStr)) { reportErr(keyBuf, "invalid turns (0..100 or auto)"); return true; }
    reportOk(keyBuf, isDeadbandTurnsAuto() ? "auto" : String(getDeadbandTurns(), 4));
    return true;
  }

  reportErr(keyBuf, "unknown key");
  return true;
}
