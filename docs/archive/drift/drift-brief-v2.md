# whatELS Drift Audit — Brief v2 (standalone execution framework)

> **Status:** Operational. This document is the single authority for executing the drift audit. It carries no findings — the audit restarts as a fully new, independent effort. Any findings produced by earlier passes (including `docs/drift/pilot-1-hal-timing.md`) are quarantined: excluded from greps, never read as evidence, never seeded into the new ledger (§1.4).
> **Standalone rule:** An executing agent needs only this brief, [`drift-map.md`](./drift-map.md) (optional index — regenerate, never trust), the in-scope repo files, and the ledger this brief creates. It must not open or rely on any other plan/brief file.
> **Regenerated:** 2026-11-05 against tree at HEAD `15a56f6` (2026-10-30). All `wc -l` values below were re-measured with `wc -l` at that HEAD; re-measure at every preflight (line counts shift with the tree; HEAD moves).

---

## 0. What this audit is

**Product.** For each of nine subsystem pairs (§3), one drift-audit report at `docs/drift/<pair>-<slug>.md`: every mismatch between what the specs say and what the code does, each with verbatim anchors on both sides, a replayable grep, provenance, and a direction verdict. The audit **files drift; it never repairs it** — no `.cpp/.h/.py/.js/.html/.css/.json` edits, no builds, no unit tests, no HIL runs, no GitHub issues, no `.state/` writes. The fence binds the repo; the drift artifacts themselves and the ledger are the only files an audit pass may write.

**Method.** Pair each doc with its code home by derivation (the citation spine, §3.2), not by filename similarity alone. For every falsifiable claim in either direction, file a row. In-sync findings are filed as verification anchors, not drift. Severity is assigned only by the operator after discussion, never by the author.

**Independence.** Every claim in a new artifact must be derivable from the repo at the artifact's pinned HEAD by running the artifact's own commands. Prior passes' conclusions are not inputs.

---

## 1. Fences (binding)

### 1.1 Scope in
`docs/motion/**`, `docs/machine/**`, `docs/state/**`, `docs/protocol/**`, `docs/modes/**`, `docs/clients/**`, `docs/testing/**`, `docs/README.md`, `docs/WHATELS_MASTER_ARCHITECTURE.md`, `src/**`, `data/**`, `clients/**`.

### 1.2 Scope out (never drift sources; excluded from every grep with `--exclude-dir`)
- `docs/archive/**` — void.
- `reference/**` — monolithic-era `.ino` files; git history covers them better.
- `test/**`, `tests/**` — test code is not spec and not firmware; its specs (`docs/testing/**`) pair instead against the simulation seams inside `src/motion/hal/` and the transport code.
- `docs/drift/**` — audit artifacts never self-count in greps.
- `.state/**`, `todo/**`, `plans/**` — working notes.
- Generated build outputs, ROM headers, `.pio/**`, `node_modules` if any.

### 1.3 Authority-only (decision authority, never drift sources)
[AGENTS.md](../AGENTS.md), [GLOSSARY.md](../GLOSSARY.md), [docs/adr/](../docs/adr/) (`0001`–`0005`). Where an ADR records a decision that re-scoped a subsystem a doc describes, the row may cite the ADR as *context* for the direction verdict, but a doc being merely "pre-ADR-shaped" is not drift — the doc must contradict the code or claim conformance the code doesn't provide. An unread ADR may not be cited as if understood; name it unread.

### 1.4 Quarantine and Class C
- **Quarantine:** `docs/drift/pilot-1-hal-timing.md` is a superseded artifact from a prior framework. The confirmation probe (§7) names it as known-and-excluded; do not read it for findings; do not amend it; do not reuse its rows. All `docs/drift/**` files are excluded from greps by `--exclude-dir=drift`.
- **Class C (retired-code references — file, never patch):** any live doc citing a file that no longer exists in `src/` (historical families: `CommandDispatcher.*`, `src/display/**`, `src/input/Input.cpp`, `Axis.cpp`, `RichClientAdapter.*`, `taskDisplay`, `Nextion*`) is filed as a Class-C row (filing row type `C-n`, §4.6) with `grep -rn "<token>" src` ⇒ 0 evidence, and *no* suggested code/spec patch beyond "operator decides doc removal vs. revival". Verify each family's absence yourself; do not trust this list.
- A doc token that has no code home and whose code home *did* exist historically is Class-C-adjacent: the drift may be doc-ahead, unbuilt, or retired — `origin` (git log/blame) distinguishes which; state which one the history shows.

