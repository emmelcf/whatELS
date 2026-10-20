# whatELS Canonical Command & Protocol Wire Reference

> **Status:** Authoritative Wire Specification  
> **Target Subsystem:** Subsystem 5 (Command Dispatcher & Transport) & Subsystem 6 (Client Adapters)  
> **Master Architecture:** [`docs/clients/ELS_CLIENT_ARCHITECTURE.md`](../clients/ELS_CLIENT_ARCHITECTURE.md)  
> **Implementation Reference:** [`src/core/CommandDispatcher.cpp`](../../src/core/CommandDispatcher.cpp)  
> **Date:** October 2026  

---

## 1. Overview & Syntax Rules

The **whatELS Canonical Command Surface** is the single authoritative wire interface for controlling the whatELS motion controller. All transports (WebSocket, physical UART Serial, USB CDC) and all client endpoints (`NativeClient`, `NextionAdapter`, HIL test harness) converge on this command dictionary via [`CommandDispatcher::dispatch()`](../../src/core/CommandDispatcher.h).

### Command Grammar
Every canonical message is an ASCII string terminated by a newline (`\n` or `\r\n`):

$$\texttt{<NAMESPACE>.<action>[=<arguments>]}\backslash\text{n}$$

* **Namespaces:** `CMD`, `SETTINGS`, `PRESET`, `MODE`, `CLIENT`, and queries (`?get`, `?sysinfo`).
* **Argument Formatting:** Arguments may be formatted as comma-separated key-value pairs (e.g. `CMD.startFeed=axis:Z,feed:0.15,dir:+`) or comma-separated positional tokens (e.g. `CMD.jogStep=Z,100`).
* **Case Sensitivity:** Command names are case-insensitive (`CMD.stop` == `cmd.STOP`). Axis names (`Z`, `X`) and units (`mm`, `tpi`, `du`) accept uppercase or lowercase.

### Universal Return Grammar
Every command processed by the controller returns an ASCII response:

| Response Header | Meaning | Wire Example |
| :--- | :--- | :--- |
| `OK:CMD.<name>` | Command accepted and executed successfully. | `OK:CMD.stop\n` |
| `OK:CMD.<name>:<data>` | Command succeeded with scalar or string payload. | `OK:CMD.jogStep:Z,100\n` |
| `ERR:CMD.<name>:<reason>` | Command failed validation, was locked by motion, or hardware faulted. | `ERR:CMD.startFeed:Missing arguments\n` |
| `CMD.error=unknown_command` | Unrecognized command syntax. | `CMD.error=unknown_command\n` |
| `SYSINFO.<key>=<val>` | Multi-line structured query response. | `SYSINFO.version=1.0.4-modular\n...` |
| `K.<field>=<val>...` | Universal telemetry hydration or snapshot push. | `K.zDu=12400\|K.rpm=850...\n` |

---

## 2. Motion & Kinematic Control Namespace (`CMD.*`)

Commands that engage stepper motors, set feed directions, configure cycle parameters, or manage coordinates.

### 2.1 Motion Lifecycle & Cycle Execution

#### `CMD.startFeed`
Engages leadscrew motion according to the specified parameters. If called while idle, transitions state to `RUNNING`.
* **Arguments:** 
  * `axis`: Target axis (`Z`, `X`, or `ZX` / `TAPER`). Default: `Z`.
  * `feed`: Feed rate. If floating-point (contains `.`), parsed as mm/rev; otherwise parsed as deci-microns per rev (`dupr`).
  * `dir`: Direction (`+`, `-`, `LEFT`, `RIGHT`, `FWD`, `REV`).
  * `sync`: Spindle synchronization (`1` or `sync` for ELS electronic gearing; `0` or `async` for continuous IPM feed).
  * `dist`: (Optional) Target travel distance in deci-microns (`du`). `-1` for continuous feed.
* **Examples:**
  * `CMD.startFeed=axis:Z,feed:0.15,dir:+,sync:1`
  * `CMD.startFeed=Z,0.20,+`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.startFeed` or `ERR:CMD.startFeed:<reason>`

#### `CMD.stop`
Immediately halts all axis stepping, cancels any active jog or cutting cycle, and returns the machine state to `IDLE`.
* **Arguments:** None.
* **Example:** `CMD.stop`
* **Required Permission:** `PERM_READ_TELEMETRY` *(Emergency Stop authority is always granted to any connected client).*
* **Returns:** `OK:CMD.stop`

#### `CMD.mode`
Switches the active kinematic cutting mode.
* **Arguments:** Mode ID or mode name (`gearbox`, `turning`, `facing`, `threading`, `cutoff`, `slotting`, `sphere`).
* **Example:** `CMD.mode=50` or `CMD.mode=gearbox`
* **Safety Lockout:** Rejected if motion is currently active (`isOn == true`).
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.mode:<modeId>`

