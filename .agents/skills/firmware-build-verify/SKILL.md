---
name: firmware-build-verify
description: >-
  Builds and verifies the NanoELS ESP32-S3 firmware using PlatformIO.
  Use this skill whenever C++ source files or headers under src/ or platformio.ini are modified,
  or when checking for compilation errors without flashing hardware.
---

# Firmware Build & Verification Runbook

This skill validates the compilation of NanoELS firmware for the ESP32-S3 target without touching physical hardware or triggering flashing routines.

## 1. Compilation Command

Run the non-interactive build for the default `esp32s3` environment:

```bash
pio run -e esp32s3
```

> [!CAUTION]
> **Never add upload flags (`-t upload`) or run `esptool.py` autonomously.** The ESP32-S3 requires manual boot-mode button entry by the operator.

## 2. Expected Output & Metrics

A successful build produces output similar to:
```text
Building in release mode
...
RAM:   [==        ]  18.4% (used 60288 bytes from 327680 bytes)
Flash: [====      ]  36.2% (used 1187424 bytes from 3276800 bytes)
========================= [SUCCESS] Took ~15.20 seconds =========================
```

Check and report:
* RAM and Flash utilization percentages.
* Any compiler warnings emitted for newly edited files.

## 3. Common Build Failures & Troubleshooting

1. **Missing Domain Includes:**
   * If a `#include "Axis.h"` or `#include "Config.h"` fails, check `platformio.ini` `build_flags` for `-I src/<domain>`.
   * Standard domain include paths:
     `-I src/core`, `-I src/motion`, `-I src/input`, `-I src/web`, `-I src/display`, `-I src`.
2. **ISR Function Attribute Violations:**
   * High-frequency ISR functions must be marked `IRAM_ATTR` to execute from internal instruction RAM during SPI flash access.
   * Verify that functions called from ISRs do not allocate memory or call blocking FreeRTOS primitives.
3. **Linker Undefined Reference Errors:**
   * Verify that header declarations in `.h` match signatures in `.cpp`.
   * Ensure newly created `.cpp` files reside in `src/` so the PlatformIO build engine automatically compiles them.

## 4. Operator Handoff

Once the build exits with `[SUCCESS]`, notify the operator:
> "Firmware build succeeded. Please put the ESP32-S3 into boot mode (hold `BOOT`, pulse `RESET`) and run upload via `pio run -t upload`."
