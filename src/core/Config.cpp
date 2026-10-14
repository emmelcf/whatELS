#include "Config.h"
#include "Axis.h"
#include "Input.h"
#include <LittleFS.h>
#include "Debug.h"
#include "PersistentSetting.h"

LatheConfiguration activeMachineProfile = {};

// Variable Definitions
int ENCODER_BACKLASH = DEFAULT_ENCODER_BACKLASH;
int AXIS_ENCODER_BACKLASH = DEFAULT_AXIS_ENCODER_BACKLASH;

ControllerConfig sysConfig = {};
OperatorPreferences operatorPrefs = {};


int ENCODER_STEPS_INT = DEFAULT_ENCODER_PPR * 2;
float ENCODER_STEPS_FLOAT = DEFAULT_ENCODER_PPR * 2;
long SAFE_DISTANCE_DU = DEFAULT_SAFE_DISTANCE_DU;
long SLOT_LEFT_REDUCTION_DU = DEFAULT_SLOT_LEFT_REDUCTION_DU;

bool SHOW_KEY_PRESSES = false;

NumpadMode numpadEntryMode = PITCH;

KeyboardBinding keyboardBindings[] = {
  KEY_BINDING("left", "Left", B_LEFT),
  KEY_BINDING("right", "Right", B_RIGHT),
  KEY_BINDING("up", "Up", B_UP),
  KEY_BINDING("down", "Down", B_DOWN),
  KEY_BINDING("minus", "Minus", B_MINUS),
  KEY_BINDING("plus", "Plus", B_PLUS),
  KEY_BINDING("on", "On / start", B_ON),
  KEY_BINDING("off", "Off / stop", B_OFF),
  KEY_BINDING("stopL", "Set Z left stop", B_STOPL),
  KEY_BINDING("stopR", "Set Z right stop", B_STOPR),
  KEY_BINDING("stopU", "Set X forward stop", B_STOPU),
  KEY_BINDING("stopD", "Set X rear stop", B_STOPD),
  KEY_BINDING("multistart", "Multi-start", B_MULTISTART),
  KEY_BINDING("display", "Display info", B_DISPL),
  KEY_BINDING("step", "Move step", B_STEP),
  KEY_BINDING("measure", "Measure units", B_MEASURE),
  KEY_BINDING("reverse", "Reverse pitch", B_REVERSE),
  KEY_BINDING("diameter", "Set diameter", B_DIAMETER),
  KEY_BINDING("diaRad", "Toggle Dia/Rad", B_DIA_RAD),
  KEY_BINDING("absInc", "Toggle Abs/Inc", B_ABS_INC),
  KEY_BINDING("xToggleDro", "Toggle X Scale DRO", B_X_TOGGLE_DRO),
  KEY_BINDING("zToggleDro", "Toggle Z Scale DRO", B_Z_TOGGLE_DRO),
  KEY_BINDING("digit0", "Digit 0", B_0),
  KEY_BINDING("digit1", "Digit 1", B_1),
  KEY_BINDING("digit2", "Digit 2", B_2),
  KEY_BINDING("digit3", "Digit 3", B_3),
  KEY_BINDING("digit4", "Digit 4", B_4),
  KEY_BINDING("digit5", "Digit 5", B_5),
  KEY_BINDING("digit6", "Digit 6", B_6),
  KEY_BINDING("digit7", "Digit 7", B_7),
  KEY_BINDING("digit8", "Digit 8", B_8),
  KEY_BINDING("digit9", "Digit 9", B_9),
  KEY_BINDING("backspace", "Backspace", B_BACKSPACE),
  KEY_BINDING("modeGears", "Mode gearbox", B_MODE_GEARS),
  KEY_BINDING("modeTurn", "Mode turn", B_MODE_TURN),
  KEY_BINDING("modeFace", "Mode face", B_MODE_FACE),
  KEY_BINDING("modeCone", "Mode cone", B_MODE_CONE),
  KEY_BINDING("modeCut", "Mode cut", B_MODE_CUT),
  KEY_BINDING("modeThread", "Mode thread", B_MODE_THREAD),
  KEY_BINDING("modeAsync", "Mode async", B_MODE_ASYNC),
  KEY_BINDING("modeEllipse", "Mode ellipse", B_MODE_ELLIPSE),
  KEY_BINDING("modeGcode", "Mode GCode", B_MODE_GCODE),
  KEY_BINDING("mode", "Mode menu", B_MODE),
  KEY_BINDING("modeJoystick", "Mode joystick", B_MODE_JOYSTICK),
  KEY_BINDING("modeXGear", "Mode X gearbox", B_MODE_XGEAR),
  KEY_BINDING("modeSlot", "Mode slot", B_MODE_SLOT),
  KEY_BINDING("zeroX", "Zero X", B_X),
  KEY_BINDING("zeroZ", "Zero Z", B_Z),
  KEY_BINDING("enableX", "Enable X", B_X_ENA),
  KEY_BINDING("enableZ", "Enable Z", B_Z_ENA),
};

