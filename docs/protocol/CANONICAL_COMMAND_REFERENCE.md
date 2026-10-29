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

* **Namespaces:** `CMD`, `STATE`, `SETTINGS`, `PROPERTIES`, `SYS`, and `CLIENT`.
* **Strict Single-`=` Invariant:** There is **at most one** `=` in any message, strictly separating the `<NAMESPACE>.<action>` header from its `<arguments>` payload. Commands with no arguments (`CMD.stop`, `CMD.hold`, `CMD.resume`, `CMD.step`) have no `=`.
* **Colon Parameter Formatting (`name:value`):** In key-value payloads, parameter keys and values are strictly separated by a colon (`:`), **never `=`** (e.g. `CMD.startFeed=axis:Z,feed:1500,dir:-` or `CMD.startCycle=mode:thread,pitch:15000,passes:8`). Multiple parameters are separated strictly by a comma (`,`).
* **Zero Loose Spaces:** Unquoted spaces or tabs are strictly forbidden anywhere in command headers, parameter keys, or numeric values. The **only** permissible location for a space is inside a double-quoted string literal (e.g. `label:"Apron Console"`). Any unquoted space causes the command to be rejected immediately with `ERR:UNKNOWN_COMMAND`.
* **Case Sensitivity:** Command names are case-insensitive (`CMD.stop` == `cmd.STOP`). Axis names (`Z`, `X`) and units (`mm`, `tpi`, `du`) accept uppercase or lowercase.

### Universal Return Grammar
Every command processed by the controller returns an ASCII response:

| Response Header | Meaning | Wire Example |
| :--- | :--- | :--- |
| `OK:<NAMESPACE>.<name>` | Command accepted and executed successfully. | `OK:CMD.stop\n` |
| `OK:<NAMESPACE>.<name>:<data>` | Command succeeded with scalar or string payload. | `OK:CMD.startCycle:mode:thread\n` |
| `ERR:<NAMESPACE>.<name>:<reason>` | Command failed validation, was locked by motion, or faulted. | `ERR:CMD.startFeed:MOTION_LOCKED\n` |
| `ERR:UNKNOWN_COMMAND` | Unrecognized command syntax (including all decommissioned `?` queries). | `ERR:UNKNOWN_COMMAND\n` |
| `SYS.<key>=<val>` | Multi-line structured query response. | `SYS.version=1.0.4\n...` |
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
  * `CMD.startFeed=axis:Z,feed:1500,dir:-,sync:1`
  * `CMD.startFeed=Z,0.20,+`
