#pragma once

#include <Arduino.h>
#include "Config.h"
#include "Motion.h"

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
  long zDu;
  long xDu;
  long yDu;
  long zLeftDu;
  long zRightDu;
  long xUpDu;
  long xDownDu;
  bool zDisabled;
  bool xDisabled;
  bool yDisabled;
  int rpm;
  long turns;
  long angle;
  int mode;
  bool isOn;
  bool estop;
  long dupr;
  long nextDupr;
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
};
