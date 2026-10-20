# whatELS Core State Model Specification

> **Status:** Authoritative Specification  
> **Target System:** whatELS Modular Architecture  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `docs/ELS_CORE_STATE_MODEL.md`  
> **Referenced By:** `LATHE_MACHINE_PROFILE.md`, `ELS_MODE_ARCHITECTURE.md`, `ELS_CLIENT_ARCHITECTURE.md`, `ELS_DESIGN_NOTEBOOK.md`

---

## 1. Executive Summary & Architectural Role

The **whatELS Core State Model** (`MachineModel`) is the central, authoritative state container for the entire whatELS electronic leadscrew platform.

It is decoupled from both:
* **Operating Modes (`ELS_MODE_ARCHITECTURE.md`):** Modes are active state machines and kinematic orchestrators that inspect and mutate state.
* **Client Interfaces (`ELS_CLIENT_ARCHITECTURE.md`):** Clients are heterogeneous terminals (Web UI, tablet DROs, pendants, Nextion LCDs) that observe state and dispatch operator intent.

```
                      +-----------------------------+
                      |   LATHE_MACHINE_PROFILE     |
                      | (Geometry, Axes, Hardware)  |
                      +--------------+--------------+
                                     | defines capabilities
                                     v
+--------------------------+  reads /   +--------------------------+
|   ELS_MODE_ARCHITECTURE  |  mutates  |   ELS_CORE_STATE_MODEL   |
| (Modes, Cycles, FSMs)    |<--------->| (MachineModel, Domains,  |
+--------------------------+           |  Snapshots, Invariants)  |
                                       +-------------+------------+
                                                     | observed & commanded by
                                                     v
                                       +--------------------------+
                                       | ELS_CLIENT_ARCHITECTURE  |
                                       | (Generic Pipeline & Wire)|
                                       +-------------+------------+
                                                     | implemented by
                                                     v
                                       +--------------------------+
                                       |      CLIENT_WEB_UI       |
                                       | (Modules, Ledger, UI)    |
                                       +--------------------------+
```

---

## 2. Governing Invariants & Design Principles

### 2.1 The Controller MCU is the Sole Authority
The motion controller MCU holds the single point of truth. Connected displays, browser UIs, and pendants are purely reactive observers. No client maintains private, unsynchronized copies of machine coordinates or kinematic registers.

### 2.2 Canonical Unit Invariant: Pure Deci-Microns (`du`)
To eliminate floating-point precision loss, rounding accumulation, and cross-platform formatting quirks, all linear machine positions, leadscrew pitches, travel envelopes, and tool offsets are stored in **integer deci-microns** ($0.1\,\mu\text{m}$):

$$1\text{ du} = 0.0001\text{ mm} = 0.1\,\mu\text{m}$$
$$10{,}000\text{ du} = 1.0000\text{ mm}$$
$$254{,}000\text{ du} = 1.0000\text{ in}$$

* Integer math guarantees absolute reproducibility across 32-bit ESP32 registers, 64-bit JavaScript engines, and embedded microcontrollers.
* Conversion to floating-point millimeters or inches is strictly a client presentation operation performed at render time.

### 2.3 Cross-Slide Radius Invariant (`xDu`)
Physical leadscrew travel on a lathe cross-slide is intrinsically radial: moving the cross-slide by $1.0\text{ mm}$ moves the tool cutting tip by $1.0\text{ mm}$ relative to the spindle centerline.
* The Core State Model tracks cross-slide position strictly in **canonical radius (`xDu`)**.
* The core motion engine has zero native awareness of diameter mode.
* The choice between displaying Diameter ($\varnothing$) and Radius ($R$) is strictly client-local. When an operator inputs a coordinate in Diameter mode, the client halves the entered diameter before transmitting canonical radius `CMD.axisPosDu=X,<radiusDu>` to the controller.

