
// https://github.com/kachurovskiy/nanoels

// ! ! ! M O D I F I E D  F I R M W A R E ! ! !
// Use at your OWN risk, most coding done with Gemini 2.5 Pro (I am NOT a
// programmer!) ALSO, this FW of course has my hardware settings in it. E.g.
// stock 1.5mm leadscrew, different encoder settings, Wifi is OFF, etc

// FW HEADLINE FEATURES
// *** DRO SCALES ON X/Z ***
// *** EXTERNAL KEYPAD/PUSH BUTTONS VIA REGULAR ESP32 PINS ***
// *** SEMI-AUTO THREAD AND TURN MODES AWAIT RUN/START BUTTON PRESS TO START
// Z-MOVES (for users without an X-motor) ***
// *** CUSTOM SCREENSET: NO Y-AXIS, LARGER AXIS READOUTS, LARGER NUMPAD, LARGER
// RUN/STOP, INCREMENT & JOG BUTTONS ***
// *** VARIOUS SMALL GUI IMPROVEMENTS ***

// SCREENSET VERSION
// For full features, use this FW with screenset file: H5_GS_DRO_V17 or
// H5_GS_NON-DRO_V17

// KNOWN ISSUES AND BUGS
// Imperial GUI is a bit rough around the edges, some values/text may spill over
// into others this should be a stricly aesthetic nuisance, but I likely wont be
// fixing it, sorry Jogging on touchscreen has often lead to "double or tripple
// tapping" which can get tricky when close to a part This FW has mostly fixed
// it (Z is OK, Z still double taps but at least does it all the time) but test
// in depth Also how well it works depends on the microstepping level(It's
// optimized for 1600 steps). Also, the timings for hold down jogging have now
// been split up so it's more smooth on ext. keys (See the forum thread where
// this FW was shared for more explaining of the issue)

// HOW TO USE
// Cycle through Modes by tapping the Mode text box
// Tap the left part of the X/Z coordinate box to disable that axis
// Tap the middle part of a coordinate box to toggle between displaying ELS or
// Scale (DRO) coordinates Tap the right part of a coordinate box to zero that
// axis Long press the left part of the X/Z coordinate box to toggle font color
// of the axis readout (green/white) Long press the right part of X/Z coordinate
// boxes to input a known diameter or length for that axis Cycle between showing
// RPM, Turns or Angle by tapping the box Long press the RPM/Turns/Angle value
// box to toggle between font colors (white/green) Long press Left Stop(limit)
// to cancel all limits Tap the Pitch box to change the font type Long press the
// Pitch box to reverse the pitch (negative pitch for left hand threads) Tap the
// Units (mm/in/TPI) under the pitch value box to cycle through units (on DRO
// screenset) Long press #1 for Multistart threading (its designated button has
// been removed from the DRO screenset)

// SCREENSET/GUI/KEYBOARD CUSTOMIZATION
// You can define which MODES you want to have shown. Search for "mode selection
// table" and set true/false MODES have different colors now, to define them
// search for "mode colors" Hold down +/- buttons to enable scrolling. Search
// for "+/- BUTTON LOGIC" to set the timing of this Various long press functions
// use timings from 300ms-500ms. If you want to change these, search for "long
// press"

// EXTERNAL KEYBOARD/PUSH BUTTONS
// For Button and PIN definitions, search for "pinout". This FW uses the regular
// ESP32 pins for push button input

// DOUBLE DUTY NUMPAD (this is for having numpad functionality on a custom 10
// key keypad) Short press on "2" to set "Pitch", Long press to set "Known Dia"
// Physical NUMPAD, long press on "0" locks the value (play/run/enter/set)
// Physical NUMPAD, long press on "5" clears/cancels the value (stop)

// Date: Nov 29, 2025

#define SOFTWARE_VERSION 104 // GeckoSub's Custom/Personal FW version

// =================================================================
// Geckosub + Gemini 2.5 Pro - SECTION 1, DRO Scale Configuration
// =================================================================
#include "DRO_Scale.h"
#include <driver/pcnt.h>

// --- X-Axis DRO Scale Configuration ---
#define X_SCALE_PIN_A (gpio_num_t)40
#define X_SCALE_PIN_B (gpio_num_t)39
// The number of pulses the scale generates for one millimeter of travel.
// You will need to calibrate this value for your specific linear scale.
// Example: A 5 micron (0.005mm) scale has 1/0.005 = 200 pulses per mm.
// Example: A 2 micron (0.002mm) scale has 1/0.002 = 500 pulses per mm.
// #define X_SCALE_PULSES_PER_MM 500.0f // this readhead is a 2um unit - but
// uncalibrated
#define X_SCALE_PULSES_PER_MM                                                  \
  501.2f // this is the calibated value (50mm move showed as a 50.12mm move on
         // the DRO)
// Assign the DRO to PCNT_UNIT_3, which is unused when ACTIVE_Y is false.
#define X_SCALE_PCNT_UNIT PCNT_UNIT_3

// Create the DRO Scale object
DRO_Scale xScale(X_SCALE_PIN_A, X_SCALE_PIN_B, X_SCALE_PCNT_UNIT,
                 X_SCALE_PULSES_PER_MM);

// --- Z-Axis DRO Scale Configuration ---
#define Z_SCALE_PIN_A (gpio_num_t)9
#define Z_SCALE_PIN_B (gpio_num_t)12
// Set this to your Z-scale's pulses per mm
#define Z_SCALE_PULSES_PER_MM                                                  \
  1250.67f // this is the calibrated value using 25-50-75 blocks
// Assign the Z DRO to PCNT_UNIT_1 (was previously Z-MPG)
#define Z_SCALE_PCNT_UNIT PCNT_UNIT_1

// Create the Z DRO Scale object
DRO_Scale zScale(Z_SCALE_PIN_A, Z_SCALE_PIN_B, Z_SCALE_PCNT_UNIT,
                 Z_SCALE_PULSES_PER_MM);
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 1
// =================================================================

// Define your hardware parameters here.
const int ENCODER_PPR = 1200; // 1200 step spindle optical rotary encoder.
                              // Fractional values not supported.
const int ENCODER_BACKLASH =
    3; // Numer of impulses encoder can issue without movement of the spindle

// Spindle rotary encoder pins. Swap values if the rotation direction is wrong.
#define ENC_A 14
#define ENC_B 13

// Main lead screw (Z) parameters.
const long SCREW_Z_DU =
    15000; // 1.5mm stock leadscrew in deci-microns (10^-7 of a meter)
const long MOTOR_STEPS_Z =
    1600; // number of steps it takes to rotate the leadscrew one full turn
const long SPEED_START_Z =
    MOTOR_STEPS_Z; // Initial speed of a motor, steps / second.
const long ACCELERATION_Z =
    25 * MOTOR_STEPS_Z; // Acceleration of a motor, steps / second ^ 2.
const long SPEED_MANUAL_MOVE_Z =
    8 * MOTOR_STEPS_Z; // Maximum speed of a motor during manual move, steps /
                       // second.
const bool INVERT_Z = false; // change (true/false) if the carriage moves e.g.
                             // "left" when you press "right".
const bool INVERT_Z_ENABLE =
    false; // change (true/false) if the Z axis enable pin is inverted
const bool NEEDS_REST_Z =
    false; // Set to false for closed-loop drivers, true for open-loop.
const long MAX_TRAVEL_MM_Z = 300; // Lathe bed doesn't allow to travel more than
                                  // this in one go, 30cm / ~1 foot
const long BACKLASH_DU_Z = 0; // 0mm backlash in deci-microns (10^-7 of a meter)
const char NAME_Z =
    'Z'; // Text shown on screen before axis position value, GCode axis name

// Cross-slide lead screw (X) parameters.
const long SCREW_X_DU =
    15000; // 1.5mm stock leadscrew in deci-microns (10^-7 of a meter)
const long MOTOR_STEPS_X = 1600; // now matches the Z, part of the fix to make
                                 // touchscreen jogging not skip increments
const long SPEED_START_X =
    MOTOR_STEPS_X; // Initial speed of a motor, steps / second.
const long ACCELERATION_X =
    25 * MOTOR_STEPS_X; // Acceleration of a motor, steps / second ^ 2.
const long SPEED_MANUAL_MOVE_X =
    8 * MOTOR_STEPS_X; // Maximum speed of a motor during manual move, steps /
                       // second.
const bool INVERT_X = true; // change (true/false) if the carriage moves e.g.
                            // "left" when you press "right".
const bool INVERT_X_ENABLE =
    false; // change (true/false) if the X axis enable pin is inverted
const bool NEEDS_REST_X = false; // Set to false for all kinds of drivers or X
                                 // will be unlocked when not moving.
const long MAX_TRAVEL_MM_X =
    100; // Cross slide doesn't allow to travel more than this in one go, 10cm
const long BACKLASH_DU_X =
    0; // 0.15mm backlash in deci-microns (10^-7 of a meter)
const char NAME_X =
    'X'; // Text shown on screen before axis position value, GCode axis name

// Manual stepping with left/right/up/down buttons. Only used when step isn't
// default continuous (1mm or 0.1").
const long STEP_TIME_MS =
    500; // Time in milliseconds it should take to make 1 manual step.
// GECKOSUB: if you increase the 80ms value it can get rid of the double and
// tripple stepping but only in the two smaller increment modes. Also, it will
// make the jogging in these modes slower.
const long DELAY_BETWEEN_STEPS_MS =
    80; // Time in milliseconds to wait between steps.

// Connect to WiFi and expose web UI to control and receive GCode.
const bool WIFI_ENABLED = true;
const char *SSID = "myaccesspoint_2_2";
const char *PASSWORD = "emilcfriis";
const long INCOMING_BUFFER_SIZE = 100000;
const long OUTGOING_BUFFER_SIZE = 100000;

/* Changing anything below shouldn't be needed for basic use. */

// Configuration for axis connected to Y. This is uncommon. Dividing head (C)
// motor parameters. Throughout the configuration below we assume 1mm = 1degree
// of rotation, so 1du = 0.0001degree.
const bool ACTIVE_Y = false; // Whether the axis is connected
const bool ROTARY_Y = true; // Whether the axis is rotary or linear
const long MOTOR_STEPS_Y = 300; // Number of motor steps for 1 rotation of the the worm gear screw (full step with 20:30 reduction)
const long SCREW_Y_DU = 20000; // Degrees multiplied by 10000 that the spindle travels per 1 turn of the worm gear. 2 degrees.
const long SPEED_START_Y = 1600; // Initial speed of a motor, steps / second.
const long ACCELERATION_Y = 16000; // Acceleration of a motor, steps / second ^ 2.
const long SPEED_MANUAL_MOVE_Y = 3200; // Maximum speed of a motor during manual move, steps / second.
const bool INVERT_Y = false; // change (true/false) if the carriage moves e.g. "left" when you press "right".
const bool INVERT_Y_ENABLE = false; // change (true/false) if the Y axis enable pin is inverted
const bool NEEDS_REST_Y = false; // Set to false for closed-loop drivers. Open-loop: true if you need holding torque, false otherwise.
const long MAX_TRAVEL_MM_Y = 360; // Probably doesn't make sense to ask the dividin head to travel multiple turns.
const long BACKLASH_DU_Y = 0; // Assuming no backlash on the worm gear
const char NAME_Y = 'Y'; // Text shown on screen before axis position value, GCode axis name

// Manual handwheels. Ignore if you don't have them installed.
const float PULSE_PER_REVOLUTION = 300; // PPR of handwheels. WAS 600, but new value moves Z 1mm per MPG/REV (GeckoSub)

const int ENCODER_STEPS_INT = ENCODER_PPR * 2; // Number of encoder impulses PCNT counts per revolution of the spindle
const int ENCODER_FILTER = 1; // Encoder pulses shorter than this will be ignored. Clock cycles, 1 - 1023.
const int PCNT_LIM = 31000; // Limit used in hardware pulse counter logic.
const int PCNT_CLEAR = 30000; // Limit where we reset hardware pulse counter value to avoid overflow. Less than PCNT_LIM.
const long DUPR_MAX = 254000; // No more than 1 inch pitch
const int32_t STARTS_MAX = 124; // No more than 124-start thread
const long PASSES_MAX = 999; // No more turn or face passes than this
const long SAFE_DISTANCE_DU = 5000; // Step back 0.5mm from the material when moving between cuts in automated modes
const long SAVE_DELAY_US = 5000000; // Wait 5s after last save and last change of saveable data before saving again
const long DIRECTION_SETUP_DELAY_US = 5; // Stepper driver needs some time to adjust to direction change
const long STEPPED_ENABLE_DELAY_MS = 100; // Delay after stepper is enabled and before issuing steps

// --- Standard UI Colors ---
#define COLOR_NUMPAD_DEFAULT 65535 // White
#define COLOR_INCR_COARSE 63488 // Red for most coarse Incremenent
#define COLOR_INCR_MID 64960 // Amber for medium Incremenent
#define COLOR_INCR_FINE 2016 // Green for fine Incremenent
#define COLOR_DRO_ON 2016 // Green for ON State for Dia/Rad and Abs/Inc touch buttons
#define COLOR_DRO_OFF 25388 // Grey for OFF State for Dia/Rad and Abs/Inc touch buttons

#define COLOR_GEAR 2016 // Green
#define COLOR_ASYNC 3542 // Tiffany Blue
#define COLOR_THREAD 63488 // Red
#define COLOR_TURN 64960 // Amber
#define COLOR_FACE 25388 // Light Grey
#define COLOR_CUT 25388 // Light Grey
#define COLOR_CONE 32784 // Purple
#define COLOR_ELLIPSE 63519 // Magenta / Fuchsia
#define COLOR_GCODE 25388 // Light Grey
#define COLOR_Y_AXIS 31 // Blue
#define COLOR_DEFAULT 65535 // White

// OTHER POTENTIAL COLORS:
// 64480 // Orange (not very vibrant)
// 63488 // Red
// 64960 // Amber
// 65535 // White
// 2016 // Green
// 25388 // Grey
// 0 // Black
// 31 // Blue
// 65504 // Yellow
// 2047 // Cyan
// 1023 // Cyan
// 63519 // Magenta / Fuchsia (a common bright pink)
// 32784 // Purple
// 59134 // Light Purple (Lavender)
// 18448 // Dark Purple (Indigo)
// 12554 // Dark Grey
// 30719 // Light Blue
// 36477 // Baby Blue, a bit washed out
// 1040 // Teal, a bit dark, not much teal-green in it
// 3542 // Tiffany Blue, fairly vibrant

#define B_KNOWN_DIA 204 // Virtual button for Known Diameter (X)
#define B_KNOWN_LENGTH 205 // Virtual button for Known Length (Z)

#define B_X_TOGGLE_DRO 206 // Toggle X between Stepper/Scale
#define B_Z_TOGGLE_DRO 207 // Toggle Z between Stepper/Scale
#define B_VERSION 208 // Toggle show/hide of FW version
#define B_SPINDLE_UNITS 209 // Toggle Spindle Units (RPM/Turns/Angle)
#define B_PITCH 210 // Pitch Touch Button (Short=Font, Long=Reverse)

enum NumpadMode { PITCH, DIAMETER, LENGTH };
NumpadMode numpadEntryMode = PITCH; // Default to entering pitch
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 3
// =================================================================

// =================================================================
// Geckosub + Gemini 2.5 Pro - SECTION 4, Abs/Incr Mode Variables
// =================================================================
bool isIncrementalMode = false; // Default to absolute mode
float xScaleIncrementalOffset = 0.0f; // Stores the temporary zero for incremental measurements
float zScaleIncrementalOffset = 0.0f; // Stores the temporary zero for Z incremental measurements

// CHANGED: Set default to false (White/ELS/Stepper) instead of true
// (Green/DRO/Scale)
bool zDisplayModeDRO = false; // true=Show Z-Scale, false=Show Z-Stepper
bool xDisplayModeDRO = false; // true=Show X-Scale, false=Show X-Stepper

bool spindleValColorGreen = false; // true=Green Spindle Text, false=White
bool lastSignWasNegative = false; // Remembers sign context for Fine Pitch at 0

// --- Color Override Flags ---
bool xColorInverted = false; // Inverts the calculated color for X
bool zColorInverted = false; // Inverts the calculated color for Z

// --- Pitch Font Variables ---
int pitchFontList[] = {16, 4, 15}; // Cycle: 16 -> 4 -> 15 -> 16
int pitchFontIdx = 0; // Default index 0 (Font 16)

// --- Version Display Variables ---
bool versionVisible = true;
// Is the version currently shown?
unsigned long versionTimerStart = 0; // When did we show it?
// --- Volatile Flags for State Machines ---
// These MUST be here because we removed them from Section 8
volatile bool buttonZPressed = false;
volatile bool buttonXPressed = false;
volatile bool buttonStopLPressed = false;
volatile bool buttonStopRPressed = false;
volatile bool buttonPitchPressed = false; // Flag for Pitch Button
volatile bool buttonOnePressed = false; // Flag for '1' Button
volatile bool buttonXEnaPressed = false; // NEW: Flag for X Enable Button
volatile bool buttonZEnaPressed = false; // NEW: Flag for Z Enable Button
volatile bool buttonDisplayPressed = false; // NEW: Flag for Spindle Value Button
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 4
// =================================================================

// Version of the pref storage format, should be changed when
// non-backward-compatible changes are made to the storage logic, resulting in
// Preferences wipe on first start.
#define PREFERENCES_VERSION 1
#define PREF_NAMESPACE "h5"

// GCode-related constants.
const float LINEAR_INTERPOLATION_PRECISION = 0.1; // 0 < x <= 1, smaller values make for quicker G0 and G1 moves
const long GCODE_WAIT_EPSILON_STEPS = 10;
const bool SPINDLE_PAUSES_GCODE = true; // pause GCode execution when spindle stops
const int GCODE_MIN_RPM = 30; // pause GCode execution if RPM is below this

// To be changed whenever a different PCB / encoder / stepper / ... design is
// used.
#define HARDWARE_VERSION 5
#define NANOELS_STRINGIFY_VALUE(value) #value
#define NANOELS_STRINGIFY(value) NANOELS_STRINGIFY_VALUE(value)
#define CONTROLLER_VERSION_TEXT "H" NANOELS_STRINGIFY(HARDWARE_VERSION) "V" NANOELS_STRINGIFY(SOFTWARE_VERSION)
// =================================================================
// ACTIVE MODE SELECTION, TABLE/CONFIGURATION
// =================================================================
// Set these to 'true' to include a mode in the cycle, or 'false' to hide it.

const bool ENABLE_MODE_GEAR = true;
const bool ENABLE_MODE_TURN = true;
const bool ENABLE_MODE_FACE = true;
const bool ENABLE_MODE_CONE = true;
const bool ENABLE_MODE_CUT = true;
const bool ENABLE_MODE_THREAD = true;
const bool ENABLE_MODE_ELLIPSE = true;
const bool ENABLE_MODE_GCODE = true;
const bool ENABLE_MODE_ASYNC = true;
const bool ENABLE_MODE_Y = false;

#define Z_ENA 41
#define Z_DIR 42
#define Z_STEP 35

#define Z_PULSE_A 18
#define Z_PULSE_B 8

#define X_ENA 16
#define X_DIR 15
#define X_STEP 7

#define X_PULSE_A 47
#define X_PULSE_B 21

#define Y_ENA 1
#define Y_DIR 2
#define Y_STEP 17

#define Y_PULSE_A 45
#define Y_PULSE_B 48

#define KEY_DATA 37
// #define KEY_CLOCK 36

// BUTTON TABLE - the name in red can be used to map push buttons
// Where to define the push button pins and commands? Search for "PIN AND BUTTON
// DEFINITIONS"
#define B_LEFT 21 // Left arrow - controls Z axis movement to the left
#define B_RIGHT 22 // Right arrow - controls Z axis movement to the right
#define B_UP 23 // Up arrow - controls X axis movement forwards
#define B_DOWN 24 // Down arrow - controls X axis movement backwards
#define B_FORWARD 85 // u - Advance Y axis
#define B_BACK 74 // j - Retreat Y axis
#define B_MINUS 60 // Numpad minus - recrements the pitch or number of passes
#define B_PLUS 95 // Numpad plus - increments the pitch or number of passes
#define B_ON 30 // Enter - starts operation or mode
#define B_OFF 27 // ESC - stops operation or mode
#define B_STOPL 65 // a - sets left stop
#define B_STOPR 68 // d - sets right stop
#define B_STOPU 87 // w - sets forward stop
#define B_STOPD 83 // s - sets rear stop
#define B_STOPF 73 // i - Y forward stop
#define B_STOPB 75 // k - Y backward stop
#define B_MULTISTART 84 // t - multi-start thread button (GeckoSub, it was 83 before and possibly colliding with B_STOPD)
#define B_DISPL 12 // Win - changes info displayed in the bottom line (angle, rpm, ...)
#define B_STEP 64 // Tilda - changes distance moved when movement buttons are used
#define B_MEASURE 77 // m - controls metric / imperial / tpi
#define B_REVERSE 82 // r - changes pitch sign (left / right thread)
#define B_DIAMETER 79 // o - sets X0 so that centerline is at the middle of a given diameter value
#define B_0 48 // 0 top row - for number entry
#define B_1 49 // 1 top row
#define B_2 50 // ...
#define B_3 51
#define B_4 52
#define B_5 53
#define B_6 54
#define B_7 55
#define B_8 56
#define B_9 57
#define B_BACKSPACE 28 // removes the last entered number
#define B_MODE_GEARS 97 // F1 - sets the mode to gearbox
#define B_MODE_TURN 98 // F2 - ...
#define B_MODE_FACE 99 // F3
#define B_MODE_CONE 100 // F4
#define B_MODE_CUT 101 // F5
#define B_MODE_THREAD 102 // F6
#define B_MODE_ASYNC 103 // F7
#define B_MODE_ELLIPSE 104 // F8
#define B_MODE_GCODE 105 // F9
#define B_MODE_Y 106 // F10
#define B_MODE 107 // F11 - to cycle through modes
#define B_X 88 // x - zeroes X axi
#define B_Z 90 // z - zeroes Z axis
#define B_Y 89 // y - zeroes Y axis
#define B_X_ENA 67 // c - enables / disables X axis
#define B_Z_ENA 81 // q - enables / disables Z axis
#define B_Y_ENA 72 // h - enables / disables Y axis

#define B_SET_DIA 201 // Virtual button for setting a known diameter, for DRO use
#define B_DIA_RAD 202 // Virtual button for toggling Diameter/Radius mode, for DRO
#define B_ABS_INC 203 // Virtual button for toggling Absolute/Incremental mode, for DRO
#define B_KNOWN_LENGTH 204 // Virtual button for setting an absolute Z, for DRO

#define PREF_VERSION "v"
// #define PREF_DUPR "d"
#define PREF_DUPR_GEAR "dg" // NEW
#define PREF_DUPR_THREAD "dt" // NEW
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
#define PREF_SHOW_ANGLE "ang"
#define PREF_SHOW_TACHO "rpm"
#define PREF_STARTS "sta"
#define PREF_MODE "mod"
#define PREF_MEASURE "mea"
#define PREF_CONE_RATIO "cr"
#define PREF_TURN_PASSES "tp"
#define PREF_MOVE_STEP "ms"
#define PREF_AUX_FORWARD "af"

#define MOVE_STEP_1 10000 // 1mm
#define MOVE_STEP_2 1000 // 0.1mm
#define MOVE_STEP_3 100 // 0.01mm

#define MOVE_STEP_IMP_1 25400 // 1/10"
#define MOVE_STEP_IMP_2 2540 // 1/100"
#define MOVE_STEP_IMP_3 254 // 1/1000" also known as 1 thou

#define MODE_NORMAL 0
#define MODE_ASYNC 2
#define MODE_CONE 3
#define MODE_TURN 4
#define MODE_FACE 5
#define MODE_CUT 6
#define MODE_THREAD 7
#define MODE_ELLIPSE 8
#define MODE_GCODE 9
#define MODE_Y 10

#define MEASURE_METRIC 0
#define MEASURE_INCH 1
#define MEASURE_TPI 2

#define ESTOP_NONE 0
#define ESTOP_POS 2
#define ESTOP_MARK_ORIGIN 3
#define ESTOP_ON_OFF 4
#define ESTOP_OFF_MANUAL_MOVE 5

#define TIMER_FREQ 1000000 // 1MHz async timer frequency

// --- Mode Color Array ---
// This array maps each mode's number to its defined color.
const uint16_t MODE_COLORS[11] = {[MODE_NORMAL] = COLOR_GEAR,
                                  [1] = COLOR_DEFAULT, // Unused Mode
                                  [MODE_ASYNC] = COLOR_ASYNC,
                                  [MODE_CONE] = COLOR_CONE,
                                  [MODE_TURN] = COLOR_TURN,
                                  [MODE_FACE] = COLOR_FACE,
                                  [MODE_CUT] = COLOR_CUT,
                                  [MODE_THREAD] = COLOR_THREAD,
                                  [MODE_ELLIPSE] = COLOR_ELLIPSE,
                                  [MODE_GCODE] = COLOR_GCODE,
                                  [MODE_Y] = COLOR_Y_AXIS};

// =================================================================
// FINE PITCH ENCODER CONFIG
// =================================================================
// Set to 'true' to enable the feature, 'false' to disable it completely.
const bool FINE_PITCH_ENCODER_ENABLED = true;

// Add or remove modes from this list to control where the encoder is active.
// (MODE_NORMAL is for GEAR mode)
const int FINE_PITCH_ACTIVE_MODES[] = {MODE_NORMAL, MODE_ASYNC};
// Helper to calculate the size of the list automatically
const int NUM_FINE_PITCH_ACTIVE_MODES = sizeof(FINE_PITCH_ACTIVE_MODES) / sizeof(FINE_PITCH_ACTIVE_MODES[0]);
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 6
// =================================================================

// =================================================================
// SECTION 7, ACTIVE MODE SELECTION, TABLE/CONFIGURATION
// =================================================================
// Set these to 'true' to include a mode in the cycle (show on the display), or
// 'false' to hide it.
// const bool ENABLE_MODE_ASYNC = true;
// const bool ENABLE_MODE_GEAR = true;
// const bool ENABLE_MODE_TURN = true;
// const bool ENABLE_MODE_THREAD = true;
// const bool ENABLE_MODE_FACE = false;
// const bool ENABLE_MODE_CUT = false;
// const bool ENABLE_MODE_CONE = false;
// const bool ENABLE_MODE_ELLIPSE = false;
// const bool ENABLE_MODE_GCODE = false;
// const bool ENABLE_MODE_Y = false;

// --- Dynamic list of active ELS modes ---
int activeModes[11]; // Max possible modes is 11 (0-10)
int numActiveModes = 0;

// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 7
// =================================================================

