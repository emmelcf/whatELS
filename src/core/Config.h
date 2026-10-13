#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <driver/pulse_cnt.h>
#include "LatheProfile.h"

// Versioning
#define SOFTWARE_VERSION 38
#define HARDWARE_VERSION 5
#define NANOELS_STRINGIFY_VALUE(value) #value
#define NANOELS_STRINGIFY(value) NANOELS_STRINGIFY_VALUE(value)
#define CONTROLLER_VERSION_TEXT "H" NANOELS_STRINGIFY(HARDWARE_VERSION) "V" NANOELS_STRINGIFY(SOFTWARE_VERSION)

#define PREFERENCES_VERSION 1
#define PREF_NAMESPACE "h5"
#define CONFIG_VERSION 1
#define CONFIG_NAMESPACE "h5cfg"
#define WIFI_CONFIG_VERSION 1
#define WIFI_CONFIG_NAMESPACE "h5wifi"
#define KEYBOARD_CONFIG_VERSION 1
#define KEYBOARD_CONFIG_NAMESPACE "h5keys"

#define TIMER_FREQ 1000000 // 1MHz async timer frequency

// Spindle optical rotary encoder
const int DEFAULT_ENCODER_PPR = 1200;
const int DEFAULT_ENCODER_BACKLASH = 3;
const int DEFAULT_AXIS_ENCODER_BACKLASH = 0;
#define ENC_A 14
#define ENC_B 13

// Main lead screw (Z) parameters
const long DEFAULT_SCREW_Z_DU = 40000;
const long DEFAULT_MOTOR_STEPS_Z = 800;
const long DEFAULT_SPEED_START_Z = DEFAULT_MOTOR_STEPS_Z;
const long DEFAULT_ACCELERATION_Z = 25 * DEFAULT_MOTOR_STEPS_Z;
const long DEFAULT_SPEED_MANUAL_MOVE_Z = 8 * DEFAULT_MOTOR_STEPS_Z;
const bool DEFAULT_INVERT_Z = false;
const bool DEFAULT_INVERT_Z_ENABLE = false;
const bool DEFAULT_NEEDS_REST_Z = false;
const long DEFAULT_MAX_TRAVEL_MM_Z = 300;
const long DEFAULT_BACKLASH_DU_Z = 0;
const char NAME_Z = 'Z';

#define Z_ENA 41
#define Z_DIR 42
#define Z_STEP 35
#define Z_PULSE_A 18
#define Z_PULSE_B 8

// Cross-slide lead screw (X) parameters
const long DEFAULT_SCREW_X_DU = 40000;
const long DEFAULT_MOTOR_STEPS_X = 800;
const long DEFAULT_SPEED_START_X = DEFAULT_MOTOR_STEPS_X;
const long DEFAULT_ACCELERATION_X = 25 * DEFAULT_MOTOR_STEPS_X;
const long DEFAULT_SPEED_MANUAL_MOVE_X = 8 * DEFAULT_MOTOR_STEPS_X;
const bool DEFAULT_INVERT_X = true;
const bool DEFAULT_INVERT_X_ENABLE = false;
const bool DEFAULT_NEEDS_REST_X = false;
const long DEFAULT_MAX_TRAVEL_MM_X = 100;
const long DEFAULT_BACKLASH_DU_X = 0;
const char NAME_X = 'X';

#define X_ENA 16
#define X_DIR 15
#define X_STEP 7
#define X_PULSE_A 47
#define X_PULSE_B 21

// Y axis parameters
const bool DEFAULT_ACTIVE_Y = false;
const bool DEFAULT_ROTARY_Y = true;
const long DEFAULT_MOTOR_STEPS_Y = 300;
const long DEFAULT_SCREW_Y_DU = 20000;
const long DEFAULT_SPEED_START_Y = 1600;
const long DEFAULT_ACCELERATION_Y = 16000;
const long DEFAULT_SPEED_MANUAL_MOVE_Y = 3200;
const bool DEFAULT_INVERT_Y = false;
const bool DEFAULT_INVERT_Y_ENABLE = false;
const bool DEFAULT_NEEDS_REST_Y = false;
const long DEFAULT_MAX_TRAVEL_MM_Y = 360;
const long DEFAULT_BACKLASH_DU_Y = 0;
const char NAME_Y = 'Y';