### 2.4 Lossless Operator Intent Architecture
In conventional CNC systems, user authoring values are immediately flattened into raw kinematic floats (e.g. converting `13 TPI` to $1.953846\text{ mm/rev}$ or `1:3.5 taper` to $0.285714$). When read back by clients, this causes noticeable decimal drift (e.g. `12.9998 TPI` or `1:3.4985`).
* The Core State Model stores the exact **Authoring Intent** (`thFmt`, `thPitch`, `tpMode`, `tpVal`, `coneRatio`) alongside the resolved kinematic value (`dupr`).
* This guarantees $100\%$ zero-drift round-trip fidelity between what the machinist enters and what all displays reflect.

### 2.5 Strict Change-Driven Quiescence (Zero Polling Overhead)
* State capture and telemetry broadcast are **strictly change-driven**.
* When the lathe is stationary and idle, **zero state packets are generated or pushed**, preserving network bandwidth and CPU cycles.
* Telemetry rates during high-speed motion are rate-clamped to a safe ceiling ($\approx 28\text{ Hz}$ / $35\text{ ms}$) to prevent TCP buffer bloat while leaving Core 1 stepper timing unstarved.

### 2.6 Concurrency & Temporal Isolation
On dual-core ESP32 hardware:
* **Core 1** runs high-priority real-time motion control (stepper pulse timers, encoder ISRs).
* **Core 0** runs client communications (FreeRTOS network tasks, WebSocket server, UART, Display).
* To prevent temporal tearing and inconsistent multi-word reads across cores, state is sampled into **immutable value-semantic snapshots** captured at instant $t_0$.

### 2.7 Tiered Properties Taxonomy & Stateless Client Persistence
Properties across the whatELS ecosystem are partitioned into four distinct tiers of responsibility:

1. **Tier 1: Lathe Physical Machine Profile (`LATHE_MACHINE_PROFILE.md`):**
   * *Responsibility:* Physical lathe mechanics and drive geometry (`screwZDu`, `screwXDu`, `motorStepsZ`, `encoderPpr`, backlash, bed limits, swing).
   * *Storage & Mutability:* Commissioning calibration stored in NVS namespace `"machine"`. Defaults loaded from workshop machine presets (e.g. 7x14, 10x22, 14x40).
2. **Tier 2: Controller Infrastructure & Network:**
   * *Responsibility:* ESP32 host hardware and network configuration (WiFi AP/STA mode, SSID, AP password, mDNS hostname, UART baud rate).
   * *Storage & Mutability:* Network settings menu stored in NVS namespace `"sys"` / `"wifi"`. Defaults to standalone factory AP (`whatELS-AP` on `192.168.4.1`, port 81).
3. **Tier 3: Operational Preferences & Telemetry Tuning (`SettingsService`):**
   * *Responsibility:* Workshop behavioral tuning (startup units, jog step table, deadband overrides).
   * *Storage & Mutability:* Operator settings modal stored in NVS namespace `"prefs"`. Defaults dynamically derived from hardware (e.g. deadbands derived from encoder PPR).
4. **Tier 4: Client-Local Presentation (Client Store):**
   * *Responsibility:* Individual display ergonomics (Diameter vs. Radius display preference, DRO view mode, UI themes).
   * *Storage & Mutability:* Client-side local storage or RAM. The controller's motion core never touches these properties.

#### Architectural Hook: Controller-Backed Client Properties (Stateless Clients)
While Tier 4 presentation properties are strictly decoupled from motion math, low-cost "stateless clients" (such as a simple ESP32-C3 hardware pendant with no flash storage, or a fresh browser session on a shop tablet) require a mechanism to recover their display preferences across boots.
* The controller provides a pass-through key-value storage hook in NVS flash (`client.global.*` and `client.<clientId>.*`).
* The controller motion engine treats these keys as opaque byte blobs and never parses them for motion control, acting purely as a persistent broker for connected clients.

