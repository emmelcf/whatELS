# whatELS Drift Map — Subsystem ⇄ Docs ⇄ Source

> **Purpose:** The single mechanical artifact answering "which docs govern which source, and where does reality disagree?"
> **Regenerated:** 2026-11-03. Paths verified against the live tree that date.
> **Rule:** This map is *derived*, never hand-maintained. When in doubt, re-derive from the tree (Section R below) before acting on any row.
> **Role:** Pairing infrastructure only. The audit framework lives in [`drift-brief.md`](./drift-brief.md); its findings live in `docs/drift/<pair>.md`. This map is not a report.

---

## S. Canonical Subsystem Numbering (Authority: [`docs/WHATELS_MASTER_ARCHITECTURE.md`](../docs/WHATELS_MASTER_ARCHITECTURE.md))

| # | Subsystem | Core | Doc root | Source root |
| :-- | :-- | :-- | :-- | :-- |
| 1 | HAL & Hardware Timing | Core 1 | [`docs/motion/`](../docs/motion/) | `src/motion/hal/`, `src/core/` (HAL providers) |
| 2 | Kinematic Kernel & Motion | Core 1 | [`docs/motion/`](../docs/motion/) | `src/motion/kernel/`, `src/motion/contract/` |
| 3 | State Model & Persistence | Core 0 | [`docs/state/`](../docs/state/) | `src/core/` |
| 4 | Operational Mode Engine | Core 1 | [`docs/modes/`](../docs/modes/) | `src/motion/modes/` |
| 5 | Command Dispatcher & Transport | Core 0 | [`docs/clients/`](../docs/clients/), [`docs/protocol/`](../docs/protocol/) | `src/core_client/`, `src/core/network/` |
| 6 | HMI & Client Adapters | Core 0 | [`docs/clients/`](../docs/clients/) | `src/input/`, `clients/` (web, nextion) |
| B | Core↔core-client domain gateway (boundary bucket — **not** a renumbering) | Core 0→Core 1 | [`docs/protocol/`](../docs/protocol/) | `src/core/CoreCommands.h`, `CoreCommandService.h/.cpp`, `CommandResult.h`, `core_client/ClientContext.h`; internal analogue `motion/contract/MotionCommandPacket.h` |

`CoreCommandService.*` belongs to none of 1–6: it consumes a client type, owns the `[SAFE-01]` gate, and grants typed execution into 2 and 3. Filing it in any subsystem would break the tier rule (1–4 core · 5 core-client · 6 device-client). Its own bucket.

Cross-cutting (no single subsystem): [`docs/machine/`](../docs/machine/) (machine profile), [`docs/adr/`](../docs/adr/) (decision records), [`docs/testing/`](../docs/testing/), [`docs/archive/`](../docs/archive/).

> **Renumbering note:** pre-2026-11 documents label State/Persistence as "Subsystem 4" and Operational Modes as "Subsystem 3". The table above is authoritative; ADRs keep their historical numbers under the repair convention `[now Subsystem N]`.

---

## 1 · HAL & Hardware Timing

**Docs**
- [`docs/motion/HAL_SPECIFICATION.md`](../docs/motion/HAL_SPECIFICATION.md) — §2 stepper/servo timing, §2.1 `StepTimingConfig`, §3.1/§3.2 spindle encoder.

**Source (governing docblock cited in file)**
- `src/motion/hal/IStepperHAL.h` → HAL_SPEC §2.2
- `src/motion/hal/Esp32StepperHAL.h/.cpp` → HAL_SPEC §2
- `src/motion/hal/StepTimingConfig.h` → HAL_SPEC §2.1
- `src/motion/hal/ISpindleEncoderHAL.h` → HAL_SPEC §3.2
- `src/motion/hal/Esp32SpindleEncoderHAL.h/.cpp` → HAL_SPEC §3.1 & §3.2
- `src/motion/hal/MockSpindleEncoderHAL.h`, `MockStepperHAL.h` — test seams (no docblock citation)
- `src/motion/DRO_Scale.h/.cpp` — optical PCNT scales (no docblock citation → **gap**)
- `src/core/BoardPins.h` — pin map (no docblock citation → **gap**)
- `src/core/ISystemInfoHAL.h` → CANONICAL_COMMAND_REFERENCE §4.1 + ADR-0005 (lives in `core/`, is an S1 seam)

**Doc gaps to write:** DRO scale pulses-counters contract; `BoardPins.h` pin-map table.

---

## 2 · Kinematic Kernel & Motion

