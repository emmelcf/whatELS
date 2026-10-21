---
trigger: model_decision
description: Embedded C++ real-time invariants for src/ (ISR safety, SettingsService, PersistentSetting) and hardware flashing.
---

# Firmware Architecture & Flashing Invariants

- **ISR Safety (Zero Heap, Zero Blocking, Zero Floats):**
  - High-frequency stepper/encoder ISRs execute from IRAM (`IRAM_ATTR`).
  - Strictly prohibited in ISRs: `new`/`malloc`, `String`, `delay()`, FreeRTOS blocking queues, `Serial.print`, floating-point math.
- **Lock-Free State & NVS Persistence:**
  - All machine parameters access state via `SettingsService` / canonical accessors (no raw legacy globals).
  - All persisted settings MUST use `PersistentSetting<T>` (`PersistentSetting.h`) for single-read RAM boot caching and dirty-checking. Never call raw `Preferences.put*()`.
- **GPIO Pin Freezing:**
  - Pin definitions in `src/core/BoardPins.h` (and re-exported by `src/core/Config.h`) map to physical PCB traces. Never reassign without confirmation.
- **Hardware Flashing Protocol:**
  - ESP32-S3 USB CDC auto-resets via DTR/RTS without boot buttons.
  - Flashing (`pio run -e esp32s3 -t upload`) automatically builds all dependencies before uploading.
- **Firmware-First Testing Invariant (Upload-Before-UI Gate):**
  - If any C++ files (`src/`), embedded assets (`data/presets/`, `SystemPresetsJson.h`), or `platformio.ini` are modified:
    1. **Upload Firmware:** Run `pio run -e esp32s3 -t upload` (builds and flashes in one step; wait 5–10s for reboot and Wi-Fi re-association).
    2. **Verify Firmware (HIL):** Run `controller-hil-test` (`python3 tests/hil/run_tests.py`) over WebSocket to verify motion, state, and protocol contracts.
    3. **UI Gate:** Testing or previewing the UI (`web-ui-preview`) is strictly prohibited until steps 1 and 2 pass. Never test UI against an un-flashed or unverified controller.