### 2.8 In-Cut Execution Mode vs. Client Idle View State (ADR 0002)
* **Idle Machine Invariant:** When physical motion is disengaged (`isOn == false`), the machine is universally **`IDLE`**.
* The core controller has no awareness of which tab, menu, or form the operator is browsing across various connected clients. Client view state is strictly local.
* `KinematicsState.mode` identifies the operational mode currently executing physical motion when `isOn == true`. When idle, command validation is based on machine capability and state invariants, not arbitrary UI tab states.

---

## 3. Four-Domain State Architecture

The Core State Model partitions machine state into four independent, cohesive domains:

```
+-------------------------------------------------------------------------+
|                              MachineModel                               |
+-------------------+-------------------+----------------+----------------+
|    Kinematics     |      Intent       |     Cycle      |   Properties   |
|   (Domain 'K')    |   (Domain 'I')    |  (Domain 'C')  |  (Domain 'P')  |
|     Bit: 0x01     |     Bit: 0x02     |   Bit: 0x04    |   Bit: 0x08    |
+-------------------+-------------------+----------------+----------------+
| zDu, xDu           | thFmt, thPitch    | passes         | measure        |
| zLeftDu, zRightDu | dupr              | opIdx (curr)   | dia, abs       |
| xUpDu, xDownDu    | tpMode, tpVal     | setupIdx       | zDro, xDro     |
| rpm, turns, angle | coneRatio         | starts         | screwZDu       |
| mode, on, estop   | feedStepDu        |                | screwXDu       |
| zDis, xDis        | jogStepDu         |                | motorStepsZ/X  |
| dupr, nextDupr    | feedRev           |                | encoderPpr     |
|                   |                   |                | capabilities   |
|                   |                   |                | presetId       |
+-------------------+-------------------+----------------+----------------+
```

### 3.1 Domain Definitions & Change Frequencies

| Domain | Key | Mask Bit | Update Frequency | Purpose |
| :--- | :---: | :---: | :--- | :--- |
| **Kinematics** | `'K'` | `0x01` | High (20–50 Hz during cut) | Real-time physical machine dynamics: axis positions, soft limits, spindle dynamics, motion state, active mode. |
| **Intent** | `'I'` | `0x02` | Low (On user edit) | Exact operator authoring parameters: thread lead/TPI, taper ratio/angle, manual feed/jog step increments, feed direction. |
| **Cycle** | `'C'` | `0x04` | Medium (Per pass / step) | Automated multi-pass canned cycle progress: total pass count, current pass index, multi-start indexing. |
| **Properties** | `'P'` | `0x08` | Very Low (On boot / setup) | Persistent machine hardware constants: leadscrew pitches, motor steps, encoder PPR, DRO scale sources, deadband thresholds. |

---

## 4. C++ State Model Specification (`MachineModel.h`)

### 4.1 Domain Bitmask Flags

```cpp
enum DomainFlags : uint8_t {
  DOMAIN_NONE       = 0x00,
  DOMAIN_KINEMATICS = 0x01,  // 'K': Dynamic physical axes, limits, spindle, mode, on/off
  DOMAIN_INTENT     = 0x02,  // 'I': Thread format/pitch, taper ratio/angle, steps, feed direction
  DOMAIN_CYCLE      = 0x04,  // 'C': Multi-pass counts, current pass, operation step, multi-starts
  DOMAIN_PROPERTIES = 0x08,  // 'P': Unit system, diameter mode, DRO active, leadscrew pitches
  DOMAIN_ALL        = 0x0F   // All domains combined (0x01 | 0x02 | 0x04 | 0x08)
};
```

### 4.2 Domain Struct Definitions

