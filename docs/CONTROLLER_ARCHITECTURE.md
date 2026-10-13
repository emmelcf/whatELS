# NanoELS H5 Controller Architecture & Wire Protocol

## 1. Architectural Philosophy

The NanoELS H5 firmware is architected around a strict separation of concerns to support diverse clients (Web UI over WebSocket, local LVGL on ESP32-S3, hardware MPG handwheels, and future transports like CAN, USB-CDC, or BLE) without duplicating business logic, introducing race conditions, or risking tool crashes.

```
┌────────────────────────────────────────────────────────────────────────┐
│                         PHYSICAL HARDWARE LAYER                        │
│  (ESP32-S3 Stepper Timers, Spindle Quadrature Encoder, Endstops, E-Stop) │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Stepper Microsteps / Encoder Pulses
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                        KINEMATICS & MOTION ENGINE                      │
│  (Bresenham DDA Synchronizer, Dual-Axis Taper Co-Stepping, Limits)      │
│  • Operates in physical invariants: Deci-Microns (du) & Microsteps     │
│  • Backlash compensation, acceleration profiles, limit bounding        │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Authoritative Real-Time State
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     CANONICAL MACHINE MODEL (C++)                      │
│                 MachineModel.h / MachineModel.cpp                      │
│  • Domain Kinematics (K.*): Real-time coordinates, RPM, turns, state   │
│  • Domain Intent     (I.*): Operator setpoints (pitch, taper, steps)   │
│  • Domain Cycle      (C.*): Multi-pass state machine progress          │
│  • Domain Properties (P.*): Leadscrew pitches, steps/rev, encoder PPR  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Domain Bitmasks & Atomic Commands
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                   CLIENT ENDPOINT & PIPELINE LAYER                     │
│                        ClientEndpoint.h                                │
│  ┌───────────────────────┬──────────────────────┬────────────────────┐ │
│  │ ITelemetrySerializer  │  ICommandTranslator  │     ITransport     │ │
│  │ (RichClientAdapter)   │ (CommandDispatcher)  │ (WebSocket/Serial) │ │
│  └───────────────────────┴──────────────────────┴────────────────────┘ │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Non-Blocking Wire Streams
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                            EXTERNAL CLIENTS                            │
│    • Web UI (Alpine.js v3 + HTML5 / WebSocket)                         │
│    • Local Touchscreen (LVGL 9 / Shared Memory or IPC)                 │
│    • Wireless MPG Pendant (ESP-NOW / Bluetooth)                        │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. The 4-Domain Machine Model

The controller's authoritative state is partitioned into 4 decoupled domains (`MachineModel.h`):

### 2.1 Kinematics Domain (`DOMAIN_KINEMATICS` / `K.*`)
Represents the real-time physical state of the machine. All positions are strictly measured in **deci-microns (`du`)** where $1\,\text{du} = 0.1\,\mu\text{m} = 0.0001\,\text{mm} = \frac{1}{254,000}\,\text{inch}$.

| Key | Type | Description |
| :--- | :--- | :--- |
| `K.zDu` | `int32_t` | Z carriage coordinate in deci-microns |
| `K.xDu` | `int32_t` | X cross-slide coordinate in deci-microns (radial travel) |
| `K.yDu` | `int32_t` | Y vertical / auxiliary axis coordinate in deci-microns |
| `K.zLeftDu` | `int32_t` | Z Chuck limit coordinate (or `2147483647` if unset) |
| `K.zRightDu`| `int32_t` | Z Tailstock limit coordinate (or `-2147483648` if unset) |
| `K.xUpDu`   | `int32_t` | X Infeed / Center limit coordinate (or `2147483647` if unset) |
| `K.xDownDu` | `int32_t` | X Retract limit coordinate (or `-2147483648` if unset) |
| `K.rpm`     | `uint16_t`| Spindle speed in RPM |
| `K.turns`   | `int32_t` | Cumulative spindle revolutions (signed) |
| `K.angle`   | `uint16_t`| Spindle angular orientation ($0..3599$ in tenths of a degree) |
| `K.mode`    | `uint8_t` | Active operating mode ID ($0..10$) |
| `K.on`      | `uint8_t` | Motion active flag ($1 = \text{RUNNING}, 0 = \text{STOPPED}$) |
| `K.estop`   | `uint8_t` | Emergency stop active flag |
| `K.dupr`    | `int32_t` | Active feedrate in deci-microns per spindle revolution |

### 2.2 Intent Domain (`DOMAIN_INTENT` / `I.*`)
Stores the operator's exact authored setpoints. By storing authoring intent separately from the calculated kinematic `dupr`, the system prevents round-trip floating-point accumulation drift.

| Key | Type | Description |
| :--- | :--- | :--- |
| `I.thFmt` | `uint8_t` | Thread format: `0` (Metric mm), `1` (Imperial TPI), `2` (Imperial Inch) |
| `I.thPitch` | `float` | Exact operator-typed pitch (e.g. `1.500` or `20.00` for 20 TPI) |
| `I.tpMode` | `uint8_t` | Taper entry mode: `0` (Ratio 1:K), `1` (Angle in degrees), `2` (Preset) |
| `I.tpVal` | `float` | Exact operator-typed taper value (e.g. `16.0` for 1:16 or `1.43` for 1.43°) |
| `I.coneRatio`| `float` | Calculated kinematic cross-slide to carriage ratio |
| `I.tpPreset` | `int16_t` | Selected taper preset ID (`-1` = Custom, `0` = MT0, `1` = MT1, `2` = MT2, etc.) |
| `I.feedStepDu` | `int32_t` | Feedrate adjustment increment in deci-microns |
| `I.jogStepDu`  | `int32_t` | Manual jog step increment in deci-microns |
| `I.feedRev`    | `uint8_t` | Pitch reverse flag ($1 = \text{reversed}, 0 = \text{normal}$) |

### 2.3 Cycle Domain (`DOMAIN_CYCLE` / `C.*`)
Tracks multi-pass automated CNC cycles (Turning, Facing, Threading, Cutoff).

| Key | Type | Description |
| :--- | :--- | :--- |
| `C.passes` | `uint16_t` | Total calculated pass count |
| `C.opIdx`  | `uint16_t` | Current active pass index ($0..\text{passes}-1$) |
| `C.setupIdx` | `uint16_t` | Sub-stage index (e.g. skim verification pass) |
| `C.starts` | `uint8_t`  | Multi-start thread count ($1..8$) |

### 2.4 Properties Domain (`DOMAIN_PROPERTIES` / `P.*`)
Static and machine-level hardware properties.

| Key | Type | Description |
| :--- | :--- | :--- |
| `P.screwZDu` | `int32_t` | Z leadscrew pitch in deci-microns (e.g. `20000` = 2.0 mm) |
| `P.screwXDu` | `int32_t` | X leadscrew pitch in deci-microns (e.g. `20000` = 2.0 mm) |
| `P.motorStepsZ`| `uint16_t`| Motor microsteps per revolution for Z axis |
| `P.motorStepsX`| `uint16_t`| Motor microsteps per revolution for X axis |
| `P.encoderPpr` | `uint16_t`| Spindle encoder quadrature pulses per revolution |

---

## 3. Composable Client Endpoint Pipeline & Multi-Client Architecture

To allow any hardware transport and multiple concurrent displays to communicate with the core controller, communication is modeled as a composable pipeline ([`ClientEndpoint.h`](../src/web/ClientEndpoint.h)):

$$\text{ClientEndpoint} = \text{Transport} + \text{Serializer} + \text{Translator}$$

1. **`ITransport`**: Pure I/O wire interface (`sendText()`, `sendBytes()`, `isConnected()`). Implementations: `WebSocketClientTransport`, `UartTransport`, `SharedMemoryTransport`.
2. **`ITelemetrySerializer`**: Transforms internal `MachineModel` snapshots into wire protocols. Implementations: `RichClientAdapter` (Canonical `K.*`, `I.*`, `C.*`, `P.*`), `GrblSerializer` (`<Idle|...|FS:...>`), `DroSerializer` (minimal coordinates).
3. **`ICommandTranslator`**: Parses inbound wire bytes into atomic controller method calls. Implemented by `CommandDispatcher` (Canonical `CMD.*`) or protocol-specific parsers.

### 3.1 Smart Clients vs. Dumb Displays (Stateful Presenters)
The client model accommodates both intelligent and protocol-constrained clients:

* **Smart Clients (Web UI / Alpine Reactive Store)**:
  - The client runs its own reactive state engine on its local CPU (browser).
  - The controller's serializer is thin and stateless: it emits raw canonical data (`K.*`, `I.*`). The client formats units, diameters, and DOM elements locally.
* **Dumb Displays (Physical Nextion LCD / Serial Pendants)**:
  - Nextion hardware has no domain logic; it only receives widget text strings (`t0.txt="-18.42"`) and emits component touch IDs (Page 1, Component 4).
  - Its `ClientEndpoint` acts as a **Stateful Presenter / Transformer**:
    1. **Diff Cache**: Stores previously rendered strings, active page ID, and widget colors to eliminate redundant UART transmissions and screen flicker.
    2. **Component Mapping**: Maps raw touch events into canonical `CommandDispatcher` actions.
    3. **Quirk Isolation**: Nextion-specific hashing and delta-rendering reside strictly inside the Nextion adapter, keeping core firmware clean.

### 3.2 Heterogeneous Channels & URL-Path Routing
When multiple clients connect via WebSockets, they may require different data formats. Routing is handled via the HTTP upgrade URL path (Option B):

| URL Path | Channel Type | Serializer Used | Target Client |
| :--- | :--- | :--- | :--- |
| `ws://<ip>:81/` or `/canonical` | `CHANNEL_CANONICAL` | `RichClientAdapter` (`K.*`, `I.*`, `C.*`, `P.*`) | Rich Web UI / Multi-Axis HMI |
| `ws://<ip>:81/grbl` | `CHANNEL_GRBL` | `GrblSerializer` (`<Idle\|WPos:...\|FS:...>`) | CNC Senders (UGS, CNCjs, OpenBuilds) |
| `ws://<ip>:81/dro` | `CHANNEL_DRO` | `DroSerializer` (`K.zDu`, `K.xDu`, `K.rpm`) | Minimal Wireless DRO Readouts |

