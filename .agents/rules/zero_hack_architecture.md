---
trigger: always_on
description: Dual-core ISR/mailbox boundaries, dumb-client protocol contract, and generated asset parity.
---

# Zero-Hack Architectural Invariants

1. **Dual-Core & ISR Boundary (`src/motion/kernel/` & `src/motion/hal/`):**
   - Stepper pulse generation (`Esp32StepperHAL`), encoder counting (`Esp32SpindleEncoderHAL`), and trajectory ticks must obey `docs/motion/CONCURRENCY_AND_TIMING.md`.
   - Cross-core communication between `src/core/CommandDispatcher` and `MotionEngine` must strictly traverse `MotionCommandPacket.h` and `TelemetryMailbox.h`—never mutate kernel state directly from web or client threads.
   - Never introduce symptom-masking delays (`delay()`, `time.sleep()`), magic constants, or silent error swallowing.

2. **Dumb-Client Protocol Contract (`ADR-0002` & `ADR-0003`):**
   - `clients/web_ui/` (`whatels-state.js`, `whatels-service.js`, `lathe-controller.js`) must remain a reactive presentation and intent-dispatch layer.
   - Never duplicate motion kinematics, pitch/ratio math, or soft-limit validation in JavaScript. All commands must conform to `docs/protocol/CANONICAL_COMMAND_REFERENCE.md`.
   - Maintain zero bundler bloat: strictly vanilla JS and `alpine.min.js`.

3. **Generated Asset Parity (`data/` & `scripts/`):**
   - Never hand-edit generated headers such as `src/web/SystemPresetsJson.h`. Modify source JSONs in `data/presets/` or `data/schemas/` and run `scripts/generate_presets.py` or `scripts/embed_controller_assets.py`.

4. **Engineering Integrity & Emergency Brake:**
   - Expediency is never an excuse to compromise code quality. Zero silent error swallowing (including empty `catch` blocks), zero symptom-masking delays, and zero mutating foreign instance state.
   - If avoiding an architectural compromise requires altering an established contract or boundary: **PULL THE EMERGENCY BRAKE** (state issue, root cause, options, recommendation) and wait for operator decision.