#### Domain 'K': Kinematics State (Structured Internals)
```cpp
struct AxisLimits {
  std::optional<int32_t> leftOrUpDu;     // std::nullopt when not defined or disabled
  std::optional<int32_t> rightOrDownDu;  // std::nullopt when not defined or disabled

  bool operator==(const AxisLimits& other) const {
    return leftOrUpDu == other.leftOrUpDu && rightOrDownDu == other.rightOrDownDu;
  }
  bool operator!=(const AxisLimits& other) const { return !(*this == other); }
};

struct AxisKinematics {
  int32_t    posDu{0};       // Workpiece coordinate in canonical deci-microns (10,000 du = 1.0 mm)
  AxisLimits limits{};       // Modern Soft Limits (std::optional, Zero Magic Sentinels)
  bool       disabled{false}; // True if stepper driver is powered down / disengaged

  bool operator==(const AxisKinematics& other) const {
    return posDu == other.posDu && limits == other.limits && disabled == other.disabled;
  }
  bool operator!=(const AxisKinematics& other) const { return !(*this == other); }
};

struct SpindleKinematics {
  uint16_t rpm{0};          // Spindle speed in revolutions per minute
  float    turns{0.0f};     // Cumulative continuous spindle revolutions (signed)
  float    angle{0.0f};     // Spindle angular position in degrees [0.0°, 360.0°)

  bool operator==(const SpindleKinematics& other) const {
    return rpm == other.rpm && turns == other.turns && angle == other.angle;
  }
  bool operator!=(const SpindleKinematics& other) const { return !(*this == other); }
};

struct KinematicsState {
  AxisKinematics    z{};        // Longitudinal Z carriage kinematics & stops
  AxisKinematics    x{};        // Transverse X cross-slide kinematics & stops
  SpindleKinematics spindle{};  // Spindle rotary dynamics
  uint8_t           mode{0};    // Active operational ModeId (0: Gear, 1: Turn, 50: Gearbox, etc.)
  bool              isOn{false}; // True if motion / cycle is actively driving axes
  bool              estop{false}; // True if emergency stop or hardware fault is tripped
  int32_t           dupr{0};    // Active leadscrew feedrate in deci-microns per spindle rev
  int32_t           nextDupr{0}; // Staged feedrate for seamless next-pass transition
};
```

#### Domain 'I': Operator Intent State
```cpp
enum ThreadFormat : uint8_t {
  THREAD_FMT_METRIC = 0,  // Millimeters per pitch (e.g. 1.75 mm)
  THREAD_FMT_TPI    = 1,  // Threads Per Inch (e.g. 13 TPI)
  THREAD_FMT_INCH   = 2   // Decimal Inches per pitch (e.g. 0.0500 in)
};

enum TaperMode : uint8_t {
  TAPER_MODE_RATIO  = 0,  // Slope ratio 1:K (e.g. 1:20.0)
  TAPER_MODE_ANGLE  = 1,  // Half-angle in degrees (e.g. 1.79°)
  TAPER_MODE_PRESET = 2   // Standard machine preset (Morse, Brown & Sharpe, etc.)
};

struct ThreadSetting {
  ThreadFormat format;       // Authoring thread format
  float        authorPitch;  // Exact user-entered pitch value (e.g. 13.0 for 13 TPI)
  long         dupr;         // Canonical deci-microns per revolution
};

struct TaperSetting {
  TaperMode modeType;        // Authoring taper format
  float     authorVal;       // Exact user-entered value (e.g. 19.0 for 1:19, 1.79 for 1.79°)
  float     coneRatio;       // Kinematic diametral cone ratio C = deltaDia / deltaZ
  int       presetId;        // Preset identifier (-1 if custom)
};

struct IntentState {
  ThreadSetting thread;        // Active thread authoring state
  TaperSetting  taper;         // Active taper authoring state
  long          feedStepDu;    // Feed step override increment in deci-microns
  long          jogStepDu;     // Manual D-pad jog step increment in deci-microns
  int8_t        direction;     // Cartesian travel direction: -1 (negative coords) or +1 (positive coords)
  int32_t       targetDistDu;  // Target stroke distance in deci-microns (-1 = continuous / soft-limit bound)
};
```

