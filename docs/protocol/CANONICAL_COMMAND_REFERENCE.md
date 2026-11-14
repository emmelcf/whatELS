# whatELS Canonical Command & Protocol Wire Reference

> **Status:** Authoritative Wire Specification  
> **Target:** Core ↔ core-client boundary gateway (`CoreCommandService`) & Subsystem 5 (`StandardCommandParser` & Transports)
> **Master Architecture:** [`docs/clients/ELS_CLIENT_ARCHITECTURE.md`](../clients/ELS_CLIENT_ARCHITECTURE.md)  
> **Implementation Reference:** [`src/core_client/StandardCommandParser.cpp`](../../src/core_client/StandardCommandParser.cpp), [`src/core/CoreCommandService.cpp`](../../src/core/CoreCommandService.cpp)  
> **Date:** October 2026  

---

## 1. Overview & Syntax Rules

The **whatELS Canonical Command Surface** is the single authoritative wire interface for controlling the whatELS motion controller. All transports (WebSocket, physical UART Serial, USB CDC) and all client endpoints (`NativeClient`, `NextionAdapter`, HIL test harness) converge on this command dictionary via [`StandardCommandParser::parseAndDispatch()`](../../src/core_client/StandardCommandParser.h) and [`CoreCommandService::execute()`](../../src/core/CoreCommandService.h).

### Command Grammar
Every canonical message is an ASCII string terminated by a newline (`\n` or `\r\n`):

$$\texttt{<NAMESPACE>.<action>[=<arguments>]}\backslash\text{n}$$

* **Namespaces (6):** `CMD`, `STATE`, `SETTINGS`, `PROPERTIES`, `SYS`, and `CLIENT`.
* **Strict Single-`=` Invariant:** There is **at most one** `=` in any message, strictly separating the `<NAMESPACE>.<action>` header from its `<arguments>` payload. Commands with no arguments (`CMD.stop`, `CMD.hold`, `CMD.resume`, `CMD.step`, `CMD.ping`) have no `=`.
* **Colon Parameter Formatting (`name:value`):** In key-value payloads, parameter keys and values are strictly separated by a colon (`:`), **never `=`** (e.g. `CMD.startFeed=axis:Z,feed:1500,dir:-` or `CMD.startCycle=mode:thread,pitch:15000,passes:8`). Multiple parameters are separated strictly by a comma (`,`). Duplicate or unknown parameter keys are rejected with `ERR:UNKNOWN_COMMAND` or `ERR:<CMD>:INVALID_PARAM:<key>`.
* **Zero Loose Spaces:** Unquoted spaces or tabs are strictly forbidden anywhere in command headers, parameter keys, or numeric values. The **only** permissible location for a space is inside a double-quoted string literal (e.g. `label:"Apron Console"`). Any unquoted space causes the command to be rejected immediately with `ERR:UNKNOWN_COMMAND`.
* **Strict Case Sensitivity:** The protocol is strictly case-sensitive. Namespace headers are uppercase (`CMD`, `STATE`, `SETTINGS`, `PROPERTIES`, `SYS`, `CLIENT`), action names and parameter keys are camelCase, and axis identifiers are uppercase (`Z`, `X`). Any command, namespace, action, or parameter passed with incorrect case is rejected immediately.
* **Strict Binary Flags (`<0|1>`):** All boolean flags on the wire (such as `sync`, `step`, and `active`) are strictly represented by the numeric characters `1` (enabled / true) or `0` (disabled / false). Text variants (`true`, `false`, `sync`, `async`) are invalid and rejected immediately with `INVALID_PARAM`.
* **Pure Integer Deci-Microns (`du`):** All linear coordinates, travel distances, and feed rates on the wire are strict 32-bit signed integers in deci-microns (`1 mm = 10,000 du`, `1 mm/rev = 10,000 dupr`). Floating-point decimal points on the wire are rejected with `INVALID_PARAM` (with the sole exception of `CMD.simRpm`).

### Universal Return Grammar
Every command processed by the controller returns an ASCII response:

| Response Header | Meaning | Wire Example |
| :--- | :--- | :--- |
| `OK:<NAMESPACE>.<name>` | Command accepted and executed successfully. | `OK:CMD.stop\n` |
| `OK:<NAMESPACE>.<name>:<data>` | Command succeeded with scalar or string payload. | `OK:CLIENT.hello:session_id=1,...\n` |
| `ERR:<NAMESPACE>.<name>:<reason>` | Command failed validation, was locked by motion, or is unavailable. | `ERR:CMD.startFeed:MOTION_LOCKED\n` |
| `ERR:UNKNOWN_COMMAND` | Unrecognized command syntax (including all decommissioned `?` queries). | `ERR:UNKNOWN_COMMAND\n` |
| `SYS.<key>=<val>` | Multi-line structured query response. | `SYS.version=H5V38\n...` |
| `K.<field>=<val>...` | Universal telemetry hydration or snapshot push. | `K.zDu=12400\|K.rpm=850...\n` |

