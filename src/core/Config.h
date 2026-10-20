#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include "BoardPins.h"
#include "LatheProfile.h"

/**
 * =============================================================================
 * whatELS - Central Hardware & Motion Configuration (Modular Architecture)
 * =============================================================================
 *
 * TABLE OF CONTENTS:
 *  1. System Versioning & Storage Namespaces
 *  2. Hardware Pinout & Peripheral Flags
 *  3. Motion Physics & Default Parameters
 *  4. Operational Modes, Units & Step Increments
 *  5. HMI & Input Action Codes
 *  6. Persistent Storage (NVS) Keys
 *  7. Global Runtime State & Lifecycle Functions
 * =============================================================================
 */

// =============================================================================
// 1. SYSTEM IDENTIFICATION & VERSIONING
// =============================================================================

#define SOFTWARE_VERSION 38
#define HARDWARE_VERSION 5
#define WHATELS_STRINGIFY_VALUE(value) #value
#define WHATELS_STRINGIFY(value) WHATELS_STRINGIFY_VALUE(value)
#define CONTROLLER_VERSION_TEXT "H" WHATELS_STRINGIFY(HARDWARE_VERSION) "V" WHATELS_STRINGIFY(SOFTWARE_VERSION)

// Flash Storage Namespaces (4-tier taxonomy)
#define NVS_NAMESPACE_MACHINE "machine" // Tier 1: Hardware geometry & machine profile
#define NVS_NAMESPACE_SYS     "sys"     // Tier 2: Subsystem & peripheral configs
#define NVS_NAMESPACE_PREFS   "prefs"   // Tier 3: Workshop operator preferences
#define PREF_NAMESPACE        NVS_NAMESPACE_PREFS // Authoritative operator preferences namespace

// =============================================================================
// 2. HARDWARE PINOUT & PERIPHERAL FLAGS
// =============================================================================

// USB Host Keyboard (Supported on ESP32-S3 and ESP32-S2)
#ifndef ENABLE_USB_KEYBOARD
#define ENABLE_USB_KEYBOARD 0
#endif

// =============================================================================
// 3. MOTION PHYSICS & DEFAULT PARAMETERS
// =============================================================================

const int32_t STARTS_MAX = 124;

// -----------------------------------------------------------------------------
// Tier 2: Controller & Peripheral Configuration Structs
// -----------------------------------------------------------------------------

struct WifiConfig {
  bool enabled = true;
  String ssid = "";
  String password = "";
  String hostname = "whatels";
  String setupApSsid = "whatELS-Setup";
  String setupApPassword = "whatels-setup";
  long incomingBufferSize = 100000;
  long outgoingBufferSize = 100000;
};

struct MpgAxisConfig {
  float ppr = 600.0f;
};

struct MpgConfig {
  MpgAxisConfig zHandwheel = { 600.0f };
  MpgAxisConfig xHandwheel = { 600.0f };
  unsigned long pulseWaitMs = 20;
};

struct ControllerConfig {
  int swVersion = SOFTWARE_VERSION;
  WifiConfig wifi;
  MpgConfig mpg;
};

extern ControllerConfig sysConfig;

// =============================================================================
// 4. OPERATIONAL MODES, UNITS & STEP INCREMENTS
// =============================================================================

// Standard Jog & Feed Step Tables (Deci-microns: 10,000 du = 1.0mm; 254,000 du = 1.0")
constexpr long JOG_STEPS_METRIC[]   = { 10000, 1000, 100 };       // 1.0mm, 0.1mm, 0.01mm
constexpr long JOG_STEPS_IMPERIAL[] = { 25400, 2540, 254 };       // 0.1", 0.01", 0.001"
constexpr long FEED_STEPS_METRIC[]  = { 100, 500, 1000, 2000 };   // 0.01, 0.05, 0.10, 0.20 mm/rev
constexpr long FEED_STEPS_IMPERIAL[]= { 25, 51, 127, 254 };       // 0.001", 0.002", 0.005", 0.010"/rev

// Lathe Operating Modes
enum class LatheMode : uint8_t {
  NORMAL   = 0,
  XGEAR    = 1,
  ASYNC    = 2,
  CONE     = 3,
  TURN     = 4,
  FACE     = 5,
  CUT      = 6,
  THREAD   = 7,
  ELLIPSE  = 8,
  GCODE    = 9,
  JOYSTICK = 11,
  SLOT     = 12
};

// Units of Measure
enum class MeasureUnit : uint8_t {
  METRIC = 0,
  INCH   = 1,
  TPI    = 2
};

// Emergency Stop (E-Stop)
enum class EstopCode : uint8_t {
  NONE             = 0,
  POS              = 2,
  MARK_ORIGIN      = 3,
  ON_OFF           = 4,
  OFF_MANUAL_MOVE  = 5
};

extern EstopCode emergencyStop;
void setEmergencyStop(EstopCode kind);

// -----------------------------------------------------------------------------
// Workshop Operator Preferences (Tier 3)
// -----------------------------------------------------------------------------

