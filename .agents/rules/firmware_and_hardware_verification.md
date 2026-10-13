# Firmware Architecture & Hardware-in-the-Loop Verification Rules

> **Core Purpose:** Enforce real-time embedded safety invariants for NanoELS firmware (`src/`) and establish safe, human-in-the-loop protocols for flashing hardware and conducting live lathe testing.

---

## 1. Embedded Real-Time & C++ Invariants

1. **ISR Safety (Zero Heap, Zero Blocking in Interrupt Context):**
   * Stepper pulse generation and spindle encoder ISRs (`src/motion/`) execute at high frequencies.
   * **Strictly Prohibited in ISRs:**
     * Dynamic memory allocation (`new`, `malloc`, `realloc`, Arduino `String`).
     * Blocking delays (`delay()`, `delayMicroseconds()` beyond tiny hardware settle times, FreeRTOS blocking queues).
     * Serial printing (`Serial.print`, `DEBUG_PRINT`).
     * Floating-point operations where integer or fixed-point arithmetic is required for determinism.
2. **Tier 2 Lock-Free State Architecture:**
   * All machine parameters, profile properties, and mode configuration registers must be accessed through `SettingsService` and canonical accessors.
   * Never bypass the state model to manipulate raw legacy globals.
   * Lock-free synchronization patterns must be preserved to guarantee zero contention between the real-time motion core and Wi-Fi/Web server tasks.
3. **GPIO Pin & Hardware Definition Freezing:**
   * Pin assignments in `src/core/Config.h` map to physical PCB traces and level shifters on the machine.
   * Never reassign GPIO pins without explicit operator discussion and confirmation.
4. **Mandatory PersistentSetting<T> for Persistent Storage (Zero Flash Wear & RAM Caching):**
   * All persisted settings and preferences across the entire firmware (operator preferences, WiFi, keyboard bindings, hardware overrides) MUST use the `PersistentSetting<T>` observable property template (`src/core/PersistentSetting.h`).
   * **Strictly Prohibited:** Never write raw, ad-hoc `Preferences.put*()` calls or read directly from flash on runtime getters.
   * `PersistentSetting<T>` guarantees single-read boot caching in RAM, deterministic dirty-checking to eliminate flash wear (`if (_value == newVal) return false;`), automatic debug suppression logging, and decoupled `onChanged` event hooks.

---

## 2. Hardware Flashing & Bootloader Gate (No Autonomous Uploads)

1. **Manual Boot Mode Constraint:**
   * The ESP32-S3 board requires manual physical intervention (holding `BOOT` and pulsing `RESET`) to enter the ROM bootloader for UART/USB flashing on `/dev/ttyACM2`.
2. **Strict Upload Prohibition for Agents:**
   * The AI agent must **NEVER** autonomously execute upload commands (`pio run -t upload` or `esptool.py`).
3. **Compilation & Operator Handoff Protocol:**
   * When firmware code in `src/` is modified:
     1. Compile and verify locally: `pio run -e esp32s3`.
     2. Report compilation outcome (RAM/Flash usage, warnings, or errors).
     3. Prompt the operator to flash the physical hardware:
        > *"Firmware compiled cleanly. Please enter boot mode (hold BOOT, pulse RESET) and upload via `pio run -t upload`, then confirm when the board has booted."*
     4. Wait for operator confirmation before proceeding with any live integration tests.

---

## 3. Web UI Verification: Live System Preference vs. Emulator

1. **Live Hardware as Primary Source of Truth:**
   * Testing against the physical ESP32-S3 over Wi-Fi / WebSocket is the **authoritative gold standard**.
   * Only the live system can validate true end-to-end timing, WebSocket framing, LittleFS asset serving, and encoder-synchronized motion.
2. **Role of the Emulator (`nanoels-emulator.js`):**
   * The emulator is an auxiliary, offline development tool intended exclusively for quick CSS/DOM layout rendering and standalone component styling.
   * It is **never** a substitute for live controller verification.
3. **Mandatory Emulator Synchronization:**
   * Whenever wire protocol messages (`?get=state`, WebSocket telemetry packets) or state schemas change in firmware, `clients/web_ui/nanoels-emulator.js` must be updated concurrently to prevent emulator drift.
4. **Operator Physical Stimulus Collaboration:**
   * Features requiring physical inputs (spindle rotation, MPG handwheel stepping, limit trips) cannot self-trigger.
   * When verifying these behaviors, prompt the operator with clear, single-step physical actions:
     * *"Please rotate the spindle chuck by hand (or run spindle at ~200 RPM) to verify RPM readout."*
     * *"Please click the MPG handwheel 5 clicks in +Z to confirm incremental stepping."*