---

### 2.2 Manual Jogging & Positioning

#### `CMD.jogStep`
Executes an incremental single-step jog on the specified axis.
* **Arguments:** `<axis>,<dist_du>` (Distance in deci-microns, where $10,000 \text{ du} = 1.0\text{ mm}$).
* **Examples:**
  * `CMD.jogStep=Z,100` *(Jog Z positive 10 microns / 0.01 mm)*
  * `CMD.jogStep=X,-500` *(Jog X negative 50 microns / 0.05 mm)*
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogStep:<axis>,<dist>`

#### `CMD.jogHold`
Initiates continuous jogging on an axis. The axis continues stepping until `CMD.jogRelease` or `CMD.stop` is received.
* **Arguments:** `<axis>,<direction>` (`+` / `-` / `LEFT` / `RIGHT` / `UP` / `DOWN`).
* **Example:** `CMD.jogHold=Z,+`
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogHold:<axis>`

#### `CMD.jogRelease`
Stops an active continuous jog initiated by `CMD.jogHold`.
* **Arguments:** `<axis>` (Optional).
* **Example:** `CMD.jogRelease=Z`
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogRelease`

#### `CMD.jogStepDu`
Configures the default step increment used for physical panel jog buttons.
* **Arguments:** `<dist_du>`
* **Example:** `CMD.jogStepDu=1000` *(100 microns / 0.1 mm)*
* **Returns:** `OK:CMD.jogStepDu`

---

### 2.3 Feed Pitch, Direction & Electronic Gearing Configuration

#### `CMD.feedMm`
Sets the active feed rate in millimeters per revolution (`mm/rev`).
* **Arguments:** `<pitch_mm>` (Float).
* **Example:** `CMD.feedMm=0.15`
* **Returns:** `OK:CMD.feedMm`

#### `CMD.feedTpi`
Sets the active threading pitch in Threads Per Inch (`TPI`).
* **Arguments:** `<tpi_value>` (Float).
* **Example:** `CMD.feedTpi=20.0`
* **Returns:** `OK:CMD.feedTpi`

#### `CMD.feedDupr`
Sets the raw feed rate in deci-microns per revolution (`dupr`).
* **Arguments:** `<dupr_int32>` ($10,000\text{ dupr} = 1.0\text{ mm/rev}$).
* **Example:** `CMD.feedDupr=1500` *(0.15 mm/rev)*
* **Returns:** `OK:CMD.feedDupr`

#### `CMD.gearAxis`
Selects the primary target axis for the Electronic Gearbox.
* **Arguments:** `Z` | `X` | `ZX` (Taper).
* **Example:** `CMD.gearAxis=Z`
* **Returns:** `OK:CMD.gearAxis`

#### `CMD.gearDir` (or `CMD.feedDir`)
Sets the cutting feed direction.
* **Arguments:** `+` | `-` | `LEFT` | `RIGHT` | `FWD` | `REV`.
* **Example:** `CMD.gearDir=+`
* **Returns:** `OK:CMD.gearDir`

#### `CMD.gearSync`
Enables or disables spindle electronic synchronization.
* **Arguments:** `1` (synchronized ELS gearing) | `0` (asynchronous IPM feed).
* **Example:** `CMD.gearSync=1`
* **Returns:** `OK:CMD.gearSync`

---

### 2.4 Coordinates & Soft Limits

#### `CMD.axisZero`
Zeroes the current position coordinate of the specified axis.
* **Arguments:** `<axis>` (`Z` or `X`).
* **Example:** `CMD.axisZero=Z`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.axisZero`

#### `CMD.axisPosDu`
Presets an absolute coordinate value onto an axis.
* **Arguments:** `<axis>,<position_du>`
* **Example:** `CMD.axisPosDu=Z,500000` *(Set Z coordinate to +50.000 mm)*
* **Returns:** `OK:CMD.axisPosDu`

