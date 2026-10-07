# NanoELS Lathe Machine Profile Specification

> **Status:** Draft / Authoritative Specification  
> **Target Version:** NanoELS H5 Modular Architecture  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/docs/LATHE_MACHINE_PROFILE.md`  
> **Referenced By:** [`ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md), [`ELS_CLIENT_ARCHITECTURE.md`](./ELS_CLIENT_ARCHITECTURE.md), [`ELS_DESIGN_NOTEBOOK.md`](./ELS_DESIGN_NOTEBOOK.md), `lathe-viz-profiles.js`, `MachineModel.h`

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
    CAP_SPINDLE_ENCODER = 1 << 0, // Quadrature pulse encoder fitted to spindle
    CAP_SPINDLE_INDEX   = 1 << 1, // Single-pulse-per-revolution index optical switch
    CAP_SPINDLE_VFD     = 1 << 2, // Spindle VFD speed & direction control (Modbus / 0-10V)
    
    // Axis Motion Drivers
    CAP_MOTOR_Z         = 1 << 3, // Z carriage leadscrew stepper/servo motor installed
    CAP_MOTOR_X         = 1 << 4, // X cross-slide leadscrew stepper/servo motor installed
    CAP_MOTOR_Y         = 1 << 5, // Y milling/spindle auxiliary axis installed
    
    // Physical Linear DRO Scales (Glass / Magnetic)
    CAP_DRO_Z           = 1 << 6, // Optical linear scale attached to Z carriage
    CAP_DRO_X           = 1 << 7, // Optical linear scale attached to X cross-slide
    CAP_DRO_Y           = 1 << 8, // Optical linear scale attached to Y axis
    
    // Physical Hardware Endstops / Limit Switches
    CAP_LIMIT_SWITCH_Z  = 1 << 9, // Opto/micro-switch hardware limits on Z
    CAP_LIMIT_SWITCH_X  = 1 << 10,// Opto/micro-switch hardware limits on X
    
    // Operator Input Hardware
    CAP_MPG_Z           = 1 << 11,// Manual Pulse Generator handwheel on Z
    CAP_MPG_X           = 1 << 12,// Manual Pulse Generator handwheel on X
    CAP_JOYSTICK        = 1 << 13,// 2-axis or 4-axis analog/contact joystick
    CAP_TAILSTOCK       = 1 << 14 // Tailstock barrel position sensor / DRO fitted
};
```

---

### 3.2 Axis Hardware Configuration (`AxisHardwareConfig`)

Each axis on the machine is described by its physical kinematics and limitations:

```cpp
struct AxisHardwareConfig {
    bool     hasMotor;              // True if axis is driven by stepper/servo
    bool     hasDroScale;           // True if physical glass/magnetic scale is installed
    bool     hasMpg;                // True if dedicated MPG handwheel is fitted
    
    // Kinematic Step Math
    float    stepsPerMm;            // Motor driver pulses per millimeter of linear travel
    float    leadscrewPitchMm;      // Physical pitch of screw (e.g. 2.0 mm, 4.0 mm, 10 TPI)
    float    backlashMm;            // Measured screw backlash for backlash compensation
    
    // Dynamic Limits
    float    maxVelocityMmMin;      // Rapid traverse speed ceiling (e.g. 1500 mm/min)
    float    maxAccelMmSec2;        // Acceleration ramp limit (e.g. 250 mm/sec^2)
    
    // Physical Travel Boundaries (relative to machine datum)
    float    travelMinMm;           // Extreme physical limit toward headstock / center
    float    travelMaxMm;           // Extreme physical limit toward bed end / slide back
};
```

---

### 3.3 Spindle Hardware Configuration (`SpindleHardwareConfig`)

Spindle synchronization is the heartbeat of all electronic threading and gearing:

```cpp
struct SpindleHardwareConfig {
    uint16_t encoderPpr;            // Encoder pulses per revolution (e.g. 1024, 2400 PPR)
    bool     hasIndexPulse;         // True if single-pulse index sensor installed
    float    maxSafeRpm;            // Headstock mechanical safety ceiling (e.g. 2500 RPM)
    float    minSyncRpm;            // Minimum reliable encoder tracking RPM (typically 15 RPM)
    
    // VFD Spindle Control (Optional)
    bool     hasVfdControl;         // True if controller commands spindle start/stop/speed
    uint8_t  vfdModbusAddress;      // RS-485 Modbus RTU address
    float    vfdMaxHz;              // Maximum inverter frequency (e.g. 60 Hz or 400 Hz)
};
```

---

### 3.4 Physical Envelope & Geometry (`LatheDimensions`)

These dimensions describe the machine's physical envelope, used directly by the **Digital Twin Visualizer**, clearance collision guards, and multi-pass bounding:

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
```

---

### 3.5 The Complete Lathe Profile (`LatheConfiguration`)