- **Zero Runtime Overhead**: The channel is assigned **once** during the HTTP upgrade handshake. Runtime packets require no subscription parsing.
- **Active Channel Bitmask (`activeChannelMask`)**: The controller tracks which channels have active listeners. Formats with zero connected listeners are **never serialized**, burning zero CPU cycles.

### 3.3 Change-Driven Telemetry (Zero Minimum Rate, Clamped Ceiling)
Status pushes are strictly event-driven:
- **Zero Minimum Rate (Quiescence)**: If the lathe is stationary and idle with no buttons pressed, **zero packets** are sent. Radio, network buffers, and CPU remain completely silent.
- **Max-Rate Ceiling**: High-frequency events (e.g. 2,500+ RPM spindle motion or rapid feeds) are clamped to a clean ceiling (e.g. max ~28 Hz / 35 ms) to guarantee that FreeRTOS stepper pulse generation (`taskMoveZ`, `taskMoveX`) is never starved.

### 3.4 Unicast vs. Channel Push Separation
1. **Unicast Request-Response (1-to-1)**:
   - Queries (`?get=...`, `?sysinfo`), Settings reads (`SETTINGS.get=...`), and command confirmations (`OK`, error messages) are sent **unicast** (`sendTXT(clientId)`) exclusively to the requesting socket.
   - Prevents network amplification ($O(N^2)$ traffic) when multiple displays are active.
   - New connections receive an immediate unicast hydration frame without disturbing existing screens.
