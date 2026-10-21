# whatELS — Master System Architecture & Subsystem Specification

> **Status:** Authoritative "Ground Floor" Master Architecture Blueprint  
> **Target Platform:** whatELS Electronic Leadscrew & Multi-Client Suite (ESP32-S3)  
> **Author:** Antigravity & Emil  
> **Date:** October 8, 2026 (Updated October 2026)  
> **Location:** `docs/WHATELS_MASTER_ARCHITECTURE.md`  
> **Supersedes:** `ELS_DESIGN_NOTEBOOK.md` (as the root architectural index)

---

## 1. Executive System Vision & Core Tenets

The **whatELS Architecture** modernizes and reimagines open-source embedded lathe control into an industrial-grade, multi-client electronic leadscrew (ELS) and digital readout (DRO) platform.

### 1.1 The Evolutionary Leap
* **From:** A monolithic Arduino sketch (`h5.ino`, ~370 KB) with 200+ unencapsulated procedural globals, hardwired Nextion LCD packets, and high-frequency stepper timer ISRs interleaved with serial communication.
* **To:** A clean, decoupled **6-subsystem architecture** built on strict hardware abstraction, deterministic dual-core concurrency, domain-driven telemetry, and a client-agnostic wire protocol.

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        PHYSICAL MACHINE TOOL (LATHE IRON)                              │
│ • Carriage (Z), Cross-Slide (X), Auxiliary (Y) Leadscrews & Motors                     │
│ • Spindle Quadrature Encoder & VFD / Servo Spindle Drive                               │
│ • Linear Glass Scales (DRO), Physical Limit Switches, Apron Handwheels (MPG)          │
└───────────────────────────────────────────▲────────────────────────────────────────────┘
                                            │ Hardware Pins & ISRs
┌───────────────────────────────────────────┴────────────────────────────────────────────┐
│ CORE 1: DETERMINISTIC REAL-TIME MOTION                                                 │
│ [Subsystem 1: HAL & Hardware Timing] ──► [Subsystem 2: Kinematic Kernel & Motion]     │
│                                      ▲                                                 │
│                                      │ Motion Primitives                               │
│                      [Subsystem 3: Operational Modes Engine]                           │
└───────────────────────────────────────────▲────────────────────────────────────────────┘
                                            │ Atomic Point-in-Time Telemetry / CMDs
┌───────────────────────────────────────────┴────────────────────────────────────────────┐
│ CORE 0: ASYNCHRONOUS SUPERVISORY & COMMUNICATIONS                                     │
│ [Subsystem 4: State Model & Settings] ◄──► [Subsystem 5: Command Dispatcher & Transport]│
│                                                           │                            │
│                                                           ▼                            │
│                                            [Subsystem 6: HMI & Client Adapters]        │
└───────────────────────────────────────────────────────────┬────────────────────────────┘
                                                            │
    ┌───────────────────────────────┬───────────────────────┴───────┬────────────────────┐
    ▼                               ▼                               ▼                    ▼