#### Domain 'C': Cycle State
```cpp
struct CycleState {
  int turnPasses;   // Total calculated or overridden passes in multi-pass cycle
  int currentPass;  // Active pass index (1-based: pass 3 of 6)
  int setupIndex;   // Sub-stage within cycle (0: Skim, 1: Roughing, 2: Finish, 3: Retract)
  int starts;       // Multi-start thread index / total starts
};
```

#### Domain 'P': Machine Properties
```cpp
struct MachineProperties {
  uint32_t capabilities;     // LatheCapabilityFlags bitmask (CAP_SPINDLE_*, CAP_MOTOR_*, CAP_DRO_*)
  char  presetId[16];       // Active lathe profile preset slug (e.g. "wm210v_dual")
  int   measure;            // 0: Metric (mm), 1: Imperial (in)
  bool  isDiameterMode;     // Legacy display flag (decoupled to client in modern architecture)
  bool  isAbsoluteMode;     // True for absolute machine coords, false for incremental DRO
  bool  zDroActive;         // True if physical glass/magnetic scale is reading Z
  bool  xDroActive;         // True if physical glass/magnetic scale is reading X
  long  screwZDu;           // Physical Z leadscrew pitch in deci-microns (e.g. 20000 = 2.0mm)
  long  screwXDu;           // Physical X leadscrew pitch in deci-microns
  long  motorStepsZ;        // Microsteps per revolution on Z motor
  long  motorStepsX;        // Microsteps per revolution on X motor
  long  encoderPpr;         // Spindle encoder pulses per revolution (e.g. 1200, 4800 CPR)
  float deadbandAngle;      // Dynamic deadband threshold for spindle angle telemetry
  bool  deadbandAngleAuto;  // True if deadband is auto-derived from encoder PPR
  float deadbandTurns;      // Dynamic deadband threshold for spindle turns telemetry
  bool  deadbandTurnsAuto;  // True if deadband is auto-derived
};
```

### 4.3 Container Class & Atomic Capture API

```cpp
struct MachineModel {
  KinematicsState   kinematics;
  IntentState       intent;
  CycleState        cycle;
  MachineProperties properties;

  // Atomic state factory methods (sample instant t0 snapshot)
  static KinematicsState   captureKinematics();
  static IntentState       captureIntent();
  static CycleState        captureCycle();
  static MachineProperties captureProperties();
  static MachineModel      captureAll();

  // High-speed domain change detection
  static uint8_t detectChangedDomains(const MachineModel& prev, const MachineModel& current);
  static bool    hasChanged(const MachineModel& prev, const MachineModel& current, 
                            uint8_t domainMask = DOMAIN_ALL);
};
```

---

## 5. Temporal Isolation & Concurrency Architecture

### 5.1 FreeRTOS Core Pinning
To prevent memory corruption and priority inversion on dual-core ESP32 microcontrollers:

```
[ ESP32 Core 1: Real-Time Motion Control ]
  - Stepper Timer ISR (TIM0 / RMT): Generates high-frequency step pulses
  - PCNT Interrupts: Samples spindle encoder ticks
  - Mutates live hardware registers (z.pos, x.pos, spindlePos, isOn)
  - NEVER blocks on network mutexes, strings, or JSON formatting

[ ESP32 Core 0: Communications & Supervisory System ]
  - taskDisplay: Evaluates change detection loops
  - taskWiFi / WebSocketsServer: Dispatches telemetry frames to connected clients
  - taskSerial: Reads hardware UART from physical displays
  - Captures immutable `StatusSnapshot` at instant t0
```

### 5.2 Lock-Free Inter-Core Telemetry (`TelemetryMailbox`)
Passing a floating pointer or live reference to the mutable motion model from Core 1 to Core 0 results in temporal tearing (e.g. reading a spindle angle that corresponds to a different carriage step pulse).

Instead of maintaining a parallel queue or copying structs multiple times across tasks, the system implements a **lock-free seqlock mailbox** (`TelemetryMailbox` per `[CONC-01]`):