const int KEYBOARD_BINDING_COUNT = sizeof(keyboardBindings) / sizeof(keyboardBindings[0]);

long clampLongValue(long value, long minValue, long maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

int clampIntValue(int value, int minValue, int maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

float clampFloatValue(float value, float minValue, float maxValue) {
  if (value < minValue) return minValue;
  if (value > maxValue) return maxValue;
  return value;
}

void setMachineConfigDefaults() {
  ENCODER_BACKLASH = DEFAULT_ENCODER_BACKLASH;
  AXIS_ENCODER_BACKLASH = DEFAULT_AXIS_ENCODER_BACKLASH;
  STEP_TIME_MS = DEFAULT_STEP_TIME_MS;
  DELAY_BETWEEN_STEPS_MS = DEFAULT_DELAY_BETWEEN_STEPS_MS;
  ENABLE_CONTINUOUS_MOVE = DEFAULT_ENABLE_CONTINUOUS_MOVE;
  JOG_HOLD_DELAY_MS = DEFAULT_JOG_HOLD_DELAY_MS;
  SAFE_DISTANCE_DU = DEFAULT_SAFE_DISTANCE_DU;
  SLOT_LEFT_REDUCTION_DU = DEFAULT_SLOT_LEFT_REDUCTION_DU;
  sysConfig.mpg.zHandwheel.ppr = 600.0f;
  sysConfig.mpg.xHandwheel.ppr = 600.0f;
  joystickConfig.enabled = DEFAULT_JOYSTICK_ENABLED;
  joystickConfig.zEnabled = DEFAULT_JOYSTICK_Z_ENABLED;
  joystickConfig.xEnabled = DEFAULT_JOYSTICK_X_ENABLED;
  joystickConfig.buttonTogglesOnOff = DEFAULT_JOYSTICK_BUTTON_TOGGLES_ON_OFF;
  joystickConfig.centerSamples = DEFAULT_JOYSTICK_CENTER_SAMPLES;
  joystickConfig.oversamples = DEFAULT_JOYSTICK_OVERSAMPLES;
  joystickConfig.sampleIntervalMs = DEFAULT_JOYSTICK_SAMPLE_INTERVAL_MS;
  joystickConfig.adcMax = DEFAULT_JOYSTICK_ADC_MAX;
  joystickConfig.deadband = DEFAULT_JOYSTICK_DEADBAND;
  joystickConfig.pulseQueueLimit = DEFAULT_JOYSTICK_PULSE_QUEUE_LIMIT;
  joystickConfig.normalRps = DEFAULT_JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND;
  joystickConfig.rapidRps = DEFAULT_JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND;
  joystickConfig.invertZ = DEFAULT_INVERT_JOYSTICK_Z;
  joystickConfig.invertX = DEFAULT_INVERT_JOYSTICK_X;
  joystickConfig.invertButton = DEFAULT_INVERT_JOYSTICK_BUTTON;
}

void applyDerivedMachineConfig() {
  int ppr = (int)round(activeMachineProfile.spindle.effectivePpr());
  if (ppr <= 0) ppr = DEFAULT_ENCODER_PPR;
  ENCODER_STEPS_INT = ppr * 2;
  ENCODER_STEPS_FLOAT = (float)ENCODER_STEPS_INT;
}

void normalizeMachineConfig() {
  ENCODER_BACKLASH = clampIntValue(ENCODER_BACKLASH, 0, 30000);
  AXIS_ENCODER_BACKLASH = clampIntValue(AXIS_ENCODER_BACKLASH, 0, 30000);
  STEP_TIME_MS = clampLongValue(STEP_TIME_MS, 1, 10000);
  DELAY_BETWEEN_STEPS_MS = clampLongValue(DELAY_BETWEEN_STEPS_MS, 0, 10000);
  SAFE_DISTANCE_DU = clampLongValue(SAFE_DISTANCE_DU, 0, 10000000);
  sysConfig.mpg.zHandwheel.ppr = clampFloatValue(sysConfig.mpg.zHandwheel.ppr, 1.0, 100000.0);
  sysConfig.mpg.xHandwheel.ppr = clampFloatValue(sysConfig.mpg.xHandwheel.ppr, 1.0, 100000.0);
  joystickConfig.centerSamples = clampIntValue(joystickConfig.centerSamples, 1, 1024);
  joystickConfig.oversamples = clampIntValue(joystickConfig.oversamples, 1, 1024);
  joystickConfig.sampleIntervalMs = clampIntValue(joystickConfig.sampleIntervalMs, 1, 1000);
  joystickConfig.adcMax = clampIntValue(joystickConfig.adcMax, 1, 65535);
  joystickConfig.deadband = clampIntValue(joystickConfig.deadband, 0, joystickConfig.adcMax - 1);
  joystickConfig.pulseQueueLimit = clampIntValue(joystickConfig.pulseQueueLimit, 1, 1000000);
  joystickConfig.normalRps = clampFloatValue(joystickConfig.normalRps, 0.01, 1000.0);
  joystickConfig.rapidRps = clampFloatValue(joystickConfig.rapidRps, 0.01, 1000.0);
  SLOT_LEFT_REDUCTION_DU = clampLongValue(SLOT_LEFT_REDUCTION_DU, 0, 10000000);
  applyDerivedMachineConfig();
}

void loadMachineConfig() {
  DEBUG_PRINTLN("setMachineConfigDefaults...");
  setMachineConfigDefaults();
  Preferences cfg;
  DEBUG_PRINTLN("cfg.begin...");
  cfg.begin(CONFIG_NAMESPACE);

  DEBUG_PRINTLN("loading fields...");
  ENCODER_BACKLASH = cfg.getInt(CFG_ENCODER_BACKLASH, ENCODER_BACKLASH);
  AXIS_ENCODER_BACKLASH = cfg.getInt(CFG_AXIS_ENCODER_BACKLASH, AXIS_ENCODER_BACKLASH);
  STEP_TIME_MS = cfg.getLong(CFG_STEP_TIME_MS, STEP_TIME_MS);
  DELAY_BETWEEN_STEPS_MS = cfg.getLong(CFG_DELAY_BETWEEN_STEPS_MS, DELAY_BETWEEN_STEPS_MS);
  ENABLE_CONTINUOUS_MOVE = cfg.getBool(CFG_ENABLE_CONTINUOUS_MOVE, ENABLE_CONTINUOUS_MOVE);
  SAFE_DISTANCE_DU = cfg.getLong(CFG_SAFE_DISTANCE_DU, SAFE_DISTANCE_DU);
  SLOT_LEFT_REDUCTION_DU = cfg.getLong(CFG_SLOT_LEFT_REDUCTION_DU, SLOT_LEFT_REDUCTION_DU);
  float legacyPulsePerRevolution = cfg.getFloat(CFG_PULSE_PER_REVOLUTION, -1.0);
  sysConfig.mpg.zHandwheel.ppr = cfg.getFloat(CFG_PULSE_PER_REVOLUTION_Z, legacyPulsePerRevolution >= 1.0 ? legacyPulsePerRevolution : sysConfig.mpg.zHandwheel.ppr);
  sysConfig.mpg.xHandwheel.ppr = cfg.getFloat(CFG_PULSE_PER_REVOLUTION_X, legacyPulsePerRevolution >= 1.0 ? legacyPulsePerRevolution : sysConfig.mpg.xHandwheel.ppr);
  joystickConfig.enabled = cfg.getBool(CFG_JOYSTICK_ENABLED, joystickConfig.enabled);
  joystickConfig.zEnabled = cfg.getBool(CFG_JOYSTICK_Z_ENABLED, joystickConfig.zEnabled);
  joystickConfig.xEnabled = cfg.getBool(CFG_JOYSTICK_X_ENABLED, joystickConfig.xEnabled);
  joystickConfig.buttonTogglesOnOff = cfg.getBool(CFG_JOYSTICK_BUTTON_TOGGLES_ON_OFF, joystickConfig.buttonTogglesOnOff);
  joystickConfig.centerSamples = cfg.getInt(CFG_JOYSTICK_CENTER_SAMPLES, joystickConfig.centerSamples);
  joystickConfig.oversamples = cfg.getInt(CFG_JOYSTICK_OVERSAMPLES, joystickConfig.oversamples);
  joystickConfig.sampleIntervalMs = cfg.getInt(CFG_JOYSTICK_SAMPLE_INTERVAL_MS, joystickConfig.sampleIntervalMs);
  joystickConfig.adcMax = cfg.getInt(CFG_JOYSTICK_ADC_MAX, joystickConfig.adcMax);
  joystickConfig.deadband = cfg.getInt(CFG_JOYSTICK_DEADBAND, joystickConfig.deadband);
  joystickConfig.pulseQueueLimit = cfg.getInt(CFG_JOYSTICK_PULSE_QUEUE_LIMIT, joystickConfig.pulseQueueLimit);
  joystickConfig.normalRps = cfg.getFloat(CFG_JOYSTICK_NORMAL_RPS, joystickConfig.normalRps);
  joystickConfig.rapidRps = cfg.getFloat(CFG_JOYSTICK_RAPID_RPS, joystickConfig.rapidRps);
  joystickConfig.invertZ = cfg.getBool(CFG_INVERT_JOYSTICK_Z, joystickConfig.invertZ);
  joystickConfig.invertX = cfg.getBool(CFG_INVERT_JOYSTICK_X, joystickConfig.invertX);
  joystickConfig.invertButton = cfg.getBool(CFG_INVERT_JOYSTICK_BUTTON, joystickConfig.invertButton);
  cfg.end();
  normalizeMachineConfig();
  loadMachineProfile();
}

void printMachineProfileBanner() {
  Serial.println("\n==================================================================");
  Serial.printf(" NanoELS Machine Profile: %s (%s)\n", activeMachineProfile.profileId, activeMachineProfile.modelName);
  Serial.printf(" Capabilities: 0x%08X\n", (unsigned int)activeMachineProfile.capabilities);
  Serial.print(" Active Flags: [");
  bool first = true;
  for (uint32_t b = 0; b < 32; b++) {
    uint32_t flag = 1UL << b;
    if (activeMachineProfile.capabilities & flag) {
      if (!first) Serial.print(", ");
      Serial.print(getCapabilityName((LatheCapabilityFlags)flag));
      first = false;
    }
  }
  Serial.println("]");
  Serial.printf(" Spindle: Native PPR=%u, Ratio=%.2f -> Effective PPR=%.1f\n",
                activeMachineProfile.spindle.encoder.ppr,
                activeMachineProfile.spindle.pulleyRatio,
                activeMachineProfile.spindle.effectivePpr());
  Serial.printf(" Z-Axis: Pitch=%.3f mm (%ld du), Steps/mm=%.2f (motorSteps=%ld)\n",
                activeMachineProfile.zAxis.leadscrewPitchMm,
                activeMachineProfile.zAxis.leadscrewPitchDu(),
                activeMachineProfile.zAxis.stepsPerMm,
                (long)round(activeMachineProfile.zAxis.stepsPerMm * activeMachineProfile.zAxis.leadscrewPitchMm));
  Serial.printf(" X-Axis: Pitch=%.3f mm (%ld du), Steps/mm=%.2f (motorSteps=%ld)\n",
                activeMachineProfile.xAxis.leadscrewPitchMm,
                activeMachineProfile.xAxis.leadscrewPitchDu(),
                activeMachineProfile.xAxis.stepsPerMm,
                (long)round(activeMachineProfile.xAxis.stepsPerMm * activeMachineProfile.xAxis.leadscrewPitchMm));
  Serial.println("==================================================================\n");
}

void loadMachineProfile() {
  DEBUG_PRINTLN("loadMachineProfile: Checking NVS 'machine'...");
  Preferences pref;
  pref.begin("machine", false);

  String presetSlug = "";
  if (pref.isKey("preset")) {
    presetSlug = pref.getString("preset");
  } else if (pref.isKey("prof_id")) {
    presetSlug = pref.getString("prof_id");
  }

  char defaultSlug[32] = "wm210v_stock";
  getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug));

  const char* slugToLoad = (presetSlug.length() > 0) ? presetSlug.c_str() : defaultSlug;

  LatheConfiguration loadedProfile;
  if (loadProfileFromLittleFS(slugToLoad, &loadedProfile)) {
    DEBUG_PRINTF("loadMachineProfile: Loaded preset '%s' from LittleFS catalog\n", slugToLoad);
    activeMachineProfile = loadedProfile;
  } else {
    DEBUG_PRINTF("loadMachineProfile: Preset '%s' not found on LittleFS. Using ROM failsafe (WM210V)...\n", slugToLoad);
    activeMachineProfile = getDefaultProfile();
  }

  // Apply sparse user overrides from NVS if explicitly customized
  if (pref.isKey("model_name")) {
    String modelName = pref.getString("model_name", activeMachineProfile.modelName);
    strncpy(activeMachineProfile.modelName, modelName.c_str(), sizeof(activeMachineProfile.modelName) - 1);
    activeMachineProfile.modelName[sizeof(activeMachineProfile.modelName) - 1] = '\0';
  }

  if (pref.isKey("screw_z_du")) {
    int32_t screwZDu = pref.getInt("screw_z_du");
    activeMachineProfile.zAxis.leadscrewPitchMm = (float)screwZDu / 10000.0f;
  }

  if (pref.isKey("screw_x_du")) {
    int32_t screwXDu = pref.getInt("screw_x_du");
    activeMachineProfile.xAxis.leadscrewPitchMm = (float)screwXDu / 10000.0f;
  }

  if (pref.isKey("mot_steps_z")) {
    activeMachineProfile.zAxis.stepsPerMm = pref.getFloat("mot_steps_z");
  }

  if (pref.isKey("mot_steps_x")) {
    activeMachineProfile.xAxis.stepsPerMm = pref.getFloat("mot_steps_x");
  }

  if (pref.isKey("enc_ppr")) {
    activeMachineProfile.spindle.encoder.ppr = pref.getUShort("enc_ppr");
  }

  if (pref.isKey("spindle_ratio")) {
    activeMachineProfile.spindle.pulleyRatio = pref.getFloat("spindle_ratio");
  }

  // Clean up any stale DRO flags from NVS; DROs are disabled by default
  if (pref.isKey("has_dro_z")) {
    pref.remove("has_dro_z");
  }
  if (pref.isKey("has_dro_x")) {
    pref.remove("has_dro_x");
  }

  pref.end();

  // Ensure capabilities bitmask stays strictly synchronized with physical hardware
  if (activeMachineProfile.zAxis.hasDroScale) {
    activeMachineProfile.capabilities |= CAP_DRO_Z;
  } else {
    activeMachineProfile.capabilities &= ~CAP_DRO_Z;
  }
  if (activeMachineProfile.xAxis.hasDroScale) {
    activeMachineProfile.capabilities |= CAP_DRO_X;
  } else {
    activeMachineProfile.capabilities &= ~CAP_DRO_X;
  }

  // Synchronize legacy globals for Core 1 compatibility
  z.screwPitch = activeMachineProfile.zAxis.leadscrewPitchDu();
  if (activeMachineProfile.zAxis.stepsPerMm > 0 && activeMachineProfile.zAxis.leadscrewPitchMm > 0) {
    z.motorSteps = (long)round(activeMachineProfile.zAxis.stepsPerMm * activeMachineProfile.zAxis.leadscrewPitchMm);
  }

  x.screwPitch = activeMachineProfile.xAxis.leadscrewPitchDu();
  if (activeMachineProfile.xAxis.stepsPerMm > 0 && activeMachineProfile.xAxis.leadscrewPitchMm > 0) {
    x.motorSteps = (long)round(activeMachineProfile.xAxis.stepsPerMm * activeMachineProfile.xAxis.leadscrewPitchMm);
  }

  ENCODER_STEPS_INT = (int)round(activeMachineProfile.spindle.effectivePpr() * 2.0f);
  ENCODER_STEPS_FLOAT = (float)ENCODER_STEPS_INT;
  printMachineProfileBanner();
}

