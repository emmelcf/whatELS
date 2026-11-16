# Controller Logging Standard

> **Scope:** All C++ firmware under `src/`.  
> **Authority:** Referenced by `.agents/rules/firmware_and_hardware_verification.md`.

---

## 1. API, Tags & Host-Test Portability

* **Use `esp_log.h` exclusively:** `ESP_LOGE`, `ESP_LOGW`, `ESP_LOGI`, `ESP_LOGD`, `ESP_LOGV`. Never use `Serial.print*`, `printf`, or `std::cout` in `src/`.
* **File-local `TAG`:** Define one short PascalCase tag per `.cpp` file (`static const char* TAG = "MotionEngine";`). Avoid spaces so `esp_log_level_set(TAG, ...)` can target individual subsystems.
* **Host-test portability:** Files that compile on native host (`src/motion/kernel/`, `src/motion/modes/`, pure `src/core/`) must include `core/PlatformCompat.h` (which provides no-op `ESP_LOG*` stubs when `ESP32`/`ARDUINO` are not defined) or guard `<esp_log.h>` with `#if defined(ESP32) || defined(ARDUINO)`.

---

## 2. Real-Time & Concurrency Boundaries (Hard Rules)

* **Zero ISR / `IRAM_ATTR` logging:** Never call `ESP_LOG*` inside hardware ISRs (`Esp32StepperHAL`, `Esp32SpindleEncoderHAL`, `DRO_Scale::pcnt_on_reach`, `MotionEngine::onSyncTimerAlarm`).
* **Zero 4 kHz tick logging:** Never log inside `MotionEngine`'s 250 µs (`SYNC_TIMER_PERIOD_US`) loop.
* **Cross-core status observation:** Periodic motion state must be logged from a low-priority task (e.g., Arduino `loop()` or Core 0 `taskWiFi`) by reading `TelemetryMailbox` (`MotionTelemetrySnapshot`), never from Core 1's real-time loop.

---

## 3. Log Level Matrix

| Level | When to Use | Examples |
| :--- | :--- | :--- |
| **`E` (Error)** | Hardware/peripheral init failure, dropped data, or invariant violation. Must include error code/value. | `pcnt_new_unit` / RMT / gptimer init failure, LittleFS/NVS mount failure, `MotionCommandPacket` queue full. |
| **`W` (Warning)** | Recoverable anomaly or safety intervention. | Soft/hard limit hit, spindle stall (`RPM < 10` during slaved feed), jog lease timeout, malformed command, skipped/disabled hardware capability. |
| **`I` (Info)** | **Default compile-time level.** Boot milestones, hardware init summaries, low-frequency state transitions (~≤ 1 Hz). | Boot sequence, HAL pin/config summary, mode transitions, E-stop/feed-hold engage & clear, WS client connect/disconnect. |
| **`D` (Debug)** | Diagnostic trace for active troubleshooting. | Incoming WS/Serial commands, `MotionCommandPacket` dispatch, `configureAxis` derived math, rate-limited `MotionTelemetrySnapshot` diffs. |
| **`V` (Verbose)** | High-volume raw data dumps; enable per-TAG only when isolating a specific bug. | Raw encoder/DRO counts, byte-level parser trace. |

---

## 4. Formatting & Rate-Limiting

* **Include identifiers, units, and error codes:** State `axis=Z|X`, units (`du`, `steps`, `us`, `rpm`, `mm`), and `err=0x%x` / `esp_err_to_name(err)`.
* **Use `key=value` pairs** for state/telemetry logs:
  ```cpp
  ESP_LOGD(TAG, "snap mode=%u moving=%d hold=%d z_steps=%ld x_steps=%ld rpm=%lu lim=%u",
           snap.motionMode, snap.isMoving, snap.isFeedHold,
           (long)snap.zPosSteps, (long)snap.xPosSteps,
           (unsigned long)snap.spindleRpm, snap.activeLimitSlot);
  ```
* **Rate-limit + change-gate periodic logs:** Any repeating status log must check both `(millis() - lastMs >= intervalMs)` and a meaningful state delta so an idle machine produces zero log spam.

---

## 5. Configuration Knobs

* **`CORE_DEBUG_LEVEL`** (`platformio.ini`): Compile-time ceiling (default `ARDUHAL_LOG_LEVEL_INFO`). Raise to `ARDUHAL_LOG_LEVEL_DEBUG` when troubleshooting, or keep `DEBUG` ceiling and filter at runtime with `esp_log_level_set(TAG, ESP_LOG_DEBUG)`.
* **`MOTION_STATUS_LOG_MS`** (`Config.h` / `platformio.ini`): Compile-time interval in ms for rate-limited status diff logging (`0` = disabled).