#### `CMD.axisLimitDu`
Sets a software travel limit on the specified axis.
* **Arguments:** `<axis>,<L|R|LEFT|RIGHT>,<dist_du>`
* **Example:** `CMD.axisLimitDu=Z,LEFT,-100000` *(Set Z Left Limit to -10.0 mm)*
* **Returns:** `OK:CMD.axisLimitDu`

#### `CMD.axisClearLimit`
Clears a software travel limit register.
* **Arguments:** `<axis>,<L|R|LEFT|RIGHT>`
* **Example:** `CMD.axisClearLimit=Z,LEFT`
* **Returns:** `OK:CMD.axisClearLimit`

---

### 2.5 Taper & Cone Configuration

#### `CMD.taperAngle`
Sets the active taper angle in decimal degrees.
* **Arguments:** `<angle_degrees>` (Float).
* **Example:** `CMD.taperAngle=1.5`
* **Returns:** `OK:CMD.taperAngle`

#### `CMD.taperRatio`
Sets the active taper as a proportional ratio string.
* **Arguments:** `<ratio>` (`1:N`).
* **Example:** `CMD.taperRatio=1:5`
* **Returns:** `OK:CMD.taperRatio`

#### `CMD.taperPreset`
Applies an industry standard taper preset by name.
* **Arguments:** `MT1`, `MT2`, `MT3`, `MT4`, `MT5`, `R8`, `B16`, etc.
* **Example:** `CMD.taperPreset=MT3`
* **Returns:** `OK:CMD.taperPreset`

---

### 2.6 Simulation & Diagnostics (Elevated Permission Only)

> [!CAUTION]
> The following commands manipulate simulated encoder pulses and RPM registers. They are **strictly restricted** to test harnesses holding `PERM_SIMULATION`. Operator interfaces attempting to call these will receive `ERR:CMD.permission_denied`.

#### `CMD.simRpm`
Injects simulated spindle RPM into the motion engine for testing ELS synchronization without a spinning motor.
* **Arguments:** `<rpm_int>` (`0` to disable).
* **Example:** `CMD.simRpm=1200`
* **Required Permission:** `PERM_SIMULATION`
* **Returns:** `OK:CMD.simRpm:1200`

#### `CMD.simEncoder`
Injects synthetic spindle encoder quadrature ticks.
* **Arguments:** `<ticks_int>`
* **Example:** `CMD.simEncoder=4096`
* **Required Permission:** `PERM_SIMULATION`
* **Returns:** `OK:CMD.simEncoder`

#### `CMD.ping`
Simple diagnostic ping.
* **Arguments:** None.
* **Returns:** `OK:CMD.ping`

---

## 3. Settings & Presets Namespace (`SETTINGS.*` & `PRESET.*`)

Manages non-volatile storage (NVS) configuration, machine profiles, and tuning parameters.

### 3.1 Settings Access (`SETTINGS.*`)

| Wire Command | Description | Example |
| :--- | :--- | :--- |
| `SETTINGS.get` | Returns all key-value settings. | `SETTINGS.get` |
| `SETTINGS.get=<key>` | Reads a single configuration key. | `SETTINGS.get=leadscrew_pitch` |
| `SETTINGS.set=<key>=<val>` | Writes a configuration key to persistent NVS. | `SETTINGS.set=leadscrew_pitch=4.0` |
| `SETTINGS.reset=<key>` | Resets a specific key to factory default. | `SETTINGS.reset=leadscrew_pitch` |
| `SETTINGS.getGroup=<group>` | Dumps an entire configuration group (JSON). | `SETTINGS.getGroup=motor_z` |
| `SETTINGS.resetGroup=<group>`| Resets an entire group to factory defaults. | `SETTINGS.resetGroup=motor_z` |
| `SETTINGS.schema` | Returns JSON schema defining valid settings keys, types, and ranges. | `SETTINGS.schema` |

* **Required Permission:** `PERM_SETTINGS_WRITE` (Reads permitted with `PERM_READ_TELEMETRY`).

### 3.2 Machine Presets (`PRESET.*`)

| Wire Command | Description | Example |
| :--- | :--- | :--- |
| `PRESET.list` | Returns JSON list of available machine profile presets. | `PRESET.list` |
| `PRESET.get=<id>` | Returns JSON details of a specific machine preset. | `PRESET.get=precision_matthews_1022` |
| `PRESET.apply=<id>` | Applies a factory preset to active controller settings. | `PRESET.apply=mini_lathe_7x14` |
| `PRESET.save=<id>` | Saves the current machine configuration as a user preset. | `PRESET.save=my_custom_lathe` |
| `PRESET.delete=<id>` | Deletes a saved user preset. | `PRESET.delete=my_custom_lathe` |
| `PRESET.setDefault=<id>` | Sets the default machine preset loaded on boot. | `PRESET.setDefault=precision_matthews_1022` |

