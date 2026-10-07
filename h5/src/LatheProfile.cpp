#include "LatheProfile.h"
#include <string.h>

LatheConfiguration getPresetMini7x14() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "mini_7x14", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "7x14 Mini Lathe (Z Motor Only)", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_DRO_X;

  // Z Axis (Carriage Motorized)
  p.zAxis.hasMotor = true;
  p.zAxis.hasDroScale = false;
  p.zAxis.hasMpg = false;
  p.zAxis.stepsPerMm = 533.33f; // e.g. 800 steps / 1.5mm pitch
  p.zAxis.leadscrewPitchMm = 1.5f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 1000.0f;
  p.zAxis.maxAccelMmSec2 = 150.0f;
  p.zAxis.travelMinMm = 0.0f;
  p.zAxis.travelMaxMm = 300.0f;

  // X Axis (Manual Cross-Slide with DRO)
  p.xAxis.hasMotor = false;
  p.xAxis.hasDroScale = true;
  p.xAxis.hasMpg = false;
  p.xAxis.stepsPerMm = 0.0f;
  p.xAxis.leadscrewPitchMm = 1.0f;
  p.xAxis.backlashMm = 0.0f;
  p.xAxis.maxVelocityMmMin = 0.0f;
  p.xAxis.maxAccelMmSec2 = 0.0f;
  p.xAxis.travelMinMm = 0.0f;
  p.xAxis.travelMaxMm = 65.0f;

  // Y Axis (Inactive)
  p.yAxis = {};

  // Spindle
  p.spindle.encoderPpr = 1024;
  p.spindle.hasIndexPulse = false;
  p.spindle.maxSafeRpm = 2500.0f;
  p.spindle.minSyncRpm = 15.0f;
  p.spindle.hasVfdControl = false;
  p.spindle.vfdModbusAddress = 1;
  p.spindle.vfdMaxHz = 60.0f;

  // Geometry
  p.dimensions.bedLengthMm = 350.0f;
  p.dimensions.maxSwingDiameterMm = 180.0f;
  p.dimensions.swingOverCrossSlideMm = 90.0f;
  p.dimensions.maxZTravelMm = 300.0f;
  p.dimensions.maxXTravelMm = 65.0f;
  p.dimensions.headstockLengthMm = 130.0f;
  p.dimensions.chuckDiameterMm = 80.0f;
  p.dimensions.chuckLengthMm = 50.0f;
  p.dimensions.spindleBoreDiameterMm = 20.0f;
  p.dimensions.zZeroFromChuckFaceMm = 10.0f;
  p.dimensions.xZeroFromCenterlineMm = 0.0f;

  return p;
}

LatheConfiguration getPresetWM210V() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "wm210v_dual", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "WM210V 8x16 (Dual Motor)", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_MOTOR_X | CAP_DRO_Z | CAP_DRO_X;

  // Z Axis (Carriage Leadscrew Motorized)
  p.zAxis.hasMotor = true;
  p.zAxis.hasDroScale = true;
  p.zAxis.hasMpg = false;
  p.zAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.zAxis.leadscrewPitchMm = 2.0f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 1500.0f;
  p.zAxis.maxAccelMmSec2 = 250.0f;
  p.zAxis.travelMinMm = 0.0f;
  p.zAxis.travelMaxMm = 350.0f;

  // X Axis (Cross-Slide Leadscrew Motorized)
  p.xAxis.hasMotor = true;
  p.xAxis.hasDroScale = true;
  p.xAxis.hasMpg = false;
  p.xAxis.stepsPerMm = 400.0f; // 800 motor steps / 2.0mm pitch
  p.xAxis.leadscrewPitchMm = 2.0f;
  p.xAxis.backlashMm = 0.0f;
  p.xAxis.maxVelocityMmMin = 800.0f;
  p.xAxis.maxAccelMmSec2 = 200.0f;
  p.xAxis.travelMinMm = 0.0f;
  p.xAxis.travelMaxMm = 100.0f;

  // Y Axis (Inactive)
  p.yAxis = {};

  // Spindle (1100W BLDC motor with 38mm bore)
  p.spindle.encoderPpr = 1200;
  p.spindle.hasIndexPulse = false;
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

