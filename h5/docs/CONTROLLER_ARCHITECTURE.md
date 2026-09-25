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

## 3. Composable Client Endpoint Pipeline

To allow any hardware transport to communicate with the core controller, communication is modeled as a 3-part pipeline ([`ClientEndpoint.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/ClientEndpoint.h)):

1. **`ITransport`**: Pure communication link (`send()`, `onReceive()`, `isConnected()`). Implementations: `WebSocketTransport`, `SerialTransport`, `SharedMemoryTransport`.
2. **`ITelemetrySerializer`**: Encodes domain bitmasks into wire packets. Implemented by [`RichClientAdapter`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/RichClientAdapter.h) (emitting `K.*`, `I.*`, `C.*`, `P.*`).
3. **`ICommandTranslator`**: Parses wire strings into atomic core method calls. Implemented by [`CommandDispatcher`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/CommandDispatcher.cpp).

This architecture decouples the network stack entirely from motion code:
- WebSockets can fail or reconnect without interrupting stepper pulse generation.
- A local LVGL display on the ESP32-S3 communicates via direct C++ function calls through the exact same `MachineModel` without serializing strings.

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
