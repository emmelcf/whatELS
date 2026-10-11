# AGENT INSTRUCTIONS: NanoELS Lathe HMI, Ergonomics & Operator Safety Rules
*Target Display:* 800×480 Capacitive Touch (Landscape)
*Display Controller:* ESP32-S3 (Web Client: HTML/CSS/JS in `clients/web_ui/` | Native HMI: C++/LVGL in `screen_lvgl/`)
*Real-Time Motion Core:* NanoELS Controller Firmware (`src/`)

---

## 0. Collaboration Mode: Pair Programming & Proactive Guidance

1. **Interactive Pair Programming Stance:**
   * Work on client UIs and features is an active, collaborative pair programming exercise. The AI agent acts as a co-engineer and domain sounding board, not a passive instruction taker.
   * **No Unilateral Execution:** Never jump to write code immediately when the user shares an idea or asks an exploratory question. Always evaluate ergonomics, physical trade-offs, and controller capabilities first, debate alternatives, and reach explicit mutual consensus before modifying files.
2. **Strict Client Folder Confinement (Zero Unintended Firmware Pollution):**
   * Web UI client development takes place in `clients/web_ui/`.
   * Native display client development takes place in `screen_lvgl/`.
   * Default firmware web assets reside in `data/www/`.
   * Never modify core controller firmware (`src/`) during pure client-side tasks without explicit discussion and consensus.
3. **Proactive Feedback & Technical Recommendations:**
   * Regularly evaluate user suggestions and provide clear technical feedback regarding shop-floor practicality, lathe ergonomics, and machinist safety.
   * Proactively offer alternative designs, surface edge cases, and recommend optimizations whenever appropriate.
4. **Firmware Ground Truth & No Ghost Controls:**
   * Validate all UX and protocol proposals against the authoritative controller architecture (`docs/CONTROLLER_ARCHITECTURE.md`) before cementing UI behavior.
   * Only support what NanoELS natively supports or what the client controller can reliably orchestrate. Never invent fake software pause states or mid-cycle step controls (e.g. "Next pass").
5. **Living Design Documentation:**
   * Ensure `docs/ELS_DESIGN_NOTEBOOK.md` and related docs are continuously updated to reflect decisions, edge cases, and architectural contracts established during pair sessions.
   * Concurrently audit existing sections whenever a change affects prior rules so documentation remains 100% truthful.
6. **Detailed Micro-Commits on Milestone Agreement & Reversion Safeguards:**
   * Whenever a feature, ergonomic refinement, or bugfix is reviewed, agreed upon, and verified, perform an immediate focused git commit to anchor progress.
   * **Itemized Commit Messages:** Never use vague "WIP" or single-line commit summaries; explicitly itemize all files touched and architectural rationale.
   * **Blanket Revert Prohibition:** The agent must **NEVER** execute indiscriminate rollbacks (`git restore .`, `git checkout -- .`, `git reset --hard`, or `git clean`). Any rollback must be surgical—targeting only the specific line range or file in progress—or explicitly confirmed with the user.

---

## 1. Core Mission & Hardware Philosophy

1. **Role of the Client Interface:**
   * The client interface serves as an **ergonomic companion and workflow orchestrator**. It does NOT alter, rewrite, or bypass native NanoELS motion core firmware.
2. **Radical Hardware Transparency (No Ghost Limits):**
   * NanoELS maintains strictly **one pair of hardware limit registers per axis** (`z_min`/`z_max`, `x_min`/`x_max`).
   * The UI must NEVER maintain separate background "software safety envelopes" while running cycles against a different boundary. Every limit chip displayed on the DRO must match 1:1 with the value loaded in the physical hardware register.
3. **Opt-In Limit Restoration Pattern:**
   * When running dimension-driven cycles (Turning, Facing, Threading, Cutoff), limit registers are overwritten to enforce feature shoulders.
   * Restoring prior envelopes is controlled by an explicit card property: `RESTORE LIMITS ON COMPLETION [YES / NO]`.
   * When `NO` (default): Native NanoELS behavior is preserved (limits stay at the shoulder).
   * When `YES`: The client caches previous limits in RAM, loads cycle boundaries into NanoELS, and re-writes the cached limits upon cycle completion or manual `STOP` abort.

---

## 2. Display & Machining Ergonomics (800×480 Fixed Layout)

1. **DRO Readouts & Typography:**
   * Locked to a fixed `50px` monospace font (`JetBrains Mono`, tabular lining figures). Dynamic font auto-shrinking is strictly prohibited to eliminate digit jitter during rapid feed.
   * Coordinate rows omit inline unit suffixes (`mm`/`in`) to guarantee fit for negative 7-figure numbers (e.g., `-300.0000`). Units are declared in the top header.
   * Diameter notation strictly uses the authentic uppercase Danish glyph (`Ø`), matching uppercase `R` (e.g., `Ø 40.000 mm`).