---

## 2. Motion & Kinematic Control Namespace (`CMD.*`)

Commands that engage stepper motors, set feed directions, configure cycle parameters, or manage coordinates.

### 2.1 Motion Lifecycle & Cycle Execution

#### `CMD.startFeed`
Engages leadscrew motion according to the specified parameters. If called while idle, transitions state to `RUNNING`.
* **Arguments (Key-Value Syntax Required):** 
  * `axis`: Target axis (`Z`, `X`, or `ZX` for coordinated dual-axis cone/taper feed). **Mandatory** (zero default).
  * `feed`: Feed rate magnitude as a positive integer (`> 0`): deci-microns per rev (`dupr`) when `sync:1`, or deci-microns per minute (`du/min`) when `sync:0`. **Mandatory**.
  * `dir`: Primary axis Cartesian direction token. **Mandatory** (zero default):
    * Universal: `-`, `-1` (negative coordinate feed) or `+`, `+1` (positive coordinate feed).
    * $Z$ axis (and `ZX` primary axis): `CHUCK`, `LEFT` ($\to -1$); `TAIL`, `RIGHT` ($\to +1$).
    * $X$ axis only: `CENTER`, `IN`, `INFEED`, `UP` ($\to -1$); `RETRACT`, `OUT`, `DOWN` ($\to +1$).
    * Note: `FWD` and `REV` are strictly forbidden (reserved for spindle rotation per ADR-0001).
  * `xdir`: Secondary $X$ axis direction token (`CENTER`, `IN`, `INFEED`, `UP`, `-`, `-1` $\to -1$; `RETRACT`, `OUT`, `DOWN`, `+`, `+1` $\to +1$). **Mandatory when `axis:ZX`** (`MISSING_PARAM:xdir` if omitted); rejected with `INVALID_PARAM:xdir` on single-axis `Z` or `X`.
  * `taper`: Radial $X$ travel in integer deci-microns (`du`) per $10{,}000\text{ du}$ ($1.0\text{ mm}$) of axial $Z$ travel (`> 0`, e.g., `1000` = $0.1000$ radial slope). **Mandatory when `axis:ZX`** (`MISSING_PARAM:taper` if omitted); rejected with `INVALID_PARAM:taper` on single-axis `Z` or `X`.
  * `sync`: (Optional) Spindle synchronization (`1` for ELS electronic gearing; `0` for continuous velocity feed). Default: `1`.
  * `dist`: (Optional) Target travel distance in integer deci-microns (`du`, `> 0`), or `-1` for continuous feed to soft limit. Default: `-1`.
* **Examples:**
  * `CMD.startFeed=axis:Z,feed:1500,dir:CHUCK,sync:1`
  * `CMD.startFeed=axis:X,feed:500000,dir:CENTER,sync:0,dist:20000`
  * `CMD.startFeed=axis:ZX,feed:10000,dir:CHUCK,xdir:RETRACT,taper:1000,sync:1`
* **Safety Lockout:** Rejected if motion is currently active (`isOn == true`), returning `ERR:CMD.startFeed:MOTION_LOCKED`.
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.startFeed` or `ERR:CMD.startFeed:<reason>`

#### `CMD.startCycle`
Atomically initiates a multi-pass canned cutting cycle in a single message frame.
> [!NOTE]
> **Implementation Status (Refs Issue #10):** Multi-pass canned cycle `IELSMode` classes are currently pending implementation in `src/motion/modes/`. Until registered in `ModeRegistry`, valid `CMD.startCycle` invocations deterministically return `ERR:CMD.startCycle:UNAVAILABLE`.

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
    * `feed:<dupr>` — Synchronous feed rate in deci-microns/rev.
    * `doc:<du>` — Depth of cut per pass in deci-microns.
    * `zStart:<du>` — Starting Z coordinate.
    * `zEnd:<du>` — Finished shoulder Z limit.
    * `xStart:<du>` — Initial stock radius.
    * `xClear:<du>` — Rapid retract radial clearance.
    * `passes:<n>` — Total cutting passes (default: 1).
    * `step:<0|1>` — (Optional) Enable single-pass step mode (default: 0).
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
* Immediately halts active axis carriage advance while maintaining phase-accurate electronic gearing synchronization to the spindle.
* Transitions machine state to `CYCLE_HOLD` / `PAUSED`.
* **Arguments:** None.
* **Example:** `CMD.hold`
* **Required Permission:** `PERM_READ_TELEMETRY` *(Hold authority is universally available to all connected clients).*
* **Returns:** `OK:CMD.hold` or `ERR:CMD.hold:<reason>`

#### `CMD.resume`
Resumes a feed or cutting cycle paused in `CYCLE_HOLD`.
* Resynchronizes spindle quadrature phase and resumes cutting feed to complete the active pass.
* **Arguments:** None.
* **Example:** `CMD.resume`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.resume` or `ERR:CMD.resume:<reason>`

