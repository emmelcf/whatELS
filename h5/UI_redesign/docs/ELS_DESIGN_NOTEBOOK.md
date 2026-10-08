# NanoELS H5 — Architectural Decision Record (ADR) & Historical Solutions Log

> **Master Blueprint Reference:** The live, authoritative master system blueprint, 6-subsystem topology, file mapping, and contributor invariants are maintained in [**`NANOELS_MASTER_ARCHITECTURE.md`**](./NANOELS_MASTER_ARCHITECTURE.md).  
> **Document Role:** Architectural Decision Record (ADR), Evolutionary History & Engineering Solutions Log  
> **Status:** Living Historical Ledger  
> **Target Platform:** NanoELS H5 Core Motion Controller & Multi-Client Suite  
> **Date:** October 8, 2026  
> **Location:** `c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/docs/ELS_DESIGN_NOTEBOOK.md`

---

## 1. Historical Context & Evolutionary Journey

To understand why the codebase is structured as it is and avoid treating legacy hacks as sacred cows, all developers and AI agents must understand the three evolutionary phases of this project:

```
┌────────────────────────────────┐     ┌────────────────────────────────┐     ┌────────────────────────────────┐
│ PHASE 0: Legacy Monolith       │     │ PHASE 1: C++ Modularization    │     │ PHASE 2: Architectural Rewrite │
│ • Single 370KB `h5.ino` file   │ ──► │ • Extracted into C++ units     │ ──► │ • Ground-up modular architecture│
│ • Nextion serial tightly bound │     │   (Axis, Motion, Config, etc.) │     │ • 6 decoupled subsystems       │
│ • 200+ procedural globals      │     │ • 1:1 behavioral port of .ino  │     │ • Modernized underlying engine │
│ • Ad-hoc timing & sleep hacks  │     │ • Preserved legacy quirks      │     │ • Obsoletes legacy baggage     │
└────────────────────────────────┘     └────────────────────────────────┘     └────────────────────────────────┘
```

### Phase 0: Origin — The Russian `h5.ino` Monolith
The project began as an open-source Russian electronic leadscrew (ELS) controller implemented in a single, massive ~370 KB Arduino sketch file (`h5.ino`). While functionally impressive for hobbyist lathe automation, the codebase had severe architectural limitations:
* **Entangled Concerns:** High-frequency timer ISRs (stepper pulse generation) were directly interleaved with slow Nextion LCD serial packet decoding, operator state management, and hardware calibration.
* **Global State Explosion:** Over 200 unencapsulated global variables interacted without access control or thread safety.
* **Hardcoded Hardware & UI Assumptions:** Hardcoded for a specific low-resolution Nextion serial screen, Soviet/Russian lathe conventions, and fixed hardware wiring.
* **Ad-Hoc Timing Hacks:** Loaded with arbitrary delay loops and timing flags (`stepTimeMs`, `delayBetweenStepsMs`, `zNeedsRest`, `tftFirstUpload`, `showKeyPresses`) created to work around serial bottlenecks and Nextion display limitations.

### Phase 1: The UI Ambition & In-Flight Modularization
The primary goal was to bring a modern, high-resolution touch UI / tablet DRO (and eventually WebSocket streaming) to the lathe controller so operators would no longer be restricted to the clunky, low-resolution Nextion interface.
* **The Refactoring Effort:** The user and team extracted the 370KB monolith into separate C++ classes and compilation units under `src/`:
  - [`Axis.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Axis.cpp) / [`Axis.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Axis.h) — Stepper pulse generation and DRO scale reading.
  - [`Motion.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Motion.cpp) / [`Motion.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Motion.h) — Spindle synchronization, electronic gearing, and threading.
  - [`Config.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Config.cpp) / [`Config.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Config.h) — NVS storage and global machine configuration.
  - [`Input.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Input.cpp) / [`Input.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Input.h) — Hardware keypads, MPG handwheel, and joystick.
  - [`Display.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Display.cpp) / [`Display.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Display.h) — Nextion display communication.
  - [`SettingsService.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/SettingsService.cpp) / [`SettingsService.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/SettingsService.h) — Early settings abstraction.
* **The Lingering Hybrid Problem:** While the code was physically separated into `.cpp` and `.h` files, it was **not yet architecturally redesigned**. It remained a direct 1:1 translation of the legacy `.ino`. It still relied on raw globals (`SCREW_Z_DU`, `MOTOR_STEPS_Z`, `ENCODER_PPR`), legacy naming units (deci-microns `du`), legacy Nextion state machines, and legacy configuration schemas.

