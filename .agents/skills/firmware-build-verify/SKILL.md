---
name: firmware-build-verify
description: >-
  Builds and verifies the whatELS ESP32-S3 firmware using PlatformIO.
  Use whenever C++ source files or headers under src/ or platformio.ini are modified,
  or when checking for compilation errors without flashing hardware.
disable-model-invocation: true
---

# Firmware Build & Verification Runbook

## 1. Compilation Command
Run the non-interactive build for the default `esp32s3` environment:
```bash
pio run -e esp32s3
```

## 2. Validation & Metrics
- Check that build terminates with `[SUCCESS]`.
- Report RAM and Flash utilization percentages.
- Report any compiler warnings on newly touched files.

## 3. Common Failures & Fixes
- **Missing Includes:** Verify `-I src/<domain>` in `platformio.ini` `build_flags`.
- **IRAM Violations:** High-frequency ISR functions must be marked `IRAM_ATTR` and never allocate memory or call blocking FreeRTOS functions.
- **Linker Errors:** Ensure all new `.cpp` files reside in `src/` so PlatformIO compiles them.

## 4. Hardware Upload
- Flashing is governed by the firmware rule (`pio run -e esp32s3 -t upload`), which auto-resets the board via USB CDC without manual boot buttons.
