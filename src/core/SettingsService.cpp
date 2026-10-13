#include "SettingsService.h"
#include "Config.h"
#include "Axis.h"
#include "Motion.h"
#include "Input.h"
#include "Display.h"
#include "WebServerModule.h"
#include "Debug.h"
#include <Preferences.h>

static std::optional<float> _deadbandAngle;
static std::optional<float> _deadbandTurns;

void SettingsService::init() {
  DEBUG_PRINTLN("SettingsService::init...");
  // Load active machine profile from NVS namespace "machine"
  loadMachineProfile();

  // Load deadband overrides from PREF_NAMESPACE ("h5")
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  if (pref.isKey("db_angle")) {
    _deadbandAngle = pref.getFloat("db_angle");
  } else {
    _deadbandAngle = std::nullopt;
  }
  if (pref.isKey("sdm")) {
    spindleDisplayMode = pref.getInt("sdm", 0);
  }
  pref.end();

  syncLegacyGlobals();

  // Synchronize Intent settings from active/restored globals
  long absDu = abs(dupr);
  if (measure == MEASURE_METRIC) {
    currentThreadSetting.format = THREAD_METRIC;
    currentThreadSetting.authorPitch = absDu / 10000.0f;
  } else if (measure == MEASURE_TPI) {
    currentThreadSetting.format = THREAD_TPI;
    currentThreadSetting.authorPitch = (absDu > 0) ? (float)round(254000.0 / (double)absDu) : 0.0f;
  } else {
    currentThreadSetting.format = THREAD_INCH;
    currentThreadSetting.authorPitch = absDu / 254000.0f;
  }
  currentThreadSetting.dupr = dupr;

  if (coneRatio > 0.0f) {
    currentTaperSetting.modeType = TAPER_RATIO;
    currentTaperSetting.authorVal = 1.0f / coneRatio;
    currentTaperSetting.coneRatio = coneRatio;
    currentTaperSetting.presetId = 0;
  }
}

const LatheConfiguration& SettingsService::getActiveProfile() {
  return activeMachineProfile;
}

bool SettingsService::hasCapability(LatheCapabilityFlags flag) {
  return activeMachineProfile.hasCapability(flag);
}

const char* SettingsService::getActivePresetSlug() {
  return activeMachineProfile.profileId;
}

void SettingsService::syncLegacyGlobals() {
  SCREW_Z_DU = activeMachineProfile.zAxis.leadscrewPitchDu();
  SCREW_X_DU = activeMachineProfile.xAxis.leadscrewPitchDu();
  z.screwPitch = activeMachineProfile.zAxis.leadscrewPitchDu();
  x.screwPitch = activeMachineProfile.xAxis.leadscrewPitchDu();

  if (activeMachineProfile.zAxis.stepsPerMm > 0 && activeMachineProfile.zAxis.leadscrewPitchMm > 0) {
    MOTOR_STEPS_Z = (long)round(activeMachineProfile.zAxis.stepsPerMm * activeMachineProfile.zAxis.leadscrewPitchMm);
    z.motorSteps = MOTOR_STEPS_Z;
  }
  if (activeMachineProfile.xAxis.stepsPerMm > 0 && activeMachineProfile.xAxis.leadscrewPitchMm > 0) {
    MOTOR_STEPS_X = (long)round(activeMachineProfile.xAxis.stepsPerMm * activeMachineProfile.xAxis.leadscrewPitchMm);
    x.motorSteps = MOTOR_STEPS_X;
  }

  ENCODER_PPR = (int)round(activeMachineProfile.spindle.effectivePpr());
  ENCODER_STEPS_INT = ENCODER_PPR * 2;
  ENCODER_STEPS_FLOAT = (float)(ENCODER_PPR * 2);
  RPM_BULK = ENCODER_PPR * 2;
}

static const LatheConfiguration* getActiveBaseline() {
  const LatheConfiguration* pBaseline = findPresetBySlug(activeMachineProfile.profileId);
  if (!pBaseline) {
    static LatheConfiguration def = getDefaultProfile();
    pBaseline = &def;
  }
  return pBaseline;
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

  // Clear prior machine overrides from NVS to prevent calibration leakage across machines
  Preferences pref;
  pref.begin("machine", false);
  pref.clear();
  pref.putString("preset", pPreset->profileId);
  pref.putString("prof_id", pPreset->profileId);
  pref.putString("model_name", pPreset->modelName);
  pref.end();

  activeMachineProfile = *pPreset;
  syncLegacyGlobals();
  machineStatusForcePublish = true;

  if (reply) *reply = "OK:SETTINGS.preset=" + String(activeMachineProfile.profileId) + "\n";
  return true;
}

bool SettingsService::listPresets(String* reply) {
  if (!reply) return false;
  String list;
  listCatalogProfiles(&list);
  char defaultSlug[32] = "wm210v_stock";
  getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug));
  *reply = "PRESET.list=" + list + ";active=" + String(activeMachineProfile.profileId) + ";default=" + String(defaultSlug) + "\n";
  return true;
}

bool SettingsService::getPresetJson(const char* slug, String* reply) {
  if (!reply) return false;
  if (!slug || *slug == '\0') {
    *reply = "ERR:SETTINGS.preset.get=missing slug\n";
    return false;
  }
  String json;
  if (getProfileJsonFromLittleFS(slug, &json)) {
    *reply = json + "\n";
    return true;
  }
  *reply = "ERR:SETTINGS.preset.get=unknown preset slug: " + String(slug) + "\n";
  return false;
}