* **Safety Lockout:** Rejected if motion is currently active (`isOn == true`), returning `ERR:CMD.startFeed:MOTION_LOCKED`.
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.startFeed` or `ERR:CMD.startFeed:<reason>`

#### `CMD.startCycle`
Atomically initiates a multi-pass canned cutting cycle in a single message frame.
* **Arguments (Order-Independent Key-Value Pairs):**
  * `mode`: Cycle type (`thread` | `turn` | `face` | `taper` | `cutoff`).
  * **Threading (`mode:thread`):**
    * `pitch:<dupr>` — Thread pitch in deci-microns/rev ($10,000 \text{ dupr} = 1.0 \text{ mm}$).
    * `depth:<du>` — Total radial cut depth in deci-microns ($10,000 \text{ du} = 1.0 \text{ mm}$).
    * `passes:<n>` — Number of progressive cutting passes.
    * `spring:<n>` — Number of zero-depth spring cleanup passes (default: 1).
    * `angle:<deg>` — Infeed compound angle in decimal degrees (e.g. `29.5` for metric/UN).
    * `zStart:<du>` — Starting Z position in deci-microns.
    * `zEnd:<du>` — Ending Z shoulder limit in deci-microns.
    * `xClear:<du>` — Radial clearance position for return passes.
    * `step:<0|1>` — (Optional) Enable single-pass inspection step mode (default: 0).
  * **Turning (`mode:turn`):**
    * `doc:<du>` — Depth of cut per pass in deci-microns.
    * `feed:<dupr>` — Synchronous feed rate in deci-microns/rev.
    * `zStart:<du>` — Starting Z coordinate.
    * `zEnd:<du>` — Finished shoulder Z limit.
    * `xStart:<du>` — Initial stock radius.
    * `xEnd:<du>` — Target final workpiece radius.
    * `xClear:<du>` — Rapid retract radial clearance.
    * `retract:<du>` — Axial clearance retract.
    * `step:<0|1>` — Enable single-pass step mode.
  * **Facing (`mode:face`):**
    * `doc:<du>` — Facing depth of cut per pass along Z.
    * `feed:<dupr>` — Cross-slide feed rate in deci-microns/rev.
    * `xStart:<du>` — Outer workpiece radius.
    * `xEnd:<du>` — Centerline radius ($X=0$).
    * `zStart:<du>` — Face stock front coordinate.
    * `zEnd:<du>` — Finished face Z coordinate.
    * `retract:<du>` — Clearance retract distance.
  * **Taper (`mode:taper`):**
    * `angle:<deg>` or `ratio:<1:N>` — Taper angle or ratio string.
    * `feed:<dupr>` — Feed rate along cutting vector.
    * `zStart:<du>`, `zEnd:<du>`, `xStart:<du>` — Cutting envelope.
  * **Cutoff / Parting (`mode:cutoff`):**
    * `feed:<dupr>` — Parting infeed rate.
    * `peck:<du>` — Peck depth increment.
    * `retract:<du>` — Chip-breaking retract distance.
    * `xStart:<du>`, `xEnd:<du>`, `zPos:<du>` — Parting plane and depth.
* **Zero Ghost Limits Invariant (ADR-15):**
  Upon receiving `CMD.startCycle`, the cycle's shoulder limit (`zEnd` or `xEnd`) is committed to the controller's active hardware limit table (`z_min`/`z_max` or `x_min`/`x_max`). This limit **persists indefinitely** after cycle completion or cycle stop until explicitly cleared or updated by the operator via `CMD.axisClearLimit`. There are zero hidden or temporary ghost limits.
* **Safety Lockout:** Rejected if motion is currently active (`isOn == true`) or envelope coordinates are invalid.
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.startCycle:mode:<mode>` or `ERR:CMD.startCycle:<reason>`

#### `CMD.hold`
Feed hold with automatic radial tool retraction.
* Immediately halts $Z$ axis carriage advance while maintaining phase-accurate electronic gearing synchronization to the spindle.
* Simultaneously retracts the $X$ cross-slide radially away from the workpiece ($X+$ to `xClear`) to prevent tool tip burn, rubbing, and chatter marks.
* Transitions machine state to `CYCLE_HOLD`.
* **Arguments:** None.
* **Example:** `CMD.hold`
* **Required Permission:** `PERM_READ_TELEMETRY` *(Hold authority is universally available to all connected clients).*
* **Returns:** `OK:CMD.hold` or `ERR:CMD.hold:<reason>`

#### `CMD.resume`
Resumes a cutting cycle paused in `CYCLE_HOLD`.
* Feeds the $X$ cross-slide back into the workpiece to the active pass depth.
* Resynchronizes spindle quadrature phase and resumes synchronous $Z$ cutting feed to complete the active pass.
* **Arguments:** None.
* **Example:** `CMD.resume`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.resume` or `ERR:CMD.resume:<reason>`

#### `CMD.step`
Advances to the next pass when running a canned cycle in Step Mode (`step:1`).
* When Step Mode is enabled, the controller finishes a pass, retracts to `xClear`, returns to `zStart`, and latches in `STEP_WAIT`.
* Receiving `CMD.step` verifies spindle rotation and executes the next single cutting pass.
* **Arguments:** None.
* **Example:** `CMD.step`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.step` or `ERR:CMD.step:<reason>`

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

## 4. Query, State & Diagnostics Namespaces (`STATE.*`, `PROPERTIES.*`, `SYS.*`)

Lightweight, high-integrity unicast queries for atomic state hydration, hardware capability introspection, and system diagnostics.

> [!IMPORTANT]
> **Decommissioning Notice (ADR-0005):** All legacy `?` query prefixes (`?sysinfo`, `?state`, `?get`, and bare `?`) are completely decommissioned with zero backwards-compatibility cruft. Any attempt to send a query starting with `?` is rejected immediately with `ERR:UNKNOWN_COMMAND`.

