# whatELS — Layered Spec-to-Code Traceability Matrix

Companion to [spec_drift_report.md](./spec_drift_report.md). Maps every architectural layer and active spec in [`docs/`](../../docs) (excluding `docs/archive/`) to its corresponding implementation, data, client, and test files.

---

## 1. Master Architecture & ADRs

| Spec Document | Mapped Code / Config | Coverage Status | `@see` Header Status |
| :--- | :--- | :--- | :--- |
| [`WHATELS_MASTER_ARCHITECTURE.md`](../../docs/WHATELS_MASTER_ARCHITECTURE.md) | [`src/main.cpp`](../../src/main.cpp) | **Partial / Drifted** (`loop()` uses legacy `isOn`/`dupr`) | N/A (`.cpp`) |
| [`adr/0001-motion-invocation-and-gearbox-consolidation.md`](../../docs/adr/0001-motion-invocation-and-gearbox-consolidation.md) | [`CoreCommands.h`](../../src/core/CoreCommands.h), [`StandardCommandParser.cpp`](../../src/core_client/StandardCommandParser.cpp), [`TelemetrySerializer.cpp`](../../src/core/TelemetrySerializer.cpp) | **Aligned** | Missing `@see` |
| [`adr/0002-command-surface-rationalization-and-dumb-client-decoupling.md`](../../docs/adr/0002-command-surface-rationalization-and-dumb-client-decoupling.md) | [`StandardCommandParser.cpp`](../../src/core_client/StandardCommandParser.cpp), [`CoreCommandService.cpp`](../../src/core/CoreCommandService.cpp) | **Partial** (`CMD.feedNudge` missing) | Missing `@see` |
| [`adr/0003-command-result-and-baseline-client-architecture.md`](../../docs/adr/0003-command-result-and-baseline-client-architecture.md) | [`CommandResult.h`](../../src/core/CommandResult.h), [`ClientEndpoint.h`](../../src/core_client/ClientEndpoint.h), [`ClientManager.cpp`](../../src/core_client/ClientManager.cpp), [`NativeClient.cpp`](../../src/core_client/clients/native/NativeClient.cpp) | **Partial** (REST, `GrblClient`, `DroClient` missing) | Missing `@see` |
| [`adr/0004-pluggable-transports-and-webserial.md`](../../docs/adr/0004-pluggable-transports-and-webserial.md) | [`ITransport.h`](../../src/core_client/transports/ITransport.h), [`WebSocketTransport.h`](../../src/core_client/transports/WebSocketTransport.h) | **Spec No Code** (`SerialTransport` missing) | Missing `@see` |
| [`adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md`](../../docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md) | [`WifiService.cpp`](../../src/core/WifiService.cpp), [`HttpServer.cpp`](../../src/core/network/HttpServer.cpp), [`ISystemInfoHAL.h`](../../src/core/ISystemInfoHAL.h), [`Esp32SystemInfoHAL.cpp`](../../src/core/Esp32SystemInfoHAL.cpp), [`WebServerModule.cpp`](../../src/web/WebServerModule.cpp) | **Partial** (`HttpServer` unwired; `WebServerModule` still runs Port 80 & 50 Hz WS) | Missing `@see` |

---

## 2. Protocol, Core & State Layer

