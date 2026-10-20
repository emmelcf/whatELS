#include "SettingsService.h"
#include "Config.h"
#include "OperatorIntent.h"
#include <esp_log.h>
#include "SystemSchemaJson.h"

static const char* TAG = "SETTINGS";
#include "PersistentSetting.h"
#include "motion/kernel/MotionEngine.h"
#include "motion/MotionConversions.h"

static ThreadSetting currentThreadSetting = { THREAD_METRIC, 0.0f, 0 };
static TaperSetting currentTaperSetting = { TAPER_RATIO, 1.0f, 1.0f, 0 };

// Conversational Cycle & Pass State
long setupIndex = 0;
long opIndex = 0;
bool opIndexAdvanceFlag = false;

// Conversational G-code State
String gcodeCommand = "";
int gcodeProgramIndex = 0;
int gcodeProgramCount = 0;
String gcodeSaveName = "";
String gcodeSaveValue = "";
String gcodeProgram = "";
int gcodeProgramCharIndex = 0;

static int s_starts = 1;
static int s_turnPasses = 1;
static bool s_auxForward = true;
static bool s_isOn = false;
static LatheMode s_mode = LatheMode::NORMAL;

static std::optional<float> _deadbandAngle;
static std::optional<float> _deadbandTurns;

static PersistentSetting<bool> s_diameterMode(PREF_NAMESPACE, PREF_DIAMETER_MODE, true, [](const bool& val) {
  operatorPrefs.isDiameterMode = val;
});

static PersistentSetting<bool> s_absoluteMode(PREF_NAMESPACE, PREF_ABSOLUTE_MODE, true, [](const bool& val) {
  operatorPrefs.isAbsoluteMode = val;
});

static PersistentSetting<bool> s_zDroMode(PREF_NAMESPACE, PREF_Z_DRO_MODE, false, [](const bool& val) {
  operatorPrefs.zDroActive = val;
});

static PersistentSetting<bool> s_xDroMode(PREF_NAMESPACE, PREF_X_DRO_MODE, false, [](const bool& val) {
  operatorPrefs.xDroActive = val;
});

static PersistentSetting<int> s_measure(PREF_NAMESPACE, PREF_MEASURE, static_cast<int>(MeasureUnit::METRIC), [](const int& val) {
  operatorPrefs.measure = static_cast<MeasureUnit>(val);
});

static PersistentSetting<int> s_spindleDisplayMode(PREF_NAMESPACE, "sdm", 0, [](const int& /*val*/) {
});

static PersistentSetting<long> s_jogStepDu(PREF_NAMESPACE, PREF_MOVE_STEP, JOG_STEPS_METRIC[0], [](const long& val) {
  operatorPrefs.jogStepDu = val;
});

static PersistentSetting<long> s_feedStepDu(PREF_NAMESPACE, PREF_FEED_STEP, FEED_STEPS_METRIC[0], [](const long& val) {
  operatorPrefs.feedStepDu = val;
});

static void migrateSettingsIfNeeded() {
  Preferences sysPref;
  if (!sysPref.begin("sys", false)) {
    return;
  }

  // Migrate legacy "h5" operator preferences to "prefs" namespace if present
  Preferences oldH5;
  if (oldH5.begin("h5", true)) {
    Preferences prefsPref;
    if (prefsPref.begin("prefs", false)) {
      if (oldH5.isKey(PREF_MEASURE)) prefsPref.putInt(PREF_MEASURE, oldH5.getInt(PREF_MEASURE));
      if (oldH5.isKey(PREF_MOVE_STEP)) prefsPref.putLong(PREF_MOVE_STEP, oldH5.getLong(PREF_MOVE_STEP));
      if (oldH5.isKey(PREF_DIAMETER_MODE)) prefsPref.putBool(PREF_DIAMETER_MODE, oldH5.getBool(PREF_DIAMETER_MODE));
      if (oldH5.isKey(PREF_ABSOLUTE_MODE)) prefsPref.putBool(PREF_ABSOLUTE_MODE, oldH5.getBool(PREF_ABSOLUTE_MODE));
      if (oldH5.isKey(PREF_Z_DRO_MODE)) prefsPref.putBool(PREF_Z_DRO_MODE, oldH5.getBool(PREF_Z_DRO_MODE));
      if (oldH5.isKey(PREF_X_DRO_MODE)) prefsPref.putBool(PREF_X_DRO_MODE, oldH5.getBool(PREF_X_DRO_MODE));
      if (oldH5.isKey("sdm")) prefsPref.putInt("sdm", oldH5.getInt("sdm"));
      if (oldH5.isKey(PREF_SHOW_ANGLE)) prefsPref.putBool(PREF_SHOW_ANGLE, oldH5.getBool(PREF_SHOW_ANGLE));
      if (oldH5.isKey(PREF_SHOW_TACHO)) prefsPref.putBool(PREF_SHOW_TACHO, oldH5.getBool(PREF_SHOW_TACHO));
      if (oldH5.isKey("db_angle")) prefsPref.putFloat("db_angle", oldH5.getFloat("db_angle"));
      if (oldH5.isKey("db_turns")) prefsPref.putFloat("db_turns", oldH5.getFloat("db_turns"));
      prefsPref.end();
    }
    oldH5.end();
    Preferences clearH5;
    if (clearH5.begin("h5", false)) {
      clearH5.clear();
      clearH5.end();
    }
  }

  int storedVersion = sysPref.getInt("sw_ver", 0);
  if (SOFTWARE_VERSION > storedVersion) {
    ESP_LOGI(TAG, "Migrating software settings from v%d to v%d...", storedVersion, SOFTWARE_VERSION);

    // Cascading non-destructive migration blocks for future firmware versions:
    // if (storedVersion < 39) { ... }
    // if (storedVersion < 40) { ... }

    sysPref.putInt("sw_ver", SOFTWARE_VERSION);
    sysConfig.swVersion = SOFTWARE_VERSION;
    ESP_LOGI(TAG, "Settings migration complete. Current version: v%d", SOFTWARE_VERSION);
  } else {
    sysConfig.swVersion = storedVersion;
  }
  sysPref.end();
}