2. **Channel Broadcast (1-to-Many)**:
   - When machine state changes, telemetry is serialized once per active channel and pushed to matching sockets.

### 3.5 Settings Read/Write Transaction Model
Configuration settings follow a strict transaction flow:
1. **Read**: `SETTINGS.get=<key>` $\rightarrow$ Controller replies unicast: `SETTING.<key>=<val>`.
2. **Write**: `SETTINGS.set=<key>,<val>` $\rightarrow$ Controller validates bounds, persists to NVS `Preferences`, updates `MachineModel::properties`, and replies unicast: `OK:SETTINGS.<key>=<val>`.
3. **Synchronization**: Controller marks `DOMAIN_PROPERTIES` as modified, triggering an automatic push of `P.*` to all canonical channel subscribers so all screens update in real time.

### 3.6 Multi-Task Concurrency & Mutex Protection (`wsMutex`)
The ESP32-S3 runs a dual-core FreeRTOS architecture where hard real-time stepper motors and asynchronous network/display I/O must coexist without race conditions or latency spikes:

- **Core 1 (Real-Time Motion Priority)**:
  - `taskMoveZ`, `taskMoveX`: DDA stepper pulse timing and hardware encoder quadrature ISRs run at maximum FreeRTOS priority.
  - Motion loops **never** acquire network locks or wait on WiFi I/O, guaranteeing zero step pulse jitter regardless of network load.
