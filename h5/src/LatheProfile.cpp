#include "LatheProfile.h"
#include <string.h>

LatheConfiguration getDefaultProfile() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "wm210v_8x16", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "WM210V 8x16 Bench Lathe", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_MOTOR_X | CAP_DRO_Z | CAP_DRO_X;

  // Z Axis (Carriage Leadscrew Motorized)
  p.zAxis.hasMotor = true;
  p.zAxis.drive = { DRIVE_GENERIC, -1, -1, -1 };
  p.zAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.zAxis.leadscrewPitchMm = 2.0f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 1500.0f;
  p.zAxis.maxAccelMmSec2 = 250.0f;
  p.zAxis.hasDroScale = true;
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
  p.xAxis.hasDroScale = true;
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

const LatheConfiguration* findPresetBySlug(const char* slug) {
  if (!slug || *slug == '\0') return nullptr;
  static LatheConfiguration cached;
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