struct CircleBuffer {
  char *buffer;
  size_t head;
  size_t tail;
  size_t size;
};

#include <FS.h>
#include <LittleFS.h>
#include <PS2KeyAdvanced.h> // install via Libraries as "PS2KeyAdvanced"
#include <Preferences.h>
#include <WebServer.h>
#include <WebSocketsServer.h> // install via Libraries as "WebSockets"
#include <WiFi.h>
#include <driver/pulse_cnt.h>

// #include <driver/pcnt.h>

// =================================================================
// NEW APPLICATION STATE MACHINE
// =================================================================
enum AppState { STATE_NORMAL, STATE_CHANGE_MODE };
AppState currentAppState = STATE_NORMAL;
// =================================================================

// =================================================================
// FUNCTION FORWARD DECLARATIONS
// =================================================================
struct Axis; // Add this line to inform the compiler that the Axis struct exists
void resetNumpad();
void setDupr(long value);
void setModeFromTask(int value);
void buttonMoveStepPress();
void buttonLeftStopPress(Axis *a);
// =================================================================

#include "index.html.h"

#define FORMAT_LITTLEFS_IF_FAILED true

// For MEASURE_TPI, round TPI to the nearest integer if it's within this range
// of it. E.g. 80.02tpi would be shown as 80tpi but 80.04tpi would be shown
// as-is.
const float TPI_ROUND_EPSILON = 0.03;

const float ENCODER_STEPS_FLOAT = ENCODER_STEPS_INT; // Convenience float version of ENCODER_STEPS_INT
const long RPM_BULK = ENCODER_STEPS_INT; // Measure RPM averaged over this number of encoder pulses

const long GCODE_FEED_DEFAULT_DU_SEC = 20000; // Default feed in du/sec in GCode mode
const float GCODE_FEED_MIN_DU_SEC = 167; // Minimum feed in du/sec in GCode mode - F1

#define DHIGH(x) digitalWrite(x, HIGH)
#define DLOW(x) digitalWrite(x, LOW)

#define DELAY(x) vTaskDelay(x / portTICK_PERIOD_MS);

#define LCD_HASH_INITIAL -3845709 // Random number that's unlikely to naturally occur as an actual hash
long lcdHashLine0 = LCD_HASH_INITIAL;
long lcdHashLine1 = LCD_HASH_INITIAL;
long lcdHashLine2 = LCD_HASH_INITIAL;
long lcdHashLine3 = LCD_HASH_INITIAL;
bool splashScreen = false;

// ADD THIS LINE
int16_t finePitchPulseCount = 0; // Tracks the count for the fine pitch encoder
unsigned long g_comboActionLastActive = 0; // Tracks when the hold+turn combo was last used
unsigned long keypadTimeUs = 0;

// Most buttons we only have "down" handling, holding them has no effect.
// Buttons with special "holding" logic have flags below.
bool buttonLeftPressed = false;
bool buttonRightPressed = false;
bool buttonUpPressed = false;
bool buttonDownPressed = false;
bool buttonOffPressed = false;
bool buttonBackPressed = false;
bool buttonForwardPressed = false;
volatile bool buttonMinusPressed = false; // GECKOSUB, trying to make +/- buttons work with hold down mode
volatile bool buttonPlusPressed = false; // GECKOSUB, trying to make +/- buttons work with hold down mode

// =================================================================
// Long Press Logic
// =================================================================
const unsigned long LONG_PRESS_DURATION_MS = 400; //  LONG PRESS timer in msec FOR LEFT LIMIT TO DELETE ALL LIMITS
// (buttonStopLPressed is defined in Section 4)
enum StopL_State { STATE_IDLE, STATE_WAITING, STATE_ACTION_DONE };
StopL_State stopL_state = STATE_IDLE;
unsigned long stopL_pressStartTime = 0; // Stopwatch for the long press

// --- Right Stop State Variables (Reusing StopL_State enum) ---
StopL_State stopR_state = STATE_IDLE;
unsigned long stopR_pressStartTime = 0; // Stopwatch for the Right Stop long press
// State tracking for B_X (Known Diameter)
// (buttonXPressed is defined in Section 4)
enum ButtonX_State { B_X_IDLE, B_X_WAITING, B_X_ACTION_DONE };
ButtonX_State buttonX_state = B_X_IDLE;
unsigned long buttonX_pressStartTime = 0; // Stopwatch for the B_X long press

// State tracking for B_Z (Known Length)
// (buttonZPressed is defined in Section 4)
enum ButtonZ_State { B_Z_IDLE, B_Z_WAITING, B_Z_ACTION_DONE };
ButtonZ_State buttonZ_state = B_Z_IDLE;
unsigned long buttonZ_pressStartTime = 0;

// --- State tracking for B_X_ENA and B_Z_ENA ---
enum ButtonEna_State { ENA_IDLE, ENA_WAITING, ENA_ACTION_DONE };
ButtonEna_State buttonXEna_state = ENA_IDLE;
unsigned long buttonXEna_pressStartTime = 0;

ButtonEna_State buttonZEna_state = ENA_IDLE;
unsigned long buttonZEna_pressStartTime = 0;

// --- NEW: State tracking for B_DISPL (Spindle Value) ---
enum ButtonDisplay_State { DISP_IDLE, DISP_WAITING, DISP_ACTION_DONE };
ButtonDisplay_State buttonDisplay_state = DISP_IDLE;
unsigned long buttonDisplay_pressStartTime = 0;
// =================================================================
// END: LONG PRESS STATE VARIABLES
// =================================================================

// =================================================================
// Dual-Duty Button Logic
// =================================================================
#define B_NUMPAD 200 // Invent a new, unique keycode for the Numpad toggle button
const unsigned long NUMPAD_ENTER_LONG_PRESS_MS = 300;
// Long press for B_0 to act as Enter
bool physicalNumpadModeActive = false;
// The main flag for the numpad mode

// --- State tracking for the '0' / Enter button ---
volatile bool buttonZeroPressed = false; // Tracks if the '0' button is currently held down
enum ButtonZeroState { B0_STATE_IDLE, B0_STATE_WAITING, B0_STATE_ACTION_DONE };
ButtonZeroState buttonZero_state = B0_STATE_IDLE;
unsigned long buttonZero_pressStartTime = 0;

// --- State tracking for the '5' / Stop button ---
const unsigned long NUMPAD_OFF_LONG_PRESS_MS = 300;
// Long press for B_5 to act as B_OFF
volatile bool buttonFivePressed = false; // Tracks if the '5' button is currently held down
enum ButtonFiveState { B5_STATE_IDLE, B5_STATE_WAITING, B5_STATE_ACTION_DONE };
ButtonFiveState buttonFive_state = B5_STATE_IDLE;
unsigned long buttonFive_pressStartTime = 0;

// --- State tracking for the '2' / Numpad Toggle button ---
const unsigned long NUMPAD_TOGGLE_LONG_PRESS_MS = 300;
// Long press for B_2 to toggle Numpad
volatile bool buttonTwoPressed = false;
// Tracks if the '2' button is currently held down
enum ButtonTwoState { B2_STATE_IDLE, B2_STATE_WAITING, B2_STATE_ACTION_DONE };
ButtonTwoState buttonTwo_state = B2_STATE_IDLE;
unsigned long buttonTwo_pressStartTime = 0;

// =================================================================
// END OF SECTION
// =================================================================

bool inNumpad = false;
int numpadDigits[20];
int numpadIndex = 0;
bool isOn = false;

bool nextIsOn; // isOn value that should be applied asap
bool nextIsOnFlag; // whether nextIsOn requires attention
unsigned long resetMillis = 0;
int emergencyStop = 0;

bool beepFlag = false;

long dupr = 0;
// pitch, tenth of a micron per rotation
long gearDupr = 0; // NEW: Stores pitch for non-thread modes
long threadDupr = 0; // NEW: Stores pitch for thread mode
long savedGearDupr = 0; // NEW
long savedThreadDupr = 0; // NEW
long nextDupr = dupr;
// dupr value that should be applied asap
bool nextDuprFlag = false; // whether nextDupr requires attention

SemaphoreHandle_t motionMutex; // controls blocks of code where variables affecting the motion loop() are changed

int starts = 1; // number of starts in a multi-start thread
int savedStarts = 0; // starts saved in Preferences
int nextStarts = starts; // number of starts that should be used asap
bool nextStartsFlag = false; // whether nextStarts requires attention

struct Axis {
  SemaphoreHandle_t mutex;
  char name;
  bool active;
  bool rotational;
  float motorSteps; // motor steps per revolution of the axis
  float screwPitch; // lead screw pitch in deci-microns (10^-7 of a meter)
  long pos; // relative position of the tool in stepper motor steps
  long savedPos; // value saved in Preferences
  float fractionalPos; // fractional distance in steps that we meant to travel but couldn't
  long originPos; // relative position of the stepper motor to origin, in steps
  long savedOriginPos; // originPos saved in Preferences
  long posGlobal; // global position of the motor in steps
  long savedPosGlobal; // posGlobal saved in Preferences
  int pendingPos; // steps of the stepper motor that we should make as soon as possible
  long motorPos; // position of the motor in stepper motor steps, same as pos unless moving back, then differs by backlashSteps
  long savedMotorPos; // motorPos saved in Preferences
  bool continuous; // whether current movement is expected to continue until an unknown position
  long leftStop; // left stop value of pos
  long savedLeftStop; // value saved in Preferences
  long nextLeftStop; // left stop value that should be applied asap
  bool nextLeftStopFlag; // whether nextLeftStop required attention
  long rightStop; // right stop value of pos
  long savedRightStop; // value saved in Preferences
  long nextRightStop; // right stop value that should be applied asap
  bool nextRightStopFlag; // whether nextRightStop requires attention

  long speed; // motor speed in steps / second
  long speedStart; // Initial speed of a motor, steps / second.
  long speedMax; // To limit max speed e.g. for manual moves
  long speedManualMove; // Maximum speed of a motor during manual move, steps / second.
  long acceleration; // Acceleration of a motor, steps / second ^ 2.
  long decelerateSteps; // Number of steps before the end position the deceleration should start.

  bool direction; // To reset speed when direction changes.
  bool directionInitialized;
  unsigned long stepStartUs;
  int stepperEnableCounter;
  bool disabled;
  bool savedDisabled;

  bool invertStepper; // change (true/false) if the carriage moves e.g. "left" // when you press "right".
  bool invertEnable; // change (true/false) if the Enable pin is inverted
  bool needsRest; // set to false for closed-loop drivers, true for open-loop.
  bool movingManually; // whether stepper is being moved by left/right buttons
  long estopSteps; // amount of steps to exceed machine limits
  long backlashSteps; // amount of steps in reverse direction to re-engage the carriage
  long gcodeRelativePos; // absolute position in steps that relative GCode refers to

  int ena; // Enable pin of this motor
  int dir; // Direction pin of this motor
  int step; // Step pin of this motor

  int pulseA;
  int pulseB;
  pcnt_unit_t pulseUnit;
  int pulseCount;
};

void initAxis(Axis *a, char name, bool active, bool rotational,
              float motorSteps, float screwPitch, long speedStart,
              long speedManualMove, long acceleration, bool invertStepper,
              bool invertEnable, bool needsRest, long maxTravelMm,
              long backlashDu, int ena, int dir, int step, int pulseA,
              int pulseB, pcnt_unit_t pulseUnit) {
  a->mutex = xSemaphoreCreateMutex();

  a->name = name;
  a->active = active;
  a->rotational = rotational;
  a->motorSteps = motorSteps;
  a->screwPitch = screwPitch;

  a->pos = 0;
  a->savedPos = 0;
  a->fractionalPos = 0.0;
  a->originPos = 0;
  a->savedOriginPos = 0;
  a->posGlobal = 0;
  a->savedPosGlobal = 0;
  a->pendingPos = 0;
  a->motorPos = 0;
  a->savedMotorPos = 0;
  a->continuous = false;

  a->leftStop = 0;
  a->savedLeftStop = 0;
  a->nextLeftStopFlag = false;

  a->rightStop = 0;
  a->savedRightStop = 0;
  a->nextRightStopFlag = false;

  a->speed = speedStart;
  a->speedStart = speedStart;
  a->speedMax = LONG_MAX;
  a->speedManualMove = speedManualMove;
  a->acceleration = acceleration;
  a->decelerateSteps = 0;
  long s = speedManualMove;
  while (s > speedStart) {
    a->decelerateSteps++;
    s -= a->acceleration / float(s);
  }

  a->direction = true;
  a->directionInitialized = false;
  a->stepStartUs = 0;
  a->stepperEnableCounter = 0;
  a->disabled = false;
  a->savedDisabled = false;

  a->invertStepper = invertStepper;
  a->invertEnable = invertEnable;
  a->needsRest = needsRest;
  a->movingManually = false;
  a->estopSteps = maxTravelMm * 10000 / a->screwPitch * a->motorSteps;
  a->backlashSteps = backlashDu * a->motorSteps / a->screwPitch;
  a->gcodeRelativePos = 0;

  a->ena = ena;
  a->dir = dir;
  a->step = step;

  a->pulseA = pulseA;
  a->pulseB = pulseB;
  a->pulseUnit = pulseUnit;
  a->pulseCount = 0;
}

Axis z;
Axis x;
Axis y;

unsigned long saveTime = 0; // micros() of the previous Prefs write
unsigned long spindleEncTime = 0; // micros() of the previous spindle update
unsigned long spindleEncTimeDiffBulk = 0; // micros() between RPM_BULK spindle updates
unsigned long spindleEncTimeAtIndex0 = 0; // micros() when spindleEncTimeIndex was 0
int spindleEncTimeIndex = 0; // counter going between 0 and RPM_BULK - 1
long spindlePos = 0; // Spindle position
long spindlePosAvg = 0; // Spindle position accounting for encoder backlash
long savedSpindlePosAvg = 0; // spindlePosAvg saved in Preferences
long savedSpindlePos = 0; // spindlePos value saved in Preferences
int spindleCount = 0; // Last processed spindle encoder pulse counter value.
int spindlePosSync = 0; // Non-zero if gearbox is on and a soft limit was removed while axis was on it
int savedSpindlePosSync = 0; // spindlePosSync saved in Preferences
long spindlePosGlobal = 0; // global spindle position that is unaffected by e.g. zeroing
long savedSpindlePosGlobal = 0; // spindlePosGlobal saved in Preferences

int spindleDisplayMode = 0; // 0=RPM, 1=Turns, 2=Angle (Geckosub + Gemini for share Spindle Value Text Box)
bool isDiameterMode = true; // Default to diameter mode, which is most common, DRO
bool savedShowAngle = false; // showAngle value saved in Preferences
bool savedShowTacho = false; // showTacho value saved in Preferences
int shownRpm = 0;
unsigned long shownRpmTime = 0; // micros() when shownRpm was set

// --- DRO UI State Variables ---

long moveStep = 0; // thousandth of a mm
long savedMoveStep = 0; // moveStep saved in Preferences

volatile int mode = -1; // mode of operation (ELS, multi-start ELS, asynchronous)
int nextMode = 0; // mode value that should be applied asap
bool nextModeFlag = false; // whether nextMode needs attention
int savedMode = -1; // mode saved in Preferences

int measure = MEASURE_METRIC; // Whether to show distances in inches
int savedMeasure = MEASURE_METRIC; // measure value saved in Preferences

float coneRatio = 1; // In cone mode, how much X moves for 1 step of Z
float savedConeRatio = 0; // value of coneRatio saved in Preferences
float nextConeRatio = 0; // coneRatio that should be applied asap
bool nextConeRatioFlag = false; // whether nextConeRatio requires attention

int turnPasses = 3; // In turn mode, how many turn passes to make
int savedTurnPasses = 0; // value of turnPasses saved in Preferences

long setupIndex = 0; // Index of automation setup step
bool auxForward = true; // True for external, false for internal thread
bool savedAuxForward = false; // value of auxForward saved in Preferences

long opIndex = 0; // Index of an automation operation
bool opIndexAdvanceFlag = false; // Whether user requested to move to the next pass
long opSubIndex = 0; // Sub-index of an automation operation
int opDuprSign = 1; // 1 if dupr was positive when operation started, -1 if negative
long opDupr = 0; // dupr that the multi-pass operation started with

bool awaitingButtonPress = false; // SEMI-AUTO THREAD MODE. Flag to indicate the system is waiting for user input to continue a pass

String gcodeCommand = "";
long gcodeFeedDuPerSec = GCODE_FEED_DEFAULT_DU_SEC;
bool gcodeInitialized = false;
bool gcodeAbsolutePositioning = true;
bool gcodeInBrace = false;
bool gcodeInSemicolon = false;
bool wsInKeycode = false;
volatile int wsKeycode = 0;
String keycodeCommand = "";
bool gcodeInSave = false;
bool gcodeInSaveFirstLine = false;
String gcodeSaveName = "";
String gcodeSaveValue = "";
int gcodeProgramIndex = 0;
int gcodeProgramCount = 0;
String gcodeProgram = "";
int gcodeProgramCharIndex = 0;

PS2KeyAdvanced keyboard;

hw_timer_t *async_timer = timerBegin(TIMER_FREQ);
bool timerAttached = false;

CircleBuffer inBuffer;
CircleBuffer outBuffer;

bool bufferAvailable(CircleBuffer *b) { return b->head != b->tail; }

bool writeBuffer(CircleBuffer *b, char c) {
  if ((b->head + 1) % b->size == b->tail) {
    return false;
  }
  b->buffer[b->head] = c;
  b->head = (b->head + 1) % b->size;
  return true;
}

bool writeBuffer(CircleBuffer *b, const char *str) {
  while (*str) {
    if (!writeBuffer(b, *str++)) {
      return false;
    }
  }
  return true;
}

bool writeBuffer(CircleBuffer *b, const String &str) {
  return writeBuffer(b, str.c_str());
}

bool writeBuffer(CircleBuffer *b, float f, int precision) {
  char buffer[16];
  dtostrf(f, 0, precision, buffer);
  return writeBuffer(b, buffer);
}

char shiftBuffer(CircleBuffer *b) {
  if (b->head == b->tail) {
    return 0;
  }
  char c = b->buffer[b->tail];
  b->tail = (b->tail + 1) % b->size;
  return c;
}

void initBuffer(CircleBuffer *b, size_t size) {
  b->size = size;
  b->buffer = (char *)malloc(size);
  b->head = 0;
  b->tail = 0;
}

void clearBuffer(CircleBuffer *b) {
  b->head = 0;
  b->tail = 0;
}

WebServer server(80);
WebSocketsServer webSocket(81);
String wifiStatus = "WiFi not connected";

// =================================================================
// BUTTON DEFINITIONS (REVISED FOR NUMPAD) This is where you map your GPIO pins
// to keycodes.
// =================================================================
struct PhysicalButton {
  const uint8_t PIN;
  const int normalKeycode; // The button's function in normal mode
  const int numpadKeycode; // The button's function when numpad is active
  bool lastReading;
  bool confirmedState;
  unsigned long lastDebounceTime;
};

// PIN AND BUTTON DEFINITIONS
// The number is the digital pin. The "B_something" is the button it connects
// to. Search for "BUTTON TABLE" in the code to see the list of commands

PhysicalButton physicalButtons[] = {
    // Pin, Pinout, Normal Function, Numpad Function, {Initial States...}
    {38, B_LEFT, B_8, HIGH, HIGH, 0}, // JOG LEFT -> NUM 8
    {6, B_RIGHT, B_9, HIGH, HIGH, 0}, // JOG RIGHT -> NUM 9
    {5, B_PLUS, B_1, HIGH, HIGH, 0}, // PLUS -> NUM 1
    {4, B_MINUS, B_6, HIGH, HIGH, 0}, // MINUS -> NUM 6
    {10, B_MODE, B_4, HIGH, HIGH, 0}, // MODE -> NUM 4
    {11, B_STEP, B_7, HIGH, HIGH, 0}, // STEP -> NUM 7
    {48, B_OFF, B_5, HIGH, HIGH, 0}, // STOP -> NUM 5
    {45, B_ON, B_0, HIGH, HIGH, 0}, // RUN -> NUM 0
    {3, B_KNOWN_DIA, B_2, HIGH, HIGH, 0}, // KNOWN DIA -> NUM 2
    {46, B_REVERSE, B_3, HIGH, HIGH, 0}, // JOG REVERSE -> NUM 3

    // NOTE: Pin 36 has been removed from this array to be handled by a custom
    // state machine.
};
const int NUM_PHYSICAL_BUTTONS = sizeof(physicalButtons) / sizeof(PhysicalButton);
const unsigned long BUTTON_DEBOUNCE_DELAY = 50;
// =================================================================
// END OF SECTION
// =================================================================

void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
  if (type == WStype_TEXT) {
    for (size_t i = 0; i < length; i++) {
      writeBuffer(&inBuffer, payload[i]);
    }
  }
}

void handleClientRequests() { server.send(200, "text/html", indexhtml); }

int getGcodeProgramCount() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return 0;
  }

  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();
    if (filename.endsWith(".gcode")) {
      count++;
    }
    file = root.openNextFile();
  }

  return count;
}

bool saveGcode() {
  if (gcodeSaveName.length() < 2) {
    writeBuffer(&outBuffer, "error: name must be at least 2 chars\n");
    return false;
  }
  if (gcodeSaveValue.length() < 2) {
    writeBuffer(&outBuffer, "error: program too short\n");
    return false;
  }

  String filename = "/" + gcodeSaveName + ".gcode";
  File file = LittleFS.open(filename, "w");
  if (!file) {
    writeBuffer(&outBuffer, "error: failed to open file\n");
    return false;
  }

  file.print(gcodeSaveValue);
  file.close();

  writeBuffer(&outBuffer, "success: G-code saved\n");
  gcodeProgramCount = getGcodeProgramCount();
  return true;
}

bool removeGcodeByName(const String &name) {
  if (name.length() == 0)
    return false;

  String filename = "/" + name + ".gcode";
  if (!LittleFS.exists(filename)) {
    writeBuffer(&outBuffer, "error: file not found\n");
    return false;
  }

  if (!LittleFS.remove(filename)) {
    writeBuffer(&outBuffer, "error: failed to delete " + filename + "\n");
    return false;
  }

  writeBuffer(&outBuffer, "success: " + name + " deleted\n");
  gcodeProgramCount = getGcodeProgramCount();
  return true;
}

String readGcodeProgram(const String &name) {
  String filename = "/" + name + ".gcode";
  File file = LittleFS.open(filename, "r");
  if (!file) {
    return "";
  }

  String result;
  result.reserve(file.size());
  char buf[64];
  while (file.available()) {
    size_t bytesRead = file.readBytes(buf, sizeof(buf));
    result += String(buf).substring(0, bytesRead);
  }
  file.close();
  return result;
}