- **Core 0 (Network & Display Pipeline)**:
  - `taskWiFi`: Runs `webSocket.loop()`, WiFi connection management, and HTTP REST handlers.
  - `taskDisplay`: Periodically runs `publishMachineStatus()` to push state updates to WebSocket and Nextion displays.
- **Recursive FreeRTOS Mutex (`wsMutex`)**:
  - `webSocket` packet transmission (`sendTXT`), client session enumeration, and `loop()` are guarded by a recursive mutex (`xSemaphoreCreateRecursiveMutex`).
  - A recursive mutex is mandatory because `webSocket.loop()` inside `taskWiFi` invokes `handleWebSocketEvent()`, which can in turn invoke `handleWebUiEventCommand()` and synchronously call `sendTXT(clientId, ...)` without self-deadlock.
  - When `publishMachineStatus()` is called from `taskDisplay`, it acquires `wsMutex` with a non-blocking timeout, serializing telemetry and pushing frames cleanly without memory corruption or socket race conditions.

### 3.7 Dedicated Settings Service & Parameter Ledger
Configuration reads and writes are managed by [`SettingsService.h`](../src/core/SettingsService.h) and [`SettingsService.cpp`](../src/core/SettingsService.cpp), isolating NVS persistence and validation from command routing:

| Setting Key | Type | Min | Max | NVS Key | Description |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `screwZDu` | `int32_t` | 1,000 | 500,000 | `"screwZ"` | Z leadscrew pitch in deci-microns (e.g. `20000` = 2.0mm) |
| `screwXDu` | `int32_t` | 1,000 | 500,000 | `"screwX"` | X leadscrew pitch in deci-microns |
| `screwYDu` | `int32_t` | 1,000 | 500,000 | `"screwY"` | Y auxiliary leadscrew pitch in deci-microns |
| `motorStepsZ` | `uint16_t` | 200 | 25,600 | `"stepsZ"` | Microsteps per revolution for Z stepper |
| `motorStepsX` | `uint16_t` | 200 | 25,600 | `"stepsX"` | Microsteps per revolution for X stepper |
| `motorStepsY` | `uint16_t` | 200 | 25,600 | `"stepsY"` | Microsteps per revolution for Y stepper |
| `encoderPpr` | `uint16_t` | 100 | 40,000 | `"encPpr"` | Spindle encoder quadrature pulses per rev |
| `measure` | `uint8_t` | 0 | 1 | `"measure"` | Display units: `0` = Metric (mm), `1` = Imperial (inch) |
| `dia` | `uint8_t` | 0 | 1 | `"dia"` | X DRO mode: `0` = Radius, `1` = Diameter |
| `abs` | `uint8_t` | 0 | 1 | `"abs"` | Coordinate mode: `0` = Incremental, `1` = Absolute |
| `zDro` | `uint8_t` | 0 | 1 | `"zDro"` | Z scale source: `0` = Motor steps, `1` = External glass DRO |
| `xDro` | `uint8_t` | 0 | 1 | `"xDro"` | X scale source: `0` = Motor steps, `1` = External glass DRO |
| `feedStepDu` | `int32_t` | 1 | 100,000 | `"feedStep"` | Feedrate nudge increment in deci-microns |
| `jogStepDu` | `int32_t` | 1 | 100,000 | `"jogStep"` | Manual jog step increment in deci-microns |
| `backlashZDu` | `int32_t` | 0 | 50,000 | `"backlashZ"`| Z axis leadscrew backlash compensation in `du` |
| `backlashXDu` | `int32_t` | 0 | 50,000 | `"backlashX"`| X axis leadscrew backlash compensation in `du` |

- Out-of-range values are rejected immediately with `ERR:INVALID_RANGE`.
- Unrecognized keys return `ERR:UNKNOWN_KEY`.
- Valid writes automatically mutate `MachineModel::properties`, save to NVS flash via `Preferences`, and mark `DOMAIN_PROPERTIES` as changed so all connected displays receive updated `P.*` packets.

