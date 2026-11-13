# whatELS drift audit — clean brief

Start here — this file is the entire framework: read it in full before touching anything else. All artifacts from earlier passes have been **physically deleted**: `docs/drift/` is empty and `plans/` holds only this brief. Nothing in this file describes what any earlier pass found, counted, or concluded.

## The product

A report. Find where the specs and the code disagree, group the disagreements, and give each one enough that **the operator and the agent can discuss an action**. This pass fixes nothing: no `.cpp/.h/.py/.js` edits, no builds, no tests, no HIL, no GitHub issues, no `.state/`.

## What one item records

| field | content |
|---|---|
| `id` | `S1-01` — stable, becomes the issue handle later |
| `group` | `K<n>` within the artifact |
| `drift` | one falsifiable sentence naming the disagreement |
| `doc_anchor` `code_anchor` | `file:lines` |
| `grep` | the command plus expected hit counts, so the item is reproducible |
| `context` | what the code actually does at the anchor; what the spec claims; how they differ |
| `origin` | commit hash, date, subject for the **code** anchor; last edit date for the **doc** anchor; which is newer |
| `direction` | `doc→code` (doc leads, code lags) · `code→doc` (code leads, doc lags) · `both` · `unknown` |
| `corroboration` | does another live doc already state this (then that doc is the laggard)? does the code match a shape recorded in git history? 1–2 lines |
| `suggested_resolution` | the leaf's own assessment: fix-doc / fix-code / both / retire, plus one sentence why. An **assessment**, never a decision |

No severity, no class letter. Prioritising comes after the discussion, not before it.

## The one rule that must not be re-inherited

A spec's `Status: Authoritative …` line is a **fact about that document**. It is not license to call the code wrong, and it is not an order of authority. Documentation updates get missed in this repo — that is drift in the other direction. Direction is an `assessment` carried by `origin` + `corroboration`; when neither carries it, write `unknown`. Never let the doc's self-claim decide which side is stale.

## Subsystem truth — locked, use this going forward

| # | Subsystem | Home |
|---|---|---|
| 1 | Hardware Timing & HAL | `src/motion/hal/` |
| 2 | Kinematic Kernel & Motion | `src/motion/kernel/` |
| 3 | State Model & Persistence | `src/core/` (state/settings services) |
| 4 | Operational Mode Engine | `src/motion/modes/` |
| 5 | Command Dispatcher & Transport | `src/core_client/` |
| 6 | Human-Machine Interface (HMI) Adapters | `src/input/`, `clients/` |

Tiers: **1–4 = core** · **5 = core-client** · **6 = device-client**. Core 0 / Core 1 name the **physical ESP32 cores** ([`CONCURRENCY_AND_TIMING.md`](../docs/motion/CONCURRENCY_AND_TIMING.md)); the numbering is a grouping, **not** a data-flow order — do not read 3→4 as a hand-off.

**Boundary bucket (7th row, not a renumbering).** [`CoreCommandService.*`](../src/core/CoreCommandService.cpp) is the core↔core-client **domain gateway**: it consumes a client type (`core_client::ClientContext&`), owns the `[SAFE-01]` gate, and grants typed execution into 2 and 3. It belongs to none of 1–6, and filing it there would break the tier rule. Its files: `CoreCommands.h`, `CoreCommandService.h/.cpp`, `CommandResult.h`, `ClientContext.h`; internal analogue `motion/contract/MotionCommandPacket.h` (the Core 0→Core 1 half of the same pattern).

**ADR rule.** ADRs keep their historical numbers, annotated in place: `Subsystem 3 [now 4 — Operational Mode Engine]`. Never rewrite an ADR's own claim.

## Reference repair — done before the audit, so the audit reads a clean tree

Two classes are fixed mechanically now; one is deliberately **not** fixed.

| class | what | action |
|---|---|---|
| A | doc→doc and doc→live-file links broken by the docs reorganisation | **fix now** — the target exists and is unambiguous |
| B | `Location:` front matter naming a pre-move path | **fix now** — rewrite to the file's real path |
| D | live specs claiming a subsystem number that contradicts the truth above | **fix now** — renumber to the truth |
| **C** | references to **retired code** (`CommandDispatcher.*`, `src/display/*`, `src/input/Input.cpp`, `Axis.cpp`, `RichClientAdapter.*`) | **DO NOT FIX** — these are drift items, filed by the audit. Patching them would invent a pairing before the evidence exists |

Master architecture §4 holds **no file names and no subsystem numbers** — a placeholder points to the map. Leaving incorrect references there risks context pollution for later agents.

## Direction and grouping

Direction is adjudicated **per group**, not per row. A group is a code path × topic: rows sharing `affected_components`, or explained by one change of direction (a consolidation, a rename, a quick fix, a break with best practice). One verdict retires several rows. Where an item is genuinely one-sided, say so and carry no direction at all.

