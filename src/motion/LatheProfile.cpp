#include "LatheProfile.h"
#include "SystemProfilesJson.h"
#include "motion/LittleFsProfileStorage.h"
#include <string.h>
#include <strings.h>
#include <string>
#include <ArduinoJson.h>

static constexpr const char* kProfileCatalogPath = "/presets/profiles.json";
static whatels::motion::IProfileStorage* s_profileStorage = nullptr;

void setProfileStorage(whatels::motion::IProfileStorage* storage) {
  s_profileStorage = storage;
}

whatels::motion::IProfileStorage* getProfileStorage() {
#if defined(ARDUINO) || defined(ESP32)
  if (!s_profileStorage) {
    static whatels::motion::LittleFsProfileStorage s_defaultLittleFsStorage;
    s_profileStorage = &s_defaultLittleFsStorage;
  }
#endif
  return s_profileStorage;
}

static bool readCatalogDoc(JsonDocument& doc) {
  whatels::motion::IProfileStorage* storage = getProfileStorage();
  if (!storage || !storage->exists(kProfileCatalogPath)) return false;
  std::string content;
  if (!storage->readFile(kProfileCatalogPath, content)) return false;
  return deserializeJson(doc, content) == DeserializationError::Ok;
}

static bool writeCatalogDoc(const JsonDocument& doc) {
  whatels::motion::IProfileStorage* storage = getProfileStorage();
  if (!storage) return false;
  std::string content;
  serializeJson(doc, content);
  return storage->writeFile(kProfileCatalogPath, content);
}

LatheConfiguration getDefaultProfile() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "wm210v_8x16", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "WM210V 8x16 Bench Lathe", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_MOTOR_X;

  // Z Axis (Carriage Leadscrew Motorized)
  p.zAxis.hasMotor = true;
  p.zAxis.drive = { DRIVE_GENERIC, -1, -1, -1, false, false, false };
  p.zAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.zAxis.leadscrewPitchMm = 2.0f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 1500.0f;
  p.zAxis.maxAccelMmSec2 = 250.0f;
  p.zAxis.startVelocityMmMin = 120.0f;
  p.zAxis.hasDroScale = false;
  p.zAxis.droScale = { 200, 9, 12, false, false, -1 };
  p.zAxis.hasMpg = false;
  p.zAxis.limits = { 0.0f, 350.0f, -1, -1, false };

  // X Axis (Cross-Slide Leadscrew Motorized)
  p.xAxis.hasMotor = true;
  p.xAxis.drive = { DRIVE_GENERIC, -1, -1, -1, false, false, false };
  p.xAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.xAxis.leadscrewPitchMm = 2.0f;
  p.xAxis.backlashMm = 0.0f;
  p.xAxis.maxVelocityMmMin = 800.0f;
  p.xAxis.maxAccelMmSec2 = 200.0f;
  p.xAxis.startVelocityMmMin = 120.0f;
  p.xAxis.hasDroScale = false;
  p.xAxis.droScale = { 200, 40, 39, false, false, -1 };
  p.xAxis.hasMpg = false;
  p.xAxis.limits = { 0.0f, 100.0f, -1, -1, false };

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

  p.isSystemProfile = true;
  return p;
}