Combining the components into a single cohesive profile:

```cpp
struct LatheConfiguration {
    char                   profileId[24];   // Machine slug, e.g. "pm1022_full"
    char                   modelName[48];   // Human readable label, e.g. "Precision Matthews 1022 (2-Axis)"
    uint32_t               capabilities;    // Bitmask of LatheCapabilityFlags
    
    AxisHardwareConfig     zAxis;           // Carriage (Z)
    AxisHardwareConfig     xAxis;           // Cross-Slide (X)
    AxisHardwareConfig     yAxis;           // Auxiliary axis (Y)
    
    SpindleHardwareConfig  spindle;         // Spindle encoder & VFD
    LatheDimensions        dimensions;      // Bed, chuck, and swing geometry
};
```

---

## 4. Standard Lathe Preset Library

The system provides pre-configured profiles for common workshop lathes and retrofit configurations:

### Preset 1: 7x14 Mini Lathe (Basic ELS — Z Motor Only)
* **Target Machines:** Sieg SC2, Grizzly G8688, Real Bull 7x14.
* **Capabilities:** `CAP_SPINDLE_ENCODER | CAP_MOTOR_Z | CAP_DRO_X` (Manual X handwheel with scale).
* **Specifications:**
  * Bed Length: $350\text{ mm}$
  * Swing over bed: $180\text{ mm}$ ($7"$)
  * Chuck Diameter: $80\text{ mm}$ ($3"$)
  * Leadscrew Pitch: $1.5\text{ mm}$ (Metric) or $16\text{ TPI}$ (Imperial)
  * Spindle Encoder: $1024\text{ PPR}$
  * $Z$ Travel: $300\text{ mm}$, $X$ Travel: $65\text{ mm}$

### Preset 2: 10x22 Benchtop Lathe (Dual-Axis Motorized)
* **Target Machines:** Precision Matthews PM-1022V, Grizzly G0602, Weiss WM210V.
* **Capabilities:** `CAP_SPINDLE_ENCODER | CAP_SPINDLE_INDEX | CAP_MOTOR_Z | CAP_MOTOR_X | CAP_DRO_Z | CAP_DRO_X`.
* **Specifications:**
  * Bed Length: $600\text{ mm}$
  * Swing over bed: $250\text{ mm}$ ($10"$)
  * Chuck Diameter: $125\text{ mm}$ ($5"$)
  * Leadscrew Pitch: $2.0\text{ mm}$ or $8\text{ TPI}$
  * Spindle Encoder: $2400\text{ PPR}$
  * $Z$ Travel: $550\text{ mm}$, $X$ Travel: $115\text{ mm}$

### Preset 3: Large Toolroom Lathe (Heavy Duty)
* **Target Machines:** 12x36 / 14x40 Toolroom Lathes (South Bend, Colchester, Precision Matthews 1440).
* **Capabilities:** Full dual-axis motors, dual MPGs, optical linear glass scales, VFD spindle control.
* **Specifications:**
  * Bed Length: $1200\text{ mm}$
  * Swing over bed: $360\text{ mm}$ ($14"$)
  * Chuck Diameter: $200\text{ mm}$ ($8"$)
  * Spindle Bore: $40\text{ mm}$
  * $Z$ Travel: $900\text{ mm}$, $X$ Travel: $180\text{ mm}$

---

## 5. Wire Formats & Serialization

### 5.1 JSON Telemetry & Negotiation Schema (`GET /api/lathe-profile` or WebSocket)

```json
{
  "profile_id": "pm1022_full",
  "model_name": "Precision Matthews 1022 (Dual Motor)",
  "capabilities": [
    "SPINDLE_ENCODER",
    "SPINDLE_INDEX",
    "MOTOR_Z",
    "MOTOR_X",
    "DRO_Z",
    "DRO_X",
    "MPG_Z",
    "MPG_X"
  ],
  "axes": {
    "z": {
      "has_motor": true,
      "has_dro": true,
      "steps_per_mm": 400.0,
      "max_velocity_mm_min": 1800.0,
      "travel_min_mm": -550.0,
      "travel_max_mm": 0.0
    },
    "x": {
      "has_motor": true,
      "has_dro": true,
      "steps_per_mm": 800.0,
      "max_velocity_mm_min": 800.0,
      "travel_min_mm": -115.0,
      "travel_max_mm": 10.0
    }
  },
  "spindle": {
    "encoder_ppr": 2400,
    "has_index": true,
    "max_safe_rpm": 2500
  },
  "dimensions": {
    "bed_length_mm": 600.0,
    "max_swing_dia_mm": 250.0,
    "chuck_dia_mm": 125.0,
    "chuck_len_mm": 65.0,
    "max_z_travel_mm": 550.0,
    "max_x_travel_mm": 115.0
  }
}
```

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