void handleGcodeAdd() {
  if (server.hasArg("name") && server.hasArg("gcode")) {
    gcodeSaveName = server.arg("name");
    gcodeSaveValue = server.arg("gcode");
    if (saveGcode()) {
      server.send(200, "text/plain", "G-code saved successfully");
    } else {
      server.send(500, "text/plain", "Failed to save G-code");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

void handleGcodeList() {
  String response = "";
  File root = LittleFS.open("/");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      String filename = file.name();
      if (filename.endsWith(".gcode")) {
        response += filename.substring(0, filename.length() - 6) + "\n";
      }
      file.close();
      file = root.openNextFile();
    }
  }
  server.send(200, "text/plain", response);
}

void handleGcodeGet() {
  if (server.hasArg("name")) {
    String gcode = readGcodeProgram(server.arg("name"));
    if (gcode != "") {
      server.send(200, "text/plain", gcode);
    } else {
      server.send(404, "text/plain", "G-code file not found");
    }
  } else {
    server.send(400, "text/plain", "Missing parameter: name");
  }
}

void handleGcodeRemove() {
  if (server.hasArg("name")) {
    if (removeGcodeByName(server.arg("name"))) {
      server.send(200, "text/plain", "G-code removed successfully");
    } else {
      server.send(500, "text/plain", "Failed to remove G-code");
    }
  } else {
    server.send(400, "text/plain", "Missing parameter: name");
  }
}

void handleStatus() {
  server.send(200, "text/plain",
              "LittleFS.freeSpace=" +
                  String(LittleFS.totalBytes() - LittleFS.usedBytes()) + "\n");
}

void taskWiFi(void *param) {
  WiFi.begin(SSID, PASSWORD);
  wifiStatus = "Connecting to WiFi";
  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED)
      break;
    DELAY(500);
    taskYIELD();
  }
  if (WiFi.status() != WL_CONNECTED) {
    if (WiFi.status() == WL_NO_SSID_AVAIL) {
      wifiStatus = "No SSID available";
    } else if (WiFi.status() == WL_CONNECT_FAILED) {
      wifiStatus = "WiFi connection failed";
    } else if (WiFi.status() == WL_CONNECTION_LOST) {
      wifiStatus = "WiFi connection lost";
    } else if (WiFi.status() == WL_DISCONNECTED) {
      wifiStatus = "WiFi disconnected"; // Likely wrong password
    } else {
      wifiStatus = "WiFi unknown error";
    }
    vTaskDelete(NULL);
    return;
  }
  wifiStatus = "See " + WiFi.localIP().toString();

  initBuffer(&inBuffer, 1024);
  initBuffer(&outBuffer, 1024);

  server.on("/", handleClientRequests);
  server.on("/gcode/add", HTTP_POST, handleGcodeAdd);
  server.on("/gcode/list", HTTP_GET, handleGcodeList);
  server.on("/gcode/get", HTTP_GET, handleGcodeGet);
  server.on("/gcode/remove", HTTP_POST, handleGcodeRemove);
  server.on("/status", HTTP_GET, handleStatus);
  server.begin();

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);

  while (emergencyStop == ESTOP_NONE) {
    server.handleClient();
    webSocket.loop();

    if (bufferAvailable(&outBuffer)) {
      String outData = "";
      while (bufferAvailable(&outBuffer)) {
        outData += shiftBuffer(&outBuffer);
      }
      webSocket.broadcastTXT(outData);
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}

void setAsyncTimerEnable(bool value) {
  if (value) {
    timerStart(async_timer);
  } else {
    timerStop(async_timer);
  }
}

void setEmergencyStop(int kind) {
  emergencyStop = kind;
  setAsyncTimerEnable(false);
  xSemaphoreTake(z.mutex, 10);
  xSemaphoreTake(x.mutex, 10);
  xSemaphoreTake(y.mutex, 10);
}

void updateEnable(Axis *a) {
  if (!a->disabled && (!a->needsRest || a->stepperEnableCounter > 0)) {
    digitalWrite(a->ena, a->invertEnable ? LOW : HIGH);
    // Stepper driver needs some time before it will react to pulses.
    DELAY(STEPPED_ENABLE_DELAY_MS);
  } else {
    digitalWrite(a->ena, a->invertEnable ? HIGH : LOW);
  }
}

void stepperEnable(Axis *a, bool value) {
  if (!a->needsRest || !a->active) {
    return;
  }
  if (value) {
    a->stepperEnableCounter++;
    if (value == 1) {
      updateEnable(a);
    }
  } else if (a->stepperEnableCounter > 0) {
    a->stepperEnableCounter--;
    if (a->stepperEnableCounter == 0) {
      updateEnable(a);
    }
  }
}

void markAxisOrigin(Axis *a) {
  bool hasSemaphore = xSemaphoreTake(a->mutex, 10) == pdTRUE;
  if (!hasSemaphore) {
    beepFlag = true;
  }
  if (a->leftStop != LONG_MAX) {
    a->leftStop -= a->pos;
  }
  if (a->rightStop != LONG_MIN) {
    a->rightStop -= a->pos;
  }
  a->motorPos -= a->pos;
  a->originPos += a->pos;
  a->pos = 0;
  a->fractionalPos = 0;
  a->pendingPos = 0;
  if (hasSemaphore) {
    xSemaphoreGive(a->mutex);
  }
}

void zeroSpindlePos() {
  spindlePos = 0;
  spindlePosAvg = 0;
  spindlePosSync = 0;
}

// Loose the thread and mark current physical positions of
// encoder and stepper as a new 0. To be called when dupr changes
// or ELS is turned on/off. Without this, changing dupr will
// result in stepper rushing across the lathe to the new position.
// Must be called while holding motionMutex.
void markOrigin() {
  markAxisOrigin(&z);
  markAxisOrigin(&x);
  markAxisOrigin(&y);
  zeroSpindlePos();
}

void markAxis0(Axis *a) { a->originPos = -a->pos; }

void setIsOnFromTask(bool on) {
  nextIsOn = on;
  nextIsOnFlag = true;
}

void setIsOnFromLoop(bool on) {
  if (isOn && on) {
    return;
  }
  if (!on) {
    isOn = false;
    setupIndex = 0;
  }
  stepperEnable(&z, on);
  stepperEnable(&x, on);
  stepperEnable(&y, on);
  markOrigin();
  if (on) {
    isOn = true;
    opDuprSign = dupr >= 0 ? 1 : -1;
    opDupr = dupr;
    opIndex = 0;
    opIndexAdvanceFlag = false;
    opSubIndex = 0;
    setupIndex = 0;
  }
}

int getApproxRpm() {
  unsigned long t = micros();
  unsigned long elapsedTime = t - spindleEncTime;
  if (elapsedTime > 50000) {
    // RPM less than 10.
    spindleEncTimeDiffBulk = 0;
    return 0;
  }
  int rpm = 0;
  if (spindleEncTimeDiffBulk > 0) {
    rpm = 60000000 / spindleEncTimeDiffBulk;
    if (abs(rpm - shownRpm) > (rpm < 1000 ? 3 : 5)) {
      // Don't update RPM with insignificant differences.
      shownRpm = rpm;
      shownRpmTime = t;
    }
  }
  return rpm;
}

bool stepperIsRunning(Axis *a) { return micros() - a->stepStartUs < 50000; }

void toScreen(const String &command) {
  Serial1.print(command);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
  Serial1.write(0xFF);
}

void setText(const String &id, const String &text) {
  // New logic: Check if we are setting the mode text
  if (id == "bMode") {
    // If so, automatically set the correct color from our array
    toScreen("bMode.pco=" + String(MODE_COLORS[mode]));
  }
  // Original line to set the text content
  toScreen(id + ".txt=\"" + text + "\"");
}

// Returns number of letters printed.
// Geckosub, always 2 decimals in metric, always 3 in imperial
// but not sure if there's any  issues as this ChatGPT code...

// Smart version (used for numpad so that T3 pitch input doesn't get rounded)
String printDeciMicrons(long deciMicrons, int precisionPointsMax) {
  bool imperial = measure != MEASURE_METRIC;
  long v = imperial ? round(deciMicrons / 25.4) : deciMicrons;

  int points = 0;
  if (v == 0 && precisionPointsMax >= 5) {
    points = 5;
  } else if ((v % 10) != 0 && precisionPointsMax >= 4) {
    points = 4;
  } else if ((v % 100) != 0 && precisionPointsMax >= 3) {
    points = 3;
  } else if ((v % 1000) != 0 && precisionPointsMax >= 2) {
    points = 2;
  } else if ((v % 10000) != 0 && precisionPointsMax >= 1) {
    points = 1;
  }

  // 🔑 Force at least 2 decimals if allowed (metric only)
  if (!imperial && points < 2 && precisionPointsMax >= 2) {
    points = 2;
  }

  return String(deciMicrons / (imperial ? 254000.0 : 10000.0), points);
}

// ✅ Fixed version (used for axis positions / stop distances)
String printDeciMicronsFixed(long deciMicrons) {
  bool imperial = (measure != MEASURE_METRIC);
  int points = imperial ? 3 : 2; // Imperial → 3 decimals, Metric → 2 decimals
  return String(deciMicrons / (imperial ? 254000.0 : 10000.0), points);
}

String printDegrees(long degrees10000) {
  int points = 0;
  if ((degrees10000 % 100) != 0) {
    points = 3;
  } else if ((degrees10000 % 1000) != 0) {
    points = 2;
  } else if ((degrees10000 % 10000) != 0) {
    points = 1;
  }
  return String(degrees10000 / 10000.0, points) + char(223); // degree symbol
}

String printDupr(long value) {
  if (measure != MEASURE_TPI)
    return printDeciMicrons(value, 5);

  float tpi = 254000.0 / value;
  String result = "";
  if (abs(tpi - round(tpi)) < TPI_ROUND_EPSILON) {
    result = String(int(round(tpi)));
  } else {
    int tpi100 = round(tpi * 100);
    int points = 0;
    if ((tpi100 % 10) != 0) {
      points = 2;
    } else if ((tpi100 % 100) != 0) {
      points = 1;
    }
    result = String(tpi, points);
  }
  return result + "tpi";
}

long stepsToDu(Axis *a, long steps) {
  return round(steps * a->screwPitch / a->motorSteps);
}

long getAxisPosDu(Axis *a) { return stepsToDu(a, a->pos + a->originPos); }

long getAxisStopDiffDu(Axis *a) {
  if (a->leftStop == LONG_MAX || a->rightStop == LONG_MIN)
    return 0;
  return stepsToDu(a, a->leftStop - a->rightStop);
}

String printAxisPos(Axis *a) {
  if (a->rotational)
    return printDegrees(getAxisPosDu(a));
  return printDeciMicronsFixed(getAxisPosDu(
      a)); // fixed decimals: 2 for metric, 3 for imperial, geckosub
}

long getAxisLeftStopDistanceDu(Axis *a) {
  return stepsToDu(a, a->leftStop - a->pos);
}

String printDistanceToLeftStop(Axis *a) {
  if (a->leftStop == LONG_MAX)
    return "";
  if (a->rotational)
    return printDegrees(getAxisLeftStopDistanceDu(a));
  return printDeciMicronsFixed(getAxisLeftStopDistanceDu(
      a)); // fixed decimals: 2 for metric, 3 for imperial, geckosub
}

long getAxisRightStopDistanceDu(Axis *a) {
  return stepsToDu(a, a->pos - a->rightStop);
}

String printDistanceToRightStop(Axis *a) {
  if (a->rightStop == LONG_MIN)
    return "";
  if (a->rotational)
    return printDegrees(getAxisRightStopDistanceDu(a));
  return printDeciMicronsFixed(getAxisRightStopDistanceDu(a));
}

String printAxisStopDiff(Axis *a, bool addTrailingSpace) {
  String result = "";
  if (a->rotational) {
    result = printDegrees(getAxisStopDiffDu(a));
  } else {
    result = printDeciMicrons(getAxisStopDiffDu(a),
                              3); // geckosub, didn't touch this yet
  }
  return addTrailingSpace ? result + ' ' : result;
}

String printNoTrailing0(float value) {
  long v = round(value * 100000);
  int points = 0;
  if ((v % 10) != 0) {
    points = 5;
  } else if ((v % 100) != 0) {
    points = 4;
  } else if ((v % 1000) != 0) {
    points = 3;
  } else if ((v % 10000) != 0) {
    points = 2;
  } else if ((v % 100000) != 0) {
    points = 1;
  }
  return String(value, points);
}

bool needZStops() {
  return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD ||
         mode == MODE_ELLIPSE;
}

bool isPassMode() {
  return mode == MODE_TURN || mode == MODE_FACE || mode == MODE_CUT ||
         mode == MODE_THREAD || mode == MODE_ELLIPSE;
}

bool manualMovesAllowedWhenOn() {
  return mode == MODE_NORMAL || mode == MODE_ASYNC || mode == MODE_CONE ||
         mode == MODE_Y;
}

bool manualMovesIgnoredWhenOn() { return mode == MODE_GCODE; }

int getLastSetupIndex() {
  if (mode == MODE_CONE || mode == MODE_GCODE)
    return 2;
  if (mode == MODE_THREAD)
    return 4;
  if (mode == MODE_TURN || mode == MODE_FACE || mode == MODE_CUT ||
      mode == MODE_ELLIPSE)
    return 3;
  return 0;
}

Axis *getPitchAxis() { return mode == MODE_FACE ? &x : &z; }

long getPassModeZStart() {
  if (mode == MODE_TURN || mode == MODE_THREAD)
    return dupr > 0 ? z.rightStop : z.leftStop;
  if (mode == MODE_FACE)
    return auxForward ? z.rightStop : z.leftStop;
  if (mode == MODE_ELLIPSE)
    return dupr > 0 ? z.leftStop : z.rightStop;
  return z.pos;
}

long getPassModeXStart() {
  if (mode == MODE_TURN || mode == MODE_THREAD)
    return auxForward ? x.rightStop : x.leftStop;
  if (mode == MODE_FACE || mode == MODE_CUT)
    return dupr > 0 ? x.rightStop : x.leftStop;
  if (mode == MODE_ELLIPSE)
    return x.rightStop;
  return x.pos;
}

long getNumpadResult() {
  long result = 0;
  for (int i = 0; i < numpadIndex; i++) {
    result += numpadDigits[i] * pow(10, numpadIndex - 1 - i);
  }
  return result;
}

float numpadToConeRatio() { return getNumpadResult() / 100000.0; }

long numpadToDeciMicrons() {
  long result = getNumpadResult();
  if (result == 0) {
    return 0;
  }
  if (measure == MEASURE_INCH) {
    result = result * 254;
  } else if (measure == MEASURE_TPI) {
    result = round(254000.0 / result);
  } else { // Metric
    // CHANGED: Multiplier from 10 to 100 so 5-5-5 becomes 5.55mm (55500 du)
    // instead of 0.555mm
    result = result * 100;
  }
  return result;
}

long spindleModulo(long value) {
  value = value % ENCODER_STEPS_INT;
  if (value < 0) {
    value += ENCODER_STEPS_INT;
  }
  return value;
}

bool removeAllGcode() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return false;
  }

  File file = root.openNextFile();
  while (file) {
    String path = file.path();
    file.close(); // can't remove while open
    if (path.endsWith(".gcode")) {
      if (!LittleFS.remove(path)) {
        writeBuffer(&outBuffer, "error: failed to delete " + path + "\n");
      }
    }
    file = root.openNextFile();
  }

  gcodeProgramCount = getGcodeProgramCount();
  return true;
}

String getCurrentGcodeProgramName() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return "";
  }

  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();
    if (filename.endsWith(".gcode")) {
      if (count == gcodeProgramIndex) {
        return filename.substring(0, filename.length() - 6);
      }
      count++;
    }
    file = root.openNextFile();
  }

  return "";
}

String printMode() {
  if (mode == MODE_NORMAL)
    return "GEAR";
  if (mode == MODE_ASYNC)
    return "ASYNC";
  if (mode == MODE_CONE)
    return "CONE";
  if (mode == MODE_TURN)
    return "TURN";
  if (mode == MODE_FACE)
    return "FACE";
  if (mode == MODE_CUT)
    return "CUT";
  if (mode == MODE_THREAD)
    return "THREAD";
  if (mode == MODE_ELLIPSE)
    return "ELLIP";
  if (mode == MODE_GCODE)
    return "GCODE";
  if (mode == MODE_Y)
    return "Y";
  return "";
}

unsigned long lastDisplayUpdateTime = 0;

// =================================================================
// Display Update Logic
// =================================================================
void updateDisplay() {
  if (millis() - lastDisplayUpdateTime < 100)
    return;
  lastDisplayUpdateTime = millis();

  // --- NEW: Auto-hide Version Text ---
  if (versionVisible && (millis() - versionTimerStart > 5000)) {
    versionVisible = false;
    setText("tVersion", "");
  }

  long newHashLine0 = isOn + spindlePosSync + mode + measure + dupr + starts +
                      currentAppState + (lastSignWasNegative ? 1 : 0);
  if (lcdHashLine0 != newHashLine0) {
    lcdHashLine0 = newHashLine0;
    if (spindlePosSync)
      setText("bStatus", "SYN");
    else
      setText("bStatus", isOn ? "ON" : "OFF");
    setText("bMode", printMode());

    const long MAX_FINE_PITCH_DU = 3000;
    String bPitchText;
    if (measure == MEASURE_METRIC && dupr >= 0 && dupr <= MAX_FINE_PITCH_DU) {
      bPitchText = String(dupr / 10000.0, 2);
    } else {
      bPitchText = printDupr(dupr);
    }

    if (starts != 1)
      bPitchText += " x" + String(starts);
    setText("tPitch", bPitchText);

    // --- NEW PITCH COLOR LOGIC ---
    if (dupr > 0) {
      toScreen("tPitch.pco=65535"); // Positive = White
    } else if (dupr < 0) {
      toScreen("tPitch.pco=63488"); // Negative = Red
    } else {
      // It's 0, so check the context
      if (lastSignWasNegative) {
        toScreen("tPitch.pco=63488"); // Red "0"
      } else {
        toScreen("tPitch.pco=65535"); // White "0"
      }
    }

    setText("bMeasure", measure == MEASURE_INCH ? "in" : measure == MEASURE_METRIC ? "mm" : "TPI");
  }

  long newHashLine1 = moveStep + measure;
  if (lcdHashLine1 != newHashLine1) {
    lcdHashLine1 = newHashLine1;
    setText("tStepVal", printDeciMicrons(moveStep, 5));

    if (moveStep == MOVE_STEP_1 || moveStep == MOVE_STEP_IMP_1) {
      toScreen("tStepVal.pco=" + String(COLOR_INCR_COARSE));
      toScreen("tStepVal.pco2=" + String(COLOR_INCR_COARSE));
    } else if (moveStep == MOVE_STEP_2 || moveStep == MOVE_STEP_IMP_2) {
      toScreen("tStepVal.pco=" + String(COLOR_INCR_MID));
      toScreen("tStepVal.pco2=" + String(COLOR_INCR_MID));
    } else {
      toScreen("tStepVal.pco=" + String(COLOR_INCR_FINE));
      toScreen("tStepVal.pco2=" + String(COLOR_INCR_FINE));
    }
  }

  // --- Z-Axis Display String (Calculated BEFORE hash check) ---
  String zPosStr = "";
  if (z.active && !z.disabled) {
    float zVal = 0.0f;
    // 1. Get Raw Value in MM
    if (zDisplayModeDRO) {
      zVal = zScale.getPosition();
    } else {
      // Convert stepper deci-microns to mm
      zVal = getAxisPosDu(&z) / 10000.0f;
    }

    // 2. Apply Incremental Offset
    if (isIncrementalMode) {
      zVal -= zScaleIncrementalOffset;
    }

    // 3. Format to String
    if (measure != MEASURE_METRIC) {
      zPosStr = String(zVal / 25.4, 3);
    } else {
      zPosStr = String(zVal, 2);
    }
  }
  // ---

  // --- Calculate zPosStr hash manually ---
  long zPosStrHash = 0;
  for (int i = 0; i < zPosStr.length(); i++) {
    zPosStrHash += zPosStr.charAt(i);
  }
  // ---

  long newHashLine2 =
      long(xScale.getPosition() * 1000) + (isDiameterMode ? 1 : 0) +
      (isIncrementalMode ? 2 : 0) + x.disabled + x.leftStop - x.rightStop +
      z.pos + z.originPos + z.disabled + z.leftStop - z.rightStop + y.pos +
      y.originPos + y.disabled + y.leftStop - y.rightStop + measure +
      (zDisplayModeDRO ? 100 : 0) + (xDisplayModeDRO ? 200 : 0) + zPosStrHash +
      (long)(zScaleIncrementalOffset * 1000) +
      (long)(xScaleIncrementalOffset * 1000) + x.pos + x.originPos +
      (xColorInverted ? 500 : 0) + (zColorInverted ? 1000 : 0);
  // Added Override flags to hash

  if (lcdHashLine2 != newHashLine2) {
    lcdHashLine2 = newHashLine2;
    String xPosStr = "";
    if (x.active && !x.disabled) {
      // --- NEW X-AXIS LOGIC ---
      float xPos = 0.0f;
      if (xDisplayModeDRO) {
        xPos = xScale.getPosition();
      } else {
        xPos = getAxisPosDu(&x) / 10000.0f;
      }

      if (isIncrementalMode) {
        xPos -= xScaleIncrementalOffset;
      }
      if (isDiameterMode) {
        xPos *= 2.0f;
      }
      if (measure != MEASURE_METRIC) {
        xPosStr = String(xPos / 25.4, 3);
      } else {
        xPosStr = String(xPos, 2);
      }
    }
    setText("tX", xPosStr);
    // --- New X-Axis Color Logic with Override ---
    bool showXGreen = xDisplayModeDRO;
    if (xColorInverted)
      showXGreen = !showXGreen; // Apply override

    if (showXGreen) {
      toScreen("tX.pco=" + String(COLOR_DRO_ON)); // Green
    } else {
      toScreen("tX.pco=" + String(COLOR_NUMPAD_DEFAULT)); // White
    }
    // ------------------------------

    setText("tXUp", !x.active || x.disabled ? "" : ("\r\n\r\n" + printDistanceToLeftStop(&x) + "  "));
    setText("tXDown", !x.active || x.disabled ? "" : ("\r\n\r\n" + printDistanceToRightStop(&x) + "  "));
    setText("tY", !y.active || y.disabled ? "" : printAxisPos(&y));
    setText("tYUp", !y.active || y.disabled ? "" : printDistanceToLeftStop(&y));
    setText("tYDown", !y.active || y.disabled ? "" : printDistanceToRightStop(&y));
    // --- New Z-Axis Display Logic ---
    setText("tZ", zPosStr);

    // --- New Z-Axis Color Logic with Override ---
    bool showZGreen = zDisplayModeDRO;
    if (zColorInverted)
      showZGreen = !showZGreen; // Apply override

    if (showZGreen) {
      toScreen("tZ.pco=" + String(COLOR_DRO_ON)); // Green
    } else {
      toScreen("tZ.pco=" + String(COLOR_NUMPAD_DEFAULT)); // White
    }
    // --- End New Z-Axis Logic ---

    setText("tZLeft", !z.active || z.disabled ? "" : ("\r\n\r\n" + printDistanceToLeftStop(&z) + "  "));
    setText("tZRight", !z.active || z.disabled ? "" : ("\r\n\r\n" + printDistanceToRightStop(&z) + "  "));

    if (isDiameterMode) {
      toScreen("tDia.pco=" + String(COLOR_DRO_ON));
      toScreen("tDia.font=3");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(COLOR_DRO_OFF));
      toScreen("tRad.font=2");
      setText("tRad", "rad");
    } else {
      toScreen("tDia.pco=" + String(COLOR_DRO_OFF));
      toScreen("tDia.font=2");
      setText("tDia", "dia");
      toScreen("tRad.pco=" + String(COLOR_DRO_ON));
      toScreen("tRad.font=3");
      setText("tRad", "rad");
    }

    if (isIncrementalMode) {
      toScreen("tInc.pco=" + String(COLOR_DRO_ON));
      toScreen("tInc.font=3");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(COLOR_DRO_OFF));
      toScreen("tAbs.font=2");
      setText("tAbs", "abs");
    } else {
      toScreen("tInc.pco=" + String(COLOR_DRO_OFF));
      toScreen("tInc.font=2");
      setText("tInc", "inc");
      toScreen("tAbs.pco=" + String(COLOR_DRO_ON));
      toScreen("tAbs.font=3");
      setText("tAbs", "abs");
    }
  }

  long numpadResult = getNumpadResult();
  long gcodeCommandHash = 0;
  for (int i = 0; i < gcodeCommand.length(); i++) {
    gcodeCommandHash += gcodeCommand.charAt(i);
  }
  for (int i = 0; i < wifiStatus.length(); i++) {
    gcodeCommandHash += wifiStatus.charAt(i);
  }
  bool spindleStopped = micros() > spindleEncTime + 100000;
  int rpm = getApproxRpm();
  long newHashLine3 =
      z.pos + spindlePos + rpm + spindleDisplayMode + measure +
      (numpadResult > 0 ? numpadResult : -1) + mode * 5 + dupr +
      (mode == MODE_CONE ? round(coneRatio * 10000) : 0) + turnPasses +
      opIndex + setupIndex + gcodeProgramIndex + gcodeProgramCount +
      spindleStopped * 3 + (isOn ? 139 : -117) + (inNumpad ? 10 : 0) +
      (auxForward ? 17 : -31) + (z.leftStop == LONG_MAX ? 123 : z.leftStop) +
      (z.rightStop == LONG_MIN ? 1234 : z.rightStop) +
      (spindleValColorGreen ? 300 : 0) + // Added Color Flag to Hash

      (x.leftStop == LONG_MAX ? 1235 : x.leftStop) +
      (x.rightStop == LONG_MIN ? 123456 : x.rightStop) + gcodeCommandHash +
      (mode == MODE_Y
           ? y.pos + y.originPos + (y.leftStop == LONG_MAX ? 123 : y.leftStop) +
                 (y.rightStop == LONG_MIN ? 1234 : y.rightStop) + y.disabled
           : 0) +
      x.pos + z.pos + numpadEntryMode;
  if (lcdHashLine3 != newHashLine3) {
    lcdHashLine3 = newHashLine3;
    float turns;
    switch (spindleDisplayMode) {
    case 0:
      setText("tSpindleTog", "RPM");
      setText("tSpindleVal", String(rpm));
      break;
    case 1:
      setText("tSpindleTog", "Turns");
      turns = (float)abs(spindlePos) / ENCODER_STEPS_INT;
      setText("tSpindleVal", String(turns, turns < 100 ? 2 : (turns < 1000 ? 1 : 0)));
      break;
    case 2:
      setText("tSpindleTog", "Angle");
      setText("tSpindleVal", String(spindleModulo(spindlePos) * 360 / ENCODER_STEPS_FLOAT, 1));
      break;
    }

    // --- NEW: Spindle Value Color Update ---
    if (spindleValColorGreen) {
      toScreen("tSpindleVal.pco=" + String(COLOR_DRO_ON));
      // Green
    } else {
      toScreen("tSpindleVal.pco=" + String(COLOR_NUMPAD_DEFAULT));
      // White
    }
    // ---------------------------------------

    String result = "";
    if (inNumpad && result == "") {
      // --- NEW: Conditional Divisor and Precision for Metric vs Imperial ---
      float divisor = (measure == MEASURE_METRIC) ? 100.0f : 1000.0f;
      int precision = (measure == MEASURE_METRIC) ? 2 : 3;
      // ---------------------------------------------------------------------

      if (numpadEntryMode == DIAMETER) {
        // Changed: Use dynamic divisor
        float displayVal = getNumpadResult() / divisor;
        String unit = (measure == MEASURE_METRIC) ? "mm" : "in";
        // Changed: Use dynamic precision
        result = "set dia: " + String(displayVal, precision) + unit + "?";
      } else if (numpadEntryMode == LENGTH) {
        // Changed: Use dynamic divisor
        float displayVal = getNumpadResult() / divisor;
        String unit = (measure == MEASURE_METRIC) ? "mm" : "in";
        // Changed: Use dynamic precision
        result = "set Z: " + String(displayVal, precision) + unit + "?";
      } else {
        // Pitch Logic: Handle TPI vs Standard
        if (measure == MEASURE_TPI) {
          result = "set pitch: " + printDupr(numpadToDeciMicrons()) + "?";
        } else {
          // Use dynamic precision here as well (2 for mm, 3 for in)
          result = "set pitch: " +
                   printDeciMicrons(numpadToDeciMicrons(), precision) + "?";
        }
      }
    }

    if (result == "") {
      if (mode == MODE_GCODE) {
        if (setupIndex == 1 && gcodeProgramCount == 0) {
          result = "No stored programs";
        } else if (setupIndex == 1) {
          if (gcodeProgramIndex >= gcodeProgramCount) {
            result = "Program deleted";
          } else {
            result = getCurrentGcodeProgramName();
          }
        } else if (setupIndex == 2) {
          if (spindleStopped)
            result = "Turn on the spindle!";
          else
            result = "Spindle on. Go?";
        } else if (isOn) {
          result = gcodeCommand.substring(0, 20);
        }
      } else if (isPassMode()) {
        bool missingZStops =
            needZStops() && (z.leftStop == LONG_MAX || z.rightStop == LONG_MIN);
        bool missingStops =
            missingZStops || x.leftStop == LONG_MAX || x.rightStop == LONG_MIN;
        if (!inNumpad && missingStops) {
          result = needZStops() ? "Set all stops" : "Set X stops";
        } else if (numpadResult != 0 && setupIndex == 1) {
          long passes = min(PASSES_MAX, numpadResult);
          result = String(passes) + (passes == 1 ? " pass?" : " passes?");
        } else if (!isOn && setupIndex == 1) {
          result =
              String(turnPasses) + (turnPasses == 1 ? " pass?" : " passes?");
        } else if (!isOn && setupIndex == 2) {
          if (mode == MODE_FACE) {
            result = auxForward ? "Right to left?" : "Left to right?";
          } else if (mode == MODE_CUT) {
            result = dupr >= 0 ? "Pitch > 0, external" : "Pitch < 0, internal";
          } else {
            result = auxForward ? "External?" : "Internal?";
          }
        } else if (mode == MODE_THREAD && !isOn && setupIndex == 3) {
          result = "Cone ratio " + String(numpadToConeRatio(), 5) + "?";
        } else if (!isOn && setupIndex == getLastSetupIndex()) {
          long zOffset = getPassModeZStart() - z.pos;
          long xOffset = getPassModeXStart() - x.pos;
          result = "Go";
          if (zOffset != 0) {
            result += " " + String(z.name) + printDeciMicrons(stepsToDu(&z, zOffset), 2);
          }
          if (xOffset != 0) {
            result += " " + String(x.name) + printDeciMicrons(stepsToDu(&x, xOffset), 2);
          }
          result += "?";
        } else if (isOn && numpadResult == 0) {
          result = "Pass " + String(opIndex) + " of " + String(max(opIndex, long(turnPasses * starts)));
        }
      } else if (mode == MODE_CONE) {
        if (numpadResult != 0 && setupIndex == 1)
          result = "Use ratio " + String(numpadToConeRatio(), 5) + "?";
        else if (!isOn && setupIndex == 1)
          result = "Use ratio " + printNoTrailing0(coneRatio) + "?";
        else if (!isOn && setupIndex == 2)
          result = auxForward ? "External?" : "Internal?";
        else if (!isOn && setupIndex == 3)
          result = "Go?";
        else if (isOn && numpadResult == 0)
          result = "Cone ratio " + printNoTrailing0(coneRatio);
      }
    }

    if (result == "")
      result = wifiStatus;
    setText("t3", result);
  }
}
// =================================================================
// END OF SECTION
// =================================================================