### Phase 2: The Breakthrough — True Architectural Modernization
As work progressed, a crucial realization emerged: **Restricting the new modern UI and system capabilities to only what the old Russian `.ino` supported was holding the entire project back.**
* Instead of merely putting a cosmetic web facade over legacy Arduino quirks, the decision was made to **redesign both the UI and the underlying controller functionality from first principles**.
* The platform was reconceptualized as a modern, industrial-grade, multi-client motion system built on the **6 decoupled foundational subsystems** defined in [`NANOELS_MASTER_ARCHITECTURE.md`](./NANOELS_MASTER_ARCHITECTURE.md).

---

## 2. The 4-Way Code Disposition Matrix

When inspecting or refactoring any parameter, struct, or function from Phase 0/1, classify it into one of four buckets:

| Disposition | Meaning | Action | Examples |
| :--- | :--- | :--- | :--- |
| **Keep & Modernize** | True machine physics or motion requirements. | Move into typed structs (`LatheConfiguration`, `SettingsService`) with clean naming. | Leadscrew pitch, motor steps, encoder PPR, axis acceleration, DRO scale resolution. |
| **Relocate** | Misplaced domain: cycle/machining parameters dumped into machine settings. | Move from machine config into `IntentState` (`I.*`) or `CycleState` (`C.*`). | `safeDistanceDu`, `slotLeftReductionDu`, `threadAngle`, `springPasses`. |
| **Discard** | Obsolete baggage from Phase 0/1. | Remove completely or mark deprecated; do NOT propagate into new designs. | `stepTimeMs`, `delayBetweenStepsMs`, `zNeedsRest`, `showKeyPresses`, `tftFirstUpload`. |
| **Introduce New** | Modern capability required by Phase 2 architecture. | Implement according to the 6 authoritative subsystem blueprints. | Capability bitmask (`LatheCapabilityFlags`), Machine Profile catalog, Sparse NVS overrides. |

---

## 3. Architectural Decision Records (ADRs) & Solutions Log

Below is the record of major architectural decisions, the problems they solved, and their engineering rationale:

### ADR 01: Decoupled 3-Tier Client Transport Pipeline
* **Context:** Legacy firmware hardcoded Nextion serial routines directly into motion and input loops. Adding WebSockets or USB pendants created circular dependencies.
* **Decision:** Established a 3-tier pipeline (`ITransport` $\rightarrow$ `ICommandTranslator` $\rightarrow$ `CommandDispatcher`). Physical transports (WebSocket, UART, Serial) are dumb byte pipes. All clients emit canonical line commands (`CMD.*`, `SETTINGS.*`) and receive domain-isolated telemetry (`K`, `I`, `C`, `P`).
* **Documented in:** [`ELS_CLIENT_ARCHITECTURE.md`](./ELS_CLIENT_ARCHITECTURE.md).

### ADR 02: Two-Way Pass Derivation Macro Bridge
* **Context:** Machinists think in terms of `MAX DOC` (Depth of Cut, e.g. 0.50 mm/pass), whereas firmware motion loops execute a discrete integer pass count (`turnPasses`).
* **Decision:** Implemented a two-way macro bridge:
  $$\text{turnPasses} = \left\lceil \frac{\text{Total Depth}}{\text{Max DOC}} \right\rceil$$
  Editing `MAX DOC` automatically recalculates discrete passes; conversely, manually overriding the pass count in the runstack recalculates actual DOC per pass, preventing rounding surprises.

### ADR 03: Asymmetric Anti-Trap Limit State Machine
* **Context:** When a carriage hit an electronic limit stop (`zLeft` / `zRight`), legacy firmware stalled motion silently, locking out jog buttons and leaving the operator trapped without knowing how to escape.
* **Decision:** Built an asymmetric anti-trap guard in telemetry:
  * When `distanceToStop == 0`, jog steps *into* the boundary wall are locked out.
  * Jog steps *away* from the boundary wall remain 100% accessible.
  * UI displays vector arrow-into-wall glyphs (`|←`, `→|`, `⤒`, `⭳`) with remaining distance.

### ADR 04: Separation of Continuous Gearing vs. Canned Taper Cycles
* **Context:** Legacy firmware conflated manual electronic leadscrew feeding (`GEARBOX Z+X`) and autonomous multi-pass conical machining (`TAPER`) into a single global mode (`MODE_CONE`).
* **Decision:** Formally separated them into independent operational modes:
  * `GEARBOX`: Continuous manual synchronized electronic gearing (single-pass or handwheel).
  * `TAPER`: Automated multi-pass roughing and finishing cycle with clearance retreats and step passes.