### 3.8 Industrial Shop-Floor EMI & Transient Network Hardening
Machine shop environments feature high electrical interference (VFD spindle drives, contactor arcing, servo switching), leading to occasional 2.4 GHz packet drops or beacon timeouts:

1. **Transient Disconnect Debounce (2.5 seconds)**:
   - When `WiFi.status() != WL_CONNECTED`, the controller does not immediately tear down connections or trigger a reconnection storm.
   - It debounces for 2,500 ms. If the AP beacon recovers during this window, normal streaming resumes without dropping TCP sockets or disrupting operator UI views.
2. **Exponential Backoff Reconnect Loop (3s $\rightarrow$ 30s)**:
   - If AP connectivity is genuinely lost, the controller initiates a background reconnection sequence.
   - Initial retry occurs after 3 seconds, doubling on subsequent failures (6s $\rightarrow$ 12s $\rightarrow$ 24s $\rightarrow$ 30s maximum cap) to prevent radio bus saturation and excessive CPU consumption.
3. **Physical Display Status Integration (Nextion LCD)**:
   - During reconnection, the controller publishes countdown telemetry to physical displays (`t3.txt="WiFi retry (Xs)"`).
   - Hardware operators at the lathe are immediately informed that remote web clients are in reconnect state.
   - Once reconnected, `t3.txt` is updated with `"See <ip>"`, and remote Web UIs reconnect and receive a fresh hydration frame automatically.

---

## 4. Semantic Intent Wire Protocol

### 4.1 State Query
- `?get=ALL` : Requests full state dump across all 4 domains (`K`, `I`, `C`, `P`).
- `?get=K,I` : Requests specific domains (e.g. Kinematics and Intent only).
- `?sysinfo` : Requests chip model, flash size, and heap diagnostics.

### 4.2 Semantic Motion Commands
The controller core enforces safety and machine geometry. Clients transmit **operator intent**, not signed directional guesses:

| Command | Semantics | Description |
| :--- | :--- | :--- |
| `CMD.jogHold=X,INFEED` | X into cut | Drives cross-slide toward workpiece centerline (handles front vs rear toolpost) |
| `CMD.jogHold=X,RETRACT` | X out of cut | Drives cross-slide away from workpiece centerline |
| `CMD.jogHold=Z,CHUCK` | Z to headstock | Drives carriage toward spindle chuck |
| `CMD.jogHold=Z,TAIL` | Z to tailstock | Drives carriage toward tailstock |
| `CMD.jogRelease=X` | Release hold | Stops cross-slide continuous jog |
| `CMD.jogRelease=Z` | Release hold | Stops carriage continuous jog |
| `CMD.jogStep=X,INFEED` | Discrete step | Pulses cross-slide by active `jogStepDu` into cut |
| `CMD.jogStep=Z,CHUCK` | Discrete step | Pulses carriage by active `jogStepDu` toward chuck |

### 4.3 Setpoints & Parameters
- `CMD.feedDupr=<du>` : Directly sets kinematic feedrate in deci-microns per revolution.
- `CMD.feedMm=<mm>` : Sets metric pitch (e.g. `CMD.feedMm=1.5`).
- `CMD.feedTpi=<tpi>` : Sets imperial thread (e.g. `CMD.feedTpi=20.0`).
- `CMD.feedRev` : Reverses feed direction without recalculating pitch.
- `CMD.taperRatio=<ratioK>` : Sets taper ratio $1:K$ (e.g. `CMD.taperRatio=16.0`).
- `CMD.taperAngle=<deg>` : Sets taper half-angle (e.g. `CMD.taperAngle=1.43`).
- `CMD.taperPreset=<presetId>,<deg>,<ratio>` : Sets authoritative taper preset ID and parameters (e.g. `CMD.taperPreset=2,1.4306,20.0196` for MT2).
- `CMD.axisZero=<axis>` : Zeroes axis coordinate register (`Z`, `X`, or `Y`).
- `CMD.axisPosDu=<axis>,<du>` : Presets axis coordinate register to an exact deci-micron position.
- `CMD.axisLimitDu=<axis>,<L|R>,<du>` : Sets soft limit boundary.
- `CMD.axisClearLimit=<axis>,<L|R>` : Clears soft limit boundary.
- `CMD.mode=<modeId>` : Selects controller operating mode ($0 = \text{GEAR}, 3 = \text{CONE}, 4 = \text{TURN}, 7 = \text{THREAD}$, etc.).
- `CMD.on=<0|1>` : Binary cycle start (`1`) or stop (`0`).