void SettingsService::init() {
  ESP_LOGI(TAG, "Initializing SettingsService...");
  migrateSettingsIfNeeded();

  // Load active machine profile from NVS namespace "machine"
  loadMachineProfile();

  // Load deadband overrides from PREF_NAMESPACE ("prefs")
  Preferences pref;
  pref.begin(PREF_NAMESPACE, true);
  if (pref.isKey("db_angle")) {
    _deadbandAngle = pref.getFloat("db_angle");
    operatorPrefs.deadbandAngle = _deadbandAngle.value();
    operatorPrefs.isDeadbandAngleAuto = false;
  } else {
    _deadbandAngle = std::nullopt;
    operatorPrefs.deadbandAngle = 0.0f;
    operatorPrefs.isDeadbandAngleAuto = true;
  }
  if (pref.isKey("db_turns")) {
    _deadbandTurns = pref.getFloat("db_turns");
    operatorPrefs.deadbandTurns = _deadbandTurns.value();
    operatorPrefs.isDeadbandTurnsAuto = false;
  } else {
    _deadbandTurns = std::nullopt;
    operatorPrefs.deadbandTurns = 0.0f;
    operatorPrefs.isDeadbandTurnsAuto = true;
  }
  pref.end();

  // Load observable operator & display preferences
  s_diameterMode.init();
  operatorPrefs.isDiameterMode = s_diameterMode.get();

  s_absoluteMode.init();
  operatorPrefs.isAbsoluteMode = s_absoluteMode.get();

  s_zDroMode.init();
  operatorPrefs.zDroActive = s_zDroMode.get();

  s_xDroMode.init();
  operatorPrefs.xDroActive = s_xDroMode.get();

  s_measure.init();
  operatorPrefs.measure = static_cast<MeasureUnit>(s_measure.get());

  s_spindleDisplayMode.init();

  s_jogStepDu.init();
  operatorPrefs.jogStepDu = s_jogStepDu.get();

  s_feedStepDu.init();
  operatorPrefs.feedStepDu = s_feedStepDu.get();

  // Initialize intent settings
  long absDu = abs(currentThreadSetting.dupr);
  if (absDu == 0) {
    currentThreadSetting.format = THREAD_METRIC;
    currentThreadSetting.authorPitch = 1.0f;
    currentThreadSetting.dupr = 10000;
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

  // [CONC-02] In-motion flash write lockout to protect Core 1 from SPI flash cache freeze
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.preset=ERR_MOTION_ACTIVE: cannot switch preset while motion is active\n";
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
  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.preset.save=ERR_MOTION_ACTIVE: cannot save preset while motion is active\n";
    return false;
  }

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

  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.preset.delete=ERR_MOTION_ACTIVE: cannot delete preset while motion is active\n";
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

  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.preset.setDefault=ERR_MOTION_ACTIVE: cannot set default preset while motion is active\n";
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

bool SettingsService::getSchema(const char* schemaName, String* reply) {
  if (!reply) return false;
  if (!schemaName || *schemaName == '\0' ||
      strcasecmp(schemaName, "profile") == 0 ||
      strcasecmp(schemaName, "lathe-profile") == 0 ||
      strcasecmp(schemaName, "lathe-profiles") == 0 ||
      strcasecmp(schemaName, "profiles") == 0 ||
      strcasecmp(schemaName, "all") == 0) {
    *reply = String(LATHE_PROFILE_SCHEMA_JSON) + "\n";
    return true;
  }
  *reply = "ERR:SETTINGS.schema=unknown schema: " + String(schemaName) + "\n";
  return false;
}

bool SettingsService::reset(const char* key, String* reply) {
  if (!key || *key == '\0') return false;

  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.reset=ERR_MOTION_ACTIVE: cannot write to NVS flash while motion is active\n";
    return false;
  }

  auto reportOk = [&](const char* name) {
    if (reply) *reply = "OK:SETTINGS.reset=" + String(name) + "\n";
    };

  const LatheConfiguration* pBaseline = getActiveBaseline();

  Preferences pref;
  pref.begin("machine", false);

  if (strcasecmp(key, "machine.z.pitchDu") == 0 || strcasecmp(key, "screwZDu") == 0) {
    activeMachineProfile.zAxis.leadscrewPitchMm = pBaseline->zAxis.leadscrewPitchMm;
    pref.remove("screw_z_du");
    pref.end();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.pitchDu") == 0 || strcasecmp(key, "screwXDu") == 0) {
    activeMachineProfile.xAxis.leadscrewPitchMm = pBaseline->xAxis.leadscrewPitchMm;
    pref.remove("screw_x_du");
    pref.end();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.z.motorSteps") == 0 || strcasecmp(key, "motorStepsZ") == 0) {
    activeMachineProfile.zAxis.stepsPerMm = pBaseline->zAxis.stepsPerMm;
    pref.remove("mot_steps_z");
    pref.end();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.x.motorSteps") == 0 || strcasecmp(key, "motorStepsX") == 0) {
    activeMachineProfile.xAxis.stepsPerMm = pBaseline->xAxis.stepsPerMm;
    pref.remove("mot_steps_x");
    pref.end();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.spindle.ppr") == 0 || strcasecmp(key, "encoderPpr") == 0) {
    activeMachineProfile.spindle.encoder.ppr = pBaseline->spindle.encoder.ppr;
    pref.remove("enc_ppr");
    pref.end();
    applyDerivedMachineConfig();
    reportOk(key);
    return true;
  }
  if (strcasecmp(key, "machine.spindle.ratio") == 0) {
    activeMachineProfile.spindle.pulleyRatio = pBaseline->spindle.pulleyRatio;
    pref.remove("spindle_ratio");
    pref.end();
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

  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS.resetGroup=ERR_MOTION_ACTIVE: cannot write to NVS flash while motion is active\n";
    return false;
  }

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
    operatorPrefs.feedStepDu = 100;
    s_feedStepDu.set(100);
    if (reply) *reply = "OK:SETTINGS.resetGroup=prefs\n";
      return true;
  }

  if (reply) *reply = "ERR:SETTINGS.resetGroup=unknown group: " + String(group) + "\n";
  return false;
}

