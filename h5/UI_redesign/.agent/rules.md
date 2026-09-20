# AGENT INSTRUCTIONS: NanoELS H5 + ESP32-S3 Lathe DRO Companion
*Target Display:* 800×480 Capacitive Touch (Landscape)
*Display Controller:* ESP32-S3 (Prototype: HTML/CSS/JS | Target: C++/LVGL)
*Real-Time Motion Core:* NanoELS H5 Controller (UART / Serial)

---

## 0. Collaboration Mode: Pair Programming & Proactive Guidance

1. **Interactive Pair Programming Stance:**
   * Work on this UI redesign is an active, collaborative pair programming exercise. The AI agent acts as a co-engineer and domain sounding board, not a passive instruction taker.
2. **Proactive Feedback & Technical Recommendations:**
   * Regularly evaluate user suggestions and provide clear technical feedback regarding shop-floor practicality, lathe ergonomics, and machinist safety.
   * Proactively offer alternative designs, surface edge cases, and recommend optimizations whenever appropriate.
3. **Firmware Ground Truth:**
   * Validate all UX and protocol proposals against the actual NanoELS controller firmware (`/docs/h5.ino`) before cementing UI behavior.
4. **Living Design Documentation:**
   * Ensure `ELS_DESIGN_NOTEBOOK.md` and `rules.md` are continuously updated to reflect decisions, edge cases, and architectural contracts established during pair sessions.
5. **Micro-Commits on Milestone Agreement & Reversion Safeguards:**
   * Whenever a feature, ergonomic refinement, or bugfix is reviewed, agreed upon, and verified, perform an immediate focused git commit to anchor progress.
   * **Blanket Revert Prohibition:** The agent must **NEVER** execute indiscriminate rollbacks (`git restore .`, `git checkout -- .`, `git reset --hard`, or `git clean`).
   * Any rollback or undo must be surgical—targeting only the specific line range or file in progress—or explicitly confirmed with the user.

---

## 1. Core Mission & Hardware Philosophy

1. **Role of the ESP32-S3:**
   * The ESP32-S3 serves as an **ergonomic companion and workflow orchestrator**. It does NOT alter, rewrite, or bypass native NanoELS motion core firmware.
2. **Radical Hardware Transparency (No Ghost Limits):**
   * NanoELS maintains strictly **one pair of hardware limit registers per axis** (`z_min`/`z_max`, `x_min`/`x_max`).
   * The UI must NEVER maintain separate background "software safety envelopes" while running cycles against a different boundary. Every limit chip displayed on the DRO must match 1:1 with the value loaded in the physical hardware register.
3. **Opt-In Limit Restoration Pattern:**
   * When running dimension-driven cycles (Turning, Facing, Threading, Cutoff), limit registers are overwritten to enforce feature shoulders.
   * Restoring prior envelopes is controlled by an explicit card property: `RESTORE LIMITS ON COMPLETION [YES / NO]`.
   * When `NO` (default): Native NanoELS behavior is preserved (limits stay at the shoulder).
   * When `YES`: The ESP32-S3 caches previous limits in RAM, loads cycle boundaries into NanoELS, and re-writes the cached limits upon cycle completion or manual `STOP` abort.

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
   * Step increment selector cycles on tap (`0.010` → `0.100` → `1.000`). Long-press (>600ms) summons the numeric keypad for custom increments.
4. **Universal Runstack Placement:**
   * Bottom-right quadrant must always feature `START FEED` (Green / `#3ddc84`) directly stacked above `STOP` (Red / `#ff5252`). Never move or reorder this stack.
5. **Numeric Keypad Architecture:**
   * Standard 3×4 calculator layout with bottom row `[ CURR ] [ CLR ] [ ⌫ ]`.
   * Opens clean and empty with placeholder `0`.
   * `INSERT CURR` is conditionally gated—visible only when editing coordinates or soft limits; hidden for pitch, DOC, speeds, or pass counts.
   * Direct focus and cursor repositioning supported.

---

## 3. Operational Modes & Dynamic Center Deck