**Docs**
- [`docs/motion/MOTION_ENGINE_SPECIFICATION.md`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md) — §2.3 geometry/state, §3.1 soft limits, §3.2 limit drum, §4 trajectory, §5 spindle sync, §6 orchestrator.
- [`docs/motion/CONCURRENCY_AND_TIMING.md`](../docs/motion/CONCURRENCY_AND_TIMING.md) — §3.1 telemetry mailbox, §3.2 command packet queue, §2/§4 Core 1 timing.
- [`docs/motion/MODE_INTEGRATION_CONTRACT.md`](../docs/motion/MODE_INTEGRATION_CONTRACT.md) — the S2 ⇄ S4 boundary (§2.2 halt reasons, §3 motion event listener).

**Source (governing docblock cited in file)**
- `src/motion/kernel/AxisGeometry.h` → MOTION_ENGINE §2.3
- `src/motion/kernel/AxisState.h` → MOTION_ENGINE §2.3
- `src/motion/kernel/SoftLimit.h` → MOTION_ENGINE §3.1
- `src/motion/kernel/SoftLimitTable.h/.cpp` → MOTION_ENGINE §3.2
- `src/motion/kernel/TrajectoryPlanner.h/.cpp` → MOTION_ENGINE §4
- `src/motion/kernel/SpindleSynchronizer.h/.cpp` → MOTION_ENGINE §5
- `src/motion/kernel/MotionEngine.h/.cpp` → MOTION_ENGINE §6 + CONCURRENCY §2 & §4
- `src/motion/contract/TelemetryMailbox.h` → CONCURRENCY §3.1
- `src/motion/contract/MotionCommandPacket.h` → CONCURRENCY §3.2
- `src/motion/contract/MotionCommands.h` → MODE_INTEGRATION §2.2 & §3
- `src/motion/kernel/AxisId.h`, `src/motion/MotionConversions.h` — no docblock citation → **gap**

---

## 3 · State Model & Persistence

**Docs**
- [`docs/state/ELS_CORE_STATE_MODEL.md`](../docs/state/ELS_CORE_STATE_MODEL.md) — `MachineModel` domains `K/I/C/P`.
- [`docs/state/ELS_PROPERTIES_AND_SETTINGS.md`](../docs/state/ELS_PROPERTIES_AND_SETTINGS.md) — 4-tier properties, NVS engine.
- [`docs/state/ELS_PRESET_CATALOG.md`](../docs/state/ELS_PRESET_CATALOG.md) — ROM + LittleFS presets.
- [`docs/state/README.md`](../docs/state/README.md) — directory index.
- [`docs/machine/LATHE_MACHINE_PROFILE.md`](../docs/machine/LATHE_MACHINE_PROFILE.md) — `LatheConfiguration` (cross-cutting input to this subsystem).
- [`docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md`](../docs/adr/0005-protocol-rationalization-and-subsystem-5-decoupling.md) — seam policy.

**Source (governing docblock cited in file)**
- `src/core/MachineModel.h/.cpp` → ELS_CORE_STATE_MODEL (docblock citation **missing**; classify by location)
- `src/core/SettingsService.h/.cpp`, `src/core/PersistentSetting.h` → ELS_PROPERTIES_AND_SETTINGS (citation **missing**)
- `src/core/PresetService.h/.cpp` → `docs/ELS_PRESET_CATALOG.md` (docblock path **stale** — actual file is `docs/state/ELS_PRESET_CATALOG.md`)
- `src/core/TelemetrySerializer.h/.cpp` → ELS_CORE_STATE_MODEL telemetry export (citation **missing**)
- `src/core/OperatorIntent.h` → ELS_CORE_STATE_MODEL intent domain (citation **missing**)
- `src/core/CoreCommands.h`, `src/core/CoreCommandService.h/.cpp` → **bucket B** (domain gateway, see table S): docblock cites `SPECIFICATION_PROTOCOL_RATIONALIZATION_AND_SUBSYSTEM5.md`, which does not exist. Superseded by ADR-0005.
- `src/core/ISettingsStorage.h` → ADR-0005 + AGENTS Tier 1
- `src/core/Config.h/.cpp` → **legacy** config baggage (see U)
- `src/motion/LatheProfile.h/.cpp` → LATHE_MACHINE_PROFILE (lives under `motion/`, is state)

---

## 4 · Operational Mode Engine

**Docs**
- [`docs/modes/ELS_MODE_ARCHITECTURE.md`](../docs/modes/ELS_MODE_ARCHITECTURE.md) — lifecycle, registry, inheritance.
- One folder per mode, nine total: [`gearbox/`](../docs/modes/gearbox/) · [`turning/`](../docs/modes/turning/) · [`facing/`](../docs/modes/facing/) · [`taper/`](../docs/modes/taper/) · [`cutoff/`](../docs/modes/cutoff/) · [`slotting/`](../docs/modes/slotting/) · [`spheres/`](../docs/modes/spheres/) · [`joystick/`](../docs/modes/joystick/) · [`threading/`](../docs/modes/threading/).
- Boundary contract to S2: [`docs/motion/MODE_INTEGRATION_CONTRACT.md`](../docs/motion/MODE_INTEGRATION_CONTRACT.md).