void saveMachineProfile() {
  DEBUG_PRINTLN("saveMachineProfile: Persisting sparse overrides to NVS 'machine'...");
  Preferences pref;
  pref.begin("machine", false);

  pref.putString("preset", activeMachineProfile.profileId);
  pref.putString("prof_id", activeMachineProfile.profileId);
  pref.putString("model_name", activeMachineProfile.modelName);

  if (activeMachineProfile.zAxis.hasDroScale) {
    pref.putBool("has_dro_z", true);
  } else if (pref.isKey("has_dro_z")) {
    pref.remove("has_dro_z");
  }
  if (activeMachineProfile.xAxis.hasDroScale) {
    pref.putBool("has_dro_x", true);
  } else if (pref.isKey("has_dro_x")) {
    pref.remove("has_dro_x");
  }

  pref.end();
}

void setKeyboardConfigDefaults() {
  SHOW_KEY_PRESSES = false;
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    keyboardBindings[i].code = keyboardBindings[i].defaultCode;
  }
}

const char* keyboardActionLabel(byte actionCode) {
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (keyboardBindings[i].actionCode == actionCode) {
      return keyboardBindings[i].label;
    }
  }
  return "Unmapped";
}

byte keyboardActionForCode(byte physicalCode) {
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (keyboardBindings[i].code == physicalCode) {
      return keyboardBindings[i].actionCode;
    }
  }
  return 0;
}