static bool parseProfileObject(JsonObject target, const char* slug, LatheConfiguration* outProfile) {
  if (target.isNull() || !outProfile) return false;

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
  outProfile->isSystemProfile = target["identity"]["system"] | false;

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
    outProfile->zAxis.startVelocityMmMin = zAxis["kinematics"]["start_velocity_mm_min"] | outProfile->zAxis.startVelocityMmMin;
    outProfile->zAxis.stepsPerMm = zAxis["drive"]["steps_per_mm"] | outProfile->zAxis.stepsPerMm;
    outProfile->zAxis.drive.invertDir = zAxis["drive"]["invert_dir"] | outProfile->zAxis.drive.invertDir;
    outProfile->zAxis.drive.invertEnable = zAxis["drive"]["invert_enable"] | outProfile->zAxis.drive.invertEnable;
    outProfile->zAxis.drive.needsRest = zAxis["drive"]["needs_rest"] | outProfile->zAxis.drive.needsRest;
    outProfile->zAxis.hasDroScale = zAxis["feedback"]["has_dro"] | false;
  }

  // Axes: X
  JsonObject xAxis = target["axes"]["x"];
  if (!xAxis.isNull()) {
    outProfile->xAxis.leadscrewPitchMm = xAxis["kinematics"]["leadscrew_pitch_mm"] | outProfile->xAxis.leadscrewPitchMm;
    outProfile->xAxis.backlashMm = xAxis["kinematics"]["backlash_mm"] | outProfile->xAxis.backlashMm;
    outProfile->xAxis.maxVelocityMmMin = xAxis["kinematics"]["max_velocity_mm_min"] | outProfile->xAxis.maxVelocityMmMin;
    outProfile->xAxis.maxAccelMmSec2 = xAxis["kinematics"]["max_accel_mm_sec2"] | outProfile->xAxis.maxAccelMmSec2;
    outProfile->xAxis.startVelocityMmMin = xAxis["kinematics"]["start_velocity_mm_min"] | outProfile->xAxis.startVelocityMmMin;
    outProfile->xAxis.stepsPerMm = xAxis["drive"]["steps_per_mm"] | outProfile->xAxis.stepsPerMm;
    outProfile->xAxis.drive.invertDir = xAxis["drive"]["invert_dir"] | outProfile->xAxis.drive.invertDir;
    outProfile->xAxis.drive.invertEnable = xAxis["drive"]["invert_enable"] | outProfile->xAxis.drive.invertEnable;
    outProfile->xAxis.drive.needsRest = xAxis["drive"]["needs_rest"] | outProfile->xAxis.drive.needsRest;
    outProfile->xAxis.hasDroScale = xAxis["feedback"]["has_dro"] | false;
  }

  // Ensure capabilities bitmask stays in sync with axes
  if (outProfile->zAxis.hasDroScale) outProfile->capabilities |= CAP_DRO_Z;
  else outProfile->capabilities &= ~CAP_DRO_Z;
  if (outProfile->xAxis.hasDroScale) outProfile->capabilities |= CAP_DRO_X;
  else outProfile->capabilities &= ~CAP_DRO_X;

  return true;
}

bool getDefaultCatalogProfileId(char* outSlug, size_t maxLen) {
  if (!outSlug || maxLen == 0) return false;

  // 1. Check profile storage catalog first (operator customization)
  JsonDocument doc;
  if (readCatalogDoc(doc)) {
    const char* defId = doc["default_profile_id"];
    if (defId && *defId != '\0') {
      strncpy(outSlug, defId, maxLen - 1);
      outSlug[maxLen - 1] = '\0';
      return true;
    }
  }

  // 2. Fallback to compiled-in ROM system profiles
  JsonDocument romDoc;
  if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
    const char* defId = romDoc["default_profile_id"] | "wm210v_stock";
    strncpy(outSlug, defId, maxLen - 1);
    outSlug[maxLen - 1] = '\0';
    return true;
  }

  strncpy(outSlug, "wm210v_stock", maxLen - 1);
  outSlug[maxLen - 1] = '\0';
  return true;
}

bool setDefaultCatalogProfileId(const char* slug) {
  if (!slug || *slug == '\0') return false;

  JsonDocument doc;
  if (!readCatalogDoc(doc)) {
    // Seed doc from ROM system profiles
    deserializeJson(doc, SYSTEM_PROFILES_JSON);
  }

  // Validate that slug exists in either storage catalog or ROM
  bool exists = false;
  JsonArray profiles = doc["profiles"].as<JsonArray>();
  for (JsonObject p : profiles) {
    const char* id = p["identity"]["profile_id"];
    if (id && strcasecmp(id, slug) == 0) {
      exists = true;
      break;
    }
  }
  if (!exists) {
    // Also check ROM catalog
    JsonDocument romDoc;
    if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
      JsonArray romProfiles = romDoc["profiles"].as<JsonArray>();
      for (JsonObject p : romProfiles) {
        const char* id = p["identity"]["profile_id"];
        if (id && strcasecmp(id, slug) == 0) {
          exists = true;
          break;
        }
      }
    }
  }
  if (!exists) return false;

  doc["default_profile_id"] = slug;
  return writeCatalogDoc(doc);
}

bool loadProfileFromLittleFS(const char* slug, LatheConfiguration* outProfile) {
  if (!slug || !outProfile) return false;

  // 1. Check profile storage user catalog first
  JsonDocument doc;
  if (readCatalogDoc(doc)) {
    JsonArray profiles = doc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      if (id && strcasecmp(id, slug) == 0) {
        return parseProfileObject(p, slug, outProfile);
      }
    }
  }

  // 2. Check compiled-in ROM system profiles
  JsonDocument romDoc;
  if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
    JsonArray profiles = romDoc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      if (id && strcasecmp(id, slug) == 0) {
        return parseProfileObject(p, slug, outProfile);
      }
    }
  }

  return false;
}