┌───────────────────────┐ ┌───────────────────┐ ┌────────────────────────┐ ┌─────────────────┐
│ Modern Web UI (Tablet)│ │ Native LVGL 9 HMI │ │ Physical Inputs & MPG  │ │ Transitional    │
│ (800×480 Capacitive)  │ │ (ESP32-S3 Display)│ │ (Keypad, Joystick)     │ │ Nextion Display │
└───────────────────────┘ └───────────────────┘ └────────────────────────┘ └─────────────────┘
```

---

### 1.2 Dual-Core Concurrency & FreeRTOS Architecture

The ESP32-S3 dual-core processor enforces strict hardware isolation between deterministic motion timing and asynchronous communication:

| Core | Primary Role | Execution Paradigm | Safety & Timing Invariants |
| :--- | :--- | :--- | :--- |
| **Core 1** | **Real-Time Motion Control** | High-frequency hardware timer ISRs, pulse counters (`PCNT`), deterministic DDA stepping (`taskMoveZ`, `taskMoveX`). | **Zero Heap Allocation, Zero Blocking Mutexes, Zero Network I/O.** Must execute in nanoseconds without latency jitter. |
| **Core 0** | **Supervisory & Comms** | FreeRTOS tasks (`taskWiFi`, `taskDisplay`) handling WebSockets, HTTP server, UART line parsing, NVS flash writes. | May perform JSON parsing, network transfers, and flash I/O. Communicates with Core 1 strictly via atomic state snapshots and thread-safe queues. |

#### Recursive Mutex & Task Protection (`wsMutex`)
To prevent race conditions between asynchronous network events and telemetry broadcasts on Core 0:
- **`taskWiFi`**: Runs `webSocket.loop()`, client handshakes, and command parsing. When a client sends a command, `handleWebUiEventCommand()` synchronously dispatches replies.
- **`taskDisplay`**: Periodically publishes changed machine telemetry (`publishMachineStatus()`) across active transports.
- **`wsMutex`**: Shared WebSocket access is protected by a recursive FreeRTOS mutex (`xSemaphoreCreateRecursiveMutex`). This allows re-entrant calls when command handlers synchronously emit response frames without self-deadlock, while ensuring Core 1 stepper pulses remain 100% untouched and unblocked.

---

### 1.3 Governing Architectural Invariants

1. **The Controller is the Sole Authority:** Mode execution, boundary collision enforcement, electronic gearing ratios, and multi-pass canned cycles run deterministically on the controller MCU. Connected screens (Web, tablet, LVGL, Nextion) are reactive observers transmitting *operator intent*.
2. **Radical Hardware Fidelity & Transparency:** The motion core operates on a strict binary state: `STOPPED` (`isOn == false`) or `RUNNING` (`isOn == true`). There are no software "ghost limits", no simulated mid-pass pauses, and no synthetic machine state that does not map directly to controller registers.
3. **Running Safety Lockout & Permanent STOP:** When lathe motion is engaged (`isOn == true`), modal dialogs that occlude live DRO readouts are locked out, and mode switching is prohibited. The red **`STOP`** button is permanently anchored and universally unoccluded across all client views.
4. **Zero-WiFi Control Plane Mandate:** Wireless networks in a machine shop are subject to severe EMI from VFDs and motor brushes. Automated threading cycles, electronic gearing, and emergency stops execute strictly via on-chip memory or wired serial UART. Wi-Fi and WebSockets are dedicated to supervisory telemetric observation and setup.

---

## 2. The 6 Subsystems: Responsibilities & Invariants

To eliminate cross-file coupling and provide clear architectural homes for all code, the system is partitioned into **6 distinct subsystems**:

```
┌─────────────────────────────────────────────────────────────────────────────────────────────────┐
│                                   THE 6 ARCHITECTURAL SUBSYSTEMS                                │
├─────────────────────┬────────────────────────────────┬──────────────────────────────────────────┤
│ Subsystem           │ Core Responsibility            │ Strict Boundaries & Invariants           │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 1. Hardware Timing  │ Stepper pulse timer ISRs, pin  │ • Core 1 only.                           │
│    & HAL (`hal/`)   │ drivers, quadrature spindle    │ • NEVER include WiFi, WebSockets, or UI. │
│                     │ interrupts, optical DRO scales.│ • Lock-free hardware register access.    │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 2. Kinematic Kernel │ Atomic kinematic functions     │ • Core 1 only.                           │
│    & Motion         │ (`FollowSpindle`,              │ • Operates in integer deci-microns (du). │
│    (`motion/`)      │  `MoveVelocity`), gearing math,│ • Universal soft limit safety envelope   │
│                     │ backlash, accel/decel profiling│   enforced across ALL modes.             │
│                     │ and hardware pulse generation. │ • Pure math & pulse coordination.        │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 3. Operational Mode │ Operational mode state machines│ • Implements `IELSMode` (enter/loop/exit)│
│    Engine (`modes/`)| (`GearboxMode`, `TurnCycle`,   │ • Orchestrates functions from Subsystem 2│
│                     │ `FaceCycle`, `ThreadCycle`).   │ • Atomic motion execution (`startFeed`). │
│                     │ Prunes unavailable via `caps`. │ • Mode-agnostic extensibility.           │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 4. State Model &    │ Single source of truth. 4-tier │ • Atomic point-in-time snapshots (K,I,C,P│
│    Persistence      │ properties (`machine`, `sys`,  │ • Sparse NVS overrides (pristine flash). │
│    (`model/`,       │ `prefs`, `client`), LittleFS   │ • 3-tier preset fallback hierarchy:      │
│    `settings/`)     │ JSON catalog & schema streaming│   Active (NVS) ──► Default ──► ROM Failsafe│
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 5. Command          │ Line protocol decoder, client  │ • Canonical ASCII wire strings (`CMD.*`, │
│    Dispatcher &     │ multiplexing, routing incoming │   `SETTINGS.*`).                         │
│    Transport        │ commands, broadcasting deltas. │ • Normalizes semantic terms (CHUCK/TAIL) │
│    (`transport/`)   │ Ingress Staging Adapter for    │   to Cartesian signs (-1/+1).            │
│                     │ simple clients; atomic cmds.   │ • Transports (WS, UART) feed Dispatcher. │
├─────────────────────┼────────────────────────────────┼──────────────────────────────────────────┤
│ 6. Human-Machine    │ Hardware inputs (keypad, MPG,  │ • "Input is just an internal client."    │
│    Interface (HMI)  │ joystick), Nextion display,    │ • Translates buttons/ticks into CMD.*    │
│    Adapters (`hmi/`)| native LVGL display, Web UI.   │ • Displays observe telemetry; never run  │
│                     │                                │   motion logic directly.                 │
└─────────────────────┴────────────────────────────────┴──────────────────────────────────────────┘
```

---

## 3. Transitional Adapters & Verification Bridges

During this architectural transition, two specific legacy components require careful isolation to avoid polluting the core:

### 3.1 Legacy Nextion Display: Decommissioned
* **Decommissioning Status:**  
  The legacy procedural Nextion LCD UART driver (`src/display/Display.cpp`, `DisplayModel.cpp`) and Nextion UART OTA flashing routes have been fully decommissioned and removed from the core firmware to eliminate code coupling, procedural globals, and anti-patterns.
* **Future Client Disposition:**  
  If Nextion or dumb UART displays are supported in the future, they will reside entirely outside the core firmware as an isolated client adapter conforming to [`ELS_CLIENT_ARCHITECTURE.md`](./clients/ELS_CLIENT_ARCHITECTURE.md).

---

### 3.2 Flash Partition Topology & Web Delivery

To guarantee that firmware upgrades never destroy user calibrations, custom machine profiles, or uploaded part programs (lathecode / G-code), the ESP32 storage architecture strictly separates system code from operator runtime data:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       whatELS FLASH PARTITION ROLES                         │
├────────────────────────────────┬────────────────────────────────────────────┤
│ 1. APPLICATION FLASH (`app0`)  │ • C++ Motion Core & Hard Real-Time Loops   │
│    Firmware Binary (.bin)      │ • Embedded Gzipped Web UI (`PROGMEM`)      │
│    Over-The-Air (OTA) Target   │ • Factory System Templates (WM210V, etc.)  │
│                                │ • Authoritative JSON Configuration Schemas │
├────────────────────────────────┼────────────────────────────────────────────┤
│ 2. NON-VOLATILE STORAGE (`nvs`)│ • Sparse Machine Overrides (`screw_z_du`)  │
│    Fast Key-Value Flash        │ • Active Profile Identifier (`preset`)     │
│                                │ • Wi-Fi Credentials & Hardware Diagnostics │
├────────────────────────────────┼────────────────────────────────────────────┤
│ 3. USER FILESYSTEM (LittleFS)  │ • User Custom Lathe Profiles (`/presets/`) │
│    Dedicated Operator Storage  │ • User Lathecode & G-Code Programs (`/`)   │
│    NEVER WIPED BY FIRMWARE     │ • User Tool Libraries & Wear Offsets       │
└────────────────────────────────┴────────────────────────────────────────────┘
```

* **Single-Binary OTA Deployment:** All firmware logic, the complete setup Web UI (`data/www/`), and built-in factory lathe templates are compiled directly into `firmware.bin`. Updating via USB or Web OTA (`/firmware/upload`) flashes only the application partition, guaranteeing that backend kinematics and frontend UI update atomically with zero version desynchronization.
* **Preservation of User Files:** The LittleFS filesystem partition is **never formatted, wiped, or overwritten during firmware updates**. Custom user presets and uploaded machining files remain 100% persistent across firmware releases.
* **Guaranteed ROM Asset Delivery:** All web portal assets (`index.html`, `style.css`, `app.js`) are served directly out of flash ROM (`send_P`) with gzip encoding. Stale filesystem files cannot shadow or break the controller web interface.
* **Drop-In Protocol Alignment:** The web client connects to the controller WebSocket endpoint and exchanges canonical ASCII line commands (`CMD.*`, `SETTINGS.*`) and domain telemetry frames (`K`, `I`, `C`, `P`).

---

## 4. Current Codebase File Audit & Target Subsystem Mapping

Below is the complete audit of all implementation and header files across `src/`, detailing their current role, target subsystem, and migration path:

| File (in `src/`) | Current Role in Codebase | Target Subsystem | Architectural Disposition & Migration Task |
| :--- | :--- | :--- | :--- |
| [`main.cpp`](../src/main.cpp) | Boot orchestrator, task creator, procedural `loop()` mode ladder. | **System Bootstrap** | Refactor `loop()` to delegate to `IELSModeRegistry::getActiveMode()->update()`. Remove procedural mode `if-else` ladder. |
| [`Axis.cpp`](../src/motion/Axis.cpp)<br>[`Axis.h`](../src/motion/Axis.h) | `Axis` struct, hardware stepping pins, backlash, motor step math. | **Subsystem 1 (HAL)** | Extract hardware pin stepping and pulse generation into clean HAL classes. Remove coupling to legacy global variables. |
| [`Motion.cpp`](../src/motion/Motion.cpp)<br>[`Motion.h`](../src/motion/Motion.h) | Mixed: Timer ISRs, spindle pulse tracking, legacy mode loops (`modeTurn`, etc.). | **Subsystem 2 (Kinematics)** | Separate real-time timer ISRs & spindle math from mode logic. Move canned cycles (`modeTurn`, `modeCone`) into Subsystem 3 `IELSMode` classes. |
| [`Config.cpp`](../src/core/Config.cpp)<br>[`Config.h`](../src/core/Config.h) | System and network configuration, keyboard & WiFi settings, and persistent key mappings. | **Subsystem 4 (Persistence)** | Modernized foundation. Hardware pins isolated into `BoardPins.h`; active machine profile and persistence managed by `SettingsService`. |
| [`Input.cpp`](../src/input/Input.cpp)<br>[`Input.h`](../src/input/Input.h) | Keypad matrix, button debounce, joystick ADC, Nextion input parser. Directly mutates state. | **Subsystem 6 (HMI Adapters)** | Refactor so input routines NEVER mutate motion state directly. They must emit canonical `CMD.*` strings into `CommandDispatcher`. |
| `src/display/` (`Display.cpp`, `DisplayModel.cpp`) | Legacy Nextion LCD UART driver & display formatter. | **Decommissioned** | **[REMOVED]** Decommissioned from firmware. Any future UART display will be an isolated client adapter conforming to Client Architecture. |
| [`WebServerModule.cpp`](../src/web/WebServerModule.cpp)<br>[`WebServerModule.h`](../src/web/WebServerModule.h) | HTTP static file server, OTA firmware flashing (`/firmware/upload`), and WebSocket server lifecycle. | **Subsystem 5 (Transport)** | Modernized foundation. Stripped legacy routes and direct mutation; delegates all network state machine handling to `WifiService` and WebSocket routing to `ClientManager`. |
| [`WifiService.cpp`](../src/core/WifiService.cpp)<br>[`WifiService.h`](../src/core/WifiService.h) | Encapsulated Wi-Fi infrastructure (Station/AP state machine, exponential-backoff watchdog, mDNS, scan). | **Subsystem 5 (Transport)** | Clean core network service. Decoupled from HTTP serving; integrates directly with `SettingsService`. |
| [`DRO_Scale.cpp`](../src/motion/DRO_Scale.cpp)<br>[`DRO_Scale.h`](../src/motion/DRO_Scale.h) | ESP32 PCNT hardware pulse counter for optical linear scales. | **Subsystem 1 (HAL)** | Clean HAL component. Keep and integrate with `QuadratureEncoderConfig`. |
| [`UsbKeyboard.cpp`](../src/input/UsbKeyboard.cpp)<br>[`UsbKeyboard.h`](../src/input/UsbKeyboard.h) | USB Host keyboard driver. | **Subsystem 6 (HMI Adapters)** | Clean input driver. Maps USB key scancodes to canonical `CMD.*` strings. |
| [`LatheProfile.cpp`](../src/motion/LatheProfile.cpp)<br>[`LatheProfile.h`](../src/motion/LatheProfile.h) | Typed `LatheConfiguration`, capabilities bitmask, baseline profiles. | **Subsystem 4 (Persistence)** | Modern foundation. Simplify to single ROM baseline `getDefaultProfile()`; presets migrate to LittleFS `/presets/profiles.json`. |
| [`SettingsService.cpp`](../src/core/SettingsService.cpp)<br>[`SettingsService.h`](../src/core/SettingsService.h) | 4-tier parameter registry, sparse NVS overrides, in-memory RAM cache. | **Subsystem 4 (Persistence)** | Modern foundation. Add LittleFS profile catalog file I/O and dynamic schema streaming (`SETTINGS.schema=*`). |
| [`MachineModel.cpp`](../src/core/MachineModel.cpp)<br>[`MachineModel.h`](../src/core/MachineModel.h) | Atomic state capture across 4 domains (`K`, `I`, `C`, `P`), delta detection. | **Subsystem 4 (State Model)** | Modern foundation. Authoritative state bridge between Core 1 and Core 0. |
| [`CommandDispatcher.cpp`](../src/core/CommandDispatcher.cpp)<br>[`CommandDispatcher.h`](../src/core/CommandDispatcher.h) | Line protocol command router (`CMD.*`, `SETTINGS.*`). | **Subsystem 5 (Transport)** | Modern foundation. The sole entry point for all commands across all clients (Web, physical keys, serial). |
| [`ClientEndpoint.h`](../src/core_client/ClientEndpoint.h)<br>[`ClientManager.cpp`](../src/core_client/ClientManager.cpp) | Client session manager, transport abstraction, and broadcast distributor. | **Subsystem 5 (Transport)** | Modern foundation. Consolidated in `src/core_client/`; multiplexes connected clients, routes via `NativeClient`. |
| [`TelemetrySerializer.cpp`](../src/core/TelemetrySerializer.cpp)<br>[`TelemetrySerializer.h`](../src/core/TelemetrySerializer.h) | Universal domain state serializer (`K.*`, `I.*`, `C.*`, `P.*`). | **Subsystem 4 (State & Telemetry Model)** | Modern foundation. Universal core state exporter. Serializes atomic `MachineModel` snapshots into canonical wire strings for all clients. Zero web coupling. |

---

## 5. Authoritative Specification Hierarchy & Deep-Dive Documents

To prevent architectural drift and maintain single-topic depth, all detailed implementation rules are partitioned into dedicated standalone specifications:

```
WHATELS_MASTER_ARCHITECTURE.md (This Master Blueprint)
│
├── 1. Motion Core & Hardware Timing (Core 1)
│    └── motion/ (HAL_SPECIFICATION.md, MOTION_ENGINE_SPECIFICATION.md,
│                 CONCURRENCY_AND_TIMING.md, MODE_INTEGRATION_CONTRACT.md)
│
├── 2. Core State & Settings Subsystems (Subsystems 3 & 4)
│    └── state/ (ELS_CORE_STATE_MODEL.md, ELS_PROPERTIES_AND_SETTINGS.md,
│                ELS_PRESET_CATALOG.md, TIER2_STATE_MAPPING.md)
│
├── 3. Operational Mode Engine & Canned Cycles (Subsystem 5, Core 1)
│    └── modes/ (ELS_MODE_ARCHITECTURE.md, README.md, cycle directories)
│
├── 4. Machine Profile & Mechanics
│    └── machine/ (LATHE_MACHINE_PROFILE.md)
│
├── 5. Client Transport, Protocol & Multi-Client Safety
│    └── clients/ (ELS_CLIENT_ARCHITECTURE.md, hmi/, web_ui/)
│
└── 6. Architecture Decision Records & Historical Archive
     ├── adr/ (0001, 0002)
     └── archive/ (reviews/argon/, ELS_DESIGN_NOTEBOOK.md, IMPLEMENTATION_PLAN_V1.md)
```

---

## 6. Rules of Engagement for Human Developers & AI Agents

All contributors and AI pair-programming agents working in this repository MUST adhere to the following rules:

1. **Verify Against the 6 Subsystems Before Modifying Code:**
   * Before adding or editing code, identify which subsystem the file belongs to.
   * If a file bridges multiple subsystems (e.g. `Input.cpp` calling motion math), **refactor the boundary** rather than deepening the coupling.
2. **Never Treat Legacy Globals in `Config.h` as Sacred:**
   * Raw globals (`dupr`, `starts`, `z`, `x`, `isOn`) are legacy migration baggage.
   * When touching a feature, migrate access to `MachineModel` (for state) or `SettingsService` (for configuration).
3. **Strict Adherence to Core 1 Motion Safety:**
   * Never introduce blocking calls (`delay()`, flash writes, mutex locks, JSON serialization) into Core 1 motion loops.
   * Motion timing must remain deterministic under all cutting conditions.
4. **"Input and Display are Just Clients":**
   * Physical keypads, joysticks, MPG handwheels, and Nextion screens must interact with the motion engine exclusively through `CommandDispatcher` (`CMD.*`) and `MachineModel`.
5. **Clean Milestone Verification:**
   * Build the firmware via PlatformIO (`pio run`) after every change.
   * Ensure zero warnings and zero regressions in existing lathe motion.