| Spec Document | Mapped Code Files | Coverage Status | Notes |
| :--- | :--- | :--- | :--- |
| [`CANONICAL_COMMAND_REFERENCE.md`](../../docs/protocol/CANONICAL_COMMAND_REFERENCE.md) | [`StandardCommandParser.h/.cpp`](../../src/core_client/StandardCommandParser.cpp), [`CoreCommands.h`](../../src/core/CoreCommands.h), [`CoreCommandService.h/.cpp`](../../src/core/CoreCommandService.cpp) | **Aligned (Core CMD)** / **Drifted** | `startCycle`/`step` stubbed; `SETTINGS` response framing & `reset=ALL` diverge; `PRESET` unwired |
| [`ELS_CORE_STATE_MODEL.md`](../../docs/state/ELS_CORE_STATE_MODEL.md) | [`MachineModel.h/.cpp`](../../src/core/MachineModel.cpp), [`OperatorIntent.h`](../../src/core/OperatorIntent.h), [`TelemetrySerializer.h/.cpp`](../../src/core/TelemetrySerializer.cpp) | **Drifted** | `IntentState` uses `feedReverse` / `I.feedRev=` vs `direction`/`targetDistDu` |
| [`ELS_PRESET_CATALOG.md`](../../docs/state/ELS_PRESET_CATALOG.md) | [`PresetService.h/.cpp`](../../src/core/PresetService.cpp) | **Unwired / Spec Conflict** | `PRESET.*` not registered in parser; conflicts with ADR-0005 |
| [`ELS_PROPERTIES_AND_SETTINGS.md`](../../docs/state/ELS_PROPERTIES_AND_SETTINGS.md) | [`SettingsService.h/.cpp`](../../src/core/SettingsService.cpp), [`ISettingsStorage.h`](../../src/core/ISettingsStorage.h), [`PersistentSetting.h`](../../src/core/PersistentSetting.h), [`Config.h/.cpp`](../../src/core/Config.h), [`BoardPins.h`](../../src/core/BoardPins.h) | **Drifted** | Multi-dot `SETTINGS.preset.*`/`client.*` unparseable; raw `Preferences` calls (`ISettingsStorage.h` added in `368fdf2`, unwired); `ControllerConfig` partial |
| *(No Active Spec)* | [`src/input/UsbKeyboard.h/.cpp`](../../src/input/UsbKeyboard.h) | **CODE_NO_SPEC** | HID-to-PS2 & `ENABLE_USB_KEYBOARD` undocumented |

---

## 3. Motion Kernel, HAL & Machine Profile Layer

| Spec Document | Mapped Code / Data Files | Coverage Status | Notes |
| :--- | :--- | :--- | :--- |
| [`motion/README.md`](../../docs/motion/README.md) | `src/motion/{hal,kernel,contract}/*` | **Partial** | `IDROScaleHAL`, `IEndstopHAL`, `Esp32EndstopHAL`, `cycles/*` missing |
| [`CONCURRENCY_AND_TIMING.md`](../../docs/motion/CONCURRENCY_AND_TIMING.md) | [`MotionCommandPacket.h`](../../src/motion/contract/MotionCommandPacket.h), [`TelemetryMailbox.h`](../../src/motion/contract/TelemetryMailbox.h), [`MotionEngine.cpp`](../../src/motion/kernel/MotionEngine.cpp) | **Major Drift** | 1 kHz vs 4 kHz; software step accumulator vs `startPulseTrain`; missing watchdogs & `.cpp` `IRAM_ATTR` |
| [`MODE_INTEGRATION_CONTRACT.md`](../../docs/motion/MODE_INTEGRATION_CONTRACT.md) | [`MotionCommands.h`](../../src/motion/contract/MotionCommands.h), [`MotionEngine.cpp`](../../src/motion/kernel/MotionEngine.cpp) | **Partial** | `StrokeParams` unimplemented; `syncIndex` & `accelMmSec2` ignored |
| [`HAL_SPECIFICATION.md`](../../docs/motion/HAL_SPECIFICATION.md) | [`StepTimingConfig.h`](../../src/motion/hal/StepTimingConfig.h), [`IStepperHAL.h`](../../src/motion/hal/IStepperHAL.h), [`Esp32StepperHAL.cpp`](../../src/motion/hal/Esp32StepperHAL.cpp), [`ISpindleEncoderHAL.h`](../../src/motion/hal/ISpindleEncoderHAL.h), [`Esp32SpindleEncoderHAL.cpp`](../../src/motion/hal/Esp32SpindleEncoderHAL.cpp), [`DRO_Scale.cpp`](../../src/motion/DRO_Scale.cpp) | **Partial / Drifted** | GPTimer vs RMT; `esp_rom_delay_us` busy-waits; `IDROScaleHAL` & `IEndstopHAL` missing; undocumented sim hooks |
| [`MOTION_ENGINE_SPECIFICATION.md`](../../docs/motion/MOTION_ENGINE_SPECIFICATION.md) | [`AxisGeometry.h`](../../src/motion/kernel/AxisGeometry.h), [`AxisId.h`](../../src/motion/kernel/AxisId.h), [`AxisState.h`](../../src/motion/kernel/AxisState.h), [`SoftLimitTable.cpp`](../../src/motion/kernel/SoftLimitTable.cpp), [`SpindleSynchronizer.cpp`](../../src/motion/kernel/SpindleSynchronizer.cpp), [`TrajectoryPlanner.cpp`](../../src/motion/kernel/TrajectoryPlanner.cpp), [`MotionConversions.h`](../../src/motion/MotionConversions.h) | **Partial** | Missing backlash compensation, runway check invocation, & `configureAxis(timing)` |
| *(No Spec)* | [`MockStepperHAL.h`](../../src/motion/hal/MockStepperHAL.h), [`MockSpindleEncoderHAL.h`](../../src/motion/hal/MockSpindleEncoderHAL.h) | **CODE_NO_SPEC** | Test doubles undocumented in `HAL_SPECIFICATION.md` |
| [`LATHE_MACHINE_PROFILE.md`](../../docs/machine/LATHE_MACHINE_PROFILE.md) | [`LatheProfile.h/.cpp`](../../src/motion/LatheProfile.cpp), [`lathe-profile.schema.json`](../../data/schemas/lathe-profile.schema.json), [`profiles.json`](../../data/presets/profiles.json) | **Major Drift** | Key mismatch (`direction_inverted` / `enable_active_low` vs `invert_dir` / `invert_enable`); `ControlsHardwareConfig` omitted |