struct OperatorPreferences {
  MeasureUnit measure = MeasureUnit::METRIC; // Metric (0), Imperial (1), Pitch/TPI (2)
  bool isDiameterMode = true;     // Diameter Ø (2x cross-slide) vs Radius R (1x)
  bool isAbsoluteMode = true;     // Absolute (G90) vs Incremental (G91)
  bool zDroActive = false;        // Optical linear scale vs motor step tracking
  bool xDroActive = false;
  long jogStepDu = JOG_STEPS_METRIC[0];   // 1.0 mm (metric) or 0.1" (imperial)
  long feedStepDu = FEED_STEPS_METRIC[0];  // 0.01 mm/rev or 0.001"/rev
  float deadbandAngle = 0.0f;
  bool isDeadbandAngleAuto = true;
  float deadbandTurns = 0.0f;
  bool isDeadbandTurnsAuto = true;
};

extern OperatorPreferences operatorPrefs;

// =============================================================================
// 5. HMI & INPUT ACTION CODES
// =============================================================================

// Motion & Directional Actions
#define B_LEFT 21
#define B_RIGHT 22
#define B_UP 23
#define B_DOWN 24
#define B_FORWARD 85
#define B_BACK 74
#define B_MINUS 60
#define B_PLUS 95

// Spindle, Stops & Auxiliary Operations
#define B_ON 30
#define B_OFF 27
#define B_STOPL 65
#define B_STOPR 68
#define B_STOPU 87
#define B_STOPD 83
#define B_STOPF 73
#define B_STOPB 75
#define B_MULTISTART 84
#define B_DISPL 12
#define B_STEP 64
#define B_MEASURE 77
#define B_REVERSE 82
#define B_DIAMETER 79

// Numeric Keypad & Navigation
#define B_0 48
#define B_1 49
#define B_2 50
#define B_3 51
#define B_4 52
#define B_5 53
#define B_6 54
#define B_7 55
#define B_8 56
#define B_9 57
#define B_BACKSPACE 28

// Mode Selection Actions (Consolidated)
#define B_MODE 107
#define B_MODE_GEARS 97
#define B_MODE_TURN 98
#define B_MODE_FACE 99
#define B_MODE_CONE 100
#define B_MODE_CUT 101
#define B_MODE_THREAD 102
#define B_MODE_ASYNC 103
#define B_MODE_ELLIPSE 104
#define B_MODE_GCODE 105
#define B_MODE_JOYSTICK 108
#define B_MODE_XGEAR 109
#define B_MODE_SLOT 110

// Axis, DRO & Display Configuration Actions
#define B_X 88
#define B_Z 90
#define B_X_ENA 67
#define B_Z_ENA 81
#define B_X_TOGGLE_DRO 206
#define B_Z_TOGGLE_DRO 207
#define B_DIA_RAD 202
#define B_ABS_INC 203
#define B_KNOWN_DIA 204
#define B_KNOWN_LENGTH 205
#define B_VERSION 208
#define B_SPINDLE_UNITS 209
#define B_PITCH 210

// Input Binding Struct
struct KeyboardBinding {
  const char* id = nullptr;
  const char* label = nullptr;
  uint8_t actionCode = 0;
  uint8_t defaultCode = 0;
  uint8_t code = 0;
};

#define KEY_BINDING(id, label, code) { id, label, code, code, code }

extern KeyboardBinding keyboardBindings[];
extern const int KEYBOARD_BINDING_COUNT;

// =============================================================================
// 6. PERSISTENT STORAGE (NVS) KEYS
// =============================================================================

// Persistent Operator Preferences (Tier 3: Namespace "prefs")
#define PREF_VERSION "v"
#define PREF_SHOW_ANGLE "ang"
#define PREF_SHOW_TACHO "rpm"
#define PREF_MOVE_STEP "ms"
#define PREF_FEED_STEP "feed_step"
#define PREF_MEASURE "mea"
#define PREF_Z_DRO_MODE "zdro"
#define PREF_X_DRO_MODE "xdro"
#define PREF_DIAMETER_MODE "dia"
#define PREF_ABSOLUTE_MODE "abs"
#define PREF_DEADBAND_ANGLE "db_angle"
#define PREF_DEADBAND_TURNS "db_turns"

// Keyboard Configuration Keys (Tier 2: Namespace "sys")
#define KCFG_SHOW_KEY_PRESSES "showKeys"

// =============================================================================
// 7. GLOBAL RUNTIME STATE & LIFECYCLE FUNCTIONS
// =============================================================================

// Global Variables (extern declarations)
extern bool SHOW_KEY_PRESSES;

// Active Lathe Machine Profile (Tier 1 Architecture)
extern LatheConfiguration activeMachineProfile;
void loadMachineProfile();
void saveMachineProfile();
void applyDerivedMachineConfig();
void printMachineProfileBanner();

// Keyboard Configuration Lifecycle (Tier 2: "sys")
void setKeyboardConfigDefaults();
bool validateKeyboardConfig(String* error);
void loadKeyboardConfig();
void saveKeyboardConfig();

// WiFi Configuration Lifecycle (Tier 2: "sys")
void setWifiConfigDefaults();
bool validateWifiConfig(String* error);
void loadWifiConfig();
void saveWifiConfig();

// MPG Handwheel Configuration Lifecycle (Tier 2: "sys")
void setMpgConfigDefaults();
void loadMpgConfig();
void saveMpgConfig();
