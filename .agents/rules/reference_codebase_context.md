# Reference Codebase Context & Monolithic Predecessor Rules

> **Core Purpose:** Guide future AI agents on the historical context, purpose, and strict rules regarding the original `.ino` files located in `reference/`, and how they relate to the active modular codebase in `src/`.

---

## 1. Architectural History & Relationship

1. **The Original Monolithic Implementation:**
   * The files in `reference/` (specifically `h5_monolithic.ino` and `H5_DRO_V104personal.ino`) are the original upstream single-file Arduino sketches (~7,000+ lines) for the NanoELS H5 controller.
   * In the original monolithic sketch, hardware pin definitions, FreeRTOS tasks, optical encoder pulse counting, stepper pulse generation, canned cycle state machines (threading, turning, taper, facing), Nextion display serial protocol, and early web servers were all colocated in a single file with dozens of shared global variables.

2. **The Active Modular Codebase (`src/`):**
   * The active, compiled codebase is located in `src/` and built via PlatformIO (`platformio.ini`).
   * The monolithic code was refactored into distinct, decoupled subsystems:
     * `src/motion/` — Real-time motion engine, `Axis` control, stepper pulse generation, linear DRO scale decoding, and canned cycles (`modeTurn`, `modeThread`, `modeCone`, etc.).
     * `src/core/` — `MachineModel` (canonical state invariants), `SettingsService` (NVS persistence), `LatheProfile`, and hardware pin definitions (`Config.h`).
     * `src/input/` — Spindle optical encoder PCNT management, MPG handwheels, joystick, and keyboard inputs.
     * `src/web/` — WebServer, WebSockets, REST endpoints, `CommandDispatcher`, and domain telemetry serializers.
     * `src/display/` — `DisplayModel`, `RichClientAdapter`, and Nextion display drivers.

---

## 2. Mandatory Agent Rules

### Rule 1: Strict Immutability of `reference/`
* **NEVER edit, modify, or reformat files in `reference/`.**
* These files are preserved historical artifacts. They are excluded from compilation and must remain frozen.

### Rule 2: Active Code Edits Must Target `src/` (or `clients/`)
* All feature additions, bug fixes, refactoring, and optimizations belong strictly in `src/` (firmware) or `clients/` (user interfaces).
* Never suggest building or flashing files from `reference/`.

### Rule 3: Use `reference/` as the Behavioral Ground Truth (Rosetta Stone)
* When implementing, verifying, or debugging complex lathe operations:
  * **Spindle Synchronization & Gearing:** Consult `reference/h5_monolithic.ino` to cross-reference Bresenham fractional gearing math, encoder pulse accumulation, and modulo math.
  * **Threading Infeed & Lead Angle Calculation:** Check the original pass progression algorithms and spindle index synchronization.
  * **Hardware Register Settings & Quirks:** Check how PCNT filters, timer prescalers, and direction setup delays were originally configured.

### Rule 4: Acknowledge Modern Evolution & Intentional Divergence
* The reference `.ino` files represent the **original baseline**, but functionality has been significantly expanded, modernized, and intentionally modified (e.g., lock-free Tier 2 state model, multi-client WebSockets, machine physical geometry profiles, linear DRO scales, Touch DRO UI).
* **`docs/` is the Authority for Modern Behavior:** Intentional expansions and architectural shifts are documented across `docs/` (e.g. `docs/CONTROLLER_ARCHITECTURE.md`, `docs/ELS_CORE_STATE_MODEL.md`, `docs/modes/`).

### Rule 5: Discrepancy Protocol — Ask Before Assuming
* If you find a behavior in `src/` that diverges from `reference/h5_monolithic.ino` and is **not explicitly explained in `docs/`**:
  * **Do NOT assume** `src/` has a bug and blindly revert it to the monolithic `.ino` logic.
  * **Do NOT assume** the undocumented divergence was intentional and proceed on guesswork.
  * **MANDATORY ACTION:** Pause and ask the user for clarification on the intended behavior.

### Rule 6: Close the Documentation Loop
* Whenever user clarification resolves an undocumented behavior, architectural divergence, or newly decided feature rule:
  * **Immediately capture the clarification in the appropriate document under `docs/`** (or `.agents/rules/`).
  * Never leave clarified behavior ephemeral in chat memory. Progressively eliminate documentation gaps.

---

## 3. Subsystem Cross-Reference Guide

When tracing legacy logic from `reference/h5_monolithic.ino` into the active modular architecture:

| Legacy Monolithic Component (`reference/h5_monolithic.ino`) | Active Modular Location (`src/`) |
| :--- | :--- |
| Global pin `#define`s (`ENC_A`, `Z_STEP`, `Z_DIR`, etc.) | [`src/core/Config.h`](file:///home/emil/code/nanoels/src/core/Config.h) |
| Global variables (`pos`, `motorPos`, `pendingPos`, `dupr`) | [`src/motion/Axis.h`](file:///home/emil/code/nanoels/src/motion/Axis.h) & [`src/core/MachineModel.h`](file:///home/emil/code/nanoels/src/core/MachineModel.h) |
| `stepTo()`, `moveAxis()`, `posFromSpindle()` | [`src/motion/Axis.cpp`](file:///home/emil/code/nanoels/src/motion/Axis.cpp) |
| Canned cycles (`modeTurn()`, `modeFace()`, `modeCone()`) | [`src/motion/Motion.cpp`](file:///home/emil/code/nanoels/src/motion/Motion.cpp) |
| Spindle PCNT interrupt & encoder delta handling | [`src/input/Input.cpp`](file:///home/emil/code/nanoels/src/input/Input.cpp) |
| Nextion display command formatting (`t0.txt=...`) | [`src/display/Display.cpp`](file:///home/emil/code/nanoels/src/display/Display.cpp) |
| Web UI handlers & WebSocket status publishing | [`src/web/WebServerModule.cpp`](file:///home/emil/code/nanoels/src/web/WebServerModule.cpp) |
| NVS EEPROM / Preferences storage | [`src/core/SettingsService.cpp`](file:///home/emil/code/nanoels/src/core/SettingsService.cpp) |
