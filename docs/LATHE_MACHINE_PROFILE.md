# NanoELS Lathe Machine Profile Specification

> **Status:** Draft / Authoritative Specification  
> **Target Version:** NanoELS H5 Modular Architecture  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `docs/LATHE_MACHINE_PROFILE.md`  
> **Referenced By:** [`ELS_CORE_STATE_MODEL.md`](./ELS_CORE_STATE_MODEL.md), [`ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md), [`ELS_CLIENT_ARCHITECTURE.md`](./ELS_CLIENT_ARCHITECTURE.md), [`ELS_DESIGN_NOTEBOOK.md`](./ELS_DESIGN_NOTEBOOK.md), `lathe-viz-profiles.js`, `MachineModel.h`

---

## 1. Executive Summary & Purpose

The **Lathe Machine Profile** defines the complete physical, kinematic, and electrical reality of a specific lathe installation.

Because electronic leadscrew (ELS) systems are installed across a wide spectrum of machine tools—ranging from mini-lathes ($7\times14$) to heavy benchtop machines ($10\times22$, $12\times36$) and custom builds—the software stack must never hardcode machine dimensions, motor configurations, or travel limits.

Furthermore, machinists upgrade their lathes in progressive stages:
1. **Stage 1 (Basic ELS):** $Z$-axis leadscrew stepper + spindle encoder (manual $X$ cross-slide).
2. **Stage 2 (DRO Enhanced):** Added physical optical/magnetic linear scales on $Z$ and/or $X$.
3. **Stage 3 (Full 2-Axis CNC/ELS):** Motorized $X$ cross-slide stepper installed.
4. **Stage 4 (Accessory Expansion):** Electronic handwheels (MPG), deflecting joysticks, VFD spindle control, or $Y$-axis auxiliary milling attachments.

This document serves as the **single authoritative specification** defining:
* The canonical coordinate reference frame and geometry conventions.
* The hardware capabilities bitmask and axis configuration structs.
* Spindle pulse, indexing, and VFD interface properties.
* The standard preset library of workshop lathe models.
* Serialization contracts across C++ firmware, WebSocket JSON, and client presentation state.

---

## 2. Coordinate Reference Frame & Physical Geometry Conventions

```
              SPINDLE & CHUCK                              LATHE BED & WAYS
   ┌──────────────────────────────────┐ ┌──────────────────────────────────────────────────┐
   │                                  │ │                                                  │
   │           Headstock              │ │                                                  │
   │        ┌──────────────┐          │ │                                                  │
   │        │              │          │ │                     Centerline                   │
   ├────────┼── Spindle ───┼──────────┼─┼─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─┼─── Tailstock Quill
   │        │   Chuck      │          │ │                                                  │
   │        └──────┬───────┘          │ │                  [Tool Post / Insert]            │
   │               │                  │ │                           ▲                      │
   │               │                  │ │                           │  X-axis (Cross-slide)│
   │               │                  │ │                           ▼                      │
   │               │                  │ │                 ◄─────────────────►              │
   │               │                  │ │                  Z-axis (Carriage)               │
   └───────────────┴──────────────────┘ └──────────────────────────────────────────────────┘
                   ▲
             Chuck Face Origin (Z = 0 reference)
```

### 2.1 The Two Coordinate Domains
1. **Machine Coordinate System (MCS):**
   * Physical hardware coordinates measured in **canonical deci-microns** ($10{,}000\text{ du} = 1.0\text{ mm}$).
   * Origin ($Z=0, X=0$) can be referenced against chuck face or home limit switches.
   * Motion limits (`zLeftDu`, `zRightDu`, `xUpDu`, `xDownDu`) are evaluated strictly in this domain.
2. **Workpiece Coordinate System (WCS):**
   * Machinists think in terms of part dimensions (e.g. cutting a shoulder to $Z = -25.00\text{ mm}$, turning down to diameter $X = 20.00\text{ mm}$).
   * Set by touch-off zeroing (`CMD.axisZero=Z`, `CMD.axisZero=X`).

### 2.2 Directional & Polarity Conventions
To support diverse leadscrew thread directions (right-hand vs. left-hand) and operator preferences, polarity is configurable:
* **$Z$-Axis (Longitudinal Carriage):**
  * Standard: Moving **toward the headstock/chuck** is negative (cutting into part: $Z-$), moving **away toward tailstock** is positive ($Z+$).
  * Configurable screen polarity: `zPositive: 'right' | 'left'` (default: `'right'`).
* **$X$-Axis (Transverse Cross-Slide):**
  * Standard: Moving **toward spindle centerline** (infeeding / decreasing diameter) is negative ($X-$), moving **outward away from centerline** (retracting) is positive ($X+$).
  * Configurable screen polarity: `xPositive: 'out' | 'in'` (default: `'out'`).
* **Diameter vs. Radius Mode:**
  * Cross-slide $X$ physical stepping is always radial ($1\text{ mm}$ tool displacement $= 1\text{ mm}$ radius).
  * When in Diameter Mode (`dia == true`), display readouts and operator setpoints reflect $2\times$ radial displacement.

---

## 3. Data Structures & Type Definitions

### 3.1 Hardware Capability Bitmask (`LatheCapabilityFlags`)

Every lathe advertises its physical attachments through a 32-bit capability mask:

```cpp
enum LatheCapabilityFlags : uint32_t {
    CAP_NONE            = 0,
    
    // Spindle Capabilities
    CAP_SPINDLE_ENCODER = 1 << 0, // Pulse encoder fitted to spindle (shaft or servo pass-through)
    CAP_SPINDLE_INDEX   = 1 << 1, // Single-pulse-per-revolution index sensor (Z-channel / optical)
    CAP_SPINDLE_VFD     = 1 << 2, // Spindle VFD / servo speed & direction control (Modbus / 0-10V)
    
    // Axis Motion Drivers
    CAP_MOTOR_Z         = 1 << 3, // Z carriage leadscrew drive installed
    CAP_MOTOR_X         = 1 << 4, // X cross-slide leadscrew drive installed
    
    // Physical Linear DRO Scales (Glass / Magnetic)
    CAP_DRO_Z           = 1 << 6, // Optical linear scale attached to Z carriage
    CAP_DRO_X           = 1 << 7, // Optical linear scale attached to X cross-slide
    
    // Physical & Sensorless Travel Limits
    CAP_LIMIT_SWITCH_Z  = 1 << 9, // Physical microswitches / inductive limits on Z
    CAP_LIMIT_SWITCH_X  = 1 << 10,// Physical microswitches / inductive limits on X
    CAP_LIMIT_SENSORLESS= 1 << 11,// TMC StallGuard or Servo torque-sensing limits
    
    // Operator Input & Pendants
    CAP_MPG_DEDICATED   = 1 << 12,// Dedicated per-axis MPG handwheels on apron
    CAP_MPG_SHARED      = 1 << 13,// Single shared MPG with axis selector (pendant / console)
    CAP_JOYSTICK        = 1 << 14,// 2-axis or 4-axis analog/contact joystick
    CAP_TAILSTOCK_DRO   = 1 << 15,// Tailstock barrel position scale fitted
    
    // Drive Safety Interlocks
    CAP_DRIVE_ALARM_Z   = 1 << 16,// Closed-loop stepper / servo fault alarm pin on Z
    CAP_DRIVE_ALARM_X   = 1 << 17 // Closed-loop stepper / servo fault alarm pin on X
};
```

---

### 3.2 Reusable Encoder Configuration (`QuadratureEncoderConfig`)

Quadrature encoders are used identically across the spindle, optical linear scales (DRO), and manual pulse generator (MPG) handwheels. A single canonical struct models their properties:

```cpp
struct QuadratureEncoderConfig {
    uint16_t ppr;                  // Native pulses per revolution (or pulses/mm for linear scales)
    bool     hasIndex;             // Single-pulse index signal (Z-channel)
    bool     inverted;             // Direction phase invert (swap A/B)
    uint8_t  filterBacklashPulses; // Noise / reversal filter deadband in counts
};
```

---

### 3.3 Axis Hardware Configuration (`AxisHardwareConfig`)

Each axis on the machine is described by its physical kinematics, actuator drive technology, limits, and direct feedback:

```cpp
enum AxisDriveType : uint8_t {
    DRIVE_NONE_MANUAL      = 0, // Unmotorized axis (manual leadscrew, DRO scale only)
    DRIVE_GENERIC          = 1, // Minimal legacy-compatible mode (steps/mm, dir, enable)
    DRIVE_STEPPER_OPEN     = 2, // Open-loop stepper with timing HAL specs
    DRIVE_STEPPER_CLOSED   = 3, // Closed-loop stepper with fault/stall alarm input
    DRIVE_AC_SERVO         = 4  // Digital AC servo with in-position & brake signals
};

enum AxisFeedbackType : uint8_t {
    FEEDBACK_INFERRED      = 0, // Open loop: position inferred from step counts
    FEEDBACK_OPTICAL_SCALE = 1, // Optical glass linear scale (TTL quadrature)
    FEEDBACK_MAGNETIC_TAPE = 2  // Magnetic tape quadrature scale
};

enum HardLimitMethod : uint8_t {
    LIMIT_NONE             = 0,
    LIMIT_SWITCH_NC        = 1, // Mechanical microswitch (Normally Closed)
    LIMIT_SWITCH_NO        = 2, // Mechanical microswitch (Normally Open)
    LIMIT_INDUCTIVE        = 3, // Inductive proximity sensor (NPN / PNP)
    LIMIT_SENSORLESS_STALL = 4, // Trinamic StallGuard back-EMF stall detection
    LIMIT_SERVO_TORQUE     = 5  // AC Servo torque clamp threshold reached
};

struct AxisHardwareConfig {
    // 1. Kinematic Physics (Universal)
    float    leadscrewPitchMm;      // Physical pitch of screw (e.g. 2.0 mm, 4.0 mm, 10 TPI)
    float    backlashMm;            // Measured screw backlash for backlash compensation
    bool     directionInverted;     // Invert step/dir direction
    
    // Dynamic Limits
    float    maxVelocityMmMin;      // Rapid traverse speed ceiling (e.g. 1500 mm/min)
    float    maxAccelMmSec2;        // Acceleration ramp limit (e.g. 250 mm/sec^2)
    
    // 2. Travel Limits (Soft Virtual & Hard Physical)
    bool     softLimitsEnabled;     // Virtual software boundary calculation
    float    travelMinMm;           // Extreme coordinate limit toward headstock / center
    float    travelMaxMm;           // Extreme coordinate limit toward bed end / slide back
    bool     hardLimitsInstalled;   // Physical limit switches or sensorless probing
    HardLimitMethod hardLimitMethod;// Switch, inductive, StallGuard, servo torque
    int8_t   stallSensitivity;      // Stall threshold if sensorless (-64..63)
    float    homingBackoffMm;       // Retreat distance after touching limit
    
    // 3. Drive & Actuation Technology
    AxisDriveType driveType;        // DRIVE_GENERIC, DRIVE_STEPPER_CLOSED, DRIVE_AC_SERVO, etc.
    float    stepsPerMm;            // Driver steps per millimeter
    bool     enableActiveLow;       // Driver enable signal polarity
    uint16_t stepPulseWidthNs;      // Timing HAL pulse duration (e.g. 500ns for servo, 2500ns for opto)
    
    // Drive Electrical & Signal Interlocks (Optional / Specialized)
    bool     hasAlarmInput;         // Fault / stall alarm pin from driver (trips E-stop on stall)
    bool     alarmActiveLow;        // Fault pin polarity
    bool     hasInPositionInput;    // Servo INP signal for synchronous handshake
    bool     hasBrakeOutput;        // Electromagnetic brake release sequencing
    float    ratedTorqueNm;         // Motor continuous torque (for thrust estimation)
    float    ratedPowerW;           // Motor wattage
    float    busVoltageV;           // Driver DC supply voltage
    
    // 4. Direct Position Feedback (Linear DRO Scale)
    AxisFeedbackType feedbackType;  // FEEDBACK_INFERRED, FEEDBACK_OPTICAL_SCALE, etc.
    QuadratureEncoderConfig scale;  // Reusable encoder definition (pulses/mm, etc.)
};
```

---

### 3.4 Spindle Hardware Configuration (`SpindleHardwareConfig`)

Spindle synchronization is the heartbeat of electronic threading and gearing. The profile decouples **native encoder PPR** from **pulley transmission ratio**, perfectly handling direct spindle encoders, belted encoders, or spindle servo drives with buffered encoder outputs:

```cpp
enum SpindleFeedbackSource : uint8_t {
    SPINDLE_SHAFT          = 0, // Standalone encoder mounted directly on spindle shaft (1:1)
    SERVO_DRIVE_PASSTHROUGH= 1, // Buffered OA/OB quadrature output from spindle servo drive
    TACH_OPTICAL           = 2  // Single-pulse index or optical tachometer
};

enum SpindleDriveType : uint8_t {
    SPINDLE_MANUAL_AC      = 0, // Uncontrolled AC induction / pulley belts
    SPINDLE_VFD_ANALOG     = 1, // 0-10V analog speed + FWD/REV relays
    SPINDLE_VFD_MODBUS     = 2, // RS-485 Modbus RTU digital control
    SPINDLE_AC_SERVO       = 3  // Spindle servo (enables rigid tapping & C-axis indexing)
};

struct SpindleHardwareConfig {
    // 1. Kinematic Limits
    float    maxSafeRpm;            // Headstock mechanical safety ceiling (e.g. 2500 RPM)
    float    minSyncRpm;            // Minimum reliable encoder tracking RPM (typically 15 RPM)
    bool     directionInverted;     // Invert CW/CCW encoder sensing
    
    // 2. Spindle Feedback (Decoupled True PPR + Pulley Ratio)
    SpindleFeedbackSource source;   // SPINDLE_SHAFT or SERVO_DRIVE_PASSTHROUGH
    QuadratureEncoderConfig encoder;// True native encoder PPR (e.g. 1200, 2400)
    float    pulleyRatio;           // Pulley ratio (motor/encoder revs per spindle rev, e.g. 1.0 or 1.5)
    // NOTE: Effective Spindle PPR = encoder.ppr * pulleyRatio
    
    // 3. Spindle Drive & Control
    SpindleDriveType driveType;     // SPINDLE_MANUAL_AC, SPINDLE_VFD_MODBUS, etc.
    float    ratedPowerW;           // Spindle motor wattage (e.g. 750W, 1500W)
    uint8_t  vfdModbusAddress;      // RS-485 Modbus RTU address
    float    vfdMaxHz;              // Maximum inverter frequency (e.g. 50Hz, 60Hz, 400Hz)
};
```

---

### 3.5 Operator Controls & Pendants (`ControlsHardwareConfig`)

Manual jogging and safety interlocks accommodate dedicated handwheels, shared pendant selectors, and emergency stops:

```cpp
enum MpgArchitecture : uint8_t {
    MPG_NONE               = 0,
    MPG_DEDICATED_PER_AXIS = 1, // Independent physical handwheels on Z and X (traditional lathe apron)
    MPG_SHARED_SELECTOR    = 2  // Single handwheel with axis selector switch (pendant / console)
};

enum PendantInterface : uint8_t {
    PENDANT_GPIO           = 0, // Direct microcontroller GPIO pins
    PENDANT_UART_SERIAL    = 1, // Serial bus pendant (e.g. WHB04B)
    PENDANT_WIRELESS_ESPNOW= 2  // Wireless 2.4GHz ESP-NOW handheld
};

struct ControlsHardwareConfig {
    MpgArchitecture  mpgArchitecture;
    PendantInterface pendantInterface;
    QuadratureEncoderConfig mpgEncoder; // Standard 100 PPR handwheel
    bool             hasJoystick;
    bool             monitoredEStop;
};
```

---

### 3.6 Physical Envelope & Complete Profile (`LatheConfiguration`)

```cpp
struct LatheDimensions {
    float bedLengthMm;              // Total bed length from headstock face to end of ways
    float maxSwingDiameterMm;       // Maximum workpiece diameter clear of bed ways
    float swingOverCrossSlideMm;    // Maximum workpiece diameter clear of cross-slide
    float maxZTravelMm;             // Usable carriage longitudinal travel
    float maxXTravelMm;             // Usable cross-slide transverse travel
    
    // Headstock & Chuck Dimensions
    float headstockLengthMm;        // Headstock casting length
    float chuckDiameterMm;          // Outside diameter of chuck body
    float chuckLengthMm;            // Longitudinal length of chuck body
    float spindleBoreDiameterMm;    // Through-hole bore in spindle
    
    // Reference Offsets for Visualizer & Work Setup
    float zZeroFromChuckFaceMm;     // Default clearance offset from chuck face to Z=0
    float xZeroFromCenterlineMm;    // Default offset from spindle centerline
};

struct LatheConfiguration {
    char                   profileId[24];   // Machine slug, e.g. "wm210v_hybrid"
    char                   modelName[48];   // Human readable label, e.g. "WM210V 8x16 (Servo Z + Stepper X)"
    uint32_t               capabilities;    // Bitmask of LatheCapabilityFlags
    
    AxisHardwareConfig     zAxis;           // Carriage (Z)
    AxisHardwareConfig     xAxis;           // Cross-Slide (X)
    
    SpindleHardwareConfig  spindle;         // Spindle encoder & VFD
    ControlsHardwareConfig controls;        // MPG handwheels, pendant, joystick
    LatheDimensions        dimensions;      // Bed, chuck, and swing geometry
};
```

---

## 4. Lathe Profile Catalog Architecture & Storage Hierarchy

To guarantee that firmware upgrades never destroy user calibrations or custom lathe configurations, NanoELS employs a decoupled storage hierarchy combining **compiled-in ROM System Templates** with **User-Dedicated LittleFS Storage**:

### 4.1 The Three-Tier Fallback Hierarchy

The system establishes an unambiguous, fail-safe hierarchy of machine profile authority:

```
┌────────────────────────────────────────────────────────────────────────┐
│ TIER A: ACTIVE PROFILE (RAM Cache + Sparse NVS Overrides)              │
│ • Stored in NVS under key `machine.preset` (Domain 'P' telemetry)       │
│ • Currently loaded in high-speed RAM for Core 1 real-time motion       │
│ • Holds operator leadscrew calibrations and custom hardware tuning     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ (if NVS `machine.preset` unset,
                                    │  deleted, or reset commanded)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│ TIER B: LITTLEFS USER PRESETS & COMMISSIONING DEFAULT                  │
│ • Custom user lathes and modified templates stored on LittleFS         │
│ • Pointed to by `default_profile_id` in `/presets/profiles.json`       │
│ • Reverted to when operator commands `SETTINGS.preset.resetToDefault`  │
│   or `SETTINGS.resetGroup=machine`                                     │
│ • NEVER WIPED OR OVERWRITTEN BY FIRMWARE UPGRADES                      │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ (if LittleFS unformatted, flash
                                    │  corrupted, or catalog missing)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│ TIER C: COMPILED-IN ROM FACTORY TEMPLATES (`getDefaultProfile()`, etc.)│
│ • Hardcoded in firmware Flash ROM (`wm210v_stock`, `mini_7x14`)        │
│ • Always accessible with zero LittleFS dependency                      │
│ • Automatically updated with each firmware `.bin` release              │
│ • Read-only system templates cannot be deleted or overwritten          │
└────────────────────────────────────────────────────────────────────────┘
```

1. **Active Precedence (NVS):** On boot, the controller reads `machine.preset` from NVS. If valid in the LittleFS user catalog or ROM templates, it is loaded into RAM with any sparse NVS overrides.
2. **Default Profile Fallback:** If `machine.preset` is unset, missing, or the operator commands a reset (`SETTINGS.preset.resetToDefault`), the controller cleanly falls back to `default_profile_id` declared in the catalog.
3. **Compiled-In Failsafe (ROM):** If LittleFS is unformatted, blank, or the catalog file is missing, the firmware boots safely to `getDefaultProfile()` and serves all factory templates directly from ROM. User files are preserved independently.

### 4.2 Single File Catalog vs. Modular Flexibility

By structuring the schema as a catalog collection (`"profiles": [ ... ]`), the system gains enormous operational flexibility:
* **One-Click Backup & Community Packs:** Exporting or importing downloads a single `.json` file containing all workshop lathes (or a community pack like "Bench Lathes 7×14 and WM210V").
* **Single Profile Compatibility:** An individual profile file uses the identical schema—it simply has an array of length 1 (`"profiles": [ { ... } ]`).
* **Clean State Separation:** The catalog file is a **declarative preset library**; it never stores the runtime *active* state. Importing a new pack will never unexpectedly switch the operator's active running profile.

### 4.3 Canonical Universal Wire Commands for Presets

All clients interact with machine profiles through standard ASCII line commands:

| Command | Action | Output / Return Payload |
| :--- | :--- | :--- |
| `SETTINGS.preset.list` | Returns available profile slugs, labels, active profile (from NVS), and default (from catalog) | `PRESET.list=wm210v_stock,wm210v_hybrid;active=wm210v_hybrid;default=wm210v_stock\n` |
| `SETTINGS.preset.get=<slug>` | Streams the specific profile JSON payload | Profile JSON document |
| `SETTINGS.preset.apply=<slug>` | Sets active profile in NVS (`machine.preset = slug`), clears prior sparse machine overrides, and activates in RAM | `OK:SETTINGS.preset.apply=wm210v_hybrid\n` |
| `SETTINGS.preset.setDefault=<slug>` | Sets `default_profile_id = slug` in catalog | `OK:SETTINGS.preset.setDefault=wm210v_stock\n` |
| `SETTINGS.preset.resetToDefault` | Clears active NVS overrides and reloads `default_profile_id` | `OK:SETTINGS.preset.resetToDefault\n` |
| `SETTINGS.preset.save=<slug>` | Serializes current active profile and saves into catalog | `OK:SETTINGS.preset.save=custom_shop_1022\n` |
| `SETTINGS.preset.delete=<slug>`| Removes a profile from the catalog | `OK:SETTINGS.preset.delete=old_test_profile\n` |
| `SETTINGS.preset.export` | Streams complete catalog JSON document | Full catalog JSON payload |

> [!IMPORTANT]
> **Preset Application Behavior & Calibration Isolation:**  
> When an operator applies a different preset via `SETTINGS.preset.apply=<slug>`, any sparse machine overrides stored in NVS (`screw_z_du`, `mot_steps_z`, etc.) from the previous machine are automatically cleared. Leadscrew pitch calibrations and microstep counts belong strictly to a physical machine; carrying over custom calibration offsets across different machine presets is dangerous and forbidden.

---

## 5. Wire Formats, Import/Export & Schema Introspection

### 5.1 Canonical JSON Machine Profile Document (Catalog Format)

Lathe profiles are exported and imported as self-contained catalog documents:

```json
{
  "$schema": "/schemas/lathe-profiles.schema.json",
  "version": 1,
  "default_profile_id": "wm210v_stock",
  "profiles": [
    {
      "identity": {
        "profile_id": "wm210v_hybrid",
    "model_name": "WM210V 8x16 (Servo Z + Generic Stepper X)",
    "manufacturer": "Weiss / Generic",
    "revision": 2,
    "capabilities": 219
  },

  "envelope": {
    "bed": {
      "total_length_mm": 600.0,
      "bed_width_mm": 100.0,
      "way_type": "PRISMATIC_AND_FLAT"
    },
    "swing": {
      "over_bed_dia_mm": 210.0,
      "over_cross_slide_dia_mm": 110.0
    },
    "centers": {
      "distance_between_centers_mm": 400.0
    },
    "headstock": {
      "spindle_bore_dia_mm": 38.0,
      "spindle_nose_taper": "MT5",
      "chuck": {
        "type": "3_JAW_SCROLL",
        "diameter_mm": 125.0,
        "body_length_mm": 65.0,
        "jaw_protrusion_max_mm": 25.0
      },
      "datums": {
        "z_zero_from_chuck_face_mm": 10.0,
        "x_zero_from_centerline_mm": 0.0
      }
    }
  },

  "spindle": {
    "kinematics": {
      "max_safe_rpm": 2500.0,
      "min_sync_rpm": 15.0,
      "direction_inverted": false
    },
    "feedback": {
      "source": "SERVO_DRIVE_PASSTHROUGH",
      "encoder": {
        "ppr": 2400,
        "has_index": true,
        "inverted": false,
        "filter_backlash_pulses": 2
      },
      "pulley_ratio": 1.5
    },
    "drive": {
      "type": "VFD_MODBUS",
      "electrical": {
        "rated_power_w": 750.0,
        "max_frequency_hz": 60.0
      },
      "control": {
        "interface": "MODBUS_RS485",
        "slave_address": 1,
        "baud_rate": 9600
      }
    }
  },

  "axes": {
    "z": {
      "label": "Carriage (Longitudinal)",
      "kinematics": {
        "leadscrew_pitch_mm": 2.0,
        "backlash_mm": 0.015,
        "direction_inverted": false,
        "max_velocity_mm_min": 2500.0,
        "max_accel_mm_sec2": 350.0
      },
      "limits": {
        "soft": {
          "enabled": true,
          "min_mm": 0.0,
          "max_mm": 350.0
        },
        "hard": {
          "installed": true,
          "method": "SENSORLESS_STALLGUARD",
          "stall_sensitivity": 12,
          "homing_backoff_mm": 2.0
        }
      },
      "drive": {
        "type": "AC_SERVO",
        "steps_per_mm": 1000.0,
        "step_pulse_width_ns": 500,
        "enable_active_low": true,
        "signals": {
          "has_alarm_input": true,
          "alarm_active_low": true,
          "has_in_position_input": true,
          "has_brake_output": false
        },
        "electrical": {
          "rated_power_w": 400.0,
          "rated_torque_nm": 1.27
        }
      },
      "feedback": {
        "type": "OPTICAL_GLASS_SCALE",
        "scale": {
          "ppr": 200,
          "has_index": false,
          "inverted": false,
          "filter_backlash_pulses": 0
        }
      }
    },

    "x": {
      "label": "Cross-Slide (Transverse)",
      "kinematics": {
        "leadscrew_pitch_mm": 1.5,
        "backlash_mm": 0.025,
        "direction_inverted": false,
        "max_velocity_mm_min": 800.0,
        "max_accel_mm_sec2": 200.0
      },
      "limits": {
        "soft": {
          "enabled": true,
          "min_mm": 0.0,
          "max_mm": 100.0
        },
        "hard": {
          "installed": false,
          "method": "LIMIT_NONE"
        }
      },
      "drive": {
        "type": "GENERIC",
        "steps_per_mm": 400.0,
        "direction_inverted": false,
        "enable_active_low": true
      },
      "feedback": {
        "type": "INFERRED"
      }
    },

    "y": {
      "label": "Auxiliary / Compound",
      "drive": { "type": "DRIVE_NONE_MANUAL" },
      "feedback": { "type": "FEEDBACK_INFERRED" }
    }
  },

  "controls": {
    "mpg": {
      "architecture": "SHARED_SELECTOR",
      "pendant_interface": "UART_SERIAL",
      "encoder": {
        "ppr": 100,
        "has_index": false,
        "inverted": false,
        "filter_backlash_pulses": 0
      },
      "multipliers": [1, 10, 100]
    },
    "joystick": {
      "installed": true,
      "type": "2_AXIS_ANALOG",
      "deadband_percent": 5.0
    },
    "emergency_stop": {
      "monitored_by_mcu": true,
      "active_low": true
    }
  }
}
    }
  ]
}
```

---

### 5.2 Dynamic Schema Introspection (JSON Schema Standard)

To eliminate the need to update client code whenever firmware introduces a new machine property (e.g. adding stepper vs. servo distinction, sensorless stall thresholds, or wireless pendants), the controller publishes a formal **JSON Schema (draft-07 / 2020-12)** document.

* **Schema Location:** Embedded directly into Flash ROM (`PROGMEM`) at compile time via `scripts/embed_controller_assets.py` (authoritative source: `data/schemas/lathe-profile.schema.json`). For live developer testing without re-flashing firmware, if `/schemas/lathe-profile.schema.json` exists on LittleFS, it serves that file instead.
* **Zero MCU Overhead:** The ESP32 streams the schema over HTTP (`GET /api/schema/profile` or `GET /schemas/lathe-profile.schema.json`) or WebSocket (`SETTINGS.schema=lathe-profile`) without needing a runtime C++ schema engine.
* **Client Dynamic Form Generation:** Clients consume the schema to dynamically render settings forms, evaluate validation bounds, and handle conditional field visibility.

#### Schema Excerpt with Reusable Definitions & Polymorphic Drive Types:

```json
{
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "LatheProfileCatalog",
  "type": "object",
  "required": ["version", "default_profile_id", "profiles"],
  "properties": {
    "version": { "type": "integer", "default": 1 },
    "default_profile_id": {
      "type": "string",
      "title": "System Baseline Default Profile ID",
      "description": "Baseline fallback profile if NVS machine.preset is unset or reset"
    },
    "profiles": {
      "type": "array",
      "title": "Lathe Machine Profiles Catalog",
      "minItems": 1,
      "items": { "$ref": "#/definitions/LatheMachineProfile" }
    }
  },
  "definitions": {
    "LatheMachineProfile": {
      "type": "object",
      "required": ["identity", "envelope", "spindle", "axes"],
      "properties": {
        "identity": {
          "type": "object",
          "required": ["profile_id", "model_name"],
          "properties": {
            "profile_id": { "type": "string", "pattern": "^[a-z0-9_]{3,24}$" },
            "model_name": { "type": "string" },
            "capabilities": { "type": "integer" }
          }
        },
        "envelope": { "type": "object" },
        "spindle": { "type": "object" },
        "axes": { "type": "object" },
        "controls": { "type": "object" }
      }
    },
    "QuadratureEncoder": {
      "type": "object",
      "title": "Quadrature Pulse Encoder",
      "required": ["ppr"],
      "properties": {
        "ppr": {
          "type": "integer",
          "title": "Native Pulses Per Revolution / Unit",
          "minimum": 1,
          "maximum": 50000
        },
        "has_index": {
          "type": "boolean",
          "title": "Index Pulse (Z-Channel) Installed",
          "default": false
        },
        "inverted": {
          "type": "boolean",
          "title": "Invert Phase (Swap A/B)",
          "default": false
        },
        "filter_backlash_pulses": {
          "type": "integer",
          "title": "Noise / Reversal Filter Deadband (Counts)",
          "minimum": 0,
          "default": 0
        }
      }
    },

    "AxisLimits": {
      "type": "object",
      "required": ["soft"],
      "properties": {
        "soft": {
          "type": "object",
          "required": ["enabled", "min_mm", "max_mm"],
          "properties": {
            "enabled": { "type": "boolean", "default": true },
            "min_mm": { "type": "number", "title": "Soft Limit Minimum (mm)" },
            "max_mm": { "type": "number", "title": "Soft Limit Maximum (mm)" }
          }
        },
        "hard": {
          "type": "object",
          "properties": {
            "installed": { "type": "boolean", "default": false },
            "method": {
              "type": "string",
              "enum": [
                "LIMIT_NONE",
                "SWITCH_NC",
                "SWITCH_NO",
                "INDUCTIVE",
                "SENSORLESS_STALLGUARD",
                "SERVO_TORQUE_LIMIT"
              ],
              "default": "LIMIT_NONE"
            },
            "stall_sensitivity": {
              "type": "integer",
              "title": "Stall Sensitivity Threshold",
              "minimum": -64,
              "maximum": 63
            },
            "homing_backoff_mm": {
              "type": "number",
              "title": "Homing Backoff Retreat (mm)",
              "default": 2.0
            }
          }
        }
      }
    },

    "AxisDrive": {
      "type": "object",
      "required": ["type", "steps_per_mm"],
      "properties": {
        "type": {
          "type": "string",
          "enum": [
            "DRIVE_NONE_MANUAL",
            "GENERIC",
            "STEPPER_OPEN_LOOP",
            "STEPPER_CLOSED_LOOP",
            "AC_SERVO"
          ],
          "default": "GENERIC",
          "title": "Actuator Technology"
        },
        "steps_per_mm": {
          "type": "number",
          "title": "Steps Per Millimeter",
          "minimum": 1.0,
          "maximum": 50000.0
        },
        "enable_active_low": {
          "type": "boolean",
          "title": "Enable Signal Active Low",
          "default": true
        }
      },
      "allOf": [
        {
          "if": { "properties": { "type": { "const": "STEPPER_CLOSED_LOOP" } } },
          "then": {
            "properties": {
              "signals": {
                "type": "object",
                "required": ["has_alarm_input"],
                "properties": {
                  "has_alarm_input": { "type": "boolean", "title": "Fault / Stall Alarm Pin" },
                  "alarm_active_low": { "type": "boolean", "default": true }
                }
              }
            }
          }
        },
        {
          "if": { "properties": { "type": { "const": "AC_SERVO" } } },
          "then": {
            "properties": {
              "step_pulse_width_ns": { "type": "integer", "default": 500 },
              "signals": {
                "type": "object",
                "properties": {
                  "has_alarm_input": { "type": "boolean", "default": true },
                  "has_in_position_input": { "type": "boolean", "default": false },
                  "has_brake_output": { "type": "boolean", "default": false }
                }
              },
              "electrical": {
                "type": "object",
                "properties": {
                  "rated_power_w": { "type": "number", "title": "Motor Power (W)" },
                  "rated_torque_nm": { "type": "number", "title": "Continuous Torque (Nm)" }
                }
              }
            }
          }
        }
      ]
    }
  }
}
```

* **Client UI Dynamic Form Generation:**
  * Selecting `GENERIC` renders only the essential step calibration inputs.
  * Selecting `AC_SERVO` or `STEPPER_CLOSED_LOOP` dynamically expands advanced safety alarm pin options, pulse widths, and torque ratings.
  * Selecting `SENSORLESS_STALLGUARD` under limits automatically reveals the stall sensitivity slider.
  * All clients remain 100% decoupled from firmware releases.

---

## 6. Relationship with Other Architecture Subsystems

```
┌────────────────────────────────────────────────────────────────────────┐
│                        LATHE MACHINE PROFILE                           │
│                 (Authoritative Hardware Reality)                       │
└───────────────┬───────────────────┬───────────────────┬────────────────┘
                │                   │                   │
                ▼                   ▼                   ▼
