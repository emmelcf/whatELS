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
  CAP_NONE             = 0,

  // Spindle Capabilities (Bits 0-2)
  CAP_SPINDLE_ENCODER  = 1 << 0,  // Pulse encoder fitted to spindle (shaft or servo pass-through)
  CAP_SPINDLE_INDEX    = 1 << 1,  // Single-pulse-per-revolution index sensor (Z-channel / optical)
  CAP_SPINDLE_VFD      = 1 << 2,  // Spindle VFD / servo speed & direction control (Modbus / 0-10V)

  // Axis Stepper/Servo Drivers (Bits 3-4)
  CAP_MOTOR_Z          = 1 << 3,  // Z carriage leadscrew drive installed
  CAP_MOTOR_X          = 1 << 4,  // X cross-slide leadscrew drive installed

  // Physical Linear DRO Scales (Bits 6-7)
  CAP_DRO_Z            = 1 << 6,  // Optical linear scale attached to Z carriage
  CAP_DRO_X            = 1 << 7,  // Optical linear scale attached to X cross-slide

  // Hardware Endstops & Limits (Bits 9-11)
  CAP_LIMIT_SWITCH_Z   = 1 << 9,  // Physical microswitches / inductive limits on Z
  CAP_LIMIT_SWITCH_X   = 1 << 10, // Physical microswitches / inductive limits on X
  CAP_LIMIT_SENSORLESS = 1 << 11, // TMC StallGuard or Servo torque-sensing limits

  // Operator Controls & Inputs (Bits 12-15)
  CAP_MPG_Z            = 1 << 12, // Dedicated Manual Pulse Generator handwheel on Z
  CAP_MPG_X            = 1 << 13, // Dedicated Manual Pulse Generator handwheel on X
  CAP_JOYSTICK         = 1 << 14, // 2-axis or 4-axis analog/contact joystick
  CAP_TAILSTOCK        = 1 << 15, // Tailstock barrel position scale / DRO fitted

  // Drive Safety Interlocks (Bits 16-17)
  CAP_DRIVE_ALARM_Z    = 1 << 16, // Closed-loop stepper / servo fault alarm on Z
  CAP_DRIVE_ALARM_X    = 1 << 17, // Closed-loop stepper / servo fault alarm on X

  // Aliases for compatibility
  CAP_MPG_DEDICATED    = CAP_MPG_Z,
  CAP_MPG_SHARED       = CAP_MPG_X,
  CAP_TAILSTOCK_DRO    = CAP_TAILSTOCK
};

// -----------------------------------------------------------------------------
// 2. Hardware Configuration Structs
// -----------------------------------------------------------------------------

/**
 * @brief Quadrature pulse encoder configuration.
 * Shared by Spindle, DRO scales, and MPGs.
 */
struct QuadratureEncoderConfig {
  uint16_t ppr;                  // Native pulses per revolution (or pulses/mm for linear scales)
  int8_t   aPin;                 // GPIO pin for Channel A (-1 if unassigned)
  int8_t   bPin;                 // GPIO pin for Channel B (-1 if unassigned)
  bool     invertDir;            // Direction phase invert (swap A/B)
  bool     hasIndex;             // Single-pulse index signal (Z-channel)
  int8_t   indexPin;             // GPIO pin for Index channel (-1 if unassigned)
};

/**
 * @brief Spindle mechanical & electrical configuration.
 * Decouples native encoder PPR from spindle:motor pulley ratio.
 */
struct SpindleHardwareConfig {
  QuadratureEncoderConfig encoder;          // Spindle native encoder hardware
  float                   pulleyRatio;      // Spindle:motor ratio (default 1.0f)
  float                   maxSafeRpm;       // Headstock mechanical safety ceiling (e.g. 2500 RPM)
  float                   minSyncRpm;       // Minimum reliable encoder tracking RPM (typically 15 RPM)

  // VFD / Servo Inverter Control
  bool                    hasVfdControl;    // True if controller commands spindle speed/direction
  uint8_t                 vfdModbusAddress; // RS-485 Modbus RTU address
  float                   vfdMaxHz;         // Maximum inverter frequency (e.g. 60Hz, 400Hz)

  // Method helper: Effective Spindle PPR = native PPR * pulley ratio
  float effectivePpr() const {
    return (float)encoder.ppr * (pulleyRatio > 0.0f ? pulleyRatio : 1.0f);
  }
};

