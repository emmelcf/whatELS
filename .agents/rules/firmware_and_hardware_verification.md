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
  - USB keyboard emulation is disabled; ESP32-S3 USB CDC auto-resets via DTR/RTS (no manual button presses needed).
  - Autonomous agent compile & upload authorized: `pio run -e esp32s3` then `pio run -e esp32s3 -t upload`.
  - Allow 5–10s for board reboot and Wi-Fi reconnection (`whatels.local`).