---

## 4. Machining Modes Layer

| Mode | Spec Document(s) | Mapped Code Files | Coverage Status |
| :--- | :--- | :--- | :--- |
| **Mode Architecture** | [`ELS_MODE_ARCHITECTURE.md`](../../docs/modes/ELS_MODE_ARCHITECTURE.md), [`modes/README.md`](../../docs/modes/README.md) | [`IELSMode.h`](../../src/motion/modes/IELSMode.h), [`ModeRegistry.h/.cpp`](../../src/motion/modes/ModeRegistry.cpp) | **Drifted** (`CONE=57` missing; `PROPERTIES.modes` schema mismatch; `BaseMode`/`VisibilityPolicy` missing) |
| **Electronic Gearbox** | [`modes/gearbox/SPECIFICATION.md`](../../docs/modes/gearbox/SPECIFICATION.md) | [`UnifiedGearboxMode.h/.cpp`](../../src/motion/modes/composite/UnifiedGearboxMode.cpp) | **Implemented (with Drift)** (`GEARBOX_PARAMS` & `CAP_*` mismatch; legacy `CMD.mode`/`CMD.feedDupr` in spec) |
| **Taper & Chamfer** | [`modes/taper/SPECIFICATION.md`](../../docs/modes/taper/SPECIFICATION.md) | [`UnifiedGearboxMode.cpp`](../../src/motion/modes/composite/UnifiedGearboxMode.cpp) (ZX), [`ConePrimitive.cpp`](../../src/motion/modes/primitives/ConePrimitive.cpp) | **Partial** (Continuous ZX feed works; multi-pass `CMD.startCycle` stubbed) |
| **Turning** | [`modes/turning/SPECIFICATION.md`](../../docs/modes/turning/SPECIFICATION.md) | `IELSMode.h` (`TURNING=51` constant only) | **SPEC_NO_CODE** |
| **Facing** | [`modes/facing/SPECIFICATION.md`](../../docs/modes/facing/SPECIFICATION.md) | `IELSMode.h` (`FACING=52` constant only) | **SPEC_NO_CODE** |
| **Threading & Wizard** | [`modes/threading/SPECIFICATION.md`](../../docs/modes/threading/SPECIFICATION.md), [`WIZARD_WORKFLOW.md`](../../docs/modes/threading/WIZARD_WORKFLOW.md) | `IELSMode.h` (`THREADING=53` constant only) | **SPEC_NO_CODE** |
| **Cutoff / Parting** | [`modes/cutoff/SPECIFICATION.md`](../../docs/modes/cutoff/SPECIFICATION.md) | `IELSMode.h` (`CUTOFF=54` constant only) | **SPEC_NO_CODE** |
| **Slotting / Keyway** | [`modes/slotting/SPECIFICATION.md`](../../docs/modes/slotting/SPECIFICATION.md) | `IELSMode.h` (`SLOTTING=55` constant only) | **SPEC_NO_CODE** |
| **Half-Spheres** | [`modes/spheres/SPECIFICATION.md`](../../docs/modes/spheres/SPECIFICATION.md) | `IELSMode.h` (`SPHERE=56` constant only) | **SPEC_NO_CODE** |
| **Joystick / Pendant** | [`modes/joystick/SPECIFICATION.md`](../../docs/modes/joystick/SPECIFICATION.md) | *(none)* | **SPEC_NO_CODE** |
| **Mode Primitives** | *(Contradicts `ELS_MODE_ARCHITECTURE.md` §4.1.1)* | [`src/motion/modes/primitives/{Async,Cone,GearX,GearZ}Primitive.*`](../../src/motion/modes/primitives) | **CODE_NO_SPEC / Orphaned** (Registered in `ModeRegistry`, unused by `UnifiedGearboxMode`) |