/**
 * @brief Actuator drive technology types.
 */
enum DriveType : uint8_t {
  DRIVE_GENERIC              = 0, // Minimal step/dir/enable interface
  DRIVE_STEPPER_CLOSED_LOOP  = 1, // Closed-loop stepper with fault/stall alarm
  DRIVE_AC_SERVO             = 2  // Digital AC servo with in-position & brake signals
};

/**
 * @brief Drive electrical & signal interlocks.
 */
struct DriveConfig {
  DriveType driveType;           // Actuator technology enum
  int8_t    alarmPin;            // Driver fault/alarm input GPIO (-1 if unused)
  int8_t    inPositionPin;       // Servo INP signal GPIO (-1 if unused)
  int8_t    brakePin;            // Electromagnetic brake output GPIO (-1 if unused)
};

/**
 * @brief Travel limits and boundary switches.
 */
struct LimitsConfig {
  float  travelMinMm;            // Extreme physical limit toward chuck/center
  float  travelMaxMm;            // Extreme physical limit toward bed end/retract
  int8_t minSwitchPin;           // Minimum limit microswitch GPIO (-1 if unused)
  int8_t maxSwitchPin;           // Maximum limit microswitch GPIO (-1 if unused)
  bool   stallGuardEnabled;      // True if sensorless stall detection is active
};

/**
 * @brief Complete axis hardware configuration.
 */
struct AxisHardwareConfig {
  // Motor Configuration
  bool                    hasMotor;          // True if axis is driven by stepper/servo
  DriveConfig             drive;             // Drive type and interlock pins
  float                   stepsPerMm;        // Motor driver pulses per millimeter of travel
  float                   leadscrewPitchMm;  // Physical screw pitch in mm
  float                   backlashMm;        // Measured screw backlash for compensation
  float                   maxVelocityMmMin;  // Rapid traverse ceiling (mm/min)
  float                   maxAccelMmSec2;    // Acceleration ramp limit (mm/sec^2)

  // Feedback & Limits
  bool                    hasDroScale;       // True if physical glass/magnetic scale fitted
  QuadratureEncoderConfig droScale;          // Reusable encoder config for linear scale
  bool                    hasMpg;            // True if dedicated MPG handwheel fitted
  LimitsConfig            limits;            // Travel boundaries and switch pins

  // Canonical deci-micron helpers
  long leadscrewPitchDu() const {
    return (long)round(leadscrewPitchMm * 10000.0f);
  }
  long backlashDu() const {
    return (long)round(backlashMm * 10000.0f);
  }
};

/**
 * @brief Lathe physical envelope and visualizer geometry.
 */
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
  char                  profileId[24];   // Machine slug, e.g. "wm210v_8x16"
  char                  modelName[48];   // Human readable label, e.g. "WM210V 8x16 Bench Lathe"
  uint32_t              capabilities;    // Bitmask of LatheCapabilityFlags
  bool                  isSystemProfile; // True if read-only factory template

  AxisHardwareConfig    zAxis;           // Carriage (Z)
  AxisHardwareConfig    xAxis;           // Cross-Slide (X)

  SpindleHardwareConfig spindle;         // Spindle encoder & VFD
  LatheDimensions       dimensions;      // Bed, chuck, and swing geometry

  // Convenience helper
  bool hasCapability(LatheCapabilityFlags flag) const {
    return (capabilities & (uint32_t)flag) != 0;
  }
};

// -----------------------------------------------------------------------------
// 4. Standard Lathe Preset API & LittleFS Catalog
// -----------------------------------------------------------------------------

LatheConfiguration getDefaultProfile();
const LatheConfiguration* findPresetBySlug(const char* slug);
bool loadProfileFromLittleFS(const char* slug, LatheConfiguration* outProfile);
bool isSystemCatalogProfile(const char* slug);
bool getProfileJsonFromLittleFS(const char* slug, String* outJson);
bool saveProfileToLittleFS(const LatheConfiguration& profile);
bool deleteProfileFromLittleFS(const char* slug);
bool getDefaultCatalogProfileId(char* outSlug, size_t maxLen);
bool setDefaultCatalogProfileId(const char* slug);
bool listCatalogProfiles(String* outList);
const char* getCapabilityName(LatheCapabilityFlags flag);
