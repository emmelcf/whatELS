# whatELS Testing & Host Seam Architecture

> **Status:** Active Testing Index
> **Master Blueprint:** [`WHATELS_MASTER_ARCHITECTURE.md`](../WHATELS_MASTER_ARCHITECTURE.md)
> **Governing ADR:** [`ADR-0005`](../adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md)
> **Settings Spec:** [`ELS_PROPERTIES_AND_SETTINGS.md`](../state/ELS_PROPERTIES_AND_SETTINGS.md)

---

## 1. Test Suites Overview

| Suite | Location | Execution Target | Documentation |
| :--- | :--- | :--- | :--- |
| **Native Host Unit Tests** | `test/*.cpp` | Host (`g++` / PlatformIO `native`) | Pure C++ seam boundaries (this document) |
| **Hardware-in-the-Loop (HIL)** | `tests/hil/` | Physical ESP32-S3 via WebSocket | [`HIL_TEST_HARNESS_SPECIFICATION.md`](HIL_TEST_HARNESS_SPECIFICATION.md) |

---

## 2. Pure C++ Seam Discipline (Zero Fake Framework Mocks)

Per `AGENTS.md` (Tier 1 Invariant — Hardware Ground Truth & Zero Fake Mocks), host unit tests run strictly against pure C++ abstract interfaces. Vendor headers (`Arduino.h`, `Preferences.h`, `freertos/*`) are forbidden in `test/`.

### 2.1 Persistent Settings Storage Seam (`ISettingsStorage` & `MockSettingsStorage`)

To decouple `SettingsService` and `PersistentSetting<T>` from ESP32 `Preferences.h` NVS flash APIs, persistent key-value storage is abstracted behind a pure C++ boundary:

- **Interface:** [`src/core/ISettingsStorage.h`](../../src/core/ISettingsStorage.h) (`whatels::core::ISettingsStorage`)
  - **Lifecycle:** `begin(name, readOnly)`, `end()`, `clear()`, `remove(key)`, `isKey(key)`
  - **Typed Getters:** `getBool`, `getU8`, `getU16`, `getI32`, `getFloat`, `getString` (return `true` if key exists and populate `outVal`/`outBuf`; `false` if absent)
  - **Typed Putters:** `putBool`, `putU8`, `putU16`, `putI32`, `putFloat`, `putString` (return `true` on success; `false` on error)
- **Host Test Double:** [`test/mocks/MockSettingsStorage.h`](../../test/mocks/MockSettingsStorage.h) (`whatels::core::MockSettingsStorage`)
  - In-memory `std::map<std::string, std::map<std::string, std::string>>` keyed by namespace (`activeNs`, default `"prefs"`).
  - Enforces `opened` and `readOnly` state checks on mutating operations (`clear`, `remove`, `put*`).
  - Provides pre-seeding helpers (`seedI32`, `seedBool`, `seedFloat`, `seedString`) for deterministic unit test setup without disk or flash I/O.

### 2.2 Other Pure HAL & System Seams

| Interface | Host Test Double | Domain |
| :--- | :--- | :--- |
| [`src/core/ISystemInfoHAL.h`](../../src/core/ISystemInfoHAL.h) | [`test/mocks/MockSystemInfoHAL.h`](../../test/mocks/MockSystemInfoHAL.h) | Chip/heap/uptime system info |
| [`src/motion/hal/IStepperHAL.h`](../../src/motion/hal/IStepperHAL.h) | [`src/motion/hal/MockStepperHAL.h`](../../src/motion/hal/MockStepperHAL.h) | Stepper pulse generation |
| [`src/motion/hal/ISpindleEncoderHAL.h`](../../src/motion/hal/ISpindleEncoderHAL.h) | [`src/motion/hal/MockSpindleEncoderHAL.h`](../../src/motion/hal/MockSpindleEncoderHAL.h) | Spindle quadrature & simulation |
