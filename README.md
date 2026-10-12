# NanoELS H5 — Electronic Lead Screw & Multi-Client Suite

CNC and electronic lead screw (ELS) controller based on the ESP32-S3 supporting up to 3 axes for metal lathes.

- **Electronic Gearing & Multi-Start Threading** with real-time spindle encoder synchronization
- **Multi-Pass Canned Cycles** (Turning, Facing, Cones/Tapers, Cutoff, Spheres, Slotting)
- **Modular Multi-Client Architecture**: Embedded Web UI, Modern Touch DRO, Native LVGL Touch Display, USB/PS2 keyboard, and MPG pendants
- **Hardware Scale & DRO Support**: Direct integration with optical rotary encoders and linear glass DRO scales
- **Dual-Core Concurrency**: Real-time deterministic motion timing on Core 1; asynchronous web, telemetry, and supervisory services on Core 0

---

## Project Structure

```text
nanoels/
├── platformio.ini           # PlatformIO build configuration (ESP32-S3, LittleFS)
├── README.md                # Project overview & build guide
├── todo/                    # Development roadmap, architecture backlog & specs
│   └── TODO.md              # Master roadmap & topic index
│
├── src/                     # Core controller firmware (headers & implementations)
│   ├── main.cpp             # System setup & cooperative executive loops
│   ├── core/                # MachineModel, Config, SettingsService, Debug
│   ├── motion/              # Motion engine, Axis control, LatheProfile, DRO scales
│   ├── input/               # Keyboard, MPG, and operator input controls
│   ├── web/                 # WebServer, WebSockets (:81), CommandDispatcher, ClientEndpoint
│   └── display/             # DisplayModel, RichClientAdapter, Display hardware drivers
│
├── data/                    # LittleFS flash filesystem assets
│   └── www/                 # Default embedded web UI (index.html, app.js, style.css)
│
├── clients/                 # Operator client applications
│   └── web_ui/              # Modern 800×480 Touch DRO Web UI (Alpine.js, HTML5/CSS3)
│
├── docs/                    # Authoritative system & architectural documentation
│   ├── CONTROLLER_ARCHITECTURE.md   # Firmware pipeline, domains (K, I, C, P), and wire protocol
│   ├── NANOELS_MASTER_ARCHITECTURE.md # Master 6-subsystem architecture blueprint
│   ├── ELS_CLIENT_ARCHITECTURE.md    # Multi-client pipeline & WebSocket specification
│   ├── ELS_CORE_STATE_MODEL.md       # Canonical deci-micron state invariants
│   ├── LATHE_MACHINE_PROFILE.md      # Physical lathe geometry & axis definitions
│   ├── ELS_MODE_ARCHITECTURE.md      # Operational mode state machine specifications
│   ├── ELS_PROPERTIES_AND_SETTINGS.md# Persistent parameter & NVS settings ledger
│   ├── TIER2_STATE_MAPPING.md        # State mapping across subsystems
│   ├── modes/                        # Detailed mode specifications (turning, threading, etc.)
│   ├── hmi/                          # Touch targets & 800×480 screen guidelines
│   └── clients/                      # Web UI interaction ledger & documentation
└── reference/               # Preserved historical reference files (read-only)
    ├── README.md                     # Context & mapping guide to modular src/
    ├── h5_monolithic.ino             # Original upstream monolithic sketch (~7k LOC)
    ├── H5_DRO_V104personal.ino       # Historical DRO development variant
    └── NANOELS_H5_README.md          # Legacy H5 documentation, BOM & schematics
```

---

## Getting Started

### Prerequisites
* [VS Code](https://code.visualstudio.com/) with the **PlatformIO IDE** extension, or the **PlatformIO Core (CLI)** (`pio`).

### Building Firmware
Compile the project from the repository root:
```bash
pio run
```
Build outputs are generated in `.pio/build/esp32s3/`:
* `firmware.bin`
* `firmware.factory.bin` (flashable at offset `0x0000`)

### Uploading to Device
Connect the ESP32-S3 via USB and run:
```bash
# Upload firmware
pio run -t upload

# Upload LittleFS Web UI filesystem assets (data/www)
pio run -t uploadfs
```

### Serial Monitor
```bash
pio device monitor -b 115200
```

---

## Architectural Guidelines & Safety Invariants

* **Canonical Units**: All physical positions, pitches, and travels are represented internally in **integer deci-microns** ($1\,\text{du} = 0.1\,\mu\text{m} = 0.0001\,\text{mm}$).
* **Cross-Slide Radius**: The controller tracks cross-slide position strictly in canonical radius (`xDu`). Diameter display ($\varnothing$) is a client-local presentation concern.
* **Lossless Intent**: Operator authoring setpoints (e.g., integer TPI or custom taper ratios) are preserved alongside calculated steps to eliminate floating-point round-trip drift.
* **Controller Authority**: The MCU is the sole authority for machine state and limit checking. Clients are reactive observers transmitting operator intent.
* **Shop Safety Rules**: Detailed machinist safety, 800×480 touch target constraints, and operator frame-of-reference rules are maintained in [`.agents/rules/lathe_hmi_and_safety.md`](.agents/rules/lathe_hmi_and_safety.md).

---

## License & Attribution

This project is licensed under the terms described in [LICENSE](LICENSE). Historical notes and notices are documented in `reference/` and `docs/`.