### 3.1 Dynamic Center Action Deck
Located directly above the jog D-pad. Houses large tactile `[ − ]` and `[ + ]` nudge buttons for live feed rate trimming:
* **Gearbox Mode:** Dynamic deck shows `SYNC / ASYNC` toggle and `AXIS: Z / X` selector. The unselected axis DRO row in the left column dims to 35% opacity.
* **Turning & Facing Modes:** Axis/sync toggles are stripped out. Center deck displays cycle status (`PASS 01 / N`) flanked by large feed nudge buttons.

### 3.2 Mode Parameters & Logic
* **Turning Mode:**
  * Parameters: `CUT PITCH`, `FEED DIR [← CHUCK / → TAIL]`, `LENGTH`, `TOTAL DEPTH (Ø)`, `MAX DEPTH/PASS`, `PASSES` (two-way coupled: default $\lceil\Delta X / \text{DOC}\rceil$, editable to override), `RETRACT`, `RESTORE LIMITS`.
  * Entering `LENGTH` computes feature shoulder boundary relative to starting coordinate and maps directly to the active hardware limit register.
* **Facing Mode:**
  * Kinematic mirror of Turning: Cutting Axis = $X$, Infeed Axis = $Z$.
  * Parameters: `FACING FEED`, `FINAL Ø` / `STROKE`, `TOTAL DEPTH`, `MAX DEPTH/PASS`, `PASSES` (two-way coupled, editable to override), `RESTORE LIMITS`.
* **Threading Mode:**
  * Parameters: `THREAD PITCH`, `LENGTH`, `TOTAL DEPTH`, `PASSES`, `INFEED [RADIAL (90°) / COMPOUND (29.5°)]`, `RESTORE LIMITS`.


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
   * **Motion Core Reality:** NanoELS (`h5.ino`) does not emit a `LIMIT_HIT` packet or error code when a soft limit is contacted. It silently clamps step pulses in that direction (`delta = limit - pos = 0`).
   * **Display Controller Duty:** The ESP32-S3 C/C++ firmware must evaluate limit contact locally from telemetry (`distanceToStop == 0` or comparing `pos` against the active limit register).
   * **Asymmetric Visual State:**
     * The UI must NEVER disable all jog buttons.
     * The tripping direction jog button (`← Z CHUCK` or `↑ TO CENTER`) is locked/disabled with red visual warning styling (`#ff5a5f`).
     * The opposing direction jog button (`→ Z TAIL` or `↓ RETRACT`) **must remain permanently enabled and vibrant** so the operator can immediately jog out of the limit boundary.
4. **Toolpath Clearance Confirmation:**
   * In Turning/Facing, display a secondary context label on the `LENGTH` row showing the target absolute machine coordinate: e.g., `LENGTH: 25.000 mm (Cut to Z: -25.000)`.
5. **Display Controller Boot & Power-Loss Re-sync:**
   * If the ESP32-S3 boots or resets while NanoELS is running, the display controller must NEVER assume $0,0$ or uninitialized limits.
   * **Boot Sequence:** Transmit `GET_MACHINE_STATE`, `GET_POS_ALL`, and `GET_LIMITS_ALL` to rehydrate UI state directly from real-time hardware registers before unlocking UI controls.

---

## 5. Protocol Division (Display Companion ↔ Motion Core)

### 5.1 Multi-Command Serial Routines (ESP32-S3 State Machine)
* **Dimension Setup:** `GET_POS` → calculate offset based on feed direction → `SET_LIMIT`.
* **Opt-In Limit Restore:** Read and cache limit in RAM (`saved_limit`) → transmit cycle limit → send `START_CYCLE` → listen for `STATE_IDLE` or operator `STOP` → transmit `SET_LIMIT(saved_limit)`.
* **Mode Switches:** Send `STOP_FEED` interlock → set mode ID & active axis mask → configure lead/pitch parameters.
* **Dynamic Feed Trim:** Calculate incremental pitch step locally → transmit atomic `UPDATE_GEAR_FEED` packet without stopping carriage.

### 5.2 Pure Local UI Responsibilities (Zero Serial Bus Traffic)
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
* [ ] Does entering a feature dimension dynamically calculate and update the visible hardware limit chip?
* [ ] Does the opt-in limit restore toggle preserve and reinstate previous envelopes on cycle finish and manual stop?
* [ ] Does the jog pad allow backing out when an axis is resting against a soft limit?