#define Y_ENA 1
#define Y_DIR 2
#define Y_STEP 17
#define Y_PULSE_A 45
#define Y_PULSE_B 48

// DRO Scales vs Handwheels configuration (set true if physical linear scale is used instead of handwheel)
#define HAS_DRO_Z false
#define HAS_DRO_X false

#define Z_SCALE_PIN_A (gpio_num_t)9
#define Z_SCALE_PIN_B (gpio_num_t)12
#define Z_SCALE_PCNT_UNIT 1
#define Z_SCALE_PULSES_PER_MM 1250.67f

#define X_SCALE_PIN_A (gpio_num_t)40
#define X_SCALE_PIN_B (gpio_num_t)39
#define X_SCALE_PCNT_UNIT 2
#define X_SCALE_PULSES_PER_MM 501.2f

#define COLOR_DRO_ON 2016
#define COLOR_DRO_OFF 25388

// Handwheels
const float DEFAULT_PULSE_PER_REVOLUTION_Z = 600;
const float DEFAULT_PULSE_PER_REVOLUTION_X = 600;
const float DEFAULT_PULSE_PER_REVOLUTION_Y = 600;
const unsigned long HANDWHEEL_PULSE_WAIT_MS = 20;

// Joystick
#define JOY_Z 4
#define JOY_X 5
#define JOY_Y 6
#define JOY_BUTTON 38

const bool DEFAULT_JOYSTICK_ENABLED = false;
const bool DEFAULT_JOYSTICK_Z_ENABLED = true;
const bool DEFAULT_JOYSTICK_X_ENABLED = true;
const bool DEFAULT_JOYSTICK_Y_ENABLED = true;
const bool DEFAULT_JOYSTICK_BUTTON_TOGGLES_ON_OFF = false;
const int DEFAULT_JOYSTICK_CENTER_SAMPLES = 64;
const int DEFAULT_JOYSTICK_OVERSAMPLES = 4;
const int DEFAULT_JOYSTICK_SAMPLE_INTERVAL_MS = 20;
const int DEFAULT_JOYSTICK_ADC_MAX = 4095;
const int DEFAULT_JOYSTICK_DEADBAND = 250;
const int DEFAULT_JOYSTICK_PULSE_QUEUE_LIMIT = 10000;
const float DEFAULT_JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND = 1.0;
const float DEFAULT_JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND = 8.0;
const float JOYSTICK_PITCH_CHANGES_PER_SECOND = 20.0;
const unsigned long JOYSTICK_PITCH_STATUS_HOLD_MS = 500;
const unsigned long JOYSTICK_BUTTON_DEBOUNCE_MS = 30;
const unsigned long JOYSTICK_BUTTON_TOGGLE_MAX_MS = 500;
const int JOYSTICK_STARTUP_CENTER_TOLERANCE_MIN = 32;
const bool DEFAULT_INVERT_JOYSTICK_Z = false;
const bool DEFAULT_INVERT_JOYSTICK_X = false;
const bool DEFAULT_INVERT_JOYSTICK_Y = false;
const bool DEFAULT_INVERT_JOYSTICK_BUTTON = false;

// Step timing
const long DEFAULT_STEP_TIME_MS = 500;
const long DEFAULT_DELAY_BETWEEN_STEPS_MS = 80;
const bool DEFAULT_ENABLE_CONTINUOUS_MOVE = true;
const long DEFAULT_JOG_HOLD_DELAY_MS = 1000;

// WiFi defaults
const bool DEFAULT_WIFI_ENABLED = true;
const char* const DEFAULT_WIFI_SSID = "";
const char* const DEFAULT_WIFI_PASSWORD = "";
const char* const DEFAULT_WIFI_HOSTNAME = "nanoels";
const char* const WIFI_SETUP_AP_SSID = "NanoEls-H5-Setup";
const char* const WIFI_SETUP_AP_PASSWORD = "nanoels-h5";
const long INCOMING_BUFFER_SIZE = 100000;
const long OUTGOING_BUFFER_SIZE = 100000;

