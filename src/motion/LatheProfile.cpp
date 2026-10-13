#include "LatheProfile.h"
#include <string.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

LatheConfiguration getDefaultProfile() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "wm210v_8x16", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "WM210V 8x16 Bench Lathe", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_MOTOR_X;

  // Z Axis (Carriage Leadscrew Motorized)
  p.zAxis.hasMotor = true;
  p.zAxis.drive = { DRIVE_GENERIC, -1, -1, -1 };
  p.zAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.zAxis.leadscrewPitchMm = 2.0f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 1500.0f;
  p.zAxis.maxAccelMmSec2 = 250.0f;
  p.zAxis.hasDroScale = false;
  p.zAxis.droScale = { 200, 9, 12, false, false, -1 };
  p.zAxis.hasMpg = false;
  p.zAxis.limits = { 0.0f, 350.0f, -1, -1, false };

  // X Axis (Cross-Slide Leadscrew Motorized)
  p.xAxis.hasMotor = true;
  p.xAxis.drive = { DRIVE_GENERIC, -1, -1, -1 };
  p.xAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.xAxis.leadscrewPitchMm = 2.0f;
  p.xAxis.backlashMm = 0.0f;
  p.xAxis.maxVelocityMmMin = 800.0f;
  p.xAxis.maxAccelMmSec2 = 200.0f;
  p.xAxis.hasDroScale = false;
  p.xAxis.droScale = { 200, 40, 39, false, false, -1 };
  p.xAxis.hasMpg = false;
  p.xAxis.limits = { 0.0f, 100.0f, -1, -1, false };

  // Y Axis (Inactive)
  p.yAxis = {};

  // Spindle (1100W BLDC motor with 38mm bore, 1200 native PPR, 1:1 pulley ratio)
  p.spindle.encoder = { 1200, 14, 13, false, false, -1 };
  p.spindle.pulleyRatio = 1.0f;
  p.spindle.maxSafeRpm = 2500.0f;
  p.spindle.minSyncRpm = 15.0f;
  p.spindle.hasVfdControl = false;
  p.spindle.vfdModbusAddress = 1;
  p.spindle.vfdMaxHz = 60.0f;

  // Geometry
  p.dimensions.bedLengthMm = 400.0f;
  p.dimensions.maxSwingDiameterMm = 210.0f;
  p.dimensions.swingOverCrossSlideMm = 110.0f;
  p.dimensions.maxZTravelMm = 350.0f;
  p.dimensions.maxXTravelMm = 100.0f;
  p.dimensions.headstockLengthMm = 150.0f;
  p.dimensions.chuckDiameterMm = 125.0f;
  p.dimensions.chuckLengthMm = 65.0f;
  p.dimensions.spindleBoreDiameterMm = 38.0f;
  p.dimensions.zZeroFromChuckFaceMm = 10.0f;
  p.dimensions.xZeroFromCenterlineMm = 0.0f;

  return p;
}

bool getDefaultCatalogProfileId(char* outSlug, size_t maxLen) {
  if (!outSlug || maxLen == 0) return false;
  if (!LittleFS.exists("/presets/profiles.json")) {
    strncpy(outSlug, "wm210v_stock", maxLen - 1);
    outSlug[maxLen - 1] = '\0';
    return false;
  }
  File file = LittleFS.open("/presets/profiles.json", "r");
  if (!file) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }
  const char* defId = doc["default_profile_id"] | "wm210v_stock";
  strncpy(outSlug, defId, maxLen - 1);
  outSlug[maxLen - 1] = '\0';
  return true;
}