┌───────────────────────┐ ┌───────────────────┐ ┌────────────────────────┐
│  ELS MODE SUBSYSTEM   │ │ DIGITAL TWIN VIZ  │ │   DRO & MOTION CORE    │
│ (Mode Inheritance &   │ │ (Scales bed,      │ │ (Translates deci-      │
│  Hardware Pruning)    │ │  chuck, tools, &  │ │  microns to stepper    │
│ • Unlocks Auto Thread │ │  manual prompts)  │ │  steps & soft-limits)  │
│ • Prunes unavailable  │ │ • Scaled SVG canvas│ • Step pulse generator   │
│   modes automatically │ │ • Renders manual  │ • Backlash compensation  │
│ • Preserves manual    │ │   handwheels      │ • Limit switch trips     │
│   options on 2-axis   │ │ • Collision guards│                          │
└───────────────────────┘ └───────────────────┘ └────────────────────────┘
```

1. **Mode Architecture (`ELS_MODE_ARCHITECTURE.md`):**  
   Modes query `LatheConfiguration.capabilities` to determine if their required hardware is satisfied. The registry prunes or selects between `ManualThreadingMode` and `AutoThreadingMode` based on `CAP_MOTOR_X`.
2. **Digital Twin Visualizer (`lathe-viz.js`):**  
   Reads `LatheDimensions` and `axes` to render the SVG lathe bed, chuck, workpiece, and toolpost to exact physical scale.
3. **Motion Core (`Motion.cpp` / `MachineModel.h`):**  
   Uses `AxisHardwareConfig.stepsPerMm` and `backlashMm` to perform zero-drift integer arithmetic conversions.