```cpp
// src/motion/contract/TelemetryMailbox.h
struct MotionTelemetrySnapshot {
  int32_t  zPosSteps;        // Current leadscrew step position
  int32_t  zCoordDu;         // Workpiece coordinate in deci-microns
  int32_t  xPosSteps;        // Current cross-slide step position
  int32_t  xCoordDu;         // Workpiece coordinate in deci-microns
  uint32_t spindleRpm;       // Filtered RPM
  int64_t  spindlePulses;    // Raw continuous spindle encoder pulses
  uint16_t motionMode;       // Active MotionMode enum
  bool     isMoving;         // Active stepping flag
  bool     isFeedHold;       // Pause flag
  uint8_t  activeLimitSlot;  // Limit currently being approached or rested on (0 if none)
  int32_t  distToLimitDu;    // Remaining distance to stop
  uint32_t sequenceNumber;   // Monotonic sequence tag for atomic consistency
};
```

* **Zero Lock Contention:** Core 1 motion tasks publish to `TelemetryMailbox` with memory fences on every tick without ever blocking on FreeRTOS queues or Core 0 memory.
* **Tear-Free Direct Sampling:** Core 0 supervisory tasks (`MachineModel::captureKinematics()`, `WebServerModule`) sample directly from `TelemetryMailbox` via atomic sequence validation. If a write is in progress, it retries lock-free.
* **Single Source of Truth:** Core 0 captures canonical `KinematicsState` directly from this mailbox and active axis configuration without maintaining parallel redundant snapshot queues.

---

## 6. Change Detection & Dynamic Deadbands

To achieve zero network traffic when the lathe is stationary while maintaining fluid responsiveness during cutting, the Core State Model implements high-speed change detection with hardware-derived deadbands:

### 6.1 Dynamic Spindle Deadbands
Digital encoders dither on mechanical boundaries (e.g. vibrating within a single pulse while the lathe motor is idle). Emitting telemetry on every dither would saturate network radios.

Firmware calculates dynamic deadbands derived from the physical encoder PPR:

$$\Delta\theta_{\text{auto}} = \frac{360.0^\circ}{\text{ENCODER\_STEPS\_FLOAT}}$$
$$\Delta\text{turns}_{\text{auto}} = \frac{2.0}{\text{ENCODER\_STEPS\_FLOAT}}$$

* For a 600 PPR quadrature encoder ($2{,}400$ edges), angle dithering within $\pm 0.15^\circ$ is suppressed.
* If an operator enters manual overrides, the custom thresholds are respected; otherwise, auto-derivation guarantees optimal noise immunity.

### 6.2 Change Detection Bitmask Algorithm
The `detectChangedDomains()` method compares `prev` and `current` snapshots in nanoseconds using bitwise operations:

```cpp
uint8_t MachineModel::detectChangedDomains(const MachineModel& prev, const MachineModel& current) {
  uint8_t changed = DOMAIN_NONE;

  // Kinematics: Fast scalar equality checks + float deadbands
  if (prev.kinematics.zDu != current.kinematics.zDu ||
      prev.kinematics.xDu != current.kinematics.xDu ||
      prev.kinematics.rpm != current.kinematics.rpm ||
      fabsf(prev.kinematics.turns - current.kinematics.turns) > turnsDeadband ||
      fabsf(prev.kinematics.angle - current.kinematics.angle) > angleDeadband ||
      prev.kinematics.isOn != current.kinematics.isOn ||
      prev.kinematics.mode != current.kinematics.mode ||
      prev.kinematics.estop != current.kinematics.estop) {
    changed |= DOMAIN_KINEMATICS;
  }

  // Intent: Authored parameter changes
  if (prev.intent.thread.format != current.intent.thread.format ||
      fabsf(prev.intent.thread.authorPitch - current.intent.thread.authorPitch) > 0.0001f ||
      prev.intent.taper.coneRatio != current.intent.taper.coneRatio ||
      prev.intent.feedReverse != current.intent.feedReverse) {
    changed |= DOMAIN_INTENT;
  }

  // Cycle: Pass advancement
  if (prev.cycle.currentPass != current.cycle.currentPass ||
      prev.cycle.turnPasses != current.cycle.turnPasses) {
    changed |= DOMAIN_CYCLE;
  }

  return changed;
}
```