2. **Operator Frame of Reference (Directional Alignment):**
   * **Carriage ($Z$ Axis):**
     * `← Z CHUCK` ($Z-$) moves toward the headstock.
     * `→ Z TAIL` ($Z+$) moves toward the tailstock.
   * **Cross-Slide ($X$ Axis):**
     * `↑ TO CENTER` ($X-$) plunges into workpiece / toward spindle centerline.
     * `↓ RETRACT` ($X+$) backs out toward the operator / clears the cut.
   * *Never invert up/down arrows. Up must always mean into the cut.*
3. **Physical Apron Jog Cluster:**
   * The 3×3 D-pad and chamfer diagonal keys must remain **pinned to the bottom edge of the center column**, providing palm stability against the lathe bed rim during manual stepping.
   * Step increment selector cycles on tap through discrete standard tables (Metric: `0.010 → 0.100 → 1.000 mm`; Imperial: `0.0010 → 0.0100 → 0.1000 in`). Long-press (>600ms) summons the numeric keypad for custom increments.
4. **Universal Runstack Placement:**
   * Bottom-right quadrant must always feature `START FEED` / `START CYCLE` (Green / `#3ddc84`) directly stacked above `STOP` (Red / `#ff5252`), separated by a generous 12px safety gap. Never move or reorder this stack.
   * `STOP` is permanent and stationary across all 9 modes; visually subdued when idle, illuminating vibrant red with an active glow during motion.