bool loadProfileFromLittleFS(const char* slug, LatheConfiguration* outProfile) {
  if (!slug || !outProfile) return false;
  if (!LittleFS.exists("/presets/profiles.json")) {
    return false;
  }
  File file = LittleFS.open("/presets/profiles.json", "r");
  if (!file) return false;

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  JsonObject target;
  bool found = false;
  for (JsonObject p : profiles) {
    const char* id = p["identity"]["profile_id"];
    if (id && strcasecmp(id, slug) == 0) {
      target = p;
      found = true;
      break;
    }
  }

  if (!found) {
    return false;
  }

  // Baseline from ROM to ensure all fields have valid defaults
  *outProfile = getDefaultProfile();

  // Populate identity
  const char* pId = target["identity"]["profile_id"] | slug;
  strncpy(outProfile->profileId, pId, sizeof(outProfile->profileId) - 1);
  outProfile->profileId[sizeof(outProfile->profileId) - 1] = '\0';

  const char* mName = target["identity"]["model_name"] | "Custom Lathe";
  strncpy(outProfile->modelName, mName, sizeof(outProfile->modelName) - 1);
  outProfile->modelName[sizeof(outProfile->modelName) - 1] = '\0';

  if (target["identity"]["capabilities"].is<uint32_t>()) {
    outProfile->capabilities = target["identity"]["capabilities"].as<uint32_t>();
  }

  // Dimensions / Envelope
  JsonObject env = target["envelope"];
  if (!env.isNull()) {
    outProfile->dimensions.bedLengthMm = env["bed"]["total_length_mm"] | outProfile->dimensions.bedLengthMm;
    outProfile->dimensions.maxSwingDiameterMm = env["swing"]["over_bed_dia_mm"] | outProfile->dimensions.maxSwingDiameterMm;
    outProfile->dimensions.swingOverCrossSlideMm = env["swing"]["over_cross_slide_dia_mm"] | outProfile->dimensions.swingOverCrossSlideMm;
    outProfile->dimensions.spindleBoreDiameterMm = env["headstock"]["spindle_bore_dia_mm"] | outProfile->dimensions.spindleBoreDiameterMm;
    outProfile->dimensions.chuckDiameterMm = env["headstock"]["chuck"]["diameter_mm"] | outProfile->dimensions.chuckDiameterMm;
    outProfile->dimensions.chuckLengthMm = env["headstock"]["chuck"]["body_length_mm"] | outProfile->dimensions.chuckLengthMm;
    outProfile->dimensions.zZeroFromChuckFaceMm = env["headstock"]["datums"]["z_zero_from_chuck_face_mm"] | outProfile->dimensions.zZeroFromChuckFaceMm;
    outProfile->dimensions.xZeroFromCenterlineMm = env["headstock"]["datums"]["x_zero_from_centerline_mm"] | outProfile->dimensions.xZeroFromCenterlineMm;
  }

  // Spindle
  JsonObject sp = target["spindle"];
  if (!sp.isNull()) {
    outProfile->spindle.maxSafeRpm = sp["kinematics"]["max_safe_rpm"] | outProfile->spindle.maxSafeRpm;
    outProfile->spindle.minSyncRpm = sp["kinematics"]["min_sync_rpm"] | outProfile->spindle.minSyncRpm;
    outProfile->spindle.encoder.ppr = sp["feedback"]["encoder"]["ppr"] | outProfile->spindle.encoder.ppr;
    outProfile->spindle.encoder.hasIndex = sp["feedback"]["encoder"]["has_index"] | outProfile->spindle.encoder.hasIndex;
    outProfile->spindle.pulleyRatio = sp["feedback"]["pulley_ratio"] | outProfile->spindle.pulleyRatio;
  }

  // Axes: Z
  JsonObject zAxis = target["axes"]["z"];
  if (!zAxis.isNull()) {
    outProfile->zAxis.leadscrewPitchMm = zAxis["kinematics"]["leadscrew_pitch_mm"] | outProfile->zAxis.leadscrewPitchMm;
    outProfile->zAxis.backlashMm = zAxis["kinematics"]["backlash_mm"] | outProfile->zAxis.backlashMm;
    outProfile->zAxis.maxVelocityMmMin = zAxis["kinematics"]["max_velocity_mm_min"] | outProfile->zAxis.maxVelocityMmMin;
    outProfile->zAxis.maxAccelMmSec2 = zAxis["kinematics"]["max_accel_mm_sec2"] | outProfile->zAxis.maxAccelMmSec2;
    outProfile->zAxis.stepsPerMm = zAxis["drive"]["steps_per_mm"] | outProfile->zAxis.stepsPerMm;
    outProfile->zAxis.hasDroScale = zAxis["feedback"]["has_dro"] | false;
  }

  // Axes: X
  JsonObject xAxis = target["axes"]["x"];
  if (!xAxis.isNull()) {
    outProfile->xAxis.leadscrewPitchMm = xAxis["kinematics"]["leadscrew_pitch_mm"] | outProfile->xAxis.leadscrewPitchMm;
    outProfile->xAxis.backlashMm = xAxis["kinematics"]["backlash_mm"] | outProfile->xAxis.backlashMm;
    outProfile->xAxis.maxVelocityMmMin = xAxis["kinematics"]["max_velocity_mm_min"] | outProfile->xAxis.maxVelocityMmMin;
    outProfile->xAxis.maxAccelMmSec2 = xAxis["kinematics"]["max_accel_mm_sec2"] | outProfile->xAxis.maxAccelMmSec2;
    outProfile->xAxis.stepsPerMm = xAxis["drive"]["steps_per_mm"] | outProfile->xAxis.stepsPerMm;
    outProfile->xAxis.hasDroScale = xAxis["feedback"]["has_dro"] | false;
  }

  // Ensure capabilities bitmask stays in sync with axes
  if (outProfile->zAxis.hasDroScale) outProfile->capabilities |= CAP_DRO_Z;
  else outProfile->capabilities &= ~CAP_DRO_Z;
  if (outProfile->xAxis.hasDroScale) outProfile->capabilities |= CAP_DRO_X;
  else outProfile->capabilities &= ~CAP_DRO_X;

  return true;
}

