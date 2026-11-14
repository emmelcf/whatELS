#include "Config.h"
#include "SettingsService.h"
#include <esp_log.h>
#include "PersistentSetting.h"

static const char* TAG = "CONFIG";

EstopCode emergencyStop = EstopCode::NONE;
void setEmergencyStop(EstopCode kind) {
  emergencyStop = kind;
}

// Variable Definitions
ControllerConfig sysConfig = {};
OperatorPreferences operatorPrefs = {};

bool SHOW_KEY_PRESSES = false;

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

// =============================================================================
// Keyboard Configuration Lifecycle (Tier 2: "sys")
// =============================================================================

void setKeyboardConfigDefaults() {
  SHOW_KEY_PRESSES = false;
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    keyboardBindings[i].code = keyboardBindings[i].defaultCode;
  }
}

bool validateKeyboardConfig(String* error) {
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (keyboardBindings[i].code == 0) {
      if (error) *error = String(keyboardBindings[i].label) + " must use key code 1..255";
      return false;
    }
    if (keyboardBindings[i].code == 170) {
      if (error) *error = String(keyboardBindings[i].label) + " cannot use reserved keyboard code 170";
      return false;
    }
    for (int j = i + 1; j < KEYBOARD_BINDING_COUNT; j++) {
      if (keyboardBindings[i].code == keyboardBindings[j].code) {
        if (error) {
          *error = String("Key code ") + String(keyboardBindings[i].code) +
                   " is assigned to both " + keyboardBindings[i].label +
                   " and " + keyboardBindings[j].label;
        }
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
  if (whatels::core::ISettingsStorage* storage = SettingsService::getStorage()) {
    if (storage->begin("h5keys", true)) {
      bool hasShowKeys = false;
      bool showKeysVal = false;
      if (storage->isKey(KCFG_SHOW_KEY_PRESSES)) {
        hasShowKeys = storage->getBool(KCFG_SHOW_KEY_PRESSES, showKeysVal);
      }
      uint8_t keyVals[KEYBOARD_BINDING_COUNT] = {};
      bool hasKey[KEYBOARD_BINDING_COUNT] = {};
      for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
        if (storage->isKey(keyboardBindings[i].id)) {
          hasKey[i] = storage->getU8(keyboardBindings[i].id, keyVals[i]);
        }
      }
      storage->end();

      if (storage->begin("sys", false)) {
        if (hasShowKeys) {
          storage->putBool(KCFG_SHOW_KEY_PRESSES, showKeysVal);
        }
        for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
          if (hasKey[i]) {
            storage->putU8(keyboardBindings[i].id, keyVals[i]);
          }
        }
        storage->end();
      }

      if (storage->begin("h5keys", false)) {
        storage->clear();
        storage->end();
      }
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
  ESP_LOGD(TAG, "Finished loadKeyboardConfig");
}

// =============================================================================
// WiFi Configuration Lifecycle (Tier 2: "sys")
// =============================================================================

void setWifiConfigDefaults() {
  sysConfig.wifi.enabled = true;
  sysConfig.wifi.ssid = "";
  sysConfig.wifi.password = "";
  sysConfig.wifi.hostname = "whatels";
}

bool validateWifiConfig(String* error) {
  if (sysConfig.wifi.ssid.length() > 32) {
    if (error) *error = "wifiSsid must be 32 characters or less";
    return false;
  }
  if (sysConfig.wifi.password.length() > 63) {
    if (error) *error = "wifiPassword must be 63 characters or less";
    return false;
  }
  if (sysConfig.wifi.password.length() > 0 && sysConfig.wifi.password.length() < 8) {
    if (error) *error = "wifiPassword must be empty or at least 8 characters";
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
static PersistentSetting<String> s_wifiHostname("sys", "wifi_host", "whatels", [](const String& val) {
  sysConfig.wifi.hostname = val;
});

void loadWifiConfig() {
  setWifiConfigDefaults();

  // Transparent migration from legacy "h5wifi" namespace if present
  if (whatels::core::ISettingsStorage* storage = SettingsService::getStorage()) {
    if (storage->begin("h5wifi", true)) {
      if (storage->isKey("ssid")) {
        char buf[64];
        String oldSsid = storage->getString("ssid", buf, sizeof(buf)) ? String(buf) : String("");
        String oldPwd = storage->getString("pwd", buf, sizeof(buf)) ? String(buf) : String("");
        String oldHn = storage->getString("hn", buf, sizeof(buf)) ? String(buf) : String("whatels");
        bool oldEn = true;
        storage->getBool("en", oldEn);
        storage->end();

        if (storage->begin("sys", false)) {
          storage->putString("wifi_ssid", oldSsid.c_str());
          storage->putString("wifi_pwd", oldPwd.c_str());
          storage->putString("wifi_host", oldHn.c_str());
          storage->putBool("wifi_en", oldEn);
          storage->end();
        }

        if (storage->begin("h5wifi", false)) {
          storage->clear();
          storage->end();
        }
      } else {
        storage->end();
      }
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

// =============================================================================
// MPG Handwheel Configuration Lifecycle (Tier 2: "sys")
// =============================================================================

void setMpgConfigDefaults() {
  sysConfig.mpg.zHandwheel.ppr = 600.0f;
  sysConfig.mpg.xHandwheel.ppr = 600.0f;
  sysConfig.mpg.pulseWaitMs = 20;
}

// Persistent Settings for MPG (Stored in Tier 2 "sys")
static PersistentSetting<float> s_mpgZPpr("sys", "mpg_z_ppr", 600.0f, [](const float& val) {
  sysConfig.mpg.zHandwheel.ppr = val;
});
static PersistentSetting<float> s_mpgXPpr("sys", "mpg_x_ppr", 600.0f, [](const float& val) {
  sysConfig.mpg.xHandwheel.ppr = val;
});

void loadMpgConfig() {
  setMpgConfigDefaults();

  // One-time migration from legacy "h5cfg" if present
  if (whatels::core::ISettingsStorage* storage = SettingsService::getStorage()) {
    if (storage->begin("h5cfg", true)) {
      float legacyPulsePerRevolution = -1.0f;
      storage->getFloat("pulseRev", legacyPulsePerRevolution);
      float zPulse = (legacyPulsePerRevolution >= 1.0f) ? legacyPulsePerRevolution : -1.0f;
      float xPulse = (legacyPulsePerRevolution >= 1.0f) ? legacyPulsePerRevolution : -1.0f;
      storage->getFloat("zPulseRev", zPulse);
      storage->getFloat("xPulseRev", xPulse);
      storage->end();

      if (zPulse >= 1.0f || xPulse >= 1.0f) {
        if (storage->begin("sys", false)) {
          if (zPulse >= 1.0f) storage->putFloat("mpg_z_ppr", zPulse);
          if (xPulse >= 1.0f) storage->putFloat("mpg_x_ppr", xPulse);
          storage->end();
        }
      }

      if (storage->begin("h5cfg", false)) {
        storage->clear();
        storage->end();
      }
    }
  }

  s_mpgZPpr.init();
  s_mpgXPpr.init();
  sysConfig.mpg.zHandwheel.ppr = s_mpgZPpr.get();
  sysConfig.mpg.xHandwheel.ppr = s_mpgXPpr.get();
}

void saveMpgConfig() {
  s_mpgZPpr.set(sysConfig.mpg.zHandwheel.ppr);
  s_mpgXPpr.set(sysConfig.mpg.xHandwheel.ppr);
}