float SettingsService::getDeadbandAngle() {
  float cpr = activeMachineProfile.spindle.countsPerRev();
  if (cpr <= 0.0f) cpr = getDefaultProfile().spindle.countsPerRev();
  float derived = 360.0f / cpr;
  return _deadbandAngle.value_or(derived);
}

bool SettingsService::isDeadbandAngleAuto() {
  return !_deadbandAngle.has_value();
}

float SettingsService::getDeadbandTurns() {
  float cpr = activeMachineProfile.spindle.countsPerRev();
  if (cpr <= 0.0f) cpr = getDefaultProfile().spindle.countsPerRev();
  float derived = 2.0f / cpr;
  return _deadbandTurns.value_or(derived);
}

bool SettingsService::isDeadbandTurnsAuto() {
  return !_deadbandTurns.has_value();
}

bool SettingsService::setScrewZDu(long du) {
  if (du < 100 || du > 10000000) return false;
  if (activeMachineProfile.zAxis.leadscrewPitchDu() == du) {
    ESP_LOGD(TAG, "Suppressed redundant write for screw_z_du");
    return true;
  }
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

  applyDerivedMachineConfig();
  return true;
}

bool SettingsService::setScrewXDu(long du) {
  if (du < 100 || du > 10000000) return false;
  if (activeMachineProfile.xAxis.leadscrewPitchDu() == du) {
    ESP_LOGD(TAG, "Suppressed redundant write for screw_x_du");
    return true;
  }
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

  applyDerivedMachineConfig();
  return true;
}