bool listCatalogProfiles(String* outList) {
  if (!outList) return false;
  std::string list;

  // 1. Always enumerate compiled-in ROM system templates first
  JsonDocument romDoc;
  if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
    JsonArray romProfiles = romDoc["profiles"].as<JsonArray>();
    for (JsonObject p : romProfiles) {
      const char* id = p["identity"]["profile_id"];
      if (id) {
        if (!list.empty()) list += ",";
        list += id;
        list += ":sys";
      }
    }
  }

  // 2. Enumerate user presets from profile storage (skipping any that duplicate system names)
  JsonDocument doc;
  if (readCatalogDoc(doc)) {
    JsonArray profiles = doc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      bool isSys = p["identity"]["system"] | false;
      if (id && !isSys) {
        std::string token = std::string(id) + ":";
        if (list.find(token) == std::string::npos) {
          if (!list.empty()) list += ",";
          list += id;
          list += ":user";
        }
      }
    }
  }

  if (list.empty()) {
    list = "wm210v_stock:sys";
  }
  *outList = list.c_str();
  return true;
}

bool isSystemCatalogProfile(const char* slug) {
  if (!slug || *slug == '\0') return false;

  // Well-known fallback aliases
  if (strcasecmp(slug, "wm210v_8x16") == 0 ||
      strcasecmp(slug, "wm210v_stock") == 0 ||
      strcasecmp(slug, "wm210v_dual") == 0 ||
      strcasecmp(slug, "mini_7x14") == 0 ||
      strcasecmp(slug, "default") == 0) {
    return true;
  }

  // Check compiled-in ROM catalog
  JsonDocument romDoc;
  if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
    JsonArray profiles = romDoc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      if (id && strcasecmp(id, slug) == 0) {
        return p["identity"]["system"] | true;
      }
    }
  }

  return false;
}

bool getProfileJsonFromLittleFS(const char* slug, String* outJson) {
  if (!slug || !outJson) return false;

  // 1. Check profile storage user catalog first
  JsonDocument doc;
  if (readCatalogDoc(doc)) {
    JsonArray profiles = doc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      if (id && strcasecmp(id, slug) == 0) {
        std::string jsonStr;
        serializeJson(p, jsonStr);
        *outJson = jsonStr.c_str();
        return true;
      }
    }
  }

  // 2. Check compiled-in ROM system profiles
  JsonDocument romDoc;
  if (!deserializeJson(romDoc, SYSTEM_PROFILES_JSON)) {
    JsonArray profiles = romDoc["profiles"].as<JsonArray>();
    for (JsonObject p : profiles) {
      const char* id = p["identity"]["profile_id"];
      if (id && strcasecmp(id, slug) == 0) {
        std::string jsonStr;
        serializeJson(p, jsonStr);
        *outJson = jsonStr.c_str();
        return true;
      }
    }
  }

  return false;
}