// Hardware limits & filters
const int ENCODER_FILTER = 1;
const int PCNT_LIM = 31000;
const int PCNT_CLEAR = 30000;
const long DUPR_MAX = 254000;
const int32_t STARTS_MAX = 124;
const long PASSES_MAX = 999;
const long DEFAULT_SAFE_DISTANCE_DU = 5000;
const long DEFAULT_SLOT_LEFT_REDUCTION_DU = 0;
const long SAVE_DELAY_US = 5000000;
const long DIRECTION_SETUP_DELAY_US = 10;
const long STEPPED_ENABLE_DELAY_MS = 100;
const float TPI_ROUND_EPSILON = 0.03;

// GCode constants
const float LINEAR_INTERPOLATION_PRECISION = 0.1;
const long GCODE_WAIT_EPSILON_STEPS = 10;
const bool SPINDLE_PAUSES_GCODE = true;
const int GCODE_MIN_RPM = 30;
const long GCODE_FEED_DEFAULT_DU_SEC = 20000;
const float GCODE_FEED_MIN_DU_SEC = 167;

// Nextion LCD & PS2 keyboard
const long NEXTION_NORMAL_BAUD = 115200;
const long NEXTION_FIRST_UPLOAD_BAUD = 9600;
const long NEXTION_UPLOAD_BAUD = 115200;
const int NEXTION_TFT_PACKET_SIZE = 4096;
const unsigned long NEXTION_CONNECT_TIMEOUT_MS = 2000;
const unsigned long NEXTION_ACK_TIMEOUT_MS = 5000;
#define KEY_DATA 37
#define KEY_CLOCK 36

// USB Host Keyboard (Supported on ESP32-S3 and ESP32-S2)
#ifndef ENABLE_USB_KEYBOARD
#define ENABLE_USB_KEYBOARD 0
#endif

// Debug UART via MISC Header (for Rainbowlink v2 / bench debugging)
#define DEBUG_UART_TX_PIN 10
#define DEBUG_UART_RX_PIN 11
#define DEBUG_UART_BAUD 115200

#define DHIGH(x) digitalWrite(x, HIGH)
#define DLOW(x) digitalWrite(x, LOW)
#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS)

#define LCD_HASH_INITIAL -3845709

// Step sizes (Jogging)
#define MOVE_STEP_1 10000 // 1mm
#define MOVE_STEP_2 1000  // 0.1mm
#define MOVE_STEP_3 100   // 0.01mm

#define MOVE_STEP_IMP_1 25400 // 1/10"
#define MOVE_STEP_IMP_2 2540  // 1/100"
#define MOVE_STEP_IMP_3 254   // 1/1000"

// Feed step sizes (Chip-load / feed rate adjustment)
#define FEED_STEP_1 100   // 0.01mm/rev
#define FEED_STEP_2 500   // 0.05mm/rev
#define FEED_STEP_3 1000  // 0.10mm/rev
#define FEED_STEP_4 2000  // 0.20mm/rev

#define FEED_STEP_IMP_1 25   // 0.001"/rev
#define FEED_STEP_IMP_2 51   // 0.002"/rev
#define FEED_STEP_IMP_3 127  // 0.005"/rev
#define FEED_STEP_IMP_4 254  // 0.010"/rev

// Lathe modes
#define MODE_NORMAL 0
#define MODE_XGEAR 1
#define MODE_ASYNC 2
#define MODE_CONE 3
#define MODE_TURN 4
#define MODE_FACE 5
#define MODE_CUT 6
#define MODE_THREAD 7
#define MODE_ELLIPSE 8
#define MODE_GCODE 9
#define MODE_Y 10
#define MODE_JOYSTICK 11
#define MODE_SLOT 12

// Units
#define MEASURE_METRIC 0
#define MEASURE_INCH 1
#define MEASURE_TPI 2

