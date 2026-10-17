#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "OperatorIntent.h"

// -----------------------------------------------------------------------------
// Inter-Core Motion Snapshot (~64 Bytes Value-Semantic Transport)
// -----------------------------------------------------------------------------
struct StatusSnapshot {
  uint32_t timestamp;
  int32_t  zDu;
  int32_t  xDu;
  int32_t  zLeftDu;
  bool     zLeftDefined;
  bool     zLeftEnabled;
  int32_t  zRightDu;
  bool     zRightDefined;
  bool     zRightEnabled;
  int32_t  xUpDu;
  bool     xUpDefined;
  bool     xUpEnabled;
  int32_t  xDownDu;
  bool     xDownDefined;
  bool     xDownEnabled;
  bool     zDisabled;
  bool     xDisabled;
  uint16_t rpm;
  float    turns;
  float    angle;
  uint8_t  mode;
  bool     isOn;
  bool     estop;
  int32_t  dupr;
  int32_t  nextDupr;
};

extern QueueHandle_t motionSnapshotQueue;
void publishMotionSnapshot();

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

struct KinematicsState {
  long    zDu;
  long    xDu;
  int32_t zLeftDu;
  bool    zLeftDefined;
  bool    zLeftEnabled;
  int32_t zRightDu;
  bool    zRightDefined;
  bool    zRightEnabled;
  int32_t xUpDu;
  bool    xUpDefined;
  bool    xUpEnabled;
  int32_t xDownDu;
  bool    xDownDefined;
  bool    xDownEnabled;
  bool    zDisabled;
  bool    xDisabled;
  int     rpm;
  float   turns;
  float   angle;
  int     mode;
  bool    isOn;
  bool    estop;
  long    dupr;
  long    nextDupr;
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