bool saveIfChanged() {
  // Should avoid calling Preferences whenever possible to reduce memory wear
  // and avoid ~20ms write delay that blocks interrupts.
  if (gearDupr == savedGearDupr && threadDupr == savedThreadDupr &&
      starts == savedStarts && z.pos == z.savedPos &&
      z.originPos == z.savedOriginPos && z.posGlobal == z.savedPosGlobal &&
      z.motorPos == z.savedMotorPos && z.leftStop == z.savedLeftStop &&
      z.rightStop == z.savedRightStop && z.disabled == z.savedDisabled &&
      spindlePos == savedSpindlePos && spindlePosAvg == savedSpindlePosAvg &&
      spindlePosSync == savedSpindlePosSync &&
      savedSpindlePosGlobal == spindlePosGlobal && moveStep == savedMoveStep &&
      mode == savedMode && measure == savedMeasure && x.pos == x.savedPos &&
      x.originPos == x.savedOriginPos && x.posGlobal == x.savedPosGlobal &&
      x.motorPos == x.savedMotorPos && x.leftStop == x.savedLeftStop &&
      x.rightStop == x.savedRightStop && x.disabled == x.savedDisabled &&
      y.pos == y.savedPos && y.originPos == y.savedOriginPos &&
      y.posGlobal == y.savedPosGlobal && y.motorPos == y.savedMotorPos &&
      y.leftStop == y.savedLeftStop && y.rightStop == y.savedRightStop &&
      y.disabled == y.savedDisabled && coneRatio == savedConeRatio &&
      turnPasses == savedTurnPasses && savedAuxForward == auxForward)
    return false;

  Preferences pref;
  pref.begin(PREF_NAMESPACE);

  if (gearDupr != savedGearDupr)
    pref.putLong(PREF_DUPR_GEAR, savedGearDupr = gearDupr);
  if (threadDupr != savedThreadDupr)
    pref.putLong(PREF_DUPR_THREAD, savedThreadDupr = threadDupr);
  if (starts != savedStarts)
    pref.putInt(PREF_STARTS, savedStarts = starts);

  if (z.pos != z.savedPos)
    pref.putLong(PREF_POS_Z, z.savedPos = z.pos);
  if (z.posGlobal != z.savedPosGlobal)
    pref.putLong(PREF_POS_GLOBAL_Z, z.savedPosGlobal = z.posGlobal);
  if (z.originPos != z.savedOriginPos)
    pref.putLong(PREF_ORIGIN_POS_Z, z.savedOriginPos = z.originPos);
  if (z.motorPos != z.savedMotorPos)
    pref.putLong(PREF_MOTOR_POS_Z, z.savedMotorPos = z.motorPos);
  if (z.leftStop != z.savedLeftStop)
    pref.putLong(PREF_LEFT_STOP_Z, z.savedLeftStop = z.leftStop);
  if (z.rightStop != z.savedRightStop)
    pref.putLong(PREF_RIGHT_STOP_Z, z.savedRightStop = z.rightStop);
  if (z.disabled != z.savedDisabled)
    pref.putBool(PREF_DISABLED_Z, z.savedDisabled = z.disabled);

  if (spindlePos != savedSpindlePos)
    pref.putLong(PREF_SPINDLE_POS, savedSpindlePos = spindlePos);
  if (spindlePosAvg != savedSpindlePosAvg)
    pref.putLong(PREF_SPINDLE_POS_AVG, savedSpindlePosAvg = spindlePosAvg);
  if (spindlePosSync != savedSpindlePosSync)
    pref.putInt(PREF_OUT_OF_SYNC, savedSpindlePosSync = spindlePosSync);
  if (spindlePosGlobal != savedSpindlePosGlobal)
    pref.putLong(PREF_SPINDLE_POS_GLOBAL,
                 savedSpindlePosGlobal = spindlePosGlobal);

  if (moveStep != savedMoveStep)
    pref.putLong(PREF_MOVE_STEP, savedMoveStep = moveStep);
  if (mode != savedMode)
    pref.putInt(PREF_MODE, savedMode = mode);
  if (measure != savedMeasure)
    pref.putInt(PREF_MEASURE, savedMeasure = measure);

  if (x.pos != x.savedPos)
    pref.putLong(PREF_POS_X, x.savedPos = x.pos);
  if (x.posGlobal != x.savedPosGlobal)
    pref.putLong(PREF_POS_GLOBAL_X, x.savedPosGlobal = x.posGlobal);
  if (x.originPos != x.savedOriginPos)
    pref.putLong(PREF_ORIGIN_POS_X, x.savedOriginPos = x.originPos);
  if (x.motorPos != x.savedMotorPos)
    pref.putLong(PREF_MOTOR_POS_X, x.savedMotorPos = x.motorPos);
  if (x.leftStop != x.savedLeftStop)
    pref.putLong(PREF_LEFT_STOP_X, x.savedLeftStop = x.leftStop);
  if (x.rightStop != x.savedRightStop)
    pref.putLong(PREF_RIGHT_STOP_X, x.savedRightStop = x.rightStop);
  if (x.disabled != x.savedDisabled)
    pref.putBool(PREF_DISABLED_X, x.savedDisabled = x.disabled);

  if (y.pos != y.savedPos)
    pref.putLong(PREF_POS_Y, y.savedPos = y.pos);
  if (y.posGlobal != y.savedPosGlobal)
    pref.putLong(PREF_POS_GLOBAL_Y, y.savedPosGlobal = y.posGlobal);
  if (y.originPos != y.savedOriginPos)
    pref.putLong(PREF_ORIGIN_POS_Y, y.savedOriginPos = y.originPos);
  if (y.motorPos != y.savedMotorPos)
    pref.putLong(PREF_MOTOR_POS_Y, y.savedMotorPos = y.motorPos);
  if (y.leftStop != y.savedLeftStop)
    pref.putLong(PREF_LEFT_STOP_Y, y.savedLeftStop = y.leftStop);
  if (y.rightStop != y.savedRightStop)
    pref.putLong(PREF_RIGHT_STOP_Y, y.savedRightStop = y.rightStop);
  if (y.disabled != y.disabled)
    pref.putBool(PREF_DISABLED_Y, y.savedDisabled = y.disabled);

  if (coneRatio != savedConeRatio)
    pref.putFloat(PREF_CONE_RATIO, savedConeRatio = coneRatio);
  if (turnPasses != savedTurnPasses)
    pref.putInt(PREF_TURN_PASSES, savedTurnPasses = turnPasses);
  if (auxForward != savedAuxForward)
    pref.putBool(PREF_AUX_FORWARD, savedAuxForward = auxForward);
  pref.end();
  return true;
}

void beep() {
  // TODO
}

void taskDisplay(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    updateDisplay();
    // Calling Preferences.commit() blocks all interrupts for 30ms, don't call
    // saveIfChanged() if encoder is likely to move soon.
    unsigned long now = micros();
    if (!stepperIsRunning(&z) && !stepperIsRunning(&x) &&
        (now > spindleEncTime + SAVE_DELAY_US) &&
        (now < saveTime || now > saveTime + SAVE_DELAY_US) &&
        (now < keypadTimeUs || now > keypadTimeUs + SAVE_DELAY_US)) {
      if (saveIfChanged()) {
        saveTime = now;
      }
    }
    if (beepFlag) {
      beepFlag = false;
      beep();
    }
    if (abs(z.pendingPos) > z.estopSteps || abs(x.pendingPos) > x.estopSteps) {
      setEmergencyStop(ESTOP_POS);
    }
    taskYIELD();
  }
  setText("bMode", "ESTOP");
  if (emergencyStop == ESTOP_POS) {
    setText("t3", "Requested position outside machine");
  } else if (emergencyStop == ESTOP_MARK_ORIGIN) {
    setText("t3", "Unable to mark origin");
  } else if (emergencyStop == ESTOP_ON_OFF) {
    setText("t3", "Unable to turn on/off");
  } else if (emergencyStop == ESTOP_OFF_MANUAL_MOVE) {
    setText("t3", "Off during manual move");
  }
  vTaskDelete(NULL);
}

void setMeasure(int value) {
  if (measure == value) {
    return;
  }
  measure = value;
  moveStep = measure == MEASURE_METRIC ? MOVE_STEP_1 : MOVE_STEP_IMP_1;

  if (measure == MEASURE_METRIC) // --- NEW: Force Pitch Font based on Unit ---
    pitchFontIdx = 0; // Default to Font 16 for Metric
  else
    pitchFontIdx = 1; // Default to Font 4 for Inch/TPI
  toScreen("tPitch.font=" +
           String(pitchFontList[pitchFontIdx])); // Apply the font immediately
}

// FIX: Increased division from 3 to 10 to wait longer before accepting a new
// button press. This prevents "double-jumping" on touchscreens where release
// latency is high. (Jog issue)
void waitForPendingPosNear0(Axis *a) {
  // GECKOSUB CHANGE: Wait until 90% done (div 10) instead of 66% done (div 3)
  while (abs(a->pendingPos) > a->motorSteps / 10) {
    taskYIELD();
  }
}

void waitForPendingPos0(Axis *a) {
  while (a->pendingPos != 0) {
    taskYIELD();
  }
}

bool isContinuousStep() {
  return moveStep ==
         (measure == MEASURE_METRIC ? MOVE_STEP_1 : MOVE_STEP_IMP_1);
}

// For rotational axis the moveStep of 0.1" means 0.1°.
long getMoveStepForAxis(Axis *a) {
  return (a->rotational && measure != MEASURE_METRIC) ? (moveStep / 25.4)
                                                      : moveStep;
}

long getStepMaxSpeed(Axis *a) {
  return isContinuousStep()
             ? a->speedManualMove
             : min(long(a->speedManualMove),
                   abs(getMoveStepForAxis(a)) * 1000 / STEP_TIME_MS);
}

void waitForStep(Axis *a) {
  if (isContinuousStep()) {
    // Move continuously for default step.
    waitForPendingPosNear0(a);
  } else {
    // Move with tiny pauses allowing to stop precisely.
    a->continuous = false;
    waitForPendingPos0(a);
    DELAY(DELAY_BETWEEN_STEPS_MS);
  }
}

int getAndResetPulses(Axis *a) {
  int16_t count;
  pcnt_get_counter_value(a->pulseUnit, &count);
  int delta = count - a->pulseCount;
  if (delta == 0)
    return 0;
  if (isOn && manualMovesIgnoredWhenOn()) {
    pcnt_counter_clear(a->pulseUnit);
    a->pulseCount = 0;
    return 0;
  }
  if (count >= PCNT_CLEAR || count <= -PCNT_CLEAR) {
    pcnt_counter_clear(a->pulseUnit);
    a->pulseCount = 0;
  } else {
    a->pulseCount = count;
  }
  return delta;
}

// Calculates stepper position from spindle position.
long posFromSpindle(Axis *a, long s, bool respectStops) {
  long newPos =
      s * a->motorSteps / a->screwPitch / ENCODER_STEPS_FLOAT * dupr * starts;

  // Respect left/right stops.
  if (respectStops) {
    if (newPos < a->rightStop) {
      newPos = a->rightStop;
    } else if (newPos > a->leftStop) {
      newPos = a->leftStop;
    }
  }

  return newPos;
}

// Calculates spindle position from stepper position.
long spindleFromPos(Axis *a, long p) {
  return p * a->screwPitch * ENCODER_STEPS_FLOAT / a->motorSteps /
         (dupr * starts);
}

bool stepTo(Axis *a, long newPos, bool continuous) {
  if (xSemaphoreTake(a->mutex, 10) == pdTRUE) {
    a->continuous = continuous;
    if (newPos == a->pos) {
      a->pendingPos = 0;
    } else {
      a->pendingPos =
          newPos - a->motorPos - (newPos > a->pos ? 0 : a->backlashSteps);
    }
    xSemaphoreGive(a->mutex);
    return true;
  }
  return false;
}

// Moves the stepper so that the tool is located at the newPos.
bool stepToContinuous(Axis *a, long newPos) { return stepTo(a, newPos, true); }

bool stepToFinal(Axis *a, long newPos) { return stepTo(a, newPos, false); }

void setDir(Axis *a, bool dir) {
  // Start slow if direction changed.
  if (a->direction != dir || !a->directionInitialized) {
    a->speed = a->speedStart;
    a->direction = dir;
    a->directionInitialized = true;
    digitalWrite(a->dir, dir ^ a->invertStepper);
    delayMicroseconds(DIRECTION_SETUP_DELAY_US);
  }
}

Axis *getAsyncAxis() { return mode == MODE_Y ? &y : &z; }

unsigned int getTimerLimit() {
  if (dupr == 0) {
    return 65535;
  }
  return min(long(65535),
             long(TIMER_FREQ / (z.motorSteps * abs(dupr) / z.screwPitch)) - 1);
}

void updateAsyncTimerSettings() {
  // dupr and therefore direction can change while we're in async mode.
  setDir(getAsyncAxis(), dupr > 0);

  // dupr can change while we're in async mode, keep updating timer frequency.
  timerAlarm(async_timer, getTimerLimit(), true, 0);
  // without this timer stops working if already above new limit
  timerWrite(async_timer, 0);
}

// =================================================================
// TaskMoveZ
// =================================================================

// GECKOSUB CHANGE: Global variable for dynamic jog delay
volatile unsigned long jogDelayMs = 300;

void taskMoveZ(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    int pulseDelta = 0;
    // getAndResetPulses(&z); // MPG logic disabled
    bool left = buttonLeftPressed;
    bool right = buttonRightPressed;
    if (!left && !right && pulseDelta == 0) {
      taskYIELD();
      continue;
    }
    if (spindlePosSync != 0) {
      // Edge case.
      taskYIELD();
      continue;
    }
    if (isOn && !manualMovesAllowedWhenOn()) {
      setIsOnFromTask(false);
      taskYIELD();
      continue;
    }
    int sign = pulseDelta == 0 ? (left ? 1 : -1) : (pulseDelta > 0 ? 1 : -1);
    bool stepperOn = true;
    stepperEnable(&z, true);
    z.movingManually = true;

    // GECKOSUB CHANGE: Track when the press started
    unsigned long pressStart = millis();

    if (isOn && dupr != 0 && mode == MODE_NORMAL) {
      // Move by moveStep in the desired direction but stay in the thread by
      // possibly traveling a little more.
      int diff = ceil(moveStep * 1.0 / abs(dupr * starts)) *
                 ENCODER_STEPS_FLOAT * sign * (dupr > 0 ? 1 : -1);
      long prevSpindlePos = spindlePos;
      bool resting = false;
      do {
        z.speedMax = z.speedManualMove;
        if (xSemaphoreTake(motionMutex, 100) == pdTRUE) {
          if (!resting) {
            spindlePos += diff;
            spindlePosAvg += diff;
          }
          // If spindle is moving, it will be changing spindlePos at the same
          // time. Account for it.
          while (diff > 0 ? (spindlePos < prevSpindlePos)
                          : (spindlePos > prevSpindlePos)) {
            spindlePos += diff;
            spindlePosAvg += diff;
          };
          prevSpindlePos = spindlePos;
          xSemaphoreGive(motionMutex);
        }

        long newPos = posFromSpindle(&z, prevSpindlePos, true);
        if (newPos != z.pos) {
          stepToContinuous(&z, newPos);
          waitForPendingPosNear0(&z);
        } else if (z.pos == (left ? z.leftStop : z.rightStop)) {
          // We're standing on a stop with the L/R move button pressed.
          resting = true;
          if (stepperOn) {
            stepperEnable(&z, false);
            stepperOn = false;
          }
          DELAY(200);
        }
      } while (left ? buttonLeftPressed : buttonRightPressed);
    } else {
      z.speedMax = getStepMaxSpeed(&z);
      int delta = 0;
      do {
        float fractionalDelta =
            (pulseDelta == 0 ? moveStep * sign / z.screwPitch
                             : pulseDelta / PULSE_PER_REVOLUTION) *
                z.motorSteps +
            z.fractionalPos;
        delta = round(fractionalDelta);
        // Don't lose fractional steps when moving by 0.01" or 0.001".
        z.fractionalPos = fractionalDelta - delta;
        if (delta == 0) {
          // When moveStep is e.g. 1 micron and MOTOR_STEPS_Z is 200, make delta
          // non-zero.
          delta = sign;
        }

        long posCopy = z.pos + z.pendingPos;
        // Don't left-right move out of stops.
        if (posCopy + delta > z.leftStop) {
          delta = z.leftStop - posCopy;
        } else if (posCopy + delta < z.rightStop) {
          delta = z.rightStop - posCopy;
        }
        z.speedMax = getStepMaxSpeed(&z);
        stepToContinuous(&z, posCopy + delta);
        waitForStep(&z);

        // GECKOSUB FIX: Dynamic Key Repeat Delay (100ms or 300ms)
        while ((left ? buttonLeftPressed : buttonRightPressed) &&
               (millis() - pressStart < jogDelayMs)) {
          taskYIELD();
        }

      } while (delta != 0 && (left ? buttonLeftPressed : buttonRightPressed));
      z.continuous = false;
      waitForPendingPos0(&z);
      if (isOn && mode == MODE_CONE) {
        if (xSemaphoreTake(motionMutex, 100) != pdTRUE) {
          setEmergencyStop(ESTOP_MARK_ORIGIN);
        } else {
          markOrigin();
          xSemaphoreGive(motionMutex);
        }
      } else if (isOn && mode == MODE_ASYNC) {
        // Restore async direction.
        updateAsyncTimerSettings();
      }
    }
    z.movingManually = false;
    if (stepperOn) {
      stepperEnable(&z, false);
    }
    z.speedMax = LONG_MAX;
    taskYIELD();
  }
  vTaskDelete(NULL);
}

// =================================================================
// SECTION END
// =================================================================

// =================================================================
// TaskMoveX
// =================================================================
void taskMoveX(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    int pulseDelta = 0; // Set to 0 to disable X-axis MPG
    bool up = buttonUpPressed || pulseDelta > 0;
    bool down = buttonDownPressed || pulseDelta < 0;
    if (!up && !down) {
      taskYIELD();
      continue;
    }
    if (isOn && !manualMovesAllowedWhenOn()) {
      setIsOnFromTask(false);
      taskYIELD();
      continue;
    }
    x.movingManually = true;
    x.speedMax = getStepMaxSpeed(&x);
    stepperEnable(&x, true);

    // Track when the press started
    unsigned long pressStart = millis();

    int delta = 0;
    int sign = up ? 1 : -1;
    do {
      float fractionalDelta =
          (pulseDelta == 0 ? moveStep * sign / x.screwPitch
                           : pulseDelta / PULSE_PER_REVOLUTION) *
              x.motorSteps +
          x.fractionalPos;
      delta = round(fractionalDelta);
      // Don't lose fractional steps when moving by 0.01" or 0.001".
      x.fractionalPos = fractionalDelta - delta;
      if (delta == 0) {
        // When moveStep is e.g.
        // 1 micron and MOTOR_STEPS_Z is 200, make delta non-zero.
        delta = sign;
      }

      long posCopy = x.pos + x.pendingPos;
      if (posCopy + delta > x.leftStop) {
        delta = x.leftStop - posCopy;
      } else if (posCopy + delta < x.rightStop) {
        delta = x.rightStop - posCopy;
      }
      stepToContinuous(&x, posCopy + delta);
      waitForStep(&x);

      // GECKOSUB FIX: Dynamic Key Repeat Delay (100ms or 300ms)
      // Hard Break Logic retained
      bool releasedEarly = false;
      while (millis() - pressStart < jogDelayMs) {
        if (!(up ? buttonUpPressed : buttonDownPressed)) {
          releasedEarly = true;
          break;
        }
        taskYIELD();
      }

      if (releasedEarly)
        break; // Force exit the do-while loop

      // pulseDelta = getAndResetPulses(&x);
      // REMOVED - This conflicts with Fine Pitch Encoder
    } while (delta != 0 && (/*pulseDelta != 0 ||*/ (up ? buttonUpPressed
                                                       : buttonDownPressed)));
    x.continuous = false;
    waitForPendingPos0(&x);
    if (isOn && mode == MODE_CONE) {
      if (xSemaphoreTake(motionMutex, 100) != pdTRUE) {
        setEmergencyStop(ESTOP_MARK_ORIGIN);
      } else {
        markOrigin();
        xSemaphoreGive(motionMutex);
      }
    }
    x.movingManually = false;
    x.speedMax = LONG_MAX;
    stepperEnable(&x, false);

    taskYIELD();
  }
  vTaskDelete(NULL);
}

// =================================================================
// END OF SECTION
// =================================================================

// =================================================================
// TaskMoveY
// =================================================================

void taskMoveY(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    bool plus = buttonForwardPressed;
    bool minus = buttonBackPressed;
    if (mode != MODE_Y || (!plus && !minus)) {
      taskYIELD();
      continue;
    }
    y.movingManually = true;
    y.speedMax = getStepMaxSpeed(&y);
    stepperEnable(&y, true);

    int delta = 0;
    int sign = plus ? 1 : -1;
    do {
      float fractionalDelta = getMoveStepForAxis(&y) * sign / y.screwPitch * y.motorSteps + y.fractionalPos;
      delta = round(fractionalDelta);
      y.fractionalPos = fractionalDelta - delta;
      if (delta == 0)
        delta = sign;
      long posCopy = y.pos + y.pendingPos;
      if (posCopy + delta > y.leftStop) {
        delta = y.leftStop - posCopy;
      } else if (posCopy + delta < y.rightStop) {
        delta = y.rightStop - posCopy;
      }
      stepToContinuous(&y, posCopy + delta);
      waitForStep(&y);
    } while (plus ? buttonForwardPressed : buttonBackPressed);
    y.continuous = false;
    waitForPendingPos0(&y);
    // Restore async direction.
    if (isOn && mode == MODE_Y)
      updateAsyncTimerSettings();
    y.movingManually = false;
    y.speedMax = LONG_MAX;
    stepperEnable(&y, false);
    taskYIELD();
  }
  vTaskDelete(NULL);
}

// =================================================================
// END OF TaskMoveY
// =================================================================

String getValueString(const String &command, char letter) {
  int index = command.indexOf(letter);
  if (index == -1) {
    return "";
  }
  String valueString;
  for (int i = index + 1; i < command.length(); i++) {
    char c = command.charAt(i);
    if (isDigit(c) || c == '.' || c == '-') {
      valueString += c;
    } else {
      break;
    }
  }
  return valueString;
}

float getFloat(const String &command, char letter) {
  return getValueString(command, letter).toFloat();
}

int getInt(const String &command, char letter) {
  return getValueString(command, letter).toInt();
}

void setFeedRate(const String &command) {
  float feed = getFloat(command, 'F');
  if (feed <= 0)
    return;
  gcodeFeedDuPerSec =
      round(feed * (measure == MEASURE_METRIC ? 10000 : 254000) / 60.0);
}

long mmOrInchToAbsolutePos(Axis *a, float mmOrInch) {
  long scaleToDu = measure == MEASURE_METRIC ? 10000 : 254000;
  long part1 = a->gcodeRelativePos;
  long part2 = round(mmOrInch * scaleToDu / a->screwPitch * a->motorSteps);
  return part1 + part2;
}

void updateAxisSpeeds(long diffX, long diffZ, long diffY) {
  if (diffX == 0 && diffZ == 0 && diffY == 0)
    return;
  long absX = abs(diffX);
  long absZ = abs(diffZ);
  long absC = abs(diffY);
  float stepsPerSecX = gcodeFeedDuPerSec * x.motorSteps / x.screwPitch;
  float minStepsPerSecX = GCODE_FEED_MIN_DU_SEC * x.motorSteps / x.screwPitch;
  if (stepsPerSecX > x.speedManualMove)
    stepsPerSecX = x.speedManualMove;
  else if (stepsPerSecX < minStepsPerSecX)
    stepsPerSecX = minStepsPerSecX;
  float stepsPerSecZ = gcodeFeedDuPerSec * z.motorSteps / z.screwPitch;
  float minStepsPerSecZ = GCODE_FEED_MIN_DU_SEC * z.motorSteps / z.screwPitch;
  if (stepsPerSecZ > z.speedManualMove)
    stepsPerSecZ = z.speedManualMove;
  else if (stepsPerSecZ < minStepsPerSecZ)
    stepsPerSecZ = minStepsPerSecZ;
  float stepsPerSecY = gcodeFeedDuPerSec * y.motorSteps / y.screwPitch;
  float minStepsPerSecY = GCODE_FEED_MIN_DU_SEC * y.motorSteps / y.screwPitch;
  if (stepsPerSecY > y.speedManualMove)
    stepsPerSecY = y.speedManualMove;
  else if (stepsPerSecY < minStepsPerSecY)
    stepsPerSecY = minStepsPerSecY;
  float secX = absX / stepsPerSecX;
  float secZ = absZ / stepsPerSecZ;
  float secY = absC / stepsPerSecY;
  float sec = ACTIVE_Y ? max(max(secX, secZ), secY) : max(secX, secZ);
  x.speedMax = sec > 0 ? absX / sec : x.speedManualMove;
  z.speedMax = sec > 0 ? absZ / sec : z.speedManualMove;
  y.speedMax = sec > 0 ? absC / sec : y.speedManualMove;
  if (x.speedMax < minStepsPerSecX)
    x.speedMax = minStepsPerSecX;
  if (z.speedMax < minStepsPerSecZ)
    z.speedMax = minStepsPerSecZ;
  if (y.speedMax < minStepsPerSecY)
    y.speedMax = minStepsPerSecY;
}

void gcodeWaitEpsilon(int epsilon) {
  while (abs(x.pendingPos) > epsilon || abs(z.pendingPos) > epsilon ||
         abs(y.pendingPos) > epsilon ||
         (SPINDLE_PAUSES_GCODE && getApproxRpm() < GCODE_MIN_RPM)) {
    taskYIELD();
  }
}

