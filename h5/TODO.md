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
