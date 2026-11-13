# Drift audit — pilot pair 1 (HAL + timing)

Pair: [`docs/motion/HAL_SPECIFICATION.md`](../docs/motion/HAL_SPECIFICATION.md) (316L, read fully) + [`docs/motion/CONCURRENCY_AND_TIMING.md`](../docs/motion/CONCURRENCY_AND_TIMING.md) (265L, read fully) × [`src/motion/hal/`](../src/motion/hal/) + fallback-paired [`src/motion/DRO_Scale.h/.cpp`](../src/motion/DRO_Scale.h), [`src/core/BoardPins.h`](../src/core/BoardPins.h), [`src/main.cpp`](../src/main.cpp), and the citation-spine pair [`src/motion/contract/`](../src/motion/contract/) (`MotionCommandPacket.h`, `TelemetryMailbox.h`).
This pass files drift; it repairs nothing. No `.cpp/.h` touched, no build, no test, no HIL, no issue, no `.state/`.
Confirmation probes ran clean: `docs/drift/` held only this artifact (written by this pass); `plans/` holds `drift-brief.md` + `drift-map.md` only.

## Spine (re-derived from citations; overrides [`plans/drift-map.md`](plans/drift-map.md) where they differ)

`grep -rn "Conforms to.*\.md" src/motion/hal src/motion/contract` → 10 hits:

| Code anchor | Citation target | Status |
|---|---|---|
| [`IStepperHAL.h:15`](../src/motion/hal/IStepperHAL.h:15) | HAL_SPEC §2.2 | checked, paired |
| [`StepTimingConfig.h:6`](../src/motion/hal/StepTimingConfig.h:6) | HAL_SPEC §2.1 | checked, paired |
| [`Esp32StepperHAL.h:53`](../src/motion/hal/Esp32StepperHAL.h:53) | HAL_SPEC §2 + StepTimingConfig | checked, paired |
| [`ISpindleEncoderHAL.h:6`](../src/motion/hal/ISpindleEncoderHAL.h:6) | HAL_SPEC §3.2 | checked, paired — and §3.2 does NOT carry the seam's hooks/`updateProfile`/`getCountsPerSpindleRev` (S3-02/S3-01) |
| [`Esp32SpindleEncoderHAL.h:9`](../src/motion/hal/Esp32SpindleEncoderHAL.h:9) | HAL_SPEC §3.1 & §3.2 | checked, paired — same §3.2 omissions |
| [`MotionCommandPacket.h:7`](../src/motion/contract/MotionCommandPacket.h:7) | CONCURRENCY §3.2 | checked, paired (map S1 rows had this only implicitly — spine adds it) |
| [`TelemetryMailbox.h:14`](../src/motion/contract/TelemetryMailbox.h:14) + [`:33`](../src/motion/contract/TelemetryMailbox.h:33) | CONCURRENCY §3.1 / [CONC-01] | checked, paired |
| [`MotionCommands.h:8`](../src/motion/contract/MotionCommands.h:8), [`:77`](../src/motion/contract/MotionCommands.h:77) | MODE_INTEGRATION_CONTRACT §2.2/§3 | out of pair-1 scope (pair-2 spine) |
| `MockStepperHAL.h`, `MockSpindleEncoderHAL.h` | no citation | fallback rule 2: name morphology `Mock<Seam>HAL` mirrors the seam |
| [`DRO_Scale.h/.cpp`](../src/motion/DRO_Scale.h) | no citation | fallback rule 1: §4 class-shape grep + symbol grep (`Z_SCALE_PULSES_PER_MM`→ELS_PROPERTIES L506/507) |
| [`BoardPins.h`](../src/core/BoardPins.h) | no citation | fallback rule 1: pin macros are `configureAxis`/`init` arguments per HAL §2.2 ([`main.cpp:64-71`](../src/main.cpp:64)) |
| [`main.cpp`](../src/main.cpp) | no citation | fallback rule 1: CONCURRENCY §2 task diagram + §5 lease row name the wiring it implements |
| HAL_SPEC §4/§5 seams `IDROScaleHAL`,`IEndstopHAL` | spec-side only | **0 code hits** — unimplemented layer (S4 rows) |

Provenance (blame once/file, `git log -1` per doc):

| File | Blame / last edit |
|---|---|
| StepTimingConfig.h | 16L all `ec2753b` 2026-10-15 `feat(hal): implement HAL contracts, PCNT driver, and TelemetryMailbox` |
| IStepperHAL.h | 94L all `ec2753b` 2026-10-15 |
| ISpindleEncoderHAL.h | 64L `ec2753b` 2026-10-15 · 25L `4d7b895` 2026-10-16 `feat(test): firmware simulation injection bridge…` · 10L `5051263` 2026-10-20 `fix(firmware): align spindle CPR…remove magic fallbacks` · 4L `82bed72` 2026-10-16 |
| Esp32StepperHAL.h | 140L `ce21bce` 2026-10-16 `feat(hal): implement concrete Esp32StepperHAL with hardware timer and timing guards` · 2L `3719431` 2026-10-16 `refactor(motion): decommission legacy Motion.cpp, Axis.cpp…` |
| Esp32StepperHAL.cpp | 329L `ce21bce` 2026-10-16 |
| Esp32SpindleEncoderHAL.h | 56L `ec2753b` · 13L `4d7b895` · 6L `5051263` 2026-10-20 · 2L `8abe614` 2026-10-20 `refactor(core): standardize unit conversions and CPR deadband` |
| Esp32SpindleEncoderHAL.cpp | 171L `ec2753b` · 31L `4d7b895` · 30L `7ecdd23` 2026-10-19 `feat(hil,firmware): harden HIL test harness…` · 20L `5051263` 2026-10-20 |
| MockStepperHAL.h | 169L `11dd2f6` 2026-10-16 `test(motion): implement automated E2E simulation harness…` |
| MockSpindleEncoderHAL.h | 121L `11dd2f6` · 20L `4d7b895` · 16L `5051263` · 7L `8abe614` · 3L `82bed72` |
| DRO_Scale.h / .cpp | 33+5L `460f124` 2026-09-13 `Checkpoint` (blame path `h5/include/DRO_Scale.h`), 53+33L `acf333a` 2026-09-16 (path `h5/src/DRO_Scale.cpp`), 2+10L `ef835f5` 2026-10-20 `refactor(dro): add dynamic scale factor setter and getter`, 7L `79202c1` 2026-10-12, 4+4L `dce3642` 2026-10-20, 3+4L `3719431` 2026-10-16 |
| BoardPins.h | 55L all `b048559` 2026-10-20 `refactor(core): modernize Config and isolate board pins into BoardPins.h` |
| main.cpp | task-wiring lines `acf333a` 2026-09-16 (L85); Core-1 wiring L61-86 mixed `b048559`/`3719431`/`4a0e289` |
| HAL_SPECIFICATION.md | `455ae29` 2026-10-20 `rebrand: Rebrand project to whatELS…` (prior: `b9af9cb`/`397bb4a` 2026-10-15 baseline) |
| CONCURRENCY_AND_TIMING.md | `455ae29` 2026-10-20 (same rebrand; prior `b9af9cb` 2026-10-15) |
| motion/README.md | `455ae29` 2026-10-20 |
| MOTION_ENGINE_SPECIFICATION.md | `455ae29` 2026-10-20 (prior `8bb9131`/`cc26612` 2026-10-19) |
| HIL_TEST_HARNESS_SPECIFICATION.md | `e636302` 2026-10-29 `docs(architecture): harmonize all subsystem and client specs with ADR-0005 (Refs #2)` |
| ELS_PROPERTIES_AND_SETTINGS.md | `ce3a29c` 2026-10-20 `docs(state): update core state model and properties guide…` |
| WHATELS_MASTER_ARCHITECTURE.md | `ab6b788` 2026-10-29; CLIENT_IMPLEMENTATION_GUIDE.md `a8240aa` 2026-10-28; ELS_MODE_ARCHITECTURE.md `71d5def` 2026-10-29; ELS_CORE_STATE_MODEL.md `e636302` 2026-10-29 |

Both governing docs were last touched by the 2026-10-20 rebrand only — the `Status: Authoritative` lines (HAL L3, CONC L3) were **not** consulted for direction; `origin` + `corroboration` carry it per the brief.

---

## K1 [`src/motion/hal/`](../src/motion/hal/) — stepper/servo timing contract × HAL §2.1/§2.2

Direction verdict for the group: **both** — the docs lead on the non-blocking invariant (code violates it), the code leads on a stale seam doc-comment. One group covers four rows.