// Emergency Stop
#define ESTOP_NONE 0
#define ESTOP_POS 2
#define ESTOP_MARK_ORIGIN 3
#define ESTOP_ON_OFF 4
#define ESTOP_OFF_MANUAL_MOVE 5

#define TIMER_FREQ 1000000

// Keyboard & Nextion Button Actions
#define B_LEFT 21
#define B_RIGHT 22
#define B_UP 23
#define B_DOWN 24
#define B_FORWARD 85
#define B_BACK 74
#define B_MINUS 60
#define B_PLUS 95
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
#define B_MODE_GEARS 97
#define B_MODE_TURN 98
#define B_MODE_FACE 99
#define B_MODE_CONE 100
#define B_MODE_CUT 101
#define B_MODE_THREAD 102
#define B_MODE_SLOT 110
#define B_X_TOGGLE_DRO 206
#define B_Z_TOGGLE_DRO 207
#define B_DIA_RAD 202
#define B_ABS_INC 203
#define B_KNOWN_DIA 204
#define B_KNOWN_LENGTH 205
#define B_VERSION 208
#define B_SPINDLE_UNITS 209
#define B_PITCH 210

enum NumpadMode { PITCH, DIAMETER, LENGTH };
extern NumpadMode numpadEntryMode;

#define B_MODE_ASYNC 103
#define B_MODE_ELLIPSE 104
#define B_MODE_GCODE 105
#define B_MODE_Y 106
#define B_MODE 107
#define B_MODE_JOYSTICK 108
#define B_MODE_XGEAR 109
#define B_X 88
#define B_Z 90
#define B_Y 72
#define B_X_ENA 67
#define B_Z_ENA 81
#define B_Y_ENA 89

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

// Preference keys: Persistent Operator & UI Presentation Preferences
#define PREF_VERSION "v"
#define PREF_SHOW_ANGLE "ang"
#define PREF_SHOW_TACHO "rpm"
#define PREF_MOVE_STEP "ms"
#define PREF_MEASURE "mea"
#define PREF_Z_DRO_MODE "zdro"
#define PREF_X_DRO_MODE "xdro"
#define PREF_DIAMETER_MODE "dia"
#define PREF_ABSOLUTE_MODE "abs"

// Legacy / Deprecated volatile keys (retained for backward migration compatibility if needed)
#define PREF_DUPR "d"
#define PREF_POS_Z "zp"
#define PREF_LEFT_STOP_Z "zls"
#define PREF_RIGHT_STOP_Z "zrs"
#define PREF_ORIGIN_POS_Z "zpo"
#define PREF_POS_GLOBAL_Z "zpg"
#define PREF_MOTOR_POS_Z "zpm"
#define PREF_DISABLED_Z "zd"
#define PREF_POS_X "xp"
#define PREF_LEFT_STOP_X "xls"
#define PREF_RIGHT_STOP_X "xrs"
#define PREF_ORIGIN_POS_X "xpo"
#define PREF_POS_GLOBAL_X "xpg"
#define PREF_MOTOR_POS_X "xpm"
#define PREF_DISABLED_X "xd"
#define PREF_POS_Y "y1p"
#define PREF_LEFT_STOP_Y "y1ls"
#define PREF_RIGHT_STOP_Y "y1rs"
#define PREF_ORIGIN_POS_Y "y1po"
#define PREF_POS_GLOBAL_Y "y1pg"
#define PREF_MOTOR_POS_Y "y1pm"
#define PREF_DISABLED_Y "y1d"
#define PREF_SPINDLE_POS "sp"
#define PREF_SPINDLE_POS_AVG "spa"
#define PREF_OUT_OF_SYNC "oos"
#define PREF_SPINDLE_POS_GLOBAL "spg"
#define PREF_STARTS "sta"
#define PREF_MODE "mod"
#define PREF_CONE_RATIO "cr"
#define PREF_TURN_PASSES "tp"
#define PREF_AUX_FORWARD "af"