bool validateKeyboardConfig(String* error) {
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (keyboardBindings[i].code == 0) {
      *error = String(keyboardBindings[i].label) + " must use key code 1..255";
      return false;
    }
    if (keyboardBindings[i].code == 170) {
      *error = String(keyboardBindings[i].label) + " cannot use reserved keyboard code 170";
      return false;
    }
    for (int j = i + 1; j < KEYBOARD_BINDING_COUNT; j++) {
      if (keyboardBindings[i].code == keyboardBindings[j].code) {
        *error = String("Key code ") + String(keyboardBindings[i].code) + " is assigned to both " + keyboardBindings[i].label + " and " + keyboardBindings[j].label;
        return false;
      }
    }
  }
  return true;
}

// Persistent Settings for Keyboard (Stored in Tier 2 "sys")
static PersistentSetting<bool> s_showKeyPresses("sys", "show_keys", false, [](const bool& val) {
  SHOW_KEY_PRESSES = val;
});
static PersistentSetting<uint8_t> s_keyBindings[KEYBOARD_BINDING_COUNT];

void saveKeyboardConfig() {
  s_showKeyPresses.set(SHOW_KEY_PRESSES);
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    s_keyBindings[i].set(keyboardBindings[i].code);
  }
}

void loadKeyboardConfig() {
  setKeyboardConfigDefaults();

  // Transparent migration from legacy "h5keys" namespace if present
  Preferences oldKeys;
  if (oldKeys.begin("h5keys", true)) {
    Preferences sysPref;
    if (sysPref.begin("sys", false)) {
      if (oldKeys.isKey(KCFG_SHOW_KEY_PRESSES)) {
        sysPref.putBool(KCFG_SHOW_KEY_PRESSES, oldKeys.getBool(KCFG_SHOW_KEY_PRESSES));
      }
      for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
        if (oldKeys.isKey(keyboardBindings[i].id)) {
          sysPref.putUChar(keyboardBindings[i].id, oldKeys.getUChar(keyboardBindings[i].id));
        }
      }
      sysPref.end();
    }
    oldKeys.end();
    Preferences clearOld;
    if (clearOld.begin("h5keys", false)) {
      clearOld.clear();
      clearOld.end();
    }
  }

  s_showKeyPresses.init();
  SHOW_KEY_PRESSES = s_showKeyPresses.get();

  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    s_keyBindings[i].setup("sys", keyboardBindings[i].id, keyboardBindings[i].defaultCode);
    s_keyBindings[i].init();
    keyboardBindings[i].code = s_keyBindings[i].get();
  }

  String error = "";
  if (!validateKeyboardConfig(&error)) {
    setKeyboardConfigDefaults();
    saveKeyboardConfig();
  }
  DEBUG_PRINTLN("Finished loadKeyboardConfig");
}