---

## 2. Roles, cycle, and hygiene

### 2.1 Three roles per pair
| Phase | Actor | Output | Hard rules |
|---|---|---|---|
| 0 Preflight | author session | spine derived, `wc -l` measured, HEAD pinned, ledger read | run before drafting any row |
| 1 Author | one session | the artifact `docs/drift/<pair>-<slug>.md` + its ledger checkpoint/split lines — nothing else | never cite a file it did not read; append rows to the artifact as completed (§3.4), never buffer for one final write; self-checks (§6.1) run while authoring |
| 2 Verify | fresh session, artifact + repo only, never the author's chat | `## Verification` block appended to the same artifact | runs §6.2 suite verbatim; writes no drift rows, changes no other text |
| 3 Hand-off | operator-gated | ledger row statuses; issue handles minted only for rows that reached `discussed` | audit pass never edits code/specs to resolve a row |

Row lifecycle: `filed → verified → discussed → actioned` (actioned happens in a later, separately authorized pass). Author and verifier must be different sessions; a same-session verifier is permitted only as a brand-new task cold-started from §9's verify prompt with no chat context carried.

### 2.2 Artifact-only writes
One pair = one artifact. The author writes the artifact plus its own ledger checkpoint/split-log lines (§3.4, §8e); row-status entries are created at hand-off. Delegation to sub-sessions (chunk splitting, §3.4) has no backchannel: a delegated leaf returns by writing its rows into the named artifact only.

### 2.3 Grep discipline
- Every grep an artifact asserts must be the exact command that produced the number, runnable verbatim from the workspace root, including `--include=*.md --exclude-dir=archive --exclude-dir=drift` where docs are swept.
- Never report a count that was not produced by that command. "Seen in grep output, uncounted" is a context sentence, never a hit count.
- Grep first, then read: locate the home of a token by grep; open the file that grep names. Read fully before quoting.

### 2.4 Quote-from-print
No verbatim quote enters an artifact until the exact cited line has been printed (`sed -n '<N>p' <file>` or the read tool). Anchors are re-checked against the print at row-writing time, never from memory of an earlier read.

