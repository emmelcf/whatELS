#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <driver/pulse_cnt.h>
#include "LatheProfile.h"

/**
 * =============================================================================
 * NanoELS - Central Hardware & Motion Configuration (H5 Architecture)
 * =============================================================================
 *
 * TABLE OF CONTENTS:
 *  1. System Versioning & Storage Namespaces
 *  2. Hardware Pinout & Board Definitions (NanoELS H5 PCB)
 *  3. Motion Physics, Limits & Default Machine Parameters
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
#define NANOELS_STRINGIFY_VALUE(value) #value
#define NANOELS_STRINGIFY(value) NANOELS_STRINGIFY_VALUE(value)
#define CONTROLLER_VERSION_TEXT "H" NANOELS_STRINGIFY(HARDWARE_VERSION) "V" NANOELS_STRINGIFY(SOFTWARE_VERSION)

// Flash Storage Namespaces (4-tier taxonomy)
#define NVS_NAMESPACE_MACHINE "machine" // Tier 1: Hardware geometry & pin limits
#define NVS_NAMESPACE_SYS     "sys"     // Tier 2: Subsystem & peripheral configs
#define NVS_NAMESPACE_PREFS   "prefs"   // Tier 3: Workshop operator preferences
#define PREF_NAMESPACE        NVS_NAMESPACE_PREFS // Authoritative operator preferences namespace
#define CONFIG_NAMESPACE      "h5cfg"   // Legacy fallback alias

// =============================================================================
// 2. HARDWARE PINOUT & BOARD DEFINITIONS (NanoELS H5 PCB)
// =============================================================================

// GPIO Fast Helper Macros
#define DHIGH(x) digitalWrite(x, HIGH)
#define DLOW(x) digitalWrite(x, LOW)
#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

// Spindle Optical Rotary Encoder
#define ENC_A 14
#define ENC_B 13

// Main Lead Screw (Z) Stepper / Servo Pins
#define Z_ENA 41
#define Z_DIR 42
#define Z_STEP 35
#define Z_PULSE_A 18
#define Z_PULSE_B 8

// Cross-Slide Lead Screw (X) Stepper / Servo Pins
#define X_ENA 16
#define X_DIR 15
#define X_STEP 7
#define X_PULSE_A 47
#define X_PULSE_B 21

// NanoELS H5 PCB Silkscreen Hardware Pin Headers (Reserved / Unpopulated)
#define Y_ENA 1
#define Y_DIR 2
#define Y_STEP 17
#define Y_PULSE_A 45
#define Y_PULSE_B 48

// DRO Linear Scales Pinouts (Quadrature Encoder interface)
#define Z_SCALE_PIN_A (gpio_num_t)9
#define Z_SCALE_PIN_B (gpio_num_t)12
#define Z_SCALE_PCNT_UNIT 1
#define Z_SCALE_PULSES_PER_MM 1250.67f

#define X_SCALE_PIN_A (gpio_num_t)40
#define X_SCALE_PIN_B (gpio_num_t)39
#define X_SCALE_PCNT_UNIT 2
#define X_SCALE_PULSES_PER_MM 501.2f

// Analog / Contact Joystick Pins
#define JOY_Z 4
#define JOY_X 5
#define JOY_Y 6 // Unpopulated H5 PCB silkscreen pin header
#define JOY_BUTTON 38

// Nextion LCD & PS/2 Keyboard Pins and UART Settings
#define KEY_DATA 37
#define KEY_CLOCK 36
const long NEXTION_NORMAL_BAUD = 115200;
const long NEXTION_FIRST_UPLOAD_BAUD = 9600;
const long NEXTION_UPLOAD_BAUD = 115200;
const int NEXTION_TFT_PACKET_SIZE = 4096;
const unsigned long NEXTION_CONNECT_TIMEOUT_MS = 2000;
const unsigned long NEXTION_ACK_TIMEOUT_MS = 5000;
#define LCD_HASH_INITIAL -3845709

// USB Host Keyboard (Supported on ESP32-S3 and ESP32-S2)
#ifndef ENABLE_USB_KEYBOARD
#define ENABLE_USB_KEYBOARD 0
#endif

// Debug UART via MISC Header (for Rainbowlink v2 / bench debugging)
#define DEBUG_UART_TX_PIN 10
#define DEBUG_UART_RX_PIN 11
#define DEBUG_UART_BAUD 115200

// =============================================================================
// 3. MOTION PHYSICS, LIMITS & DEFAULT MACHINE PARAMETERS
// =============================================================================

#define TIMER_FREQ 1000000 // 1MHz async timer frequency

// Spindle Optical Rotary Encoder & PCNT Filters
const int DEFAULT_ENCODER_PPR = 1200;
const int DEFAULT_ENCODER_BACKLASH = 3;
const int DEFAULT_AXIS_ENCODER_BACKLASH = 0;
const int ENCODER_FILTER = 1;
const int PCNT_LIM = 31000;
const int PCNT_CLEAR = 30000;
const long DUPR_MAX = 254000;
const int32_t STARTS_MAX = 124;
const long PASSES_MAX = 999;

// Axis Identifiers
const char NAME_Z = 'Z';
const char NAME_X = 'X';

// -----------------------------------------------------------------------------
// Tier 2: Controller & Peripheral Configuration Structs
// -----------------------------------------------------------------------------

struct WifiConfig {
  bool enabled = true;
  String ssid = "";
  String password = "";
  String hostname = "nanoels";
  String setupApSsid = "NanoEls-H5-Setup";
  String setupApPassword = "nanoels-h5";
  long incomingBufferSize = 100000;
  long outgoingBufferSize = 100000;
};

struct MpgConfig {
  QuadratureEncoderConfig zHandwheel = { 600, Z_PULSE_A, Z_PULSE_B, false, false, -1 };
  QuadratureEncoderConfig xHandwheel = { 600, X_PULSE_A, X_PULSE_B, false, false, -1 };
  unsigned long pulseWaitMs = 20;
};

struct MotionTimingConfig {
  long directionSetupDelayUs = 10;   // Wait time after DIR toggle before STEP pulse
  long steppedEnableDelayMs = 100;   // Driver power-on delay before motion
  long jogHoldDelayMs = 1000;        // Hold duration before continuous jogging starts
  long stepTimeMs = 500;
  long delayBetweenStepsMs = 80;
  bool enableContinuousMove = true;
};

struct GCodeConfig {
  float linearInterpolationPrecision = 0.1f;
  long waitEpsilonSteps = 10;
  bool spindlePausesGcode = true;
  int minRpm = 30;
  long defaultFeedDuPerSec = 20000;
  float minFeedDuPerSec = 167.0f;
};

struct JoystickConfig {
  bool enabled = false;
  bool zEnabled = true;
  bool xEnabled = true;
  bool buttonTogglesOnOff = false;
  int centerSamples = 64;
  int oversamples = 4;
  int sampleIntervalMs = 20;
  int adcMax = 4095;
  int deadband = 250;
  int pulseQueueLimit = 10000;
  float normalRps = 1.0f;
  float rapidRps = 8.0f;
  float pitchChangesPerSec = 20.0f;
  unsigned long pitchStatusHoldMs = 500;
  unsigned long buttonDebounceMs = 30;
  unsigned long buttonToggleMaxMs = 500;
  int startupCenterToleranceMin = 32;
  bool invertZ = false;
  bool invertX = false;
  bool invertButton = false;
};

struct NextionTheme {
  uint16_t activeHighlight = 2016;  // RGB565 Green (0x07E0) for active states & highlights
  uint16_t inactiveDim     = 25388; // RGB565 Dim Gray (0x632C) for inactive states
};

struct ControllerConfig {
  int swVersion = SOFTWARE_VERSION;
  WifiConfig wifi;
  JoystickConfig joystick;
  MpgConfig mpg;
  MotionTimingConfig timing;
  GCodeConfig gcode;
  NextionTheme theme;
};

extern ControllerConfig sysConfig;

const long DEFAULT_SAFE_DISTANCE_DU = 5000;
const long DEFAULT_SLOT_LEFT_REDUCTION_DU = 0;
const float TPI_ROUND_EPSILON = 0.03f;

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

// Numpad Entry Mode
enum NumpadMode { PITCH, DIAMETER, LENGTH };
extern NumpadMode numpadEntryMode;

// Input Binding & Event Structs
struct KeyboardBinding {
  const char* id = nullptr;
  const char* label = nullptr;
  byte actionCode = 0;
  byte defaultCode = 0;
  byte code = 0;
};

struct WebUiEvent {
  byte actionCode = 0;
  bool isPress = false;
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

// Machine Configuration Keys
#define CFG_VERSION "v"
#define CFG_ENCODER_BACKLASH "encBacklash"
#define CFG_AXIS_ENCODER_BACKLASH "axisEncBacklash"
#define CFG_STEP_TIME_MS "stepMs"
#define CFG_DELAY_BETWEEN_STEPS_MS "stepDelay"
#define CFG_ENABLE_CONTINUOUS_MOVE "contStep"
#define CFG_SAFE_DISTANCE_DU "safeDistDu"
#define CFG_SLOT_LEFT_REDUCTION_DU "slotLeftDu"
#define CFG_PULSE_PER_REVOLUTION "pulseRev"
#define CFG_PULSE_PER_REVOLUTION_Z "zPulseRev"
#define CFG_PULSE_PER_REVOLUTION_X "xPulseRev"
#define CFG_JOYSTICK_ENABLED "joyOn"
#define CFG_JOYSTICK_Z_ENABLED "joyZOn"
#define CFG_JOYSTICK_X_ENABLED "joyXOn"
#define CFG_JOYSTICK_BUTTON_TOGGLES_ON_OFF "joyBtnOn"
#define CFG_JOYSTICK_CENTER_SAMPLES "joyCenter"
#define CFG_JOYSTICK_OVERSAMPLES "joyOver"
#define CFG_JOYSTICK_SAMPLE_INTERVAL_MS "joySample"
#define CFG_JOYSTICK_ADC_MAX "joyAdc"
#define CFG_JOYSTICK_DEADBAND "joyDead"
#define CFG_JOYSTICK_PULSE_QUEUE_LIMIT "joyQueue"
#define CFG_JOYSTICK_NORMAL_RPS "joyNormal"
#define CFG_JOYSTICK_RAPID_RPS "joyRapid"
#define CFG_INVERT_JOYSTICK_Z "joyInvZ"
#define CFG_INVERT_JOYSTICK_X "joyInvX"
#define CFG_INVERT_JOYSTICK_BUTTON "joyInvB"

// Network & WiFi Configuration Keys
#define WCFG_VERSION "v"
#define WCFG_ENABLED "en"
#define WCFG_SSID "ssid"
#define WCFG_PASSWORD "pwd"
#define WCFG_HOSTNAME "hn"

// Keyboard Configuration Keys
#define KCFG_VERSION "v"
#define KCFG_SHOW_KEY_PRESSES "showKeys"



// =============================================================================
// 7. GLOBAL RUNTIME STATE & LIFECYCLE FUNCTIONS
// =============================================================================

// Global Variables (extern declarations)
extern int ENCODER_BACKLASH;
extern int AXIS_ENCODER_BACKLASH;

extern int ENCODER_STEPS_INT;
extern float ENCODER_STEPS_FLOAT;
extern long SAFE_DISTANCE_DU;
extern long SLOT_LEFT_REDUCTION_DU;

extern bool SHOW_KEY_PRESSES;

// Helpers
long clampLongValue(long value, long minValue, long maxValue);
int clampIntValue(int value, int minValue, int maxValue);
float clampFloatValue(float value, float minValue, float maxValue);

// Machine Configuration Lifecycle
void setMachineConfigDefaults();
void applyDerivedMachineConfig();
void normalizeMachineConfig();
void loadMachineConfig();

// Active Lathe Machine Profile (Tier 1 Architecture)
extern LatheConfiguration activeMachineProfile;
void loadMachineProfile();
void saveMachineProfile();
void printMachineProfileBanner();

// Keyboard Configuration Lifecycle
void setKeyboardConfigDefaults();
const char* keyboardActionLabel(byte actionCode);
byte keyboardActionForCode(byte physicalCode);
bool validateKeyboardConfig(String* error);
void loadKeyboardConfig();
void saveKeyboardConfig();

// WiFi Configuration Lifecycle
void setWifiConfigDefaults();
bool validateWifiConfig(String* error);
void loadWifiConfig();
void saveWifiConfig();

// Measure / Display Configuration
void setMeasure(MeasureUnit value);
void setMeasure(int value);