---

## 4. Query & Discovery Namespace

Fast, lightweight unicast queries for hydration, capability introspection, and system diagnostics.

### 4.1 System Information (`?sysinfo`)
Queries controller hardware, build version, and compiled feature flags.
* **Command:** `?sysinfo`
* **Response (Multi-line text):**
  ```text
  SYSINFO.version=1.0.4-modular
  SYSINFO.chip=ESP32-S3 rev 2
  SYSINFO.flash=16 MB
  SYSINFO.heap=142 KB
  SYSINFO.features=spindleSim
  ```

### 4.2 State Hydration Query (`?get` / `?state`)
Requests an immediate atomic snapshot of machine state without waiting for a motion event.
* **Syntax:** `?get` or `?get=<domains>`
* **Domain Flags:** `K` (Kinematics), `I` (Intent), `C` (Cycle), `P` (Properties), `ALL`.
* **Examples:**
  * `?get=ALL` — Hydrates all four domains.
  * `?get=K,I` — Hydrates coordinates and setup parameters only.
* **Response:** Universal canonical telemetry string:
  ```text
  K.zDu=0|K.xDu=0|K.rpm=0|K.mode=50|K.on=0|I.pitchDu=1500|I.axis=Z|P.leadscrewPitch=40000\n
  ```

### 4.3 Mode Discovery (`MODE.discover`)
Queries which cutting modes and conversational wizards can physically run on the current machine hardware.
* **Command:** `MODE.discover`
* **Response (JSON):**
  ```json
  {
    "activeProfile": "Precision Matthews 1022",
    "capabilities": ["SPINDLE_ENCODER", "MOTOR_Z", "MOTOR_X"],
    "modes": [
      { "id": 50, "name": "GEARBOX", "status": "AVAILABLE" },
      { "id": 51, "name": "TURNING", "status": "AVAILABLE" },
      { "id": 52, "name": "FACING", "status": "AVAILABLE" },
      { "id": 53, "name": "THREADING", "status": "AVAILABLE" }
    ]
  }
  ```

---

## 5. Client Handshake Namespace (`CLIENT.*`)

Governs connection establishment, capability negotiation, and authoritative `session_id` allocation.

### 5.1 Inbound Hello (`CLIENT.hello`)
Must be transmitted by the client immediately upon opening any transport connection:
* **Syntax:** `CLIENT.hello type=<profile_name>,ver=<version_string>,label="<friendly_label>"`
* **Example:** `CLIENT.hello type=smarttouch,ver=1.2.0,label="Apron Console"`
* **Returns:**
  * Success: `OK:CLIENT.hello session_id=<id>,type=<name>,controllerVer=<ver>,perms=<hex_mask>`
  * Unknown Type: `ERR:CLIENT.unknown_type:type=<name>`
  * Disallowed Medium: `ERR:CLIENT.transport_disallowed:type=<name>,transport=<t>`
  * Incompatible Version: `ERR:CLIENT.incompatible_version:min_required=<req>,received=<rec>`

---

## 6. Physical Units & Canonical Conventions

To eliminate rounding errors and floating-point drift across heterogeneous clients:

| Dimension | Canonical Unit | Multiplier / Conversion | Wire Example |
| :--- | :--- | :--- | :--- |
| **Linear Length / Position** | Deci-micron (`du`) | $1\text{ mm} = 10,000\text{ du}$<br>$1\text{ in} = 254,000\text{ du}$ | `124,500 du` = $12.450\text{ mm}$ |
| **Feed Pitch (Metric)** | Deci-microns / rev (`dupr`) | $1\text{ mm/rev} = 10,000\text{ dupr}$ | `1,500 dupr` = $0.150\text{ mm/rev}$ |
| **Feed Pitch (Imperial)** | Threads Per Inch (`TPI`) | Exact float/int | `20.0 TPI` |
| **Spindle Speed** | RPM | Exact integer | `850 RPM` |
| **Spindle Position** | Fractional revolutions (`turns`) | Float (1.0 = 360°) | `14.250 turns` |
| **Spindle Angle** | Degrees | Float ($0.0^\circ \dots 359.9^\circ$) | `184.2 deg` |