### S1-01 §2.1 "never execute blocking delay loops" vs `esp_rom_delay_us` in the pulse path
- group: K1
- drift: HAL §2.1/§1.1.3 claims the HAL never blocks; [`Esp32StepperHAL.cpp`](../src/motion/hal/Esp32StepperHAL.cpp) busy-delays up to 5 ms inside `IRAM_ATTR` methods on Core 1.
- doc_anchor: [`HAL_SPECIFICATION.md:17`](../docs/motion/HAL_SPECIFICATION.md:17) ("never execute blocking delay loops (`delay()` or busy `micros()` spins). Step pulses… handled strictly by hardware peripherals (RMT, GPTimer) or short, bounded microsecond pin toggles"); [`:61`](../docs/motion/HAL_SPECIFICATION.md:61) non-blocking enable-arming invariant; §2.2 block comments [`:91`](../docs/motion/HAL_SPECIFICATION.md:91), [`:119`](../docs/motion/HAL_SPECIFICATION.md:119).
- code_anchor: [`Esp32StepperHAL.cpp:87`](../src/motion/hal/Esp32StepperHAL.cpp:87) (`setDirection`: `esp_rom_delay_us(requiredHoldUs − elapsed)`, hold ≤ 5 ms), [`:159`](../src/motion/hal/Esp32StepperHAL.cpp:159) (`pulseStep`: setup delay ≤ 5 ms), [`:171`](../src/motion/hal/Esp32StepperHAL.cpp:171) (`esp_rom_delay_us(pulseWidthUs)` = 2.5 ms pulse hold inside the step); `pulseStep` is `IRAM_ATTR` ([`:142`](../src/motion/hal/Esp32StepperHAL.cpp:142)) and is called from the GPTimer alarm ISR ([`:281`](../src/motion/hal/Esp32StepperHAL.cpp:281)).
- grep: `grep -rn "esp_rom_delay_us" src` → 3 hits, all [`Esp32StepperHAL.cpp`](../src/motion/hal/Esp32StepperHAL.cpp) (L87, L159, L171); `grep -rn "esp_rom_delay_us" docs` → 0. (re-verified 2026-11-03; earlier count of 4 miscounted an include line)
- context: The spec's invariant is absolute ("never execute blocking delay loops", §2.1.3 L17). The implementation blocks the Core-1 pulse worker for the entire DIR-hold (5 ms), DIR-setup (5 ms) and STEP-width (2.5 ms) windows — a synchronous step dispatch can stall the motion tick ~7.5 ms; the pulse train path (GPTimer alarm, [`:188-198`](../src/motion/hal/Esp32StepperHAL.cpp:188) `alarm_count = periodUs`, min 10) is the spec-honest half. Enable-arming (the L61 invariant) is correctly non-blocking: [`setEnabled`](../src/motion/hal/Esp32StepperHAL.cpp:109-121) latches a timestamp, [`:149-153`](../src/motion/hal/Esp32StepperHAL.cpp:149-153) gates later pulses.
- origin: code anchor `ce21bce` 2026-10-16 `feat(hal): implement concrete Esp32StepperHAL with hardware timer and timing guards`; doc last edit `455ae29` 2026-10-20 (rebrand only; invariant prose present since 2026-10-15 baseline `b9af9cb`). Neither side proven newer as a decision — same-week, doc prose precedes the impl.
- direction: both (doc claim never satisfied by any code shape; code comment claims it violates)
- corroboration: the doc claim is echoed live in [`CONCURRENCY_AND_TIMING.md`](../docs/motion/CONCURRENCY_AND_TIMING.md) §4.3 (Core 1 loop budget ≤ 35 µs, period 250 µs, L228-234) — a 2.5–5 ms stall is 70–140× that budget, so a second live doc corroborates that the code shape breaks the documented timing model. `git log -S "esp_rom_delay_us"` shows the delay predates the doc baseline (introduced 2026-10-16).
- suggested_resolution: fix-doc candidate — the honest doc rewording states the two-mode reality (train = GPTimer non-blocking; single step/jog = bounded direct toggle, §1.1.3's own "short, bounded microsecond pin toggles" wording understates 2.5–5 ms); fix-code (true non-blocking state machine) is the alternative but is a product decision. Assessment only; operator discusses on K1.

### S1-02 §2.2 seam comment names `dirSetupDelayUs`/`stepPulseWidthUs`; struct fields are `…Ns`
- group: K1
- drift: the seam's own doc-comments reference microsecond field names that do not exist; the fields are nanosecond-named.
- doc_anchor: [`HAL_SPECIFICATION.md:91`](../docs/motion/HAL_SPECIFICATION.md:91) `@details Enforces dirSetupDelayUs if direction changes while stationary.`; [`:119`](../docs/motion/HAL_SPECIFICATION.md:119) `Enforces stepPulseWidthUs.` — mirrored verbatim in the code seam [`IStepperHAL.h:34`](../src/motion/hal/IStepperHAL.h:34), [`:60`](../src/motion/hal/IStepperHAL.h:60).
- code_anchor: [`StepTimingConfig.h:9`](../src/motion/hal/StepTimingConfig.h:9) `dirSetupDelayNs = 5000`, [`:11`](../src/motion/hal/StepTimingConfig.h:11) `stepPulseWidthNs = 2500`; impl converts ns→µs at [`Esp32StepperHAL.cpp:85`](../src/motion/hal/Esp32StepperHAL.cpp:85), [`:157`](../src/motion/hal/Esp32StepperHAL.cpp:157), [`:169`](../src/motion/hal/Esp32StepperHAL.cpp:169).
- grep: `grep -rn "dirSetupDelayUs\|stepPulseWidthUs" src docs` → 4 hits (HAL ×2, IStepperHAL ×2); `grep -rn "dirSetupDelayNs" src docs` → 2 hits, zero in docs (spec §2.1 code block [`:51`](../docs/motion/HAL_SPECIFICATION.md:51) uses the `Ns` names — so the spec *code block* is right, the spec *prose comments* are stale).
- context: unit-naming drift in comments: ns-vs-µs is a factor-1000 trap for readers; values themselves agree (5000 ns = 5 µs, 2500 ns = 2.5 µs).
- origin: code `ec2753b` 2026-10-15 (both files same commit); spec block same baseline — comment text present since 2026-10-15. Unresolvable which-side-newer → `unknown`-carried, but internally inconsistent on the doc side (block says `Ns`, comments say `Us`) without needing origin: the doc contradicts itself.
- direction: unknown (self-inconsistent doc; code comments copy the doc's stale names)
- corroboration: the code block inside HAL §2.1 (L49-59) is byte-identical to [`StepTimingConfig.h:8-16`](../src/motion/hal/StepTimingConfig.h:8) — values/field names verified in-sync; only the prose comments lag.
- suggested_resolution: fix-doc (comment naming) — mechanical, zero behavior change; the same edit lands in the seam docblocks. Assessment.

In-sync verification carried by this group (one verdict retires them): §2.1 struct block ↔ [`StepTimingConfig.h`](../src/motion/hal/StepTimingConfig.h) 7/7 fields and defaults identical; §2.2 block [`HAL_SPECIFICATION.md:65-152`](../docs/motion/HAL_SPECIFICATION.md:65) ↔ [`IStepperHAL.h`](../src/motion/hal/IStepperHAL.h) interface 11/11 pure-virtual signatures identical (incl. `hasDriverFault` at spec L150 ↔ code L93); non-blocking enable-arming invariant satisfied (above); `AxisId` enum spec L69-72 ↔ [`IStepperHAL.h:5-11`](../src/motion/hal/IStepperHAL.h:5) (guard mechanism undocumented, minor, not filed).

## K2 [`src/motion/hal/Esp32StepperHAL.h/.cpp`](../src/motion/hal/Esp32StepperHAL.h) — concrete stepper driver surface × HAL §2

Group verdict: **code→doc** — the concrete ESP32 driver grew two members no spec names; its own docblock claims conformance to §2.

### S2-01 `configureFaultPin` — HAL member absent from every live doc
- group: K2
- drift: the concrete HAL grows a fault-pin wiring method that no spec section names.
- doc_anchor: closest home HAL §2.2 [`HAL_SPECIFICATION.md:150`](../docs/motion/HAL_SPECIFICATION.md:150) `hasDriverFault(AxisId) const = 0` — the seam queries faults but nothing configures the pin in spec terms; `configureFaultPin` doc hits: 0.
- code_anchor: [`Esp32StepperHAL.h:72`](../src/motion/hal/Esp32StepperHAL.h:72) `bool configureFaultPin(AxisId axis, int faultPin);` (not `override` — concrete-only); body [`Esp32StepperHAL.cpp:61-74`](../src/motion/hal/Esp32StepperHAL.cpp:61) (GPIO_MODE_INPUT + pull-up).
- grep: `grep -rn "configureFaultPin" src docs` → 2 src hits (`.h:72`, `.cpp:61`), 0 doc hits (verified live docs only; archive excluded per scope fence).
- context: §2.2's `hasDriverFault` has no spec-supplied way to bind a pin; the impl's own comment at [`Esp32StepperHAL.cpp:262`](../src/motion/hal/Esp32StepperHAL.cpp:262) ("Leadshine … assert fault LOW") and HAL §1.1.4 (driver-agnostic, L18) imply this is a driver-family knob. Note: a live doc half-states it — [`LATHE_MACHINE_PROFILE.md:190`](../docs/machine/LATHE_MACHINE_PROFILE.md:190) has `bool alarmActiveLow; // Fault pin polarity` (grep-only sighting; the 811-line doc not read — see coverage boundary; `grep -rn alarmActiveLow src` → 0, so that field is doc-ahead code-absent).
- origin: code `ce21bce` 2026-10-16 (newer); spec last 2026-10-20 rebrand-only. Code newer.
- direction: code→doc
- corroboration: none live states `configureFaultPin`; LATHE_MACHINE_PROFILE's fault-polarity field corroborates the *concept* but names no code home. Dead-seam flag: `configureFaultPin` has 0 call sites in [`main.cpp`](../src/main.cpp) or elsewhere (`grep -rn "configureFaultPin" src` finds only the definition pair) — the seam is unbuilt wiring, not drift-repair.
- suggested_resolution: fix-doc (HAL §2.2 gains the concrete-driver member, or the LATHE_MACHINE_PROFILE profile struct becomes the home and HAL cross-refs it). Assessment; the dead-seam question is the operator's.

### S2-02 `getAxisState` diagnostics accessor absent from spec
- group: K2
- drift: the concrete HAL exposes an internal-state accessor for telemetry/diagnostics that no doc names.
- doc_anchor: none; §2.2 has no diagnostics member (block read to its end at L152).
- code_anchor: [`Esp32StepperHAL.h:127`](../src/motion/hal/Esp32StepperHAL.h:127) `const AxisHardwareState& getAxisState(AxisId) const;`; impl [`:269-275`](../src/motion/hal/Esp32StepperHAL.cpp:269); backing struct [`:24-39`](../src/motion/hal/Esp32StepperHAL.h:24).
- grep: `grep -rn "getAxisState\|AxisHardwareState" src docs` → 5 src hits (h ×2 incl. struct, cpp ×1, contexts struct), 0 doc hits.
- context: a diagnostics window onto pins/timing/pulse-train state; plausibly a TelemetryMailbox/`getAssertedMask`-era consumer, currently 0 call sites outside the header pair.
- origin: `ce21bce` 2026-10-16.
- direction: code→doc
- corroboration: no live doc states it.
- suggested_resolution: fix-doc low-priority (a diagnostics accessor is spec-worthy only if some consumer reads it; today none does — retire-or-document decision is the operator's). Assessment.

In-sync verification retired by this verdict: §2 docblock conformance claim [`Esp32StepperHAL.h:53`](../src/motion/hal/Esp32StepperHAL.h:53) ↔ §2 — true for all 11 seam overrides present (11/11, incl. ns-guard conversion); `startPulseTrain` 100 kHz ceiling [`:189-190`](../src/motion/hal/Esp32StepperHAL.cpp:189) matches CONCURRENCY §4.2 row (L223, GPTimer ISR ≤ 100 kHz).

### S1-03 RMT: four live docs claim an RMT-driven stepper path; the code has no RMT (filed under K1's mechanism topic, pinned here for the stepper path)
- group: K1/K2 boundary — mechanism claim spanning the timing docs
- drift: docs name a hardware RMT (Motion Timing Unit) pulse peripheral for stepper trains; the shipped driver is GPTimer-alarm + direct toggle with zero RMT in `src`.
- doc_anchor: [`HAL_SPECIFICATION.md:17`](../docs/motion/HAL_SPECIFICATION.md:17) ("hardware peripherals (RMT, GPTimer)") + [`:38`](../docs/motion/HAL_SPECIFICATION.md:38) (diagram "GPIO / RMT / GPTimer"); [`CONCURRENCY_AND_TIMING.md:222`](../docs/motion/CONCURRENCY_AND_TIMING.md:222) (§4.2 row "Hardware RMT | ESP32-S3 RMT | Up to 1 MHz"); README:56 and :87; [ELS_MODE_ARCHITECTURE.md:80](../docs/modes/ELS_MODE_ARCHITECTURE.md:80); [ELS_CORE_STATE_MODEL.md:312](../docs/state/ELS_CORE_STATE_MODEL.md:312).
- code_anchor: [`Esp32StepperHAL.cpp:286-314`](../src/motion/hal/Esp32StepperHAL.cpp:286) (`initTimer`: GPTimer only), [`:171`](../src/motion/hal/Esp32StepperHAL.cpp:171); `grep -rniw rmt src` → 0.
- grep: `grep -rn RMT docs --include=*.md --exclude-dir=archive --exclude-dir=drift` → 7 hits / 5 live files (HAL_SPEC ×2: L17, L38 · CONCURRENCY ×1: L222 · motion/README ×2: L56, L87 · ELS_MODE_ARCHITECTURE ×1: L80 · ELS_CORE_STATE_MODEL ×1: L312); `grep -rniw rmt src` → 0; `git log -S RMT -- src/motion/hal` → 0 commits.
- context: ESP32-S3 offers no RMT peripheral (it is the NANOELS/RV1089-era mechanism; the monolithic `.ino` trees are out of scope per the fence, git history covers them). The §4.2 table's *claim* — ≤ 1 MHz pulse rate via RMT — is a capability no shipped code uses; the code's own ceiling is GPTimer 100 kHz (S2 verification above). Docs are not wrong about the *platform family*; they are wrong about this firmware.
- origin: doc prose present since 2026-10-15 baseline, last touched rebrand-only 2026-10-20; code never had RMT (`ce21bce` 2026-10-16 implements GPTimer). Doc→code.
- direction: doc→code (6 doc sites overstate the peripheral set)
- corroboration: yes — six live docs agree with each other and all six miss the code; corroboration across laggard docs, not with the code.
- suggested_resolution: fix-doc — annotate §4.2/§1.1.3/README as "platform-capability table; whatELS firmware path is GPTimer + direct toggle (100 kHz ceiling)". One edit retires all six rows. Assessment; fix-code (actually use RMT where hardware offers it) would be a product decision.

## K3 [`src/motion/hal/ISpindleEncoderHAL.h`](../src/motion/hal/ISpindleEncoderHAL.h) + [`Esp32SpindleEncoderHAL.*`](../src/motion/hal/Esp32SpindleEncoderHAL.h) — spindle seam × HAL §3.1/§3.2 + HIL §2.1

Group verdict: **code→doc** for the seam extras (S3-01/02/04) — the seam grew members after its §3.2 block froze; **doc→code** on one witness-listing line (S3-05) where the newer doc restates an older code listing; **both** verdict retires the placement row (S3-03).

### S3-01 Seam members `updateProfile` and `getCountsPerSpindleRev` exist in code+mocks+call sites; zero doc contract
- group: K3
- drift: two live, called HAL methods have no contract home; the seam's own citation claims conformance to a §3.2 block that omits them.
- doc_anchor: HAL §3.2 block [`HAL_SPECIFICATION.md:178-239`](../docs/motion/HAL_SPECIFICATION.md:178) lists init/getPulseDelta/getAccumulatedPulses/getFilteredRpm/getAngleDegrees/hasIndexSynced/resetIndexSync — neither name appears; live-doc hits for either token: 0.
- code_anchor: seam [`ISpindleEncoderHAL.h:31-38`](../src/motion/hal/ISpindleEncoderHAL.h:31) (`updateProfile`, default `{}` body = optional), [`:53-56`](../src/motion/hal/ISpindleEncoderHAL.h:53) (`getCountsPerSpindleRev`, default `return 0`); impl [`Esp32SpindleEncoderHAL.h:26-30,34`](../src/motion/hal/Esp32SpindleEncoderHAL.h:26), [`Esp32SpindleEncoderHAL.cpp:46,110-127`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:46); call sites: [`SettingsService.cpp:175`](../src/core/SettingsService.cpp:175) (drives `updateProfile` from profile config), [`MotionEngine.cpp:173-174,550-551,619-620`](../src/motion/kernel/MotionEngine.cpp:173), [`MachineModel.cpp:55-56`](../src/core/MachineModel.cpp:55).
- grep: `grep -rn "updateProfile" src` → 6 hits / 5 files (SettingsService.cpp ×1, Esp32 impl pair ×3, `ISpindleEncoderHAL.h` ×1, `MockSpindleEncoderHAL.h` ×1); `grep -rn "updateProfile" docs --include=*.md --exclude-dir=archive --exclude-dir=drift` → 0. `grep -rn "getCountsPerSpindleRev" src` → 12 hits / 5 files (MotionEngine.cpp ×6, MachineModel.cpp ×2, seam+impl+mock headers ×4); live-doc hits → 0. (counts re-verified 2026-11-03 against the tree at HEAD `15a56f6`)
- context: `getCountsPerSpindleRev()` is the 4X-CPR accessor the kernel consumes for feed math (spec states the 4× quadrature relation only in prose, §3.1 L162 and §2 [CONC-01] note; the accessor itself is undocumented); `updateProfile` re-bases PPR/teeth without PCNT re-init (its L32 comment: "without hardware re-init") — the live wiring path is `SettingsService::applyDerivedMachineConfig()` (ELS_PROPERTIES L506-507 describes *that* path for DRO scale factors only, not encoder geometry).
- origin: seam extras: [`ISpindleEncoderHAL.h:34`](../src/motion/hal/ISpindleEncoderHAL.h:34) and `getCountsPerSpindleRev` region blame `5051263` 2026-10-20 `fix(firmware): align spindle CPR with 4X quadrature and remove magic fallbacks` (CPR-alignment commit — introduced the accessor deliberately); §3.2 block last touched rebrand 2026-10-20 but its content froze at 2026-10-15 baseline. Code newer.
- direction: code→doc
- corroboration: ELS_PROPERTIES L506/507 corroborates the *adjacent* re-base path (`zScale` via `applyDerivedMachineConfig`) — different symbol, same mechanism family; no doc states the encoder geometry methods.
- suggested_resolution: fix-doc — §3.2 gains both signatures (incl. the "default body = optional override" convention the seam uses) + §3.1.1 gets the counts/rev formula (`4 × nativePpr × ratio`, [`:126`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:126)). Assessment.

### S3-02 §3.2 omits the four simulation hooks; their contract home is HIL §2.1
- group: K3
- drift: the seam's simulation surface is fully specced — but in the *test* spec, not in its governing HAL §3.2; the seam's L6 citation ("Conforms to §3.2") names a block that lacks them.
- doc_anchor: HAL §3.2 [`:178-239`](../docs/motion/HAL_SPECIFICATION.md:178) — zero hook mentions; HAL-01 line [`:36`](../docs/motion/HAL_SPECIFICATION.md:36) mentions "simulation hooks" only in passing cross-ref. HIL §2.1 [`HIL_TEST_HARNESS_SPECIFICATION.md:63-101`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:63) is the seam listing (4/4 hooks, matching comments).
- code_anchor: seam [`ISpindleEncoderHAL.h:79-103`](../src/motion/hal/ISpindleEncoderHAL.h:79); impl [`Esp32SpindleEncoderHAL.h:41-44`](../src/motion/hal/Esp32SpindleEncoderHAL.h:41), [`Esp32SpindleEncoderHAL.cpp:234-252`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:234); mock [`MockSpindleEncoderHAL.h:82-98`](../src/motion/hal/MockSpindleEncoderHAL.h:82).
- grep: `grep -rn "setSimulationActive|isSimulationActive|injectSimulatedPulses|setSimulatedRpm" docs --include=*.md` → 8 hits, all in HIL_TEST_HARNESS; 0 in HAL_SPEC.
- context: pairing, not absence: the contract exists and is complete, but reachable only from the testing spec. Per the spine rule the seam→§3.2 citation must be extended (§3.2 pointer to HIL §2.1 + [HAL-01]), otherwise the seam reads as non-conforming.
- origin: hooks landed `4d7b895` 2026-10-16 (`feat(test): …simulation injection bridge and HIL harness specification`); HIL §2.1 last touched `e636302` 2026-10-29 (newer); HAL §3.2 rebrand-only 2026-10-20. Code landed with its spec the same day; only the *pointer* lags on the HAL side.
- direction: code→doc (placement gap) — genuinely one-sided on the pointer; contract itself in-sync.
- corroboration: yes — HIL §2.1 states the whole seam; the laggard is HAL_SPEC §3.2.
- suggested_resolution: fix-doc (HAL §3.2 gains a cross-ref line to HIL §2.1; single-source stays with HIL). Assessment.

### S3-03 Index-pulse semantics: §3.1 prose "hardware reset" vs code = latch + timestamp (index counter never reset)
- group: K3
- drift: §3.1 says the Phase-Z pulse is a "hardware reset for … synchronization"; the code latches a flag + records `m_lastIndexPulseTimeUs` and never touches the PCNT count on index.
- doc_anchor: [`HAL_SPECIFICATION.md:163`](../docs/motion/HAL_SPECIFICATION.md:163) "Optional Index pulse (Phase Z) hardware reset for single-point thread threading synchronization."
- code_anchor: seam [`:70-77`](../src/motion/hal/ISpindleEncoderHAL.h:70) (`hasIndexSynced`, `resetIndexSync`), spec block [`:226-238`](../docs/motion/HAL_SPECIFICATION.md:226) matches those names (in-sync 2/2); impl [`indexIsrHandler:30-36`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:30) sets `m_hasIndexSynced` + `m_lastIndexPulseTimeUs` ([`:74`](../src/motion/hal/Esp32SpindleEncoderHAL.h:74) member), rising-edge attach [`:94`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:94).
- grep: `grep -rn "lastIndexPulseTimeUs" src docs` → 2 src hits (decl + ISR write), 0 reads anywhere in `src` (write-only state), 0 doc hits.
- context: the observable contract (sync flag + explicit re-arm) matches the seam; the prose's "hardware reset" overstates (the counter keeps running — consistent with the [HAL-01] never-clear invariant the impl honors). One doc word, two code behaviors (flag in-sync, timestamp dead).
- origin: seam/index members `ec2753b` 2026-10-15 + `4d7b895`; §3.1 prose baseline 2026-10-15. Same-day pair; internally: the timestamp field is never read → dead state.
- direction: both (prose word overstated; code carries unused state)
- corroboration: `m_lastIndexPulseTimeUs` dead-state corroborated by 0-read grep across `src` — no doc states or denies it.
- suggested_resolution: fix-doc (§3.1 reword "index latch + timestamp record") + fix-code candidate (dead timestamp member → retire or expose accessor; operator's) — one discussion retires both rows. Assessment.

### S3-04 [HAL-01] verification: 16-bit modular path + 500 ns glitch filter + boot-clear all match (spec §3.1 ↔ impl)
- group: K3 — verification row, in-sync
- drift: falsifiable claim that impl obeys [HAL-01]; it does — filed as a verification anchor, not a drift row.
- doc_anchor: [HAL-01] [`HAL_SPECIFICATION.md:164-173`](../docs/motion/HAL_SPECIFICATION.md:164) (never-clear during operation, free-run [-32768,+32767], modular snippet); §3.1 glitch [`:174`](../docs/motion/HAL_SPECIFICATION.md:174) ≈500 ns.
- code_anchor: [`Esp32SpindleEncoderHAL.cpp:51-65`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:51) (limits + 500 ns filter), [`:98`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:98) boot-only clear ("Initial boot-time zero ONLY" — permitted by §3.1's "during operation" qualifier), [`:173-185`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:173) modular delta (matches spec snippet byte-for-byte, L168-173).
- grep: `grep -rn "pcnt_unit_clear_count" src/motion/hal` → 2 hits (init boot + [`DRO_Scale.cpp:73`](../src/motion/DRO_Scale.cpp:73)); modular tokens `current16|delta16|m_lastCount16` → spec 3 hits, impl 3 hits.
- origin/corroboration: code `ec2753b` 2026-10-15; CONCURRENCY §4.1 restates the identical constraint ([`:207-216`](../docs/motion/CONCURRENCY_AND_TIMING.md:207) + identical snippet) — two live docs, one code shape: consistent.
- suggested_resolution: no action; anchor for later discussion. In-sync (verification).

### S3-05 HIL §2.1.1 listing zeroes `m_pendingSimPulses` on deactivate; code does not (and the doc's own prose says Core 1 owns cleanup)
- group: K3 (witness-doc listing vs impl)
- drift: the HIL spec's concrete listing clears pending pulses in `setSimulationActive(false)`; the shipped impl stores `m_simRpm` only, leaving pending-drain to the Core-1 transition handler — the spec listing is stale even though the newer spec restates the ownership model in prose.
- doc_anchor: [`HIL_TEST_HARNESS_SPECIFICATION.md:124-130`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:124) shows `m_pendingSimPulses.store(0)` inside the deactivate branch; prose [`:106`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:106) ("Core 0 only flips `m_simActive`") corroborates the code shape on the *other* side.
- code_anchor: [`Esp32SpindleEncoderHAL.cpp:234-240`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:234) — `if (!active) { m_simRpm.store(0); }`, no pending store; transition handler [`:133-144`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:133) (Core-1 edge detect; drains via [`:157-162`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:157)).
- grep: `grep -rn "pendingSimPulses.store" src docs` → 1 doc hit (HIL L127), 0 src hits; `grep -rn "pendingSimPulses" src/motion/hal/Esp32SpindleEncoderHAL.cpp` → 4 hits (decl, edge-handler untouched, fetch_sub drain, inject fetch_add).
- origin: impl body `7ecdd23` 2026-10-19 (`feat(hil,firmware): harden HIL test harness…`); HIL doc `e636302` 2026-10-29 → doc newer than code, yet the doc's listing still shows the older shape the code never had (git shows `setSimulationActive` body never contained a pending-store after 7ecdd23). Doc edit ≠ listing correctness; `origin` carries doc-newer but `corroboration` (doc prose + code comment agree with *code*) shows the listing — not the semantics — is the laggard.
- direction: doc→code (listing staleness only; the prose contract is in-sync)
- corroboration: code comment L235 + HIL prose L106 state the ownership rule identically — semantics in-sync; the listing at L124-130 is the stale artifact. Behaviorally one-sided pending-leak note: an injected-but-undrained pending count survives deactivation (drained only while sim stays active) — flagged for operator, not asserted.
- suggested_resolution: fix-doc (HIL listing → shipped shape). Assessment.

### S3-06 Slew chunk: HIL says "≈600 RPM" per tick; code ceiling is `m_maxSimPulseChunk{96}` = ~1200 RPM at 4800 CPR
- group: K3 (witness-doc claim vs code constant)
- drift: the harness spec's slew bound understates the shipped chunk by a factor of two at the shipped default CPR.
- doc_anchor: [`HIL_TEST_HARNESS_SPECIFICATION.md:107`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:107) ("drained at a realistic maximum chunk per tick (equivalent to $600\text{ RPM}$)").
- code_anchor: [`Esp32SpindleEncoderHAL.h:55`](../src/motion/hal/Esp32SpindleEncoderHAL.h:55) `m_maxSimPulseChunk{96}; // Max pulses consumed per tick (~1200 RPM at 4800 CPR)`; drain [`Esp32SpindleEncoderHAL.cpp:157-162`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp:157-162) `std::clamp(pending, ±chunk)`.
- grep: `grep -rn "maxSimPulseChunk" src` → 2 hits (decl + clamp); `grep -rn "600 RPM|600\\\\text{ RPM}" docs` → 1 hit (HIL L107). Arithmetic: 96 counts ÷ 4800 CPR = 0.02 rev/tick; tick ≈1 ms → 1.2 rev/s = 1200 RPM.
- context: the code's own comment says 1200 RPM at 4800 CPR — the two live artifacts (doc prose vs code comment) disagree; CPR comes from profile (default 1200 PPR ⇒ 4800 CPR, [`:64-68`](../src/motion/hal/Esp32SpindleEncoderHAL.h:64) initializers + HAL §3.2 L191 examples).
- origin: chunk line blame — [`:55`](../src/motion/hal/Esp32SpindleEncoderHAL.h:55) falls in the `5051263` 2026-10-20 CPR-alignment band (file: 6L `5051263`, rest older); HIL prose `e636302` 2026-10-29 (doc newer by edit, code newer by intent: the alignment commit is precisely the CPR change that shifts the RPM figure). Adjudication: doc-newer-edit vs code-newer-act → direction `unknown` is honest here; filing anyway.
- direction: unknown (falsifiable numeric claim on both sides; edit dates disagree with the causal commit)
- corroboration: ELS_CORE_STATE_MODEL L272 mentions "4800 CPR" examples — no doc carries the 1200 figure.
- suggested_resolution: fix-doc or fix-code is the operator's — the number is a harness velocity budget; what matters is which bound the HIL suites assume. Assessment; filed because one of the two must change.

Mock verification retired by K3: `MockSpindleEncoderHAL.h` (fallback pairing, rule 2 — name morphology) mirrors the seam 1:1 (init sig, updateProfile, getCountsPerSpindleRev incl. the same teeth-ratio formula L60-64, hooks) — seam↔mock in-sync; E2E harness files out of scope (fence).

## K4 [`src/motion/hal/`](../src/motion/hal/) — planned seam layer (HAL §4 `IDROScaleHAL`, §5 `IEndstopHAL`) × MOTION_ENGINE ctor × build reality

Group verdict: **doc→code** — one consolidation-shaped direction retires all rows: the §4/§5 seam layer is declared in specs but was never created; no code file has ever matched those names.

### S4-01 §4/§5 declared seams — 0 code hits
- group: K4
- drift: two abstract interfaces (and README's promised `Esp32EndstopHAL.h/.cpp`) have no file, citation, or call site in the tree.
- doc_anchor: HAL §4 block [`HAL_SPECIFICATION.md:247-273`](../docs/motion/HAL_SPECIFICATION.md:247) (`IDROScaleHAL`: init/getPositionDu/zero/setPositionDu), §5 block [`:281-315`](../docs/motion/HAL_SPECIFICATION.md:281) (`ENDSTOP_Z_MIN…ESTOP` enum + init/isAsserted/getAssertedMask/isEmergencyStopAsserted); README:43,51,85,86,89; MOTION_ENGINE ctor block [`MOTION_ENGINE_SPECIFICATION.md:387`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md:387) 4-arg ctor + m_droHal/m_endstopHal [`:422-423`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md:422).
- code_anchor: `ls src/motion/hal` → 9 files, none of these; ctor [`MotionEngine.h:70`](../src/motion/kernel/MotionEngine.h:70) is 2-arg (`setStepperHAL`/`setEncoderHAL` at [`:125-127`](../src/motion/kernel/MotionEngine.h:125) style wiring confirmed via [`main.cpp:73-75`](../src/main.cpp:73)).
- grep: `grep -rn "IDROScaleHAL\|IEndstopHAL\|Esp32EndstopHAL" src` → 0; docs (live) → 15 hits; `git log --grep IDROScaleHAL` → no landing commit (unbuilt, not retired — no history shape).
- context: §1.1 diagram (HAL L30 box) draws `IDROScaleHAL` as Layer-1; README labels the tree as the planned architecture. This is the unbuilt-adoption frontier pair, and the live bridge at the boundary is the monolithic `DRO_Scale` (K5).
- origin: spec-side all present at 2026-10-15 baseline, last touched rebrand 2026-10-20; code side: no history exists — direction carried by "docs since baseline, code never landed" → doc→code (planned-vs-built).
- direction: doc→code (one verdict for the four doc sites + ctor-arity row)
- corroboration: README's own "planned" framing (L85 tree comment) marks this as intent, not build-state — so the drift is narrow: the *spec* presents planned seams as conformance contracts (HAL §4 "Conforms to…" would-be pairings) while README marks them as plan. The laggard is HAL_SPEC + MOTION_ENGINE ctor; README is the honest witness.
- suggested_resolution: fix-doc (HAL §4/§5 marked "planned seam — adoption ADR pending" with README as witness) — or Class-C-adjacent decision (adopt seams / retire sections) is the operator's. Assessment.

### S4-02 MOTION_ENGINE ctor arity (4-arg spec vs 2-arg code)
- group: K4
- drift: the spec's MotionEngine ctor takes the DRO/endstop seams; the shipped ctor takes stepper+encoder only, via setters.
- doc_anchor: [`MOTION_ENGINE_SPECIFICATION.md:387`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md:387); private members [`:422-423`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md:422).
- code_anchor: [`MotionEngine.h:70`](../src/motion/kernel/MotionEngine.h:70) ctor; setter block region [`:125-127`](../src/motion/kernel/MotionEngine.h:125) read (setStepperHAL/setEncoderHAL); no `m_droHal`/`m_endstopHal` (0 hits).
- grep: `grep -c "m_droHal\|m_endstopHal" src/motion/kernel/MotionEngine.h` → 0; spec block hits 4.
- context: pair-2 boundary — MOTION_ENGINE §1-§7 not read here; only the ctor block was opened to adjudicate this seam-adoption row (cross-ref row owned by K4's direction verdict; deeper ctor audit deferred to pair 2).
- origin: ctor line `06db3a1` 2026-10-15; doc baseline 2026-10-15, rebrand-later — same-day birth, code shipped 2-arg from the start.
- direction: doc→code (same as group)
- corroboration: README "planned" framing again corroborates intent; ADR-0005 is decision authority but not a drift source (fence: `docs/adr/` authority-only) — unread here, boundary noted.
- suggested_resolution: same one edit as S4-01 (one verdict retires the row pair). Assessment.

## K5 [`src/motion/DRO_Scale.h/.cpp`](../src/motion/DRO_Scale.h) — concrete optical DRO × HAL §4 (fallback pairing: rule 1 — class-shape + symbol grep; recorded in each `grep` field)

Group verdict: **both**, one change-event: the live concrete driver is a pre-modularization h5-era file (Class-C provenance, filed not patched), and §4 describes an unbuilt abstract seam over it.

### S5-01 `DRO_Scale` concrete class carries §4-era ops + extras (§4 seam itself never extracted)
- group: K5
- drift: §4's interface (axis-aware, du-units, `zero`) does not match the live monolithic class (global `xScale`/`zScale`, float raw/mm positions, mm-named ops, scale-factor setter/getter §4 lacks); blame shows the file lived at `h5/include/…`/`h5/src/…` until 2026-10-16 modularization.
- doc_anchor: [`HAL_SPECIFICATION.md:256`](../docs/motion/HAL_SPECIFICATION.md:256) (`init(AxisId, pinA, pinB, resolutionNm)`), [`:261`](../docs/motion/HAL_SPECIFICATION.md:261) `getPositionDu(AxisId)`, [`:266`](../docs/motion/HAL_SPECIFICATION.md:266) `zero(AxisId)`, [`:271`](../docs/motion/HAL_SPECIFICATION.md:271) `setPositionDu(AxisId, posDu)`; README L85/89 tree promises the seam files.
- code_anchor: [`DRO_Scale.h:27`](../src/motion/DRO_Scale.h:27) ctor (pins+unit+scale+filter, no AxisId), [`:29`](../src/motion/DRO_Scale.h:29) `attach`, [`:30`](../src/motion/DRO_Scale.h:30) `getPosition` (float, raw+offset), [`:31`](../src/motion/DRO_Scale.h:31) `clearCount`, [`:34-37`](../src/motion/DRO_Scale.h:34) `getRawPosition`/`setPosition`/`setScaleFactor`/`getScaleFactor`, [`:40-41`](../src/motion/DRO_Scale.h:40) global `xScale`/`zScale`; impl ctor [`:23-30`](../src/motion/DRO_Scale.cpp:23), overflow watchpoints [`:11-21,64-70`](../src/motion/DRO_Scale.cpp:11), setScaleFactor guard [`:105-109`](../src/motion/DRO_Scale.cpp:105).
- grep: pairing-fallback record: `grep -rn "DRO_Scale" docs --include=*.md` → 0 live-doc hits (archive hits void); symbol fallback: `grep -rn "Z_SCALE_PULSES_PER_MM\|X_SCALE_PULSES_PER_MM\|xScale\|zScale" docs` → ELS_PROPERTIES L69, L506, L507 (names the *macros* the concrete file instantiates, [`:45/50`](../src/core/BoardPins.h:45)); `grep -rn "DRO_Scale\|xScale.attach" src docs` src side → [`main.cpp:7,45-51`](../src/main.cpp:45) boots them under `CAP_DRO_X/Z`.
- context: units disagree (spec: deci-microns via du; code: float mm ÷ pulsesPerMm); naming (`zero` vs `clearCount`, `getPositionDu` vs `getPosition`+`getRawPosition`); §4 is axis-object-oriented, code is two globals + AxisId absent; scale-factor mutation (live path ELS_PROPERTIES L506-507 describes it for `applyDerivedMachineConfig`) has no §4 home. §4's "Conforms to"-era pairing would pair to a file that was never created.
- origin: code anchors: L27/attach region `acf333a` 2026-09-16 blame-path `h5/include/DRO_Scale.h` (Class C: retired-monolithic origin filed, not patched — the brief's class-C rule for retired code covers this file's pre-modularization lineage: paths `h5/src/DRO_Scale.cpp` 2026-09-13 `Checkpoint` `460f124`, `h5/include/DRO_Scale.h`); setScaleFactor pair `ef835f5` 2026-10-20 `refactor(dro): add dynamic scale factor setter and getter` (code-newer); §4 text since 2026-10-15 baseline. Doc last-edit 2026-10-20 (rebrand) — same date band as the setter commit; adjudication: code newer for S5-01c (setter row), doc→code for seam-shape rows (seam never landed).
- direction: both (seam-shape doc→code via never-landed seam; scale-factor surface code→doc)
- corroboration: ELS_PROPERTIES already states the dynamic scale-factor path (live witness on the code side → ELS_PROPERTIES is *not* the laggard for S5-01c; HAL §4 is); blame's `h5/…` paths are the git-history shape the fence says "git history covers better".
- suggested_resolution: fix-doc — §4 gains a "live concrete driver today: `src/motion/DRO_Scale.*` (monolithic, two global instances, float-mm positions)" note + the seam-adoption pointer to S4-01's decision. One group discussion retires the seam/units/naming rows. Assessment.

### S5-02 §4-era operation *semantics* match the impl (verification anchor)
- group: K5
- drift: falsifiable claim that attach/clearCount/setPosition offsets behave per §4's math — they do for the ops that exist.
- doc_anchor: [`HAL_SPECIFICATION.md:266`](../docs/motion/HAL_SPECIFICATION.md:266) "Zeros the scale hardware accumulator at current position"; [`:271`](../docs/motion/HAL_SPECIFICATION.md:271) "Sets absolute position preset".
- code_anchor: [`clearCount:96-103`](../src/motion/DRO_Scale.cpp:96) zeros overflow + PCNT count + offset; [`setPosition:91-94`](../src/motion/DRO_Scale.cpp:91) offset = desired − raw.
- grep: `grep -rn "overflowCount" src/motion/DRO_Scale.cpp` → 6 hits; semantics visible in the read set.
- direction: in-sync (verification; naming/units stay filed above)
- suggested_resolution: no action.

### S5-03 §5 (endstop/E-stop HAL) has a DRO-adjacent boot claim the code contradicts elsewhere
- (folded) — §5 unbuilt already carried by S4-01; the physical pin *presence* (DRO pins, [`:42-49`](../src/core/BoardPins.h:42)) has no live-doc pin home (S6-01, below).

## K6 [`src/core/BoardPins.h`](../src/core/BoardPins.h) + [`src/main.cpp`](../src/main.cpp) — board pins and task wiring × HAL §2.2 signature args + CONCURRENCY §2 diagram/§5 (fallback pairing rule 1: symbols are call args / task-wiring implemented in `main.cpp`; recorded here)

Group verdict: **both** — pins: code exists, doc home missing (code→doc); task wiring: diagram claims shipped priorities the code doesn't have (doc→code). One group, two directions explained by the same event family (docs rebrand + code untouched in h5-era lines).

### S6-01 No live doc names the physical pin map
- group: K6
- drift: the board's copper-trace pin numbers exist only in code; the specs that use them (configureAxis/init call args, DRO rows) never cite them.
- doc_anchor: nearest: HAL §2.2 `configureAxis(…, stepPin, dirPin, enablePin, …)` [`:87`](../docs/motion/HAL_SPECIFICATION.md:87); ELS_PROPERTIES L506/507 (`Z_SCALE_PULSES_PER_MM` names a macro in BoardPins.h); [LATHE_MACHINE_PROFILE.md:190](../docs/machine/LATHE_MACHINE_PROFILE.md:190) `alarmActiveLow` (unread doc; grep sighting); WHATELS_MASTER_ARCHITECTURE L61/L65 `taskWiFi`. No live doc contains ENC_A/Z_STEP/Z_ENA pin values.
- code_anchor: [`BoardPins.h:17-18`](../src/core/BoardPins.h:17) ENC 14/13; [`:21-26`](../src/core/BoardPins.h:21) Z ENA/DIR/STEP 41/42/35 + PULSE A/B; [`:28-32`](../src/core/BoardPins.h:28) X 16/15/7 + PULSE; Y reserved [`:35-39`](../src/core/BoardPins.h:35); DRO scales [`:42-49`](../src/core/BoardPins.h:42); debug UART [`:53-55`](../src/core/BoardPins.h:53) (consumed at [`main.cpp:23-26`](../src/main.cpp:23)); call sites [`main.cpp:64-65`](../src/main.cpp:64) (`configureAxis(AXIS_Z, Z_STEP, Z_DIR, Z_ENA, …)`), [`:71`](../src/main.cpp:71) (`init(ENC_A, ENC_B, indexPin…`) — note `indexPin` here comes from profile (L68: `profile.spindle.encoder.indexPin`), while BoardPins defines **no** index pin for the spindle encoder.
- grep: `grep -rn "Z_STEP\|ENC_A\|BoardPins" docs --include=*.md` → 8 hits, all `CAP_*` capability tokens, zero pin numbers; `grep -rln "BoardPins" docs` → 0.
- context: LATHE_MACHINE_PROFILE (811L, unread) is the natural home for a machine-pin table — its existence/currency on pins is unchecked this pass; filed as a doc-pairing gap regardless (0 live-doc hit for the pin tokens proves no doc names them *as pins*; if LATHE_MACHINE_PROFILE carries them under different tokens, the gap narrows and the pairing pointer still lands). The spindle-index asymmetry (profile supplies indexPin; encoder has no index macro home) is context, not a claim.
- origin: BoardPins.h 55L all `b048559` 2026-10-20 `refactor(core): modernize Config and isolate board pins into BoardPins.h` (isolation commit = deliberate code-new event); doc side: no doc ever held these values (nothing to be stale — the pairing never existed).
- direction: code→doc (pairing gap; one-sided — nothing doc-side leads or lags, no contract was ever stated)
- corroboration: none live (grep proves doc-absence).
- suggested_resolution: fix-doc — LATHE_MACHINE_PROFILE gains (or already holds; verify next pass) the canonical pin table and HAL §2.2/DRO sections point at [`src/core/BoardPins.h`](../src/core/BoardPins.h). Assessment; the "or already holds under other tokens" caveat belongs to the operator's discussion, not resolved here.

### S6-02 CONCURRENCY §2 diagram task boxes vs shipped wiring (Core-0 taskWiFi priority/stack; HMI box; IELSMode placement)
- group: K6
- drift: the architecture diagram asserts shipped task parameters the code does not match: `taskWiFi` runs at priority 0, stack 10000 (diagram: Priority 3, 8KB); the "Command Router / HMI Dispatcher (Priority 5, 8KB)" box has no dedicated FreeRTOS task in `src`; Core-1 half (Priority 22, motion task) matches.
- doc_anchor: [`CONCURRENCY_AND_TIMING.md:44-49`](../docs/motion/CONCURRENCY_AND_TIMING.md:44) (Web/WS task "(Priority 3, 8KB)"; HMI Dispatcher "(Priority 5, 8KB)"); [`:61-63`](../docs/motion/CONCURRENCY_AND_TIMING.md:61) Core-1 "(Priority 22, 12KB Internal SRAM, IRAM_ATTR)"; [`:241-245`](../docs/motion/CONCURRENCY_AND_TIMING.md:241) §4.4 lockout snippet (verification row, below).
- code_anchor: [`main.cpp:85`](../src/main.cpp:85) `xTaskCreatePinnedToCore(taskWiFi, "taskWiFi", 10000, NULL, 0, NULL, 0)`; [`:81`](../src/main.cpp:81) `engine.startTask(22, 1)` (matches diagram box); no `taskDisplay`/HMI task: `grep -rn "taskDisplay" src` → 0 (supervisory loop runs in the Arduino main thread, [`main.cpp:90-110`](../src/main.cpp:90)).
- grep: `grep -rn "taskWiFi" src docs` → src: main.cpp (create + wifi-gate L83), docs live: CONCURRENCY §2 diagram box, WHATELS_MASTER_ARCHITECTURE L61/L65 ("taskWiFi, taskDisplay"), CLIENT_IMPLEMENTATION_GUIDE L724, ELS_CORE_STATE_MODEL L319 — none carries the numeric priority/stack except the diagram; `grep -n "xTaskCreatePinnedToCore" src/main.cpp` → 1 hit.
- context: WHATELS_MASTER_ARCHITECTURE L61 also names `taskDisplay` — retired-surface (display decommissioned per WHATELS_MASTER_ARCHITECTURE L131, Class-C filed below), so the live "Command Router (Priority 5, 8KB)" box has no shipped counterpart at all; taskWiFi priority 0 (lowest) is the h5-era default never re-tuned — §4.4's Core-0 starvation model (L237-245) depends on that box being accurate.
- origin: doc diagram since 2026-10-15 baseline (rebrand-only later); taskWiFi line `acf333a` 2026-09-16 (blame path `h5/src/main.cpp` — pre-modularization Class-C lineage) → the *code* line predates the doc; no history ever changed the priority. Neither side proven newer; the claim was simply never enacted.
- direction: doc→code (diagram asserts intent never shipped) — with a Class-C provenance component on the h5-era line
- corroboration: three other live docs reference taskWiFi by name without contradicting (WMA L61/L65, CLIENT_GUIDE L724, CORE_STATE L319) — the numeric boxes are the only over-specific sites; git shape: `h5/src/main.cpp` path blame proves the wiring predates the modular baseline (history covers it, fence note).
- suggested_resolution: fix-doc (diagram boxes annotated with shipped reality, or priority/stack retuned — product decision for the starvation model). Assessment; IELSMode-on-Core-1 leg verified in-sync (below), so the row only carries the Core-0 boxes.

### S6-03 §4.4 CONC-02 in-motion flash lockout: gate shipped, message string differs
- group: K6 (verification + string-drift pair)
- drift: the lockout exists on both gates ([SAFE-01] MOTION_LOCKED in CoreCommandService; ERR_MOTION_ACTIVE in SettingsService) but the spec's exact reply string doesn't match either.
- doc_anchor: [`CONCURRENCY_AND_TIMING.md:242-245`](../docs/motion/CONCURRENCY_AND_TIMING.md:242) snippet returns `"ERR:SETTINGS=cannot write to NVS flash while motion is active\n"`.
- code_anchor: [`SettingsService.cpp:1427`](../src/core/SettingsService.cpp:1427) replies `ERR:SETTINGS=ERR_MOTION_ACTIVE: cannot write to NVS flash while motion is active\n` (+ L527, L605 same shape on reset/resetGroup); [`CoreCommandService.cpp:22`](../src/core/CoreCommandService.cpp:22) [SAFE-01] returns reason `MOTION_LOCKED`.
- grep: `grep -rn "cannot write to NVS flash while motion is active" src docs` → 3 src hits + 1 doc hit (all src carry the extra `ERR_MOTION_ACTIVE: ` prefix); `grep -rn "isAnyAxisMoving" src` → 7 hits (gates real).
- context: mechanism in-sync; the wire string is a client-visible token — ELS_PROPERTIES/ADR-0003 (unread) define the result channel where the reason token lives; that string comparison drifts at the wire level.
- origin: doc snippet since 2026-10-15 (rebrand-only after); code sites `ce3a29c`-era + SettingsService commits through 2026-10-20. The prefixed string shape predates the doc's bare string → doc's snippet is simplified, not authoritative; carried doc→code-lite (string), mechanism in-sync.
- direction: doc→code (string form only)
- corroboration: gate presence corroborated by ELS_PROPERTIES L506-507 dynamic-config path (not read fully) — presence, not the string.
- suggested_resolution: fix-doc (snippet → shipped string) mechanical. Assessment; pair-5/6 wire audit may re-open this token at the client level.

### S6-04 §5 failsafe hardware watchdog + 200 ms jog-lease: spec claims no Core-1 implementation found in this read set
- group: K6 (doc claims; code home outside pair-1 files — filed with boundary)
- drift: §5 prescribes a 50 ms Core-1 watchdog (GPIO de-assert + VFD relay open + RTC panic log) and a 200 ms jog lease on Core 1; this pass found no impl in its read set; `grep -rniw watchdog src` → only MotionEngine.h L129 *sim* watchdog + WifiService lifecycle (different concern).
- doc_anchor: [`CONCURRENCY_AND_TIMING.md:253-258`](../docs/motion/CONCURRENCY_AND_TIMING.md:253) (watchdog 50 ms, "wired directly to Core 1 hardware interrupts" L263), [`:264`](../docs/motion/CONCURRENCY_AND_TIMING.md:264) (jogHold 200 ms lease; halt on dropped packets).
- code_anchor: not in pair-1 read set: [`MotionEngine.cpp`](../src/motion/kernel/MotionEngine.cpp) unread this pass (except grep sightings [`:345`](../src/motion/kernel/MotionEngine.cpp:345) `isAnyAxisMoving`, L730 snapshot publish); jog path seen only at Core-0 gate ([`CoreCommandService.cpp:82-95`](../src/core/CoreCommandService.cpp:82)); the lease watchdog would live on Core 1 in unread kernel files.
- grep: `grep -rniw "watchdog" src` → 3 hits (sim-watchdog comment, WifiService.h lifecycle ×2 — read set boundary); `grep -rn "jogHold\|jogRelease" src` → CoreCommandService gate + CoreCommands.h docblock pair; docs → §5 claim + CANONICAL (unread).
- context: deliberately filed-unfinished: the claim may be satisfied in the unread kernel (MotionEngine.cpp/SpindleSynchronizer.cpp). This is not a drift assertion; it is an open pairing whose verification range lands in pair 2's read set.
- origin: doc since baseline (rebrand-only 2026-10-20); code side undetermined.
- direction: unknown (pairing incomplete at this pass's boundary)
- corroboration: none in this pass.
- suggested_resolution: none this pass — hand to pair 2 (kernel); keep the row open so the claim doesn't read as absent-evidence.

## K7 [`src/motion/contract/`](../src/motion/contract/) — `TelemetryMailbox.h` / `MotionCommandPacket.h` × CONCURRENCY §3.1/§3.2 (citation-spine pair — spine adds it to pair 1; drift-map S1 rows were partial)

Group verdict: **code→doc**, one-sided rows only — both structs' fields verified 1:1; the drift is defaults/enums §3.2 doesn't state.

### S7-01 §3.2 packet — fields in-sync, defaults + enum count drift
- group: K7
- drift: §3.2's `MotionCommandPacket` listing shows bare fields; the shipped struct carries defaults (`direction = 0` — a third polarity the spec's `−1/+1` comment never names, `respectSoftLimits = true`, `secondaryRatioDen = 1`) and §3.2 block matches the enum 12/12 (verified) while §3 intro L163 attributes command production to "Web UI or Nextion display" (Nextion retired — Class-C filed at S7-03 below).
- doc_anchor: [`CONCURRENCY_AND_TIMING.md:165-195`](../docs/motion/CONCURRENCY_AND_TIMING.md:165) block (enum 12 types L167-179; fields L182-194 secondary axis [KIN-04]); L163 producer prose.
- code_anchor: [`MotionCommandPacket.h:10-37`](../src/motion/contract/MotionCommandPacket.h:10) — enum 12/12 identical; defaults L25-36.
- grep: `grep -c "START_SPINDLE_SYNC" src docs`-family diffed visually (block ↔ struct, 12/12); `grep -rn "direction           = 0\|direction *= *0" src` → 1 (packet default); doc polarity comment [`:183`](../docs/motion/CONCURRENCY_AND_TIMING.md:183) `(−1 Chuck/Centerline…)` omits 0.
- context: `direction = 0` (neutral) is consumed by the kernel (unread) — the spec's own `setDirection` polarity pair (HAL §2.2 L93-95: −1/+1) has no doc home for the neutral packet value; KIN-04 secondary fields documented in MOTION_ENGINE (grep sighting L415 — block read for K4 only, full audit pair-2).
- origin: struct `06db3a1` 2026-10-15 (`ec2753b`-era HAL commit; KIN-04 fields landed `bef5d65` 2026-10-29 per drift-map/blame band); §3.2 block since baseline. Mixed age: enum band older than the doc baseline? No — struct 2026-10-15 precedes doc baseline 2026-10-15 by day-parity; KIN-04 fields code-newer (2026-10-29) vs doc block frozen — §3.2 L189-193 already *lists* secondary fields, so that leg is doc-current (in-sync). The 0-polarity default is the one-sided row.
- direction: code→doc (the neutral-polarity default; one-sided, no doc-side lead)
- corroboration: none; HAL §2.2's −1/+1 pair is the nearest doc shape.
- suggested_resolution: fix-doc (§3.2 packet comment gains the neutral default) — one-line, mechanical. Assessment.

### S7-02 §3.1 TelemetryMailbox — struct 12/12 fields + seqlock impl verified byte-shape in-sync
- group: K7 — verification anchor, in-sync
- drift: falsifiable claim: impl obeys §3.1 (odd/even seq, fences, ≤4 retries, `IRAM_ATTR publish`). It does: [`TelemetryMailbox.h:38-69`](../src/motion/contract/TelemetryMailbox.h:38) ↔ spec block L100-160 — identical fences, identical retry budget, identical publish ordering; struct [`:16-29`](../src/motion/contract/TelemetryMailbox.h:16) ↔ spec L106-119 fields 12/12 (verified).
- grep: `grep -rn "atomic_thread_fence" src/motion/contract/TelemetryMailbox.h docs` → 2+2; `retries < 4` → 1+1; `IRAM_ATTR publish` → 1+1.
- direction: in-sync (verification)
- corroboration: [CONC-01] tag itself cross-doc'd (README, §2 diagram); no laggard found for this pair in pair-1 files.
- suggested_resolution: none.

### S7-03 Class-C references filed (retired code — DO NOT PATCH, filed only)
- group: K7 filing row (no verdict — filing)
- [`CommandDispatcher.cpp`]: [`HIL_TEST_HARNESS_SPECIFICATION.md:105`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:105), [`:188`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:188), [`:231`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:231), [`:233`](../docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md:233); [`ELS_PRESET_CATALOG.md:185`](../docs/state/ELS_PRESET_CATALOG.md:185); [`ELS_MODE_ARCHITECTURE.md:992`](../docs/modes/ELS_MODE_ARCHITECTURE.md:992) — file absent (parser home is `core_client/StandardCommandParser.*`, per brief's subsystem table).
- `src/display/Display.cpp`, `taskDisplay`: [`WHATELS_MASTER_ARCHITECTURE.md:131`](../docs/WHATELS_MASTER_ARCHITECTURE.md:131) (retirement notice — self-referential Class-C: the *notice* is fine, the *references* in L61/L65 boxes are stale surfaces), L61 `taskDisplay` (0 in src).
- `Input.cpp`: [`WHATELS_MASTER_ARCHITECTURE.md:209`](../docs/WHATELS_MASTER_ARCHITECTURE.md:209) (guidance example citing a retired file) — filed.
- `Axis.cpp`: 0 live-doc hits this sweep (retirement commit `3719431` is the history record).
- `RichClientAdapter`: 0 live-doc hits.
- `h5/…` blame paths (`h5/src/main.cpp`, `h5/include/DRO_Scale.h`, `h5/src/DRO_Scale.cpp`) + the fenced monolithic `.ino`: provenance witnesses only — the fence says git history covers them better; nothing patched.
- direction: n/a (filing per brief)

---

## Coverage boundary

Read to closure (this pass): [`HAL_SPECIFICATION.md`](../docs/motion/HAL_SPECIFICATION.md) L1-316 (all); [`CONCURRENCY_AND_TIMING.md`](../docs/motion/CONCURRENCY_AND_TIMING.md) L1-265 (all); all 9 [`src/motion/hal/`](../src/motion/hal/) files (StepTimingConfig.h, IStepperHAL.h/.cpp — note: `IStepperHAL.cpp` does not exist in the tree; seam + impl + mocks + configs read), [`Esp32StepperHAL.h`](../src/motion/hal/Esp32StepperHAL.h) 142L, [`Esp32StepperHAL.cpp`](../src/motion/hal/Esp32StepperHAL.cpp) 330L, [`ISpindleEncoderHAL.h`](../src/motion/hal/ISpindleEncoderHAL.h) 103L, [`Esp32SpindleEncoderHAL.h`](../src/motion/hal/Esp32SpindleEncoderHAL.h) 81L, [`Esp32SpindleEncoderHAL.cpp`](../src/motion/hal/Esp32SpindleEncoderHAL.cpp) 253L, [`MockStepperHAL.h`](../src/motion/hal/MockStepperHAL.h) (L1-70 read; seam-override surface verified, remainder mock bookkeeping — noted in S3 mock verification with that caveat), [`MockSpindleEncoderHAL.h`](../src/motion/hal/MockSpindleEncoderHAL.h) 168L; [`DRO_Scale.h`](../src/motion/DRO_Scale.h) 43L, [`DRO_Scale.cpp`](../src/motion/DRO_Scale.cpp) 114L; [`BoardPins.h`](../src/core/BoardPins.h) 55L; [`main.cpp`](../src/main.cpp) L1-110 of 114; [`MotionCommandPacket.h`](../src/motion/contract/MotionCommandPacket.h) 38L, [`TelemetryMailbox.h`](../src/motion/contract/TelemetryMailbox.h) L1-70 of 75; witness regions read before citing: HIL §1-§2.1.1 (L55-149 of 1103 — includes the L105-130 listings adjudicated in S3-05/06), MOTION_ENGINE ctor block [`:387,422-423`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md:387) via grep + grep-sighted block (full §1-§7 unread — pair 2), ELS_PROPERTIES L69/L506-507/L610-611 rows via grep (doc unread — rows quoted verbatim from grep output; full audit pair 5), CONCURRENCY §5 (read), HAL-01/CONC-01 tags (read), grep-output-verbatim lines from WHATELS_MASTER_ARCHITECTURE L61/L65/L131/L209, ELS_CORE_STATE_MODEL L272/L312/L319, CLIENT_IMPLEMENTATION_GUIDE L591/L724, ELS_MODE_ARCHITECTURE L80/L992, ELS_PRESET_CATALOG L185, HIL L105/L188/L211-217/L231-233 — these are quoted as grep hits; the docs themselves are not read to closure (witness rows carry that caveat inline).
Split note: read set completed within budget; [`plans/drift-dispatch.md`](plans/drift-dispatch.md) intentionally not created — the pair-1 hand-off row (pair 2, range: [`src/motion/kernel/`](../src/motion/kernel/) + MOTION_ENGINE §1-§7 + S6-04 lease/watchdog claim resolution) belongs to the operator after review.
Unread (declared, out of this artifact's claims): [`MOTION_ENGINE_SPECIFICATION.md`](../docs/motion/MOTION_ENGINE_SPECIFICATION.md) §1-§7 prose (only ctor block greps read), [`MODE_INTEGRATION_CONTRACT.md`](../docs/motion/MODE_INTEGRATION_CONTRACT.md), [`README.md`](../docs/motion/README.md) L56/L85-89 read via grep, all of [`src/motion/kernel/`](../src/motion/kernel/) except the ctor grep-sightings (MotionEngine.cpp/.h, SpindleSynchronizer.*, TrajectoryPlanner.*, SoftLimit*, AxisState/Geometry — S6-04's range), [`src/web/`](../src/web/), [`src/core/`](../src/core/) beyond BoardPins/DRO grep sites (SettingsService strings read as grep lines only), `data/`, `clients/`, LATHE_MACHINE_PROFILE.md (811L — pin-table existence unchecked, S6-01 carries that caveat; grep sighting L190 only), ELS_PROPERTIES §4.2/§5 tables beyond quoted rows, ADR-0001..0005 (authority-only per fence — decision authority for the S4-01 adoption verdict lives in ADR-0005, unread here), `test/`, `tests/` (fence), `reference/` (fence: blame-path witnesses only), generated ROM headers (fence), `docs/archive/**` (void — grep hits seen were filtered and are neither authority nor witness).

Every item carries its grep + hit counts; anchors resolve from the blame table above. This artifact files 21 rows (S1-01..03, S2-01..02, S3-01..06, S4-01..02, S5-01..02, S6-01..04, S7-01..03; some shared across groups); verification anchors are not drift. Nothing was repaired.