void setWifiConfigDefaults() {
  sysConfig.wifi.enabled = true;
  sysConfig.wifi.ssid = "";
  sysConfig.wifi.password = "";
  sysConfig.wifi.hostname = "nanoels";
}

bool validateWifiConfig(String* error) {
  if (sysConfig.wifi.ssid.length() > 32) {
    *error = "wifiSsid must be 32 characters or less";
    return false;
  }
  if (sysConfig.wifi.password.length() > 63) {
    *error = "wifiPassword must be 63 characters or less";
    return false;
  }
  if (sysConfig.wifi.password.length() > 0 && sysConfig.wifi.password.length() < 8) {
    *error = "wifiPassword must be empty or at least 8 characters";
    return false;
  }
  return true;
}

// Persistent Settings for WiFi (Stored in Tier 2 "sys")
static PersistentSetting<bool> s_wifiEnabled("sys", "wifi_en", true, [](const bool& val) {
  sysConfig.wifi.enabled = val;
});
static PersistentSetting<String> s_wifiSsid("sys", "wifi_ssid", "", [](const String& val) {
  sysConfig.wifi.ssid = val;
});
static PersistentSetting<String> s_wifiPassword("sys", "wifi_pwd", "", [](const String& val) {
  sysConfig.wifi.password = val;
});
static PersistentSetting<String> s_wifiHostname("sys", "wifi_host", "nanoels", [](const String& val) {
  sysConfig.wifi.hostname = val;
});