* **Documented in:** [`ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md).

### ADR 05: Conversational Fastener Wizard & Skim Pass Verification
* **Context:** Inexperienced operators frequently miscalculate pitch diameters, thread depths, or runout, leading to scrapped parts or tool collisions.
* **Decision:** Implemented a 4-stage conversational Fastener Wizard (ISO Metric, Unified National UN, NPT, BSPT) paired with a human-in-the-loop Skim Pass verification workflow:
  1. Operator takes a light skim pass on the stock.
  2. Measures actual OD with a micrometer and inputs it.
  3. System automatically zero-calibrates tool offset and computes exact compound infeed depth ($h = 0.61343 \times P$).

### ADR 06: Cross-Mode "Prepare Stock" Bridge
* **Context:** Screw cutting requires exact stock preparation (turning the cylinder to nominal OD or machining a pipe taper cone). Forcing operators to re-enter coordinates across modes wasted time and caused errors.
* **Decision:** Created an intelligent cross-mode bridge: from the Threading or Taper wizard, a single tap transitions the machine directly into Turning or Facing with workpiece coordinates pre-populated, machining the stock to size before engaging screw cutting.

### ADR 07: Hardware Lathe Profile & Capability Bitmask
* **Context:** Hardware attachments (linear DRO scales, cross-slide motors, VFDs) were scattered across rigid `#define` flags (`HAS_DRO_X`, `ACTIVE_Y`). Firmware had to be recompiled for every lathe variation.
* **Decision:** Created `LatheConfiguration` with a 32-bit hardware capability bitmask (`LatheCapabilityFlags`). Connected clients query `P.caps` to dynamically gate features (e.g. hiding facing cycles if `CAP_MOTOR_X` is absent).
* **Documented in:** [`LATHE_MACHINE_PROFILE.md`](./LATHE_MACHINE_PROFILE.md).

### ADR 08: 4-Tier Properties & Settings Taxonomy (Sparse NVS Engine)
* **Context:** Legacy `Config.cpp` dumped every factory default into NVS flash on boot ("default pollution"). Firmware upgrades to acceleration or deadbands never reached existing machines because stale values were frozen in flash.
* **Decision:** Enforced a clean 4-tier separation (`machine.*`, `sys.*`, `prefs.*`, `client.*`). Adopted the **Dynamic Default vs. Sparse User Override** paradigm: NVS only stores explicitly customized keys. A pristine machine consumes 0 bytes of flash overrides and inherits firmware improvements automatically.
* **Documented in:** [`ELS_PROPERTIES_AND_SETTINGS.md`](./ELS_PROPERTIES_AND_SETTINGS.md).

### ADR 09: LittleFS JSON Profile Catalog & Dynamic Schema Introspection
* **Context:** Baking a museum catalog of commercial lathe models into C++ flash caused ROM bloat. Furthermore, adding new hardware properties (e.g. stepper vs. servo, stallguard thresholds) historically required updating every client app.
* **Decision:**
  1. Compiled-in ROM holds only **one** failsafe baseline (`getDefaultProfile()`, WM210V 8×16).
  2. All stock and custom machine presets live in LittleFS (`/presets/profiles.json`).
  3. The controller serves a standard **JSON Schema (draft-07 / 2020-12)** document (`/schemas/lathe-profiles.schema.json`) with conditional `if/then` dependencies.
  4. Clients dynamically render settings forms from the schema with zero MCU computation and zero client software updates when firmware introduces new properties.
* **Documented in:** [`LATHE_MACHINE_PROFILE.md`](./LATHE_MACHINE_PROFILE.md).

### ADR 10: Standalone Profiles ("Clone & Own") vs. Slicer-Style Inheritance
* **Context:** Consideration was given to whether custom profiles should inherit from built-in profiles and only store diffs (similar to 3D slicers like SuperSlicer).
* **Decision:** Rejected profile inheritance in favor of **complete, standalone profiles ("Clone & Own")**:
  * Eliminates "spooky action at a distance" where an upstream preset update silently changes a machine's motion parameters.
  * Eliminates dangling reference errors when exporting or sharing profile packs.
  * Eliminates recursive resolution overhead on the ESP32 microcontroller stack.
  * Runtime tuning deltas are cleanly isolated in NVS (`machine.preset`), while profiles in the catalog remain 100% self-contained and portable.
* **Documented in:** [`LATHE_MACHINE_PROFILE.md`](./LATHE_MACHINE_PROFILE.md).