### 2.5 Units and numbers
Every quantitative claim carries the full conversion chain with unit cancellation, written out (e.g. `5000 ns × (1 µs / 1000 ns) = 5 µs`). Prose must not carry a magnitude that the `units` field does not support. Before filing, scan the artifact for every numeric/unit token that appears in more than one row and require agreement (the author's own rows may contradict each other — that has happened and is the reason this rule exists).

### 2.6 Hardware capability claims
Any claim about what the silicon can or cannot do must name the witness actually consulted — for ESP32-S3, the installed framework's SoC capability header (`components/soc/esp32s3/include/soc/soc_caps.h`; print the real path and the macro checked, e.g. `SOC_RMT_TX_CANDIDATES`). No witness ⇒ the claim is inadmissible and must be deleted or softened.

### 2.7 Provenance
- Code anchor: `git log -1 --format='%h %ad %s' --date=short <hash>` printed as-is; no paraphrased hash families ("X-era") without the hash itself next to it.
- Doc anchor: `git log -1 --date=short -- <spec-path>` output (docs change rarely; this is usually a rebrand or spec-sync commit and is often *not* the decision origin).
- Blame once per file for the read set, into one table at the artifact top; individual rows reference the table rather than restating it.

---

## 3. Pairs, read sets, preflight

### 3.1 Pair queue (execution order as numbered; operator may re-order)
`wc -l` values measured at HEAD `15a56f6`; re-measure at preflight.

| # | Pair | Docs (wc -l) | Code (wc -l) | Notes |
|---|---|---|---|---|
| 1 | HAL + timing | `docs/motion/HAL_SPECIFICATION.md` 315, `docs/motion/CONCURRENCY_AND_TIMING.md` 264, `docs/motion/README.md` 113 | all of `src/motion/hal/` (9 files: `IStepperHAL.h` 94, `StepTimingConfig.h` 16, `ISpindleEncoderHAL.h` 103, `Esp32StepperHAL.h` 142 `.cpp` 329, `Esp32SpindleEncoderHAL.h` 80 `.cpp` 252, `MockStepperHAL.h` 169, `MockSpindleEncoderHAL.h` 167), `src/motion/contract/TelemetryMailbox.h` 74 + `MotionCommandPacket.h` 37, `src/motion/DRO_Scale.h` 43 `.cpp` 113, `src/core/BoardPins.h` 55, `src/main.cpp` 113 | citation-dense; mocks in-scope and must be read fully |
| 2 | Kinematic kernel | `docs/motion/MOTION_ENGINE_SPECIFICATION.md` 425, `docs/motion/MODE_INTEGRATION_CONTRACT.md` 341 | `src/motion/kernel/` (`MotionEngine.h` 174 `.cpp` 750, `TrajectoryPlanner.h` 129 `.cpp` 171, `SpindleSynchronizer.h` 105 `.cpp` 70, `SoftLimitTable.h` 104 `.cpp` 74, `SoftLimit.h` 28, `AxisState.h` 59, `AxisGeometry.h` 14, `AxisId.h` 10), `src/motion/contract/MotionCommands.h` 106, `src/motion/MotionConversions.h` 122 | pairs with pair 1's contract files; kernel chunking per §3.4 |
| 3 | Machine profile | `docs/machine/LATHE_MACHINE_PROFILE.md` 810 | `src/motion/LatheProfile.h` 243 `.cpp` 546, `data/presets/profiles.json` 198, `data/schemas/lathe-profile.schema.json` 256 | the profile doc is the largest in-scope doc; budget chunks |
| 4 | Core state model | `docs/state/ELS_CORE_STATE_MODEL.md` 504, `docs/state/README.md` 22 | `src/core/MachineModel.h` 118 `.cpp` 190, `src/core/TelemetrySerializer.h` 15 `.cpp` 117, `src/motion/contract/TelemetryMailbox.h` consumer side | publish/serialize surface |
| 5 | Properties & settings | `docs/state/ELS_PROPERTIES_AND_SETTINGS.md` 790, `docs/state/ELS_PRESET_CATALOG.md` 256 | `src/core/SettingsService.h` 138 `.cpp` 1632, `src/core/PersistentSetting.h` 125, `src/core/ISettingsStorage.h` 46, `src/core/PresetService.h` 35 `.cpp` 402 | settings service is the largest source file; split by command groups |
| 6 | Wire / command protocol | `docs/protocol/CANONICAL_COMMAND_REFERENCE.md` 363 | `src/core_client/StandardCommandParser.h` 53 `.cpp` 877, `src/core/CoreCommands.h` 276, `src/core/CoreCommandService.h` 46 `.cpp` 280, `src/core/CommandResult.h` 40 | command tokens cross into pairs 5/9; cross-pair sightings (§4.7) carry them |
| 7 | Transports + testing harness | `docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md` 1102 | `src/core_client/ClientManager.h` 38 `.cpp` 83, `ClientContext.h` 34, `ClientEndpoint.h` 38, `ClientSerializers.h` 40, `clients/native/NativeClient.h` 29 `.cpp` 49, `transports/ITransport.h` 17, `transports/WebSocketTransport.h` 41, `src/core/network/HttpServer.h` 70 `.cpp` 358, `src/core/WifiService.h` 73 `.cpp` 239, `src/web/WebServerModule.h` 85 `.cpp` 978 | HIL spec pairs against simulation seams (pair-1 code) and transport code; 3 chunks minimum |
| 8 | Modes | `docs/modes/ELS_MODE_ARCHITECTURE.md` 1014, `docs/modes/README.md` 44, `turning/SPECIFICATION.md` 109, `facing/SPECIFICATION.md` 102, `gearbox/SPECIFICATION.md` 124, `threading/SPECIFICATION.md` 121, `threading/WIZARD_WORKFLOW.md` 138, `taper/SPECIFICATION.md` 102, `cutoff/SPECIFICATION.md` 73, `slotting/SPECIFICATION.md` 75, `spheres/SPECIFICATION.md` 54, `joystick/SPECIFICATION.md` 58 | `src/motion/modes/` (`IELSMode.h` 242, `ModeRegistry.h` 44 `.cpp` 172, `composite/UnifiedGearboxMode.h` 97 `.cpp` 326, `primitives/AsyncPrimitive.h` 58 `.cpp` 191, `ConePrimitive.h` 59 `.cpp` 232, `GearXPrimitive.h` 57 `.cpp` 182, `GearZPrimitive.h` 57 `.cpp` 182) | citation-sparse (many mode specs carry no `Conforms to` citation) → fallback pairing rule carries the pair; budget accordingly |
| 9 | Device clients | `docs/clients/ELS_CLIENT_ARCHITECTURE.md` 746, `docs/clients/CLIENT_IMPLEMENTATION_GUIDE.md` 660, `docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md` 446, `docs/clients/hmi/HMI_DESIGN_GUIDELINES.md` 151, `docs/clients/hmi/RICH_TOUCH_800X480.md` 223, `docs/clients/web_ui/OVERVIEW.md` 297, `docs/clients/web_ui/INTERACTION_LEDGER.md` 101, `docs/clients/web_ui/ERGONOMICS.md` 87 | `clients/web_ui/*.js` (`lathe-controller.js` 4368, `whatels-service.js` 835, `whatels-emulator.js` 822, `whatels-state.js` 327, `whatels-formatters.js` 233, `whatels-presets.js` 42), `clients/web_ui/lathe_dro_whatels_controller.html` 544, `clients/sdk/js/**` (`whatels-client.d.ts` 151, transports `websocket-transport.js` 208 `emulator-transport.js` 116 `client-transport.js` 76), `data/www/*` (`style.css` 886, `app.js` 515, `index.html` 440) | dumb-client invariant (ADR-0002) is the frame: client-side kinematics/pitch math/soft limits in JS are firmware-boundary drift even when no doc contradicts them |

Cross-cutting witness (every preflight, cheap): `docs/README.md` 66, `docs/WHATELS_MASTER_ARCHITECTURE.md` 220. Rows about them belong to the pair whose subsystem they describe.

### 3.2 Spine derivation (preflight, per pair)
```
grep -rn "Conforms to.*\.md" <pair code dirs>        # doc-side of the spine
grep -rln "<doc basename>" src --include=*.h --include=*.cpp   # doc named in code
```
The citation spine (code headers citing their governing spec) is the primary pairing source; filename similarity is the fallback **only** when a file carries no citation, and the fallback basis (name morphology `Mock<Seam>` ↔ seam; class-shape/symbol grep) must be recorded inside the row's grep field. Derived rows from the spine: for each citation, read the cited doc fully; for each doc, read its cited code fully. Pairing density (docs cited / docs in pair) tells you where fallback pairing must carry the load — pair 8 is citation-sparse by construction; measure it yourself per pair and state it in the artifact header.

### 3.3 What each row needs before it can be filed
Read set first-hand; anchors printed from the file (quote-from-print); grep replayable; provenance replayable; for quantitative claims, the units ledger. A row whose anchor file was only grep-sighted must say so in `context` and may claim only what the grep line proves.

### 3.4 Session budget and chunking (128k context window)

Compaction is dangerous only when analysis lives solely in context. Two structural rules remove that failure mode: read sets are sized so a session never approaches the window, and rows are flushed to disk as they are completed, so a lost session costs at most one unsaved row.

**Budget arithmetic (tokens ≈ chars/4).** Fixed per-session overhead: system prompt + skills + environment details ≈ 10k; full brief ≈ 7.5k (this file is 29k chars — re-measure `wc -c` if it grows); prompt/todo/ledger reads ≈ 3k; artifact output ≈ 6–10k. Reads cost ≈ 1.5–2× their raw size in practice (grep context blocks, quote re-checks, tool-call echoes). Hard cap: **a session reads ≤ 100k chars (≈25k tokens) of new files, then must flush and end.** Worst-case close: 10 + 7.5 + 3 + 10 + 25×2 ≈ 80k tokens < 128k — compaction never fires mid-critical-step.

**Discipline inside every authoring session:**
- Append each completed row to the artifact immediately (`apply_diff`); analysis state lives on disk continuously, not in context.
- Never re-read a file already in context; quote-from-print checks print only the cited line (`sed -n '<N>p' <file>`).
- One phase per session: an authoring session never verifies its own artifact in the same context; verifiers cold-start (§2.1).
- Before ending a session, update the ledger checkpoint line (§8e). Resumption = §7 probes + that line, never chat replay.
- A single file > 40k chars is read in grep-anchored ranges (hit ± surrounding block), never whole; the coverage boundary declares ranges, not whole files, for it. If a claim needs more of that file, split further.
- Verifier sessions are inherently cheap: artifact + ledger + printed anchor lines; always within budget.

**Measured pair budgets at HEAD `15a56f6`** (`wc -c` over the §3.1 read sets) and the resulting chunk plan (proposed — preflight may re-split at file/section boundaries; ledger split-log records the reality):

| Pair | Read set (chars) | Plan |
|---|---|---|
| 1 HAL + timing | 94k | single session |
| 2 Kernel | 109k | 2 chunks: A = `MOTION_ENGINE_SPECIFICATION.md` + `MotionEngine.h/.cpp` (54k); B = `MODE_INTEGRATION_CONTRACT.md` + small kernel files + `MotionCommands.h` + `MotionConversions.h` (55k) |
| 3 Profile | 83k | single |
| 4 State | 47k | single |
| 5 Properties | 158k | 2 chunks: A = `ELS_PROPERTIES_AND_SETTINGS.md` + `SettingsService.h` + `PersistentSetting.h` + `ISettingsStorage.h` (73k); B = `SettingsService.cpp` + `PresetService.*` + `ELS_PRESET_CATALOG.md` (85k) |
| 6 Protocol | 75k | single |
| 7 Transports + HIL | 126k | 2 chunks: A = HIL spec + `src/core_client/` (74k); B = `WebServerModule.*` + `HttpServer.*` + `WifiService.*` (54k) + targeted HIL ranges |
| 8 Modes | 193k | 3 chunks: A = `ELS_MODE_ARCHITECTURE.md` + `IELSMode.h` + `ModeRegistry.*` (77k); B = gearbox/turning/facing specs + `UnifiedGearboxMode.*` + `GearX/GearZPrimitive.*` (51k); C = remaining 8 mode docs + `AsyncPrimitive.*` + `ConePrimitive.*` (58k) |
| 9 Device clients | 566k | 5–6 chunks: A = client architecture + sdk spec + `clients/sdk/js/**` (98k); B = implementation guide + `data/www/` (86k); C = `docs/clients/web_ui/**` + `whatels-state/-formatters/-presets.js` (67k); D = `whatels-service.js` + `whatels-emulator.js` + hmi docs (90k); E = `lathe-controller.js` 208k ≈ 52k tokens — grep-anchored ranges around dumb-client token hits only (pitch/lead/rpm math, soft-limit logic), declared partial in the boundary + controller HTML |

A chunk never cites a file outside its own read range without marking it grep-sighted; each chunk owns complete `## K<n>` group sections of the artifact, so a partially authored artifact is still a valid partial artifact.

### 3.5 Artifact header (template; first block after the title)
```
HEAD: <hash> (<date>) — every grep, count, and line number pinned to this commit
wc -l + wc -c: <file> N B; …          # every read-set file at preflight (wc -c feeds the §3.4 session budget)
Spine: <n> citations in <pair dirs> → <pairing density>   # derived at preflight
Rows filed: N (S-… list) · verification anchors: … · Class-C filings: …
Open pairings written: O-…   Cross-pair sightings written: X-…
Incoming rows closed/deferred: O-…, X-…        # from the ledger register (§8)
Confirmation probe: `ls docs/drift plans` ⇒ <contents seen, expected>
```

---

## 4. Row schema

Every drift row is a `###` block titled `S-<pair>-<nn> <one-line claim>` with these fields.

1. `group:` — the code-path × spec-section group the row belongs to (rows are grouped under `## K<n>` sections in the artifact; direction is adjudicated per group, §5).
2. `drift:` — one or two sentences stating the mismatch as a falsifiable claim.
3. `doc_anchor:` — file, line number(s), the exact quoted text in backticks. Quote-from-print (§2.4).
4. `code_anchor:` — same form on the code side.
5. `grep:` — one or more items, each exactly:
   `` `<command as run from workspace root>` ⇒ declared: <N> hits [files: <list>] ``
   The verifier replays each command verbatim; declared ≠ measured ⇒ the row is `UNVERIFIED`.
6. `context:` — what the two sides actually do; whether the doc's claim is live, planned, or historical; anything the reader needs to judge the row (including "this anchor was grep-sighted only").
7. `origin:` — provenance per §2.7 on both sides: which side changed last, what event explains it (a refactor, a spec sync, a never-landed plan), and whether origin is decidable.
8. `direction:` — one of `doc→code`, `code→doc`, `both`, `unknown`, `in-sync (verification)`, `n/a (filing)` — adjudicated per §5.
9. `corroboration:` — do other live docs corroborate one side? (Agreement among laggard docs is not agreement with the code; say which it is.)
10. `suggested_resolution:` — fix-doc / fix-code / both / operator's call, one or two sentences, explicitly assessment-only.
11. For quantitative rows: `units:` — the conversion chain per §2.5.

Specs often embed code blocks; prose and blocks are separate artifacts. A row comparing spec-prose against a spec-block against code must state which of the three lags (all three cases are real).

### 4.6 Row types beyond `S`
- `O-<pair>-n` **open pairing:** a doc claim whose code home is outside the reading pair's range. Carries the claim, both anchors as far as known, the adjudicating grep, and **which pair owns it**. Registered in the ledger; the owning pair closes it or defers with a reason. This is how claims get handed across pair boundaries — never bury one in prose.
- `X-<pair>-n` **cross-pair sighting:** a live token hit observed while sweeping whose home belongs to another pair's read set. Carries the token, both anchors, the producing grep, and the **receiving pair**. The receiving pair adjudicates or defers.
- `C-n` **Class-C filing:** per §1.4 — retired-file references in live docs. Filing only, no verdict, no patch suggestion.

### 4.7 Coverage boundary (closes every artifact)
Two declared lists: (a) what was read fully this pass (with wc -l / line ranges for partial reads), (b) what was declared-unread and therefore cannot support claims. Grep-only lines quoted verbatim are permitted inside (a) with that label. An artifact without a coverage boundary is not finished.

---

## 5. Direction adjudication (per group, after its rows are in)

- `doc→code` — the doc states a contract/shape the code never matched or no longer matches, and the doc is the newer or never-enacted side (e.g. planned seams declared but never built; diagram boxes asserting shipped values the code doesn't have; a capability no shipped code uses).
- `code→doc` — the code grew surface (members, fields, defaults) that its governing doc, frozen earlier, doesn't name — especially where the code's own citation claims conformance to the stale block.
- `both` — requires an independently falsifiable claim on each side (doc overstates X; code carries dead or contradictory state Y). One-sided rows must not inflate to `both`.
- `unknown` — honest when edit dates and causal commits disagree, or when the pairing is incomplete at the pass boundary (file as `O-n` instead where possible).
- Never let a doc's `Status: Authoritative` banner decide direction; a doc's *newer edit* doesn't automatically make it right either (`origin` carries it, `corroboration` cross-checks, prose-vs-block and code-comments-vs-code-shape expose which artifact actually lags).
- Verification rows (claim tested, found in-sync) are filed with `direction: in-sync (verification)` and are not drift.

---

## 6. Self-checks and the verification suite

### 6.1 Author-time self-checks (run while authoring, not after)
- Print every cited line before quoting (§2.4).
- Replay every grep the moment the row is drafted (§2.3).
- Recompute every units chain; run the intra-artifact numeric contradiction scan (§2.5).
- Sweep the artifact's own links from its own directory (§8 prefix rules) — expect 0 broken.
- Hardware-capability sentences get their witness printed (§2.6).

### 6.2 Independent verification suite (mechanical, judgment-free)
Verifier runs each at the artifact's pinned HEAD (if HEAD moved, fail the pin check first). Any mismatch ⇒ row marked `UNVERIFIED` with the mismatch printed, pending author amendment + re-pass.

| ID | Check | How | Pass criterion |
|---|---|---|---|
| V0 | HEAD pin | `git rev-parse --short HEAD` | equals artifact header pin |
| V1 | Links resolve from the artifact's directory | regex-sweep `](…)` targets, skip code-span notation and `…`/`...` placeholders, strip `:line` suffixes, resolve relative to the artifact dir | 0 broken |
| V2 | Grep replay | parse each grep field per §4.5; run verbatim | declared == measured, every row |
| V3 | Quote placement | `sed -n '<N>p' <file>` for every `file:line` quote anchor | quote found at the cited line |
| V4 | Units recompute + contradiction scan | recompute each `units` chain; cross-row token scan | arithmetic agrees; no numeric/unit token appears twice with different values |
| V5 | Row census | `### S-/O-/X-/C-` headers vs header-template list; each id defined once | counts agree |
| V6 | Provenance replay | `git log -1 …` per cited hash and doc | matches the row |
| V7 | Coverage boundary present, and no claim cites a file listed as unread | read the boundary | holds |

Verifier appends:
```
## Verification (session <date>, HEAD <hash>)
| row | V0 | V1 | V2 | V3 | V4 | V5 | V6 | V7 | verdict | notes (printed mismatch for each failure) |
Summary: N rows · x PASS · y UNVERIFIED
```

---

## 7. Confirmation probes (run before ANY phase)
```
ls docs/drift plans
git rev-parse --short HEAD
```
Expected (until the new audit starts producing artifacts): `docs/drift/` holds only `pilot-1-hal-timing.md` (quarantined superseded artifact, §1.4 — its presence is expected and is not a leak); `plans/` holds `drift-brief.md` (superseded historical v1 — do not read as authority), this brief, `drift-map.md`, and once kickoff is approved, `drift-ledger.md`. Any *other* file under `docs/drift/` not named in the ledger pair queue = stop and name it. `grep -rln -i "drift" docs --include=*.md --exclude-dir=drift` should return nothing new.

---

## 8. The ledger (`plans/drift-ledger.md`, created at kickoff)

Mutable execution state; created empty of findings. Structure:
- **(a) Pair queue:** nine rows (pair, artifact path, status: `not-started / authoring / verified / discussed`).
- **(b) Open-pairing register:** every `O-n` (id, source pair, owning pair, adjudicating grep, status).
- **(c) Cross-pair sighting register:** every `X-n` (token, anchors, receiving pair, status).
- **(d) Row-status index:** `S-id → filed/verified/discussed/actioned`.
- **(e) Split & checkpoint log:** one line per chunk — pair, chunk id, file range, reason, owning session, plus a live checkpoint: `files read (wc -c) · rows written (S-ids) · next range`. The checkpoint line is the only cross-session memory of unfinished chunk work; a resumed session starts from §7 probes + that line, never from chat.

Issue handles (`Refs #…`) are minted only for rows at `discussed`, by the operator.

### 8.1 Link prefix rules (depth is a fact, not style)
| File lives at | Links to repo root must use |
|---|---|
| `plans/` | `../` |
| `docs/drift/` (all audit artifacts) | `../../` |
| `docs/<sub>/` | `../` for docs dirs, `../../` for repo root siblings |
Line-suffix anchors (`path:91)`) are convention text, not VSCode-resolvable; the V1 rule ignores them. Inside briefs and the ledger, example link strings that exist only to illustrate syntax must be written as inline code, never as live links that fail V1 against the wrong base directory.

---

## 9. Session prompts (copy verbatim)

**Preflight + author:**
```
Read plans/drift-brief-v2.md in full. Nothing else in plans/ is authority.
Run §7 confirmation probes; stop on anything unexpected.
Audit pair <N> (<name> from §3.1) per §2 phases 0–1:
  preflight: spine via §3.2 greps, wc -l for the §3.1 read set, HEAD pin,
  read ledger (plans/drift-ledger.md) for incoming O-/X- rows;
  author: docs/drift/<pair-slug>.md ONLY, §3.5 header, §4 schema rows,
  §5 group verdicts, §4.7 coverage boundary.
Honor §1 fences, §2 rules (grep discipline, quote-from-print, units ledger,
hardware witness, provenance), §6.1 self-checks. Respect the §3.4 session budget:
≤100k chars of new reads per session, append each row to the artifact as completed,
no re-reads (sed line-print for quote checks), update the ledger checkpoint before ending.
Close incoming O-/X- as adjudicated or deferred-with-reason; register every outgoing one.
Present the artifact for operator review; start nothing else.
```

**Verify:**
```
Read plans/drift-brief-v2.md §4–§6 and §8 only.
Verify docs/drift/<artifact>.md independently against the repo: run V0–V7 (§6.2).
If the artifact's pinned HEAD ≠ current HEAD, fail V0 and report it.
Append the §6.2 Verification block; per-row PASS/UNVERIFIED with printed mismatch.
Write no drift rows; change no other text.
```

**Hand-off:** update ledger statuses; mint issue handles only for `discussed` rows; append next-pair queue line and any new `O-n`/`X-n`.

---

## 10. What completeness means

No table in this brief is exhaustive — read sets are as measured at one HEAD and will drift; pairing densities are measured, not inherited. Completeness is: every in-scope doc of the pair read (or split-and-covered per §3.4), every spine edge walked, every fallback pairing basis recorded, every claim anchored + grepped + provenanced, every cross-pair claim registered rather than buried, and the whole artifact re-derivable at its pinned HEAD by the §6.2 suite. An artifact that cannot survive its own suite is a draft, never a finding set.
