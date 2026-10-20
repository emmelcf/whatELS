# Reference Materials & Monolithic Predecessors

This directory contains preserved historical artifacts from the original upstream [NanoELS H5 project](https://github.com/kachurovskiy/nanoels).

> **IMPORTANT NOTICE FOR DEVELOPERS & AI AGENTS:**  
> The files in this folder are **read-only historical references**. They are **not** compiled as part of the PlatformIO build and must **never** be edited or flashed to hardware. All active firmware development occurs in [`src/`](../src/).

---

## 1. Directory Contents

| File | Description |
| :--- | :--- |
| **`h5_monolithic.ino`** | The original, full-featured monolithic Arduino sketch (~7,000 lines) containing the complete upstream electronic lead screw (ELS) implementation, Nextion screen driver, motion planner, and web server in a single file. |
| **`H5_DRO_V104personal.ino`** | A historical DRO-specific development variant of the H5 sketch. |
| **`NANOELS_H5_README.md`** | The upstream repository README, detailing original bill of materials (BOM), schematics, PCB ordering instructions, and wiring diagrams. |

---

## 2. Relationship to the Active Modular Codebase (`src/`)

The active firmware in `src/` represents a comprehensive architectural refactoring of `h5_monolithic.ino` into clean, testable, and decoupled modules:

* **Motion Planning & Stepping:** Extracted from the monolithic loop into [`src/motion/`](../src/motion/) (`Motion.cpp`, `Axis.cpp`).
* **Canonical State & Invariants:** Converted raw global variables into the formal state model in [`src/core/MachineModel.h`](../src/core/MachineModel.h) and [`src/core/SettingsService.h`](../src/core/SettingsService.h).
* **Hardware & Pin Definitions:** Isolated into [`src/core/Config.h`](../src/core/Config.h).
* **Input & Encoder Capture:** Isolated into [`src/input/Input.cpp`](../src/input/Input.cpp).
* **Communications & Web Server:** Separated into [`src/web/`](../src/web/).
* **Display Abstraction:** Refactored into [`src/display/`](../src/display/) (`DisplayModel.cpp`, Nextion drivers).

---

## 3. How to Use This Directory

Consult `h5_monolithic.ino` when:
* **Verifying Complex Lathe Mathematics:** Checking original Bresenham gearing ratios, fractional step accumulation, or multi-start thread pass sequencing.
* **Investigating Hardware Quirks:** Cross-referencing ESP32-S3 hardware register initializations (PCNT filtering, timer prescalers, direction pin setup delays).
* **Clarifying Intent:** When active modular logic in `src/` appears ambiguous, check how the original monolithic firmware solved the problem on live physical lathes.

---

## 4. Modern Evolution & Discrepancy Protocol

While `reference/` serves as the baseline:
1. **Behavioral Expansions:** whatELS has intentionally expanded beyond the original sketch (e.g. multi-client WebSocket streaming, lock-free state engine, machine profile geometry, Touch DRO UI). Authoritative modern behaviors are described in [`docs/`](../docs/).
2. **If Uncertain, Ask:** If code in `src/` diverges from `reference/` and the rationale is not explicitly documented in `docs/`, developers and AI agents must **ask for clarification** rather than making assumptions or blindly reverting code.
3. **Update Documentation:** Once user clarification resolves any discrepancy, the clarified behavior must immediately be recorded in the appropriate document under `docs/`.