**Source (governing docblock cited in file)**
- `src/motion/modes/IELSMode.h` → ELS_MODE_ARCHITECTURE + MODE_INTEGRATION (citation **missing**)
- `src/motion/modes/ModeRegistry.h/.cpp` → ELS_MODE_ARCHITECTURE (citation **missing**)
- `src/motion/modes/composite/UnifiedGearboxMode.h/.cpp` → `docs/modes/gearbox/` (citation **missing**)
- `src/motion/modes/primitives/` — `AsyncPrimitive.*`, `ConePrimitive.*`, `GearXPrimitive.*`, `GearZPrimitive.*` → each should map to the mode folder it serves (citation **missing** on all)
- `src/motion/modes/primitives/` + `docs/modes/{turning,facing,slotting}/SPECIFICATION.md` → **the biggest gap**: those three specs describe canned cycles that have no concrete class behind them (ADR-0001 retired the primitives); reality for those cycles today is `UnifiedGearboxMode`.

---

## 5 · Command Dispatcher & Transport

**Docs**
- [`docs/clients/ELS_CLIENT_ARCHITECTURE.md`](../docs/clients/ELS_CLIENT_ARCHITECTURE.md) — §3.5 client contexts, transports, ingress.
- [`docs/protocol/CANONICAL_COMMAND_REFERENCE.md`](../docs/protocol/CANONICAL_COMMAND_REFERENCE.md) — wire grammar §4.1 (SYS namespace).
- [`docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md`](../docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md) — universal SDK.
- [`docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md) — harness.

**Source (governing docblock cited in file)**
- `src/core_client/StandardCommandParser.h/.cpp` → ELS_CLIENT_ARCHITECTURE (citation **missing**)
- `src/core_client/ClientContext.h` → docblock cites §3.5 of an unqualified "ELS_CLIENT_ARCHITECTURE.md" (path **missing**)
- `src/core_client/ClientEndpoint.h`, `ClientManager.h/.cpp`, `ClientSerializers.h` → ELS_CLIENT_ARCHITECTURE (citation **missing**)
- `src/core_client/clients/native/NativeClient.h/.cpp` → ELS_CLIENT_ARCHITECTURE (citation **missing**)
- `src/core_client/transports/ITransport.h`, `WebSocketTransport.h` → ELS_CLIENT_ARCHITECTURE (citation **missing**)
- `src/core/WifiService.h/.cpp` → ELS_CLIENT_ARCHITECTURE transport services (citation **missing**)
- `src/core/network/HttpServer.h/.cpp` → ELS_CLIENT_ARCHITECTURE appliance (citation **missing**)

---

## 6 · HMI & Client Adapters

**Docs**
- [`docs/clients/hmi/RICH_TOUCH_800X480.md`](../docs/clients/hmi/RICH_TOUCH_800X480.md), [`docs/clients/hmi/HMI_DESIGN_GUIDELINES.md`](../docs/clients/hmi/HMI_DESIGN_GUIDELINES.md).
- [`docs/clients/web_ui/OVERVIEW.md`](../docs/clients/web_ui/OVERVIEW.md), [`docs/clients/web_ui/INTERACTION_LEDGER.md`](../docs/clients/web_ui/INTERACTION_LEDGER.md), [`docs/clients/CLIENT_IMPLEMENTATION_GUIDE.md`](../docs/clients/CLIENT_IMPLEMENTATION_GUIDE.md).
- Mode-facing surfaces also referenced from [`docs/modes/<mode>/`](../docs/modes/) wizard docs.

**Source (governing docblock cited in file)**
- `src/input/UsbKeyboard.h/.cpp` → ELS_CLIENT_ARCHITECTURE (citation **missing**)
- `clients/web_ui/` — `alpine.min.js`, `whatels-state.js`, `whatels-service.js`, `whatels-presets.js`, `whatels-formatters.js`, `whatels-emulator.js`, `lathe-controller.css/.js`, `lathe_dro_whatels_controller.html`, `lathe_dro_nanoels_controller.html`, `dev/`.
- `clients/nextion/` — `generate_assets.py`, `presets.json`, `build/`.
- No mode class files: mode surface is client-side, per ADR-0002 dual-registry model.

---

## U · Unassigned & Retiring Source

| Path | Status | Action |
| :-- | :-- | :-- |
| `src/main.cpp` | System bootstrap | Out of subsystem numbering — intentional |
| `src/core/Config.h/.cpp` | Legacy config + network globals | Superseded by `SettingsService` + `WifiService`; candidate for deletion or archive |
| `src/web/WebServerModule.h/.cpp` | Monolithic web/socket server | Superseded by `HttpServer` + `WebSocketTransport`; compile-flagged, zero link symbols — **deletion candidate** |
| `src/core_client/clients/` (empty dirs) | Placeholder | No action |

---

## D · Docs Claimed By No Subsystem

- `docs/adr/README.md`, ADR-0002/0003 (decision records; advisory by design).
- `docs/archive/*` (archived by ADR-0005; do not classify).
- `docs/plans/` (empty — superseded by `plans/`).
- `docs/README.md`, root `README.md`, root `AGENTS.md` (entry documents).
- `docs/machine/` → cross-cutting input to S3/S4 (not a subsystem of itself).

---

## G · Known Drift (found during regeneration, 2026-11-03)

| # | Drift | Severity |
| :-- | :-- | :-- |
| G1 | Three subsystem-numbering dialects coexist across `docs/` (canonical post-ADR-0005 vs pre-rename vs unlabelled) | High |
| G2 | `src/web/WebServerModule.*` and `src/core/Config.*` carry no docblock citation and are superseded — invisible to any grep-based classification | High |
| G3 | **Verified 2026-11-03:** of 87 `src/**` `.h/.cpp` files, **20** carry a governing citation (`Conforms to … .md` or a `docs/….md` path in `@details`) and **67** carry none. Only 5 files use a `@file` tag at all — that tag is *not* this repo's convention; the spine key is `@details` alone. Classification from inside the file is impossible for 77% of the tree | High |
| G4 | Two docblock citations are dangling or stale: `CoreCommandService.h` (superseded doc name) and `PresetService.h` (pre-relocation path) | Med |
| G5 | S4 mode folder has no docblock-to-mode-spec edge; 14 headers vs 9 spec folders unmapped by content | High |
| G6 | 3 of 9 mode specs (`turning`, `facing`, `slotting`) describe canned cycles that have no concrete source: those cycles exist today inside `UnifiedGearboxMode`, and ADR-0001 retired the primitives those specs assume | High |
| G7 | S6 web cluster: 10 JS/HTML assets, no per-file spec or docblock analogue | Med |

Resolved on this date by mechanical repair: docblock/front-matter paths in the relocated cluster, and the `NANOELS_ → WHATELS_` blueprint references. G3–G7 remain open decisions.

---

## R · Regeneration Procedure

Run from the workspace root; rebuild this map when any of these outputs changes.

```sh
# 1. Subsystem numbering authority
grep -n "Subsystem" docs/WHATELS_MASTER_ARCHITECTURE.md | head -40

# 2. Every source file's governing spec, where it exists
grep -rn "@details\|Conforms to\|specification defined in" src --include='*.h' --include='*.cpp'

# 3. Source files with NO governing citation (the uncited set = G3 list)
find src -name '*.h' -o -name '*.cpp' | sort > /tmp/all
grep -rl "Conforms to.*\.md" src --include='*.h' --include='*.cpp' | sort -u > /tmp/cited
grep -rl "docs/.*\.md" src --include='*.h' --include='*.cpp' | sort -u >> /tmp/cited
sort -u /tmp/cited | comm -23 /tmp/all -
# 2026-11-03 verified against 87 files: 20 cited (see spine rows) · 67 uncited
# NOTE: do NOT key this on `@file` — only 5 files carry that tag; @details is the convention.

# 4. Dangling doc links — resolve every relative .md target against its own file's directory
find docs clients src -type f -name '*.md' -print0 \
| while IFS= read -r -d '' f; do
    base=$(dirname "$f")
    grep -oE '\]\(\.{1,2}/[^)]+\.md' "$f" | sed 's/^](//; s/)$//' | cut -d'#' -f1 \
    | while read -r t; do [ -e "$base/$t" ] || echo "DANGLING $f -> $t"; done
  done

# 4b. Orphan docs — a spec no other file links to
find docs -name '*.md' > /tmp/docs_exist
grep -rl '\]\(\.{1,2}/' docs clients src --include='*.md' | sed 's/:.*//' | sort -u > /tmp/linkers
# orphan = in docs_exist, not named as a target of any linker, and not a root README

# 5. Mode folder: mode classes present vs mode specs present
find src/motion/modes -name '*.h' | sort > /tmp/classes
find docs/modes -mindepth 2 -name 'SPECIFICATION.md' | sort > /tmp/specs
```

Steps 2–5 output is authoritative until the next tree change. Do not trust a row if step 4 reports it dangling; fix the row, not the memory.
