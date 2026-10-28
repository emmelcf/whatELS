# ADR 0002: Command Surface Rationalization and Decoupling of Dumb Client State

* **Status:** Accepted
* **Date:** October 15, 2026
* **Authors:** Emil & Antigravity
* **Deciders:** Emil & Antigravity
* **Context:** Controller command interface simplification, rich-client vs. dumb-client separation of concerns, and elimination of idle mode state confusion.
* **Complements:** [ADR 0001: Consolidated Motion Invocation, Cartesian Polarity, and Kinematic Kernel Refactoring](./0001-motion-invocation-and-gearbox-consolidation.md)

---

## 1. Context & Problem Statement

The legacy NanoELS codebase inherited a command protocol designed around the Nextion LCD display and monolithic loop state (`h5_monolithic.ino`). Under that architecture, UI buttons directly manipulated global controller variables over serial as they were pressed:
- Tapping an axis button sent `CMD.gearAxis=X`.
- Tapping sync toggle sent `CMD.gearSync=0`.
- Tapping direction toggle sent `CMD.gearDir=...`.
- Tapping a feedrate selector sent `CMD.feedMm=...`.
- Finally, pressing the engage button sent `CMD.on=1`.

When the Modern Web UI and multi-client architecture were introduced, this paradigm produced several severe architectural defects:

1. **State Pollution While Idle**:  
   The controller was burdened with tracking which mode or tab was "selected" by a client even when no motion was running. In reality, the physical lathe is simply **IDLE**. Forcing the controller to store whether the operator is looking at "Gearbox", "Thread", or "Face" tabs on a tablet caused multi-client state collisions and confusion over what commands were permissible while idle.

2. **Network Race Conditions & Incoherent Intermediate States**:  
   Drip-feeding individual parameters across asynchronous networks (WebSockets/TCP) meant the controller frequently held mismatched intermediate states. If a network blip or quick sequence occurred, motion could engage before all parameters settled.

3. **Duplication of Command Surface**:  
   The controller exposed a tangled mix of:
   - Atomic invocations (`CMD.startFeed=...`, `CMD.jogStep=...`).
   - Fragmented modal setters (`CMD.gearAxis=...`, `CMD.gearSync=...`, `CMD.gearDir=...`, `CMD.feedMm=...`, `CMD.feedRev`).
   - Legacy monolithic string commands (`@pitch:...`, `@feed:...`).

4. **Conflation of Rich vs. Dumb Client Needs**:  
   Modern rich clients (such as the Web UI) are stateful applications capable of validating forms locally, holding authoring parameters in local memory, and dispatching a single atomic execution frame upon user confirmation. Trying to support dumb clients (which cannot hold state and must use the controller as their memory) inside the core command dispatcher bloated the core and compromised its determinism.

---

## 2. Decision Drivers

* **Rich-Client First**: Prioritize the modern, full-featured Web UI client as the canonical reference architecture.
* **Deterministic Concurrency**: Core motion commands must be self-contained and atomic; intermediate modal staging in the core is eliminated.
* **Strict Separation of Concerns**: View state (which tab is open, input field editing) belongs entirely on the client. Machine state (`IDLE`, `IN_MOTION`, `ESTOP`) belongs on the controller.
* **Lean Command Surface**: Remove or quarantine redundant, modal, and legacy commands from the core command dispatcher.
* **Pluggable Dumb-Client Support**: Dumb clients (e.g. Nextion LCD, raw serial terminals) will be supported via an optional, external client translation shim/adapter, rather than polluting the core.

---

## 3. Decision Details

### 3.1 The Canonical Rich Client API (Core Focus)

The core controller command surface is strictly rationalized around atomic intents, hardware state queries, and persistent settings:

| Command Category | Wire Syntax | Description & Semantics |
| :--- | :--- | :--- |
| **Atomic Feed Motion** | `CMD.startFeed=axis:<axis>,sync:<sync>,feed:<rate>,dir:<dir>,[dist:<dist>]` | Atomically validates and initiates continuous or targeted feed motion via Key-Value pairs (or positional fallback). |
| **Motion Stop** | `CMD.stop` or `CMD.on=0` | Immediately stops all in-flight motion with controlled deceleration. |
| **Manual Jogging** | `CMD.jogHold=<axis>,<dir>`<br>`CMD.jogRelease=<axis>`<br>`CMD.jogStep=<axis>,<dir>` | Direct real-time manual positioning. |
| **Dynamic In-Cut Nudge** | `CMD.feedNudge=<+1\|-1>` | Adjusts active feedrate under chip load. |
| **Coordinate & Limits** | `CMD.axisZero=<axis>`<br>`CMD.axisPosDu=<axis>,<posDu>`<br>`CMD.axisLimitDu=<axis>,<L\|R>,<posDu>`<br>`CMD.axisClearLimit=<axis>,<L\|R>` | Origin datum setting and software travel limit registers. |
| **Persistent Settings** | `SETTINGS.get=*`<br>`SETTINGS.set=<key>,<val>` | Core machine configuration (leadscrews, steps, encoder PPR). |
| **Hydration & Telemetry** | `CMD.state=ALL`<br>`?sysinfo` | Atomic multi-domain state snapshots and firmware capability queries. |