#### `CMD.step`
Advances to the next pass when running a canned cycle in Step Mode (`step:1`).
* When Step Mode is enabled, the controller finishes a pass, retracts to `xClear`, returns to `zStart`, and latches in `STEP_WAIT`.
* Receiving `CMD.step` verifies spindle rotation and executes the next single cutting pass. *(Currently returns `ERR:CMD.step:UNAVAILABLE` pending Issue #10 canned cycle implementation).*
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

#### `CMD.feedNudge`
Incrementally adjusts the active feed rate up or down by one configured `feedStepDu` increment.
* **Arguments:** `<+1|-1>`
* **Examples:** `CMD.feedNudge=+1`, `CMD.feedNudge=-1`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.feedNudge` or `ERR:CMD.feedNudge:<reason>`

---

### 2.2 Manual Jogging & Positioning

#### `CMD.jogStep`
Executes an incremental single-step jog on the specified axis.
* **Arguments:** `<axis>,<dist_du>` or `axis:<Z|X>,dist:<dist_du>` (Signed integer distance in deci-microns, $\neq 0$, where $10,000 \text{ du} = 1.0\text{ mm}$).
* **Examples:**
  * `CMD.jogStep=Z,100` *(Jog Z positive 10 microns / 0.01 mm)*
  * `CMD.jogStep=axis:X,dist:-500` *(Jog X negative 50 microns / 0.05 mm)*
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogStep` or `ERR:CMD.jogStep:<reason>`

#### `CMD.jogHold`
Initiates continuous jogging on an axis. The axis continues stepping until `CMD.jogRelease` or `CMD.stop` is received.
* **Arguments:** `<axis>,<direction>` or `axis:<Z|X>,dir:<direction>` (`+` / `-` / `-1` / `+1`, `LEFT` / `RIGHT` / `CHUCK` / `TAIL` for $Z$, `UP` / `DOWN` / `CENTER` / `RETRACT` / `IN` / `INFEED` / `OUT` for $X$).
* **Example:** `CMD.jogHold=Z,+`
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogHold` or `ERR:CMD.jogHold:<reason>`

#### `CMD.jogRelease`
Stops an active continuous jog initiated by `CMD.jogHold`.
* **Arguments:** `<axis>` or `axis:<Z|X>` (Optional; stops all axes if omitted).
* **Examples:** `CMD.jogRelease=Z` or `CMD.jogRelease`
* **Required Permission:** `PERM_JOG_PULSE`
* **Returns:** `OK:CMD.jogRelease` or `ERR:CMD.jogRelease:<reason>`

---

### 2.3 Coordinates & Soft Limits

#### `CMD.axisZero`
Zeroes the current position coordinate of the specified axis.
* **Arguments:** `<axis>` or `axis:<Z|X>` (`Z` or `X`).
* **Example:** `CMD.axisZero=Z`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.axisZero` or `ERR:CMD.axisZero:<reason>`

#### `CMD.axisPosDu`
Presets an absolute integer coordinate value onto an axis.
* **Arguments:** `<axis>,<position_du>` or `axis:<Z|X>,pos:<position_du>`
* **Example:** `CMD.axisPosDu=Z,500000` *(Set Z coordinate to +50.000 mm)*
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.axisPosDu` or `ERR:CMD.axisPosDu:<reason>`

#### `CMD.axisLimitDu`
Sets a software travel limit on the specified axis.
* **Arguments:** `<axis>,<bound>,<coord_du>` or `axis:<Z|X>,bound:<bound>,coord:<coord_du>`
  * Valid `<bound>` tokens: Universal `L`, `R`, `MIN`, `MAX`; $Z$-axis `LEFT`, `CHUCK`, `RIGHT`, `TAIL`; $X$-axis `UP`, `CENTER`, `IN`, `INFEED`, `DOWN`, `RETRACT`, `OUT`.
* **Example:** `CMD.axisLimitDu=Z,LEFT,-100000` *(Set Z Left Limit to -10.0 mm)*
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.axisLimitDu` or `ERR:CMD.axisLimitDu:<reason>`

#### `CMD.axisClearLimit`
Clears a software travel limit register.
* **Arguments:** `<axis>,<bound>` or `axis:<Z|X>,bound:<bound>`
* **Example:** `CMD.axisClearLimit=Z,LEFT`
* **Required Permission:** `PERM_MOTION_START`
* **Returns:** `OK:CMD.axisClearLimit` or `ERR:CMD.axisClearLimit:<reason>`

---

### 2.4 Simulation & Diagnostics (Elevated Permission Only)

> [!CAUTION]
> The following commands manipulate simulated encoder pulses and RPM registers. They are **strictly restricted** to test harnesses holding `PERM_SIMULATION`. Operator interfaces attempting to call these will receive `ERR:CMD.<name>:PERMISSION_DENIED`.

#### `CMD.simEncoder`
Enables or disables synthetic spindle encoder simulation mode on the controller HAL. Rejected with `SAFETY_INTERLOCK_ACTIVE` if any axis is currently moving or physical spindle RPM > 0.
* **Arguments:** `<0|1>` or `active:<0|1>`
* **Example:** `CMD.simEncoder=1`
* **Required Permission:** `PERM_SIMULATION`
* **Returns:** `OK:CMD.simEncoder` or `ERR:CMD.simEncoder:<reason>`

#### `CMD.simRpm`
Injects simulated spindle RPM into the motion engine for testing ELS synchronization without a spinning motor.
* **Arguments:** `<rpm>` or `rpm:<rpm>` (`>= 0`, `0` to stop).
* **Example:** `CMD.simRpm=1200`
* **Required Permission:** `PERM_SIMULATION`
* **Returns:** `OK:CMD.simRpm` or `ERR:CMD.simRpm:<reason>`

#### `CMD.simPulses`
Injects a discrete number of synthetic spindle encoder quadrature pulses.
* **Arguments:** `<pulses_int>` or `pulses:<pulses_int>` ($\neq 0$).
* **Example:** `CMD.simPulses=4096`
* **Required Permission:** `PERM_SIMULATION`
* **Returns:** `OK:CMD.simPulses` or `ERR:CMD.simPulses:<reason>`

#### `CMD.ping`
Refreshes the simulation / jog safety watchdog heartbeat.
* **Arguments:** None.
* **Returns:** `OK:CMD.ping`

---

## 3. Settings & Machine Profiles Namespace (`SETTINGS.*`)

Manages non-volatile storage (NVS) configuration, machine profile selection, and tuning parameters.

### 3.1 Settings Access (`SETTINGS.*`)

| Wire Command | Description | Example |
| :--- | :--- | :--- |
| `SETTINGS.get` | Returns all key-value settings. | `SETTINGS.get` |
| `SETTINGS.get=<key>` | Reads a single configuration key or group (`machine`, `sys`, `prefs`). | `SETTINGS.get=machine.z.pitchDu` |
| `SETTINGS.set=<key>:<val>` | Writes a configuration key to persistent NVS. | `SETTINGS.set=machine.z.pitchDu:40000` |
| `SETTINGS.set=profile:<id>` | Activates a machine profile preset (gated by `[SAFE-01]`; rejected if motion is active). | `SETTINGS.set=profile:wm210v` |
| `SETTINGS.reset=<key>` | Resets a specific key or `ALL` to factory default. | `SETTINGS.reset=ALL` |
| `SETTINGS.presets` | Returns JSON list of available machine profile presets. | `SETTINGS.presets` |
| `SETTINGS.catalogSave=<json>` | Saves or updates a custom user catalog entry (`profiles`, `threads`, `tapers`) in LittleFS `/presets/user/<domain>/`. | `SETTINGS.catalogSave={"domain":"threads","id":"custom_m12",...}` |
| `SETTINGS.catalogDelete=<json>` | Deletes a custom user catalog entry from LittleFS `/presets/user/<domain>/` (ROM entries, active profile, and default profile are protected). | `SETTINGS.catalogDelete={"domain":"threads","id":"custom_m12"}` |
| `SETTINGS.schema` | Returns JSON schema defining valid settings keys, types, and ranges. | `SETTINGS.schema` |

* **Required Permission:** `PERM_SETTINGS_WRITE` for `set`, `reset`, `catalogSave`, and `catalogDelete` (Reads permitted with `PERM_READ_TELEMETRY`).
* **Retired Commands:** Legacy `PRESET.*` and `SETTINGS.preset.*` commands are retired in favor of `PROPERTIES.catalog`, `SETTINGS.catalogSave`, `SETTINGS.catalogDelete`, and `SETTINGS.set=profile:<id>`.

---

## 4. Query, State & Diagnostics Namespaces (`STATE.*`, `PROPERTIES.*`, `SYS.*`)

Lightweight, high-integrity unicast queries for atomic state hydration, hardware capability introspection, and system diagnostics.

> [!IMPORTANT]
> **Decommissioning Notice (ADR-0005):** All legacy `?` query prefixes (`?sysinfo`, `?state`, `?get`, and bare `?`) are completely decommissioned with zero backwards-compatibility cruft. Any attempt to send a query starting with `?` is rejected immediately with `ERR:UNKNOWN_COMMAND`.

### 4.1 System & Appliance Diagnostics (`SYS.get`)
Queries controller hardware architecture, firmware version, memory utilization, and runtime health metrics.
* **Syntax:** `SYS.get`, `SYS.get=info`, or `SYS.get=health`
* **Endpoints:**
  * `SYS.get` / `SYS.get=info` — Firmware build version, target ESP32 silicon revision, flash size, and compiled feature flags (replaces legacy `?sysinfo`).
  * `SYS.get=health` — Dynamic appliance vitals: uptime in seconds, free heap memory in KB, and active Wi-Fi mode.
* **Response (Multi-line text):**
  ```text
  SYS.version=H5V38
  SYS.chip=ESP32-S3 rev 2
  SYS.flash=16 MB
  SYS.features=spindleSim,ota
  ```
* **Off-Target Behavior:** Builds or endpoints without an injected `ISystemInfoHAL` provider return `ERR:SYS.get:UNAVAILABLE`. Test suites inject `MockSystemInfoHAL` to simulate hardware vitals deterministically.
* **Required Permission:** `PERM_READ_TELEMETRY`
* **Returns:** Multi-line `SYS.<key>=<val>` block or `ERR:SYS.get:<reason>`.

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
* **Returns:** Canonical multi-line or pipe-delimited telemetry frame.

### 4.3 Mode & Profile Discovery (`PROPERTIES.*`)
Introspects machine physical capabilities, available cutting modes, profile geometry, and the unified preset catalog.
* **Available Endpoints:**
  * `PROPERTIES.modes[=all]` — Returns JSON list of cutting modes (`PROPERTIES.modes` returns modes supported by the active hardware profile; `PROPERTIES.modes=all` returns all registered modes including those unavailable on the current hardware profile, replacing legacy `MODE.discover`).
  * `PROPERTIES.profiles` — Returns JSON list of available machine hardware profiles.
  * `PROPERTIES.catalog=path=<domain>[/<catalog>[/<id>]]&$select=...&$filter=...&$metadata=true` — Unified transport-agnostic OData-style query across `profiles`, `threads`, and `tapers` (merging factory ROM `SystemPresetsJson.h` and LittleFS `/presets/user/<domain>/`).
  * `PROPERTIES.get=<path>` — Queries a specific hardware property key.
* **Example `PROPERTIES.modes` Response (JSON):**
  ```json
  {
    "activeProfile": "Precision Matthews 1022",
    "capabilities": ["SPINDLE_ENCODER", "MOTOR_Z", "MOTOR_X"],
    "modes": [
      { "id": 50, "name": "GEARBOX", "status": "AVAILABLE" }
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
3. **Controller Validation:** The controller parses the profile type (`type` is mandatory), validates version compatibility, assigns an authoritative `session_id`, and responds:
   ```text
   OK:CLIENT.hello:session_id=<id>,type=<name>,controllerVer=<ver>,perms=<hex_mask>
   ```
4. **Transition to Connected:** Only upon receiving this ACK does the client transition to `CONNECTED` and unlock UI controls permitted by its permission bitmask.
5. **Watchdog Timeout:** If no `OK:CLIENT.hello:` ACK is received within **2000 ms**, the client drops the transport link, raises an error alert, and returns to disconnected state.

#### Error Responses:
* Missing Type: `ERR:CLIENT.hello:MISSING_PARAM:type`
* Unknown Parameter: `ERR:CLIENT.hello:INVALID_PARAM:<key>`

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