void gcodeWaitNear() { gcodeWaitEpsilon(GCODE_WAIT_EPSILON_STEPS); }

void gcodeWaitStop() { gcodeWaitEpsilon(0); }

// Rapid positioning / linear interpolation.
void G00_01(const String &command) {
  long xStart = x.pos;
  long zStart = z.pos;
  long yStart = y.pos;
  long xEnd = command.indexOf(x.name) >= 0 ? mmOrInchToAbsolutePos(&x, getFloat(command, x.name)) : xStart;
  long zEnd = command.indexOf(z.name) >= 0 ? mmOrInchToAbsolutePos(&z, getFloat(command, z.name)) : zStart;
  long yEnd = command.indexOf(y.name) >= 0 ? mmOrInchToAbsolutePos(&y, getFloat(command, y.name)) : yStart;
  long xDiff = xEnd - xStart;
  long zDiff = zEnd - zStart;
  long yDiff = yEnd - yStart;
  updateAxisSpeeds(xDiff, zDiff, yDiff);
  long chunks = round(max(max(abs(xDiff), abs(zDiff)), abs(yDiff)) *
                      LINEAR_INTERPOLATION_PRECISION);
  for (long i = 0; i < chunks; i++) {
    if (!isOn)
      return;
    float scale = i / float(chunks);
    stepToContinuous(&x, xStart + xDiff * scale);
    stepToContinuous(&z, zStart + zDiff * scale);
    if (ACTIVE_Y)
      stepToContinuous(&y, yStart + yDiff * scale);
    gcodeWaitNear();
  }
  // To avoid any rounding error, move to precise position.
  stepToFinal(&x, xEnd);
  stepToFinal(&z, zEnd);
  if (ACTIVE_Y)
    stepToFinal(&y, yEnd);
  gcodeWaitStop();
}

bool handleGcode(const String &command) {
  int op = getInt(command, 'G');
  if (op == 0 || op == 1) { // 0 also covers X and Z commands without G.
    G00_01(command);
  } else if (op == 20 || op == 21) {
    setMeasure(op == 20 ? MEASURE_INCH : MEASURE_METRIC);
  } else if (op == 90 || op == 91) {
    gcodeAbsolutePositioning = op == 90;
  } else if (op == 94) {
    /* no-op feed per minute */
  } else if (op == 18) {
    /* no-op ZX plane selection */
  } else {
    writeBuffer(&outBuffer, "error: unsupported command ");
    writeBuffer(&outBuffer, command);
    writeBuffer(&outBuffer, "\n");
    return false;
  }
  return true;
}

bool handleMcode(const String &command) {
  int op = getInt(command, 'M');
  if (op == 0 || op == 1 || op == 2 || op == 30) {
    setIsOnFromTask(false);
  } else {
    setIsOnFromTask(false);
    writeBuffer(&outBuffer, "error: unsupported command ");
    writeBuffer(&outBuffer, command);
    writeBuffer(&outBuffer, "\n");
    return false;
  }
  return true;
}

// Process one command, return ok flag.
bool handleGcodeCommand(String command) {
  command.trim();
  if (command.length() == 0)
    return false;

  // Trim N.. prefix.
  char code = command.charAt(0);
  int spaceIndex = command.indexOf(' ');
  if (code == 'N' && spaceIndex > 0) {
    command = command.substring(spaceIndex + 1);
    code = command.charAt(0);
  }

  // Update position for relative calculations right before performing them.
  z.gcodeRelativePos = gcodeAbsolutePositioning ? -z.originPos : z.pos;
  x.gcodeRelativePos = gcodeAbsolutePositioning ? -x.originPos : x.pos;
  y.gcodeRelativePos = gcodeAbsolutePositioning ? -y.originPos : y.pos;

  setFeedRate(command);
  switch (code) {
  case 'G':
  case NAME_Z:
  case NAME_X:
  case NAME_Y:
    return handleGcode(command);
  case 'F':
    return true; /* feed already handled above */
  case 'M':
    return handleMcode(command);
  case 'T':
    return true; /* ignoring tool changes */
  default:
    writeBuffer(&outBuffer, "error: unsupported command ");
    writeBuffer(&outBuffer, code);
    writeBuffer(&outBuffer, "\n");
    return false;
  }
  return false;
}