bool saveProfileToLittleFS(const LatheConfiguration& profile) {
  if (profile.profileId[0] == '\0') return false;
  // Guard: Never allow overwriting a system template!
  if (isSystemCatalogProfile(profile.profileId)) return false;

  JsonDocument doc;
  readCatalogDoc(doc);

  if (!doc["version"].is<int>()) doc["version"] = 1;
  if (!doc["profiles"].is<JsonArray>()) doc["profiles"].to<JsonArray>();

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  JsonObject target;
  for (JsonObject p : profiles) {
    const char* id = p["identity"]["profile_id"];
    if (id && strcasecmp(id, profile.profileId) == 0) {
      target = p;
      break;
    }
  }
  if (target.isNull()) {
    target = profiles.add<JsonObject>();
  }

  JsonObject ident = target["identity"].to<JsonObject>();
  ident["profile_id"] = profile.profileId;
  ident["model_name"] = profile.modelName;
  ident["capabilities"] = profile.capabilities;
  ident["system"] = false;

  JsonObject env = target["envelope"].to<JsonObject>();
  env["bed"]["total_length_mm"] = profile.dimensions.bedLengthMm;
  env["swing"]["over_bed_dia_mm"] = profile.dimensions.maxSwingDiameterMm;
  env["swing"]["over_cross_slide_dia_mm"] = profile.dimensions.swingOverCrossSlideMm;
  env["headstock"]["spindle_bore_dia_mm"] = profile.dimensions.spindleBoreDiameterMm;
  env["headstock"]["chuck"]["diameter_mm"] = profile.dimensions.chuckDiameterMm;
  env["headstock"]["chuck"]["body_length_mm"] = profile.dimensions.chuckLengthMm;
  env["headstock"]["datums"]["z_zero_from_chuck_face_mm"] = profile.dimensions.zZeroFromChuckFaceMm;
  env["headstock"]["datums"]["x_zero_from_centerline_mm"] = profile.dimensions.xZeroFromCenterlineMm;

  JsonObject sp = target["spindle"].to<JsonObject>();
  sp["kinematics"]["max_safe_rpm"] = profile.spindle.maxSafeRpm;
  sp["kinematics"]["min_sync_rpm"] = profile.spindle.minSyncRpm;
  sp["feedback"]["encoder"]["ppr"] = profile.spindle.encoder.ppr;
  sp["feedback"]["encoder"]["has_index"] = profile.spindle.encoder.hasIndex;
  sp["feedback"]["pulley_ratio"] = profile.spindle.pulleyRatio;

  JsonObject zA = target["axes"]["z"].to<JsonObject>();
  zA["kinematics"]["leadscrew_pitch_mm"] = profile.zAxis.leadscrewPitchMm;
  zA["kinematics"]["backlash_mm"] = profile.zAxis.backlashMm;
  zA["kinematics"]["max_velocity_mm_min"] = profile.zAxis.maxVelocityMmMin;
  zA["kinematics"]["max_accel_mm_sec2"] = profile.zAxis.maxAccelMmSec2;
  zA["kinematics"]["start_velocity_mm_min"] = profile.zAxis.startVelocityMmMin;
  zA["drive"]["steps_per_mm"] = profile.zAxis.stepsPerMm;
  zA["drive"]["invert_dir"] = profile.zAxis.drive.invertDir;
  zA["drive"]["invert_enable"] = profile.zAxis.drive.invertEnable;
  zA["drive"]["needs_rest"] = profile.zAxis.drive.needsRest;
  zA["feedback"]["has_dro"] = profile.zAxis.hasDroScale;

  JsonObject xA = target["axes"]["x"].to<JsonObject>();
  xA["kinematics"]["leadscrew_pitch_mm"] = profile.xAxis.leadscrewPitchMm;
  xA["kinematics"]["backlash_mm"] = profile.xAxis.backlashMm;
  xA["kinematics"]["max_velocity_mm_min"] = profile.xAxis.maxVelocityMmMin;
  xA["kinematics"]["max_accel_mm_sec2"] = profile.xAxis.maxAccelMmSec2;
  xA["kinematics"]["start_velocity_mm_min"] = profile.xAxis.startVelocityMmMin;
  xA["drive"]["steps_per_mm"] = profile.xAxis.stepsPerMm;
  xA["drive"]["invert_dir"] = profile.xAxis.drive.invertDir;
  xA["drive"]["invert_enable"] = profile.xAxis.drive.invertEnable;
  xA["drive"]["needs_rest"] = profile.xAxis.drive.needsRest;
  xA["feedback"]["has_dro"] = profile.xAxis.hasDroScale;

  return writeCatalogDoc(doc);
}

bool deleteProfileFromLittleFS(const char* slug) {
  if (!slug || *slug == '\0') return false;
  // Guard: Never allow deleting a system template!
  if (isSystemCatalogProfile(slug)) return false;
  // Guard: Never allow deleting the catalog default profile!
  char defaultSlug[32] = {0};
  if (getDefaultCatalogProfileId(defaultSlug, sizeof(defaultSlug))) {
    if (strcasecmp(defaultSlug, slug) == 0) return false;
  }

  JsonDocument doc;
  if (!readCatalogDoc(doc)) return false;

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  bool found = false;
  for (size_t i = 0; i < profiles.size(); i++) {
    const char* id = profiles[i]["identity"]["profile_id"];
    if (id && strcasecmp(id, slug) == 0) {
      profiles.remove(i);
      found = true;
      break;
    }
  }
  if (!found) return false;

  return writeCatalogDoc(doc);
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
    case CAP_DRO_Z:            return "DRO_Z";
    case CAP_DRO_X:            return "DRO_X";
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
