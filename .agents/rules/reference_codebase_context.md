---
trigger: model_decision
description: Context, algorithms (Bresenham math, threading progression), and read-only invariants for legacy reference/ Arduino sketches.
---

# Reference Codebase Context & Monolithic Predecessor Rules

- **Strict Immutability:** NEVER edit, modify, format, build, or flash files in `reference/`. They are frozen historical artifacts.
- **Ground Truth Algorithm Reference (Rosetta Stone):**
  - Consult `reference/h5_monolithic.ino` for Bresenham fractional gearing math, encoder pulse accumulation, and threading infeed progressions.
- **Modern Authority:**
  - `docs/` governs modern modular behavior (ADR 15, Tier 2 state, linear DRO, WebSockets).
  - If `src/` diverges from `reference/` and is undocumented in `docs/`, ask operator before assuming intent.
- **Legacy to Modular Subsystem Mapping:**
  - Pins: `src/core/Config.h` | State (`pos`, `dupr`): `MachineModel.h` & `AxisState.h`
  - Motion / Stepper: `MotionEngine.cpp` & `Esp32StepperHAL.cpp`
  - Spindle PCNT / Encoder: `Esp32SpindleEncoderHAL.cpp` | Cycles: `src/motion/modes/`
  - Display / Clients: `src/core_client/` | Web / REST: `WebServerModule.cpp` & `CommandDispatcher.cpp`
  - Preferences: `SettingsService.cpp` & `PersistentSetting.h`