void taskGcode(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    if (mode != MODE_GCODE) {
      gcodeInitialized = false;
    } else if (!gcodeInitialized) {
      gcodeInitialized = true;
      gcodeCommand = "";
      gcodeAbsolutePositioning = true;
      gcodeFeedDuPerSec = GCODE_FEED_DEFAULT_DU_SEC;
      gcodeInBrace = false;
      gcodeInSemicolon = false;
    }
    // Implementing a relevant subset of RS274 (Gcode) and GRBL (state
    // management) covering basic use cases.
    char receivedChar = '\0';
    bool isWebSocket = false;
    if (mode == MODE_GCODE && isOn &&
        gcodeProgramCharIndex < gcodeProgram.length()) {
      receivedChar = gcodeProgram.charAt(gcodeProgramCharIndex);
      gcodeProgramCharIndex++;
    } else if (bufferAvailable(&inBuffer)) {
      isWebSocket = true;
      receivedChar = shiftBuffer(&inBuffer);
    }
    int charCode = int(receivedChar);
    if (charCode > 0) {
      if (gcodeInBrace) {
        if (receivedChar == ')')
          gcodeInBrace = false;
      } else if (wsInKeycode) {
        if (charCode < 32) {
          if (wsKeycode == 0) {
            wsKeycode = keycodeCommand.toInt();
            writeBuffer(&outBuffer, String(wsKeycode));
            writeBuffer(&outBuffer, "\n");
          } else {
            writeBuffer(&outBuffer, "slower\n");
          }
          wsInKeycode = false;
          keycodeCommand = "";
        } else {
          keycodeCommand += receivedChar;
        }
      } else if (receivedChar == '(') {
        gcodeInBrace = true;
      } else if (receivedChar == ';' /* start of comment till end of line */) {
        gcodeInSemicolon = true;
      } else if (gcodeInSemicolon && charCode >= 32) {
        // Ignoring comment.
      } else if (receivedChar == '!' /* stop */) {
        setIsOnFromTask(false);
      } else if (receivedChar == '~' /* resume */) {
        setIsOnFromTask(true);
      } else if (receivedChar == '%' /* start/end marker */) {
        // Not using % markers in this implementation.
      } else if (receivedChar == '?' /* status */) {
        writeBuffer(&outBuffer, "<");
        writeBuffer(&outBuffer, isOn ? "Run" : "Idle");
        writeBuffer(&outBuffer, "|WPos:");
        float divisor = measure == MEASURE_METRIC ? 10000.0 : 254000.0;
        writeBuffer(&outBuffer, getAxisPosDu(&x) / divisor, 3);
        writeBuffer(&outBuffer, ",0.000,");
        writeBuffer(&outBuffer, getAxisPosDu(&z) / divisor, 3);
        writeBuffer(&outBuffer, "|FS:");
        writeBuffer(&outBuffer, round(gcodeFeedDuPerSec * 60 / 10000.0));
        writeBuffer(&outBuffer, ",");
        writeBuffer(&outBuffer, getApproxRpm());
        writeBuffer(&outBuffer, "|Id:");
        writeBuffer(&outBuffer, "NanoELS-DRO V" + String(SOFTWARE_VERSION));
        writeBuffer(&outBuffer, ">"); // no new line to allow client to easily
                                      // cut out the status response
      } else if (gcodeInSave &&
                 receivedChar == '"' /* end of saved program */) {
        gcodeInSave = false;
        if (gcodeSaveName.length() == 0) {
          if (removeAllGcode())
            writeBuffer(&outBuffer, "ok\n");
        } else if (gcodeSaveValue.length() > 1) {
          if (saveGcode())
            writeBuffer(&outBuffer, "ok\n");
        } else if (gcodeSaveName.length() == 1) {
          writeBuffer(&outBuffer, "error: name must be at least 2 chars\n");
        } else {
          removeGcodeByName(gcodeSaveName);
        }
        gcodeSaveName = "";
        gcodeSaveValue = "";
      } else if (!gcodeInSave &&
                 receivedChar == '"' /* start of save program */) {
        gcodeInSave = true;
        gcodeInSaveFirstLine = true;
      } else if (gcodeInSaveFirstLine && receivedChar >= 32) {
        gcodeSaveName += receivedChar;
      } else if (gcodeInSaveFirstLine && receivedChar < 32) {
        gcodeInSaveFirstLine = false;
        writeBuffer(&outBuffer, "ok\n");
      } else if (gcodeInSave) {
        gcodeSaveValue += receivedChar;
        if (receivedChar < 32) {
          gcodeInBrace = false;
          gcodeInSemicolon = false;
          writeBuffer(&outBuffer, "ok\n");
        }
      } else if (isOn) {
        if (gcodeInBrace && charCode < 32) {
          writeBuffer(&outBuffer, "error: comment not closed\n");
          setIsOnFromTask(false);
        } else if (charCode < 32 && gcodeCommand.length() > 1) {
          if (handleGcodeCommand(gcodeCommand)) {
            if (isWebSocket)
              writeBuffer(&outBuffer, "ok\n");
          }
          gcodeCommand = "";
          gcodeInSemicolon = false;
        } else if (charCode < 32) {
          if (isWebSocket)
            writeBuffer(&outBuffer, "ok\n");
          gcodeCommand = "";
          gcodeInSemicolon = false;
        } else if (charCode >= 32 && (charCode == 'G' || charCode == 'M')) {
          // Split consequent G and M commands on one line.
          // No "ok" for commands in the middle of the line.
          handleGcodeCommand(gcodeCommand);
          gcodeCommand = receivedChar;
        } else if (charCode >= 32) {
          gcodeCommand += receivedChar;
        }
      } else if (receivedChar == '=' /* start of keycode command */) {
        wsInKeycode = true;
        keycodeCommand = "";
      } else {
        // ignoring non-realtime command input when off
        // to flush any commands coming after an error
      }
    }
    if (mode == MODE_GCODE && isOn && gcodeProgramCharIndex > 0 &&
        gcodeProgramCharIndex == gcodeProgram.length()) {
      setIsOnFromTask(false);
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}

void startPulseCounter(pcnt_unit_t unit, int gpioA, int gpioB) {
  pcnt_config_t pcntConfig;
  pcntConfig.pulse_gpio_num = gpioA;
  pcntConfig.ctrl_gpio_num = gpioB;
  pcntConfig.channel = PCNT_CHANNEL_0;
  pcntConfig.unit = unit;
  pcntConfig.pos_mode = PCNT_COUNT_INC;
  pcntConfig.neg_mode = PCNT_COUNT_DEC;
  pcntConfig.lctrl_mode = PCNT_MODE_REVERSE;
  pcntConfig.hctrl_mode = PCNT_MODE_KEEP;
  pcntConfig.counter_h_lim = PCNT_LIM;
  pcntConfig.counter_l_lim = -PCNT_LIM;
  pcnt_unit_config(&pcntConfig);
  pcnt_set_filter_value(unit, ENCODER_FILTER);
  pcnt_filter_enable(unit);
  pcnt_counter_pause(unit);
  pcnt_counter_clear(unit);
  pcnt_counter_resume(unit);
}

// =================================================================
// Interrupt Task Configuration
// =================================================================

// Attaching interrupt on core 0 to have more time on core 1 where axes are
// moved.
void taskAttachInterrupts(void *param) {
  startPulseCounter(PCNT_UNIT_0, ENC_A, ENC_B);
  // startPulseCounter(PCNT_UNIT_1, Z_PULSE_A, Z_PULSE_B); // REMOVED - This
  // counter is now for Z-DRO
  startPulseCounter(PCNT_UNIT_2, X_PULSE_A, X_PULSE_B);
  // Conditionally start the Y-MPG counter ONLY if the Y-axis is active.
  // PCNT_UNIT_4 is now used by the DRO and is configured separately.
  if (ACTIVE_Y) {
    startPulseCounter(PCNT_UNIT_3, Y_PULSE_A, Y_PULSE_B);
  }
  vTaskDelete(NULL);
}

void setDupr(long value) {
  // Can't apply changes right away since we might be in the middle of motion
  // logic.
  nextDupr = value;
  nextDuprFlag = true;
}

// =================================================================
// Geckosub + Gemini 2.5 Pro - SECTION 16, FINE PITCH ENCODER
// =================================================================
void handleFinePitchEncoder() {
  // --- Read the encoder hardware ---
  static int16_t lastEncoderCount = 0;
  int16_t currentEncoderCount;
  pcnt_get_counter_value(PCNT_UNIT_2, &currentEncoderCount);
  int delta = currentEncoderCount - lastEncoderCount;

  if (delta == 0)
    return;
  // No movement

  if (currentEncoderCount >= PCNT_CLEAR || currentEncoderCount <= -PCNT_CLEAR) {
    pcnt_counter_clear(PCNT_UNIT_2);
    lastEncoderCount = 0;
  } else {
    lastEncoderCount = currentEncoderCount;
  }

  // --- Decide what to do based on the application state ---
  if (currentAppState == STATE_CHANGE_MODE) {
    /***** MODE CHANGE LOGIC *****/
    static int modeChangeAccumulator = 0;
    modeChangeAccumulator += delta;
    int modeJumps = modeChangeAccumulator / 4;

    if (modeJumps != 0 && numActiveModes > 0) {
      int currentModeIndex = -1;
      for (int i = 0; i < numActiveModes; i++) {
        if (mode == activeModes[i]) {
          currentModeIndex = i;
          break;
        }
      }

      if (currentModeIndex ==
          -1) { // If current mode not in active list, jump to first
        currentModeIndex = 0;
      }

      int newModeIndex = (currentModeIndex + modeJumps) % numActiveModes;
      if (newModeIndex < 0) {
        newModeIndex += numActiveModes;
      }

      setModeFromTask(activeModes[newModeIndex]);
      modeChangeAccumulator %= 4;
    }
  } else { // If in STATE_NORMAL
    /***** FINE PITCH ADJUSTMENT LOGIC *****/
    if (!FINE_PITCH_ENCODER_ENABLED)
      return;
    bool isModeActive = false;
    for (int i = 0; i < NUM_FINE_PITCH_ACTIVE_MODES; i++) {
      if (mode == FINE_PITCH_ACTIVE_MODES[i]) {
        isModeActive = true;
        break;
      }
    }
    if (!isModeActive)
      return;

    static int pitchAdjustAccumulator = 0;
    // static bool lastSignWasNegative = false; // <-- REMOVED (now global in
    // Sec 4)
    const long CLICK_INCREMENT_DU = 100; // 0.01mm
    const long MAX_PITCH_DU = 3000; // 0.300mm
    const long SNAP_START_DU = 600; // 0.060mm

    pitchAdjustAccumulator += delta;
    int clicks = pitchAdjustAccumulator / 2;
    if (clicks == 0)
      return;
    pitchAdjustAccumulator %= 2;

    // --- NEW SNAP AND CLAMP LOGIC ---
    long newDupr;
    long currentDupr = dupr; // Get a snapshot

    // Update state *before* logic
    if (currentDupr < 0) {
      lastSignWasNegative = true;
    } else if (currentDupr > 0) {
      lastSignWasNegative = false;
    }
    // If currentDupr is 0, lastSignWasNegative retains its previous value

    if (abs(currentDupr) > MAX_PITCH_DU) {
      // Current pitch is OUTSIDE fine range. Snap to start value.
      if (currentDupr >= 0) {
        newDupr = SNAP_START_DU;
        lastSignWasNegative = false;
      } else {
        newDupr = -SNAP_START_DU;
        lastSignWasNegative = true;
      }
    } else {
      // Current pitch is INSIDE fine range. Adjust normally.
      long adjustment = (clicks * CLICK_INCREMENT_DU);

      // Handle the "0" case based on last known sign
      if (currentDupr == 0) {
        if (lastSignWasNegative) {
          // We are at "negative 0"
          if (clicks > 0) { // Scrolling right (coarser)
            newDupr = -adjustment; // Go to -0.01
          } else { // Scrolling left (finer)
            newDupr = 0; // Stay at 0
          }
        } else {
          // We are at "positive 0"
          if (clicks > 0) { // Scrolling right (coarser)
            newDupr = adjustment; // Go to +0.01
          } else { // Scrolling left (finer)
            newDupr = 0; // Stay at 0
          }
        }
      } else if (currentDupr > 0) {
        // We are adjusting a POSITIVE pitch (e.g., 0.10 -> 0.11)
        newDupr = currentDupr + adjustment;
        if (newDupr < 0) {
          newDupr = 0;
        } // Don't cross zero
        if (newDupr > MAX_PITCH_DU) {
          newDupr = MAX_PITCH_DU;
        } // Don't go over max
      } else {
        // We are adjusting a NEGATIVE pitch (e.g., -0.10 -> -0.11)
        newDupr = currentDupr - adjustment; // SUBTRACT to reverse direction
        if (newDupr > 0) {
          newDupr = 0;
        } // Don't cross zero
        if (newDupr < -MAX_PITCH_DU) {
          newDupr = -MAX_PITCH_DU;
        } // Don't go under min
      }
    }

    setDupr(newDupr);
    // --- END NEW LOGIC ---
  }
}
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 16
// =================================================================

// Must be called while holding motionMutex.
void applyDupr() {
  if (nextDupr == dupr) {
    return;
  }
  dupr = nextDupr;
  markOrigin();
  if (mode == MODE_ASYNC || mode == MODE_Y) {
    updateAsyncTimerSettings();
  }
}

void setStarts(int value) {
  // Can't apply changes right away since we might be in the middle of motion
  // logic.
  nextStarts = value;
  nextStartsFlag = true;
}

// Must be called while holding motionMutex.
void applyStarts() {
  if (starts == nextStarts) {
    return;
  }
  starts = nextStarts;
  markOrigin();
}

// Only used for async movement in ASYNC and Y modes.
// Keep code in this method to absolute minimum to achieve high stepper speeds.
void IRAM_ATTR onAsyncTimer() {
  Axis *a = getAsyncAxis();
  if (!isOn || a->movingManually || (mode != MODE_ASYNC && mode != MODE_Y)) {
    return;
  } else if (dupr > 0 && a->pos < a->leftStop) {
    if (a->pos <= a->motorPos) {
      a->pos++;
    }
    a->motorPos++;
    a->posGlobal++;
  } else if (dupr < 0 && a->pos > a->rightStop) {
    if (a->pos >= a->motorPos + a->backlashSteps) {
      a->pos--;
    }
    a->motorPos--;
    a->posGlobal--;
  } else {
    return;
  }

  DLOW(a->step);
  a->stepStartUs = micros();
  delayMicroseconds(10);
  DHIGH(a->step);
}

void setModeFromTask(int value) {
  nextMode = value;
  nextModeFlag = true;
}

void setModeFromLoop(int value) {
  if (mode == value) {
    return;
  }
  if (isOn) {
    setIsOnFromLoop(false);
  }

  // --- NEW PITCH SAVING LOGIC ---
  // Save the current 'dupr' to the appropriate variable before changing modes
  if (mode == MODE_THREAD) {
    threadDupr = dupr; // Save current pitch to thread-specific variable
  } else if (mode != -1) { // -1 is the default/boot value, don't save it
    gearDupr = dupr; // Save current pitch to the general "gear" variable
  }

  // Load the new pitch based on the mode we are ENTERING
  if (value == MODE_THREAD) {
    setDupr(threadDupr); // Load the saved thread pitch
  } else {
    setDupr(gearDupr); // Load the saved gear pitch
  }
  // --- END NEW LOGIC ---

  if (mode == MODE_THREAD) {
    setStarts(1);
  } else if (mode == MODE_ASYNC || mode == MODE_Y) {
    setAsyncTimerEnable(false);
  }

  mode = value;
  setupIndex = 0;

  if (mode == MODE_ASYNC || mode == MODE_Y) {
    if (!timerAttached) {
      timerAttached = true;
      timerAttachInterrupt(async_timer, &onAsyncTimer);
    }
    updateAsyncTimerSettings();
    setAsyncTimerEnable(true);
  }
}

void setTurnPasses(int value) {
  if (isOn) {
    beep();
  } else {
    turnPasses = value;
  }
}

void setConeRatio(float value) {
  // Can't apply changes right away since we might be in the middle of motion
  // logic.
  nextConeRatio = value;
  nextConeRatioFlag = true;
}

void applyConeRatio() {
  if (nextConeRatio == coneRatio) {
    return;
  }
  coneRatio = nextConeRatio;
  markOrigin();
}

void reset() {
  z.leftStop = LONG_MAX;
  z.nextLeftStopFlag = false;
  z.rightStop = LONG_MIN;
  z.nextRightStopFlag = false;
  z.originPos = 0;
  z.posGlobal = 0;
  z.motorPos = 0;
  z.pendingPos = 0;
  z.disabled = false;
  x.leftStop = LONG_MAX;
  x.nextLeftStopFlag = false;
  x.rightStop = LONG_MIN;
  x.nextRightStopFlag = false;
  x.originPos = 0;
  x.posGlobal = 0;
  x.motorPos = 0;
  x.pendingPos = 0;
  x.disabled = false;
  y.leftStop = LONG_MAX;
  y.nextLeftStopFlag = false;
  y.rightStop = LONG_MIN;
  y.nextRightStopFlag = false;
  y.originPos = 0;
  y.posGlobal = 0;
  y.motorPos = 0;
  y.pendingPos = 0;
  y.disabled = false;

  setDupr(0); // Set active pitch to 0
  gearDupr = 0; // NEW: Reset gear pitch
  threadDupr = 0; // NEW: Reset thread pitch

  setStarts(1);
  moveStep = MOVE_STEP_1;
  setModeFromTask(MODE_NORMAL);
  measure = MEASURE_METRIC;

  setConeRatio(1);
  auxForward = true;
}

long normalizePitch(long pitch) {
  int scale = 1;
  if (measure == MEASURE_METRIC) {
    // Drop the 3rd and 4th precision point if any.
    scale = 100;
  } else if (measure == MEASURE_INCH) {
    // Always drop the 4th precision point in inch representation if any.
    scale = 254;
  }
  return round(pitch / scale) * scale;
}

void buttonPlusMinusPress(bool plus) {
  // Mutex is aquired in setDupr() and setStarts().
  bool minus = !plus;
  if (mode == MODE_THREAD && setupIndex == 2) {
    // CHANGED: Limit set to > 1 so we can return to single start
    // (to disable multistart within thread mode)
    if (minus && starts > 1) {
      setStarts(starts - 1);
    } else if (plus && starts < STARTS_MAX) {
      setStarts(starts + 1);
    }
  } else if (isPassMode() && setupIndex == 1 && getNumpadResult() == 0) {
    if (minus && turnPasses > 1) {
      setTurnPasses(turnPasses - 1);
    } else if (plus && turnPasses < PASSES_MAX) {
      setTurnPasses(turnPasses + 1);
    }
  } else if (measure != MEASURE_TPI) {
    int delta = measure == MEASURE_METRIC ? MOVE_STEP_3 : MOVE_STEP_IMP_3;
    // Switching between mm/inch/tpi often results in getting non-0 3rd and 4th
    // precision points that can't be easily controlled. Remove them.
    long normalizedDupr = normalizePitch(dupr);
    if (minus && dupr > -DUPR_MAX) {
      setDupr(max(-DUPR_MAX, normalizedDupr - delta));
    } else if (plus && dupr < DUPR_MAX) {
      setDupr(min(DUPR_MAX, normalizedDupr + delta));
    }
  } else { // TPI
    if (dupr == 0) {
      setDupr(plus ? 1 : -1);
    } else {
      long currentTpi = round(254000.0 / dupr);
      long tpi = currentTpi + (plus ? 1 : -1);
      long newDupr =
          tpi == 0 ? (plus ? DUPR_MAX : -DUPR_MAX) : round(254000.0 / tpi);
      // Happens for small pitches like 0.01mm.
      if (newDupr == dupr) {
        newDupr += plus ? -1 : 1;
      }
      if (newDupr != dupr && newDupr < DUPR_MAX && newDupr > -DUPR_MAX) {
        setDupr(newDupr);
      }
    }
  }
}

void buttonOnOffPress(bool on) {
  // New logic for thread dwell  SEMI-AUTO THREAD & TURN MODE
  // Changed: Added MODE_TURN to the check
  if (on && isOn && (mode == MODE_THREAD || mode == MODE_TURN) &&
      awaitingButtonPress) {
    awaitingButtonPress = false;
    return; // Do nothing else
  }

  resetMillis = millis();
  // --- Cancel logic for numpad modes ---
  if (!on && inNumpad) { // If the "OFF" button is pressed AND we are in any
                         // numpad mode
    inNumpad = false;
    physicalNumpadModeActive = false;
    // Color update removed
    numpadEntryMode = PITCH;
    // Reset to default mode for next time
    resetNumpad();
    beep();
    return;
    // Stop further processing
  }

  bool missingZStops =
      needZStops() && (z.leftStop == LONG_MAX || z.rightStop == LONG_MIN);
  if (on && isPassMode() &&
      (missingZStops || x.leftStop == LONG_MAX || x.rightStop == LONG_MIN)) {
    beep();
  } else if (!isOn && on && mode == MODE_GCODE &&
             gcodeProgramIndex >= gcodeProgramCount && setupIndex == 1) {
    beep();
  } else if (!isOn && on && setupIndex < getLastSetupIndex()) {
    if (mode == MODE_THREAD && setupIndex == 3)
      setConeRatio(0);
    // Move to the next setup step.
    setupIndex++;
  } else if (isOn && on &&
             (mode == MODE_TURN || mode == MODE_FACE || mode == MODE_THREAD)) {
    // Move to the next pass.
    opIndexAdvanceFlag = true;
  } else if (!on &&
             (z.movingManually || x.movingManually || x.movingManually)) {
    setEmergencyStop(ESTOP_OFF_MANUAL_MOVE);
  } else if (!isOn && on && mode == MODE_GCODE &&
             gcodeProgramIndex >= gcodeProgramCount) {
    beep();
  } else if (!isOn && on && mode == MODE_GCODE) {
    String name = getCurrentGcodeProgramName();
    if (name.length() == 0) {
      beep();
    } else {
      gcodeProgramCharIndex = 0;
      gcodeProgram = readGcodeProgram(name);
      if (gcodeProgram.length() == 0) {
        beep();
      } else {
        gcodeProgram += '\n'; // ensures the last line is executed
        setIsOnFromTask(true);
      }
    }
  } else {
    setIsOnFromTask(on);
  }
}

void buttonOffRelease() {
  if (millis() - resetMillis > 3000) {
    reset();
    splashScreen = true;
  }
}

void setLeftStop(Axis *a, long value) {
  // Can't apply changes right away since we might be in the middle of motion
  // logic.
  a->nextLeftStop = value;
  a->nextLeftStopFlag = true;
}

void leaveStop(Axis *a, long oldStop) {
  if (mode == MODE_CONE) {
    // To avoid rushing to a far away position if standing on limit.
    markOrigin();
  } else if (mode == MODE_NORMAL && a == getPitchAxis() && a->pos == oldStop) {
    // Spindle is most likely out of sync with the stepper because
    // it was spinning while the lead screw was on the stop.
    spindlePosSync = spindleModulo(spindlePos - spindleFromPos(a, a->pos));
  }
}

void applyLeftStop(Axis *a) {
  // Accept left stop even if it's lower than pos.
  // Stop button press processing takes time during which motor could have
  // moved.
  long oldStop = a->leftStop;
  a->leftStop = a->nextLeftStop;
  leaveStop(a, oldStop);
}

void setRightStop(Axis *a, long value) {
  // Can't apply changes right away since we might be in the middle of motion
  // logic.
  a->nextRightStop = value;
  a->nextRightStopFlag = true;
}

void applyRightStop(Axis *a) {
  // Accept right stop even if it's higher than pos.
  // Stop button press processing takes time during which motor could have
  // moved.
  long oldStop = a->rightStop;
  a->rightStop = a->nextRightStop;
  leaveStop(a, oldStop);
}

void buttonLeftStopPress(Axis *a) {
  setLeftStop(a, a->leftStop == LONG_MAX ? a->pos : LONG_MAX);
}

void buttonRightStopPress(Axis *a) {
  setRightStop(a, a->rightStop == LONG_MIN ? a->pos : LONG_MIN);
}

// Geckosub + Gemini - Shared Spindle Value Text Box
void buttonDisplayPress() {
  spindleDisplayMode++;
  if (spindleDisplayMode > 2) {
    spindleDisplayMode = 0;
  }
}

void buttonMoveStepPress() {
  if (measure == MEASURE_METRIC) {
    if (moveStep == MOVE_STEP_1) {
      moveStep = MOVE_STEP_2;
    } else if (moveStep == MOVE_STEP_2) {
      moveStep = MOVE_STEP_3;
    } else {
      moveStep = MOVE_STEP_1;
    }
  } else {
    if (moveStep == MOVE_STEP_IMP_1) {
      moveStep = MOVE_STEP_IMP_2;
    } else if (moveStep == MOVE_STEP_IMP_2) {
      moveStep = MOVE_STEP_IMP_3;
    } else {
      moveStep = MOVE_STEP_IMP_1;
    }
  }
}

void buttonMeasurePress() {
  if (measure == MEASURE_METRIC) {
    setMeasure(MEASURE_INCH);
  } else if (measure == MEASURE_INCH) {
    setMeasure(MEASURE_TPI);
  } else {
    setMeasure(MEASURE_METRIC);
  }
}

void buttonReversePress() {
  if (dupr == 0) {
    // Special case: Toggle the "sign" of 0
    lastSignWasNegative = !lastSignWasNegative;
  } else {
    // Standard behavior: Invert the pitch
    setDupr(-dupr);
  }
}

void numpadPress(int digit) {
  if (!inNumpad) {
    numpadIndex = 0;
  }
  numpadDigits[numpadIndex] = digit;
  if (numpadIndex < 7) {
    numpadIndex++;
  } else {
    numpadIndex = 0;
  }
}

void numpadBackspace() {
  if (inNumpad && numpadIndex > 0) {
    numpadIndex--;
  }
}

void resetNumpad() { numpadIndex = 0; }

void numpadPlusMinus(bool plus) {
  if (numpadDigits[numpadIndex - 1] < 9 && plus) {
    numpadDigits[numpadIndex - 1]++;
  } else if (numpadDigits[numpadIndex - 1] > 1 && !plus) {
    numpadDigits[numpadIndex - 1]--;
  }
  // TODO: implement going over 9 and below 1.
}

unsigned long multistartPressMillis = 0;

void buttonMultistartPress() {
  if (millis() - multistartPressMillis > 3000 && starts > 1) {
    setStarts(1);
  } else {
    setStarts(starts + 1);
  }
  multistartPressMillis = millis();
}

void disableAllStops() {
  setLeftStop(&z, LONG_MAX);
  setRightStop(&z, LONG_MIN);
  setLeftStop(&x, LONG_MAX);
  setRightStop(&x, LONG_MIN);
  setLeftStop(&y, LONG_MAX);
  setRightStop(&y, LONG_MIN);
  beep(); // Give an audible confirmation
}

// =================================================================
// Numpad Result Handler
// =================================================================
bool processNumpadResult(int keyCode) {
  if (keyCode == B_ON) { // Numpad Enter was pressed
    if (numpadEntryMode == DIAMETER) {
      // --- NEW: Conditional Divisor (100 for Metric / 1000 for Imperial) ---
      float divisor = (measure == MEASURE_METRIC) ? 100.0f : 1000.0f;
      float desiredValue = getNumpadResult() / divisor;

      if (xDisplayModeDRO) {
        // --- DRO MODE (Scale) ---
        // Convert user's diameter entry to radius for internal math.
        desiredValue /= 2.0f;
        if (measure != MEASURE_METRIC) { // If in imperial, convert inches to mm
          desiredValue *= 25.4f;
        }
        xScale.setPosition(desiredValue);
      } else {
        // --- ELS MODE (Stepper) ---
        // Convert input (Diameter) to Target Radius in MM
        float targetRadiusMm = desiredValue / 2.0f;
        if (measure != MEASURE_METRIC) {
          targetRadiusMm = (desiredValue * 25.4f) / 2.0f;
        }
        // Convert to Stepper Steps: (mm * 10000) * motorSteps / screwPitch
        long targetSteps =
            round((targetRadiusMm * 10000.0) * x.motorSteps / x.screwPitch);
        // Update Origin so that (pos + originPos) equals the target
        x.originPos = targetSteps - x.pos;
      }
      numpadEntryMode = PITCH; // Reset
      resetNumpad();
      inNumpad = false;
      physicalNumpadModeActive = false; // Color update removed
      return true;
    } else if (numpadEntryMode == LENGTH) {
      // --- NEW KNOWN LENGTH LOGIC ---
      // --- NEW: Conditional Divisor ---
      float divisor = (measure == MEASURE_METRIC) ? 100.0f : 1000.0f;
      float desiredValue = getNumpadResult() / divisor;

      if (zDisplayModeDRO) {
        // --- DRO MODE (Scale) ---
        if (measure != MEASURE_METRIC)
          desiredValue *= 25.4f; // Convert inches to mm
        zScale.setPosition(desiredValue);
      } else {
        // --- ELS MODE (Stepper) ---
        float targetMm = desiredValue;
        if (measure != MEASURE_METRIC)
          targetMm = desiredValue * 25.4f;
        long targetSteps =
            round((targetMm * 10000.0) * z.motorSteps / z.screwPitch);
        z.originPos = targetSteps - z.pos;
      }

      numpadEntryMode = PITCH; // Reset
      resetNumpad();
      inNumpad = false;
      physicalNumpadModeActive = false; // Color update removed
      return true;
    } else { // Default mode is PITCH
      long newDu = numpadToDeciMicrons();
      if (abs(newDu) <= DUPR_MAX) {
        setDupr(newDu);
      }
      resetNumpad();
      inNumpad = false;
      physicalNumpadModeActive = false;
      // Color update removed
      return true;
    }
  }

  // Handle other keys after numpad entry (same as before)
  long newDu = numpadToDeciMicrons();
  float newConeRatio = numpadToConeRatio();
  long numpadResult = getNumpadResult();
  resetNumpad();

  if (isPassMode() && setupIndex == 1) {
    setTurnPasses(int(min(PASSES_MAX, numpadResult)));
    setupIndex++;
  } else if (mode == MODE_THREAD && setupIndex == 3) {
    setConeRatio(newConeRatio);
    setupIndex++;
  } else if (mode == MODE_CONE && setupIndex == 1) {
    setConeRatio(newConeRatio);
    setupIndex++;
  }

  Axis *a = (keyCode == B_STOPL || keyCode == B_STOPR || keyCode == B_LEFT ||
             keyCode == B_RIGHT || keyCode == B_Z)
                ? &z
                : &x;
  int sign = ((keyCode == B_STOPL || keyCode == B_STOPU || keyCode == B_LEFT ||
               keyCode == B_UP || keyCode == B_Z || keyCode == B_X)
                  ? 1
                  : -1);
  if (keyCode == B_STOPF || keyCode == B_STOPB || keyCode == B_FORWARD ||
      keyCode == B_BACK || keyCode == B_Y) {
    a = &y;
    sign = (keyCode == B_BACK || keyCode == B_STOPB) ? -1 : 1;
  }
  long posDiffAbs = (a->rotational ? numpadResult * 10 : newDu) /
                    a->screwPitch * a->motorSteps;
  long pos = a->pos + posDiffAbs * sign;

  if (keyCode == B_STOPL) {
    setLeftStop(&z, pos);
    return true;
  } else if (keyCode == B_STOPR) {
    setRightStop(&z, pos);
    return true;
  } else if (keyCode == B_STOPU) {
    setLeftStop(&x, pos);
    return true;
  } else if (keyCode == B_STOPD) {
    setRightStop(&x, pos);
    return true;
  } else if (keyCode == B_STOPF && ACTIVE_Y) {
    setLeftStop(&y, pos);
    return true;
  } else if (keyCode == B_STOPB && ACTIVE_Y) {
    setRightStop(&y, pos);
    return true;
  }

  if (!isOn &&
      (keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_UP ||
       keyCode == B_DOWN || keyCode == B_FORWARD || keyCode == B_BACK)) {
    if (pos < a->rightStop) {
      pos = a->rightStop;
      beep();
    } else if (pos > a->leftStop) {
      pos = a->leftStop;
      beep();
    } else if (abs(pos - a->pos) > a->estopSteps) {
      beep();
      return true;
    }
    a->speedMax = a->speedManualMove;
    stepToFinal(a, pos);
    return true;
  }

  if (keyCode == B_Z || keyCode == B_X || keyCode == B_Y) {
    a->originPos = -pos;
    return true;
  }
  if (keyCode == B_DIAMETER) {
    a->originPos = -a->pos - posDiffAbs / 2;
    return true;
  }
  if (keyCode == B_STEP) {
    if (newDu > 0) {
      moveStep = newDu;
    } else {
      beep();
    }
    return true;
  }
  return false;
}

bool processNumpad(int keyCode) {
  if (keyCode == B_0) {
    numpadPress(0);
    inNumpad = true;
  } else if (keyCode == B_1) {
    numpadPress(1);
    inNumpad = true;
  } else if (keyCode == B_2) {
    numpadPress(2);
    inNumpad = true;
  } else if (keyCode == B_3) {
    numpadPress(3);
    inNumpad = true;
  } else if (keyCode == B_4) {
    numpadPress(4);
    inNumpad = true;
  } else if (keyCode == B_5) {
    numpadPress(5);
    inNumpad = true;
  } else if (keyCode == B_6) {
    numpadPress(6);
    inNumpad = true;
  } else if (keyCode == B_7) {
    numpadPress(7);
    inNumpad = true;
  } else if (keyCode == B_8) {
    numpadPress(8);
    inNumpad = true;
  } else if (keyCode == B_9) {
    numpadPress(9);
    inNumpad = true;
  } else if (keyCode == B_BACKSPACE) {
    numpadBackspace();
    inNumpad = true;
  } else if (inNumpad && (keyCode == B_PLUS || keyCode == B_MINUS)) {
    numpadPlusMinus(keyCode == B_PLUS);
    return true;
  } else if (inNumpad) {
    inNumpad = false;
    return processNumpadResult(keyCode);
  }
  return inNumpad;
}

const int NEXTION_BUFFER_LENGTH = 256;
byte nextionBuffer[NEXTION_BUFFER_LENGTH];
int nextionBufferIndex = 0;

bool checkForTerminator() {
  if (nextionBufferIndex < 3)
    return false;
  return nextionBuffer[nextionBufferIndex - 3] == 0xFF &&
         nextionBuffer[nextionBufferIndex - 2] == 0xFF &&
         nextionBuffer[nextionBufferIndex - 1] == 0xFF;
}

const byte HEX_TO_KEYCODE[256] = {
    // "0," just means that the touch key is not doing anything

    // KEY-ID = COMMMAND        HEX KEY FOR NEXTION TO SEND       WHAT THE
    // COMMAND/BUTTON DOES
    [0] = 0, // printh 65 00 00 01 FF FF FF      NOT IN USE
    [1] = 0, // printh 65 00 01 01 FF FF FF      NOT IN USE
    [2] = 0, // printh 65 00 02 01 FF FF FF      NOT IN USE
    [3] = B_OFF, // printh 65 00 03 01 FF FF FF      OFF (stop, cancel, delete)
    [4] = B_MODE, // printh 65 00 04 01 FF FF FF      MODE SELECTION
    [5] =
        B_REVERSE, // printh 65 00 05 01 FF FF FF      REVERSE PITCH (+/- pitch)
    [6] = B_MEASURE, // printh 65 00 06 01 FF FF FF      UNITS (metric,
                     // imperial, TPI)
    [7] = B_STEP, // printh 65 00 07 01 FF FF FF      INCREMENTS - STEPS
    [8] = B_DISPL, // printh 65 00 08 01 FF FF FF      TOGGLES RPM/TURNS/ANGLE
                     // DISPLAY
    [9] = B_OFF, // printh 65 00 09 01 FF FF FF      tTurns (OUTPUT WHEN
                     // PRESSING THE TURN BOX) ???
    [10] = B_OFF, // printh 65 00 0A 01 FF FF FF      tAngle (OUTPUT WHEN
                     // PRESSING THE ANGLE BOX) ???
    [11] = B_X_ENA, // printh 65 00 0B 01 FF FF FF      ENABLE/DISABLE X AXIS
    [12] = B_X, // printh 65 00 0C 01 FF FF FF      ZERO X
    [13] =
        B_X_TOGGLE_DRO, // printh 65 00 0D 01 FF FF FF      Toggle X Stepper/DRO
    [14] = 0, // printh 65 00 0E 01 FF FF FF      NOT IN USE
    [15] = B_Y_ENA, // printh 65 00 0F 01 FF FF FF      ENABLE/DISABLE Y AXIS
    [16] = 0, // printh 65 00 10 01 FF FF FF      NOT IN USE
    [17] = B_Y, // printh 65 00 11 01 FF FF FF      ZERO Y
    [18] = 0, // printh 65 00 12 01 FF FF FF      NOT IN USE
    [19] = B_Z_ENA, // printh 65 00 13 01 FF FF FF      ENABLE/DISABLE Z AXIS
    [20] = 0, // printh 65 00 14 01 FF FF FF      NOT IN USE
    [21] = B_Z, // printh 65 00 15 01 FF FF FF      ZERO Z
    [22] =
        B_Z_TOGGLE_DRO, // printh 65 00 16 01 FF FF FF      Toggle Z Stepper/DRO
    [23] = B_OFF, // printh 65 00 17 01 FF FF FF      STOP BUTTON (not in use?
                  // B_OFF does its job?)
    [24] = B_BACKSPACE, // printh 65 00 18 01 FF FF FF      BACKSPACE
    [25] = B_ON, // printh 65 00 19 01 FF FF FF      PLAY, GO, RUN, ENTER, SET
    [26] = B_0, // printh 65 00 1A 01 FF FF FF      NUMPAD 0
    [27] = B_1, // printh 65 00 1B 01 FF FF FF      NUMPAD 1
    [28] = B_2, // printh 65 00 1C 01 FF FF FF      NUMPAD 2
    [29] = B_3, // printh 65 00 1D 01 FF FF FF      NUMPAD 3
    [30] = B_4, // printh 65 00 1E 01 FF FF FF      NUMPAD 4
    [31] = B_5, // printh 65 00 1F 01 FF FF FF      NUMPAD 5
    [32] = B_6, // printh 65 00 20 01 FF FF FF      NUMPAD 6
    [33] = B_7, // printh 65 00 21 01 FF FF FF      NUMPAD 7
    [34] = B_8, // printh 65 00 22 01 FF FF FF      NUMPAD 8
    [35] = B_9, // printh 65 00 23 01 FF FF FF      NUMPAD 9
    [36] = B_STOPU, // printh 65 00 24 01 FF FF FF      STOP/LIMIT UP (X)
    [37] = B_STOPD, // printh 65 00 25 01 FF FF FF      STOP/LIMIT DOWN (X)
    [38] = B_STOPF, // printh 65 00 26 01 FF FF FF      STOP/LIMIT FORWARD (Y)
    [39] = B_STOPB, // printh 65 00 27 01 FF FF FF      STOP/LIMIT BACK (Y)
    [40] = B_STOPL, // printh 65 00 28 01 FF FF FF      STOP/LIMIT LEFT (Z)
    [41] = B_STOPR, // printh 65 00 29 01 FF FF FF      STOP/LIMIT RIGHT (Z)
    [42] = B_PLUS, // printh 65 00 2A 01 FF FF FF      + (PLUS)
    [43] = B_MINUS, // printh 65 00 2B 01 FF FF FF      - (MINUS)
    [44] = B_UP, // printh 65 00 2C 01 FF FF FF      JOG UP (X)
    [45] = B_DOWN, // printh 65 00 2D 01 FF FF FF      JOG DOWN (X)
    [46] = B_FORWARD, // printh 65 00 2E 01 FF FF FF      JOG FORWARD (Y)
    [47] = B_BACK, // printh 65 00 2F 01 FF FF FF      JOG BACK (Y)
    [48] = B_LEFT, // printh 65 00 30 01 FF FF FF      JOG LEFT (Z)
    [49] = B_RIGHT, // printh 65 00 31 01 FF FF FF      JOG RIGHT (Z)
    [50] = B_MULTISTART, // printh 65 00 32 01 FF FF FF      MULTI START THREAD
    [51] = B_KNOWN_DIA, // printh 65 00 33 01 FF FF FF      DRO, setting the
                         // workpiece diameter
    [52] = B_KNOWN_LENGTH, // printh 65 00 34 01 FF FF FF      DRO, setting a Z
    [53] = B_DIA_RAD, // printh 65 00 35 01 FF FF FF      DRO, dia/rad -
                           // switching between diameter and radius mode
    [54] = B_ABS_INC, // printh 65 00 36 01 FF FF FF      DRO, abs/inc -
                           // switching between absolute and incremental mode
    [55] = B_VERSION, // printh 65 00 37 01 FF FF FF      show/hide the current
                      // FW version
    [56] = B_SPINDLE_UNITS, // printh 65 00 38 01 FF FF FF      Small button
                            // over Spindle Unit Text
    [57] = B_PITCH, // printh 65 00 39 01 FF FF FF      Pitch Box (Font/Reverse)
                    // and Font Toggling
};
// =================================================================
// Geckosub + Gemini 2.5 Pro - END OF SECTION 19
// =================================================================

int processNextionMessage() {
  if (nextionBufferIndex < 6)
    return 0;
  if (nextionBuffer[0] == 0x65 && nextionBuffer[1] == 0x00) {
    int code = HEX_TO_KEYCODE[nextionBuffer[2]];
    if (nextionBuffer[3] == 0)
      code |= PS2_BREAK;
    return code;
  }
  return 0;
}

// =================================================================
// KEYPAD EVENT HANDLER
// =================================================================
void processKeypadEvent() {
  int event = 0;
  bool isPhysicalButton = false;

  if (wsKeycode != 0) {
    event = wsKeycode;
    wsKeycode = 0;
    isPhysicalButton = true;
  } else if (keyboard.available()) {
    event = keyboard.read();
  } else if (Serial1.available() > 0) {
    byte incomingByte = Serial1.read();
    if (nextionBufferIndex < NEXTION_BUFFER_LENGTH) {
      nextionBuffer[nextionBufferIndex] = incomingByte;
      nextionBufferIndex++;
    } else {
      nextionBufferIndex = 0;
    }
    if (checkForTerminator()) {
      event = processNextionMessage();
      nextionBufferIndex = 0;
    }
  }
  if (event == 0)
    return;

  // --- RESTORED FROM V98: Dynamic Jog Delay ---
  // Sets the repeat delay based on input source.
  if (isPhysicalButton) {
    jogDelayMs = 100; // External Buttons: Fast/Snappy response
  } else {
    jogDelayMs = 300; // Touchscreen: Safe to prevent double-taps
  }
  // ---------------------------------------------

  int keyCode = event & 0xFF;
  bool isPress = !(event & PS2_BREAK);
  keypadTimeUs = micros();

  // --- FIX: Restore External/Internal Toggle (Left/Right) ---
  // Intercept Left/Right keys when in Setup Step 2 (Ext/Int Prompt)
  // Toggle the value and suppress the jog action.
  // Applied to: Thread, Turn, Face, Cone, Ellipse. (Cut uses Pitch sign, not
  // this toggle).
  bool isExtIntMode =
      (mode == MODE_THREAD || mode == MODE_TURN || mode == MODE_FACE ||
       mode == MODE_CONE || mode == MODE_ELLIPSE);
  if (isPress && isExtIntMode && setupIndex == 2 &&
      (keyCode == B_LEFT || keyCode == B_RIGHT)) {
    auxForward = !auxForward;
    beep();
    // Suppress the press so it doesn't trigger jogging logic below
    isPress = false;
  }
  // ----------------------------------------------------------

  if (keyCode == B_LEFT) {
    buttonLeftPressed = isPress;
  } else if (keyCode == B_RIGHT) {
    buttonRightPressed = isPress;
  } else if (keyCode == B_UP) {
    buttonUpPressed = isPress;
  } else if (keyCode == B_DOWN) {
    buttonDownPressed = isPress;
  } else if (keyCode == B_STOPL) {
    buttonStopLPressed = isPress;
  } else if (isPhysicalButton && (keyCode == B_0 || keyCode == B_ON)) {
    buttonZeroPressed = isPress;
  } else if (isPhysicalButton && (keyCode == B_OFF || keyCode == B_5)) {
    buttonFivePressed = isPress;
  } else if (isPhysicalButton && (keyCode == B_KNOWN_DIA || keyCode == B_2)) {
    buttonTwoPressed = isPress;
  } else if (keyCode == B_PLUS) {
    buttonPlusPressed = isPress;
  } else if (keyCode == B_MINUS) {
    buttonMinusPressed = isPress;
  } else if (keyCode == B_X) {
    buttonXPressed = isPress;
  } else if (keyCode == B_Z) {
    buttonZPressed = isPress;
  } else if (keyCode == B_PITCH) {
    buttonPitchPressed = isPress;
  } else if (keyCode == B_1) {
    buttonOnePressed = isPress;
    // UNGUARDED: Works for Touch AND Physical (Keep Logic for #1)
  } else if (keyCode == B_X_ENA) {
    buttonXEnaPressed = isPress;
  } else if (keyCode == B_Z_ENA) {
    buttonZEnaPressed = isPress;
  } else if (keyCode == B_DISPL) {
    buttonDisplayPressed = isPress;
  }

  if (isPress) {
    if (!isPhysicalButton) {
      switch (keyCode) {
      case B_0:
      case B_1: // CHANGED: Handle B_1 immediately for touch to restore short
                // press
        buttonOnePressed = false; // Prevent Main Loop state machine
                                  // (Multistart) from seeing this
        processNumpad(keyCode);
        return;
      case B_2:
      case B_3:
      case B_4:
      case B_5:
      case B_6:
      case B_7:
      case B_8:
      case B_9:
      case B_BACKSPACE:
        processNumpad(keyCode);
        return;
      case B_ON:
        if (inNumpad)
          processNumpad(B_ON);
        else
          buttonOnOffPress(true);
        return;
      case B_OFF:
        buttonOnOffPress(false);
        return;
      }
    }

    // CHANGED: B_1 is included here for both.
    // 0, 2, 5 are included but their flags are guarded above.
    bool isHandledByStateMachine =
        (keyCode == B_ON || keyCode == B_OFF || keyCode == B_0 ||
         keyCode == B_5 || keyCode == B_2 || keyCode == B_KNOWN_DIA ||
         keyCode == B_MODE_THREAD || keyCode == B_X || keyCode == B_Z ||
         keyCode == B_STOPL || keyCode == B_PLUS || keyCode == B_MINUS ||
         keyCode == B_LEFT || keyCode == B_RIGHT || keyCode == B_UP ||
         keyCode == B_DOWN || keyCode == B_PITCH || keyCode == B_1 ||
         keyCode == B_X_ENA || keyCode == B_Z_ENA || keyCode == B_DISPL);
    if (isHandledByStateMachine) {
    } else {
      if (processNumpad(keyCode))
        return;
      switch (keyCode) {
      case B_STOPR:
        buttonRightStopPress(&z);
        break;
      case B_STOPU:
        buttonLeftStopPress(&x);
        break;
      case B_STOPD:
        buttonRightStopPress(&x);
        break;
      case B_STOPF:
        if (ACTIVE_Y)
          buttonLeftStopPress(&y);
        break;
      case B_STOPB:
        if (ACTIVE_Y)
          buttonRightStopPress(&y);
        break;
      case B_MODE_Y:
        if (ENABLE_MODE_Y && y.active)
          setModeFromTask(MODE_Y);
        break;
      case B_MODE_ELLIPSE:
        if (ENABLE_MODE_ELLIPSE)
          setModeFromTask(MODE_ELLIPSE);
        break;
      case B_MODE_GCODE:
        if (ENABLE_MODE_GCODE)
          setModeFromTask(MODE_GCODE);
        break;
      case B_MODE_ASYNC:
        if (ENABLE_MODE_ASYNC)
          setModeFromTask(MODE_ASYNC);
        break;
      case B_MULTISTART:
        buttonMultistartPress();
        break;
      case B_KNOWN_LENGTH:
        numpadEntryMode = LENGTH;
        inNumpad = true;
        resetNumpad();
        beep();
        break;
      case B_Y:
        if (ACTIVE_Y)
          markAxis0(&y);
        break;
      case B_DIA_RAD:
        isDiameterMode = !isDiameterMode;
        beep();
        break;
      case B_ABS_INC:
        isIncrementalMode = !isIncrementalMode;
        beep();
        break;
      case B_Y_ENA:
        if (ACTIVE_Y) {
          y.disabled = !y.disabled;
          updateEnable(&y);
        }
        break;
      // RESTORED: Standard B_STEP Logic (Short Press Only)
      case B_STEP:
        if (millis() - g_comboActionLastActive > 500) {
          buttonMoveStepPress();
        }
        break;

      case B_REVERSE:
        buttonReversePress();
        break;
      case B_MEASURE:
        buttonMeasurePress();
        break;
      case B_MODE_GEARS:
        if (ENABLE_MODE_GEAR)
          setModeFromTask(MODE_NORMAL);
        break;
      case B_MODE_TURN:
        if (ENABLE_MODE_TURN)
          setModeFromTask(MODE_TURN);
        break;
      case B_MODE:
        if (numActiveModes > 0) {
          int currentModeIndex = -1;
          for (int i = 0; i < numActiveModes; i++) {
            if (mode == activeModes[i]) {
              currentModeIndex = i;
              break;
            }
          }
          int newModeIndex = 0;
          if (currentModeIndex != -1) {
            newModeIndex = (currentModeIndex + 1) % numActiveModes;
          }
          setModeFromTask(activeModes[newModeIndex]);
        }
        break;
      case B_MODE_FACE:
        if (ENABLE_MODE_FACE)
          setModeFromTask(MODE_FACE);
        break;
      case B_MODE_CONE:
        if (ENABLE_MODE_CONE)
          setModeFromTask(MODE_CONE);
        break;
      case B_MODE_CUT:
        if (ENABLE_MODE_CUT)
          setModeFromTask(MODE_CUT);
        break;
      case B_X_TOGGLE_DRO:
        xDisplayModeDRO = !xDisplayModeDRO;
        // Disabling ELS/DRO readout toggling for users without linear scales
        beep();
        break;
      case B_Z_TOGGLE_DRO:
        zDisplayModeDRO = !zDisplayModeDRO;
        // Disabling ELS/DRO readout toggling for users without linear scales
        beep();
        break;
      case B_VERSION:
        versionVisible = !versionVisible;
        if (versionVisible) {
          versionTimerStart = millis();
          setText("tVersion", "NanoELS-DRO V" + String(SOFTWARE_VERSION));
        } else {
          setText("tVersion", "");
        }
        break;
      case B_SPINDLE_UNITS:
        spindleValColorGreen = !spindleValColorGreen;
        beep();
        break;
      }
    }
  } else { // on release
    // --- BUG FIX ---
    // Only call buttonOffRelease for non-physical buttons (like the
    // touchscreen). The physical button's logic is handled by its state machine
    // in loop().
    if (keyCode == B_OFF && !isPhysicalButton) {
      buttonOffRelease();
    }
    // --- END BUG FIX ---
  }
}

// =================================================================
// PHYSICAL BUTTON HANDLER
// =================================================================
void handlePhysicalButtons() {
  if (wsKeycode != 0) {
    return;
  }

  for (int i = 0; i < NUM_PHYSICAL_BUTTONS; i++) {
    bool currentReading = digitalRead(physicalButtons[i].PIN);

    if (currentReading != physicalButtons[i].lastReading) {
      physicalButtons[i].lastDebounceTime = millis();
    }

    if ((millis() - physicalButtons[i].lastDebounceTime) >
        BUTTON_DEBOUNCE_DELAY) {
      if (currentReading != physicalButtons[i].confirmedState) {
        physicalButtons[i].confirmedState = currentReading;

        // Determine which keycode to send based on the current mode.
        int keycodeToSend = 0;

        // =================================================================
        // Geckosub + Gemini 2.5 Pro
        // The B_NUMPAD button is a special case and must always be sent,
        // regardless of whether numpad mode is active or not.
        // =================================================================
        if (physicalButtons[i].normalKeycode == B_NUMPAD) {
          keycodeToSend = B_NUMPAD;
        } else if (physicalNumpadModeActive) {
          keycodeToSend = physicalButtons[i].numpadKeycode;
        } else {
          keycodeToSend = physicalButtons[i].normalKeycode;
        }
        // =================================================================

        // Only send a keycode if one is defined for the current mode
        if (keycodeToSend != 0) {
          if (currentReading == LOW) { // PRESS event
            wsKeycode = keycodeToSend;
            break;
          } else { // RELEASE event
            wsKeycode = keycodeToSend | PS2_BREAK;
            break;
          }
        }
      }
    }
    physicalButtons[i].lastReading = currentReading;
  }
}
// =================================================================
// END OF SECTION
// =================================================================

void taskKeypad(void *param) {
  while (emergencyStop == ESTOP_NONE) {
    processKeypadEvent();
    taskYIELD();
  }
  vTaskDelete(NULL);
}

void moveAxis(Axis *a) {
  // Most of the time a step isn't needed.
  if (a->pendingPos == 0) {
    if (a->speed > a->speedStart) {
      a->speed--;
    }
    return;
  }

  unsigned long nowUs = micros();
  float delayUs = 1000000.0 / a->speed;
  if (nowUs - a->stepStartUs < delayUs - 5) {
    // Not enough time has passed to issue this step.
    return;
  }

  if (xSemaphoreTake(a->mutex, 1) == pdTRUE) {
    // Check pendingPos again now that we have the mutex.
    if (a->pendingPos != 0) {
      bool dir = a->pendingPos > 0;
      setDir(a, dir);

      DLOW(a->step);
      int delta = dir ? 1 : -1;
      a->pendingPos -= delta;
      if (dir && a->motorPos >= a->pos) {
        a->pos++;
      } else if (!dir && a->motorPos <= (a->pos - a->backlashSteps)) {
        a->pos--;
      }
      a->motorPos += delta;
      a->posGlobal += delta;

      bool accelerate = a->continuous || a->pendingPos >= a->decelerateSteps ||
                        a->pendingPos <= -a->decelerateSteps;
      a->speed += (accelerate ? 1 : -1) * a->acceleration * delayUs / 1000000.0;
      if (a->speed > a->speedMax) {
        a->speed = a->speedMax;
      } else if (a->speed < a->speedStart) {
        a->speed = a->speedStart;
      }
      a->stepStartUs = nowUs;

      DHIGH(a->step);
    }
    xSemaphoreGive(a->mutex);
  }
}

void modeGearbox() {
  if (z.movingManually) {
    return;
  }
  z.speedMax = LONG_MAX;
  stepToContinuous(&z, posFromSpindle(&z, spindlePosAvg, true));
}

long auxSafeDistance, startOffset;

// =================================================================
// TURN MODE
// =================================================================
void modeTurn(Axis *main, Axis *aux) {
  if (main->movingManually || aux->movingManually || turnPasses <= 0 ||
      main->leftStop == LONG_MAX || main->rightStop == LONG_MIN ||
      aux->leftStop == LONG_MAX || aux->rightStop == LONG_MIN || dupr == 0 ||
      (dupr * opDuprSign < 0) || starts < 1) {
    setIsOnFromLoop(false);
    return;
  }

  // Variables below have to be re-calculated every time because origin can
  // change while TURN is running e.g. due to dupr change.
  long mainStartStop = opDuprSign > 0 ? main->rightStop : main->leftStop;
  long mainEndStop = opDuprSign > 0 ? main->leftStop : main->rightStop;
  long auxStartStop = auxForward ? aux->rightStop : aux->leftStop;
  long auxEndStop = auxForward ? aux->leftStop : aux->rightStop;

  // opIndex 0 is only executed once, do setup calculations here.
  if (opIndex == 0) {
    auxSafeDistance = (auxForward ? -1 : 1) * SAFE_DISTANCE_DU *
                      aux->motorSteps / aux->screwPitch;
    startOffset = starts == 1 ? 0 : round(ENCODER_STEPS_FLOAT / starts);

    // Move to right-bottom limit.
    main->speedMax = main->speedManualMove;
    aux->speedMax = aux->speedManualMove;
    long auxPos = auxStartStop;
    // Overstep by 1 so that "main" backlash is taken out before "opSubIndex ==
    // 1".
    long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      stepToFinal(main, mainStartStop);
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses * starts) {
    if (opIndexAdvanceFlag && (opIndex + starts) < turnPasses * starts) {
      opIndexAdvanceFlag = false;
      opIndex += starts;
    }
    long auxPos = auxEndStop - (auxEndStop - auxStartStop) / turnPasses *
                                   (turnPasses - ceil(opIndex / float(starts)));
    // Bringing X to starting position.
    if (opSubIndex == 0) {
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 1;
        spindlePosSync = spindleModulo(spindlePosGlobal -
                                       spindleFromPos(main, main->posGlobal) +
                                       startOffset * (opIndex - 1));
        return;
        // Instead of jumping to the next step, let spindlePosSync get to 0
        // first.
      }
    }
    // spindlePosSync counted down to 0, start thread from here.
    if (opSubIndex == 1) {
      markOrigin();
      main->speedMax = LONG_MAX;
      opSubIndex = 2;
      // markOrigin() changed Start/EndStop values, re-calculate them.
      return;
    }
    // Doing the pass cut.
    if (opSubIndex == 2) {
      // In case we were pushed to the next opIndex before finishing the current
      // one.
      long mainTargetPos = posFromSpindle(main, spindlePosAvg, true);
      long auxTargetPos = auxPos;
      if (mode == MODE_THREAD && coneRatio != 0) {
        float coneEffectRatio = -coneRatio / 2 / main->motorSteps *
                                aux->motorSteps / aux->screwPitch *
                                main->screwPitch * (auxForward ? 1 : -1);
        auxTargetPos = auxPos + round(mainTargetPos * coneEffectRatio);
      }

      if (auxTargetPos > aux->leftStop)
        auxTargetPos = aux->leftStop;
      if (auxTargetPos < aux->rightStop)
        auxTargetPos = aux->rightStop;

      stepToContinuous(main, mainTargetPos);
      stepToContinuous(aux, auxTargetPos);
      if (main->pos == mainEndStop ||
          (coneRatio != 0 &&
           aux->pos == (opDuprSign > 0 ? auxStartStop : auxEndStop))) {
        // --- NEW: PAUSE AT END OF CUT ---
        awaitingButtonPress = true;
        opSubIndex = 5;
        // --------------------------------
      }
    }
    // Retracting the tool
    if (opSubIndex == 3) {
      long auxPos = auxStartStop + auxSafeDistance;
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 4;
      }
    }
    // Returning to start of main.
    if (opSubIndex == 4) {
      main->speedMax = main->speedManualMove;
      // Overstep by 1 so that "main" backlash is taken out before "opSubIndex
      // == 2".
      long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
      stepToFinal(main, mainPos);
      if (main->pos == mainPos) {
        stepToFinal(main, mainStartStop);
        // --- NEW: PAUSE AT START OF NEXT PASS ---
        awaitingButtonPress = true;
        opSubIndex = 6;
        // ----------------------------------------
      }
    }
    // --- New States for Dwelling ---
    if (opSubIndex == 5) { // Dwelling at the end of the cut
      if (!awaitingButtonPress) {
        opSubIndex = 3; // Button pressed, proceed to retract
      }
    }
    if (opSubIndex == 6) { // Dwelling at the start before next cut
      if (!awaitingButtonPress) {
        opIndex++; // Button pressed, increment pass
        opSubIndex = 0; // Proceed to plunge
      }
    }
  } else {
    // Move to right-bottom limit.
    main->speedMax = main->speedManualMove;
    long auxPos = auxStartStop;
    long mainPos = mainStartStop;
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      setIsOnFromLoop(false);
      beep();
    }
  }
}
// =================================================================
// Geckosub + Gemini 2.5 Pro - SECTION 21B - TURN MODE END
// =================================================================