// Machine config keys
#define CFG_VERSION "v"
#define CFG_ENCODER_PPR "encPpr"
#define CFG_ENCODER_BACKLASH "encBacklash"
#define CFG_AXIS_ENCODER_BACKLASH "axisEncBacklash"
#define CFG_SCREW_Z_DU "zScrew"
#define CFG_MOTOR_STEPS_Z "zMotor"
#define CFG_SPEED_START_Z "zStart"
#define CFG_ACCELERATION_Z "zAccel"
#define CFG_SPEED_MANUAL_MOVE_Z "zManual"
#define CFG_INVERT_Z "zInv"
#define CFG_INVERT_Z_ENABLE "zInvEn"
#define CFG_NEEDS_REST_Z "zRest"
#define CFG_MAX_TRAVEL_MM_Z "zTravel"
#define CFG_BACKLASH_DU_Z "zBacklash"
#define CFG_SCREW_X_DU "xScrew"
#define CFG_MOTOR_STEPS_X "xMotor"
#define CFG_SPEED_START_X "xStart"
#define CFG_ACCELERATION_X "xAccel"
#define CFG_SPEED_MANUAL_MOVE_X "xManual"
#define CFG_INVERT_X "xInv"
#define CFG_INVERT_X_ENABLE "xInvEn"
#define CFG_NEEDS_REST_X "xRest"
#define CFG_MAX_TRAVEL_MM_X "xTravel"
#define CFG_BACKLASH_DU_X "xBacklash"
#define CFG_STEP_TIME_MS "stepMs"
#define CFG_DELAY_BETWEEN_STEPS_MS "stepDelay"
#define CFG_ENABLE_CONTINUOUS_MOVE "contStep"
#define CFG_SAFE_DISTANCE_DU "safeDistDu"
#define CFG_SLOT_LEFT_REDUCTION_DU "slotLeftDu"
#define CFG_ACTIVE_Y "yActive"
#define CFG_ROTARY_Y "yRotary"
#define CFG_MOTOR_STEPS_Y "yMotor"
#define CFG_SCREW_Y_DU "yScrew"
#define CFG_SPEED_START_Y "yStart"
#define CFG_ACCELERATION_Y "yAccel"
#define CFG_SPEED_MANUAL_MOVE_Y "yManual"
#define CFG_INVERT_Y "yInv"
#define CFG_INVERT_Y_ENABLE "yInvEn"
#define CFG_NEEDS_REST_Y "yRest"
#define CFG_MAX_TRAVEL_MM_Y "yTravel"
#define CFG_BACKLASH_DU_Y "yBacklash"
#define CFG_PULSE_PER_REVOLUTION "pulseRev"
#define CFG_PULSE_PER_REVOLUTION_Z "zPulseRev"
#define CFG_PULSE_PER_REVOLUTION_X "xPulseRev"
#define CFG_PULSE_PER_REVOLUTION_Y "yPulseRev"
#define CFG_JOYSTICK_ENABLED "joyOn"
#define CFG_JOYSTICK_Z_ENABLED "joyZOn"
#define CFG_JOYSTICK_X_ENABLED "joyXOn"
#define CFG_JOYSTICK_Y_ENABLED "joyYOn"
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
#define CFG_INVERT_JOYSTICK_Y "joyInvY"
#define CFG_INVERT_JOYSTICK_BUTTON "joyInvB"

#define WCFG_VERSION "v"
#define WCFG_ENABLED "en"
#define WCFG_SSID "ssid"
#define WCFG_PASSWORD "pwd"
#define WCFG_HOSTNAME "hn"

#define KCFG_VERSION "v"
#define KCFG_SHOW_KEY_PRESSES "showKeys"

// Global Variables (extern declarations)
extern int ENCODER_PPR;
extern int ENCODER_BACKLASH;
extern int AXIS_ENCODER_BACKLASH;

extern long SCREW_Z_DU;
extern long MOTOR_STEPS_Z;
extern long SPEED_START_Z;
extern long ACCELERATION_Z;
extern long SPEED_MANUAL_MOVE_Z;
extern bool INVERT_Z;
extern bool INVERT_Z_ENABLE;
extern bool NEEDS_REST_Z;
extern long MAX_TRAVEL_MM_Z;
extern long BACKLASH_DU_Z;