5. **Universal Touch Target Geometry (5.0" 800×480 Standard):**
   * Primary action buttons: Minimum height **44px–48px**, minimum width **56px–64px**.
   * Secondary / setup chips: Minimum height **36px–38px**, minimum width **50px**.
   * Minimum separation gap: **4px–6px** between touchable zones.
   * Absolute rule: No interactive button may ever be rendered below 36px height or 48px width.
6. **Numeric Keypad Architecture:**
   * Standard 3×4 calculator layout with bottom quick-math row: `[ CURR ] [ ÷ 2 ] [ CLR ] [ ⌫ ]`.
   * Opens pre-filled with the current value and fully selected for instant overwrite.
   * `÷ 2` provides instant halving of workpiece diameters for radius calculations.
   * `INSERT CURR` is conditionally gated—visible only when editing coordinates or soft limits; hidden for pitch, DOC, speeds, or pass counts.
   * Direct focus and cursor repositioning supported.

---

## 3. Operational Modes & Dynamic Center Deck

### 3.1 Dynamic Center Action Deck
Located directly above the jog D-pad. Houses live operational apron controls only:
* **Gearbox Mode:** Houses live feed rate nudge buttons `[ − ]` and `[ + ]`, active feed readout, active axis direction toggle (`DIR: ← TO CHUCK / → TO TAIL`), and discrete step override chips. (Setup toggles `SYNC/ASYNC`, `AXIS: Z/X`, and `Z UNIT: FEED/TPI` live in the top-aligned right Mode Card).
* **Turning & Facing Modes:** Center deck displays real-time cycle status (`PASS 01 / N`) flanked by live feed nudge buttons.
* **Threading Mode:** Displays thread lead trim when stopped; switches to locked cycle telemetry (`THREADING RUNNING`, target depth, pass count) when active. All parameter editing and nudge buttons are removed during a run.

### 3.2 Mode Parameters & Logic
* **Turning Mode:**
  * Parameters: `OPERATION [EXTERNAL (OD) / INTERNAL (ID / BORE)]`, `FEED DIR [← CHUCK / → TAIL]`, `LENGTH`, `TOTAL DEPTH (Ø)`, `MAX DEPTH/PASS`, `PASSES` (two-way coupled: default $\lceil\Delta X / \text{DOC}\rceil$, editable to override), `RESTORE LIMITS`. (Feed pitch is governed exclusively in the center action deck).
  * Entering `LENGTH` computes feature shoulder boundary relative to starting coordinate and maps directly to the active hardware limit register.
* **Facing Mode:**
  * Kinematic mirror of Turning: Cutting Axis = $X$, Infeed Axis = $Z$.
  * Parameters: `STROKE DIR [↑ TO CENTER / ↓ RETRACT]`, `STROKE (Ø)`, `TOTAL DEPTH`, `MAX DEPTH/PASS`, `PASSES` (two-way coupled, editable to override), `RESTORE LIMITS`. (Facing feed is governed exclusively in the center action deck).
* **Threading Mode:**
  * Parameters: `LEAD / PITCH` (integer TPI preserved for imperial, mm pitch for metric), `PASS`, `PASS DEPTH`, `DIRECTION [RH / LH]`, `CYCLE INFEED [90° RADIAL]`.
  * Pitch and pass depth are strictly locked during active cutting; only `STOP` is permitted.
* **Mode Categorization & Cutoff Tool Alignment:**
  * Categorized into 3 functional groups: Continuous/Geared (`GEARBOX`, `JOYSTICK`), Multi-Pass CNC (`TURNING`, `FACING`, `THREADING`), and Contouring/Specialty (`TAPER`, `CUTOFF`, `SLOTTING`, `SPHERE`).
  * Cutoff / parting icon arrow points away from the operator into the cut (`↑` / `X-`).

---

## 4. Machining Edge Cases & Safety Interlocks

1. **Directional Handling in Dimension Math:**
   * If `FEED DIR` is `← TOWARD CHUCK (Z−)`:
     $$Z_{\text{SHOULDER}} = Z_{\text{START}} - \text{Length} \quad \longrightarrow \quad \text{maps to } Z_{\text{MIN}}$$
   * If `FEED DIR` is `→ TOWARD TAIL (Z+)` (back-turning or shoulder clearance):
     $$Z_{\text{SHOULDER}} = Z_{\text{START}} + \text{Length} \quad \longrightarrow \quad \text{maps to } Z_{\text{MAX}}$$
2. **Spindle Encoder Sync & Stall Watchdog:**
   * In synchronized modes, carriage motion is slaved to spindle pulses.
   * If cycle is active (`RUNNING`) but spindle `RPM < 10`:
     * Display pulsing amber warning on the dynamic deck: `WAITING FOR SPINDLE...`
   * If spindle encoder drops to 0 for $>2000\,\text{ms}$ mid-cut:
     * Flag: `SPINDLE STALLED / CYCLE PAUSED`. Motion core must pause infeed to prevent tool breakage.
3. **Asymmetric Limit Recovery (Anti-Trap Rule):**
   * **Motion Core Reality:** NanoELS does not emit a `LIMIT_HIT` packet or error code when a soft limit is contacted. It silently clamps step pulses in that direction (`delta = limit - pos = 0`).
   * **Display Controller Duty:** The client firmware/software must evaluate limit contact locally from telemetry (`distanceToStop == 0` or comparing `pos` against the active limit register).
   * **Asymmetric Visual State:**
     * The UI must NEVER disable all jog buttons.
     * The tripping direction jog button (`← Z CHUCK` or `↑ TO CENTER`) is locked/disabled with red visual warning styling (`#ff5a5f`).
     * The opposing direction jog button (`→ Z TAIL` or `↓ RETRACT`) **must remain permanently enabled and vibrant** so the operator can immediately jog out of the limit boundary.
4. **Toolpath Clearance Confirmation:**
   * In Turning/Facing, display a secondary context label on the `LENGTH` row showing the target absolute machine coordinate: e.g., `LENGTH: 25.000 mm (Cut to Z: -25.000)`.
5. **Display Controller Boot & Power-Loss Re-sync:**
   * If the client boots or resets while NanoELS is running, the client must NEVER assume $0,0$ or uninitialized limits.
   * **Boot Sequence:** Transmit hydration query (`?get=state` or canonical request) to rehydrate UI state directly from real-time hardware registers before unlocking UI controls.

---

## 5. Protocol Division (Display Companion ↔ Motion Core)

### 5.1 Multi-Command Routines (Client State Machine)
* **Dimension Setup:** `GET_POS` → calculate offset based on feed direction → `SET_LIMIT`.
* **Opt-In Limit Restore:** Read and cache limit in RAM (`saved_limit`) → transmit cycle limit → send `START_CYCLE` → listen for `STATE_IDLE` or operator `STOP` → transmit `SET_LIMIT(saved_limit)`.
* **Mode Switches:** Send `STOP_FEED` interlock → set mode ID & active axis mask → configure lead/pitch parameters.
* **Dynamic Feed Trim:** Calculate incremental pitch step locally → transmit atomic feed update without stopping carriage.

### 5.2 Pure Local UI Responsibilities (Zero Wire Bus Traffic)
* Real-time pass count and DOC step derivation ($\lceil\text{Total Depth} / \text{Max DOC}\rceil$).
* Imperial/Metric conversion ($25.4$) and Radius/Diameter scaling ($2\times$).
* Keypad buffer handling, cursor navigation, and conditional gating.
* Inactive axis styling and visual dimming.
* **Soft limit boundary contact inference and anti-trap jog styling** (`distanceToStop == 0`).

---

## 6. Verification Checklist for AI Agents
* [ ] Does the UI strictly fit within 800×480 with zero horizontal or vertical window scrollbars?
* [ ] Are coordinate displays locked to 50px JetBrains Mono tabular figures with no auto-shrinking?
* [ ] Is the D-pad pinned to the bottom of the center column with inverted cross-slide arrows (`↑` into cut, `↓` retract)?
* [ ] Are all touch targets compliant with universal sizing standards ($\ge 38\text{px}$ secondary, $\ge 44–48\text{px}$ primary, $\ge 50\text{px}$ width)?
* [ ] Are discrete standard step arrays maintained for metric vs. imperial (no awkward decimal conversions)?
* [ ] Does Threading mode preserve integer TPI and strictly lock parameter editing during active cuts?
* [ ] Does entering a feature dimension dynamically calculate and update the visible hardware limit chip?
* [ ] Does the opt-in limit restore toggle preserve and reinstate previous envelopes on cycle finish and manual stop?
* [ ] Does the jog pad allow backing out when an axis is resting against a soft limit?
* [ ] Are all Web UI client files and assets confined within `clients/web_ui/`?
* [ ] Has `docs/ELS_DESIGN_NOTEBOOK.md` been updated to reflect all decisions and the true state of the UI?
