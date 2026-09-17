# NanoELS H5 - Project TODOs & Architecture Roadmap

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
   - Bound to modern LVGL display driver components (`screen_lvgl`).

## 2. Dynamic Hardware Pulse Counter Assignment & Linear Scale Settings

### Motivation
The ESP32-S3 contains **4 independent hardware pulse counter (PCNT) units** (`driver/pulse_cnt.h`). In a complete lathe setup with spindle encoder, X/Z handwheels, Y rotary axis, and X/Z optical glass or magnetic scales, the number of quadrature devices (up to 6) exceeds the 4 available hardware counters:

| Channel / Unit | Default Allocation | Alternative / DRO Allocation |
| :--- | :--- | :--- |
| **Unit 0** | Spindle Optical Encoder (`ENC_A` 14, `ENC_B` 13) | Fixed / Mandatory for electronic leadscrew (ELS) |
| **Unit 1** | Z-Axis Handwheel MPG (`Z_PULSE_A` 18, `Z_PULSE_B` 8) | Z-Axis Linear DRO Scale (`Z_SCALE_PIN_A` 9, `Z_SCALE_PIN_B` 12) |
| **Unit 2** | X-Axis Handwheel MPG (`X_PULSE_A` 47, `X_PULSE_B` 21) | Reserved for X Handwheel |
| **Unit 3** | Y-Axis Handwheel MPG (`Y_PULSE_A` 45, `Y_PULSE_B` 48) | X-Axis Linear DRO Scale (`X_SCALE_PIN_A` 40, `X_SCALE_PIN_B` 39) |

Currently, scales are conditionally compiled out via `#define HAS_DRO_Z false` and `#define HAS_DRO_X false` to prioritize physical handwheels for Z and X.

### Tasks to Implement

1. **Web UI / Preferences PCNT Mapping:**
   - Add preferences (`cfg.droZ`, `cfg.droX`, `cfg.hasY`) to configure channel assignment at runtime.
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

## 3. Motion Timing & User Interface Settings

1. **Configurable Jog Hold Delay (`jogHoldDelayMs`):**
   - Current default is `1000 ms` in `Config.h` (`DEFAULT_JOG_HOLD_DELAY_MS`).
   - Expose `jogHoldDelayMs` in the Web UI Settings tab and store in Preferences (NVS).
   - Allows machinists to customize the threshold between single-step taps and continuous jogging (e.g. 500ms for capacitive vs 1000ms for resistive).