## Pairing — discover it, do not assert it

The repo already encodes spec↔code pairing, and the audit must **derive** the read sets from that encoding rather than trust a table written by hand.

| mechanism | where | precision | how to enumerate |
|---|---|---|---|
| `@details Conforms to <DOC>.md Section N` | header docblocks | **file + spec section** | `grep -rn "Conforms to.*\.md" src` — one hit per paired header, giving the pair and the section the code claims to implement |
| `@details Implements the … specification defined in docs/<DOC>.md` | header docblocks | file + doc (no section) | `grep -rn "docs/.*\.md" src` |
| `> **Referenced By:** …` / `> **Related Specifications:**` / `> **Layer Dependency:**` | spec front matter | doc→doc and doc→bare filename | `grep -rn "Referenced By:\|Related Specifications:\|Layer Dependency:\|Predecessor Docs:" docs` |
| directory mirror (`src/foo/` ↔ `docs/foo/`) | [AGENTS.md §1.5](../AGENTS.md) | coarse | last resort; the mirror is violated for `src/motion/hal` → `docs/motion` |

Rules that follow from this, and they are findings, not assumptions:

- A header citing a doc **section** names its governing spec and the claim to check. That is the strongest pairing signal in the repo; use it as the spine of the pair table and grep it first.
- A citation whose target file **does not exist** is itself drift (a dangling reference). Verify every cited path exists before treating the citation as pairing; a broken path is an item, not a dead end.
- A spec's `Location:` front matter can disagree with the file's actual path. Pair on the real path, and file the mismatch.
- **Code with no citation is unpaired, not drift-free.** For those files, pairing falls back to: the class/type name inside the spec, an enum or symbol name grepped into the spec, the registry that binds them (`ModeRegistry.cpp`), and the subsystem table in [docs/WHATELS_MASTER_ARCHITECTURE.md](../docs/WHATELS_MASTER_ARCHITECTURE.md). Record which fallback was used in the item's `grep` field so the pairing is itself checkable.
- Unpaired **specs** (a doc no header cites) are in scope too: they claim contracts nothing points at.

## Scope fence

In: `docs/{protocol,state,motion,machine,modes,clients}/**`, `src/**`, `data/**`, `clients/**`.
Out: `docs/archive/**` (void — neither authority nor witness), `reference/**` (**the monolithic `.ino` is the modularisation starting point; git history covers it better — ignore it**), `test/**` and `tests/**`, generated ROM headers (`SystemProfilesJson.h`, `SystemSchemaJson.h`, `SystemPresetsJson.h`, `ControllerWebAssets.h` per [`.gitignore:26-29`](../.gitignore): authority is `data/presets/*` + [`scripts/embed_controller_assets.py`](../scripts/embed_controller_assets.py)), and `.state/`.
Authority-only, never drift sources: [AGENTS.md](../AGENTS.md), [GLOSSARY.md](../GLOSSARY.md), [docs/adr/](../adr/).

## Git, without per-row commands

`git blame` **once per file** in the read set returns every line's hash, date, subject in a single call. ~12 calls cover the whole audit; every anchor then resolves from that one table. Docs likewise: `git log -1 --format='%h | %ad | %s' --date=short -- <spec>`.
Architect mode has no shell — if your mode has one, run it yourself; if not, dispatch a subagent into a mode that does and have it write the blame tables into the artifact. These are read-only local commands, permitted by [AGENTS.md §1.5](../AGENTS.md).

## Pairs and chunk sizes

This table is **convenience, not authority**: rebuild it from the citation spine above at the start of the audit, and let the spine override it. The line counts are measured and reusable; the pairings are asserted and must be re-derived. If the rebuild disagrees with this row, the rebuild wins.