LatheConfiguration getPresetEngine1440() {
  LatheConfiguration p = {};
  strncpy(p.profileId, "engine_1440", sizeof(p.profileId) - 1);
  strncpy(p.modelName, "14x40 Toolroom Lathe (Heavy Duty)", sizeof(p.modelName) - 1);
  p.capabilities = CAP_SPINDLE_ENCODER | CAP_SPINDLE_INDEX | CAP_SPINDLE_VFD |
                   CAP_MOTOR_Z | CAP_MOTOR_X |
                   CAP_DRO_Z | CAP_DRO_X |
                   CAP_LIMIT_SWITCH_Z | CAP_LIMIT_SWITCH_X |
                   CAP_MPG_Z | CAP_MPG_X;

  // Z Axis
  p.zAxis.hasMotor = true;
  p.zAxis.hasDroScale = true;
  p.zAxis.hasMpg = true;
  p.zAxis.stepsPerMm = 400.0f;
  p.zAxis.leadscrewPitchMm = 4.0f;
  p.zAxis.backlashMm = 0.0f;
  p.zAxis.maxVelocityMmMin = 2500.0f;
  p.zAxis.maxAccelMmSec2 = 400.0f;
  p.zAxis.travelMinMm = 0.0f;
  p.zAxis.travelMaxMm = 900.0f;

  // X Axis
  p.xAxis.hasMotor = true;
  p.xAxis.hasDroScale = true;
  p.xAxis.hasMpg = true;
  p.xAxis.stepsPerMm = 400.0f;
  p.xAxis.leadscrewPitchMm = 2.5f;
  p.xAxis.backlashMm = 0.0f;
  p.xAxis.maxVelocityMmMin = 1200.0f;
  p.xAxis.maxAccelMmSec2 = 300.0f;
  p.xAxis.travelMinMm = 0.0f;
  p.xAxis.travelMaxMm = 180.0f;

  // Y Axis
  p.yAxis = {};

  // Spindle
  p.spindle.encoderPpr = 2400;
  p.spindle.hasIndexPulse = true;
  p.spindle.maxSafeRpm = 2000.0f;
  p.spindle.minSyncRpm = 10.0f;
  p.spindle.hasVfdControl = true;
  p.spindle.vfdModbusAddress = 1;
  p.spindle.vfdMaxHz = 60.0f;

  // Geometry
  p.dimensions.bedLengthMm = 1200.0f;
  p.dimensions.maxSwingDiameterMm = 360.0f;
  p.dimensions.swingOverCrossSlideMm = 220.0f;
  p.dimensions.maxZTravelMm = 900.0f;
  p.dimensions.maxXTravelMm = 180.0f;
  p.dimensions.headstockLengthMm = 300.0f;
  p.dimensions.chuckDiameterMm = 200.0f;
  p.dimensions.chuckLengthMm = 95.0f;
  p.dimensions.spindleBoreDiameterMm = 40.0f;
  p.dimensions.zZeroFromChuckFaceMm = 15.0f;
  p.dimensions.xZeroFromCenterlineMm = 0.0f;

  return p;
}

LatheConfiguration getDefaultProfile() {
  return getPresetWM210V();
}

const LatheConfiguration* findPresetBySlug(const char* slug) {
  if (!slug) return nullptr;
  static LatheConfiguration cached;
  if (strcasecmp(slug, "mini_7x14") == 0 || strcasecmp(slug, "7x14") == 0) {
    cached = getPresetMini7x14();
    return &cached;
  }
  if (strcasecmp(slug, "wm210v_dual") == 0 || strcasecmp(slug, "wm210v") == 0 || strcasecmp(slug, "8x16") == 0) {
    cached = getPresetWM210V();
    return &cached;
  }
  if (strcasecmp(slug, "engine_1440") == 0 || strcasecmp(slug, "1440") == 0) {
    cached = getPresetEngine1440();
    return &cached;
  }
  return nullptr;
}

const char* getCapabilityName(LatheCapabilityFlags flag) {
  switch (flag) {
    case CAP_SPINDLE_ENCODER: return "SPINDLE_ENCODER";
    case CAP_SPINDLE_INDEX:   return "SPINDLE_INDEX";
    case CAP_SPINDLE_VFD:     return "SPINDLE_VFD";
    case CAP_MOTOR_Z:         return "MOTOR_Z";
    case CAP_MOTOR_X:         return "MOTOR_X";
    case CAP_MOTOR_Y:         return "MOTOR_Y";
    case CAP_DRO_Z:           return "DRO_Z";
    case CAP_DRO_X:           return "DRO_X";
    case CAP_DRO_Y:           return "DRO_Y";
    case CAP_LIMIT_SWITCH_Z:  return "LIMIT_SWITCH_Z";
    case CAP_LIMIT_SWITCH_X:  return "LIMIT_SWITCH_X";
    case CAP_MPG_Z:           return "MPG_Z";
    case CAP_MPG_X:           return "MPG_X";
    case CAP_JOYSTICK:        return "JOYSTICK";
    case CAP_TAILSTOCK:       return "TAILSTOCK";
    default:                  return "NONE";
  }
}