bool SettingsService::applyPreset(const char* slug, String* reply) {
  bool ok = setPreset(slug, reply);
  if (ok && reply) {
    *reply = "OK:SETTINGS.preset.apply=" + String(slug) + "\n";
  }
  return ok;
}

bool SettingsService::saveCurrentAsPreset(const char* slug, String* reply) {
  // If slug is omitted or "current", save in-place to the active profile slug
  const char* targetSlug = (slug && *slug != '\0' && strcasecmp(slug, "current") != 0) ? slug : activeMachineProfile.profileId;

  // Guard: Never allow overwriting a system template!
  if (isSystemCatalogProfile(targetSlug)) {
    if (reply) {
      *reply = "ERR:SETTINGS.preset.save=cannot overwrite system template: " + String(targetSlug) + " (use save=<new_name>)\n";
    }
    return false;
  }

  strncpy(activeMachineProfile.profileId, targetSlug, sizeof(activeMachineProfile.profileId) - 1);
  activeMachineProfile.profileId[sizeof(activeMachineProfile.profileId) - 1] = '\0';
  activeMachineProfile.isSystemProfile = false;

  if (saveProfileToLittleFS(activeMachineProfile)) {
    // Persist new preset identity to NVS
    Preferences pref;
    pref.begin("machine", false);
    pref.putString("preset", activeMachineProfile.profileId);
    pref.putString("prof_id", activeMachineProfile.profileId);
    pref.end();

    if (reply) *reply = "OK:SETTINGS.preset.save=" + String(targetSlug) + "\n";
    machineStatusForcePublish = true;
    return true;
  }
  if (reply) *reply = "ERR:SETTINGS.preset.save=failed to save preset to LittleFS\n";
  return false;
}

bool SettingsService::deletePreset(const char* slug, String* reply) {
  if (!slug || *slug == '\0') {
    if (reply) *reply = "ERR:SETTINGS.preset.delete=missing slug\n";
    return false;
  }
  if (isSystemCatalogProfile(slug)) {
    if (reply) *reply = "ERR:SETTINGS.preset.delete=cannot delete system template: " + String(slug) + "\n";
    return false;
  }
  if (strcasecmp(activeMachineProfile.profileId, slug) == 0) {
    if (reply) *reply = "ERR:SETTINGS.preset.delete=cannot delete active preset: " + String(slug) + "\n";
    return false;
  }
  char defaultSlug[32] = {0};
  if (getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug))) {
    if (strcasecmp(defaultSlug, slug) == 0) {
      if (reply) *reply = "ERR:SETTINGS.preset.delete=cannot delete default preset: " + String(slug) + "\n";
      return false;
    }
  }
  if (deleteProfileFromLittleFS(slug)) {
    if (reply) *reply = "OK:SETTINGS.preset.delete=" + String(slug) + "\n";
    return true;
  }
  if (reply) *reply = "ERR:SETTINGS.preset.delete=cannot delete preset: " + String(slug) + "\n";
  return false;
}

bool SettingsService::setDefaultPreset(const char* slug, String* reply) {
  if (!slug || *slug == '\0') {
    if (reply) *reply = "ERR:SETTINGS.preset.setDefault=missing slug\n";
    return false;
  }
  if (setDefaultCatalogProfileId(slug)) {
    if (reply) *reply = "OK:SETTINGS.preset.setDefault=" + String(slug) + "\n";
    return true;
  }
  if (reply) *reply = "ERR:SETTINGS.preset.setDefault=failed to set default preset: " + String(slug) + "\n";
  return false;
}

bool SettingsService::resetToDefaultPreset(String* reply) {
  char defaultSlug[32] = "wm210v_stock";
  getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug));
  bool ok = applyPreset(defaultSlug, reply);
  if (ok && reply) {
    *reply = "OK:SETTINGS.preset.resetToDefault=" + String(defaultSlug) + "\n";
  }
  return ok;
}