// =================================================================
// Geckosub + Gemini 2.5 Pro - SECTION 22 - THREAD MODE WITH BUILT-IN PAUSE
// (threading w.o. X-stepper)
// =================================================================
void modeThreadPaused(Axis *main, Axis *aux) {
  if (main->movingManually || aux->movingManually || turnPasses <= 0 ||
      main->leftStop == LONG_MAX || main->rightStop == LONG_MIN ||
      aux->leftStop == LONG_MAX || aux->rightStop == LONG_MIN || dupr == 0 ||
      (dupr * opDuprSign < 0) || starts < 1) {
    setIsOnFromLoop(false);
    return;
  }

  long mainStartStop = opDuprSign > 0 ? main->rightStop : main->leftStop;
  long mainEndStop = opDuprSign > 0 ? main->leftStop : main->rightStop;
  long auxStartStop = auxForward ? aux->rightStop : aux->leftStop;
  long auxEndStop = auxForward ? aux->leftStop : aux->rightStop;

  if (opIndex == 0) {
    auxSafeDistance = (auxForward ? -1 : 1) * SAFE_DISTANCE_DU *
                      aux->motorSteps / aux->screwPitch;
    startOffset = starts == 1 ? 0 : round(ENCODER_STEPS_FLOAT / starts);
    main->speedMax = main->speedManualMove;
    aux->speedMax = aux->speedManualMove;
    long auxPos = auxStartStop;
    long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      stepToFinal(main, mainStartStop);
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses * starts) {
    if (opIndexAdvanceFlag && (opIndex + starts) < turnPasses * starts) {
      opIndexAdvanceFlag = false;
      opIndex += starts;
    }
    long auxPos = auxEndStop - (auxEndStop - auxStartStop) / turnPasses *
                                   (turnPasses - ceil(opIndex / float(starts)));

    if (opSubIndex == 0) { // Plunging in X
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 1;
        spindlePosSync = spindleModulo(spindlePosGlobal -
                                       spindleFromPos(main, main->posGlobal) +
                                       startOffset * (opIndex - 1));
        return;
      }
    }
    if (opSubIndex == 1) { // Waiting for spindle sync
      markOrigin();
      main->speedMax = LONG_MAX;
      opSubIndex = 2;
      return;
    }
    if (opSubIndex == 2) { // The main cutting pass
      long mainTargetPos = posFromSpindle(main, spindlePosAvg, true);
      long auxTargetPos = auxPos;
      if (coneRatio != 0) {
        float coneEffectRatio = -coneRatio / 2 / main->motorSteps *
                                aux->motorSteps / aux->screwPitch *
                                main->screwPitch * (auxForward ? 1 : -1);
        auxTargetPos = auxPos + round(mainTargetPos * coneEffectRatio);
      }
      if (auxTargetPos > aux->leftStop)
        auxTargetPos = aux->leftStop;
      if (auxTargetPos < aux->rightStop)
        auxTargetPos = aux->rightStop;
      stepToContinuous(main, mainTargetPos);
      stepToContinuous(aux, auxTargetPos);
      if (main->pos == mainEndStop ||
          (coneRatio != 0 &&
           aux->pos == (opDuprSign > 0 ? auxStartStop : auxEndStop))) {
        awaitingButtonPress = true; // Set the flag to wait for button
        opSubIndex = 5; // Go to "Dwell at End" state
      }
    }
    if (opSubIndex == 3) { // Retracting the tool
      long auxPos = auxStartStop + auxSafeDistance;
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 4;
      }
    }
    if (opSubIndex == 4) { // Returning to start Z
      main->speedMax = main->speedManualMove;
      long mainPos = mainStartStop + (opDuprSign > 0 ? -1 : 1);
      stepToFinal(main, mainPos);
      if (main->pos == mainPos) {
        stepToFinal(main, mainStartStop);
        awaitingButtonPress = true; // Set the flag to wait for button
        opSubIndex = 6; // Go to "Dwell at Start" state
      }
    }
    // --- New States for Dwelling ---
    if (opSubIndex == 5) { // Dwelling at the end of the cut
      if (!awaitingButtonPress) {
        opSubIndex = 3; // Button was pressed, proceed to retract
      }
    }
    if (opSubIndex == 6) { // Dwelling at the start before next cut
      if (!awaitingButtonPress) {
        opIndex++; // Button was pressed, increment pass index
        opSubIndex = 0; // Proceed to plunge for next pass
      }
    }
  } else {
    main->speedMax = main->speedManualMove;
    long auxPos = auxStartStop;
    long mainPos = mainStartStop;
    stepToFinal(main, mainPos);
    stepToFinal(aux, auxPos);
    if (main->pos == mainPos && aux->pos == auxPos) {
      setIsOnFromLoop(false);
      beep();
    }
  }
}

void modeCone() {
  if (z.movingManually || x.movingManually || coneRatio == 0) {
    return;
  }

  float zToXRatio = -coneRatio / 2 / z.motorSteps * x.motorSteps /
                    x.screwPitch * z.screwPitch * (auxForward ? 1 : -1);
  if (zToXRatio == 0) {
    return;
  }

  // TODO: calculate maximum speeds and accelerations to avoid potential desync.
  x.speedMax = LONG_MAX;
  z.speedMax = LONG_MAX;

  // Respect limits of both axis by translating them into limits on spindlePos
  // value.
  long spindle = spindlePosAvg;
  long spindleMin = LONG_MIN;
  long spindleMax = LONG_MAX;
  if (z.leftStop != LONG_MAX) {
    (dupr > 0 ? spindleMax : spindleMin) = spindleFromPos(&z, z.leftStop);
  }
  if (z.rightStop != LONG_MIN) {
    (dupr > 0 ? spindleMin : spindleMax) = spindleFromPos(&z, z.rightStop);
  }
  if (x.leftStop != LONG_MAX) {
    long lim = spindleFromPos(&z, round(x.leftStop / zToXRatio));
    if (zToXRatio < 0) {
      (dupr > 0 ? spindleMin : spindleMax) = lim;
    } else {
      (dupr > 0 ? spindleMax : spindleMin) = lim;
    }
  }
  if (x.rightStop != LONG_MIN) {
    long lim = spindleFromPos(&z, round(x.rightStop / zToXRatio));
    if (zToXRatio < 0) {
      (dupr > 0 ? spindleMax : spindleMin) = lim;
    } else {
      (dupr > 0 ? spindleMin : spindleMax) = lim;
    }
  }
  if (spindle > spindleMax) {
    spindle = spindleMax;
  } else if (spindle < spindleMin) {
    spindle = spindleMin;
  }

  stepToContinuous(&z, posFromSpindle(&z, spindle, true));
  stepToContinuous(&x, round(z.pos * zToXRatio));
}

void modeCut() {
  if (x.movingManually || turnPasses <= 0 || x.leftStop == LONG_MAX ||
      x.rightStop == LONG_MIN || dupr == 0 || dupr * opDuprSign < 0) {
    setIsOnFromLoop(false);
    return;
  }

  long startStop = opDuprSign > 0 ? x.rightStop : x.leftStop;
  long endStop = opDuprSign > 0 ? x.leftStop : x.rightStop;

  if (opIndex == 0) {
    // Move to back limit.
    x.speedMax = x.speedManualMove;
    long xPos = startStop;
    stepToFinal(&x, xPos);
    if (x.pos == xPos) {
      opIndex = 1;
      opSubIndex = 0;
    }
  } else if (opIndex <= turnPasses) {
    // Set spindlePos and x.pos in sync.
    if (opSubIndex == 0) {
      spindlePosAvg = spindlePos = spindleFromPos(&x, x.pos);
      opSubIndex = 1;
    }
    // Doing the pass cut.
    if (opSubIndex == 1) {
      x.speedMax = LONG_MAX;
      long endPos =
          endStop - (endStop - startStop) / turnPasses * (turnPasses - opIndex);
      long xPos = posFromSpindle(&x, spindlePosAvg, true);
      if (dupr > 0 && xPos > endPos)
        xPos = endPos;
      else if (dupr < 0 && xPos < endPos)
        xPos = endPos;
      stepToContinuous(&x, xPos);
      if (x.pos == endPos) {
        opSubIndex = 2;
      }
    }
    // Returning to start.
    if (opSubIndex == 2) {
      x.speedMax = x.speedManualMove;
      stepToFinal(&x, startStop);
      if (x.pos == startStop) {
        opSubIndex = 0;
        opIndex++;
      }
    }
  } else {
    setIsOnFromLoop(false);
    beep();
  }
}

void modeEllipse(Axis *main, Axis *aux) {
  if (main->movingManually || aux->movingManually || turnPasses <= 0 ||
      main->leftStop == LONG_MAX || main->rightStop == LONG_MIN ||
      aux->leftStop == LONG_MAX || aux->rightStop == LONG_MIN ||
      main->leftStop == main->rightStop || aux->leftStop == aux->rightStop ||
      dupr == 0 || dupr != opDupr) {
    setIsOnFromLoop(false);
    return;
  }

  // Start from left or right depending on the pitch.
  long mainStartStop = opDuprSign > 0 ? main->rightStop : main->leftStop;
  long mainEndStop = opDuprSign > 0 ? main->leftStop : main->rightStop;
  long auxStartStop = aux->rightStop;
  long auxEndStop = aux->leftStop;

  main->speedMax = main->speedManualMove;
  aux->speedMax = aux->speedManualMove;

  if (opIndex == 0) {
    opIndex = 1;
    opSubIndex = 0;
    spindlePos = 0;
    spindlePosAvg = 0;
  } else if (opIndex <= turnPasses) {
    float pass0to1 = opIndex / float(turnPasses);
    long mainDelta = round(pass0to1 * (mainEndStop - mainStartStop));
    long auxDelta = round(pass0to1 * (auxEndStop - auxStartStop));
    long spindleDelta = spindleFromPos(main, mainDelta);

    // Move to starting position.
    if (opSubIndex == 0) {
      long auxPos = auxStartStop;
      stepToFinal(aux, auxPos);
      if (aux->pos == auxPos) {
        opSubIndex = 1;
      }
    } else if (opSubIndex == 1) {
      long mainPos = mainEndStop - mainDelta;
      stepToFinal(main, mainPos);
      if (main->pos == mainPos) {
        opSubIndex = 2;
        spindlePos = 0;
        spindlePosAvg = 0;
      }
    } else if (opSubIndex == 2) {
      float progress0to1 = 0;
      if ((spindleDelta > 0 && spindlePosAvg >= spindleDelta) ||
          (spindleDelta < 0 && spindlePosAvg <= spindleDelta)) {
        progress0to1 = 1;
      } else {
        progress0to1 = spindlePosAvg / float(spindleDelta);
      }
      float mainCoeff = auxForward ? cos(HALF_PI * (3 + progress0to1))
                                   : (1 + sin(HALF_PI * (progress0to1 - 1)));
      long mainPos = mainEndStop - mainDelta + round(mainDelta * mainCoeff);
      float auxCoeff = auxForward ? (1 + sin(HALF_PI * (3 + progress0to1)))
                                  : sin(HALF_PI * progress0to1);
      long auxPos = auxStartStop + round(auxDelta * auxCoeff);
      stepToContinuous(main, mainPos);
      stepToContinuous(aux, auxPos);
      if (progress0to1 == 1 && main->pos == mainPos && aux->pos == auxPos) {
        opIndex++;
        opSubIndex = 0;
      }
    }
  } else if (opIndex == turnPasses + 1) {
    stepToFinal(aux, auxStartStop);
    if (aux->pos == auxStartStop) {
      setIsOnFromLoop(false);
      beep();
    }
  }
}

void discountFullSpindleTurns() {
  // When standing at the stop, ignore full spindle turns.
  // This allows to avoid waiting when spindle direction reverses
  // and reduces the chance of the skipped stepper steps since
  // after a reverse the spindle starts slow.
  if (dupr != 0 && !stepperIsRunning(&z) &&
      (mode == MODE_NORMAL || mode == MODE_CONE)) {
    int spindlePosDiff = 0;
    if (z.pos == z.rightStop) {
      long stopSpindlePos = spindleFromPos(&z, z.rightStop);
      if (dupr > 0) {
        if (spindlePos < stopSpindlePos - ENCODER_STEPS_INT) {
          spindlePosDiff = ENCODER_STEPS_INT;
        }
      } else {
        if (spindlePos > stopSpindlePos + ENCODER_STEPS_INT) {
          spindlePosDiff = -ENCODER_STEPS_INT;
        }
      }
    } else if (z.pos == z.leftStop) {
      long stopSpindlePos = spindleFromPos(&z, z.leftStop);
      if (dupr > 0) {
        if (spindlePos > stopSpindlePos + ENCODER_STEPS_INT) {
          spindlePosDiff = -ENCODER_STEPS_INT;
        }
      } else {
        if (spindlePos < stopSpindlePos - ENCODER_STEPS_INT) {
          spindlePosDiff = ENCODER_STEPS_INT;
        }
      }
    }
    if (spindlePosDiff != 0) {
      spindlePos += spindlePosDiff;
      spindlePosAvg += spindlePosDiff;
    }
  }
}

void processSpindleCounter() {
  int16_t count;
  pcnt_get_counter_value(PCNT_UNIT_0, &count);
  int delta = count - spindleCount;
  if (delta == 0) {
    return;
  }
  if (count >= PCNT_CLEAR || count <= -PCNT_CLEAR) {
    pcnt_counter_clear(PCNT_UNIT_0);
    spindleCount = 0;
  } else {
    spindleCount = count;
  }

  unsigned long microsNow = micros();
  if (spindleDisplayMode == 0 || mode == MODE_GCODE) { // 0 is the mode for RPM
    if (spindleEncTimeIndex >= RPM_BULK) {
      spindleEncTimeDiffBulk = microsNow - spindleEncTimeAtIndex0;
      spindleEncTimeAtIndex0 = microsNow;
      spindleEncTimeIndex = 0;
    }
    spindleEncTimeIndex += abs(delta);
  } else {
    spindleEncTimeDiffBulk = 0;
  }

  spindlePos += delta;
  spindlePosGlobal += delta;
  if (spindlePosGlobal > ENCODER_STEPS_INT) {
    spindlePosGlobal -= ENCODER_STEPS_INT;
  } else if (spindlePosGlobal < 0) {
    spindlePosGlobal += ENCODER_STEPS_INT;
  }
  if (spindlePos > spindlePosAvg) {
    spindlePosAvg = spindlePos;
  } else if (spindlePos < spindlePosAvg - ENCODER_BACKLASH) {
    spindlePosAvg = spindlePos + ENCODER_BACKLASH;
  }
  spindleEncTime = microsNow;

  if (spindlePosSync != 0) {
    spindlePosSync += delta;
    if (spindlePosSync % ENCODER_STEPS_INT == 0) {
      spindlePosSync = 0;
      Axis *a = getPitchAxis();
      spindlePosAvg = spindlePos = spindleFromPos(a, a->pos);
    }
  }
}

// Apply changes requested by the keyboard thread.
void applySettings() {
  if (nextDuprFlag) {
    applyDupr();
    // --- FIX: Update the backing storage immediately when pitch changes ---
    if (mode == MODE_THREAD) {
      threadDupr = dupr;
    } else {
      gearDupr = dupr;
    }

    nextDuprFlag = false;
  }
  if (nextStartsFlag) {
    applyStarts();
    nextStartsFlag = false;
  }
  if (z.nextLeftStopFlag) {
    applyLeftStop(&z);
    z.nextLeftStopFlag = false;
  }
  if (z.nextRightStopFlag) {
    applyRightStop(&z);
    z.nextRightStopFlag = false;
  }
  if (x.nextLeftStopFlag) {
    applyLeftStop(&x);
    x.nextLeftStopFlag = false;
  }
  if (x.nextRightStopFlag) {
    applyRightStop(&x);
    x.nextRightStopFlag = false;
  }
  if (y.nextLeftStopFlag) {
    applyLeftStop(&y);
    y.nextLeftStopFlag = false;
  }
  if (y.nextRightStopFlag) {
    applyRightStop(&y);
    y.nextRightStopFlag = false;
  }
  if (nextConeRatioFlag) {
    applyConeRatio();
    nextConeRatioFlag = false;
  }
  if (nextIsOnFlag) {
    setIsOnFromLoop(nextIsOn);
    nextIsOnFlag = false;
  }
  if (nextModeFlag) {
    setModeFromLoop(nextMode);
    nextModeFlag = false;
  }
}