bool SettingsService::setMotorStepsZ(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  long currentStepsZ = activeMachineProfile.zAxis.motorSteps();
  if (currentStepsZ == steps) {
    ESP_LOGD(TAG, "Suppressed redundant write for mot_steps_z");
    return true;
  }
  float pitchMm = activeMachineProfile.zAxis.leadscrewPitchMm;
  if (pitchMm <= 0.0f) return false;
  float stepsPerMm = (float)steps / pitchMm;
  activeMachineProfile.zAxis.stepsPerMm = stepsPerMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  long baseSteps = pBaseline ? pBaseline->zAxis.motorSteps() : -1;
  if (baseSteps == steps) {
    pref.remove("mot_steps_z");
  } else {
    pref.putFloat("mot_steps_z", stepsPerMm);
  }
  pref.end();

  applyDerivedMachineConfig();
  return true;
}

bool SettingsService::setMotorStepsX(long steps) {
  if (steps < 10 || steps > 1000000) return false;
  long currentStepsX = activeMachineProfile.xAxis.motorSteps();
  if (currentStepsX == steps) {
    ESP_LOGD(TAG, "Suppressed redundant write for mot_steps_x");
    return true;
  }
  float pitchMm = activeMachineProfile.xAxis.leadscrewPitchMm;
  if (pitchMm <= 0.0f) return false;
  float stepsPerMm = (float)steps / pitchMm;
  activeMachineProfile.xAxis.stepsPerMm = stepsPerMm;

  const LatheConfiguration* pBaseline = getActiveBaseline();
  Preferences pref;
  pref.begin("machine", false);
  long baseSteps = pBaseline ? pBaseline->xAxis.motorSteps() : -1;
  if (baseSteps == steps) {
    pref.remove("mot_steps_x");
  } else {
    pref.putFloat("mot_steps_x", stepsPerMm);
  }
  pref.end();

  applyDerivedMachineConfig();
  return true;
}

bool SettingsService::setEncoderPpr(int ppr) {
  if (ppr < 10 || ppr > 30000) return false;
  if (activeMachineProfile.spindle.encoder.ppr == (uint16_t)ppr) {
    ESP_LOGD(TAG, "Suppressed redundant write for enc_ppr");
    return true;
  }
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

  applyDerivedMachineConfig();
  return true;
}

bool SettingsService::setSpindleRatio(float ratio) {
  if (ratio < 0.01f || ratio > 50.0f) return false;
  if (fabsf(activeMachineProfile.spindle.pulleyRatio - ratio) < 0.0001f) {
    ESP_LOGD(TAG, "Suppressed redundant write for spindle_ratio");
    return true;
  }
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

  applyDerivedMachineConfig();
  return true;
}


// --- Tier 2 / Category 1: Travel Stops & Boundaries ---
bool SettingsService::setAxisLimitDu(AxisId axis, LimitDirection dir, long limitDu) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  const char* label = (dir == LIMIT_LEFT_OR_UP) ? "LEFT" : "RIGHT";
  MotionEngine::instance().setSoftLimitDu(axis, slot, limitDu, label, true);

  return true;
}

bool SettingsService::clearAxisLimit(AxisId axis, LimitDirection dir) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  MotionEngine::instance().getSoftLimits(axis).clearLimit(slot);

  return true;
}

bool SettingsService::hasAxisLimit(AxisId axis, LimitDirection dir) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  return MotionEngine::instance().getSoftLimits(axis).getSlot(slot).isEnforced();
}

long SettingsService::getAxisLimitDu(AxisId axis, LimitDirection dir) {
  size_t slot = (dir == LIMIT_LEFT_OR_UP) ? 0 : 1;
  const SoftLimit& lim = MotionEngine::instance().getSoftLimits(axis).getSlot(slot);
  return lim.isEnforced() ? lim.coordDu : 0;
}

// --- Tier 2 / Category 2: Pitch, Threading & Feed Intent ---
bool SettingsService::setFeedrateDupr(long duprVal, bool preserveDirection) {
  if (preserveDirection && (currentThreadSetting.dupr < 0)) {
    duprVal = -abs(duprVal);
  }
  long absDu = abs(duprVal);
  if (operatorPrefs.measure == MeasureUnit::METRIC) {
    currentThreadSetting.format = THREAD_METRIC;
    currentThreadSetting.authorPitch = MotionConversions::duToMm(absDu);
  } else if (operatorPrefs.measure == MeasureUnit::TPI) {
    currentThreadSetting.format = THREAD_TPI;
    currentThreadSetting.authorPitch = (absDu > 0) ? MotionConversions::duprToTpi(absDu) : 0.0f;
  } else {
    currentThreadSetting.format = THREAD_INCH;
    currentThreadSetting.authorPitch = MotionConversions::duToInch(absDu);
  }
  currentThreadSetting.dupr = duprVal;

  return true;
}