### 4.1 System & Appliance Diagnostics (`SYS.get`)
Queries controller hardware architecture, firmware version, memory utilization, and runtime health metrics.
* **Syntax:** `SYS.get=<info|health>`
* **Endpoints:**
  * `SYS.get=info` — Firmware build version, target ESP32 silicon revision, flash size, and compiled feature flags (replaces legacy `?sysinfo`).
  * `SYS.get=health` — Dynamic appliance vitals: free heap memory, stack watermark, uptime in seconds, Core 0 / Core 1 loop frequencies, and Wi-Fi RSSI.
* **Response (Multi-line text):**
  ```text
  SYS.version=1.0.5
  SYS.chip=ESP32-S3 rev 2
  SYS.flash=16 MB
  SYS.heap=142 KB
  SYS.uptime=3600
  SYS.features=spindleSim,ota
  ```
* **Required Permission:** `PERM_READ_TELEMETRY`
* **Returns:** `OK:SYS.get` or multi-line `SYS.<key>=<val>` block.

### 4.2 State Hydration Query (`STATE.get`)
Requests an immediate atomic snapshot of machine state across one or more telemetry domains without waiting for motion events or periodic broadcast ticks (replaces legacy `CMD.state`).
* **Syntax:** `STATE.get` or `STATE.get=<domains>`
* **Domain Flags:** `K` (Kinematics), `I` (Intent), `C` (Cycle), `P` (Properties), `ALL`. Tokens must be strict uppercase and comma-separated.
* **Examples:**
  * `STATE.get` or `STATE.get=ALL` — Hydrates all four domains simultaneously.
  * `STATE.get=K,I` — Hydrates coordinates and active cutting setup parameters only.
* **Response:** Universal canonical telemetry string:
  ```text
  K.zDu=0|K.xDu=0|K.rpm=0|K.mode=50|K.on=0|I.pitchDu=1500|I.axis=Z|P.leadscrewPitch=40000\n
  ```
* **Required Permission:** `PERM_READ_TELEMETRY`
* **Returns:** `OK:STATE.get` followed by canonical telemetry frame.

### 4.3 Mode & Profile Discovery (`PROPERTIES.*`)
Introspects machine physical capabilities, available cutting modes, and profile geometry.
* **Available Endpoints:**
  * `PROPERTIES.modes` — Returns JSON list of cutting modes and conversational wizards physically supported by the active hardware profile (replaces legacy `MODE.discover`).
  * `PROPERTIES.profiles` — Returns JSON list of available machine hardware profiles in LittleFS.
  * `PROPERTIES.get=<path>` — Queries a specific hardware property key.
* **Example `PROPERTIES.modes` Response (JSON):**
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
* **Required Permission:** `PERM_READ_TELEMETRY`

---

## 5. Client Handshake Namespace (`CLIENT.*`)

Governs connection establishment, capability negotiation, and authoritative `session_id` allocation.

### 5.1 Inbound Hello & Two-Way Verification (`CLIENT.hello`)
A client must never enable motion controls or assume a connected state based solely on transport link establishment.

#### Handshake Sequence:
1. **Link Connect:** The client transport connects (WebSocket Port 81 or WebSerial). The client immediately enters `HANDSHAKE_PENDING` with all jog, feed, and cycle buttons disabled.
2. **Inbound Hello:** The client sends:
   ```text
   CLIENT.hello=type:<profile_name>,ver:<version_string>,label:"<friendly_label>"
   ```
   *Example:* `CLIENT.hello=type:smarttouch,ver:1.2.0,label:"Apron Console"`
3. **Controller Validation:** The controller parses the profile type, validates version compatibility, assigns an authoritative `session_id`, and responds:
   ```text
   OK:CLIENT.hello session_id=<id>,type=<name>,controllerVer=<ver>,perms=<hex_mask>
   ```
4. **Transition to Connected:** Only upon receiving this ACK does the client transition to `CONNECTED` and unlock UI controls permitted by its permission bitmask.
5. **Watchdog Timeout:** If no `OK:CLIENT.hello` ACK is received within **2000 ms**, the client drops the transport link, raises an error alert, and returns to disconnected state.

#### Error Responses:
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