### 3.2 Idle Machine State vs. Client View State

* When physical motion is stopped (`isOn == false`), the machine state is universally **`IDLE`**.
* The core controller maintains no concept of an "idle mode". Tab selection, form inputs, metric/imperial display preference, and preset dropdowns are strictly client-side view state.
* The core controller only instantiates and tracks an `activeMode` when motion is physically commanded and running in-cut (`isOn == true`).

### 3.3 Retirement & Quarantining of Fragmented Commands

The following fragmented commands are **deprecated and removed from the canonical core API**:
* `CMD.gearAxis=<axis>`
* `CMD.gearSync=<sync>`
* `CMD.gearDir=<dir>`
* `CMD.feedMm=<val>`
* `CMD.feedDupr=<val>`
* `CMD.feedRev`
* Legacy serial strings (`@pitch:`, `@feed:`)

**Handling in Code:**
- In `CommandDispatcher.cpp`, these commands are isolated inside an explicitly marked `Transitional / Legacy Compatibility Adapter` block.
- In documentation, they are removed from the canonical table and documented only in a "Deprecated / Transitional" appendix.
- The Web UI client (`lathe-controller.js`) is updated to stop sending partial parameter mutations while the user interacts with form fields.

### 3.4 Dumb-Client Decoupling Architecture

For clients that lack local state retention (such as the Nextion LCD or simple serial pendants):
- They do **not** dictate the core architecture.
- A dedicated **Client Translation Shim** (or Ingress Adapter) will maintain an in-memory staging struct on behalf of the dumb client.
- When the dumb client sends its engage trigger (e.g. Nextion button press), the translation shim compiles its staged parameters and calls the canonical atomic API (`CMD.startFeed`) internally.

```
+─────────────────────────────────────────────────────────────────────────+
| Modern Rich Clients (Web UI, Touch DRO, Tablets)                       |
| - Owns view state, tabs, form validation                                |
| - Dispatches atomic commands: CMD.startFeed=axis:Z,sync:1,feed:150,dir:CHUCK |
+─────────────────────────────────────────────────────────────────────────+
                                    │
                                    ▼
+─────────────────────────────────────────────────────────────────────────+
| CANONICAL CORE CONTROLLER (whatELS Motion Kernel)                       |
| - CommandDispatcher: Pure, atomic, stateless ingress                    |
| - State Model: IDLE vs. IN_MOTION (no idle tab awareness)               |
| - Kinematic Kernel: Motion::startFeed(req), stepVelocity, stepSync      |
+─────────────────────────────────────────────────────────────────────────+
                                    ▲
                                    │ (Internal atomic dispatch)
+─────────────────────────────────────────────────────────────────────────+
| OPTIONAL DUMB CLIENT SHIM (Nextion / Serial Terminal Adapter)           |
| - Holds staging state on behalf of memoryless hardware                  |
| - Collects button taps, packages into atomic CMD.startFeed on run       |
+─────────────────────────────────────────────────────────────────────────+
```

---

## 4. Consequences & Impact

### Positive
* **Zero Parameter Drift:** Eliminates torn frames, mismatched mode parameters, and phantom motion starts.
* **Radical Core Simplification:** `CommandDispatcher` drops dozens of redundant switch cases and stateful flags.
* **True Multi-Client Freedom:** Multiple web clients or pendants can connect without fighting over which "tab" or "mode" the machine is in while sitting idle.
* **Clear Documentation:** Developers and third-party integrators have a single, unambiguous atomic protocol to implement.

### Trade-offs & Mitigations
* *Dumb-Client Staging Required:* Legacy Nextion code cannot directly talk to the raw core without a translation shim.  
  *Mitigation:* Staging logic is quarantined in a dedicated compatibility section in `CommandDispatcher.cpp` until the Nextion driver is refactored into its own `ClientEndpoint` module.
* *Web UI Client Update:* Web UI must assemble the atomic parameter tuple on START.  
  *Mitigation:* `lathe-controller.js` already holds the current UI form state in JavaScript; updating `buildInceptionBundle()` / START handler directly dispatches `CMD.startFeed`.

---

## 5. References

* [ADR 0001: Consolidated Motion Invocation, Cartesian Polarity, and Kinematic Kernel Refactoring](./0001-motion-invocation-and-gearbox-consolidation.md)
* [`docs/ELS_CLIENT_ARCHITECTURE.md`](../ELS_CLIENT_ARCHITECTURE.md)
* [`docs/ELS_CORE_STATE_MODEL.md`](../ELS_CORE_STATE_MODEL.md)
* [`docs/clients/web_ui/INTERACTION_LEDGER.md`](../clients/web_ui/INTERACTION_LEDGER.md)