| pair | spec | code | chunks |
|---|---|---|---|
| 1 HAL + timing | [`HAL_SPECIFICATION`](../docs/motion/HAL_SPECIFICATION.md) 316 ✔, [`CONCURRENCY_AND_TIMING`](../docs/motion/CONCURRENCY_AND_TIMING.md) 265 ✔ | [`Esp32StepperHAL.cpp`](../src/motion/hal/Esp32StepperHAL.cpp) 330 ✔, [`Esp32SpindleEncoderHAL.cpp`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp) ?, hal headers | 2 |
| 2 Kinematic kernel | [`MOTION_ENGINE_SPECIFICATION`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md) 426 ✔, [`MODE_INTEGRATION_CONTRACT`](../docs/motion/MODE_INTEGRATION_CONTRACT.md) 342 ✔ | [`MotionEngine.cpp`](../src/motion/kernel/MotionEngine.cpp) 751 ✔, `MotionEngine.h` 175 ✔, kernel small files ? | 2 |
| 3 Machine profile | [`LATHE_MACHINE_PROFILE`](../docs/machine/LATHE_MACHINE_PROFILE.md) 811 ✔ | [`LatheProfile.cpp`](../src/motion/LatheProfile.cpp) 547 ✔, `data/schemas/` | 1, may split |
| 4 State model | [`ELS_CORE_STATE_MODEL`](../docs/state/ELS_CORE_STATE_MODEL.md) 505 ✔ | [`MachineModel.cpp`](../src/core/MachineModel.cpp) 191 ✔, [`TelemetrySerializer.cpp`](../src/core/TelemetrySerializer.cpp) 118 ✔, headers | 1 |
| 5 Properties/settings | [`ELS_PROPERTIES_AND_SETTINGS`](../docs/state/ELS_PROPERTIES_AND_SETTINGS.md) 791 ✔ | [`SettingsService.cpp`](../src/core/SettingsService.cpp) 1633 ✔ | 3 |
| 6 Wire commands | [`CANONICAL_COMMAND_REFERENCE`](../docs/protocol/CANONICAL_COMMAND_REFERENCE.md) 364 ✔ | [`StandardCommandParser.cpp`](../src/core_client/StandardCommandParser.cpp) 878 ✔ | 2 |
| 7 Transports | `docs/clients/{GUIDE,ARCHITECTURE,sdk,hmi}` ? | `ClientManager.*`, `NativeClient.*`, `ITransport.h`, `WebSocketTransport.h`, `HttpServer.*`, `WifiService.*`, `WebServerModule.*` | measure |
| 8 Modes | `docs/modes/**` 11 files ? | `src/motion/modes/**`, `primitives/**`, `ModeRegistry.*` | measure, 2–3 |
| 9 Device clients | `docs/clients/{web_ui,sdk,hmi}` ? | `clients/web_ui/*.js`, `clients/sdk/js/**`, `data/www/*` | measure, 1–2 |

Direction of work is **spec → code**: enumerate the spec's contracts, grep each into the code. That read order is not an authority rule. Inside the same pass, the reverse sweeps: public members per header, quoted string keys, parser verbs, enumerator labels — each grepped back into the spec. Private helpers, inline branches, and behaviour inside method bodies are out of scope by principle; say so in the coverage boundary rather than closing it.

## Rules that bind every leaf

- Read the read set first-hand before writing an item. Never cite a file you did not open.
- Every item carries the grep that reproduces it, with hit counts. No grep → inadmissible.
- Organise the artifact **by group**: group header stating the code path and topic, then items nested under it. The group is what gets reviewed.
- Close every artifact with a Coverage boundary: read to closure, and what remains unread.
- If compaction or a bigger-than-budget read set is hit, split at a file or section boundary, record the new range in the artifact's Coverage boundary, and write one line to [plans/drift-dispatch.md](./drift-dispatch.md) naming the pair, the new range, and why. The split record is mutable; this brief is not.
- Delegation has no backchannel: each leaf writes its own `docs/drift/<pair>.md` and nothing else; the parent reads files.

## Layout for the finished audit

`docs/drift/<pair>.md` per pair; then one index with the group list, each group's direction verdict left **open** for the operator. The discussion is the operator's, on a group, with the item's `context`, `origin`, `corroboration`, `suggested_resolution` in front of them.

## Invariants to confirm before the pilot

1. `docs/drift/` is empty; `plans/` contains this brief plus [`drift-map.md`](./drift-map.md) (pairing infrastructure — not findings). If any other file is present under either, it is a leak from a superseded pass: **stop, name it, and do not read it as findings** — its content may look like evidence, and an item it lacks is not evidence that drift is absent.
2. Any drift finding found anywhere outside this brief and the repo's specs/code is a surviving finding: report it, never incorporate it.

Confirm with:

```
ls docs/drift plans
grep -rln -i "drift\|authority: spec" docs plans --include=*.md
```

Expected: empty `docs/drift`, this brief and `drift-map.md` under `plans/`, and only those two as grep hits (`drift-map.md` pairs sources by subsystem — it is not a report). The pattern is one probe, not the whole hazard.

**Treat no list in this brief as exhaustive.** The pair table and the field list are the framework; completeness is what the audit produces, not what this file predicts.

## Prompt for the fresh session

```
Read plans/drift-brief.md in full before doing anything else. It is the entire framework for
this task. Earlier audit artifacts were deleted: docs/drift/ is empty. Run the brief's two
confirmation probes first and stop if any stray file is present - do not read stray content
as findings, in either direction.

Pilot pair 1 (HAL + timing) end to end: read the spec, enumerate its contracts, grep each into
the code, run git blame once per code file and git log -1 per spec, then write
docs/drift/pilot-1-hal-timing.md with items grouped by code path x topic, in the brief's field
order, each carrying its grep and hit counts. Stop and present it for my review before any
other pair starts.
```
