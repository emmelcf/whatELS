# whatELS - Project TODOs & Architecture Roadmap

This folder contains the master TODO list for whatELS, along with dedicated subfolders containing in-depth design documents and specifications for complex roadmap items.

---

## Index of Roadmap Items

| Item | Title | Supporting Docs | Status |
| :---: | :--- | :---: | :---: |
| **1** | [Instantiable & Extendable DisplayModel Architecture](#1-instantiable--extendable-displaymodel-architecture) | — | Planned |
| **2** | [Dynamic Hardware Pulse Counter Assignment & Linear Scale Settings](#2-dynamic-hardware-pulse-counter-assignment--linear-scale-settings) | — | Planned |
| **3** | [Motion Timing & User Interface Settings](#3-motion-timing--user-interface-settings) | — | Planned |
| **4** | [Angled / Flank Infeed for Automatic Threading (`MODE_THREAD`)](#4-angled--flank-infeed-for-automatic-threading-mode_thread) | — | Planned |
| **5** | [Hardware-in-the-Loop (HIL) Test Rig & Step Interception](#5-hardware-in-the-loop-hil-test-rig--step-interception) | [`todo/hil_rig/README.md`](hil_rig/README.md) | Backlog / Documented |

---

## 1. Instantiable & Extendable DisplayModel Architecture

### Motivation
Currently, `DisplayModel` centralizes unit conversions (Metric vs Imperial) and formatting logic for all display consumers (Nextion display, WebSocket streaming via `UI.*`, and GRBL status queries via `WPos`). However, different consumers have fundamentally different needs:

| Consumer | Primary Use Case | Target Display Profile |
| :--- | :--- | :--- |
| **Physical DRO (Nextion / LVGL)** | Machinist standing at the lathe. | Fixed 3 decimal places (`0.000`), diameter mode for X, tracks machine hardware buttons. |
| **GRBL / CNC G-Code Sender** | Automated CAM/CNC senders (Candle, UGS, CNCjs). | Higher precision (e.g. 4 decimals in mm, 5 in inch), radius mode for X (standard G-code), no trailing zero padding. |
| **Web UI Dashboard** | Remote monitor or tablet. | Can follow client-specific preferences (e.g. Imperial readout on phone while operator runs Metric at machine). |

### Proposed Design

Refactor `DisplayModel` into an instantiable, configurable class that takes a `DisplayOptions` configuration profile:

```cpp
struct DisplayOptions {
  int  measureSystem    = MEASURE_METRIC; // MEASURE_METRIC or MEASURE_INCH
  int  positionDecimals = 3;              // 3 for DRO, 4 or 5 for GRBL/CAM
  bool diameterMode     = true;           // Lathe diameter vs. true radius
  bool fixedPadding     = true;           // true: "0.000", false: trimmed "0"
};

class DisplayModel {
public:
  DisplayModel(const DisplayOptions& defaultOptions = {});

  void setOptions(const DisplayOptions& options);
  const DisplayOptions& getOptions() const;

  // Measurement and formatting methods
  String formatPosition(float mm) const;
  String formatAxis(Axis* a) const;
  DisplaySnapshot getSnapshot() const;

private:
  DisplayOptions _options;
};
```

### Channel Profiles to Implement

1. **`nextionDisplay`**:
   - `positionDecimals = 3`
   - `diameterMode = isDiameterMode`
   - `fixedPadding = true`
   - Bound to hardware Nextion UI driver in `Display.cpp`.

2. **`grblDisplay`**:
   - `positionDecimals = 4` (or 5 for inches)
   - `diameterMode = false` (standard G-code tool coordinates)
   - `fixedPadding = false`
   - Bound to GRBL `?` query handler in `Motion.cpp`.

3. **`webDisplay` / Client Profiles**:
   - Can track per-client WebSocket preferences or machine defaults.
   - Pushed via `publishMachineStatus()` in `WebServerModule.cpp`.

4. **`lvglDisplay`**:
   - `positionDecimals = 3`
   - `diameterMode = isDiameterMode`
   - `fixedPadding = true`
   - Bound to native embedded LVGL touch display client.

---

## 2. Dynamic Hardware Pulse Counter Assignment & Linear Scale Settings

### Motivation
The ESP32-S3 contains **4 independent hardware pulse counter (PCNT) units** (`driver/pulse_cnt.h`). In a complete lathe setup with spindle encoder, X/Z handwheels, and X/Z optical glass or magnetic scales, the 4 available hardware counters are allocated as follows:

| Channel / Unit | Default Allocation | Alternative / DRO Allocation |
| :--- | :--- | :--- |
| **Unit 0** | Spindle Optical Encoder (`ENC_A` 14, `ENC_B` 13) | Fixed / Mandatory for electronic leadscrew (ELS) |
| **Unit 1** | Z-Axis Handwheel MPG (`Z_PULSE_A` 18, `Z_PULSE_B` 8) | Z-Axis Linear DRO Scale (`Z_SCALE_PIN_A` 9, `Z_SCALE_PIN_B` 12) |
| **Unit 2** | X-Axis Handwheel MPG (`X_PULSE_A` 47, `X_PULSE_B` 21) | Reserved for X Handwheel |
| **Unit 3** | Unallocated / Reserved | X-Axis Linear DRO Scale (`X_SCALE_PIN_A` 40, `X_SCALE_PIN_B` 39) |

Currently, scales are conditionally compiled out via `#define HAS_DRO_Z false` and `#define HAS_DRO_X false` to prioritize physical handwheels for Z and X.

### Tasks to Implement

1. **Web UI / Preferences PCNT Mapping:**
   - Add preferences (`cfg.droZ`, `cfg.droX`) to configure channel assignment at runtime.
   - Dynamically attach the appropriate peripheral during startup (`taskAttachInterrupts`):
     - If `droZ` is enabled $\rightarrow$ allocate Unit 1 to `zScale`.
     - If `droZ` is disabled $\rightarrow$ allocate Unit 1 to `PULSE_COUNTER_Z` handwheel.
     - If `droX` is enabled $\rightarrow$ allocate Unit 3 to `xScale`.
     - If `hasY` is enabled and `droX` is disabled $\rightarrow$ allocate Unit 3 to `PULSE_COUNTER_Y`.

2. **Configurable Linear Scale Resolution:**
   - Store scale resolution in `Preferences` (NVS) rather than hardcoded `#define`:
     - `xScalePulsesPerMm` (default `500.0f` for 2 µm scale or `200.0f` for 5 µm scale).
     - `zScalePulsesPerMm` (default `1000.0f` for 1 µm scale or `200.0f` for 5 µm scale).
   - Add preset dropdowns in the Web UI:
     - 5 µm scale $\rightarrow$ 200 pulses/mm
     - 2 µm scale $\rightarrow$ 500 pulses/mm
     - 1 µm scale $\rightarrow$ 1,000 pulses/mm
     - 0.5 µm scale $\rightarrow$ 2,000 pulses/mm
     - Custom float input for fine calibration.

3. **In-UI Calibration Routine:**
   - Provide a calibration wizard in the Web UI:
     1. Operator touches off with gauge block or indicator and zeroes scale.
     2. Operator moves a known physical distance (e.g. 25.0 mm or 50.0 mm).
     3. Firmware reads total raw pulse delta and calculates:
        $$\text{PulsesPerMm} = \frac{\Delta \text{RawPulses}}{\text{KnownDistanceMm}}$$
     4. Saves the calibrated factor directly to NVS preferences.

---

## 3. Motion Timing & User Interface Settings

1. **Configurable Jog Hold Delay (`jogHoldDelayMs`):**
   - Current default is `1000 ms` in `Config.h` (`DEFAULT_JOG_HOLD_DELAY_MS`).
   - Expose `jogHoldDelayMs` in the Web UI Settings tab and store in Preferences (NVS).
   - Allows machinists to customize the threshold between single-step taps and continuous jogging (e.g. 500ms for capacitive vs 1000ms for resistive).

---

## 4. Angled / Flank Infeed for Automatic Threading (`MODE_THREAD`)

### Motivation
In `Motion.cpp` (`modeTurn()`), automatic threading currently executes **straight radial plunge infeed ($90^\circ$)**:
- While whatELS applies a quadratic de-gressing pass calculation (`fraction * fraction`) to reduce cut depth as thread depth increases, plunging straight down cuts simultaneously on both the leading and trailing edges of the tool insert.
- On medium and coarse pitch threads, radial plunge creates excessive cutting pressure, chatter, poor surface finish, and premature insert tip chipping.
- Standard lathe machining practice utilizes **modified flank infeed** (typically $29^\circ$ or $29.5^\circ$ for $60^\circ$ ISO Metric / UN threads, or $27^\circ$ for $55^\circ$ BSW/Whitworth), cutting primarily on the leading edge while lightly shaving the trailing flank.

### Proposed Implementation
1. **Configurable Thread Infeed Angle (`threadInfeedAngle`):**
   - Add parameter in `Config.h` / Preferences with common presets:
     - `0.0°` (Straight Radial Plunge — useful for brass, square threads, or very fine pitch)
     - `29.0°` / `29.5°` (Modified Flank Infeed — industry standard for $60^\circ$ threads)
     - `30.0°` (Full Flank Infeed)
     - `27.0°` / `27.5°` (For $55^\circ$ British Standard / Whitworth)
2. **Synchronized Infeed Geometry:**
   - On each threading pass $i$, calculate both the radial plunge depth $\Delta X_i$ and the corresponding axial $Z$ start offset:
     $$\Delta Z_{\text{offset}} = \Delta X_i \cdot \tan(\alpha)$$
   - Direction rule: For external right-hand threads, shift $Z$ start slightly away from chuck (or toward chuck for internal) so tool cuts on the leading edge.
3. **Spring Passes:**
   - Support 1 or 2 final "spring passes" (repeated passes at final full depth without incremental infeed) to eliminate tool deflection and produce clean, burr-free flanks.

---

## 5. Hardware-in-the-Loop (HIL) Test Rig & Step Interception

### Summary
A testing architecture utilizing a secondary ESP32-S3 as a dedicated hardware stimulus generator and step pulse analyzer. This closes the verification gap by validating that physical `STEP` and `DIR` pulses match the mathematical toolpath without altering production firmware or telemetry protocols.

* **Full Architecture & Specification:** See [`todo/hil_rig/README.md`](hil_rig/README.md).
* **Key Components:**
  * **Secondary ESP32-S3 Hardware Quadrature Generator:** Injects high-speed quadrature A/B signals (10–3,000 RPM spindle, MPG clicks) into primary controller inputs.
  * **Secondary ESP32-S3 Hardware Pulse Analyzer:** Counts physical `Z_STEP`, `Z_DIR`, `X_STEP`, `X_DIR` pins via PCNT hardware counters.
  * **WebSerial Browser Connection:** Streams physical step counts directly to the digital twin visualizer (`lathe-viz.js`).
* **Design Principle:** Zero changes to primary controller state messaging to avoid the observer effect.