extern long SCREW_X_DU;
extern long MOTOR_STEPS_X;
extern long SPEED_START_X;
extern long ACCELERATION_X;
extern long SPEED_MANUAL_MOVE_X;
extern bool INVERT_X;
extern bool INVERT_X_ENABLE;
extern bool NEEDS_REST_X;
extern long MAX_TRAVEL_MM_X;
extern long BACKLASH_DU_X;

extern long STEP_TIME_MS;
extern long DELAY_BETWEEN_STEPS_MS;
extern bool ENABLE_CONTINUOUS_MOVE;
extern long JOG_HOLD_DELAY_MS;

extern bool WIFI_ENABLED;
extern String WIFI_SSID;
extern String WIFI_PASSWORD;
extern String WIFI_HOSTNAME;

extern bool ACTIVE_Y;
extern bool ROTARY_Y;
extern long MOTOR_STEPS_Y;
extern long SCREW_Y_DU;
extern long SPEED_START_Y;
extern long ACCELERATION_Y;
extern long SPEED_MANUAL_MOVE_Y;
extern bool INVERT_Y;
extern bool INVERT_Y_ENABLE;
extern bool NEEDS_REST_Y;
extern long MAX_TRAVEL_MM_Y;
extern long BACKLASH_DU_Y;

extern float PULSE_PER_REVOLUTION_Z;
extern float PULSE_PER_REVOLUTION_X;
extern float PULSE_PER_REVOLUTION_Y;

extern bool JOYSTICK_ENABLED;
extern bool JOYSTICK_Z_ENABLED;
extern bool JOYSTICK_X_ENABLED;
extern bool JOYSTICK_Y_ENABLED;
extern bool JOYSTICK_BUTTON_TOGGLES_ON_OFF;
extern int JOYSTICK_CENTER_SAMPLES;
extern int JOYSTICK_OVERSAMPLES;
extern int JOYSTICK_SAMPLE_INTERVAL_MS;
extern int JOYSTICK_ADC_MAX;
extern int JOYSTICK_DEADBAND;
extern int JOYSTICK_PULSE_QUEUE_LIMIT;
extern float JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND;
extern float JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND;
extern bool INVERT_JOYSTICK_Z;
extern bool INVERT_JOYSTICK_X;
extern bool INVERT_JOYSTICK_Y;
extern bool INVERT_JOYSTICK_BUTTON;

extern int ENCODER_STEPS_INT;
extern float ENCODER_STEPS_FLOAT;
extern long RPM_BULK;
extern long SAFE_DISTANCE_DU;
extern long SLOT_LEFT_REDUCTION_DU;

extern bool SHOW_KEY_PRESSES;

// DRO Scale state
extern bool zDisplayModeDRO;
extern bool xDisplayModeDRO;
extern bool isDiameterMode;
extern bool isAbsoluteMode;
extern int spindleDisplayMode;
extern bool spindleValColorGreen;
extern float xScaleIncrementalOffset;
extern float zScaleIncrementalOffset;

// Helpers
long clampLongValue(long value, long minValue, long maxValue);
int clampIntValue(int value, int minValue, int maxValue);
float clampFloatValue(float value, float minValue, float maxValue);

// Functions
void setMachineConfigDefaults();
void applyDerivedMachineConfig();
void normalizeMachineConfig();
void loadMachineConfig();
void saveMachineConfig();

// Active Lathe Machine Profile (Tier 1 Architecture)
extern LatheConfiguration activeMachineProfile;
void loadMachineProfile();
void saveMachineProfile();
void printMachineProfileBanner();

void setKeyboardConfigDefaults();
const char* keyboardActionLabel(byte actionCode);
byte keyboardActionForCode(byte physicalCode);
bool validateKeyboardConfig(String* error);
void loadKeyboardConfig();
void saveKeyboardConfig();

void setWifiConfigDefaults();
bool validateWifiConfig(String* error);
void loadWifiConfig();
void saveWifiConfig();

void setMeasure(int value);
