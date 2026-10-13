# NanoELS Properties & Settings Architecture Specification

> **Status:** Authoritative Specification  
> **Target Version:** NanoELS H5 Modular Architecture  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `docs/ELS_PROPERTIES_AND_SETTINGS.md`  
> **Referenced By:** [`ELS_DESIGN_NOTEBOOK.md`](./ELS_DESIGN_NOTEBOOK.md), [`ELS_CORE_STATE_MODEL.md`](./ELS_CORE_STATE_MODEL.md), [`LATHE_MACHINE_PROFILE.md`](./LATHE_MACHINE_PROFILE.md), [`ELS_CLIENT_ARCHITECTURE.md`](./ELS_CLIENT_ARCHITECTURE.md), [`ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md)

---

## 1. Executive Summary & Architectural Role

The **NanoELS Properties & Settings System** (`SettingsService`) is the centralized configuration engine, parameter registry, and persistence manager for the entire NanoELS motion platform.

### 1.1 The Core Problem
In the original legacy `h5.ino` implementation and its initial modular extraction (`Config.cpp`), system settings suffered from three major architectural flaws:
1. **Conflated Responsibilities:** Machine physical dimensions (leadscrew pitches), host controller electronics (WiFi passwords), and operator display choices (diameter vs. radius mode) were dumped into flat, unstructured global variables.
2. **NVS "Default Pollution":** Every factory default was written into non-volatile flash on first boot. If a firmware update improved default motor timings or encoder noise thresholds, existing machines never saw the update because stale values were frozen in flash.
3. **No Distinction Between Machine Iron, Controller Host, and Display Clients:** A shop lathe's physical leadscrew pitch was stored alongside Nextion LCD button debounce repeat timers.

### 1.2 Governing Tenets of the Modern Architecture
1. **Clean 4-Tier Separation:** Physical machine iron, controller electronics, workshop operator preferences, and client display ergonomics are strictly isolated into independent tiers.
2. **The "Dynamic Default vs. Sparse Override" Paradigm:** NVS flash strictly stores user customizations. If a setting matches the active machine preset or hardware default, it consumes zero flash space. Updating a preset automatically updates all un-overridden properties.
3. **High-Speed In-Memory Cache (Zero ISR Contention):** Real-time stepper timers (Core 1) read physical machine parameters from an in-memory RAM cache in nanoseconds without acquiring FreeRTOS mutexes or accessing SPI flash.
4. **Universal Default Inheritance & Reset Authority:** Every property knows its factory/preset default. Operators can query defaults and instantly reset an individual key or an entire group back to baseline.
5. **Controller-Backed Broker for Stateless Clients:** The controller provides pass-through persistent storage for connected clients (e.g., bare-metal hardware pendants or ephemeral tablet browsers) without polluting motion kinematics.
6. **Volatile Session Kinematics & Clean Boot Baseline:** Live axis coordinates, spindle phase tracking, and soft travel limits are strictly session-volatile in RAM. On boot, physical coordinates start clean at zero and limits start unbound to prevent collision hazards caused by manual carriage or handwheel movement while the controller is unpowered.
7. **Smart Observable Properties (`PersistentSetting<T>`) & Zero-Overhead Dirty-Checking:** All persistent settings use an in-memory observable wrapper that dirty-checks before touching SPI flash (`if (value == newVal) return;`), completely eliminating redundant NVS flash wear and dispatching an `onChanged` event hook for telemetry and logging.

---

## 2. The 4-Tier Properties Taxonomy

Properties across the NanoELS ecosystem are partitioned into four distinct tiers of authority and ownership:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ TIER 1: PHYSICAL MACHINE PROFILE (`machine.*`)                                         │
│ Intrinsic to the machine tool iron (stays with the lathe if controller is replaced).   │
│ NVS Namespace: "machine" | Authority: Machine Profile & Lathe Configuration            │
├───────────────────────┬────────────────────────────────────────────────────────────────┤
│ Machine Identity      │ `preset` (slug), `profileId`, `modelName`                      │
│ Physical Geometry     │ `bedLengthMm`, `maxSwingDiameterMm`, `swingOverCrossSlideMm`,  │
│                       │ `chuckDiameterMm`, `chuckLengthMm`, `spindleBoreDiameterMm`,   │
│                       │ `zZeroFromChuckFaceMm`, `xZeroFromCenterlineMm`                │
│ Spindle Mechanics     │ `encoderPpr`, `pulleyRatio`, `source`, `encoderBacklash`,      │
│                       │ `maxSafeRpm`, `minSyncRpm`, `hasIndexPulse`, `vfdControl`      │
│ Z Carriage Mechanics  │ `driveType`, `zScrewDu` (pitch), `zMotorSteps`, `zPulsePpr`,   │
│                       │ `zTravelMinMm`, `zTravelMaxMm`, `zBacklashDu`, `zInvertDir`,   │
│                       │ `zAlarmPin`, `zInPositionPin`, `zBrakePin`, `zTorqueNm`        │
│ X Slide Mechanics     │ `driveType`, `xScrewDu` (pitch), `xMotorSteps`, `xPulsePpr`,   │
│                       │ `xTravelMinMm`, `xTravelMaxMm`, `xBacklashDu`, `xInvertDir`,   │
│                       │ `xAlarmPin`, `xInPositionPin`, `xBrakePin`, `xTorqueNm`        │
│ Limits & Probing      │ `softLimitsEnabled`, `hardLimitsInstalled`, `hardLimitMethod`  │
│                       │ (switch, inductive, StallGuard, servo torque), `stallThreshold`│
│ Operator Controls     │ `mpgArchitecture` (dedicated vs shared), `pendantInterface`,   │
│                       │ `joystick.enabled`, `eStopMonitored`                           │
│ Capabilities Bitmask  │ `capabilities` (32-bit LatheCapabilityFlags)                   │
└───────────────────────┴────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────────────────┐
│ TIER 2: CONTROLLER INFRASTRUCTURE & HARDWARE (`sys.*`)                                 │
│ Specific to the ESP32 compute host, network interfaces, and connected peripherals.   │
│ NVS Namespace: "sys" | Authority: Controller Firmware                                  │
├───────────────────────┬────────────────────────────────────────────────────────────────┤
│ Network & WiFi        │ `wifi.enabled`, `wifi.ssid`, `wifi.password`, `wifi.hostname`, │
│                       │ `wifi.apMode`, `wifi.channel`                                  │
│ Optical Linear Scales │ `zScalePulsesPerMm`, `xScalePulsesPerMm`, `scaleSampleFilter`  │
│ Motion Timing HAL     │ `stepPulseWidthUs`, `dirSetupTimeUs`, `jogHoldTimeoutMs`       │
│ Analog Joystick ADC   │ `joystick.enabled`, `joystick.centerSamples`, `oversamples`,   │
│                       │ `sampleIntervalMs`, `adcMax`, `deadband`, `normalRps`,         │
│                       │ `rapidRps`, `invertAxes`, `buttonMode`                         │
│ Manual Pulse Gen(MPG) │ `mpg.zPulsesPerRev`, `mpg.xPulsesPerRev`, `mpg.pulseWaitMs`    │
│ Physical Key Mappings │ `keyboard.bindingTable` (USB Host / Nextion action code maps)  │
│ Software Version Stamp│ `swVersion` (Current firmware migration checkpoint)            │
└───────────────────────┴────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────────────────┐
│ TIER 3: OPERATOR & WORKSHOP PREFERENCES (`prefs.*`)                                    │
│ Shared workshop behavioral defaults. Stored per controller.                            │
│ NVS Namespace: "prefs" | Authority: Workshop Machinist / Operator                      │
├───────────────────────┬────────────────────────────────────────────────────────────────┤
│ Default System Units  │ `startupMeasure` (0 = Metric mm, 1 = Imperial in, 2 = Pitch)   │
│ Coordinate Display    │ `diaRad` (Diameter Ø vs Radius R), `absInc` (Abs vs Inc)       │
│ Readout Source        │ `zDroActive` (Scale vs motor steps), `xDroActive`              │
│ Step Increment Tables │ `jogStepDu` (D-pad step: 10µm, 100µm, 1mm), `feedStepDu`       │
│ Telemetry Deadbands   │ `deadbandAngle` (with `isAuto`), `deadbandTurns` (with `isAuto`)│
└───────────────────────┴────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────────────────────────────┐
│ TIER 4: CLIENT-SCOPED PRESENTATION (`client.<clientId>.*`)                             │
│ Ephemeral display choices. Decoupled from motion. Stored on client or broker NVS.      │
│ NVS Namespace: "clients" | Authority: Connected Terminal / Display UI                  │
├───────────────────────┬────────────────────────────────────────────────────────────────┤
│ Client Session Info   │ `clientType` (web, nextion, pendant), `clientId`, `version`    │
│ Ergonomics & Audio    │ `speechFeedback`, `announceStatus`, `speechRate`, `theme`      │
└───────────────────────┴────────────────────────────────────────────────────────────────┘
```

### 2.1 Strongly-Typed C++ Struct Hierarchy & In-Struct Defaults

To eliminate dozens of loose `extern` globals and sprawling `DEFAULT_*` constants, configuration across all three persistent tiers is organized into cohesive C++ structs with **in-struct member initializers**. Default values are defined directly on struct fields, enabling instant, type-safe resets (e.g. `sysConfig.wifi = WifiConfig{};`).

#### Unified & Extensible Encoder Hardware Abstraction
A core insight of the NanoELS architecture is that **Spindle Encoders, DRO Linear Scales, and MPG Handwheels share the exact same physical and mathematical hardware abstraction**: they are all quadrature pulse sensors with Channel A, Channel B, pulse resolutions, and direction polarity. 

Rather than duplicating disparate encoder structs, all pulse sensors reuse the authoritative `QuadratureEncoderConfig` (defined in `src/motion/LatheProfile.h`):

```cpp
struct QuadratureEncoderConfig {
  uint16_t ppr;         // Pulses per revolution (or pulses/mm for linear scales)
  int8_t   aPin;        // GPIO pin for Channel A (-1 if unassigned)
  int8_t   bPin;        // GPIO pin for Channel B (-1 if unassigned)
  bool     invertDir;   // Swap A/B phase polarity
  bool     hasIndex;    // Single-pulse index signal (Z-channel / zero mark)
  int8_t   indexPin;    // GPIO pin for Index channel (-1 if unassigned)
};
```
*Extensibility for Future Sensor Types:* Because encoder mechanics are decoupled from motion consumers, future encoder hardware (e.g. contactless magnetic angle sensors like AS5048 / MT6701 via SPI/I2C, or serial absolute encoders via CAN/Modbus) can be integrated by adding an encoder type driver without altering electronic leadscrew gearing, threading synchronization, or DRO display logic.

```cpp
// -----------------------------------------------------------------------------
// Tier 2: Controller & Peripheral Structs
// -----------------------------------------------------------------------------

struct WifiConfig {
  bool enabled = true;
  String ssid = "";
  String password = "";
  String hostname = "nanoels";
  String setupApSsid = "NanoEls-Setup";
  String setupApPassword = "nanoels-setup";
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
};

struct GCodeConfig {
  float linearInterpolationPrecision = 0.1f;
  long waitEpsilonSteps = 10;
  bool spindlePausesGcode = true;
  int minRpm = 30;
  long defaultFeedDuPerSec = 20000;
  float minFeedDuPerSec = 167.0f;
};

struct KeyboardConfig {
  bool showPresses = false;
  KeyboardBinding bindings[43];
};

struct ControllerConfig {
  int swVersion = 0;              // Monotonic schema migration checkpoint
  WifiConfig wifi;
  JoystickConfig joystick;        // Encapsulated analog/contact joystick HAL (Input.h)
  MpgConfig mpg;
  MotionTimingConfig timing;      // Hardware pulse timing & delay parameters
  GCodeConfig gcode;              // G-Code execution limits & thresholds
  KeyboardConfig keyboard;
};

extern ControllerConfig sysConfig;

// -----------------------------------------------------------------------------
// Tier 3: Workshop Operator Preferences Struct
// -----------------------------------------------------------------------------

struct OperatorPreferences {
  int measure = 0;                // 0 = Metric, 1 = Imperial, 2 = Pitch
  bool isDiameterMode = true;     // Diameter Ø (2x cross-slide) vs Radius R (1x)
  bool isAbsoluteMode = true;     // Absolute (G90) vs Incremental (G91)
  bool zDroActive = false;        // Optical linear scale vs motor step tracking
  bool xDroActive = false;
  long jogStepDu = 10000;         // 1.0 mm (metric) or 0.1" (imperial)
  long feedStepDu = 100;          // 0.01 mm/rev or 0.001"/rev
  float deadbandAngle = 0.0f;
  bool isDeadbandAngleAuto = true;
  float deadbandTurns = 0.0f;
  bool isDeadbandTurnsAuto = true;
};

extern OperatorPreferences operatorPrefs;
```

### 2.2 API-to-Flash NVS Key Mapping (15-Character Constraint)

ESP32 non-volatile storage (NVS) enforces a strict hardware limit: **Key names must be ≤ 15 characters**.
NanoELS cleanly bridges hierarchical API dot-paths with compact NVS keys by using the top-level domain as the NVS namespace:

| API Dot-Path | Flash Namespace | NVS Key (≤15 chars) | Type | Default Value | Description |
| :--- | :---: | :---: | :---: | :--- | :--- |
| **`sys.wifi.enabled`** | `"sys"` | `"wifi_en"` | bool | `true` | Network WiFi radio state |
| **`sys.wifi.ssid`** | `"sys"` | `"wifi_ssid"` | string | `""` | Station WiFi SSID |
| **`sys.wifi.password`** | `"sys"` | `"wifi_pwd"` | string | `""` | Station WPA2 password |
| **`sys.wifi.hostname`** | `"sys"` | `"wifi_host"` | string | `"nanoels"` | mDNS discovery hostname |
| **`sys.mpg.zPulsesPerRev`** | `"sys"` | `"mpg_z_ppr"` | float | `600.0` | Z carriage handwheel PPR |
| **`sys.mpg.xPulsesPerRev`** | `"sys"` | `"mpg_x_ppr"` | float | `600.0` | X cross-slide handwheel PPR |
| **`sys.timing.dirSetupUs`** | `"sys"` | `"dir_setup_us"` | long | `10` | DIR pin hold before STEP |
| **`sys.timing.stepEnableMs`** | `"sys"` | `"step_en_ms"` | long | `100` | Driver enable settling delay |
| **`sys.timing.jogHoldMs`** | `"sys"` | `"jog_hold_ms"` | long | `1000` | Delay before continuous jog |
| **`sys.gcode.minRpm`** | `"sys"` | `"gc_min_rpm"` | int | `30` | Minimum spindle RPM for feed |
| **`sys.keys.showPresses`** | `"sys"` | `"show_keys"` | bool | `false` | Log scancodes to debug UART |
| **`sys.swVersion`** | `"sys"` | `"sw_ver"` | int | `0` | Firmware schema version |
| **`prefs.startupMeasure`** | `"prefs"` | `"measure"` | int | `0` | Default units (0=mm, 1=in) |
| **`prefs.diaRad`** | `"prefs"` | `"dia_rad"` | bool | `true` | X DRO mode (1=Dia, 0=Rad) |
| **`prefs.absInc`** | `"prefs"` | `"abs_inc"` | bool | `true` | Pos mode (1=Abs, 0=Inc) |
| **`prefs.zDroActive`** | `"prefs"` | `"zdro_active"` | bool | `false` | Read physical scale on Z |
| **`prefs.xDroActive`** | `"prefs"` | `"xdro_active"` | bool | `false` | Read physical scale on X |
| **`prefs.jogStepDu`** | `"prefs"` | `"jog_step"` | long | `10000` | Jog step increment in du |
| **`prefs.feedStepDu`** | `"prefs"` | `"feed_step"` | long | `100` | Feed increment in du |


---

## 3. The "Dynamic Default vs. Sparse User Override" Paradigm

In traditional embedded systems, default configurations are dumped into EEPROM/NVS flash upon initialization. This causes severe technical debt: if the firmware author adjusts default acceleration curves, machines already flashed in the field remain permanently frozen on obsolete values.

### 3.1 Architecture Overview

```
                      ┌─────────────────────────────────────────┐
                      │ Active Machine Preset (e.g. WM210V)     │
                      │ • screwZ = 20000 du                     │
                      │ • chuckDia = 125 mm                     │
                      │ • encoderPpr = 1200                     │
                      └────────────────────┬────────────────────┘
                                           │
                        ┌──────────────────┴──────────────────┐
                        │ (if no user override in NVS)        │
                        ▼                                     ▼
           ┌─────────────────────────┐           ┌─────────────────────────┐
           │ Dynamic Hardware        │           │ Active Preset Defaults  │
           │ Auto-Derivation         │           │ (Machine Profile)       │
           │ (e.g. Angle Deadband =  │           │                         │
           │  360° / ENCODER_TICKS)  │           │                         │
           └────────────┬────────────┘           └────────────┬────────────┘
                        │                                     │
                        └──────────────────┬──────────────────┘
                                           │ fallback default
                                           ▼
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ SettingsService In-Memory Cache (RAM)                                                  │
│                                                                                        │
│ Value Resolution:                                                                      │
│   effectiveValue = userOverride.has_value() ? userOverride.value() : presetDefault;    │
│                                                                                        │
│ Override Tracking:                                                                     │
│   isOverridden = userOverride.has_value();                                             │
└──────────────────────────────────────────▲─────────────────────────────────────────────┘
                                           │ (only when customized)
                      ┌────────────────────┴────────────────────┐
                      │ Sparse NVS Flash Storage ("machine")    │
                      │ • Only stores explicitly modified keys  │
                      │ • Empty NVS = 100% Stock Preset Baseline│
                      └─────────────────────────────────────────┘
```

### 3.2 In-Memory Caching (`std::optional`)
To eliminate SPI flash reads during active machining, all overrides are cached in fast RAM:

```cpp
struct CachedProperty {
    std::optional<long>   overrideLong;
    std::optional<float>  overrideFloat;
    std::optional<bool>   overrideBool;
    std::optional<String> overrideString;
};
```

1. **Unmodified System:** When a user unpacks or initializes NanoELS and selects `wm210v_dual`, NVS flash contains **zero axis override keys**. The RAM cache holds `std::nullopt`.
2. **User Calibration:** When the user measures minor leadscrew error and adjusts $Z$ pitch from $20{,}000\text{ du}$ ($2.0\text{ mm}$) to $20{,}025\text{ du}$, `SettingsService` writes only `"z_screw"` to NVS and updates `overrideLong = 20025`.
3. **Inheritance & Reset:** If the user sends `SETTINGS.reset=screwZDu` or `SETTINGS.set=screwZDu,default`, `SettingsService` removes `"z_screw"` from NVS and resets the RAM cache to `std::nullopt`. The effective value instantly snaps back to the preset's baseline.

### 3.3 Preset Architecture: ROM System Templates & User-Dedicated LittleFS

To ensure that firmware upgrades never erase user data or require flashing raw filesystem images:
1. **Compiled-In System Templates (ROM):** Factory machine templates (`wm210v_stock`, `mini_7x14`) are compiled directly into firmware Flash ROM. Even if LittleFS is completely blank or unformatted, the controller boots reliably into a working lathe profile and offers the full suite of factory templates.
2. **Dedicated User Filesystem (`/presets/profiles.json`):** LittleFS is reserved strictly for operator-created presets, tool libraries, and user part programs (lathecode / G-code).
   - **System Templates (`"system": true`):** Sourced from ROM or marked read-only. Cannot be overwritten or deleted.
   - **User Presets (`"system": false`):** Created by the workshop operator. Can be edited in-place, saved, or deleted.
3. **Deletion & Default Safety Guards:**
   - **System Templates Protected:** `SETTINGS.preset.delete` unconditionally rejects removing templates tagged `"system": true`.
   - **Active Preset Protected:** The preset currently loaded and in use (`activeMachineProfile.profileId`) cannot be deleted while active.
   - **Catalog Default Protected:** The preset designated as the catalog default (`default_profile_id`) cannot be deleted.
4. **Zero LittleFS Overwrite on Update:** Upgrading firmware via USB or Web OTA writes only to the application partition (`firmware.bin`). User presets on LittleFS are never wiped.
5. **Core Ownership:** The core controller scans, reads, serializes, and applies these JSON presets. All connected clients (Web UI, native LVGL display, serial pendants) interact with presets identically via canonical `SETTINGS.preset.*` wire commands.

### 3.4 Dynamic Schema Introspection (JSON Schema Standard)

Firmware evolution frequently adds new machine properties (e.g. distinguishing stepper vs. servo, adding electronic spindle brakes, or optical scale configurations). If client UIs hardcode settings structures, every firmware update requires updating all client apps.

To provide clean, zero-coupling introspection:
1. **Standard JSON Schema (draft-07 / 2020-12):** The controller embeds an authoritative schema document (`/schemas/lathe-profile.schema.json`) into Flash ROM (`PROGMEM`) at build time via `scripts/embed_controller_assets.py` (with LittleFS file override fallback for live development).
2. **Zero MCU Computation:** The ESP32 simply serves or streams the static JSON schema file over HTTP (`GET /api/schema/profile` or `GET /schemas/lathe-profile.schema.json`) or WebSocket (`SETTINGS.schema=lathe-profile`).
3. **Dynamic Client UI & Validation:** Clients parse the JSON Schema to dynamically generate input fields, enforce validation ranges (`minimum`, `maximum`), and handle conditional visibility (`if/then` blocks, such as requiring `motor_type` and `steps_per_mm` only when `has_motor: true`).

### 3.5 Smart Observable Properties (`PersistentSetting<T>`) & Redundant Write Suppression

To eliminate repetitive manual `Preferences` operations, flash wear, and accidental redundant writes, all persistent settings across the firmware—including Controller Infrastructure (WiFi, keyboard bindings in Tier 2), Operator Preferences (Tier 3), and UI Presentation (Tier 4)—MUST use the `PersistentSetting<T>` C++ observable property template (`src/core/PersistentSetting.h`). Ad-hoc raw `Preferences.put*()` calls are strictly forbidden.

#### Architectural Tenets of `PersistentSetting<T>`:
1. **Universal Usage Across Subsystems:** Any setting that persists across reboots (whether in namespace `"prefs"`, `"wifi"`, `"keyboard"`, or sparse `"machine"`) must be encapsulated in a `PersistentSetting<T>` instance. Supported types include `bool`, `int`, `long`, `float`, `uint8_t`, and Arduino `String`.
2. **Single Boot Read, Zero Flash Reads Thereafter:** On initialization (`init()`), `PersistentSetting` reads from NVS once into an internal in-memory cache. All subsequent queries (`get()` or implicit type conversion) read the RAM cache in a single CPU instruction without acquiring SPI bus mutexes.
3. **Deterministic Dirty-Checking Before Flash Writes:** Every assignment (`operator=` or `set()`) evaluates `if (_value == newValue) return false;`. If the incoming value matches the existing value in memory, the operation immediately aborts, emitting a debug diagnostic (`[NVS] Suppressed redundant write for key '...'`) and generating zero flash wear.
4. **Automatic NVS Persistence on Change:** When a value genuinely changes, `PersistentSetting` automatically opens the appropriate NVS namespace, calls the type-specific `put*()` primitive, and closes the namespace.
5. **Observable `onChanged` Event Hook:** Each setting can attach a lightweight callback hook (`void (*onChanged)(const T&)`). When a value changes, the callback triggers domain reactions (e.g. flagging `machineStatusForcePublish = true` or recalculating dependent math) completely decoupled from caller procedural logic.

### 3.6 Monotonic Software-Version Migrations (`SOFTWARE_VERSION` Engine)

In early firmware revisions, schema invalidation used a destructive "nuclear wipe" (`if (storedVer != EXPECTED) pref.clear();`). This was disastrous for operators because updating firmware wiped all calibrated axis steps, leadscrew pitch error compensation, and network credentials.

The modern NanoELS architecture strictly forbids blind namespace wipes. It uses a **monotonic sequential migration engine** keyed directly to `SOFTWARE_VERSION` (defined in `Config.h`):

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│ MIGRATION BOOT SEQUENCE (Core 0 / Main Setup)                                          │
└──────────────────────────────────────────┬─────────────────────────────────────────────┘
                                           │
                                           ▼
                    ┌──────────────────────────────────────────────┐
                    │ Read Stored Version from NVS: "sys.sw_ver"   │
                    │ int storedVersion = sysPref.getInt("sw_ver") │
                    └──────────────────────┬───────────────────────┘
                                           │
                                           ▼
                             ┌───────────────────────────┐
                             │ SOFTWARE_VERSION > stored?│
                             └─────────────┬─────────────┘
                                           │
                        ┌──────────────────┴──────────────────┐
                        │ NO (stored == current)              │ YES (firmware updated)
                        ▼                                     ▼
             ┌─────────────────────┐              ┌────────────────────────┐
             │ 0ms Bypass (Boot OK)│              │ Execute Cascading Steps│
             └─────────────────────┘              └───────────┬────────────┘
                                                              │
                                       ┌──────────────────────┼──────────────────────┐
                                       ▼                      ▼                      ▼
                             ┌──────────────────┐   ┌──────────────────┐   ┌──────────────────┐
                             │ if (stored < 39) │   │ if (stored < 40) │   │ if (stored < 41) │
                             │ Migrate v38->v39 │   │ Migrate v39->v40 │   │ Migrate v40->v41 │
                             └──────────────────┘   └──────────────────┘   └──────────────────┘
                                                              │
                                                              ▼
                                            ┌───────────────────────────────────┐
                                            │ Update Stored Version Checkpoint: │
                                            │ sysPref.putInt("sw_ver", SW_VER)  │
                                            └───────────────────────────────────┘
```

#### Architectural Invariants of Schema Evolution:
1. **Single Source of Truth:** `SOFTWARE_VERSION` (e.g. `38`) is the sole migration driver. All fragmented, artificial version constants (`PREFERENCES_VERSION`, `CONFIG_VERSION`, `WIFI_CONFIG_VERSION`, `KEYBOARD_CONFIG_VERSION`) are deprecated and removed.
2. **Version-Skipping Resilience:** If a lathe operator runs v38 and updates two years later to v42, the bootloader sequentially executes migrations for v39, v40, v41, and v42 in a single millisecond pass without losing any user configuration.
3. **No Migration Needed for Additions:** Because `PersistentSetting` falls back to in-struct defaults when an NVS key is missing, adding new settings or changing default values requires **zero migration code**. Migrations are strictly reserved for key renames or data type conversions.

---

## 4. Legacy Properties Audit & Deprecation Map

The legacy `h5.ino` code and early `Config.cpp` module contained over 45 properties. Below is the authoritative audit classifying each setting into **Keep & Modernize**, **Relocate to Cycle/Intent**, or **Discard/Deprecate**.

### 4.1 Physical Machine Properties (Tier 1)

| Legacy Identifier | New Canonical Key | Unit | Architectural Action & Rationale |
| :--- | :--- | :---: | :--- |
| `SCREW_Z_DU` | `machine.z.pitchDu` | `du` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.zAxis.leadscrewPitchDu()`. |
| `SCREW_X_DU` | `machine.x.pitchDu` | `du` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.xAxis.leadscrewPitchDu()`. |
| `MOTOR_STEPS_Z` | `machine.z.motorSteps` | `steps/rev` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `z.motorSteps` / `activeMachineProfile`. |
| `MOTOR_STEPS_X` | `machine.x.motorSteps` | `steps/rev` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `x.motorSteps` / `activeMachineProfile`. |
| `ENCODER_PPR` | `machine.spindle.encoderPpr` | `ppr` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.spindle.effectivePpr()`. |
| `ENCODER_BACKLASH` | `machine.spindle.backlash` | `pulses` | **KEEP & MODERNIZE:** Encoder edge filter on spindle direction reverse. |
| `BACKLASH_DU_Z` | `machine.z.backlashDu` | `du` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.zAxis.backlashDu()`. |
| `BACKLASH_DU_X` | `machine.x.backlashDu` | `du` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.xAxis.backlashDu()`. |
| `MAX_TRAVEL_MM_Z` | `machine.dimensions.maxZTravelMm` | `mm` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.zAxis.limits.travelMaxMm`. |
| `MAX_TRAVEL_MM_X` | `machine.dimensions.maxXTravelMm` | `mm` | **RETIRED & SUPERSEDED:** Removed from `Config.h`. Sourced directly from `activeMachineProfile.xAxis.limits.travelMaxMm`. |
| `SPEED_START_Z/X` | `machine.z.speedStartMmSec` | `mm/s` | **MODERNIZE:** Convert raw step pulses to physical linear speed. |
| `ACCELERATION_Z/X`| `machine.z.accelMmSec2` | `mm/s²` | **MODERNIZE:** Convert raw pulses/s² to physical acceleration. |
| `SPEED_MANUAL_MOVE`| `machine.z.maxRapidMmMin` | `mm/min` | **MODERNIZE:** Convert raw pulses/s to industry-standard rapid feed. |
| `INVERT_Z / INVERT_X` | `machine.z.invertDir` | `bool` | **KEEP:** Motor direction polarity flag. |
| `INVERT_Z/X_ENABLE`| `machine.z.invertEnable` | `bool` | **KEEP:** Stepper driver active-high vs active-low enable flag. |
| *(None - New)* | `machine.dimensions.bedLengthMm` | `mm` | **NEW:** Lathe bed length for digital twin & clearance bounds. |
| *(None - New)* | `machine.dimensions.chuckDiameterMm` | `mm` | **NEW:** Outside chuck diameter for safety envelope. |
| *(None - New)* | `machine.dimensions.spindleBoreDiameterMm` | `mm` | **NEW:** Spindle through-bore diameter. |

### 4.2 Controller Infrastructure & Hardware (Tier 2)

| Legacy Identifier | New Canonical Key | Unit | Architectural Action & Rationale |
| :--- | :--- | :---: | :--- |
| `wifi_ssid` | `sys.wifi.ssid` | string | **KEEP:** Network credentials. |
| `wifi_password` | `sys.wifi.password` | string | **KEEP:** Network credentials (write-only / masked). |
| `wifi_hostname` | `sys.wifi.hostname` | string | **KEEP:** mDNS network discovery hostname. |
| `Z_SCALE_PULSES_PER_MM` | `sys.dro.zPulsesPerMm` | float | **KEEP:** Optical scale quadrature calibration factor. |
| `X_SCALE_PULSES_PER_MM` | `sys.dro.xPulsesPerMm` | float | **KEEP:** Optical scale quadrature calibration factor. |
| `PULSE_PER_REV_Z/X` | `sys.mpg.zPulsesPerRev` | float | **KEEP:** Manual Pulse Generator handwheel encoder resolution. |
| `JOYSTICK_*` (15 fields) | `sys.joystick.*` | mixed | **ENCAPSULATED:** Removed loose globals from `Config.h`/`Config.cpp`. Encapsulated into `struct JoystickConfig joystickConfig` in `src/input/Input.h`. |
| `keyboardBindings[]` | `sys.keys.bindings` | table | **KEEP:** USB keyboard physical key mapping table. |

### 4.3 Relocated to Cycle / Intent Domain (NOT Machine Settings)

| Legacy Identifier | Original Location | New Architectural Home | Rationale |
| :--- | :--- | :--- | :--- |
| `SAFE_DISTANCE_DU` | `Config.cpp` | **Cycle Parameter (`C.safeRetractDu`)** | **RELOCATE:** Retract clearance between passes is a property of the cutting operation, NOT the physical lathe. |
| `SLOT_LEFT_REDUCTION_DU`| `Config.cpp` | **Cycle Parameter (`C.slotTaperDu`)** | **RELOCATE:** Step reduction for blind broaching is specific to Slotting Mode. |

### 4.4 Discarded Legacy Baggage

| Legacy Identifier | Original Role in `h5.ino` | Architectural Verdict & Rationale |
| :--- | :--- | :--- |
| `STEP_TIME_MS` | Software delay loop for button repeat | **DISCARD:** In the new client architecture, jogging is event-driven via `CMD.jogHold` / `CMD.jogRelease` with hardware timer ramping. |
| `DELAY_BETWEEN_STEPS_MS`| Software pause between repeated steps | **DISCARD:** Replaced by hardware acceleration ramping in motion HAL. |
| `ENABLE_CONTINUOUS_MOVE` | Auto-switch from step to continuous | **DISCARD:** Replaced by explicit semantic gestures in HMI profile. |
| `Z_NEEDS_REST / X_NEEDS_REST`| Cut stepper current while stationary | **DISCARD:** Disabling holding torque on a lathe carriage without a mechanical brake causes tool deflection and leadscrew back-drive. |
| `SHOW_KEY_PRESSES` | Debug overlay flag on Nextion LCD | **DISCARD:** Obsolete display debugging artifact. |
| `tftFirstUpload` | Nextion 9600 baud flashing flag | **DISCARD:** Firmware flashing utility flag, not a machine setting. |
| `Y_*` (All Y params) | Obsolete auxiliary Y-axis software logic | **DISCARD:** Fully retired Y-axis software motion, profiles, and settings; physical H5 PCB silkscreen GPIO defines (`Y_ENA 1`, `Y_DIR 2`, `Y_STEP 17`, `Y_PULSE_A 45`, `Y_PULSE_B 48`, `JOY_Y 6`) preserved in `Config.h` for hardware header reference. |
| `saveMachineConfig()` | Monolithic 45-field blind flat NVS dump | **DISCARD:** Retired in favor of `PersistentSetting<T>` dirty-checked storage and JSON lathe machine profiles. |
| `/config` & `/config/reset` | Monolithic HTTP form configuration endpoints | **DISCARD:** Retired in favor of `SETTINGS` namespace over WebSocket and REST profile schema endpoints. |
| `xScaleIncrementalOffset`<br>`zScaleIncrementalOffset` | Unused legacy DRO incremental offsets in `Config.h` | **DISCARD:** Dead globals from legacy DRO offset tracking. Fully retired. |
| `RPM_BULK` | Redundant spindle pulse count alias in `Config.h` | **DISCARD:** Redundant shadow alias, consolidated into `ENCODER_STEPS_INT`. |
| `isDiameterMode`<br>`isAbsoluteMode`<br>`zDisplayModeDRO`<br>`xDisplayModeDRO`<br>`spindleDisplayMode`<br>`spindleValColorGreen` | Presentation & Nextion UI flags in `Config.h` | **DISCARD AS GLOBALS:** Removed loose globals from `Config.h`/`Config.cpp`. Presentation state is now cleanly managed by `SettingsService`, `MachineProperties`, and `DisplaySnapshot`. |

---

## 5. Master Property Catalog & Schema Descriptors

Every setting in the system is defined by a formal `PropertyDescriptor` containing validation bounds, data types, and presentation metadata:

```cpp
enum PropertyType : uint8_t {
    PROP_TYPE_INT    = 0,
    PROP_TYPE_LONG   = 1,
    PROP_TYPE_FLOAT  = 2,
    PROP_TYPE_BOOL   = 3,
    PROP_TYPE_STRING = 4,
    PROP_TYPE_ENUM   = 5
};

struct PropertyDescriptor {
    const char*  key;          // Canonical wire key, e.g. "machine.z.pitch"
    uint8_t      tier;         // 1: Machine, 2: Sys, 3: Prefs, 4: Client
    PropertyType type;         // Data type
    const char*  label;        // Human-readable title
    const char*  unit;         // Display unit ("mm", "du", "RPM", "steps/rev")
    float        minValue;     // Validation floor
    float        maxValue;     // Validation ceiling
    float        step;         // Input step increment
    const char*  helpText;     // Tooltip / operator guidance
};
```

### 5.1 Canonical Property Catalog (Representative Subset)

```text
TIER 1: MACHINE PROFILE
---------------------------------------------------------------------------------------------------------------------
Key                     Type    Unit    Min     Max       Default (WM210V)   Description
---------------------------------------------------------------------------------------------------------------------
machine.preset          STRING  -       -       -         "wm210v_dual"      Active preset identifier
machine.caps            UINT32  hex     0       0xFFFFFFFF 0x000000DB        LatheCapabilityFlags bitmask
machine.dim.bedLength   FLOAT   mm      50.0    5000.0    400.0              Total lathe bed length
machine.dim.swingDia    FLOAT   mm      50.0    2000.0    210.0              Maximum swing diameter over bed
machine.dim.chuckDia    FLOAT   mm      20.0    1000.0    125.0              Outside diameter of chuck
machine.dim.spindleBore FLOAT   mm      5.0     300.0     38.0               Through-hole spindle bore diameter
machine.spindle.ppr     INT     ppr     100     50000     1200               Spindle native encoder pulses per rev
machine.spindle.ratio   FLOAT   ratio   0.1     50.0      1.0                Pulley ratio (encoder revs / spindle rev)
machine.spindle.maxRpm  FLOAT   RPM     100.0   10000.0   2500.0             Headstock mechanical safety ceiling
machine.z.pitchDu       LONG    du      100     1000000   20000              Carriage leadscrew pitch (20000 = 2.0mm)
machine.z.motorSteps    LONG    steps   100     100000    800                Carriage driver microsteps per rev
machine.z.maxRapidMmMin FLOAT   mm/min  100.0   10000.0   1500.0             Carriage rapid traverse ceiling
machine.z.accelMmSec2   FLOAT   mm/s²   10.0    2000.0    250.0              Carriage acceleration ramp rate
machine.x.pitchDu       LONG    du      100     1000000   20000              Cross-slide leadscrew pitch
machine.x.motorSteps    LONG    steps   100     100000    800                Cross-slide driver microsteps per rev
machine.x.maxRapidMmMin FLOAT   mm/min  50.0    5000.0    800.0              Cross-slide rapid traverse ceiling

TIER 2: CONTROLLER INFRASTRUCTURE
---------------------------------------------------------------------------------------------------------------------
sys.wifi.enabled        BOOL    -       0       1         1                  Enable wireless network radio
sys.wifi.ssid           STRING  -       -       -         "NanoELS-AP"       Network SSID name
sys.wifi.hostname       STRING  -       -       -         "nanoels"          mDNS network address
sys.dro.zPulsesPerMm    FLOAT   pls/mm  1.0     50000.0   1250.67            Z optical linear scale calibration
sys.dro.xPulsesPerMm    FLOAT   pls/mm  1.0     50000.0   501.20             X optical linear scale calibration

TIER 3: OPERATOR PREFERENCES
---------------------------------------------------------------------------------------------------------------------
prefs.startupMeasure    ENUM    -       0       2         0                  Default units (0:mm, 1:in, 2:pitch)
prefs.jogStepDu         LONG    du      10      100000    1000               Default D-pad jog step (1000 = 0.1mm)
prefs.dbAngle           FLOAT   deg     0.0     10.0      (Auto-derived)     Spindle angle change deadband
prefs.dbTurns           FLOAT   turns   0.0     1.0       (Auto-derived)     Spindle turns change deadband
```

---

## 6. C++ Interface Contract (`SettingsService`)

`SettingsService` is a static service class with high-speed in-memory cached state, FreeRTOS thread safety, and write-through NVS persistence:

```cpp
// src/core/SettingsService.h

class SettingsService {
public:
    // Boot Initialization: Loads overrides into RAM cache
    static void init();

    // Query Engine (Supports unicast request/reply)
    static bool get(const char* key, String* reply);
    static bool getGroup(const char* groupName, String* reply);
    static bool getSchema(const char* key, String* reply);

    // Mutation Engine (Bounds checked, updates RAM cache & write-through NVS)
    static bool set(const char* keyVal, String* reply);
    
    // Universal Reset Engine (Removes NVS key, reverts RAM cache to preset baseline)
    static bool reset(const char* key, String* reply);
    static bool resetGroup(const char* groupName, String* reply);

    // Preset Lifecycle Engine (LittleFS /presets/*.json)
    static bool listPresets(String* reply);
    static bool getPresetJson(const char* slug, String* reply);
    static bool applyPreset(const char* slug, String* reply);
    static bool saveCurrentAsPreset(const char* slug, String* reply);
    static bool deletePreset(const char* slug, String* reply);

    // High-Speed Real-Time Getters (Nanosecond access for Core 1 / MachineModel)
    static const LatheConfiguration& getActiveProfile();
    static float getDeadbandAngle();
    static bool  isDeadbandAngleAuto();
    static float getDeadbandTurns();
    static bool  isDeadbandTurnsAuto();

    // Tier 4 Stateless Client Storage Broker
    static bool getClientProperty(const char* clientId, const char* key, String* outVal);
    static bool setClientProperty(const char* clientId, const char* key, const char* val);

    // Dirty Notification
    static void notifyPropertiesChanged();
};
```

### 6.1 Multi-Core Concurrency & Lock-Free Execution
* **Core 1 (Motion & Stepper ISRs):** Reads physical parameters directly from `activeMachineProfile` in RAM. Zero mutex locks, zero flash reads, zero heap allocation.
* **Core 0 (Communications & WebSockets):** Handles incoming `SETTINGS.set` commands. Mutates the RAM struct, persists to NVS, and flags `machineStatusForcePublish = true` so all connected screens receive an updated Domain `'P'` broadcast on the next frame.

---

## 7. Canonical Wire Protocol Contracts

All communication with `SettingsService` adheres to clean, line-oriented ASCII formatting:

### 7.1 Querying Settings (`SETTINGS.get`)
Clients query individual keys, whole groups, or all settings:
```text
-> SETTINGS.get=machine.z.pitchDu\n
<- SETTING.machine.z.pitchDu=20000 (default=20000, overridden=0)\n

-> SETTINGS.getGroup=machine.dim\n
<- SETTING.machine.dim.bedLength=400.0\n
<- SETTING.machine.dim.chuckDia=125.0\n
<- SETTING.machine.dim.spindleBore=38.0\n
```

### 7.2 Mutating Settings (`SETTINGS.set`)
Clients update settings with automatic range validation:
```text
-> SETTINGS.set=machine.z.pitchDu,20025\n
<- OK:SETTINGS.machine.z.pitchDu=20025\n

-> SETTINGS.set=machine.z.pitchDu,-50\n
<- ERR:SETTINGS.machine.z.pitchDu=out of bounds (100..1000000)\n
```

### 7.3 Resetting to Baseline Defaults (`SETTINGS.reset`)
Clients can reset an individual key or an entire machine group back to preset baseline:
```text
-> SETTINGS.reset=machine.z.pitchDu\n
<- OK:SETTINGS.machine.z.pitchDu=20000 (reverted to preset default)\n

-> SETTINGS.resetGroup=machine\n
<- OK:SETTINGS.resetGroup=machine (all physical parameters reverted to WM210V)\n
```

### 7.4 Client-Scoped Storage Hook (`SETTINGS.client.*`)
Stateless clients persist their UI presentation preferences on the controller broker:
```text
-> SETTINGS.client.set=tablet_shop,diaRad=1\n
<- OK:SETTINGS.client.set=tablet_shop,diaRad=1\n

-> SETTINGS.client.get=tablet_shop,diaRad\n
<- SETTING.client.tablet_shop.diaRad=1\n
```

### 7.5 Machine Preset Operations (`SETTINGS.preset.*`)
Clients discover, inspect, apply, and save machine profiles on LittleFS without touching compiled firmware:
```text
-> SETTINGS.preset.list\n
<- PRESET.list=wm210v_stock:sys,mini_7x14:sys,my_lathe:user;active=wm210v_stock;default=wm210v_stock\n

-> SETTINGS.preset.get=wm210v_stock\n
<- {"identity":{"profile_id":"wm210v_stock","system":true,...},...}\n

-> SETTINGS.preset.apply=wm210v_stock\n
<- OK:SETTINGS.preset.apply=wm210v_stock\n

-- Setting a new catalog default profile:
-> SETTINGS.preset.setDefault=my_lathe\n
<- OK:SETTINGS.preset.setDefault=my_lathe\n

-- Reverting to the catalog default profile:
-> SETTINGS.preset.resetToDefault\n
<- OK:SETTINGS.preset.resetToDefault=wm210v_stock\n

-- Attempting to overwrite a system template:
-> SETTINGS.preset.save\n
<- ERR:SETTINGS.preset.save=cannot overwrite system template: wm210v_stock (use save=<new_name>)\n

-- Saving customizations as a new user preset:
-> SETTINGS.preset.save=my_lathe\n
<- OK:SETTINGS.preset.save=my_lathe\n

-- Updating an active user preset in-place:
-> SETTINGS.preset.save\n
<- OK:SETTINGS.preset.save=my_lathe\n

-- Attempting to delete a system template (rejected):
-> SETTINGS.preset.delete=wm210v_stock\n
<- ERR:SETTINGS.preset.delete=cannot delete system template: wm210v_stock\n

-- Attempting to delete the currently active preset (rejected):
-> SETTINGS.preset.delete=my_lathe\n
<- ERR:SETTINGS.preset.delete=cannot delete active preset: my_lathe\n

-- Attempting to delete the catalog default preset (rejected):
-> SETTINGS.preset.delete=my_default_lathe\n
<- ERR:SETTINGS.preset.delete=cannot delete default preset: my_default_lathe\n

-- Deleting an inactive user preset:
-> SETTINGS.preset.delete=old_test_preset\n
<- OK:SETTINGS.preset.delete=old_test_preset\n
```

### 7.6 Dynamic Schema Introspection (`SETTINGS.schema=*`)
Clients dynamically inspect configuration schemas to auto-generate forms and handle conditional validation:
```text
-> SETTINGS.schema=lathe-profile\n
<- {"$schema":"http://json-schema.org/draft-07/schema#","title":"LatheMachineProfile",...}\n
```
*(Also accessible via HTTP endpoint `GET /api/schema/profile` or `GET /schemas/lathe-profile.schema.json`)*

---

## 8. Summary of Architectural Guarantees

1. **Zero NVS Default Pollution:** Fresh machines have pristine flash; only intentional customizations are saved.
2. **Firmware Upgrade Resilience:** Firmware improvements to default curves or deadbands propagate automatically to all non-overridden machines.
3. **Decoupled Responsibilities:** Machine physical dimensions never collide with network credentials or client themes.
4. **Instant Reset Assurance:** An operator can always safely revert a customized axis or machine back to factory defaults with a single command.
5. **Rock-Solid Motion Safety:** Core 1 motion control reads validated physical settings from fast RAM with zero FreeRTOS lock contention.
6. **Zero FileSystem-Flash Dependency & Preserved User Data:** Compiling the Web UI and factory system templates into `firmware.bin` ensures that firmware updates require only flashing the application partition. User-created presets, custom tool tables, and uploaded lathecode/G-code files in LittleFS are never wiped during updates.
7. **Future-Proof Schema Decoupling:** Dynamic JSON Schema (draft-07 / 2020-12) introspection allows adding new machine properties without requiring client code changes.

