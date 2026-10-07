#pragma once

#include <Arduino.h>

/**
 * @file LatheProfile.h
 * @brief Authoritative Machine Profile and Hardware Capabilities specification for NanoELS.
 * @details Implements the Lathe Machine Profile architecture defined in LATHE_MACHINE_PROFILE.md.
 */

// -----------------------------------------------------------------------------
// 1. Hardware Capability Bitmask (LatheCapabilityFlags)
// -----------------------------------------------------------------------------

enum LatheCapabilityFlags : uint32_t {
  CAP_NONE            = 0,

  // Spindle Capabilities (Bits 0-2)
  CAP_SPINDLE_ENCODER = 1 << 0,  // Quadrature optical/magnetic pulse encoder fitted to spindle
  CAP_SPINDLE_INDEX   = 1 << 1,  // Single-pulse-per-revolution optical index switch
  CAP_SPINDLE_VFD     = 1 << 2,  // Spindle VFD speed & direction control (Modbus / 0-10V)

  // Axis Stepper/Servo Drivers (Bits 3-5)
  CAP_MOTOR_Z         = 1 << 3,  // Z carriage leadscrew stepper/servo motor installed
  CAP_MOTOR_X         = 1 << 4,  // X cross-slide leadscrew stepper/servo motor installed
  CAP_MOTOR_Y         = 1 << 5,  // Y auxiliary/milling spindle axis installed

  // Physical Linear DRO Scales (Bits 6-8)
  CAP_DRO_Z           = 1 << 6,  // Optical/magnetic linear scale attached to Z carriage
  CAP_DRO_X           = 1 << 7,  // Optical/magnetic linear scale attached to X cross-slide
  CAP_DRO_Y           = 1 << 8,  // Optical/magnetic linear scale attached to Y axis

  // Hardware Endstops & Limit Switches (Bits 9-10)
  CAP_LIMIT_SWITCH_Z  = 1 << 9,  // Hardware microswitch / opto limit switch on Z
  CAP_LIMIT_SWITCH_X  = 1 << 10, // Hardware microswitch / opto limit switch on X

  // Operator Input Controls (Bits 11-14)
  CAP_MPG_Z           = 1 << 11, // Dedicated Manual Pulse Generator handwheel on Z
  CAP_MPG_X           = 1 << 12, // Dedicated Manual Pulse Generator handwheel on X
  CAP_JOYSTICK        = 1 << 13, // 2-axis or 4-axis analog/contact joystick
  CAP_TAILSTOCK       = 1 << 14  // Tailstock barrel linear sensor / DRO fitted
};

// -----------------------------------------------------------------------------
// 2. Component Configuration Structures
// -----------------------------------------------------------------------------

struct AxisHardwareConfig {
  bool  hasMotor;              // True if axis is driven by stepper/servo
  bool  hasDroScale;           // True if physical glass/magnetic scale is installed
  bool  hasMpg;                // True if dedicated MPG handwheel is fitted

  // Kinematic Step Math
  float stepsPerMm;            // Motor driver pulses per millimeter of linear travel
  float leadscrewPitchMm;      // Physical pitch of screw (e.g. 2.0 mm, 4.0 mm, 10 TPI)
  float backlashMm;            // Measured screw backlash for backlash compensation

  // Dynamic Limits
  float maxVelocityMmMin;      // Rapid traverse speed ceiling (e.g. 1500 mm/min)
  float maxAccelMmSec2;        // Acceleration ramp limit (e.g. 250 mm/sec^2)

  // Physical Travel Boundaries (relative to machine datum)
  float travelMinMm;           // Extreme physical limit toward headstock / center
  float travelMaxMm;           // Extreme physical limit toward bed end / slide back

  // Canonical deci-micron helper
  long leadscrewPitchDu() const {
    return (long)round(leadscrewPitchMm * 10000.0f);
  }
  long backlashDu() const {
    return (long)round(backlashMm * 10000.0f);
  }
};

struct SpindleHardwareConfig {
  uint16_t encoderPpr;         // Encoder pulses per revolution (e.g. 1024, 1200, 2400)
  bool     hasIndexPulse;      // True if single-pulse index sensor installed
  float    maxSafeRpm;         // Headstock mechanical safety ceiling (e.g. 2500 RPM)
  float    minSyncRpm;         // Minimum reliable encoder tracking RPM (typically 15 RPM)

  // VFD / BLDC Motor Control (Optional)
  bool     hasVfdControl;      // True if controller commands spindle start/stop/speed
  uint8_t  vfdModbusAddress;   // RS-485 Modbus RTU address
  float    vfdMaxHz;           // Maximum inverter frequency (e.g. 60 Hz or 400 Hz)
};

struct LatheDimensions {
  float bedLengthMm;           // Total bed length from headstock face to end of ways
  float maxSwingDiameterMm;    // Maximum workpiece diameter clear of bed ways
  float swingOverCrossSlideMm; // Maximum workpiece diameter clear of cross-slide
  float maxZTravelMm;          // Usable carriage longitudinal travel
  float maxXTravelMm;          // Usable cross-slide transverse travel

  // Headstock & Chuck Dimensions
  float headstockLengthMm;     // Headstock casting length
  float chuckDiameterMm;       // Outside diameter of chuck body
  float chuckLengthMm;         // Longitudinal length of chuck body
  float spindleBoreDiameterMm; // Through-hole bore in spindle

  // Reference Offsets for Visualizer & Work Setup
  float zZeroFromChuckFaceMm;  // Default clearance offset from chuck face to Z=0
  float xZeroFromCenterlineMm; // Default offset from spindle centerline
};

// -----------------------------------------------------------------------------
// 3. Complete Lathe Profile Definition
// -----------------------------------------------------------------------------

struct LatheConfiguration {
  char                  profileId[24];   // Machine slug, e.g. "wm210v_dual"
  char                  modelName[48];   // Human readable label, e.g. "WM210V 8x16 Bench Lathe"
  uint32_t              capabilities;    // Bitmask of LatheCapabilityFlags

  AxisHardwareConfig    zAxis;           // Carriage (Z)
  AxisHardwareConfig    xAxis;           // Cross-Slide (X)
  AxisHardwareConfig    yAxis;           // Auxiliary axis (Y)

  SpindleHardwareConfig spindle;         // Spindle encoder & VFD
  LatheDimensions       dimensions;      // Bed, chuck, and swing geometry

  // Convenience helper
  bool hasCapability(LatheCapabilityFlags flag) const {
    return (capabilities & (uint32_t)flag) != 0;
  }
};

// -----------------------------------------------------------------------------
// 4. Standard Lathe Preset Library API
// -----------------------------------------------------------------------------

LatheConfiguration getPresetMini7x14();
LatheConfiguration getPresetWM210V();
LatheConfiguration getPresetEngine1440();
LatheConfiguration getDefaultProfile();

const LatheConfiguration* findPresetBySlug(const char* slug);
const char* getCapabilityName(LatheCapabilityFlags flag);