bool SettingsService::setFeedrateMm(float pitchMm, bool preserveDirection) {
  if (pitchMm <= 0.0f) return false;
  long duVal = MotionConversions::mmToDu(pitchMm);
  if (preserveDirection && (currentThreadSetting.dupr < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_METRIC;
  currentThreadSetting.authorPitch = pitchMm;
  currentThreadSetting.dupr = duVal;

  return true;
}

bool SettingsService::setFeedrateTpi(float tpi, bool preserveDirection) {
  if (tpi <= 0.0f) return false;
  long duVal = MotionConversions::tpiToDupr(tpi);
  if (preserveDirection && (currentThreadSetting.dupr < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_TPI;
  currentThreadSetting.authorPitch = tpi;
  currentThreadSetting.dupr = duVal;

  return true;
}

bool SettingsService::setFeedrateInch(float pitchInch, bool preserveDirection) {
  if (pitchInch <= 0.0f) return false;
  long duVal = MotionConversions::inchToDu(pitchInch);
  if (preserveDirection && (currentThreadSetting.dupr < 0)) {
    duVal = -abs(duVal);
  }
  currentThreadSetting.format = THREAD_INCH;
  currentThreadSetting.authorPitch = pitchInch;
  currentThreadSetting.dupr = duVal;

  return true;
}

bool SettingsService::reverseFeedDirection() {
  currentThreadSetting.dupr = -currentThreadSetting.dupr;

  return true;
}

ThreadSetting SettingsService::getThreadSetting() {
  return currentThreadSetting;
}

long SettingsService::getJogStepDu() {
  return operatorPrefs.jogStepDu;
}

bool SettingsService::cycleJogStep() {
  long nextStep = JOG_STEPS_METRIC[0];
  if (operatorPrefs.measure == MeasureUnit::METRIC) {
    if (operatorPrefs.jogStepDu == JOG_STEPS_METRIC[0]) {
      nextStep = JOG_STEPS_METRIC[1];
    } else if (operatorPrefs.jogStepDu == JOG_STEPS_METRIC[1]) {
      nextStep = JOG_STEPS_METRIC[2];
    } else {
      nextStep = JOG_STEPS_METRIC[0];
    }
  } else {
    if (operatorPrefs.jogStepDu == JOG_STEPS_IMPERIAL[0]) {
      nextStep = JOG_STEPS_IMPERIAL[1];
    } else if (operatorPrefs.jogStepDu == JOG_STEPS_IMPERIAL[1]) {
      nextStep = JOG_STEPS_IMPERIAL[2];
    } else {
      nextStep = JOG_STEPS_IMPERIAL[0];
    }
  }
  return setJogStepDu(nextStep);
}

// --- Tier 2 / Category 3: Cycle & Geometry Parameters ---
bool SettingsService::setStarts(int startsVal) {
  if (startsVal < 1 || startsVal > STARTS_MAX) return false;
  s_starts = startsVal;

  return true;
}

int SettingsService::getStarts() {
  return s_starts;
}

bool SettingsService::setTurnPasses(int passes) {
  if (passes < 1 || passes > 99) return false;
  if (isOn()) {
    return false;
  }
  s_turnPasses = passes;

  return true;
}

int SettingsService::getTurnPasses() {
  return s_turnPasses;
}

bool SettingsService::setTaperRatio(float ratioK) {
  if (ratioK < 0.0f) return false;
  if (ratioK == 0.0f) {
    currentTaperSetting.modeType = TAPER_RATIO;
    currentTaperSetting.authorVal = 0.0f;
    currentTaperSetting.coneRatio = 0.0f;
    currentTaperSetting.presetId = 0;
    return true;
  }
  currentTaperSetting.modeType = TAPER_RATIO;
  currentTaperSetting.authorVal = ratioK;
  currentTaperSetting.coneRatio = ratioK;
  currentTaperSetting.presetId = 0;

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

  return true;
}

TaperSetting SettingsService::getTaperSetting() {
  return currentTaperSetting;
}

float SettingsService::getTaperRatio() {
  return currentTaperSetting.coneRatio;
}

bool SettingsService::setAuxForward(bool fwd) {
  s_auxForward = fwd;

  return true;
}

bool SettingsService::getAuxForward() {
  return s_auxForward;
}

// --- Tier 2 / Category 4: Display & Unit Modes ---
bool SettingsService::setDiameterMode(bool dia) {
  operatorPrefs.isDiameterMode = dia;
  return s_diameterMode.set(dia);
}

bool SettingsService::isDiameterMode() {
  return s_diameterMode.get();
}

bool SettingsService::setAbsoluteMode(bool abs) {
  operatorPrefs.isAbsoluteMode = abs;
  return s_absoluteMode.set(abs);
}

bool SettingsService::isAbsoluteMode() {
  return s_absoluteMode.get();
}

bool SettingsService::setZDroMode(bool zdro) {
  if (zdro && !activeMachineProfile.zAxis.hasDroScale) {
    zdro = false;
  }
  operatorPrefs.zDroActive = zdro;
  return s_zDroMode.set(zdro);
}

bool SettingsService::isZDroMode() {
  return s_zDroMode.get();
}

bool SettingsService::setXDroMode(bool xdro) {
  if (xdro && !activeMachineProfile.xAxis.hasDroScale) {
    xdro = false;
  }
  operatorPrefs.xDroActive = xdro;
  return s_xDroMode.set(xdro);
}

bool SettingsService::isXDroMode() {
  return s_xDroMode.get();
}

bool SettingsService::setMeasure(MeasureUnit m) {
  return setMeasure(static_cast<int>(m));
}

bool SettingsService::setMeasure(int m) {
  if (m < 0 || m > 2) return false;
  operatorPrefs.measure = static_cast<MeasureUnit>(m);
  return s_measure.set(m);
}

MeasureUnit SettingsService::getMeasure() {
  return static_cast<MeasureUnit>(s_measure.get());
}

bool SettingsService::setSpindleDisplayMode(int m) {
  if (m < 0 || m > 2) return false;
  return s_spindleDisplayMode.set(m);
}

int SettingsService::getSpindleDisplayMode() {
  return s_spindleDisplayMode.get();
}

// --- Tier 2 / Category 5: Operational Mode & Motion State ---
bool SettingsService::requestMode(int targetMode) {
  if (targetMode < static_cast<int>(LatheMode::NORMAL) || targetMode > static_cast<int>(LatheMode::SLOT)) {
    return false;
  }
  s_mode = static_cast<LatheMode>(targetMode);

  return true;
}

int SettingsService::getMode() {
  return static_cast<int>(s_mode);
}

bool SettingsService::requestIsOn(bool on) {
  s_isOn = on;
  if (!on) {
    MotionEngine::instance().stopAll();
  }
  return true;
}

bool SettingsService::isOn() {
  return s_isOn;
}

void SettingsService::triggerEstop(int estopCode) {
  setEmergencyStop(static_cast<EstopCode>(estopCode));
}

void SettingsService::clearEstop() {
  emergencyStop = EstopCode::NONE;
}

int SettingsService::getEstopCode() {
  return static_cast<int>(emergencyStop);
}

void SettingsService::applyCore1PendingLatches() {
  // FreeRTOS Core 1 MotionEngine task samples parameters continuously
}

bool SettingsService::setJogStepDu(long du) {
  if (du < 1 || du > 1000000) return false;
  operatorPrefs.jogStepDu = du;
  return s_jogStepDu.set(du);
}

bool SettingsService::setDeadbandAngle(const char* val) {
  if (!val || *val == '\0' || strcasecmp(val, "auto") == 0 || strcasecmp(val, "default") == 0) {
    if (!_deadbandAngle.has_value()) {
      ESP_LOGD(TAG, "Suppressed redundant write for db_angle (already auto)");
      return true;
    }
    _deadbandAngle = std::nullopt;
    operatorPrefs.deadbandAngle = 0.0f;
    operatorPrefs.isDeadbandAngleAuto = true;
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.remove("db_angle");
    pref.end();
  } else {
    float deg = atof(val);
    if (deg < 0.0f || deg > 360.0f) return false;
    if (_deadbandAngle.has_value() && fabsf(_deadbandAngle.value() - deg) < 0.0001f) {
      ESP_LOGD(TAG, "Suppressed redundant write for db_angle");
      return true;
    }
    _deadbandAngle = deg;
    operatorPrefs.deadbandAngle = deg;
    operatorPrefs.isDeadbandAngleAuto = false;
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.putFloat("db_angle", deg);
    pref.end();
  }
  return true;
}

bool SettingsService::setDeadbandTurns(const char* val) {
  if (!val || *val == '\0' || strcasecmp(val, "auto") == 0 || strcasecmp(val, "default") == 0) {
    if (!_deadbandTurns.has_value()) {
      ESP_LOGD(TAG, "Suppressed redundant write for db_turns (already auto)");
      return true;
    }
    _deadbandTurns = std::nullopt;
    operatorPrefs.deadbandTurns = 0.0f;
    operatorPrefs.isDeadbandTurnsAuto = true;
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.remove("db_turns");
    pref.end();
  } else {
    float turns = atof(val);
    if (turns < 0.0f || turns > 10.0f) return false;
    if (_deadbandTurns.has_value() && fabsf(_deadbandTurns.value() - turns) < 0.0001f) {
      ESP_LOGD(TAG, "Suppressed redundant write for db_turns");
      return true;
    }
    _deadbandTurns = turns;
    operatorPrefs.deadbandTurns = turns;
    operatorPrefs.isDeadbandTurnsAuto = false;
    Preferences pref;
    pref.begin(PREF_NAMESPACE, false);
    pref.putFloat("db_turns", turns);
    pref.end();
  }
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
    appendSetting("machine.z.motorSteps", String((long)round(activeMachineProfile.zAxis.motorSteps())));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "machine.x.motorSteps") == 0 || strcasecmp(key, "motorStepsX") == 0) {
    appendSetting("machine.x.motorSteps", String((long)round(activeMachineProfile.xAxis.motorSteps())));
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
    appendSetting("sys.wifi.enabled", sysConfig.wifi.enabled ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.wifi.ssid") == 0) {
    appendSetting("sys.wifi.ssid", sysConfig.wifi.ssid);
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.wifi.hostname") == 0) {
    appendSetting("sys.wifi.hostname", sysConfig.wifi.hostname);
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
    appendSetting("sys.mpg.zPulsesPerRev", String(sysConfig.mpg.zHandwheel.ppr));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "sys.mpg.xPulsesPerRev") == 0 || strcasecmp(key, "pulsePerRevX") == 0) {
    appendSetting("sys.mpg.xPulsesPerRev", String(sysConfig.mpg.xHandwheel.ppr));
    if (!getAll) return true;
  }

  // --- Tier 3: Operator Preferences ---
  if (getAll || strcasecmp(key, "prefs.startupMeasure") == 0 || strcasecmp(key, "prefs.measure") == 0 || strcasecmp(key, "measure") == 0) {
    appendSetting("prefs.startupMeasure", String(static_cast<int>(operatorPrefs.measure)));
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
    appendSetting("prefs.zDroActive", isZDroMode() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.xDroActive") == 0 || strcasecmp(key, "prefs.xDro") == 0 || strcasecmp(key, "xDro") == 0 || strcasecmp(key, "xDroActive") == 0) {
    appendSetting("prefs.xDroActive", isXDroMode() ? "1" : "0");
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.feedStepDu") == 0 || strcasecmp(key, "feedStepDu") == 0) {
    appendSetting("prefs.feedStepDu", String(operatorPrefs.feedStepDu));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.jogStepDu") == 0 || strcasecmp(key, "jogStepDu") == 0) {
    appendSetting("prefs.jogStepDu", String(operatorPrefs.jogStepDu));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.backlashZDu") == 0 || strcasecmp(key, "backlashZDu") == 0) {
    appendSetting("prefs.backlashZDu", String(activeMachineProfile.zAxis.backlashDu()));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "prefs.backlashXDu") == 0 || strcasecmp(key, "backlashXDu") == 0) {
    appendSetting("prefs.backlashXDu", String(activeMachineProfile.xAxis.backlashDu()));
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
    appendSetting("machine.z.motorSteps", String((long)round(activeMachineProfile.zAxis.motorSteps())));
    appendSetting("machine.x.motorSteps", String((long)round(activeMachineProfile.xAxis.motorSteps())));
    appendSetting("machine.spindle.ppr", String(activeMachineProfile.spindle.encoder.ppr));
    appendSetting("machine.spindle.ratio", String(activeMachineProfile.spindle.pulleyRatio, 3));
    appendSetting("machine.spindle.effectivePpr", String(activeMachineProfile.spindle.effectivePpr(), 1));
    return true;
  }

  if (strcasecmp(groupName, "sys") == 0) {
    appendSetting("sys.wifi.enabled", sysConfig.wifi.enabled ? "1" : "0");
    appendSetting("sys.wifi.ssid", sysConfig.wifi.ssid);
    appendSetting("sys.wifi.hostname", sysConfig.wifi.hostname);
    appendSetting("sys.dro.zPulsesPerMm", String(activeMachineProfile.zAxis.droScale.ppr));
    appendSetting("sys.dro.xPulsesPerMm", String(activeMachineProfile.xAxis.droScale.ppr));
    appendSetting("sys.mpg.zPulsesPerRev", String(sysConfig.mpg.zHandwheel.ppr));
    appendSetting("sys.mpg.xPulsesPerRev", String(sysConfig.mpg.xHandwheel.ppr));
    return true;
  }

  if (strcasecmp(groupName, "prefs") == 0) {
    appendSetting("prefs.startupMeasure", String(static_cast<int>(operatorPrefs.measure)));
    appendSetting("prefs.jogStepDu", String(operatorPrefs.jogStepDu));
    appendSetting("prefs.feedStepDu", String(operatorPrefs.feedStepDu));
    appendSetting("prefs.diaRad", isDiameterMode() ? "1" : "0");
    appendSetting("prefs.absInc", isAbsoluteMode() ? "1" : "0");
    appendSetting("prefs.zDroActive", isZDroMode() ? "1" : "0");
    appendSetting("prefs.xDroActive", isXDroMode() ? "1" : "0");
    appendSetting("prefs.backlashZDu", String(activeMachineProfile.zAxis.backlashDu()));
    appendSetting("prefs.backlashXDu", String(activeMachineProfile.xAxis.backlashDu()));
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

  // [CONC-02] In-motion flash write lockout
  if (MotionEngine::instance().isAnyAxisMoving()) {
    if (reply) *reply = "ERR:SETTINGS=ERR_MOTION_ACTIVE: cannot write to NVS flash while motion is active\n";
    return false;
  }

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
    sysConfig.wifi.enabled = (atoi(valStr) != 0);
    saveWifiConfig();
    reportOk(keyBuf, sysConfig.wifi.enabled ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "sys.wifi.ssid") == 0) {
    sysConfig.wifi.ssid = valStr;
    saveWifiConfig();
    reportOk(keyBuf, sysConfig.wifi.ssid);
    return true;
  }

  if (strcasecmp(keyBuf, "sys.wifi.hostname") == 0) {
    sysConfig.wifi.hostname = valStr;
    saveWifiConfig();
    reportOk(keyBuf, sysConfig.wifi.hostname);
    return true;
  }

  if (strcasecmp(keyBuf, "sys.dro.zPulsesPerMm") == 0) {
    int val = atoi(valStr);
    if (val < 1 || val > 100000) { reportErr(keyBuf, "out of bounds (1..100000)"); return true; }
    activeMachineProfile.zAxis.droScale.ppr = (uint16_t)val;
    applyDerivedMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.dro.xPulsesPerMm") == 0) {
    int val = atoi(valStr);
    if (val < 1 || val > 100000) { reportErr(keyBuf, "out of bounds (1..100000)"); return true; }
    activeMachineProfile.xAxis.droScale.ppr = (uint16_t)val;
    applyDerivedMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.mpg.zPulsesPerRev") == 0 || strcasecmp(keyBuf, "pulsePerRevZ") == 0) {
    float val = atof(valStr);
    if (val < 1.0f || val > 10000.0f) { reportErr(keyBuf, "out of bounds (1..10000)"); return true; }
    sysConfig.mpg.zHandwheel.ppr = val;
    reportOk(keyBuf, String(val, 2));
    return true;
  }

  if (strcasecmp(keyBuf, "sys.mpg.xPulsesPerRev") == 0 || strcasecmp(keyBuf, "pulsePerRevX") == 0) {
    float val = atof(valStr);
    if (val < 1.0f || val > 10000.0f) { reportErr(keyBuf, "out of bounds (1..10000)"); return true; }
    sysConfig.mpg.xHandwheel.ppr = val;
    reportOk(keyBuf, String(val, 2));
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
    reportOk(keyBuf, isZDroMode() ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.xDroActive") == 0 || strcasecmp(keyBuf, "prefs.xDro") == 0 || strcasecmp(keyBuf, "xDro") == 0 || strcasecmp(keyBuf, "xDroActive") == 0) {
    int val = atoi(valStr);
    setXDroMode(val != 0);
    reportOk(keyBuf, isXDroMode() ? "1" : "0");
    return true;
  }

  if (strcasecmp(keyBuf, "prefs.feedStepDu") == 0 || strcasecmp(keyBuf, "feedStepDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 1000000) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    s_feedStepDu.set(val);
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