---

## 5. Clients & Test Harnesses Layer

| Spec Document | Mapped Code / Test Files | Coverage Status | Notes |
| :--- | :--- | :--- | :--- |
| [`CLIENT_SDK_SPECIFICATION.md`](../../docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md) | [`whatels-client.d.ts`](../../clients/sdk/js/whatels-client.d.ts), [`clients/sdk/js/transports/*`](../../clients/sdk/js/transports) | **Partial** | `WhatelsClient`, `WebSerialTransport`, `clients/sdk/{cpp,python}` missing |
| [`ELS_CLIENT_ARCHITECTURE.md`](../../docs/clients/ELS_CLIENT_ARCHITECTURE.md), [`CLIENT_IMPLEMENTATION_GUIDE.md`](../../docs/clients/CLIENT_IMPLEMENTATION_GUIDE.md) | [`whatels-service.js`](../../clients/web_ui/whatels-service.js), [`lathe-controller.js`](../../clients/web_ui/lathe-controller.js) | **Drifted** | Missing `CLIENT.hello`; emits `CMD.on=1` & legacy modal setters; JS limit/pitch math (`AGENTS.md` §2.2) |
| [`HMI_DESIGN_GUIDELINES.md`](../../docs/clients/hmi/HMI_DESIGN_GUIDELINES.md), [`RICH_TOUCH_800X480.md`](../../docs/clients/hmi/RICH_TOUCH_800X480.md), [`ERGONOMICS.md`](../../docs/clients/web_ui/ERGONOMICS.md) | [`lathe_dro_whatels_controller.html`](../../clients/web_ui/lathe_dro_whatels_controller.html), [`lathe-controller.css`](../../clients/web_ui/lathe-controller.css) | **Partial / Drifted** | `338px` vs `270px` DRO col, nested status bar, `<36px` touch targets, `overflow-y: auto` |
| [`web_ui/OVERVIEW.md`](../../docs/clients/web_ui/OVERVIEW.md), [`INTERACTION_LEDGER.md`](../../docs/clients/web_ui/INTERACTION_LEDGER.md) | [`lathe-controller.js`](../../clients/web_ui/lathe-controller.js), [`whatels-service.js`](../../clients/web_ui/whatels-service.js), [`whatels-emulator.js`](../../clients/web_ui/whatels-emulator.js) | **Drifted** | `cycleMeasure`/`cycleStep`, `jogChamfer`, `CMD.spindleSim`, and DOM selector mismatches |
| *(No Active Spec)* | [`data/www/*`](../../data/www), [`lathe_dro_nanoels_controller.html`](../../clients/web_ui/lathe_dro_nanoels_controller.html) | **CODE_NO_SPEC** | WiFi/Settings/OTA portal & legacy redirect |
| [`HIL_TEST_HARNESS_SPECIFICATION.md`](../../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md) | [`tests/hil/*`](../../tests/hil) (7 suites, 27 tests) | **High Coverage (Minor Drift)** | `client.py` drifts on `SYS.get=info`, `telemetry_seq`, `K.perms`, `"undefined"` vs `"NONE"`, `encoder_ppr` |
| *(No Active Spec)* | [`test/*.cpp`](../../test), [`test/mocks/*`](../../test/mocks), [`tests/clients/transports.test.mjs`](../../tests/clients/transports.test.mjs) | **CODE_NO_SPEC / BOUNDARY** | Unit tests lack spec; `test/mocks/` contains forbidden `Arduino.h`/`Preferences.h`/`freertos/*` ([`MockSettingsStorage.h`](../../test/mocks/MockSettingsStorage.h) added in `368fdf2` as pure seam) |