---

## 5. Decoupling of Display Units & Kinematics

### 5.1 Presentation is a Client-Local Concern
The controller core does **not** maintain a display unit state for connected clients.
- The physical machine coordinates are always stored in **canonical deci-microns (`du`)**.
- A client display formats `du` into millimeters (`du / 10000.0`) or inches (`du / 254000.0`) at render time.
- A client display formats the X axis in **Diameter (`du * 2`)** or **Radius (`du`)** mode locally.
- Multiple clients (e.g., an apron tablet, an LVGL touchscreen, and a remote monitor) can simultaneously view the machine in different units without fighting over a global controller register.

### 5.2 Threading & Taper Intent are Machine-Level Invariants
While DRO coordinates are purely a view concern, **machining specifications** (such as cutting an $M10 \times 1.5$ metric thread vs a $1/4"-20$ TPI thread) are physical properties of the part being cut.
- Intent parameters (`I.thFmt`, `I.thPitch`, `I.tpVal`) are authoritative in firmware.
- The controller core calculates the exact microstep gear ratio from these invariants.
- All connected clients display the authoritative intent regardless of their local DRO coordinate unit setting.

### 5.3 Decoupled Display Presentation Layer
The client UI incorporates pure render-time formatters (`nanoels-formatters.js`) that compute ergonomic companion views from canonical invariants:
- **Feedrates**: Derived from canonical `dupr` (deci-microns/rev) $\rightarrow$ `mm/rev` or `in/rev`. Time feeds derive from mm/min $\rightarrow$ `in/min`.
- **Cutting Surface Speed**: Derived at 10 Hz from spindle RPM and live $X$ DRO workpiece diameter $\rightarrow$ `SMM` (m/min) in metric, `SFM` (ft/min) in imperial.
- **Thread Pitch Companions**: Displaying primary specification alongside dual companion (e.g. `20 TPI (1.270 mm)` or `1.500 mm (16.93 TPI)`).
- **Taper Multi-Format Companions**: Converting canonical slope $C = \Delta \text{Dia}/\Delta Z$ simultaneously to Ratio ($1:K$), Angle ($\theta, 2\theta$), Taper Per Foot (`in/ft`), and Metric Taper (`mm/m`).
- **Ergonomic Jog Steps**: Unit toggle immediately snaps to the nearest native preset (`0.010 mm` $\leftrightarrow$ `0.0010 in`) rather than fractional conversions.

---

## 6. Flash Partition Topology & User Storage Invariants

To guarantee that firmware upgrades never destroy user calibrations, custom machine profiles, or uploaded part programs (lathecode / G-code), the ESP32 storage architecture strictly separates system code from operator runtime data:

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                       NANOELS FLASH PARTITION ROLES                         │
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
│                                │ • (Optional Dev Mode: `/www/` live assets) │
└────────────────────────────────┴────────────────────────────────────────────┘
```

### 6.1 Single-File OTA Updates & Zero Version Desynchronization
1. **Single Binary Deployment:** All firmware logic, the complete responsive Web UI (`clients/web_ui/`), and built-in factory lathe templates are compiled directly into `firmware.bin`.
2. **Atomic Upgrades:** Updating the controller via USB or the Web OTA endpoint (`/update`) flashes only the `app0`/`app1` partition. The browser UI, C++ kinematics, and WebSocket wire protocols update together atomically in a single reboot, completely preventing version mismatches between frontend code and backend APIs.
3. **LittleFS Invariant:** The LittleFS filesystem partition is **never formatted, wiped, or overwritten during firmware updates**. Custom user presets and uploaded machining files remain 100% persistent across firmware releases.

### 6.2 Developer Live-Asset Override Fallback
For rapid iteration during web development without recompiling C++ firmware:
* When an HTTP request arrives for `/index.html`, the embedded server first checks if `/www/index.html` exists on the LittleFS partition.
* If present, the controller serves the loose file from LittleFS (allowing hot-reloading web edits).
* If absent (standard production state), the controller serves the gzipped bundle directly from Flash ROM via `send_P` with `Content-Encoding: gzip`.