// =================================================================
// Setup Function
// =================================================================
void setup() {
  pinMode(ENC_A, INPUT_PULLUP);
  pinMode(ENC_B, INPUT_PULLUP);
  pinMode(Z_DIR, OUTPUT);
  pinMode(Z_STEP, OUTPUT);
  pinMode(Z_ENA, OUTPUT);
  DHIGH(Z_STEP);

  pinMode(X_DIR, OUTPUT);
  pinMode(X_STEP, OUTPUT);
  pinMode(X_ENA, OUTPUT);
  DHIGH(X_STEP);
  if (ACTIVE_Y) {
    pinMode(Y_DIR, OUTPUT);
    pinMode(Y_STEP, OUTPUT);
    pinMode(Y_ENA, OUTPUT);
    DHIGH(Y_STEP);
  }

  for (int i = 0; i < NUM_PHYSICAL_BUTTONS; i++) {
    pinMode(physicalButtons[i].PIN, INPUT_PULLUP);
  }

  // --- Build the dynamic list of active modes based on config ---
  if (ENABLE_MODE_GEAR)
    activeModes[numActiveModes++] = MODE_NORMAL;
  if (ENABLE_MODE_TURN)
    activeModes[numActiveModes++] = MODE_TURN;
  if (ENABLE_MODE_FACE)
    activeModes[numActiveModes++] = MODE_FACE;
  if (ENABLE_MODE_CONE)
    activeModes[numActiveModes++] = MODE_CONE;
  if (ENABLE_MODE_CUT)
    activeModes[numActiveModes++] = MODE_CUT;
  if (ENABLE_MODE_THREAD)
    activeModes[numActiveModes++] = MODE_THREAD;
  if (ENABLE_MODE_GCODE)
    activeModes[numActiveModes++] = MODE_GCODE;
  if (ENABLE_MODE_ASYNC)
    activeModes[numActiveModes++] = MODE_ASYNC;
  if (ENABLE_MODE_Y && y.active)
    activeModes[numActiveModes++] = MODE_Y;
  if (ENABLE_MODE_ELLIPSE)
    activeModes[numActiveModes++] = MODE_ELLIPSE;
  // --- End of new logic ---

  Preferences pref;
  pref.begin(PREF_NAMESPACE);
  if (pref.getInt(PREF_VERSION) != PREFERENCES_VERSION) {
    pref.clear();
    pref.putInt(PREF_VERSION, PREFERENCES_VERSION);
  }

  initAxis(&z, NAME_Z, true, false, MOTOR_STEPS_Z, SCREW_Z_DU, SPEED_START_Z,
           SPEED_MANUAL_MOVE_Z, ACCELERATION_Z, INVERT_Z, INVERT_Z_ENABLE,
           NEEDS_REST_Z, MAX_TRAVEL_MM_Z, BACKLASH_DU_Z, Z_ENA, Z_DIR, Z_STEP,
           Z_PULSE_A, Z_PULSE_B, PCNT_UNIT_1);
  initAxis(&x, NAME_X, true, false, MOTOR_STEPS_X, SCREW_X_DU, SPEED_START_X,
           SPEED_MANUAL_MOVE_X, ACCELERATION_X, INVERT_X, INVERT_X_ENABLE,
           NEEDS_REST_X, MAX_TRAVEL_MM_X, BACKLASH_DU_X, X_ENA, X_DIR, X_STEP,
           X_PULSE_A, X_PULSE_B, PCNT_UNIT_2);
  initAxis(&y, NAME_Y, ACTIVE_Y, ROTARY_Y, MOTOR_STEPS_Y, SCREW_Y_DU,
           SPEED_START_Y, SPEED_MANUAL_MOVE_Y, ACCELERATION_Y, INVERT_Y,
           INVERT_Y_ENABLE, NEEDS_REST_Y, MAX_TRAVEL_MM_Y, BACKLASH_DU_Y, Y_ENA,
           -1, Y_STEP, Y_PULSE_A, Y_PULSE_B, PCNT_UNIT_3);
  isOn = false;

  // --- NEW PITCH LOADING LOGIC ---
  savedGearDupr = gearDupr = pref.getLong(PREF_DUPR_GEAR);
  savedThreadDupr = threadDupr = pref.getLong(PREF_DUPR_THREAD);
  // --- END NEW LOGIC ---

  motionMutex = xSemaphoreCreateMutex();
  savedStarts = starts =
      min(STARTS_MAX, max(static_cast<int32_t>(1), pref.getInt(PREF_STARTS)));
  z.savedPos = z.pos = pref.getLong(PREF_POS_Z);
  z.savedPosGlobal = z.posGlobal = pref.getLong(PREF_POS_GLOBAL_Z);
  z.savedOriginPos = z.originPos = pref.getLong(PREF_ORIGIN_POS_Z);
  z.savedMotorPos = z.motorPos = pref.getLong(PREF_MOTOR_POS_Z);
  z.savedLeftStop = z.leftStop = pref.getLong(PREF_LEFT_STOP_Z, LONG_MAX);
  z.savedRightStop = z.rightStop = pref.getLong(PREF_RIGHT_STOP_Z, LONG_MIN);
  z.savedDisabled = z.disabled = pref.getBool(PREF_DISABLED_Z, false);
  x.savedPos = x.pos = pref.getLong(PREF_POS_X);
  x.savedPosGlobal = x.posGlobal = pref.getLong(PREF_POS_GLOBAL_X);
  x.savedOriginPos = x.originPos = pref.getLong(PREF_ORIGIN_POS_X);
  x.savedMotorPos = x.motorPos = pref.getLong(PREF_MOTOR_POS_X);
  x.savedLeftStop = x.leftStop = pref.getLong(PREF_LEFT_STOP_X, LONG_MAX);
  x.savedRightStop = x.rightStop = pref.getLong(PREF_RIGHT_STOP_X, LONG_MIN);
  x.savedDisabled = x.disabled = pref.getBool(PREF_DISABLED_X, false);
  y.savedPos = y.pos = pref.getLong(PREF_POS_Y);
  y.savedPosGlobal = y.posGlobal = pref.getLong(PREF_POS_GLOBAL_Y);
  y.savedOriginPos = y.originPos = pref.getLong(PREF_ORIGIN_POS_Y);
  y.savedMotorPos = y.motorPos = pref.getLong(PREF_MOTOR_POS_Y);
  y.savedLeftStop = y.leftStop = pref.getLong(PREF_LEFT_STOP_Y, LONG_MAX);
  y.savedRightStop = y.rightStop = pref.getLong(PREF_RIGHT_STOP_Y, LONG_MIN);
  y.savedDisabled = y.disabled = pref.getBool(PREF_DISABLED_Y, false);
  savedSpindlePos = spindlePos = pref.getLong(PREF_SPINDLE_POS);
  savedSpindlePosAvg = spindlePosAvg = pref.getLong(PREF_SPINDLE_POS_AVG);
  savedSpindlePosSync = spindlePosSync = pref.getInt(PREF_OUT_OF_SYNC);
  savedSpindlePosGlobal = spindlePosGlobal =
      pref.getLong(PREF_SPINDLE_POS_GLOBAL);
  savedMoveStep = moveStep = pref.getLong(PREF_MOVE_STEP, MOVE_STEP_1);

  setModeFromLoop(savedMode = pref.getInt(PREF_MODE));

  // --- NEW: Set the active pitch *after* loading the mode ---
  if (savedMode == MODE_THREAD) {
    dupr = threadDupr;
  } else {
    dupr = gearDupr;
  }
  nextDupr = dupr;
  // --- END NEW LOGIC ---

  savedMeasure = measure = pref.getInt(PREF_MEASURE);
  savedConeRatio = coneRatio = pref.getFloat(PREF_CONE_RATIO, coneRatio);
  savedTurnPasses = turnPasses = pref.getInt(PREF_TURN_PASSES, turnPasses);
  savedAuxForward = auxForward = pref.getBool(PREF_AUX_FORWARD, true);
  pref.end();
  if (!z.needsRest && !z.disabled)
    digitalWrite(z.ena, z.invertEnable ? LOW : HIGH);
  if (!x.needsRest && !x.disabled)
    digitalWrite(x.ena, x.invertEnable ? LOW : HIGH);
  if (y.active && !y.needsRest && !y.disabled)
    digitalWrite(y.ena, y.invertEnable ? LOW : HIGH);
  if (LittleFS.begin(true)) {
    gcodeProgramCount = getGcodeProgramCount();
  }

  // Debug.
  Serial.begin(115200);
  // Initialize the DRO Scale hardware
  xScale.attach();
  zScale.attach();

  // Nextion.
  Serial1.begin(115200, SERIAL_8N1, 44, 43);
  // Initialize the keyboard.
  // keyboard.begin(KEY_DATA, KEY_CLOCK);
  xTaskCreatePinnedToCore(taskKeypad, "taskKeypad", 10000 /* stack size */,
                          NULL, 0 /* priority */, NULL, 0 /* core */);
  // Non-time-sensitive tasks on core 0.
  delay(1300); // Nextion needs time to boot or first display update will be
               // ignored.
  // SET THE FIRMWARE VERSION TEXT AND FONT ONCE AT STARTUP
  toScreen("page 0");
  // Force display to the main page (page 0) to ensure objects are accessible

  // --- Initialize Version Display Logic ---
  String versionString = "NanoELS-DRO V" + String(SOFTWARE_VERSION);
  // FW version name being displayed on screen
  setText("tVersion", versionString);
  toScreen("tVersion.font=2");
  versionTimerStart = millis(); // Start the 5s timer
  toScreen("tPitch.font=16"); // Set default Pitch font

  xTaskCreatePinnedToCore(taskAttachInterrupts, "taskAttachInterrupts",
                          10000 /* stack size */, NULL, 0 /* priority */, NULL,
                          0 /* core */);
  xTaskCreatePinnedToCore(taskDisplay, "taskDisplay", 10000 /* stack size */,
                          NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveZ, "taskMoveZ", 10000 /* stack size */, NULL,
                          0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskMoveX, "taskMoveX", 10000 /* stack size */, NULL,
                          0 /* priority */, NULL, 0 /* core */);
  if (y.active)
    xTaskCreatePinnedToCore(taskMoveY, "taskMoveY", 10000 /* stack size */,
                            NULL, 0 /* priority */, NULL, 0 /* core */);
  xTaskCreatePinnedToCore(taskGcode, "taskGcode", 10000 /* stack size */, NULL,
                          0 /* priority */, NULL, 0 /* core */);
  if (WIFI_ENABLED)
    xTaskCreatePinnedToCore(taskWiFi, "taskWiFI", 10000 /* stack size */, NULL,
                            0 /* priority */, NULL, 0 /* core */);
}

// =================================================================
// Main Loop
// =================================================================
void loop() {
  handlePhysicalButtons();
  // New state machine for the encoder button on Pin 36
  {
    static enum { B_IDLE, B_DEBOUNCE, B_PRESSED, B_HELD } buttonState = B_IDLE;
    static unsigned long pressStartTime = 0;
    const unsigned long longPressTime = 500;
    // long press timing for the encoder press button in ms

    bool isPressedNow = (digitalRead(36) == LOW);
    switch (buttonState) {
    case B_IDLE:
      if (isPressedNow) {
        pressStartTime = millis();
        buttonState = B_DEBOUNCE;
      }
      break;
    case B_DEBOUNCE:
      if (!isPressedNow) { // It was a noise glitch
        buttonState = B_IDLE;
      } else if (millis() - pressStartTime > 50) { // Debounce time passed
        buttonState = B_PRESSED;
      }
      break;
    case B_PRESSED:
      if (!isPressedNow) { // --- SHORT PRESS ACTION ---
        if (currentAppState == STATE_NORMAL) {
          // wsKeycode = B_STEP;
          // OLD ACTION
          zDisplayModeDRO = !zDisplayModeDRO;
          // NEW ACTION: Toggle Z display
        } else { // If in mode change, short press is 'confirm'
          currentAppState = STATE_NORMAL;
        }
        buttonState = B_IDLE;
      } else if (millis() - pressStartTime >
                 longPressTime) { // --- LONG PRESS ACTION ---
        if (currentAppState == STATE_NORMAL) {
          currentAppState = STATE_CHANGE_MODE;
          // Activate mode change
        }
        buttonState = B_HELD;
      }
      break;
    case B_HELD:
      if (!isPressedNow) { // Wait for the button to be released
        buttonState = B_IDLE;
      }
      break;
    }
  }

  handleFinePitchEncoder();
  // B_STOPL STATE MACHINE
  if (buttonStopLPressed) {
    if (stopL_state == STATE_IDLE) {
      stopL_pressStartTime = millis();
      stopL_state = STATE_WAITING;
    } else if (stopL_state == STATE_WAITING) {
      if (millis() - stopL_pressStartTime > LONG_PRESS_DURATION_MS) {
        disableAllStops();
        stopL_state = STATE_ACTION_DONE;
      }
    }
  } else {
    if (stopL_state == STATE_WAITING) {
      buttonLeftStopPress(&z);
    }
    stopL_state = STATE_IDLE;
  }

  // --- NEW: B_STOPR HANDLING (Action on Release) ---
  static bool wasStopRPressed = false;
  if (buttonStopRPressed) {
    wasStopRPressed = true;
  } else if (wasStopRPressed) {
    // Trigger on the falling edge (release) to match B_STOPL robustness
    buttonRightStopPress(&z);
    wasStopRPressed = false;
  }
  // -------------------------------------------------

  // --- B_X STATE MACHINE for short vs. long press ---
  if (buttonXPressed) {
    if (buttonX_state == B_X_IDLE) {
      buttonX_pressStartTime = millis();
      buttonX_state = B_X_WAITING;
    } else if (buttonX_state == B_X_WAITING) {
      if (millis() - buttonX_pressStartTime > LONG_PRESS_DURATION_MS) {
        numpadEntryMode = DIAMETER;
        inNumpad = true;
        physicalNumpadModeActive = true;
        resetNumpad();
        buttonZero_state = B0_STATE_IDLE;
        buttonFive_state = B5_STATE_IDLE;
        beep();
        buttonX_state = B_X_ACTION_DONE;
      }
    }
  } else {
    if (buttonX_state == B_X_WAITING) {
      // --- Handle ABS vs INC logic for B_X ---
      if (isIncrementalMode) {
        xScaleIncrementalOffset = xScale.getPosition();
      } else {
        float oldPos = xScale.getPosition();
        xScale.clearCount();
        xScaleIncrementalOffset -= oldPos;
        markAxis0(&x);
      }
    }
    buttonX_state = B_X_IDLE;
  }

  // --- NEW: B_Z STATE MACHINE for short vs. long press ---
  if (buttonZPressed) {
    if (buttonZ_state == B_Z_IDLE) {
      buttonZ_pressStartTime = millis();
      buttonZ_state = B_Z_WAITING;
    } else if (buttonZ_state == B_Z_WAITING) {
      if (millis() - buttonZ_pressStartTime > LONG_PRESS_DURATION_MS) {
        numpadEntryMode = LENGTH;
        inNumpad = true;
        physicalNumpadModeActive = true;
        resetNumpad();
        buttonZero_state = B0_STATE_IDLE;
        buttonFive_state = B5_STATE_IDLE;
        beep();
        buttonZ_state = B_Z_ACTION_DONE;
      }
    }
  } else {
    if (buttonZ_state == B_Z_WAITING) {
      // --- SHORT PRESS: SMART ZERO Z LOGIC ---
      float currentPosMm = 0.0f;
      if (zDisplayModeDRO) {
        currentPosMm = zScale.getPosition();
      } else {
        currentPosMm = getAxisPosDu(&z) / 10000.0f;
      }

      if (isIncrementalMode) {
        // Inc Mode: Zero Display Only
        zScaleIncrementalOffset = currentPosMm;
      } else {
        // Abs Mode: Zero Hardware + Shift Offset
        if (zDisplayModeDRO) {
          zScale.clearCount();
        } else {
          markAxis0(&z);
        }
        zScaleIncrementalOffset -= currentPosMm;
      }
    }
    buttonZ_state = B_Z_IDLE;
  }

  // --- NEW: B_X_ENA STATE MACHINE ---
  if (buttonXEnaPressed) {
    if (buttonXEna_state == ENA_IDLE) {
      buttonXEna_pressStartTime = millis();
      buttonXEna_state = ENA_WAITING;
    } else if (buttonXEna_state == ENA_WAITING) {
      if (millis() - buttonXEna_pressStartTime > LONG_PRESS_DURATION_MS) {
        // Long Press: Toggle Color Override
        xColorInverted = !xColorInverted;
        beep();
        buttonXEna_state = ENA_ACTION_DONE;
      }
    }
  } else { // On Release
    if (buttonXEna_state == ENA_WAITING) {
      // Short Press: Enable/Disable Axis
      x.disabled = !x.disabled;
      updateEnable(&x);
    }
    buttonXEna_state = ENA_IDLE;
  }
  // ----------------------------------

  // --- NEW: B_Z_ENA STATE MACHINE ---
  if (buttonZEnaPressed) {
    if (buttonZEna_state == ENA_IDLE) {
      buttonZEna_pressStartTime = millis();
      buttonZEna_state = ENA_WAITING;
    } else if (buttonZEna_state == ENA_WAITING) {
      if (millis() - buttonZEna_pressStartTime > LONG_PRESS_DURATION_MS) {
        // Long Press: Toggle Color Override
        zColorInverted = !zColorInverted;
        beep();
        buttonZEna_state = ENA_ACTION_DONE;
      }
    }
  } else { // On Release
    if (buttonZEna_state == ENA_WAITING) {
      // Short Press: Enable/Disable Axis
      z.disabled = !z.disabled;
      updateEnable(&z);
    }
    buttonZEna_state = ENA_IDLE;
  }
  // ----------------------------------

  // --- NEW: B_DISPL STATE MACHINE ---
  if (buttonDisplayPressed) {
    if (buttonDisplay_state == DISP_IDLE) {
      buttonDisplay_pressStartTime = millis();
      buttonDisplay_state = DISP_WAITING;
    } else if (buttonDisplay_state == DISP_WAITING) {
      if (millis() - buttonDisplay_pressStartTime > LONG_PRESS_DURATION_MS) {
        // Long Press: Toggle Color
        spindleValColorGreen = !spindleValColorGreen;
        beep();
        buttonDisplay_state = DISP_ACTION_DONE;
      }
    }
  } else { // On Release
    if (buttonDisplay_state == DISP_WAITING) {
      // Short Press: Cycle Units (RPM/Turns/Angle)
      buttonDisplayPress();
    }
    buttonDisplay_state = DISP_IDLE;
  }
  // ----------------------------------

  // --- NEW: B_PITCH STATE MACHINE ---
  static enum {
    PITCH_IDLE,
    PITCH_WAITING,
    PITCH_ACTION_DONE
  } pitch_state = PITCH_IDLE;
  static unsigned long pitch_pressStartTime = 0;

  if (buttonPitchPressed) {
    if (pitch_state == PITCH_IDLE) {
      pitch_pressStartTime = millis();
      pitch_state = PITCH_WAITING;
    } else if (pitch_state == PITCH_WAITING) {
      if (millis() - pitch_pressStartTime > LONG_PRESS_DURATION_MS) {
        buttonReversePress(); // Long Press: Reverse Pitch
        beep();
        pitch_state = PITCH_ACTION_DONE;
      }
    }
  } else { // On release
    if (pitch_state == PITCH_WAITING) {
      // Short Press: Cycle Font (16 -> 4 -> 15)
      pitchFontIdx = (pitchFontIdx + 1) % 3;
      toScreen("tPitch.font=" + String(pitchFontList[pitchFontIdx]));
      beep();
    }
    pitch_state = PITCH_IDLE;
  }

  const unsigned long NUMPAD_ENTER_LONG_PRESS_MS = 300;
  const unsigned long NUMPAD_OFF_LONG_PRESS_MS = 300;
  const unsigned long NUMPAD_TOGGLE_LONG_PRESS_MS = 300;
  // '0'/Enter STATE MACHINE
  // --- B_0 (ENTER) STATE MACHINE ---
  if (buttonZeroPressed) {
    if (buttonZero_state == B0_STATE_IDLE) {
      buttonZero_pressStartTime = millis();
      buttonZero_state = B0_STATE_WAITING;
    } else if (buttonZero_state == B0_STATE_WAITING) {
      if (millis() - buttonZero_pressStartTime > NUMPAD_ENTER_LONG_PRESS_MS) {
        if (physicalNumpadModeActive)
          processNumpad(B_ON);
        else
          buttonOnOffPress(true);
        buttonZero_state = B0_STATE_ACTION_DONE;
      }
    }
  } else { // on release
    if (buttonZero_state == B0_STATE_WAITING) {
      if (physicalNumpadModeActive)
        processNumpad(B_0);
      else
        buttonOnOffPress(true);
    }
    buttonZero_state = B0_STATE_IDLE;
  }

  // --- NEW: B_1 STATE MACHINE (Touch 1 Long Press = Multistart) ---
  static enum { B1_IDLE, B1_WAITING, B1_ACTION_DONE } b1_state = B1_IDLE;
  static unsigned long b1_pressStartTime = 0;

  if (buttonOnePressed) {
    if (b1_state == B1_IDLE) {
      b1_pressStartTime = millis();
      b1_state = B1_WAITING;
    } else if (b1_state == B1_WAITING) {
      if (millis() - b1_pressStartTime > LONG_PRESS_DURATION_MS) {
        // --- LONG PRESS ACTION ---
        buttonMultistartPress();
        beep();
        b1_state = B1_ACTION_DONE;
      }
    }
  } else { // on release
    if (b1_state == B1_WAITING) {
      // --- SHORT PRESS ACTION ---
      processNumpad(B_1);
    }
    b1_state = B1_IDLE;
  }
  // ----------------------------------------------------------------

  // --- B_5 (STOP) STATE MACHINE ---
  if (buttonFivePressed) {
    if (buttonFive_state == B5_STATE_IDLE) {
      buttonFive_pressStartTime = millis();
      buttonFive_state = B5_STATE_WAITING;
    } else if (buttonFive_state == B5_STATE_WAITING) {
      if (millis() - buttonFive_pressStartTime > NUMPAD_OFF_LONG_PRESS_MS) {
        buttonOnOffPress(false);
        buttonFive_state = B5_STATE_ACTION_DONE;
      }
    }
  } else {
    if (buttonFive_state == B5_STATE_WAITING) {
      if (physicalNumpadModeActive)
        processNumpad(B_5);
      else
        buttonOnOffPress(false);
    }
    buttonFive_state = B5_STATE_IDLE;
  }

  // '2'/Numpad Toggle STATE MACHINE
  if (buttonTwoPressed) {
    if (buttonTwo_state == B2_STATE_IDLE) {
      buttonTwo_pressStartTime = millis();
      buttonTwo_state = B2_STATE_WAITING;
    } else if (buttonTwo_state == B2_STATE_WAITING) {
      if (millis() - buttonTwo_pressStartTime > NUMPAD_TOGGLE_LONG_PRESS_MS) {
        // --- LONG PRESS: SET DIAMETER (Swapped) ---
        numpadEntryMode = DIAMETER;
        inNumpad = true;
        physicalNumpadModeActive = true;
        resetNumpad();
        buttonZero_state = B0_STATE_IDLE;
        buttonFive_state = B5_STATE_IDLE;
        // Color update removed
        beep();
        buttonTwo_state = B2_STATE_ACTION_DONE;
      }
    }
  } else {
    if (buttonTwo_state == B2_STATE_WAITING) {
      if (physicalNumpadModeActive) {
        processNumpad(B_2);
      } else {
        // --- SHORT PRESS: SET PITCH (Swapped) ---
        numpadEntryMode = PITCH;
        inNumpad = true;
        physicalNumpadModeActive = true;
        resetNumpad();
        buttonZero_state = B0_STATE_IDLE;
        buttonFive_state = B5_STATE_IDLE;
        // Color update removed
        beep();
      }
    }
    buttonTwo_state = B2_STATE_IDLE;
  }

  if (emergencyStop != ESTOP_NONE) {
    return;
  }
  if (xSemaphoreTake(motionMutex, 1) != pdTRUE) {
    return;
  }

  applySettings();
  processSpindleCounter();
  discountFullSpindleTurns();

  if (!isOn || dupr == 0 || spindlePosSync != 0) {
  } else if (mode == MODE_NORMAL) {
    modeGearbox();
  } else if (mode == MODE_TURN) {
    modeTurn(&z, &x);
  } else if (mode == MODE_FACE) {
    modeTurn(&x, &z);
  } else if (mode == MODE_CUT) {
    modeCut();
  } else if (mode == MODE_CONE) {
    modeCone();
  } else if (mode == MODE_THREAD) {
    modeThreadPaused(&z, &x);
    // Call the newly named function
  } else if (mode == MODE_ELLIPSE) {
    modeEllipse(&z, &x);
  }

  moveAxis(&z);
  moveAxis(&x);
  if (ACTIVE_Y)
    moveAxis(&y);

  xSemaphoreGive(motionMutex);
  // =================================================================
  // NEW AND CORRECTED +/- BUTTON LOGIC (SINGLE TAP + HOLD DOWN)
  // =================================================================
  static bool wasPlusMinusPressed = false;
  static unsigned long pressStartTime = 0;
  static unsigned long lastRepeatTime = 0;
  const unsigned long initialDelayMs = 300;
  // this is the timer for when long press action starts
  const unsigned long repeatRateMs = 100;
  unsigned long now = millis();

  bool isPlusMinusPressed = buttonPlusPressed || buttonMinusPressed;
  if (isPlusMinusPressed && !wasPlusMinusPressed) {
    if (buttonPlusPressed)
      buttonPlusMinusPress(true);
    else
      buttonPlusMinusPress(false);
    pressStartTime = now;
    lastRepeatTime = now;
  }

  if (isPlusMinusPressed && (now - pressStartTime > initialDelayMs)) {
    if (now - lastRepeatTime > repeatRateMs) {
      if (buttonPlusPressed)
        buttonPlusMinusPress(true);
      else
        buttonPlusMinusPress(false);
      lastRepeatTime = now;
    }
  }

  wasPlusMinusPressed = isPlusMinusPressed;
}