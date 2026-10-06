#include "SettingsService.h"
#include "Config.h"
#include "Axis.h"
#include "Motion.h"
#include "Input.h"
#include "WebServerModule.h"
#include <Preferences.h>

static std::optional<float> _deadbandAngle;
static std::optional<float> _deadbandTurns;

void SettingsService::init() {
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
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

bool SettingsService::setDiameterMode(bool dia) {
  isDiameterMode = dia;
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putBool("dia", isDiameterMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setAbsoluteMode(bool abs) {
  isAbsoluteMode = abs;
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putBool("abs", isAbsoluteMode);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setZDroMode(bool zdro) {
  zDisplayModeDRO = zdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putBool("zdro", zDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setXDroMode(bool xdro) {
  xDisplayModeDRO = xdro;
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putBool("xdro", xDisplayModeDRO);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setMeasure(int m) {
  if (m < 0 || m > 2) return false;
  ::setMeasure(m);
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putInt(PREF_MEASURE, m);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setJogStepDu(long du) {
  if (du < 1 || du > 1000000) return false;
  moveStep = du;
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  pref.putLong(PREF_MOVE_STEP, du);
  pref.end();
  machineStatusForcePublish = true;
  return true;
}

bool SettingsService::setDeadbandAngle(const char* val) {
  Preferences pref;
  pref.begin(PREF_NAMESPACE);
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
  pref.begin(PREF_NAMESPACE);
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

  if (getAll || strcasecmp(key, "screwZDu") == 0) {
    appendSetting("screwZDu", String(SCREW_Z_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "screwXDu") == 0) {
    appendSetting("screwXDu", String(SCREW_X_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "screwYDu") == 0) {
    appendSetting("screwYDu", String(SCREW_Y_DU));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "motorStepsZ") == 0) {
    appendSetting("motorStepsZ", String(MOTOR_STEPS_Z));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "motorStepsX") == 0) {
    appendSetting("motorStepsX", String(MOTOR_STEPS_X));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "motorStepsY") == 0) {
    appendSetting("motorStepsY", String(MOTOR_STEPS_Y));
    if (!getAll) return true;
  }
  if (getAll || strcasecmp(key, "encoderPpr") == 0) {
    appendSetting("encoderPpr", String(ENCODER_PPR));
    if (!getAll) return true;
  }
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

  if (strcasecmp(keyBuf, "screwZDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 10000000) { reportErr(keyBuf, "out of bounds (1..10000000)"); return true; }
    SCREW_Z_DU = val;
    z.screwPitch = val / 10000.0f;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "screwXDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 10000000) { reportErr(keyBuf, "out of bounds (1..10000000)"); return true; }
    SCREW_X_DU = val;
    x.screwPitch = val / 10000.0f;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
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

  if (strcasecmp(keyBuf, "motorStepsZ") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 1000000) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    MOTOR_STEPS_Z = val;
    z.motorSteps = val;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "motorStepsX") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 1000000) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    MOTOR_STEPS_X = val;
    x.motorSteps = val;
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

  if (strcasecmp(keyBuf, "encoderPpr") == 0) {
    int val = atoi(valStr);
    if (val < 1 || val > 15000) { reportErr(keyBuf, "out of bounds (1..15000)"); return true; }
    ENCODER_PPR = val;
    applyDerivedMachineConfig();
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

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

  if (strcasecmp(keyBuf, "feedStepDu") == 0) {
    long val = atol(valStr);
    if (val < 1 || val > 100000) { reportErr(keyBuf, "out of bounds (1..100000)"); return true; }
    setFeedStepDu(val);
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "jogStepDu") == 0) {
    long val = atol(valStr);
    if (!setJogStepDu(val)) { reportErr(keyBuf, "out of bounds (1..1000000)"); return true; }
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "deadbandAngle") == 0) {
    if (!setDeadbandAngle(valStr)) { reportErr(keyBuf, "out of bounds (0..360 or 'auto')"); return true; }
    reportOk(keyBuf, isDeadbandAngleAuto() ? "auto" : String(getDeadbandAngle(), 3));
    return true;
  }

  if (strcasecmp(keyBuf, "deadbandTurns") == 0) {
    if (!setDeadbandTurns(valStr)) { reportErr(keyBuf, "out of bounds (0..100 or 'auto')"); return true; }
    reportOk(keyBuf, isDeadbandTurnsAuto() ? "auto" : String(getDeadbandTurns(), 4));
    return true;
  }

  if (strcasecmp(keyBuf, "backlashZDu") == 0) {
    long val = atol(valStr);
    if (val < 0 || val > 10000000) { reportErr(keyBuf, "out of bounds (0..10000000)"); return true; }
    BACKLASH_DU_Z = val;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  if (strcasecmp(keyBuf, "backlashXDu") == 0) {
    long val = atol(valStr);
    if (val < 0 || val > 10000000) { reportErr(keyBuf, "out of bounds (0..10000000)"); return true; }
    BACKLASH_DU_X = val;
    saveMachineConfig();
    reportOk(keyBuf, String(val));
    return true;
  }

  reportErr(keyBuf, "unknown setting key");
  return true;
}