void loadWifiConfig() {
  setWifiConfigDefaults();

  // Transparent migration from legacy "h5wifi" namespace if present
  Preferences oldWifi;
  if (oldWifi.begin("h5wifi", true)) {
    if (oldWifi.isKey("ssid")) {
      String oldSsid = oldWifi.getString("ssid", "");
      String oldPwd = oldWifi.getString("pwd", "");
      String oldHn = oldWifi.getString("hn", "nanoels");
      bool oldEn = oldWifi.getBool("en", true);
      oldWifi.end();

      Preferences sysPref;
      if (sysPref.begin("sys", false)) {
        sysPref.putString("wifi_ssid", oldSsid);
        sysPref.putString("wifi_pwd", oldPwd);
        sysPref.putString("wifi_host", oldHn);
        sysPref.putBool("wifi_en", oldEn);
        sysPref.end();
      }

      Preferences clearOld;
      if (clearOld.begin("h5wifi", false)) {
        clearOld.clear();
        clearOld.end();
      }
    } else {
      oldWifi.end();
    }
  }

  s_wifiEnabled.init();
  s_wifiSsid.init();
  s_wifiPassword.init();
  s_wifiHostname.init();

  sysConfig.wifi.enabled = s_wifiEnabled.get();
  sysConfig.wifi.ssid = s_wifiSsid.get();
  sysConfig.wifi.password = s_wifiPassword.get();
  sysConfig.wifi.hostname = s_wifiHostname.get();

  String error = "";
  if (!validateWifiConfig(&error)) {
    setWifiConfigDefaults();
  }
}

void saveWifiConfig() {
  s_wifiEnabled.set(sysConfig.wifi.enabled);
  s_wifiSsid.set(sysConfig.wifi.ssid);
  s_wifiPassword.set(sysConfig.wifi.password);
  s_wifiHostname.set(sysConfig.wifi.hostname);
}

void setMeasure(MeasureUnit value) {
  if (measure == value) {
    return;
  }
  measure = value;
  operatorPrefs.measure = value;
  moveStep = measure == MeasureUnit::METRIC ? MOVE_STEP_1 : MOVE_STEP_IMP_1;
  operatorPrefs.jogStepDu = moveStep;
}

void setMeasure(int value) {
  setMeasure(static_cast<MeasureUnit>(value));
}
