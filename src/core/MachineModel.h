#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "OperatorIntent.h"

#include <optional>

// Domain Bitmasks for Machine Model
enum DomainFlags : uint8_t {
  DOMAIN_NONE       = 0x00,
  DOMAIN_KINEMATICS = 0x01,  // 'K': Dynamic physical axes, limits, spindle, mode, on/off
  DOMAIN_INTENT     = 0x02,  // 'I': Thread format/pitch, taper ratio/angle, steps, feed direction
  DOMAIN_CYCLE      = 0x04,  // 'C': Multi-pass counts, current pass, operation step, multi-starts
  DOMAIN_PROPERTIES = 0x08,  // 'P': Unit system, diameter mode, DRO active, leadscrew pitches
  DOMAIN_ALL        = 0x0F   // All domains combined (0x01 | 0x02 | 0x04 | 0x08)
};

// Domain helper: parses queries like "K,I", "kinematics,intent", "ALL", "P", etc. into bitmask
uint8_t parseDomainQuery(const char* query);

// -----------------------------------------------------------------------------
// Domain Models: Kinematics (Structured Internals)
// -----------------------------------------------------------------------------

struct AxisLimits {
  std::optional<int32_t> leftOrUpDu;     // std::nullopt when not defined or disabled
  std::optional<int32_t> rightOrDownDu;  // std::nullopt when not defined or disabled

  bool operator==(const AxisLimits& other) const {
    return leftOrUpDu == other.leftOrUpDu && rightOrDownDu == other.rightOrDownDu;
  }
  bool operator!=(const AxisLimits& other) const { return !(*this == other); }
};

struct AxisKinematics {
  int32_t    posDu{0};
  AxisLimits limits{};
  bool       disabled{false};

  bool operator==(const AxisKinematics& other) const {
    return posDu == other.posDu && limits == other.limits && disabled == other.disabled;
  }
  bool operator!=(const AxisKinematics& other) const { return !(*this == other); }
};

struct SpindleKinematics {
  uint16_t rpm{0};
  float    turns{0.0f};
  float    angle{0.0f};

  bool operator==(const SpindleKinematics& other) const {
    return rpm == other.rpm && turns == other.turns && angle == other.angle;
  }
  bool operator!=(const SpindleKinematics& other) const { return !(*this == other); }
};

struct KinematicsState {
  AxisKinematics    z{};
  AxisKinematics    x{};
  SpindleKinematics spindle{};
  uint8_t           mode{0};
  bool              isOn{false};
  bool              estop{false};
  int32_t           dupr{0};
  int32_t           nextDupr{0};
};

struct IntentState {
  ThreadSetting thread;
  TaperSetting taper;
  long feedStepDu;
  long jogStepDu;
  bool feedReverse;
};

struct CycleState {
  int turnPasses;
  int currentPass;
  int setupIndex;
  int starts;
};

struct MachineProperties {
  uint32_t capabilities;
  char presetId[16];
  int measure;
  bool isDiameterMode;
  bool isAbsoluteMode;
  bool zDroActive;
  bool xDroActive;
  long screwZDu;
  long screwXDu;
  long motorStepsZ;
  long motorStepsX;
  long encoderPpr;
  float deadbandAngle;
  bool deadbandAngleAuto;
  float deadbandTurns;
  bool deadbandTurnsAuto;
};

struct MachineModel {
  KinematicsState kinematics;
  IntentState intent;
  CycleState cycle;
  MachineProperties properties;

  static KinematicsState captureKinematics();
  static IntentState captureIntent();
  static CycleState captureCycle();
  static MachineProperties captureProperties();
  static MachineModel captureAll();

  // Returns bitmask of DomainFlags that changed between prev and current
  static uint8_t detectChangedDomains(const MachineModel& prev, const MachineModel& current);
  static bool hasChanged(const MachineModel& prev, const MachineModel& current, uint8_t domainMask = DOMAIN_ALL);
};