---

## 7. Wire Serialization & Hydration Protocol

### 7.1 Line-Oriented Wire Stream
Domain telemetry streams over WebSocket and Serial as compact, line-oriented key-value packets. Each line begins with the domain key prefix (`K.`, `I.`, `C.`, `P.`):

```text
K.zDu=125400
K.xDu=-45200
K.rpm=620
K.turns=14.25
K.angle=91.4
K.isOn=1
K.mode=0
I.thPitch=1.500
I.feedRev=0
C.opIdx=3
C.passes=6
P.screwZDu=20000
```

### 7.2 Multi-Domain Pull Hydration (`CMD.get=`)
When any client connects, reconnects, or shifts pages, it requests an atomic point-in-time snapshot of the required domains:

```text
CMD.get=K,I,C,P\n
```

The controller captures `MachineModel::captureAll()` at instant $t_0$ and streams the requested domains sequentially in a single transaction, completely eliminating torn-frame read hazards.

---

## 8. Client-Side Reactive Model (`Alpine.store('machine')`)

In the browser and Web UI, [`whatels-state.js`](../clients/web_ui/whatels-state.js) implements an isomorphic reactive representation of `MachineModel`:

```javascript
Alpine.store('machine', {
  // Domain 'K': Kinematics
  zDu: 0,
  xDu: 0,
  rpm: 0,
  turns: 0.0,
  angle: 0.0,
  mode: 0,
  isOn: false,
  estop: false,
  dupr: 0,

  // Domain 'I': Intent
  thFmt: 0,
  thPitch: 1.5,
  tpMode: 0,
  tpVal: 20.0,
  coneRatio: 0.05,
  feedRev: false,

  // Domain 'C': Cycle
  passes: 1,
  opIdx: 0,

  // Domain 'P': Properties
  screwZDu: 20000,
  motorStepsZ: 800,
  encoderPpr: 1200,

  // Client-Local Presentation Settings (Zero Wire Traffic)
  measure: 0,  // 0: Metric (mm), 1: Imperial (in)
  dia: true,   // true: Diameter (Ø), false: Radius (R)

  // Pure Reactive Getters (DOM Bindings)
  get zDisplay() {
    return Formatters.formatAxisDu(this.zDu, this.measure === 1 ? 'in' : 'mm', false);
  },
  get xDisplay() {
    return Formatters.formatAxisDu(this.xDu, this.measure === 1 ? 'in' : 'mm', this.dia);
  }
});
```

* **Client Presentation Freedom:** Toggling `dia` or `measure` updates the client store locally. **No commands are transmitted across the wire**, and no other connected client is disturbed.

---

## 9. Summary & Cross-Index

The **Core State Model** provides the unshakeable bedrock of the whatELS ecosystem:
1. **Deterministic Kinematics:** All physical motion operates in integer deci-microns and canonical radius.
2. **Zero-Drift Intent:** Authoring units are preserved without floating-point conversion degradation.
3. **Decoupled Concurrency:** Value-semantic snapshots prevent temporal tearing across dual ESP32 cores.
4. **Bandwidth Efficiency:** Strict change-driven dispatch and dynamic encoder deadbands prevent network radio saturation.

### Cross-Index:
* **Hardware Geometry & Capabilities:** [`LATHE_MACHINE_PROFILE.md`](./LATHE_MACHINE_PROFILE.md)
* **Mode Lifecycle & Orchestration:** [`ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md)
* **Client Architecture & Protocols:** [`ELS_CLIENT_ARCHITECTURE.md`](./ELS_CLIENT_ARCHITECTURE.md)
* **UI Design Decisions & Ergonomics:** [`ELS_DESIGN_NOTEBOOK.md`](./ELS_DESIGN_NOTEBOOK.md)