bool SettingsService::reset(const char* key, String* reply) {
  if (!key || *key == '\0') return false;

  auto reportOk = [&](const char* name) {
    if (reply) *reply = "OK:SETTINGS.reset=" + String(name) + "\n";
    machineStatusForcePublish = true;
  };

  const LatheConfiguration* pBaseline = getActiveBaseline();

  Preferences pref;
  pref.begin("machine", false);

  if (strcasecmp(key, "machine.z.pitchDu") == 0 || strcasecmp(key, "screwZDu") == 0) {
    activeMachineProfile.zAxis.leadscrewPitchMm = pBaseline->zAxis.leadscrewPitchMm;
    pref.remove("screw_z_du");
    pref.end();
    syncLegacyGlobals();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.pitchDu") == 0 || strcasecmp(key, "screwXDu") == 0) {
    activeMachineProfile.xAxis.leadscrewPitchMm = pBaseline->xAxis.leadscrewPitchMm;
    pref.remove("screw_x_du");
    pref.end();
    syncLegacyGlobals();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.z.motorSteps") == 0 || strcasecmp(key, "motorStepsZ") == 0) {
    activeMachineProfile.zAxis.stepsPerMm = pBaseline->zAxis.stepsPerMm;
    pref.remove("mot_steps_z");
    pref.end();
    syncLegacyGlobals();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.motorSteps") == 0 || strcasecmp(key, "motorStepsX") == 0) {
    activeMachineProfile.xAxis.stepsPerMm = pBaseline->xAxis.stepsPerMm;
    pref.remove("mot_steps_x");
    pref.end();
    syncLegacyGlobals();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.spindle.ppr") == 0 || strcasecmp(key, "encoderPpr") == 0) {
    activeMachineProfile.spindle.encoder.ppr = pBaseline->spindle.encoder.ppr;
    pref.remove("enc_ppr");
    pref.end();
    syncLegacyGlobals();
    applyDerivedMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.spindle.ratio") == 0) {
    activeMachineProfile.spindle.pulleyRatio = pBaseline->spindle.pulleyRatio;
    pref.remove("spindle_ratio");
    pref.end();
    syncLegacyGlobals();
    applyDerivedMachineConfig();
    reportOk(key);
    return true;
  }
  pref.end();
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
    return setPreset(activeMachineProfile.profileId, reply);
  }

  if (strcasecmp(group, "prefs") == 0) {
    setDeadbandAngle("auto");
    setDeadbandTurns("auto");
    setJogStepDu(1000);
    setMeasure(0);
    setDiameterMode(true);
    setAbsoluteMode(true);
    setZDroMode(false);
    setXDroMode(false);
    feedStepDu = 100;
    if (reply) *reply = "OK:SETTINGS.resetGroup=prefs\n";
    machineStatusForcePublish = true;
    return true;
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
  activeMachineProfile.zAxis.leadscrewPitchMm = pitchMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  if (pBaseline && pBaseline->zAxis.leadscrewPitchDu() == du) {
    pref.remove("screw_z_du");
  } else {
    pref.putInt("screw_z_du", (int32_t)du);
  }
  pref.end();

  syncLegacyGlobals();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setScrewXDu(long du) {
  if (du < 100 || du > 10000000) return false;
  float pitchMm = (float)du / 10000.0f;
  activeMachineProfile.xAxis.leadscrewPitchMm = pitchMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  if (pBaseline && pBaseline->xAxis.leadscrewPitchDu() == du) {
    pref.remove("screw_x_du");
  } else {
    pref.putInt("screw_x_du", (int32_t)du);
  }
  pref.end();

  syncLegacyGlobals();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMotorStepsZ(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  float pitchMm = activeMachineProfile.zAxis.leadscrewPitchMm;
  if (pitchMm <= 0.0f) return false;
  float stepsPerMm = (float)steps / pitchMm;
  activeMachineProfile.zAxis.stepsPerMm = stepsPerMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  float baseSteps = (pBaseline && pBaseline->zAxis.leadscrewPitchMm > 0) ? (pBaseline->zAxis.stepsPerMm * pBaseline->zAxis.leadscrewPitchMm) : -1.0f;
  if (fabsf(baseSteps - (float)steps) < 0.5f) {
    pref.remove("mot_steps_z");
  } else {
    pref.putFloat("mot_steps_z", stepsPerMm);
  }
  pref.end();

  syncLegacyGlobals();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMotorStepsX(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  float pitchMm = activeMachineProfile.xAxis.leadscrewPitchMm;
  if (pitchMm <= 0.0f) return false;
  float stepsPerMm = (float)steps / pitchMm;
  activeMachineProfile.xAxis.stepsPerMm = stepsPerMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  float baseSteps = (pBaseline && pBaseline->xAxis.leadscrewPitchMm > 0) ? (pBaseline->xAxis.stepsPerMm * pBaseline->xAxis.leadscrewPitchMm) : -1.0f;
  if (fabsf(baseSteps - (float)steps) < 0.5f) {
    pref.remove("mot_steps_x");
  } else {
    pref.putFloat("mot_steps_x", stepsPerMm);
  }
  pref.end();

  syncLegacyGlobals();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setEncoderPpr(int ppr) {
  if (ppr < 10 || ppr > 30000) return false;
  activeMachineProfile.spindle.encoder.ppr = (uint16_t)ppr;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  if (pBaseline && pBaseline->spindle.encoder.ppr == (uint16_t)ppr) {
    pref.remove("enc_ppr");
  } else {
    pref.putUShort("enc_ppr", (uint16_t)ppr);
  }
  pref.end();

  syncLegacyGlobals();
  applyDerivedMachineConfig();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setSpindleRatio(float ratio) {
  if (ratio < 0.01f || ratio > 50.0f) return false;
  activeMachineProfile.spindle.pulleyRatio = ratio;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  if (pBaseline && fabsf(pBaseline->spindle.pulleyRatio - ratio) < 0.0001f) {
    pref.remove("spindle_ratio");
  } else {
    pref.putFloat("spindle_ratio", ratio);
  }
  pref.end();

  syncLegacyGlobals();
  applyDerivedMachineConfig();
  machineStatusForcePublish = true;
  return true;
}


static Axis* getAxisById(AxisId axis) {
  switch (axis) {
    case AXIS_Z: return &z;
    case AXIS_X: return &x;
    case AXIS_Y: return &y;
    default:     return nullptr;
  }
}

// --- Tier 2 / Category 1: Travel Stops & Boundaries ---
bool SettingsService::setAxisLimitDu(AxisId axis, LimitDirection dir, long limitDu) {
  if (limitDu == LONG_MAX || limitDu == LONG_MIN) {
    return clearAxisLimit(axis, dir);
  }
  Axis* a = getAxisById(axis);
  if (!a) return false;

  long stopSteps = duToSteps(a, limitDu) - a->originPos;
  const char* prefKey = nullptr;
  if (axis == AXIS_Z) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_Z : PREF_RIGHT_STOP_Z;
  } else if (axis == AXIS_X) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_X : PREF_RIGHT_STOP_X;
  } else if (axis == AXIS_Y) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_Y : PREF_RIGHT_STOP_Y;
  }

  if (dir == LIMIT_LEFT_OR_UP) {
    a->nextLeftStop = stopSteps;
    a->nextLeftStopFlag = true;
  } else {
    a->nextRightStop = stopSteps;
    a->nextRightStopFlag = true;
  }

  if (prefKey) {
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.putLong(prefKey, stopSteps);
    pref.end();
  }

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::clearAxisLimit(AxisId axis, LimitDirection dir) {
  Axis* a = getAxisById(axis);
  if (!a) return false;

  long stopSteps = (dir == LIMIT_LEFT_OR_UP) ? LONG_MAX : LONG_MIN;
  const char* prefKey = nullptr;
  if (axis == AXIS_Z) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_Z : PREF_RIGHT_STOP_Z;
  } else if (axis == AXIS_X) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_X : PREF_RIGHT_STOP_X;
  } else if (axis == AXIS_Y) {
    prefKey = (dir == LIMIT_LEFT_OR_UP) ? PREF_LEFT_STOP_Y : PREF_RIGHT_STOP_Y;
  }

  if (dir == LIMIT_LEFT_OR_UP) {
    a->nextLeftStop = LONG_MAX;
    a->nextLeftStopFlag = true;
  } else {
    a->nextRightStop = LONG_MIN;
    a->nextRightStopFlag = true;
  }

  if (prefKey) {
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.putLong(prefKey, stopSteps);
    pref.end();
  }

  machineStatusForcePublish = true;
  return true;
}

long SettingsService::getAxisLimitDu(AxisId axis, LimitDirection dir) {
  Axis* a = getAxisById(axis);
  if (!a) return (dir == LIMIT_LEFT_OR_UP) ? LONG_MAX : LONG_MIN;

  if (dir == LIMIT_LEFT_OR_UP) {
    long stop = a->nextLeftStopFlag ? a->nextLeftStop : a->leftStop;
    if (stop == LONG_MAX) return LONG_MAX;
    return stepsToDu(a, stop + a->originPos);
  } else {
    long stop = a->nextRightStopFlag ? a->nextRightStop : a->rightStop;
    if (stop == LONG_MIN) return LONG_MIN;
    return stepsToDu(a, stop + a->originPos);
  }
}

// --- Tier 2 / Category 2: Pitch, Threading & Feed Intent ---
bool SettingsService::setFeedrateDupr(long duprVal, bool preserveDirection) {
  if (preserveDirection && ((nextDuprFlag ? nextDupr : dupr) < 0)) {
    duprVal = -abs(duprVal);
  }
  long absDu = abs(duprVal);
  if (measure == MEASURE_METRIC) {
    currentThreadSetting.format = THREAD_METRIC;
    currentThreadSetting.authorPitch = absDu / 10000.0f;
  } else if (measure == MEASURE_TPI) {
    currentThreadSetting.format = THREAD_TPI;
    currentThreadSetting.authorPitch = (absDu > 0) ? (float)round(254000.0 / (double)absDu) : 0.0f;
  } else {
    currentThreadSetting.format = THREAD_INCH;
    currentThreadSetting.authorPitch = absDu / 254000.0f;
  }
  currentThreadSetting.dupr = duprVal;
  nextDupr = duprVal;
  nextDuprFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_DUPR, duprVal);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setFeedrateMm(float pitchMm, bool preserveDirection) {
  if (pitchMm <= 0.0f) return false;
  long duVal = (long)round(pitchMm * 10000.0f);
  if (preserveDirection && ((nextDuprFlag ? nextDupr : dupr) < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_METRIC;
  currentThreadSetting.authorPitch = pitchMm;
  currentThreadSetting.dupr = duVal;
  nextDupr = duVal;
  nextDuprFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_DUPR, duVal);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setFeedrateTpi(float tpi, bool preserveDirection) {
  if (tpi <= 0.0f) return false;
  long duVal = (long)round(254000.0 / (double)tpi);
  if (preserveDirection && ((nextDuprFlag ? nextDupr : dupr) < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_TPI;
  currentThreadSetting.authorPitch = tpi;
  currentThreadSetting.dupr = duVal;
  nextDupr = duVal;
  nextDuprFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_DUPR, duVal);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setFeedrateInch(float pitchInch, bool preserveDirection) {
  if (pitchInch <= 0.0f) return false;
  long duVal = (long)round(pitchInch * 254000.0f);
  if (preserveDirection && ((nextDuprFlag ? nextDupr : dupr) < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_INCH;
  currentThreadSetting.authorPitch = pitchInch;
  currentThreadSetting.dupr = duVal;
  nextDupr = duVal;
  nextDuprFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_DUPR, duVal);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::reverseFeedDirection() {
  long current = nextDuprFlag ? nextDupr : dupr;
  long reversed = -current;
  currentThreadSetting.dupr = reversed;
  nextDupr = reversed;
  nextDuprFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putLong(PREF_DUPR, reversed);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

ThreadSetting SettingsService::getThreadSetting() {
  return currentThreadSetting;
}

long SettingsService::getJogStepDu() {
  return moveStep;
}

bool SettingsService::cycleJogStep() {
  long nextStep = MOVE_STEP_1;
  if (measure == MEASURE_METRIC) {
    if (moveStep == MOVE_STEP_1) {
      nextStep = MOVE_STEP_2;
    } else if (moveStep == MOVE_STEP_2) {
      nextStep = MOVE_STEP_3;
    } else {
      nextStep = MOVE_STEP_1;
    }
  } else {
    if (moveStep == MOVE_STEP_IMP_1) {
      nextStep = MOVE_STEP_IMP_2;
    } else if (moveStep == MOVE_STEP_IMP_2) {
      nextStep = MOVE_STEP_IMP_3;
    } else {
      nextStep = MOVE_STEP_IMP_1;
    }
  }
  return setJogStepDu(nextStep);
}

// --- Tier 2 / Category 3: Cycle & Geometry Parameters ---
bool SettingsService::setStarts(int startsVal) {
  if (startsVal < 1 || startsVal > STARTS_MAX) return false;
  nextStarts = startsVal;
  nextStartsFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putInt(PREF_STARTS, startsVal);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

int SettingsService::getStarts() {
  return nextStartsFlag ? nextStarts : starts;
}

bool SettingsService::setTurnPasses(int passes) {
  if (passes < 1 || passes > 99) return false;
  if (isOn()) {
    beep();
    return false;
  }
  turnPasses = passes;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putInt(PREF_TURN_PASSES, passes);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

int SettingsService::getTurnPasses() {
  return turnPasses;
}

bool SettingsService::setTaperRatio(float ratioK) {
  if (ratioK <= 0.0f) return false;
  float cone = 1.0f / ratioK;
  currentTaperSetting.modeType = TAPER_RATIO;
  currentTaperSetting.authorVal = ratioK;
  currentTaperSetting.coneRatio = cone;
  currentTaperSetting.presetId = 0;
  nextConeRatio = cone;
  nextConeRatioFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putFloat(PREF_CONE_RATIO, cone);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setTaperAngle(float angleDeg) {
  if (angleDeg <= -90.0f || angleDeg >= 90.0f) return false;
  float rad = angleDeg * 0.017453292519943295f;
  float cone = 2.0f * tanf(rad);
  currentTaperSetting.modeType = TAPER_ANGLE;
  currentTaperSetting.authorVal = angleDeg;
  currentTaperSetting.coneRatio = cone;
  currentTaperSetting.presetId = 0;
  nextConeRatio = cone;
  nextConeRatioFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putFloat(PREF_CONE_RATIO, cone);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setTaperPreset(int presetIdx, float angleDeg, float ratioVal) {
  float cone = 0.0f;
  float author = 0.0f;
  if (ratioVal > 0.0f) {
    cone = 1.0f / ratioVal;
    author = ratioVal;
  } else {
    float rad = angleDeg * 0.017453292519943295f;
    cone = 2.0f * tanf(rad);
    author = angleDeg;
  }
  currentTaperSetting.modeType = TAPER_PRESET;
  currentTaperSetting.authorVal = author;
  currentTaperSetting.coneRatio = cone;
  currentTaperSetting.presetId = presetIdx;
  nextConeRatio = cone;
  nextConeRatioFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putFloat(PREF_CONE_RATIO, cone);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

TaperSetting SettingsService::getTaperSetting() {
  return currentTaperSetting;
}

bool SettingsService::setAuxForward(bool fwd) {
  auxForward = fwd;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool(PREF_AUX_FORWARD, fwd);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::getAuxForward() {
  return auxForward;
}

// --- Tier 2 / Category 4: Display & Unit Modes ---
bool SettingsService::setDiameterMode(bool dia) {
  ::isDiameterMode = dia;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool(PREF_DIAMETER_MODE, ::isDiameterMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::isDiameterMode() {
  return ::isDiameterMode;
}

bool SettingsService::setAbsoluteMode(bool abs) {
  ::isAbsoluteMode = abs;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool(PREF_ABSOLUTE_MODE, ::isAbsoluteMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::isAbsoluteMode() {
  return ::isAbsoluteMode;
}

bool SettingsService::setZDroMode(bool zdro) {
  if (zdro && !activeMachineProfile.zAxis.hasDroScale) {
    zdro = false;
  }
  zDisplayModeDRO = zdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool(PREF_Z_DRO_MODE, zDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::isZDroMode() {
  return zDisplayModeDRO;
}

bool SettingsService::setXDroMode(bool xdro) {
  if (xdro && !activeMachineProfile.xAxis.hasDroScale) {
    xdro = false;
  }
  xDisplayModeDRO = xdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putBool(PREF_X_DRO_MODE, xDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::isXDroMode() {
  return xDisplayModeDRO;
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

int SettingsService::getMeasure() {
  return measure;
}

bool SettingsService::setSpindleDisplayMode(int m) {
  if (m < 0 || m > 2) return false;
  spindleDisplayMode = m;
  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putInt("sdm", m);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

int SettingsService::getSpindleDisplayMode() {
  return spindleDisplayMode;
}

// --- Tier 2 / Category 5: Operational Mode & Motion State ---
bool SettingsService::requestMode(int targetMode) {
  if (targetMode < MODE_NORMAL || targetMode > MODE_SLOT) {
    if (targetMode == MODE_Y && ACTIVE_Y) {
      // Allowed
    } else {
      return false;
    }
  }
  nextMode = targetMode;
  nextModeFlag = true;

  Preferences pref;
  pref.begin(PREF_NAMESPACE, false);
  pref.putInt(PREF_MODE, targetMode);
  pref.end();

  machineStatusForcePublish = true;
  return true;
}

int SettingsService::getMode() {
  return nextModeFlag ? nextMode : mode;
}

bool SettingsService::requestIsOn(bool on) {
  nextIsOn = on;
  nextIsOnFlag = true;
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::isOn() {
  return nextIsOnFlag ? nextIsOn : ::isOn;
}

void SettingsService::triggerEstop(int estopCode) {
  setEmergencyStop(estopCode);
  machineStatusForcePublish = true;
}

void SettingsService::clearEstop() {
  emergencyStop = ESTOP_NONE;
  machineStatusForcePublish = true;
}

int SettingsService::getEstopCode() {
  return emergencyStop;
}

void SettingsService::applyCore1PendingLatches() {
  applySettings();
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

  // Check if requesting a group via get
  if (!getAll && key) {
    if (strcasecmp(key, "machine") == 0 || strcasecmp(key, "machine.*") == 0) {
      return getGroup("machine", reply);
    }
    if (strcasecmp(key, "sys") == 0 || strcasecmp(key, "sys.*") == 0) {
      return getGroup("sys", reply);
    }
    if (strcasecmp(key, "prefs") == 0 || strcasecmp(key, "prefs.*") == 0) {
      return getGroup("prefs", reply);
    }
  }

  // --- Tier 1: Machine Profile ---
  if (getAll || strcasecmp(key, "machine.preset") == 0 || strcasecmp(key, "preset") == 0) {
    appendSetting("machine.preset", String(activeMachineProfile.profileId));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.name") == 0) {
    appendSetting("machine.name", String(activeMachineProfile.modelName));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.caps") == 0 || strcasecmp(key, "caps") == 0) {
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)activeMachineProfile.capabilities);
    appendSetting("machine.caps", String(buf));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.bedLength") == 0) {
    appendSetting("machine.dim.bedLength", String(activeMachineProfile.dimensions.bedLengthMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.swingDia") == 0) {
    appendSetting("machine.dim.swingDia", String(activeMachineProfile.dimensions.maxSwingDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.chuckDia") == 0) {
    appendSetting("machine.dim.chuckDia", String(activeMachineProfile.dimensions.chuckDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.dim.spindleBore") == 0) {
    appendSetting("machine.dim.spindleBore", String(activeMachineProfile.dimensions.spindleBoreDiameterMm, 1));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.z.pitchDu") == 0 || strcasecmp(key, "screwZDu") == 0) {
    appendSetting("machine.z.pitchDu", String(activeMachineProfile.zAxis.leadscrewPitchDu()));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.x.pitchDu") == 0 || strcasecmp(key, "screwXDu") == 0) {
    appendSetting("machine.x.pitchDu", String(activeMachineProfile.xAxis.leadscrewPitchDu()));
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
    appendSetting("machine.spindle.ppr", String(activeMachineProfile.spindle.encoder.ppr));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.spindle.ratio") == 0) {
    appendSetting("machine.spindle.ratio", String(activeMachineProfile.spindle.pulleyRatio, 3));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.spindle.effectivePpr") == 0) {
    appendSetting("machine.spindle.effectivePpr", String(activeMachineProfile.spindle.effectivePpr(), 1));
    if (!getAll) return true;
  }

  // --- Tier 2: Controller Infrastructure ---
  if (getAll || strcasecmp(key, "sys.wifi.enabled") == 0) {
    appendSetting("sys.wifi.enabled", WIFI_ENABLED ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.wifi.ssid") == 0) {
    appendSetting("sys.wifi.ssid", WIFI_SSID);
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.wifi.hostname") == 0) {
    appendSetting("sys.wifi.hostname", WIFI_HOSTNAME);
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.dro.zPulsesPerMm") == 0) {
    appendSetting("sys.dro.zPulsesPerMm", String(activeMachineProfile.zAxis.droScale.ppr));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.dro.xPulsesPerMm") == 0) {
    appendSetting("sys.dro.xPulsesPerMm", String(activeMachineProfile.xAxis.droScale.ppr));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.mpg.zPulsesPerRev") == 0 || strcasecmp(key, "pulsePerRevZ") == 0) {
    appendSetting("sys.mpg.zPulsesPerRev", String(PULSE_PER_REVOLUTION_Z, 2));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.mpg.xPulsesPerRev") == 0 || strcasecmp(key, "pulsePerRevX") == 0) {
    appendSetting("sys.mpg.xPulsesPerRev", String(PULSE_PER_REVOLUTION_X, 2));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "screwYDu") == 0) {
    appendSetting("screwYDu", String(SCREW_Y_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "motorStepsY") == 0) {
    appendSetting("motorStepsY", String(MOTOR_STEPS_Y));
    if (!getAll) return true;
  }

  // --- Tier 3: Operator Preferences ---
  if (getAll || strcasecmp(key, "prefs.startupMeasure") == 0 || strcasecmp(key, "prefs.measure") == 0 || strcasecmp(key, "measure") == 0) {
    appendSetting("prefs.startupMeasure", String(measure));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.diaRad") == 0 || strcasecmp(key, "prefs.dia") == 0 || strcasecmp(key, "dia") == 0 || strcasecmp(key, "isDiameterMode") == 0) {
    appendSetting("prefs.diaRad", isDiameterMode() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.absInc") == 0 || strcasecmp(key, "prefs.abs") == 0 || strcasecmp(key, "abs") == 0 || strcasecmp(key, "isAbsoluteMode") == 0) {
    appendSetting("prefs.absInc", isAbsoluteMode() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.zDroActive") == 0 || strcasecmp(key, "prefs.zDro") == 0 || strcasecmp(key, "zDro") == 0 || strcasecmp(key, "zDroActive") == 0) {
    appendSetting("prefs.zDroActive", zDisplayModeDRO ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.xDroActive") == 0 || strcasecmp(key, "prefs.xDro") == 0 || strcasecmp(key, "xDro") == 0 || strcasecmp(key, "xDroActive") == 0) {
    appendSetting("prefs.xDroActive", xDisplayModeDRO ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.feedStepDu") == 0 || strcasecmp(key, "feedStepDu") == 0) {
    appendSetting("prefs.feedStepDu", String(feedStepDu));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.jogStepDu") == 0 || strcasecmp(key, "jogStepDu") == 0) {
    appendSetting("prefs.jogStepDu", String(moveStep));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.backlashZDu") == 0 || strcasecmp(key, "backlashZDu") == 0) {
    appendSetting("prefs.backlashZDu", String(BACKLASH_DU_Z));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.backlashXDu") == 0 || strcasecmp(key, "backlashXDu") == 0) {
    appendSetting("prefs.backlashXDu", String(BACKLASH_DU_X));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.dbAngle") == 0 || strcasecmp(key, "prefs.deadbandAngle") == 0 || strcasecmp(key, "deadbandAngle") == 0) {
    appendSetting("prefs.dbAngle", String(getDeadbandAngle(), 3));
    appendSetting("prefs.dbAngle.isAuto", isDeadbandAngleAuto() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.dbTurns") == 0 || strcasecmp(key, "prefs.deadbandTurns") == 0 || strcasecmp(key, "deadbandTurns") == 0) {
    appendSetting("prefs.dbTurns", String(getDeadbandTurns(), 4));
    appendSetting("prefs.dbTurns.isAuto", isDeadbandTurnsAuto() ? "1" : "0");
    if (!getAll) return true;
  }

  if (getAll) {
    return true;
  }

  *reply = "ERR:SETTINGS." + String(key) + "=unknown key\n";
  return true;
}

bool SettingsService::getGroup(const char* groupName, String* reply) {
  if (!groupName || *groupName == '\0' || !reply) return false;

  auto appendSetting = [&](const char* name, const String& val) {
    *reply += "SETTING." + String(name) + "=" + val + "\n";
  };

  if (strcasecmp(groupName, "machine") == 0) {
    appendSetting("machine.preset", String(activeMachineProfile.profileId));
    appendSetting("machine.name", String(activeMachineProfile.modelName));
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X", (unsigned int)activeMachineProfile.capabilities);
    appendSetting("machine.caps", String(buf));
    appendSetting("machine.dim.bedLength", String(activeMachineProfile.dimensions.bedLengthMm, 1));
    appendSetting("machine.dim.swingDia", String(activeMachineProfile.dimensions.maxSwingDiameterMm, 1));
    appendSetting("machine.dim.chuckDia", String(activeMachineProfile.dimensions.chuckDiameterMm, 1));
    appendSetting("machine.dim.spindleBore", String(activeMachineProfile.dimensions.spindleBoreDiameterMm, 1));
    appendSetting("machine.z.pitchDu", String(activeMachineProfile.zAxis.leadscrewPitchDu()));
    appendSetting("machine.x.pitchDu", String(activeMachineProfile.xAxis.leadscrewPitchDu()));
    appendSetting("machine.z.motorSteps", String(MOTOR_STEPS_Z));
    appendSetting("machine.x.motorSteps", String(MOTOR_STEPS_X));
    appendSetting("machine.spindle.ppr", String(activeMachineProfile.spindle.encoder.ppr));
    appendSetting("machine.spindle.ratio", String(activeMachineProfile.spindle.pulleyRatio, 3));
    appendSetting("machine.spindle.effectivePpr", String(activeMachineProfile.spindle.effectivePpr(), 1));
    return true;
  }

  if (strcasecmp(groupName, "sys") == 0) {
    appendSetting("sys.wifi.enabled", WIFI_ENABLED ? "1" : "0");
    appendSetting("sys.wifi.ssid", WIFI_SSID);
    appendSetting("sys.wifi.hostname", WIFI_HOSTNAME);
    appendSetting("sys.dro.zPulsesPerMm", String(activeMachineProfile.zAxis.droScale.ppr));
    appendSetting("sys.dro.xPulsesPerMm", String(activeMachineProfile.xAxis.droScale.ppr));
    appendSetting("sys.mpg.zPulsesPerRev", String(PULSE_PER_REVOLUTION_Z, 2));
    appendSetting("sys.mpg.xPulsesPerRev", String(PULSE_PER_REVOLUTION_X, 2));
    appendSetting("screwYDu", String(SCREW_Y_DU));
    appendSetting("motorStepsY", String(MOTOR_STEPS_Y));
    return true;
  }

  if (strcasecmp(groupName, "prefs") == 0) {
    appendSetting("prefs.startupMeasure", String(measure));
    appendSetting("prefs.jogStepDu", String(moveStep));
    appendSetting("prefs.feedStepDu", String(feedStepDu));
    appendSetting("prefs.diaRad", isDiameterMode() ? "1" : "0");
    appendSetting("prefs.absInc", isAbsoluteMode() ? "1" : "0");
    appendSetting("prefs.zDroActive", zDisplayModeDRO ? "1" : "0");
    appendSetting("prefs.xDroActive", xDisplayModeDRO ? "1" : "0");
    appendSetting("prefs.backlashZDu", String(BACKLASH_DU_Z));
    appendSetting("prefs.backlashXDu", String(BACKLASH_DU_X));
    appendSetting("prefs.dbAngle", String(getDeadbandAngle(), 3));
    appendSetting("prefs.dbAngle.isAuto", isDeadbandAngleAuto() ? "1" : "0");
    appendSetting("prefs.dbTurns", String(getDeadbandTurns(), 4));
    appendSetting("prefs.dbTurns.isAuto", isDeadbandTurnsAuto() ? "1" : "0");
    return true;
  }

  *reply = "ERR:SETTINGS.getGroup=unknown group: " + String(groupName) + "\n";
  return false;
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

  if (strcasecmp(keyBuf, "machine.spindle.ratio") == 0) {
    float val = atof(valStr);
    if (!setSpindleRatio(val)) { reportErr(keyBuf, "out of bounds (0.01..50.0)"); return true; }
    reportOk(keyBuf, String(val, 3));
    return true;
  }

  // --- Tier 2: Controller Infrastructure ---
  if (strcasecmp(keyBuf, "sys.wifi.enabled") == 0) {
    WIFI_ENABLED = (atoi(valStr) != 0);
    saveWifiConfig();
    reportOk(keyBuf, WIFI_ENABLED ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "sys.wifi.ssid") == 0) {
    WIFI_SSID = valStr;
    saveWifiConfig();
    reportOk(keyBuf, WIFI_SSID);
    return true;
  }

  if (strcasecmp(keyBuf, "sys.wifi.hostname") == 0) {
    WIFI_HOSTNAME = valStr;
    saveWifiConfig();
    reportOk(keyBuf, WIFI_HOSTNAME);
    return true;
  }

  if (strcasecmp(keyBuf, "sys.dro.zPulsesPerMm") == 0) {
    int val = atoi(valStr);
    if (val < 1 || val > 100000) { reportErr(keyBuf, "out of bounds (1..100000)"); return true; }
    activeMachineProfile.zAxis.droScale.ppr = (uint16_t)val;
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.dro.xPulsesPerMm") == 0) {
    int val = atoi(valStr);
    if (val < 1 || val > 100000) { reportErr(keyBuf, "out of bounds (1..100000)"); return true; }
    activeMachineProfile.xAxis.droScale.ppr = (uint16_t)val;
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.mpg.zPulsesPerRev") == 0 || strcasecmp(keyBuf, "pulsePerRevZ") == 0) {
    float val = atof(valStr);
    if (val < 1.0f || val > 10000.0f) { reportErr(keyBuf, "out of bounds (1..10000)"); return true; }
    PULSE_PER_REVOLUTION_Z = val;
    reportOk(keyBuf, String(val, 2));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.mpg.xPulsesPerRev") == 0 || strcasecmp(keyBuf, "pulsePerRevX") == 0) {
    float val = atof(valStr);
    if (val < 1.0f || val > 10000.0f) { reportErr(keyBuf, "out of bounds (1..10000)"); return true; }
    PULSE_PER_REVOLUTION_X = val;
    reportOk(keyBuf, String(val, 2));
    return true;
  }

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
  if (strcasecmp(keyBuf, "prefs.startupMeasure") == 0 || strcasecmp(keyBuf, "prefs.measure") == 0 || strcasecmp(keyBuf, "measure") == 0) {
    int val = atoi(valStr);
    if (!setMeasure(val)) { reportErr(keyBuf, "must be 0 (MM), 1 (TPI), or 2 (IN)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.diaRad") == 0 || strcasecmp(keyBuf, "prefs.dia") == 0 || strcasecmp(keyBuf, "dia") == 0 || strcasecmp(keyBuf, "isDiameterMode") == 0) {
    int val = atoi(valStr);
    setDiameterMode(val != 0);
    reportOk(keyBuf, isDiameterMode() ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.absInc") == 0 || strcasecmp(keyBuf, "prefs.abs") == 0 || strcasecmp(keyBuf, "abs") == 0 || strcasecmp(keyBuf, "isAbsoluteMode") == 0) {
    int val = atoi(valStr);
    setAbsoluteMode(val != 0);
    reportOk(keyBuf, isAbsoluteMode() ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.zDroActive") == 0 || strcasecmp(keyBuf, "prefs.zDro") == 0 || strcasecmp(keyBuf, "zDro") == 0 || strcasecmp(keyBuf, "zDroActive") == 0) {
    int val = atoi(valStr);
    setZDroMode(val != 0);
    reportOk(keyBuf, zDisplayModeDRO ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.xDroActive") == 0 || strcasecmp(keyBuf, "prefs.xDro") == 0 || strcasecmp(keyBuf, "xDro") == 0 || strcasecmp(keyBuf, "xDroActive") == 0) {
    int val = atoi(valStr);
    setXDroMode(val != 0);
    reportOk(keyBuf, xDisplayModeDRO ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.feedStepDu") == 0 || strcasecmp(keyBuf, "feedStepDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 1000000) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    feedStepDu = val;
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.jogStepDu") == 0 || strcasecmp(keyBuf, "jogStepDu") == 0) {
    long val = atol(valStr);
    if (!setJogStepDu(val)) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.dbAngle") == 0 || strcasecmp(keyBuf, "prefs.deadbandAngle") == 0 || strcasecmp(keyBuf, "deadbandAngle") == 0) {
    if (!setDeadbandAngle(valStr)) { reportErr(keyBuf, "invalid angle (0..360 or auto)"); return true; }
    reportOk(keyBuf, isDeadbandAngleAuto() ? "auto" : String(getDeadbandAngle(), 3));
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.dbTurns") == 0 || strcasecmp(keyBuf, "prefs.deadbandTurns") == 0 || strcasecmp(keyBuf, "deadbandTurns") == 0) {
    if (!setDeadbandTurns(valStr)) { reportErr(keyBuf, "invalid turns (0..100 or auto)"); return true; }
    reportOk(keyBuf, isDeadbandTurnsAuto() ? "auto" : String(getDeadbandTurns(), 4));
    return true;
  }

  reportErr(keyBuf, "unknown key");
  return true;
}