bool listCatalogProfiles(String* outList) {
  if (!outList) return false;
  if (!LittleFS.exists("/presets/profiles.json")) {
    *outList = "wm210v_stock";
    return true;
  }
  File file = LittleFS.open("/presets/profiles.json", "r");
  if (!file) {
    *outList = "wm210v_stock";
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    *outList = "wm210v_stock";
    return false;
  }

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  String list = "";
  for (size_t i = 0; i < profiles.size(); i++) {
    const char* id = profiles[i]["identity"]["profile_id"];
    if (id) {
      if (list.length() > 0) list += ",";
      list += id;
    }
  }
  *outList = list;
  return true;
}

const LatheConfiguration* findPresetBySlug(const char* slug) {
  if (!slug || *slug == '\0') return nullptr;
  static LatheConfiguration cached;
  if (loadProfileFromLittleFS(slug, &cached)) {
    return &cached;
  }
  if (strcasecmp(slug, "wm210v_8x16") == 0 ||
      strcasecmp(slug, "wm210v_dual") == 0 ||
      strcasecmp(slug, "wm210v_stock") == 0 ||
      strcasecmp(slug, "wm210v") == 0 ||
      strcasecmp(slug, "8x16") == 0 ||
      strcasecmp(slug, "default") == 0) {
    cached = getDefaultProfile();
    return &cached;
  }
  return nullptr;
}

const char* getCapabilityName(LatheCapabilityFlags flag) {
  switch (flag) {
    case CAP_SPINDLE_ENCODER:  return "SPINDLE_ENCODER";
    case CAP_SPINDLE_INDEX:    return "SPINDLE_INDEX";
    case CAP_SPINDLE_VFD:      return "SPINDLE_VFD";
    case CAP_MOTOR_Z:          return "MOTOR_Z";
    case CAP_MOTOR_X:          return "MOTOR_X";
    case CAP_MOTOR_Y:          return "MOTOR_Y";
    case CAP_DRO_Z:            return "DRO_Z";
    case CAP_DRO_X:            return "DRO_X";
    case CAP_DRO_Y:            return "DRO_Y";
    case CAP_LIMIT_SWITCH_Z:   return "LIMIT_SWITCH_Z";
    case CAP_LIMIT_SWITCH_X:   return "LIMIT_SWITCH_X";
    case CAP_LIMIT_SENSORLESS: return "LIMIT_SENSORLESS";
    case CAP_MPG_Z:            return "MPG_Z";
    case CAP_MPG_X:            return "MPG_X";
    case CAP_JOYSTICK:         return "JOYSTICK";
    case CAP_TAILSTOCK:        return "TAILSTOCK";
    case CAP_DRIVE_ALARM_Z:    return "DRIVE_ALARM_Z";
    case CAP_DRIVE_ALARM_X:    return "DRIVE_ALARM_X";
    default:                   return "NONE";
  }
}
