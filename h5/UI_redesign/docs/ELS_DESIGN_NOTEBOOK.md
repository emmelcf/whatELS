# NanoELS H5 + Custom ESP32-S3 Display Controller
## System Architecture, Design Decisions & Protocol Contract Ledger
*Target Hardware:* 800×480 Capacitive Touch Display | ESP32-S3 UI Host | NanoELS Core Controller

---

## 1. Core System Philosophy & Safety Constraints

1. **Radical Hardware Transparency:**
   * NanoELS firmware maintains strictly a single pair of physical hardware limit registers per axis (`z_min`/`z_max`, `x_min`/`x_max`).
   * The UI must never display "ghost" limits or create a false sense of security. What is displayed on the DRO limit chips must always reflect the exact boundary currently active in the motion controller.
2. **Dimension-First Machining Intent:**
   * Machinists think in workpiece drawing dimensions (e.g., "turn a 25.0 mm shoulder"), not arbitrary machine coordinates.
   * Setting a feature dimension (`LENGTH` in turning, `STROKE` in facing) automatically maps to the required hardware limit relative to tool touch-off, removing manual jog-and-set math.
3. **Opt-In Limit Restoration:**
   * Because stock NanoELS overwrites limits destructively, the companion display controller introduces an opt-in `RESTORE LIMITS ON COMPLETION` toggle. This caches existing machine envelopes (e.g., chuck safety stops) and restores them upon cycle termination without modifying core NanoELS firmware.
4. **Strict Mode Lockout During Cycle Execution:**
   * Mode switching is strictly prohibited while any motion cycle or feed is active. The operator must press `STOP` before changing operating modes to prevent unintended spindle synchronization, axis runaway, or tool crashes.

---

## 2. Display & Ergonomic Standards (800×480 Landscape)

* **DRO Layout & Typography:**
  * Coordinate readouts are locked to a static `50px` monospace font (`JetBrains Mono` / tabular lining figures). Dynamic font auto-shrinking is prohibited to prevent digit jumping during axis motion.
  * Inline unit labels (`mm`/`in`) are omitted from axis coordinate lines to guarantee full horizontal fit for negative 7-figure values (e.g., `-300.0000 in`). The active system unit is anchored prominently in the top bar.
  * Diameter symbol strictly utilizes the authentic uppercase Danish/European engineering glyph (`Ø`), matching uppercase `R`. Diameter vs. radius representation is dynamically color-coded (`Ø` in cyan, `R` in amber).
* **Directional Alignment (Operator Reference Frame):**
  * $Z$ Axis (Carriage): `← Z CHUCK` is toward the headstock ($Z-$); `→ Z TAIL` is toward the tailstock ($Z+$).
  * $X$ Axis (Cross-Slide): `↑ TO CENTER` is into the workpiece/plunge ($X-$); `↓ RETRACT` is toward the operator/backing out ($X+$).
* **Apron/Jog Cluster Placement:**
  * Jog D-pad and chamfer keys are pinned to the bottom of the center column, providing wrist/palm stability against lathe bed rims during manual stepping.
  * Step increment selector cycles on tap through discrete standard tables (Metric: `0.010 → 0.100 → 1.000 mm`; Imperial: `0.0010 → 0.0100 → 0.1000 in`), with long-press (>600ms) opening the numeric keypad for custom increments.
* **Unified Universal Runstack:**
  * All modes consistently feature the primary action stack in the bottom-right corner: `START FEED` / `START CYCLE` (green) directly above `STOP` (red), separated by a generous 12px vertical safety gap.
  * The `STOP` button is permanent and stationary across all 9 modes; it is visually subdued when idle and illuminates vibrant red with an active glow during motion.

---

## 3. Numeric Keypad Architecture

* **Standard Calculator/ATM Layout with Quick Math Row:**
```text
[ 7 ]  [ 8 ]  [ 9 ]
[ 4 ]  [ 5 ]  [ 6 ]
[ 1 ]  [ 2 ]  [ 3 ]
[ ± ]  [ 0 ]  [ . ]
[ CURR ] [ ÷ 2 ] [ CLR ] [ ⌫ ]
```
* **Pre-Filled & Selected Buffer:** The keypad opens pre-populated with the target's current value and fully highlighted/selected. Typing immediately overwrites the buffer without requiring manual clearing, while pressing Cancel preserves the original value.
* **`÷ 2` Half-Dimension Key:** Allows immediate halving of measured workpiece diameters for radius calculations and depth of cut parameters without mental math.
* **Conditional `INSERT CURR`:** The `CURR` button is strictly gated—visible only when entering axis coordinates or soft limits; hidden for pitch, pass counts, and speeds.
* **Cursor Positioning & Direct Input:** Keypad display acts as an editable input allowing pointer focus and cursor editing.

---

## 4. Operational Modes & Dynamic Center Deck

* **Dynamic Center Action Deck:**
  * Situated above the jog D-pad. Houses live operational controls only.
  * **In Gearbox Mode:** Houses large feed nudge buttons (`[ − ]` and `[ + ]`), active axis feed readout, active axis direction toggle (`DIR: ← TO CHUCK / → TO TAIL`), and feed step override chips. (Setup toggles `SYNC/ASYNC` and `AXIS Z/X` are positioned in the right Mode Card).
  * **In Turning / Facing Modes:** Displays real-time cycle status (`PASS 02 / 06`) with feed trim buttons active.
  * **In Threading Mode:** Houses thread lead trim buttons when stopped; transitions to locked cycle telemetry (`THREADING RUNNING`, target depth, pass count) when active, with all parameter editing and nudge buttons removed.
* **Mode Categorization & Iconography:**
  * Operating modes are grouped into 3 distinct functional categories:
    1. **Continuous / Geared Motion:** `⚙ GEARBOX`, `✛ JOYSTICK`
    2. **Multi-Pass CNC Cycles:** `⇋ TURNING`, `⥮ FACING`, `⩚ THREADING`
    3. **Contouring & Specialty:** `⊿ TAPER`, `⭳ CUTOFF`, `⎍ SLOTTING`, `◠ HALF-SPHERES`
  * Cutoff / parting icon arrow points away from the operator into the cut (`↑` / `X-`).
  * `CUT PITCH` (`mm/rev` synchronized).
  * `LENGTH` (sets $Z_{\text{MIN}}$ shoulder stop relative to start).
  * `TOTAL DEPTH` (total diameter reduction $\Delta X$).
  * `MAX DEPTH/PASS` (DOC ceiling).
  * `PASSES` (two-way coupled integer: default $\lceil\Delta X / \text{DOC}\rceil$, editable to override).
  * `RETRACT` (tool clearance off OD for rapid return stroke).
  * `RESTORE LIMITS` (`YES` / `NO`).
* **Facing Mode Parameters:**
  * `FACING FEED` (`mm/rev` synchronized).
  * `FINAL Ø` / `STROKE` (sets inner $X$ stop).
  * `TOTAL DEPTH` ($\Delta Z$ face cleanup).
  * `MAX DEPTH/PASS` (axial DOC ceiling).
  * `PASSES` (two-way coupled integer, editable to override).
  * `RESTORE LIMITS` (`YES` / `NO`).

---

## 5. Controller Architecture & Protocol Contracts

The system divides responsibilities between **Local Display Controller UI Logic** and **Multi-Command Serial Orchestration**:

### Category A: Multi-Command Serial Sequences (ESP32-S3 → NanoELS Core)
1. **Dimension-First Limit Assignment:**
   * `STEP 1:` ESP32 queries current position (`GET_POS_Z`).
   * `STEP 2:` ESP32 calculates $Z_{\text{SHOULDER}} = Z_{\text{CURR}} - \text{Length}$.
   * `STEP 3:` ESP32 transmits `SET_LIMIT_Z_MIN(shoulder)` to NanoELS register.
2. **Opt-In Limit Restore Sequence:**
   * `PRE-RUN:` ESP32 reads and caches active limit in local RAM (`saved_z_min`).
   * `RUN:` Transmits cycle shoulder limit to NanoELS, then sends `START_CYCLE`.
   * `TERMINATION:` On cycle completion packet or operator `STOP` abort, ESP32 transmits `SET_LIMIT_Z_MIN(saved_z_min)` to reinstate the original safety envelope.
3. **Mode & Axis Transition:**
   * `STEP 1:` Transmit safe disengage (`STOP_FEED`).
   * `STEP 2:` Transmit active mode and axis mask.
   * `STEP 3:` Transmit lead/pitch parameters.

### Category B: Pure Local UI Logic (Zero Serial Protocol Overhead)
* **Two-Way Pass Derivation Macro Bridge:** 
  * NanoELS motion core firmware (`h5.ino`) natively executes linear/parabolic passes using a discrete integer parameter (`turnPasses`), with no native concept of `maxDoc`.
  * The ESP32 display controller acts as an ergonomic macro engine: Machinists think in maximum tool/machine chip load (`MAX DOC`) and total dimension to remove (`TOTAL DEPTH`). The ESP32 computes $\text{turnPasses} = \lceil\text{Total Depth} / \text{Max DOC}\rceil$ locally, eliminating shop-floor mental math.
  * The operator can also tap `PASSES` directly to increase or decrease the pass count (e.g. for a lighter finishing cut), which recalculates the actual DOC ($\text{Total Depth} / \text{Passes}$).
  * The final resolved integer `turnPasses` is transmitted directly to the NanoELS controller register upon cycle arming.
* **Coordinate & Unit Conversions:** Metric/Imperial scaling and $\text{Ø} / R$ radial math occur entirely in display buffers; NanoELS core communications remain in native pulses/counts.
* **Keypad Buffering:** Input string assembly, sign toggling, and clearing are local operations; single write commands are emitted only upon tapping `SET`.
* **Visual Styling States:** Inactive axis row dimming in Gearbox is handled via local UI styling.
* **Anti-Trap Limit Contact Inference & Visual Lockout:**
  * **Core Motion Behavior:** NanoELS core firmware (`h5.ino`) does not emit a `LIMIT_HIT` packet, alarm, or warning flag. It silently suppresses step pulses in the tripping direction (`delta = limit - pos = 0`).
  * **Display Controller Responsibility:** The ESP32-S3 display controller C/C++ firmware must evaluate limit contact locally from telemetry (`distanceToStop == 0` or comparing `pos` with active limit registers).
  * **Asymmetric Jog UI State Machine:** When limit contact is detected, the display controller's view logic is solely responsible for styling the tripping jog button red and locking out further steps into the wall, while guaranteeing the retreat jog button remains enabled and visually unobstructed so the operator can immediately jog away.

---

## 6. Physical Screen Geometry & Universal Touch Sizing Standards

### Target Display Physics (5.0" 800×480)
* **Physical Active Area:** $\approx 108.0 \text{ mm} \times 64.8 \text{ mm}$ ($4.25\text{"} \times 2.55\text{"}$).
* **Pixel Density:** $\approx 187 \text{ DPI}$ ($\approx 0.135 \text{ mm/pixel}$).
* **Vertical Canvas Budget:** Total 480px = 42px Top Bar + 30px Status Bar + **408px Active Main Viewport**.

### Universal Minimum Touch Target Standards
In shop environments with bare or gloved hands, touch targets must satisfy industrial ergonomic thresholds (ISO 9241-410):
1. **Primary Action Targets (D-Pad, Start, Stop, Keypad Keys, Numeric Readouts):**
   * Minimum Height: **42px – 48px** ($5.7 – 6.5 \text{ mm}$).
   * Minimum Width: **56px – 64px** ($7.5 – 8.6 \text{ mm}$).
2. **Secondary/Setup Touch Chips (Mode Toggles, Unit Selectors, Limit Chips):**
   * Minimum Height: **36px – 38px** ($4.9 – 5.1 \text{ mm}$).
   * Minimum Width: **50px** ($6.8 \text{ mm}$).
   * Absolute rule: No interactive button may ever be rendered below 36px height or 48px width.
3. **Touch Separation Padding:**
   * Minimum inter-button gap: **4px – 6px** ($0.5 – 0.8 \text{ mm}$) to prevent unintended adjacent edge presses.

---

## 7. Motion State Machine & Radical Hardware Fidelity

### Single Binary State Truth (`isOn`)
* The core NanoELS motion firmware operates strictly on a binary state engine: `stopped` (`isOn == false`) or `running` (`isOn == true`).
* **No Software Pause:** There is no native pause state in NanoELS core firmware. Fabricating a software "pause" in the display controller is prohibited because pausing spindle-synchronized motion risks tool breakage and positional drift.
* **Universal Runstack Representation:**
  * Idle / Stopped: Green `START FEED` / `START CYCLE` active; red `STOP` button visually subdued (maroon `#140d0e`).
  * Running / Active: Green button transitions to dark active indicator (`FEEDING` / `CYCLE ACTIVE`); red `STOP` button illuminates vibrant red (`#ff5a5f`) with an active glow.
  * Dedicated Stop Corner: The `STOP` button is permanently anchored in the bottom-right corner across all 9 modes so muscle memory never falters in an emergency.

### No "Ghost Actions" During Autonomous Multi-Pass Cycles
* When NanoELS executes an automated multi-pass cycle (such as `THREADING`, `TURNING`, or `FACING`), the motion trajectory is computed and executed autonomously by the ESP32 motion core.
* **Prohibition of "Next Pass" or Mid-Cycle Step Commands:** The display controller has no real-time serial hooks to safely micro-step or skip passes without corrupting the motion state machine. Presenting a `NEXT PASS` button provides a false illusion of control. During active cycles, the UI strictly displays progress (`PASS 05 / 10`, current cut depth) and leaves the `STOP` button as the sole operator intervention.

---

## 8. Gearbox Mode: Setup vs. Live Operational Separation

The Gearbox interface splits cleanly between setup configuration and live operational control to avoid duplicate or shifting buttons:

### Mode Card (Right Column — 260px) = Configuration & Setup
1. **Top-Aligned Flow:** All mode card items are top-aligned (`justify-content: flex-start`), ensuring fixed vertical spacing and leaving maximum clean space above the runstack.
2. **Stationary Setup Toggles:** `SYNC / ASYNC` and `AXIS: Z / X` buttons utilize fixed flex basis (`flex: 1 1 0; min-width: 0;`), guaranteeing zero horizontal jumping or reflow when clicked.
3. **Z Feed Unit Selection:** Provides mutually exclusive selection between `FEED` (mm/rev or in/rev) and `TPI` (threads/inch). In metric, feed is identical to lead pitch.
4. **Feed Display Rows:** Features dedicated Z Carriage and X Cross-Slide blocks. The non-active axis is dimmed with an explicit `INACTIVE` badge; the active axis is highlighted without redundant text.

### Dynamic Center Deck (Center Column — 202px) = Live Apron Controls
1. **Live Feed Override / Nudge:** `[ − ]` and `[ + ]` nudge buttons for on-the-fly speed adjustments under chip load.
2. **Active Axis Direction Toggle:** A single, prominent button displaying the current motion direction relative to the lathe bed:
   * Z Axis: `DIR: ← TO CHUCK (Z−)` vs. `DIR: → TO TAIL (Z+)`
   * X Axis: `DIR: ↑ TO CENTER (X−)` vs. `DIR: ↓ RETRACT (X+)`
3. **Feed Step Increment Selection:**
   * Avoids cramped 5-button rows. Uses 3 large, discrete touch chips (each $\ge 50\text{px}$ wide, $\ge 36\text{px}$ high).
   * For arbitrary or non-standard feed rates, the operator taps the feed readout directly to open the numeric keypad.

---

## 9. Unit Systems, Standard Step Arrays & Input Ownership

### Prevention of Awkward Unit Conversions
Machinists do not work in arbitrary converted decimals (e.g. $0.010\text{ mm} \rightarrow 0.00039\text{"}$). The UI maintains distinct, discrete standard tables for metric vs. imperial operations:

| Motion Type | Metric Standard Table | Imperial Standard Table |
| :--- | :--- | :--- |
| **Manual Jog D-Pad** | `1.000 mm`, `0.100 mm`, `0.010 mm` | `0.1000"`, `0.0100"`, `0.0010"` |
| **Z Feed Rate Override** | `0.002`, `0.010`, `0.050 mm/rev` | `0.001"`, `0.003"`, `0.008" / rev` |
| **X Cross Feed Override** | `0.001`, `0.005`, `0.020 mm/rev` | `0.0005"`, `0.001"`, `0.003" / rev` |
| **Async Speed** | `5, 10, 25, 50 mm/min` | `0.2, 0.5, 1.0, 2.0 in/min` |

### Parameter Ownership vs. Display Scaling
* Machine coordinates, soft limits, and tool offsets dynamically convert between display units (`mm` and `in`).
* Part dimension inputs (e.g., Turning Length, Facing Stroke) are treated as drawing specifications; all labels and properties adapt dynamically to the active unit using unified formatting helpers (`lenStr()` and `unitLabel()`), with zero hardcoded `mm` strings in view templates.

---

## 10. Threading Execution & TPI Display Architecture

### Thread Pitch Fidelity
* In imperial mode, screw threads are fundamentally specified in Threads Per Inch (TPI). Converting TPI to a decimal pitch (e.g. `16 TPI` $\rightarrow$ `1.5875 mm/rev`) is confusing and contrary to standard machine shop convention.
* When TPI is selected in the thread pitch overlay, the UI preserves the exact TPI integer and unit string across the DRO, mode properties, and center action deck (e.g., `16 TPI`).
* Metric threads are consistently formatted as `X.XXX mm pitch`.

### Active Thread Cut Lockout
* In NanoELS, threading requires rigid encoder phase lock between spindle pulses and leadscrew steps. 
* Tapping the thread pitch readout or attempting to modify helix parameters during an active run is strictly locked out. The dynamic deck removes nudge buttons while the cycle is running, displaying cycle status and target depth only.

---

## 11. Machine Coordinate Reference Frame & Tooling Icons

1. **Cutoff / Parting Tool Direction:**
   * Lathe cutoff operations drive the blade inward from the front apron toward the spindle centerline ($X-$).
   * The Cutoff icon arrow is oriented pointing away from the operator into the cut (`↑` / `transform: rotate(180deg)` on `⭳`), matching the physical tool trajectory.
2. **Operator Cartesian Reference Frame:**
   * All D-pad and chamfer labels feature explicit Cartesian signs matching the carriage perspective:
     * `CHUCK (Z−)`: Headstock direction.
     * `TAIL (Z+)`: Tailstock direction.
     * `TO CTR (X−)`: Cross-slide moving into the workpiece (reducing diameter).
     * `RETR (X+)`: Cross-slide backing out toward the operator (increasing diameter).
     * Chamfer Northwest (`↖ Z− X−`): Simultaneous feed into workpiece toward chuck.
     * Chamfer Southeast (`↘ Z+ X+`): Simultaneous retract away from workpiece toward tailstock.

---

## 12. Universal Touch Sizing & Ergonomic Standards

### Physical Screen Ergonomics & Pixel Density
* **Target Hardware**: 5.0" diagonal display, $800 \times 480$ resolution ($108.0 \times 64.8\text{ mm}$, $187.3\text{ DPI}$, $0.135\text{ mm/px}$).
* **Touch Target Floor**:
  * Bare adult finger contact area averages $7–9\text{ mm}$ ($52–67\text{ px}$). Shop-gloved fingers require tactile targets $\ge 5.0\text{ mm}$ ($38\text{ px}$) height with minimum touch gaps $\ge 4–6\text{ px}$.
  * Elements smaller than $30\text{ px}$ ($4.0\text{ mm}$) cause frequent mis-taps and operator frustration on a machine lathe.

### Tiered Sizing Floor
1. **Primary Industrial Actions ($\ge 44–48\text{ px}$ height)**:
   * Runstack buttons (`START FEED / START CYCLE`: $46\text{ px}$, `STOP`: $46\text{ px}$).
   * Jog cluster D-pad buttons ($50–56\text{ px}$ height, $56–64\text{ px}$ width).
   * Jog step selector center button ($56\text{ px} \times 64\text{ px}$).
   * Action deck nudge buttons (`−` / `+`: $46\text{ px} \times 46\text{ px}$).
   * DRO axis zero buttons ($44\text{ px} \times 44\text{ px}$).
   * Numeric keypad keys ($44–48\text{ px}$).
2. **Secondary Setup & Configuration Targets ($\ge 38\text{ px}$ height, $\ge 50\text{ px}$ width)**:
   * DRO software limit chips (`chip-sm`: $82\text{ px} \times 38\text{ px}$).
   * Diameter/Radius representation toggle (`repBadge`: $68\text{ px} \times 38\text{ px}$).
   * Mode setup card toggle buttons (`SYNC / ASYNC`, `AXIS: Z / X`: $38\text{ px}$ height, full flex width).
   * Action deck step selection chips ($63\text{ px} \times 38\text{ px}$).
   * Action deck direction toggle button (`deckToggleDir`: $38\text{ px}$ height).
   * Chamfer diagonal jog buttons ($38\text{ px}$ height).
3. **Tertiary / Header / Status Targets**:
   * Top bar chips (`UNIT`, `MODE`, `SETTINGS`: $35\text{ px}$ height within $42\text{ px}$ bar).
   * Status deck message log trigger (`LOG`: $24\text{ px} \times 48\text{ px}$).

### Two-Button Step Progression Layout in Dynamic Deck
* The center dynamic action deck has a fixed width of $202\text{ px}$ (internal canvas $\approx 192\text{ px}$ after padding and border gaps).
* Multiple discrete chips (3 to 5 chips in a row) forced button widths down to $34\text{–}60\text{ px}$, causing frequent miss-taps with shop fingers.
* The deck standardizes on a **generous 2-button layout** where each button receives **$96\text{ px}$ width $\times 38\text{ px}$ height** ($13.0\text{ mm} \times 5.1\text{ mm}$ physically on the 5" screen):
  1. **Step Progression Cycle Button (`#deckStepCycle`)**: Displays the active increment and unit directly on its face (e.g. `STEP: ±0.05 mm`). Tapping it advances cleanly through a curated 4-step standard progression:
     * **Metric Feeds ($mm/rev$)**: `[0.010, 0.050, 0.100, 0.200]` (fine finishing tweak $\rightarrow$ medium finish $\rightarrow$ roughing $\rightarrow$ coarse).
     * **Imperial Feeds ($in/rev$)**: `[0.0010, 0.0020, 0.0050, 0.0100]` ($1\text{ thou}$ fine $\rightarrow$ $2\text{ thou}$ finish $\rightarrow$ $5\text{ thou}$ rough $\rightarrow$ $10\text{ thou}$ coarse).
     * **TPI Increments**: `[1, 2, 4, 8 TPI]`.
     * **Async Jog Feeds ($mm/min$ / $in/min$)**: `[10, 25, 50, 100 mm/min]` / `[0.5, 1.0, 2.0, 5.0 in/min]`.
  2. **Custom Step Button (`#deckStepCust`)**: Labeled `± CUST STEP`. Tapping it opens the calculator keypad to input a non-standard step increment (e.g., $0.035\text{ mm}$). When a custom step is active, the cycle button displays `STEP: ±0.035* mm`, and tapping the cycle button immediately resets back to the standard progression.
* **Separation of Concerns**: Setting a custom *step increment* (via `± CUST STEP`) is explicitly decoupled from setting the *absolute feed rate* (which is done by tapping the primary `#deckReadout`).

### Hardware State Alignment & Cycle Autonomy
* Features non-existent in NanoELS firmware (such as manual `NEXT PASS` buttons during synchronized threading) are strictly forbidden.
* Active multi-pass threading displays a dedicated `CYCLE ACTIVE — AUTO INFEED` status banner with hardware parameter lockout until the cycle completes or the operator aborts.

---

## 13. Safety-Critical Running Lockout: Modal/Popup Prohibition & STOP Button Protection

### Safety Philosophy
On an operating metal lathe, visual occlusion and surprise modal dialogs are severe safety hazards:
* If a tool is cutting near a chuck or shoulder, an accidental touch on any part of the screen must **never** be met with a modal dialog, keypad popup, toast notification, or warning banner.
* Full-screen overlays (`.overlay` with `inset: 0; z-index: 99;`) completely occlude the live DRO coordinates and **physically cover the red STOP button**, blocking emergency intervention until the operator finds and dismisses the dialog.
* In machining, every millisecond counts: the operator's path to the permanently anchored red **STOP** button must remain 100% unobstructed, 100% visible, and 100% tactile at all times during active motion.

### Multi-Tiered Implementation Standard (Defense-in-Depth)

1. **Defensive Core Engine Guards**:
   * All overlay-opening entry points enforce an unconditional silent return if `state.runState === 'running'`:
     * Numeric Keypad: `openKeypad()`
     * Gearbox Quick-Change Pitch: `openGearboxPitchModal()`
     * Threading Wizard / Pitch Selector: `openThreadWizard()`, `openPitchSelect()`
     * DRO Coordinate Setting: `openAxisOverlay()`
     * Soft Limit Setting: `openLimitOverlay()`
   * This guarantees that even if a stray touch event fires, zero modals can ever mount while the spindle or leadscrew is actively driving an axis.

2. **Gearbox & Mode Card Lockout (`bindModeEvents`)**:
   * The cycle execution guard `if(state.runState === 'running') return;` is anchored at the very beginning of `bindModeEvents()`.
   * When active, all setup toggles and parameter fields in the right-hand Mode Card are strictly non-interactive:
     * `SYNC (SPINDLE) / ASYNC (TIME)` (`#fld-gearSync`): locked (`opacity: 0.45; cursor: not-allowed;`).
     * `AXIS: Z / X` (`#fld-gearAxis`): locked (`opacity: 0.45; cursor: not-allowed;`).
     * Z Feed Unit `[ FEED ]` / `[ PITCH ]`: locked (`opacity: 0.35; pointer-events: none;`).
     * Z Carriage & X Cross-Slide feed boxes (`#fld-gearZ`, `#fld-gearX`): stripped of `.editable` and `.tap` classes (`cursor: default;`).

3. **Dynamic Center Action Deck Runtime Rules**:
   * **Locked Controls (No Modal Triggers)**:
     * Feed Readout (`#deckReadout`): Stripped of `.tap` class; renders as a clean, read-only telemetry display (`cursor: default;`). Tapping performs a silent no-op instead of opening the numeric keypad or pitch modal.
     * Custom Step (`#deckStepCust` / `± CUST STEP`): Stripped of `.tap` class; dimmed (`opacity: 0.35; cursor: not-allowed;`). Tapping performs a silent no-op.
     * Direction Toggle (`#deckToggleDir`): Stripped of `.tap` class; dimmed (`opacity: 0.45; cursor: not-allowed;`). Tapping performs a silent no-op to prevent mid-cut leadscrew reversal.
   * **Allowed Runtime Actions (Zero Modals, Zero Jitter)**:
     * Primary Feed Nudges: `[ − ]` and `[ + ]` remain fully active and responsive, allowing immediate trimming of feed per revolution under chip load.
     * Step Increment Cycling (`#deckStepCycle`): Preset cycling (`±0.010 → ±0.050 → ±0.100 mm`) remains available in-place without opening any dialogs.

4. **Global Controls Lockout**:
   * **DRO Coordinates (`#zValue`, `#xValue`)**: Tapping coordinates does not open `axisOverlay`.
   * **Soft Limits (`.chip-sm[data-bound]`)**: Tapping limit chips does not open `limitOverlay`.
   * **Jog Step Selector (`#stepMain`)**: Long-press hold skips opening `openKeypad('step')`.
   * **Top Bar Utility Chips**: `#settingsBtn`, `#btnOpenLog`, and `#openDocBtn` perform silent no-ops while running.
   * **Unit System Toggle (`#unitToggle`)**: Switching units (`mm` $\leftrightarrow$ `in`) is locked out during active motion to prevent mid-cut coordinate/ratio jumping.

5. **Visual State Affirmation (`.device.is-running`)**:
   * The hardware frame container dynamically toggles `.is-running` to reflect motion state.
   * Non-interactive elements have cursor pointers removed and dashed editable underlines replaced with solid/clean borders, eliminating misleading affordances while leaving the vibrant red **STOP** button illuminated and active in the bottom-right corner.

---

## 14. Gearbox Card Layout Stability & Zero-Jitter Control Architecture

### Rationale & Problem Statement
Prior iterations dynamically mounted and unmounted the `Z UNIT: [ FEED ] [ TPI ]` toggle row based on conditional state (`isSync && zActive ? ... : ''`).
* Whenever the operator toggled between `AXIS: Z` and `AXIS: X`, or between `SYNC` and `ASYNC`, the dynamic insertion/removal caused the Z Carriage and X Cross-Slide blocks to jump vertically by $\approx 44\text{ px}$.
* Furthermore, placing the Z feed unit toggle *above* both feed blocks created a visual disconnect between the control and the carriage parameter it governed.

### Implementation Standard
1. **Visual Proximity & Inline Coupling**:
   * The `[ FEED ] [ TPI ]` unit toggle row is permanently housed **inside** the `Z CARRIAGE` block directly beneath the Z feed readout.
   * This provides immediate cognitive coupling: the operator sees at a glance that `FEED / TPI` applies strictly to Z-axis carriage motion.
2. **Deactivation Over Dynamic Unmounting**:
   * When Z is inactive (`AXIS: X`) or when operating in `ASYNC` mode:
     * The toggle row is **not** removed from the DOM.
     * It remains strictly rendered in place with dimmed styling (`opacity: 0.35; pointer-events: none;`).
   * Because element height is static, toggling between Z and X axes or between SYNC and ASYNC modes produces **zero layout shift (zero jitter)**.
3. **Removal of Redundant 'INACTIVE' Badges**:
   * The text badge `<span class="axis-tag-badge">INACTIVE</span>` was removed from the headers of both the Z Carriage and X Cross-Slide blocks.
   * Axis activity is already definitively communicated by the top toggle (`AXIS: Z` vs `AXIS: X`) and full-card dimming (`.dimmed-axis-row` at $0.45$ opacity). Removing the text badge eliminates visual noise and reflow jitter.

---

## 15. Unified Feed Step Architecture Across Modes (Gearbox, Turning, Facing)

### Motivation & Machining Rationale
* Synchronized lathe operations (Gearbox feeds, longitudinal Turning, and transverse Facing) all govern the same physical cutting parameter: feed per revolution ($mm/rev$ or $in/rev$).
* Prior iterations maintained an inconsistent control model: Gearbox allowed user-selectable step cycling, whereas Turning and Facing relied on a hidden, hard-coded $0.005\text{ mm}$ nudge increment with no user feedback.
* On the lathe, an operator tuning a finish cut or establishing chip curl needs the exact same fine ($0.01\text{ mm}$ / $0.001"$), medium ($0.05\text{ mm}$ / $0.002"$), rough ($0.10\text{ mm}$ / $0.005"$), or coarse ($0.20\text{ mm}$ / $0.010"$) feed step options regardless of whether cutting in continuous Gearbox or automated multi-pass Turning/Facing.

### Implementation Standard
1. **Identical 2-Button Dynamic Deck Control**:
   * Turning and Facing action decks incorporate the exact same **2-button step control** beneath the feed readout:
     * `[ STEP: ±0.05 mm ]` (or active unit): cycles through the curated 4-step progression ($0.010 \rightarrow 0.050 \rightarrow 0.100 \rightarrow 0.200\text{ mm}$ / $0.0010 \rightarrow 0.0020 \rightarrow 0.0050 \rightarrow 0.0100\text{ in}$).
     * `[ ± CUST STEP ]`: opens the numeric keypad to specify non-standard increments.
2. **Axis State Coupling**:
   * **Turning (Z carriage motion)**: utilizes the shared Z-axis feed step progression (`state.feedStepIdxZ`, `state.customFeedStepZ`).
   * **Facing (X cross-slide motion)**: utilizes the shared X-axis feed step progression (`state.feedStepIdxX`, `state.customFeedStepX`).
   * *Benefit*: If an operator selects `±0.05 mm` for Z in Gearbox, switching to Turning preserves that exact step increment automatically.
3. **Nudge Integration**:
   * Tapping `—` or `+` in Turning or Facing trims the active feed rate (`turnPitch` or `facePitch`) by the exact active step delta, providing real-time feedback in both the dynamic deck and the right-hand mode parameters card.

---

## 16. Mode Card De-Duplication & Operation Selector (External OD vs Internal Boring)

### Motivation & Layout Economy
* In the 800×480 display layout, vertical real estate in the right-hand mode parameters card is constrained.
* Having `CUT PITCH` (Turning) and `FACING FEED` (Facing) inside the right-hand card was redundant because the active cutting feed rate is already prominently displayed, tapped for numeric keypad entry, and stepped with `—`/`+` nudges in the dynamic center action deck.
* Removing the redundant feed rows created room to expose the firmware-supported **Internal vs. External turning operation selector** without overcrowding the card.

### Implementation Standard
1. **Feed Rate De-Duplication**:
   * Removed `CUT PITCH` row from the Turning card and `FACING FEED` row from the Facing card.
   * Operators inspect and adjust cutting feed directly in the center action deck (`deckReadout`, `deckNudgeMinus`, `deckNudgePlus`).
2. **Operation Selector in Turning Card**:
   * Added an interactive `OPERATION` toggle chip (`[ EXTERNAL (OD) ]` $\leftrightarrow$ `[ INTERNAL (ID / BORE) ]`).
   * Tapping cycles `state.turnType` between `'EXTERNAL'` and `'INTERNAL'`.
   * Real-time status bar updates to reflect the active operation (`TURNING [EXT (OD)]` vs `TURNING [INT (BORING)]`).
3. **Motion Simulation Awareness**:
   * In multi-pass simulation, diameter stepping adapts to operation type: external turning decrements diameter (`state.xDia -= doc`) while internal turning increments diameter (`state.xDia += doc`) as material is removed from the bore.

---

## 17. Kinematic Telemetry Alignment (Spindle Relocation to Left Column) & Touch Target Elevation

### Motivation & Spatial Economy
* Previously, the `SPINDLE` panel was stacked above the Mode Card in the right column (`col-info`), consuming ~45px of vertical room.
* This vertical compression forced Mode Card interactive chips (like directional chips and toggles) down to an unergonomic ~20px height, violating the $\ge 38\text{px}$ glove-friendly touch target rule.
* Kinematically, the lathe has 3 primary axes: **$Z$ (longitudinal carriage)**, **$X$ (transverse cross-slide)**, and **$S$ (rotational spindle)**. Placing Spindle on the left unites all machine telemetry in a single dedicated column.

### Implementation Standard
1. **Left Column Telemetry Hierarchy (`col-dro`)**:
   * **Top**: Dedicated `SPINDLE` panel (rotational telemetry: RPM / turns / angle).
   * **Middle**: `$Z$` Carriage Position readout (`50px` monospace) + limit boundary chips.
   * **Bottom**: `$X$` Cross-Slide Position readout (`50px` monospace with Danish `Ø`) + limit boundary chips + diameter/radius toggle.
2. **Right Column Mode Card Expansion (`col-info`)**:
   * Removing the spindle card allows the Mode Card to expand and occupy the full column above the stationary `START` / `STOP` runstack (~242px body height).
3. **Universal Touch Target Elevation ($\ge 38\text{px}$)**:
   * `OPERATION` selector (`[ EXTERNAL (OD) ]` $\leftrightarrow$ `[ INTERNAL (ID / BORE) ]`): full $38\text{px}$ height toggle chip.
   * Longitudinal & Transverse Direction chips (`← CHUCK / → TAIL` and `↑ TO CENTER / ↓ RETRACT`): elevated to $36\text{px}$ min-height.
   * Property table rows (`.kv`): standardized with a $36\text{px}$ minimum height and centered alignment for effortless touch interactions on the shop floor.

---

## 18. Coupled Parameter Visual Affordance (Right Bracket on Max DOC & Passes)

### Motivation & Mental Model
* In automated lathe cycles (**Turning** and **Facing**), the cut geometry is governed by three interrelated parameters:
  1. **Total Depth** ($D$): The target material to be removed (part requirement).
  2. **Max DOC / Pass** ($DOC_{max}$): The maximum allowable depth of cut per pass (machining strategy).
  3. **Number of Passes** ($N$): The discrete integer pass count executed by the controller ($N = \lceil D / DOC_{max} \rceil$).
* The NanoELS motion controller firmware expects depth and pass count, but operators frequently think in terms of tool cutting depth ($DOC_{max}$). The UI enables two-way coupled entry: editing $DOC_{max}$ computes $N$, while editing $N$ updates the DOC per pass.
* Without a clear visual cue, operators could be confused when altering one field silently recalculates the other.

### Implementation Standard
1. **Visual Grouping via Engineering Right Bracket (`]`)**:
   * Wrapped `MAX DOC / PASS` and `PASSES` inside a `.kv-bracket-group` container across both Turning and Facing cards.
   * A crisp right bracket with leader arms groups the two values without consuming extra vertical space on the $800\times 480$ screen.
2. **Vertical Centerline Alignment**:
   * The top horizontal tick line aligns directly with the vertical centerline of the `MAX DOC / PASS` value.
   * The bottom horizontal tick line aligns directly with the vertical centerline of the `PASSES` value.
   * Spacing between rows inside the bracket group matches the card's standard `6px` gap, ensuring clean rhythm and alignment across all rows.

395: ---
396: 
397: ## 19. Gearbox Feed vs. Pitch Architecture & Handedness Support
398: 
399: ### Motivation & Machining Rationale
400: * In prior iterations, Gearbox mode offered `[ FEED ]` vs `[ TPI ]` unit buttons. Because metric threads are expressed in pitch ($mm$), combining $mm/rev$ and metric pitch created ambiguity: "Feed" is an arbitrary tool advance rate where handedness is meaningless (direction is simply dictated by the joystick/jog), whereas "Pitch" is a strict 1:1 spindle-synchronized screw-cutting lead requiring explicit Right-Hand (RH) or Left-Hand (LH) synchronization.
401: * Tapping `TPI` previously only converted the numeric feed value to TPI rather than opening thread lead selection.
402: 
403: ### Implementation Standard
404: 1. **Unit Mode Bifurcation (`[ FEED ]` vs `[ PITCH ]`)**:
405:    * Replaced the inline toggle with:
406:      $$\mathbf{[ FEED ]} \quad \Big| \quad \mathbf{[ PITCH ]}$$
407:    * **When in `FEED`**: Displays pure power turning/facing advance rate (e.g. `0.100 mm/rev` or `0.004 in/rev`). Tapping opens the numeric keypad. Motion direction is governed exclusively by directional controls.
408:    * **When in `PITCH`**: Displays leadscrew thread lead with explicit handedness (e.g. `1.50 mm (RH)` or `16 TPI (LH)`). Tapping opens the rich Thread Selector modal.
409: 2. **Firmware Gearing Contract (`h5.ino`)**:
410:    * Selecting Left-Hand (LH) thread synchronization negates `dupr` (`setDupr(-dupr)`), which directs NanoELS to feed the carriage in reverse relative to forward spindle rotation (`posFromSpindle()`).
411: 3. **Taper Exclusion from Gearbox**:
412:    * Tapered pipe threads (1:16 cone) require simultaneous 2-axis CNC coordination and automatic retraction, which cannot be managed during single-axis manual gearbox feeding. Tapered threading is strictly reserved for automated **Threading Mode (`MODE_THREAD`)**.
413: 
414: ---
415: 
416: ## 20. Conversational Fastener Library vs. Freeform Pitch Architecture
417: 
418: ### Motivation & Mental Model
419: * Machinists on manual lathes traditionally look up pitch, tap drill, and minor/major diameters in physical handbook tables.
420: * A modern conversational interface should allow picking standard fasteners (`M12 × 1.75`, `1/2"-13 UNC`, `1/2"-14 NPT`) and automatically compute all required NanoELS parameters without manual calculation.
421: 
422: ### Implementation Standard
423: 1. **Dual Modal Operation (`[ STANDARD LIBRARY ]` vs `[ FREEFORM PITCH ]`)**:
424:    * **`[ STANDARD LIBRARY ]`**:
425:      * **ISO Metric (M)**: M3 to M24+ (coarse and fine pitch selections). Calculates standard $60^\circ$ external infeed depth ($h = 0.6134 \times P$) and internal depth ($h = 0.5413 \times P$).
426:      * **Unified Inch (UN)**: #4 to 1-1/2"+ (UNC Coarse and UNF Fine selections). Pre-fills TPI and decimal depth.
427:      * **NPT Pipe (ASME B1.20.1)**: 1/8" to 2" nominal pipe sizes. Pre-fills standard TPI (27, 18, 14, 11.5), activates **1:16 taper ratio (`coneRatio = 0.0625`)** pursuant to `h5.ino` line 9260, and pre-fills standard pipe depth ($h = 0.8000 \times P$) and effective length ($L_2$).
428:      * **BSPT Pipe (ISO 7-1)**: 1/8" to 1" standard pipe sizes ($55^\circ$ Whitworth profile on 1:16 taper).
429:    * **`[ FREEFORM PITCH ]`**:
430:      * Unconstrained manual entry: quick-select grid for Metric pitches and Imperial TPIs, plus a numeric keypad for non-standard pitches or leadscrews. Cuts whatever is in the machine envelope without stock dimension constraints.
431: 2. **Multi-Start Threading (`starts`)**:
432:    * Threading Mode exposes an editable `STARTS` property (default `1`, supporting 1 to 124 per `STARTS_MAX` in `h5.ino`).
433:    * Effective spindle lead is computed as `pitch * starts` (`h5.ino` line 7248), with angular spindle indexing handled by `spindleModulo`.
434: 
435: ---
436: 
437: ## 21. Directional Independence: Handedness vs. Cut Direction (Blind Hole Safety)
438: 
439: ### Motivation & Safety Rationale
440: * Thread Handedness (the spiral orientation: Right-Hand vs. Left-Hand) is fundamentally distinct from Cut Direction (the carriage feed vector: Toward Chuck vs. Toward Tailstock).
441: * Threading blind internal holes towards the chuck at high speeds carries catastrophic collision risk if the boring bar over-travels the bottom.
442: * Machinists mitigate this by starting the tool inside the relief groove at the bottom of the blind hole and **feeding outward toward the tailstock into open air (Inside-Out)**.
443: 
444: ### Implementation Standard
445: 1. **Independent Toggle Separation**:
446:    * **Handedness**: `[ RIGHT-HAND (RH) ]` vs `[ LEFT-HAND (LH) ]`.
447:    * **Cut Direction**: `[ ← TO CHUCK (Z−) ]` (Standard Outside-In) vs `[ → TO TAIL (Z+) ]` (Blind Hole Safe Inside-Out).
448: 2. **Spindle Kinematic Guidance**:
449:    * When cutting an RH thread outward (`→ TO TAIL`), the UI displays an intuitive operational advisory:
450:      * *“Spindle: REVERSE (CW) — Cutting outward from blind bottom into open clearance.”*
451:    * When cutting an LH thread outward (`→ TO TAIL`), the spindle runs in standard Forward (CCW).
452: 3. **Firmware Stop Alignment (`h5.ino`)**:
453:    * `getPassModeZStart()` in `h5.ino` selects `z.rightStop` when pitch sign is positive, and `z.leftStop` when negative.
454:    * Selecting `→ TO TAIL` feeds toward the right stop, guaranteeing safe clearance.
455: 
456: ---
457: 
458: ## 22. Human-in-the-Loop "Skim Verify" Zeroing & Runout Elimination Workflow
459: 
460: ### Motivation & Machining Rationale
461: * On a manual lathe, mid-cycle tool changes between a turning tool and a threading tool ruin coordinate integrity unless complex tool tables are maintained.
462: * Using the **threading tool to take a single light clean-up skim pass** across the stock diameter prior to threading guarantees 3 critical physical conditions:
463:   1. Eliminates 3-jaw chuck runout, making the outer diameter 100% concentric with the spindle axis.
464:   2. Establishes the exact high-limit Major Diameter (6g / 2A fit) to prevent thread crest binding.
465:   3. Calibrates the physical tool tip position to the absolute micron ($0.000\text{ mm}$ offset error).
466: 
467: ### Implementation Standard
468: 1. **Opt-In `SKIM VERIFY` Property**:
469:    * Threading mode card includes a high-visibility toggle chip: `[ SKIM VERIFY: ON / OFF ]` (defaulting to `ON`).
470: 2. **Two-Stage Execution Sequence**:
471:    * **Stage 1 (Skim Pass)**: Upon pressing `START CYCLE`, the controller feeds the tool along the full thread length, taking a light $0.10 - 0.15\text{ mm}$ DOC cut down to the target Major Diameter (or the 1:16 cone for NPT).
472:    * **Stage 2 (Interactive Confirmation Modal)**:
473:      * Upon completing the skim pass, the tool retracts to safe clearance and the controller displays a modal:
474:        > **SKIM PASS COMPLETE: Tool skimmed to Ø [Major OD].**  
475:        > *Did the tool take a light continuous cut across the full length?*  
476:        > `[ ✕ ABORT / CHECK OD ]` &emsp; `[ ✓ CONFIRM & THREAD ]`
477:      * **If Aborted**: Cycle halts safely before cutting any threads, preventing part damage if the stock was undersized.
478:      * **If Confirmed**: The controller immediately initiates the synchronized multi-pass threading cycle with 100% verified zeroing.
479: 3. **Single-Tool Workflow**:
   * Precludes tool changes, eliminating QCTP offset drift and re-touching errors.

---

## 23. Firmware Implementation Specification: Translating to `h5.ino`

### Data Structures & State Variables
To support these conversational features in `h5.ino`, the controller firmware state maps as follows:

```c
typedef struct {
  // Conversational Pitch & Geometry
  float pitch;           // Thread pitch in mm (e.g., 1.75 for M12)
  int starts;            // Multi-start count: 1 to 124 (default 1)
  float totalDepth;      // Total radial cutting depth in mm (e.g. 1.073mm)
  int passes;            // Total number of roughing/finishing passes
  float coneRatio;       // 0 for straight threads, 0.0625 for NPT/BSPT 1:16 taper
  
  // Directional & Kinematic Controls
  uint8_t handedness;    // THREAD_HAND_RIGHT (0) or THREAD_HAND_LEFT (1)
  uint8_t cutDirection;  // CUT_DIR_TOWARD_CHUCK (0) or CUT_DIR_TOWARD_TAIL (1)
  uint8_t internal;      // 0 for external OD, 1 for internal ID
  
  // Skim Verification Stage
  bool skimVerifyEnabled;// true: prompt for human verification after initial pass
  uint8_t cycleStage;    // STAGE_IDLE, STAGE_SKIM_PASS, STAGE_AWAIT_VERIFY, STAGE_THREADING
} thread_conversational_t;
```

### Motion Kinematics & Directional Logic in `h5.ino`
1. **Pass Start / End Boundaries (`getPassModeZStart()` / `getPassModeZEnd()`)**:
   * **`CUT_DIR_TOWARD_CHUCK` (Z−)**:
     - `startZ = z.rightStop`
     - `targetZ = z.leftStop`
     - Carriage travels in the negative Z direction.
   * **`CUT_DIR_TOWARD_TAIL` (Z+)** (Inside-out for blind holes):
     - `startZ = z.leftStop`
     - `targetZ = z.rightStop`
     - Carriage travels in the positive Z direction.
2. **Leadscrew Synchronization (`setDupr`)**:
   * For **Right-Hand (RH)** cutting toward chuck (Forward spindle CCW): `dupr = pitch * starts`.
   * For **Right-Hand (RH)** cutting toward tailstock: To cut an RH thread moving toward the tailstock, the spindle must rotate in Reverse (CW), which preserves the right-hand helical advance angle. `dupr` sign is set to maintain pitch synchronization with reversed encoder counts.
   * For **Left-Hand (LH)** cutting toward chuck: Spindle runs Forward (CCW) with inverted carriage travel `setDupr(-pitch * starts)`.
3. **Tapered 2-Axis Tracking (`coneRatio`)**:
   * Per `h5.ino` line 9260, `MODE_THREAD` calculates the instantaneous X position as:
     $$X(z) = X_{base} \pm (\text{coneRatio} \times \Delta z)$$
   * For standard 1:16 pipe threads (NPT & BSPT), `coneRatio = 0.0625` ($1/16$ on diameter, or $1/32$ on radius).
   * Toolpost remains rigidly square ($90^\circ$) to the lathe bed; all taper geometry is generated purely through 2-axis coordinated stepper/servo interpolation.
4. **Skim Verification Cycle State Machine**:
   * **`STAGE_SKIM_PASS`**: Tool positions at $X = X_{stock}$, feeds across Z at standard turning feed rate (e.g., $0.10\text{ mm/rev}$), truing the OD to exact target size.
   * **Retract & Dwell**: Carriage retracts X by `retractDist`, moves Z back to `startZ`, and issues an event: `EVENT_SKIM_COMPLETE`.
   * **`STAGE_AWAIT_VERIFY`**: Steppers are held energized. The screen displays the confirmation modal. Spindle continues spinning or pauses depending on user preference.
   * **Operator Confirmation**:
      - If operator taps `CONFIRM`: `cycleStage = STAGE_THREADING`. Multi-pass threading commences from the newly trued zero.
      - If operator taps `ABORT`: Cycle aborts immediately, steppers release or return to standby, allowing operator to inspect stock or adjust tooling.

---

## 24. Conversational Threading UI Redesign & Stock Preparation Bridge

### Ergonomic & Screen Real-Estate Motivation
The physical 5-inch 800×480 screen cannot comfortably accommodate a monolithic thread configuration form containing standard library grids, pitch options, handedness, direction, starts, and blueprint tables simultaneously without severe vertical scrolling and visual clutter. Furthermore, displaying standards committee jargon such as "1st Choice" and "2nd Choice" ISO fasteners creates cognitive friction for lathe operators, who think primarily in diameter ranges (e.g. "I am making an M10 thread").

### Architectural Innovations

#### 1. Progressive 4-Stage Selection Flow
The configuration overlay is structured as a stepped progressive wizard that fits strictly within $\le 450\text{ px}$ vertical height:
* **Stage 1 (Family)**: Selection between `METRIC (M)`, `UN INCH`, `NPT PIPE`, `BSPT (R/Rc)`, and `FREEFORM / CUSTOM PITCH`.
* **Stage 2 (Diameter / Size Range)**:
  - Sizes are categorized into intuitive diameter bands:
    - Metric: `M1 – M6`, `M7 – M12`, `M14 – M24`, `M27 – M42`.
    - UN: `#4 – 3/8″`, `7/16″ – 3/4″`, `7/8″ – 1-1/2″`.
    - Pipe: `1/8″ – 1/2″`, `3/4″ – 2″`.
  - Common fasteners are presented immediately in a 4-column touch grid.
  - A `[ + MORE SIZES ]` toggle reveals 3rd-choice secondary sizes (e.g., M13, M15, M17, M19, M25, M28, M32) styled with distinctive dashed borders.
  - A dedicated `[ ENTER CUSTOM Ø… ]` keypad button allows operators to turn non-standard diameters (e.g. Ø 15.0 mm or Ø 49.5 mm) with automatic ISO 60° thread calculation.
* **Stage 3 (Pitch & Infeed)**:
  - Displays Coarse / Standard pitch alongside Fine and Extra-Fine options, showing calculated radial infeed depth ($h_{ext} = 0.6134 \times P$) and estimated pass counts.
  - For pipe threads (NPT / BSPT), explicitly highlights the **$1:16\text{ Taper Cone Ratio } (1^\circ 47' / 1.79^\circ)$** and thread engagement length ($L_2$).
  - Provides a direct `[ CUSTOM PITCH / KEYPAD… ]` button for arbitrary leads.
* **Stage 4 (Machining Strategy & Handedness)**:
  - High-visibility toggle chips for:
    - `EXTERNAL (OD)` vs `INTERNAL (ID)`
    - `RIGHT-HAND (RH)` vs `LEFT-HAND (LH)`
    - `← TO CHUCK (Z−)` vs `→ TO TAIL (BLIND HOLE)`
    - `SKIM PASS: ON/OFF`
  - **Thread Cut Length**: Dedicated input box with direct keypad editing and automatic pipe thread ($L_2$) presets.
  - **Conditional Multi-Start Selector (`STARTS`)**:
    - Standard fasteners (Metric, UN, Pipe) are strictly single-start ($N = 1$).
    - Selecting `CUSTOM` thread family reveals the `STARTS` stepper (`[ − ]  1 START  [ + ]` / tap to enter 1–9) alongside Thread Length in Step 4.
    - Automatically computes and displays **Effective Lead** ($\text{Lead} = \text{Pitch} \times \text{Starts}$) in the live advisory banner and Blueprint summary card.
  - Real-time kinematic safety warning when cutting RH threads towards the tailstock (`⚠ Spindle: REVERSE (CW) — Cutting outward from blind hole`).

#### 2. Anchored Dark Blueprint Card
Docked permanently at the bottom of the modal across all 4 stages:
* **Row 1**: Full thread specification and handedness (e.g. `M12 × 1.75 (EXTERNAL, RH)`).
* **Row 2**: Recommended stock preparation OD with ISO 6g tolerance limits (e.g. `Ø 11.70 – 11.97 mm`) and target root diameter (`Ø 9.85 mm`), or tap drill diameter for internal threads.
* **Row 3**: Total radial infeed depth, pass count, and spindle direction badge (`FORWARD (CCW)` or `REVERSE (CW)` / `1:16 TAPER (1.79°)`).

#### 3. Glove-Friendly Navigation Pattern
* **Back Button**: Large touch target in top-left showing the contextual previous stage name (`← CANCEL` $\rightarrow$ `← FAMILY` $\rightarrow$ `← SIZES` $\rightarrow$ `← PITCH`).
* **Close Button**: Clean, universal `[ ✕ ]` close icon in the top right.

#### 4. Center Column Spindle Kinematic & Lead Deck
In `THREADING` mode, the center action deck replaces redundant pitch nudge buttons with:
* Real-time **Spindle Kinematic Indicator** (`FORWARD (CCW)` or `REVERSE (CW)` badge with directional glyphs `↺` / `↻`).
* **Dynamic Blueprint Card**: Displays `SPEC`, `EFFECTIVE LEAD` (pitch $\times$ starts), `RADIAL INFEED`, and `PASSES`.
* Tap-to-configure binding: Tapping the center blueprint card immediately re-opens the thread configuration wizard.

#### 5. Threading Mode Card: Inline Starts Stepper
* The redundant `PROFILE` row is eliminated.
* `STARTS` is converted into an inline glove-friendly stepper: `[ − ]  1 START  [ + ]`, bounded strictly between 1 and 9 starts.

#### 6. "Prepare Stock" Cross-Mode Workflow Bridge
A major point of failure in lathe screw cutting is improper stock diameter prior to thread engagement. The `[ PREPARE STOCK ➔ ]` action button establishes an intelligent bridge across controller modes:
* **For Straight Threads (Metric, UN, Freeform)**:
  1. Saves thread specification and target stock OD (e.g. Ø 11.90 mm for M12).
  2. Automatically switches the lathe controller into **`TURNING` mode**.
  3. Pre-populates `TOTAL DEPTH` based on the difference between the current measured X diameter and target prep OD ($\Delta X = X_{current} - X_{target}$).
  4. Pre-configures pass count based on `MAX DOC`.
  5. Displays an advisory status: `PREPARE STOCK: SWITCHED TO TURNING (TARGET OD: Ø 11.97 mm FOR M12 × 1.75)`.
* **For Pipe Threads (NPT, BSPT)**:
  1. Saves thread specification.
  2. Automatically switches the lathe controller into **`TAPER` mode**.
  3. Pre-populates the exact 1:16 taper cone angle: **$1.79^\circ$ ($1^\circ 47'$)**.
  4. Pre-populates taper stroke length to the standard pipe thread engagement length ($L_2$).
  5. Displays an advisory status: `PREPARE STOCK: SWITCHED TO TAPER (1.79° / 1:16 CONE, L=13.6mm FOR 1/2" NPT)`.

---

## 7.5. Gearbox Leadscrew Quick-Change Pitch Engine (Independent from Threading Wizard)

In **Gearbox Mode**, the lathe spindle and leadscrew are electronically coupled as a digital quick-change gearbox. Unlike Conversational Threading, which is part-geometry driven (specifying standard fastener diameters, tolerance classes, tap drills, and multi-pass radial infeed depths), Gearbox mode is purely a leadscrew transmission ratio.

### Design Principles:
1. **No Workpiece Fastener Jargon or Diameter Pre-requisites:**
   * Labels like "COMMON", "COARSE", or "FINE" only have meaning when tied to a specific bolt diameter (e.g. M8 coarse vs fine). For pure leadscrew pitch selection, diameter is completely irrelevant.
   * Pitch ranges are presented as clean, purely numeric bands:
     * **Metric:** `0.25 – 0.80`, `1.00 – 2.50`, `3.00 – 6.00`.
     * **Inch:** `64 – 28`, `27 – 12`, `11.5 – 4`.
2. **Simplified Unit Toggles:**
   * Buttons are cleanly labeled `METRIC` and `INCH` (omitting parenthetical `(mm)` and `(TPI)`).
3. **Action-Trigger Custom Pitch Keypad:**
   * The `[ CUSTOM PITCH ✎ ]` button is a pure action trigger (no complex dynamic selection state) maintaining a compact intrinsic width (`flex: 0 0 auto; width: auto;`).
   * Tapping it opens the calculator keypad pre-populated with the active pitch value (in mm or TPI based on the active unit toggle) and fully selected, allowing immediate from-scratch numeric entry or quick value adjustment.
4. **Single-Tap Immediate Execution:**
   * Tapping any standard pitch instantly applies the pitch to the active carriage axis and dismisses the modal, updating the dynamic deck and setting the leadscrew gear ratio in a single tap without requiring confirmation steps or blueprint rendering.
5. **Touch Interaction Routing (Feed vs. Pitch):**
   * **Center Column Dynamic Deck Readout:** Tapping the live readout routes based on the active axis gearing mode:
     * In `FEED` mode (`mm/rev` or `in/rev`) $\rightarrow$ opens the numeric keypad for direct feed rate entry.
     * In `PITCH` mode (leadscrew threading synchronization) $\rightarrow$ opens the Gearbox Pitch Modal.
   * **Right Mode Card Controls:**
     * Tapping `[ FEED ]` switches to Feed mode and immediately brings up the numeric keypad.
     * Tapping `[ PITCH ]` switches to Pitch mode and brings up the Gearbox Pitch Modal.
     * Tapping the Z feed value box routes to the keypad in FEED mode or the Pitch Modal in PITCH mode.

---

## 7.6. Threading Mode Ergonomic Architecture & Pass Scheduling

Single-point threading is the most demanding operation on an electronic leadscrew. Adjusting kinematic parameters (pitch, lead, direction, passes) on the fly while a thread is running or armed is hazardous. The UI enforces a strict separation between **Program Configuration (Right Column Mode Card)** and **Live Execution Monitoring (Center Dynamic Deck)**.

### 1. Right Column Mode Card: Read-Only Program Specification
* **Dedicated Header Action Bar:**
  * `[+ NEW THREAD]`: Starts the 4-stage Thread Configuration Wizard from scratch at Step 1 (Thread Family).
  * `[✎ EDIT SETUP]`: Pre-loads the stored thread properties and immediately opens Step 4 (Machining Strategy & Review) for instant fine-tuning.
  * Both actions are locked during motion (`state.runState === 'running'`).
* **Authoritative Read-Only Summary Card:**
  * Replaces loose inline steppers (`+`/`−`) and tap-to-edit fields with a clean, high-contrast readout: Thread Identity, Pitch & Lead, Feed Direction & Length, Prep Stock OD, Target Root OD, Total Infeed & Passes, and Skim Pass status.
  * Eliminates operator error from accidental inline edits that bypass standard root diameter and pitch calculations.

### 2. Center Column Dynamic Deck: Cycle & Program Progress Monitor
* **Height Budgeting (<100px):**
  * Stripped of redundant blueprint cards and hazardous on-the-fly `FEED DIR` toggles, preventing vertical overflow and leaving ample room for the anchored jog cluster below.
* **When Stopped / Ready:**
  * Shows `PROGRAM STATUS: ● READY`.
  * Displays next required action (`NEXT: STAGE 1 (SKIM VERIFY)` or `PASS 01 OF 11`), Z motion envelope (`Z 0.00 → -20.00 mm`), and required spindle rotation (`SPINDLE: CCW (FWD)`).
* **When Running:**
  * Displays active pass badge (`PASS 03 / 11`).
  * 4-phase micro motion indicator: `[1. INFEED] → [2. CUT Z−] → [3. RETRACT] → [4. RETURN]` tracking active leadscrew synchronization.
  * Live quadratic infeed depth progress (`X: -0.450 / -1.075 mm`) with micro progress bar.

### 3. Pass Scheduling & Small Lathe Optimization
* **NanoELS Native Quadratic Diminishing Infeed (`h5.ino` line 9236):**
  $$\text{fraction} = \left(\frac{\text{turnPasses} - \text{opIndex}}{\text{turnPasses}}\right)^2$$
  $$\text{auxPos} = \text{auxEndStop} - (\text{auxEndStop} - \text{auxStartStop}) \times \text{fraction}$$
* **Benchtop Lathe Bias (0.10 mm Divisor):**
  * Standard tooling tables (Sandvik) recommend ~0.12 mm average DOC. On small benchtop lathes (Mini Lathes, Precision Matthews, Sieg) with 300–750W motors, the initial quadratic bite at 0.12 mm can induce belt slip or motor bogging.
  * Shifting the base divisor to `0.10 mm` (e.g. 11 passes on M12 instead of 9) drops peak cutting torque by ~25%, producing quieter cuts, better finish, and preventing motor stalls.
* **Internal Threading Offset (+2 Passes):**
  * Single-point internal threading uses slender boring bars prone to cantilever deflection and chatter. The wizard automatically adds **+2 passes** when `wizSide === 'INTERNAL'`.
* **Step 4 Passes Control:**
  * Operators can override pass count via `[ − ]` and `[ + ]` or keypad. Manually overridden counts are tagged `(MANUAL)`, while calculated counts are tagged `(AUTO)`. Toggling `EXTERNAL` $\rightleftharpoons$ `INTERNAL` dynamically recalculates the passes (+2 for internal) if in `(AUTO)` mode.
* **State Retention on Edit Back Navigation:**
  * Clicking `← BACK` from Step 4 during an `[✎ EDIT]` session restarts thread selection (Step 1) while preserving all machining strategy settings (length, internal/external, handedness, direction, passes, skim verify).

### 4. Future Firmware Enhancement Note: Compound Flank Infeed (29.5°)
* Stock NanoELS `h5.ino` currently executes 90° radial plunge infeed.
* Future firmware upgrades will introduce true modified compound flank infeed (29°–29.5°), synchronizing an incremental Z advance ($\Delta Z = \Delta X \tan(29.5^\circ)$) on each pass to cut cleanly on the leading tool flank only.

---

## 7.7. Taper & Cone Machining Architecture: Electronic Taper Attachment vs. Automated Canned Taper Cycle

In standard lathe machining, turning a taper can range from a single manual chamfer to roughing out an entire Morse taper arbor from solid cylindrical bar stock. Investigating the stock NanoELS firmware (`h5.ino` lines 9308–9356) reveals that `MODE_CONE` is architecturally an **Electronic Taper Attachment (Continuous 2-Axis Gearbox)**, not an automated canned cycle (`isPassMode()` returns `false`).

To provide both quick manual capability and full hands-off automation, the UI redesign establishes a clean architectural division:
1. **Simple Continuous Taper** is incorporated directly into **Gearbox Mode** (`AXIS: [ Z | X | Z+X TAPER ]`).
2. **Automated Taper Mode** is elevated into a full conversational multi-pass cycle in the **Contouring & Specialty** section, orchestrated by the display controller with hardware soft-stop protection and live cutting adjustments.

---

### 1. Geometric Parameter Equations & Linked Bidirectional Calculator
A lathe taper has mutually dependent geometric parameters. Specifying any valid combination dynamically calculates and updates the others in real time:

$$\text{Cone Ratio } C = \frac{D_{\text{large}} - d_{\text{small}}}{L} = 2 \times \tan\left(\theta_{\text{half}}\right)$$
$$\theta_{\text{half}} = \arctan\left(\frac{C}{2}\right) = \arctan\left(\frac{D_{\text{large}} - d_{\text{small}}}{2L}\right)$$
$$d_{\text{small}} = D_{\text{large}} - (C \times L) = D_{\text{large}} - 2L\tan\left(\theta_{\text{half}}\right)$$
$$D_{\text{large}} = d_{\text{small}} + (C \times L) = d_{\text{small}} + 2L\tan\left(\theta_{\text{half}}\right)$$

#### Built-in Machinist Presets
* **Pipe Threads**: NPT / BSPT ($1:16$, $C = 0.0625$, $\theta = 1.7899^\circ$)
* **Morse Tapers (MT0–MT6)**:
  * `MT0`: $1:19.212$ ($C \approx 0.05205$)
  * `MT1`: $1:20.047$ ($C \approx 0.04988$)
  * `MT2`: $1:20.020$ ($C \approx 0.04995$)
  * `MT3`: $1:19.922$ ($C \approx 0.05020$)
  * `MT4`: $1:19.254$ ($C \approx 0.05194$)
  * `MT5`: $1:19.004$ ($C \approx 0.05262$)
* **Machine Tool Standards**: R8 ($16^\circ 51'$ included, $C \approx 0.2963$), Brown & Sharpe ($1:24$, $C \approx 0.04167$), ISO 7:24 ($C \approx 0.29167$)
* **Standard Chamfers**: $45^\circ$ ($C = 2.0$), $30^\circ$ ($C = 1.1547$), $60^\circ$ ($C = 3.4641$)

---

### 2. Execution Architecture: Macro Orchestration vs. G-Code Generation

#### Zero-WiFi Rule for Machine Control Plane
* In the stock NanoELS firmware, G-code input is only accepted over WebSockets (`WiFi`). The physical serial link (`Serial1`, Pins 44 & 43) is strictly a binary packet bus.
* **Workshop Reliability Mandate**: Running an automated cutting cycle over wireless Wi-Fi in an industrial or home workshop with electrical motor/VFD EMI noise introduces unacceptable failure modes (packet drops, latency jitter, stalled cuts).
* The machine control plane between the Display Controller (ESP32-S3) and the NanoELS Core (ESP32) is **strictly wired over physical UART serial (`Serial1`)**.

#### Why G-Code is Discarded for Taper Mode
Beyond the Wi-Fi dependency, `MODE_GCODE` locks out on-the-fly feed adjustments (`h5.ino` line 6212: `if (mode == MODE_GCODE) return false;`) and lacks real-time feed override multipliers. If chatter develops during a heavy cut, the operator cannot touch the feed rate without aborting the program.

The **Display Controller Macro Architecture** operates 100% over the wired serial bus by stringing together passes while leveraging native ESP32 firmware features:

#### A. Hardware-Level Soft-Stop Safety
* In `h5.ino` (lines 9322–9352), `modeCone()` translates `z.leftStop` / `z.rightStop` into spindle pulse limits (`spindleMin` / `spindleMax`).
* When cutting reaches the end of the taper ($Z_{\text{end}}$), the ESP32 motion loop **instantaneously clamps axis steps at the hardware level**.
* The carriage physically stops in place. Over-travel, shoulder gouging, and chuck collisions are physically impossible, completely independent of display latency or Wi-Fi jitter.

#### B. Display Role: Air Moves Only
Because the cut stroke terminates safely in hardware, the display controller only orchestrates non-cutting repositioning in air:
1. Observes $Z$ at limit $\rightarrow$ sends `OFF`.
2. Commands cross-slide $+X$ rapid retract (clearance).
3. Commands carriage $+Z$ rapid return to $Z_{\text{start}}$.
4. Commands cross-slide $-X$ infeed to next pass depth.
5. Sends `ON` and engages `MODE_CONE` for the next pass.

---

### 3. Dynamic Live In-Cut and Between-Pass Adjustments

A manual machinist on an ELS demands tactile, adaptive control over cutting conditions:

#### 1. Live In-Cut Feed Rate Adjustment
* During any pass while cutting, the operator can press **`+`** / **`-`** (or nudge the joystick) to adjust `dupr` (feed rate per revolution) in real time.
* If chatter occurs or surface finish needs improvement, the operator immediately tunes the chip load without pausing the cut.

#### 2. Dynamic Between-Pass Depth of Cut (DOC) & Pass Rescheduling
* Between passes (during retract/return or while paused), the dynamic deck presents live supervisory controls:
  * `CURRENT PASS: 02 / 05`
  * `REMAINING STOCK: 1.200 mm`
  * `NEXT DOC: 0.400 mm` `[ − ]` `[ + ]`
  * `PASSES REMAINING: 3` `[ − ]` `[ + ]`
* **Real-Time Redistribution**:
  * If the tool handled Pass 1 easily, the operator can bump DOC up; the controller immediately reduces remaining passes.
  * If the machine vibrated or motor bogged, the operator taps `DOC −` or `PASSES +`; the controller recalculates:
    $$\text{DOC}_{\text{new}} = \frac{\Delta X_{\text{remaining}}}{N_{\text{remaining}}}$$
  * The cycle adapts dynamically to real cutting feedback without resetting or losing coordinate reference.

#### 3. Dedicated Finish Pass
* When remaining depth reaches the pre-set finish allowance (default: $0.10\text{ mm}$ / $0.004''$), the controller automatically switches to the designated finish feed rate (e.g. $0.05\text{ mm/rev}$) to yield a mirror surface before parking at safe Z.

---

## 7.8. Gearbox Simple Taper & Axis Isolation Architecture

### 1. Simple Taper Integration (`AXIS: Z+X (TAPER)`)
* In everyday lathe workflows, machinists frequently need quick, single-pass tapers, chamfers, or deburring without setting up a full multi-pass automated cycle.
* Gearbox Mode natively incorporates **Simple Taper** as the third axis option (`AXIS: [ Z | X | Z+X (TAPER) ]`), mapping directly to the firmware's `MODE_CONE` (`h5.ino` lines 9308–9356).
* The Z leadscrew acts as the primary driving axis synchronized to spindle rotation (`dupr`), while the X cross-slide operates as an electronic slave governed by the ratio $C = 2\tan(\theta_{\text{half}})$.

### 2. Strict Axis Isolation & Redundancy Elimination
* To eliminate cognitive clutter and preserve vertical screen real estate:
  * **Removal of Redundant Feed Rate Readouts**: The large feed rate boxes were removed from the Gearbox mode card because the primary driving feed rate is already continuously displayed and adjusted in the Center Column Dynamic Deck.
  * **Per-Axis Feed Rate Memory**: The controller continues to track and remember feed rates separately per axis (`gearPitchZ`, `gearPitchX`, `asyncSpeedZ`, `asyncSpeedX`). Switching axes restores each axis's individual feed parameters without cross-contamination.
  * **Contextual Card Properties**:
    * **When `AXIS: Z` is selected**: The mode card renders only Z leadscrew properties (Feed vs Pitch toggle, and thread lead when Pitch is active). Inactive X cross-feed properties are completely hidden.
    * **When `AXIS: X` is selected**: The mode card renders only X cross-feed properties. Inactive Z leadscrew properties are hidden.
    * **When `AXIS: Z+X (TAPER)` is selected**: The mode card renders only the slave Taper parameters (`EXT/INT`, `NORMAL/INVERT`, and Taper Angle/Ratio), since driving feed rate is adjusted in the center deck.

### 3. Direction Control Placement in Mode Card
* The feed direction selection button (`DIR: ← CHUCK (Z−)` / `DIR: → TAIL (Z+)` for Z and `DIR: ↑ CENTER (X−)` / `DIR: ↓ RETRACT (X+)` for X) is located directly at the top of the Gearbox mode card.
* **Safety Lockout**: To prevent accidental tool reversal during spindle rotation, the direction toggle is strictly interactive only when the motion controller is stopped (`state.runState !== 'running'`). During active cutting, the chip is dimmed (`opacity: 0.45`) and pointer events are locked.

### 4. Unified Compact Taper Display & Dedicated Edit Button Row
* To maximize readability and provide generous touch ergonomics:
  * The Taper block integrates the **Preset Name / Type** (e.g. `TAPER: 1:16 PIPE` or `TAPER: CUSTOM`), **Half-Angle** (e.g. `1.79°`), and **Cone Ratio** (e.g. `1:16 (0.0625)`) into a single, cohesive readout card spanning the full width of the block.
  * **Dedicated Edit Row**: Directly below the readout card, a dedicated action row positions the **`[ ✎ EDIT ]`** button aligned to the bottom right ($36\text{ px}$ height, amber fill, dark bold text).
  * Tapping either the readout card or the dedicated `[ ✎ EDIT ]` button opens the Taper Selection modal.

### 5. Dedicated Taper Selection Modal & Authoritative Custom Calculator
* Tapping the edit button or the taper card opens the `GEARBOX TAPER SELECTION` modal:
  * **Standard Presets Tab**: Two-tier organized selection of standard lathe tapers across three categories:
    * **Collets & Spindles**: Morse Tapers `MT1` through `MT5`, `R8 COLLET`, `5C COLLET`, `ER COLLET`, `7:24 STEEP (ISO)`.
    * **Ratios & Pins**: `1:50 METRIC PIN`, `1:48 US PIN`, `1:10 SHAFT`, `1:20 METRIC`, `1:5 METRIC`, `NPT/BSPT PIPE`.
    * **Chamfers & Centers**: `60° LATHE CENTER`, `45° CHAMFER`, `30° BEVEL`.
    * Presets are arranged in a 3-column top-aligned grid ($130\text{px}$ cards, $8\text{px}$ row gap, $14\text{px}$ column gap) with strict top-alignment and consistent spacing across all categories.
  * **Authoritative Custom Tab (Compact Single Input + Keypad + Informational Readouts)**:
    * Avoids bidirectional rounding drift and precision loss by establishing a **single authoritative entered value**:
      - **Proportional 36px Controls ($248\text{px}$ cluster)**: Features a compact input box ($140\text{px} \times 36\text{px}$) and adjacent format cycle toggle ($100\text{px} \times 36\text{px}$), strictly matching the $36\text{px}$ vertical height of the category and modal toggle chips above.
      - **Consistent Nomenclature**: Standardizes strictly on `ANGLE`, `RATIO`, and `DECIMAL` across the cycle button, keypad titles, and informational readouts.
      - **On-Screen Numeric Keypad Integration**: Tapping the input box opens the full-screen numeric keypad (`#kpOverlay`) pre-filled with the current value and titled with mode-specific engineering units (e.g., `CUSTOM TAPER (DEGREES)`, `(1:K RATIO)`, or `(DECIMAL)`). Committing via **SET** establishes the new authoritative master.
      - **Authoritative Value Tracking (Behind the Scenes)**: The exact number entered by the operator in their chosen format is tracked as the authoritative master (`authMode`, `authVal`). No developer-facing "authoritative" badges or clutter are shown in the UI.
      - **Cycle Without Edit**: Cycling the format toggle without modifying the value displays the converted equivalent for the new format while strictly retaining the original entered value as the authoritative source.
      - **Pure Text Informational Companions**: Below the input row, the two companion converted values are presented as quiet, pure typography ($14\text{px}$ monospace values with $11\text{px}$ muted labels separated by a vertical hairline) rather than boxed or recessed input-like elements.
      - **Late Conversion**: Converting to machine angle ($\theta$) and diametral ratio ($C$) occurs as late as possible when dispatching to the NanoELS motion controller, preserving exact drawing dimensions entered by the machinist.
      - **Main Screen Representation**: When custom taper is active, the Gearbox card prominently displays the entered format callout (e.g., `CUSTOM (1.79°)`, `CUSTOM (1:16.0)`, or `CUSTOM (0.05)`), allowing the operator to immediately verify against their blueprint.

### 6. Center Column Dynamic Deck Decluttering & Micro-Text Elimination
* **Elimination of Visual Clutter**: Tiny, low-contrast explanatory text snippets (e.g. `Cross-feed synchronized to...`, `Feed rate active: adjust via...`, `Z CARRIAGE`, `Z+X SLAVED`) have been removed from the entire card and center column. Such text creates visual noise and is unreadable from typical machine operating distance.
* **Ergonomic Rule**: Buttons and chips must prioritize physical touch targets ($\ge 36–38\text{ px}$) and bold, readable typography rather than miniature explanatory helper text.
* **Streamlined Header**:
  * Left: `FEED RATE` (or `THREAD LEAD` in Pitch mode, `ASYNC SPEED` in Async mode).
  * Right: Compact colored badge (`Z+X TAPER` in amber, `Z-AXIS` in green, or `X-AXIS` in blue).

### 7. Cone Slope Direction (Normal vs. Inverted Trapezoid Glyphs)
* To visually indicate the cone slope direction without cryptic text snippets or small micro-text:
  * **NORMAL (Taller toward Chuck)**: Rendered with an inline SVG trapezoid whose left side is taller than its right side (`polygon points="3,2 25,6 25,12 3,16"`), indicating a cone expanding toward the spindle.
  * **INVERT (Taller toward Tailstock)**: Rendered with an inverted SVG trapezoid whose right side is taller than its left side (`polygon points="3,6 25,2 25,16 3,12"`), indicating a cone expanding toward the tailstock.
  * Tapping the chip toggles `state.gearTaperInvert` and instantly updates the graphic glyph and cross-feed sign kinematics.

---

## 13. Component Consistency & LVGL Widget Architecture
* **Typography Inheritance**:
  * In standard browser user-agent stylesheets, `<button>` elements do not inherit `font-family` from `body` by default. A universal reset rule `button, input, select, textarea { font-family: inherit; }` enforces consistent typography (`Barlow Condensed`) across all dialog sheets and keypad modals.
* **Controller Face Component Unification (`div.tap`)**:
  * Rather than mixing HTML-form `<button>` elements with custom `<div>` touch targets, all interactive controls on the primary controller face have been standardized as uniform `div.tap` components (e.g. `btnRun`, `btnStop`, `btnOpenLog`, `btnOpenTaperModal`, `btnEditPitch`, `btnThreadNew`, `btnThreadEdit`, and D-pad arrows).
  * **LVGL 1:1 Mapping Benefit**: This eliminates browser-specific button outlines, user-agent margins, and flex quirks, mapping 1:1 to embedded LVGL objects (`lv_obj_t` with click callbacks and active-press state styles) during firmware UI implementation.

---

## 25. Universal Color Hierarchy & Visual Indicator Standards

To prevent cognitive clutter and ensure instant situational awareness on the shop floor, color is structured into four non-overlapping semantic tiers:

### Tier 1: Machine Safety & Motion State (Universal Industrial Rules)
* **Vibrant Green (`#3ddc84`)**: Reserved strictly for affirmative runtime execution:
  - Solid Fill: Stationary universal `START FEED` / `START CYCLE` button.
  - Telemetry: Top bar `RUNNING` status dot, real-time hardware pulse indicator, communication health pills (`ENC ●`, `DRIVE ●`).
* **Vibrant Red (`#ff5a5f`)**: Reserved strictly for danger, stop, and emergency boundaries:
  - Solid Fill / Glowing Halo: Stationary universal `STOP` button during active motion.
  - Hardware / Software Limit Contact: Jog arrows and chamfer buttons that trip a software limit turn high-visibility red (`.limit-blocked`) with physical step lockout into the boundary.
  - Top bar `STOPPED` status dot, modal `ABORT` buttons, and destructive emergency actions.
* **Amber / Caution (`#ffb454`)**: Reserved for supervisory interventions:
  - Supervisory Modals: Human-in-the-loop Skim Pass confirmation prompt (`skim-sheet h3`).
  - Kinematic Advisories: `⚠ Spindle: REVERSE (CW)` warning banner during blind-hole threading.
  - Machine Pause: Top bar `PAUSED` status dot.

### Tier 2: Kinematic Axis Identification (Z vs. X vs. Z+X)
Axis colors indicate *which physical leadscrew or tool vector is governed*:
* **$Z$ Axis (Longitudinal Carriage)** $\rightarrow$ **Emerald Green (`#3ddc84` / bg `#0d2116` / border `#1f4a2d`)**:
  - Center Dynamic Deck Header Badge: `Z-AXIS`.
  - Mode Card Axis Toggle: Highlights green when `AXIS: Z` is active (`.axis-z-on`).
  - Direction Chips: Carriage feed direction (`← CHUCK (Z−)` / `→ TAIL (Z+)`) in Gearbox and Turning (`.inline-dir-chip.dir-z`).
  - Parameter Card Framing: Z-axis leadscrew blocks receive a subtle green border (`rgba(61, 220, 132, 0.35)`) and dark tint (`rgba(61, 220, 132, 0.03)`).
  - Leadscrew Action Triggers: `[ ✎ PITCH ]` edit chip inside Z carriage block.
* **$X$ Axis (Transverse Cross-Slide)** $\rightarrow$ **Cyan Blue (`#5aa9ff` / bg `#0c1c2a` / border `#234058`)**:
  - Center Dynamic Deck Header Badge: `X-AXIS`.
  - Mode Card Axis Toggle: Highlights blue when `AXIS: X` is active (`.axis-x-on`).
  - Direction Chips: Cross-slide feed direction (`↑ TO CENTER (X−)` / `↓ RETRACT (X+)`) in Gearbox and Facing (`.inline-dir-chip.dir-x`).
  - Parameter Card Framing: X-axis cross-feed blocks receive a subtle blue border (`rgba(90, 169, 255, 0.35)`) and dark tint (`rgba(90, 169, 255, 0.03)`).
* **$Z+X$ Axis (Coordinated Cone / Taper)** $\rightarrow$ **Warm Amber (`#ffb454` / bg `#26190b` / border `#583e1c`)**:
  - Center Dynamic Deck Header Badge: `Z+X TAPER`.
  - Mode Card Axis Toggle: Highlights amber when `AXIS: Z+X (TAPER)` is active (`.axis-zx-on`).
  - Parameter Card Framing: Taper block receives an amber border (`rgba(255, 180, 84, 0.40)`) and dark tint (`rgba(255, 180, 84, 0.03)`).
  - Taper Action Triggers: `[ ✎ EDIT ]` button, preset title (`TAPER: 1:16 PIPE`), and cone slope direction chips.

### Tier 3: Dimensional Representation (DRO Left Column)
* **Diameter Mode (`Ø`)**: Styled in **Cyan Blue (`#5aa9ff`)** across the European engineering glyph (`Ø`) and toggle badge (`Ø DIA`), reinforcing that diameter is governed by the transverse X-axis.
* **Radius Mode (`R`)**: Styled in **Warm Amber (`#ffb454`)** across the glyph (`R`) and toggle badge (`R RAD`), warning the operator that radial infeed cuts 1:1 on tool radius.

### Tier 4: General UI Affordance & Telemetry
* **Numeric Coordinates & Critical Dimensions**: High-contrast white (`#e9edf0`, JetBrains Mono).
* **Live Feed / Dynamic Value Readout**: Warm Amber (`#ffb454`) in the center deck, visually distinguishing active feed rates from physical machine positions.
* **Engineering Units & Field Labels**: Neutral dim grays (`#84919b` / `#49545d`).
* **Active Mode Setup Toggles (Non-Axis)**: Subdued Slate Blue (`#18222a` / border `#283744` / text `#9cb1c9`) to avoid competing with primary axis colors.

---

## 26. Taper vs. Gearbox Z+X Disambiguation & State Machine Protocol

### 1. The Core Architecture Conflict
NanoELS core firmware (`Motion.cpp`) provides a single low-level motion state for 2-axis coordinated angled feed: `MODE_CONE` (Code `100`). It is fundamentally continuous electronic gearing ($Z$ leadscrew synced to spindle encoder; $X$ cross-slide slaved proportionally via `coneRatio`).

In the Companion Display controller, tapers serve two fundamentally different operational roles:
1. **Continuous Manual Gearing (`GEARBOX → AXIS: Z+X`)**: Single-pass continuous feeding along a taper angle, bounded by machine soft limits (analogous to $Z$ carriage feed and $X$ cross-facing).
2. **Automated Multi-Pass Cycle (`TAPER`)**: Autonomous canned cycle taking multiple roughing passes, automated $X$ infeed, and finish pass with rapid retract.

Because both features rely on the physical controller running in `MODE_CONE`, raw incoming telemetry (`UI.mode=CONE`) is inherently 1-to-many and cannot distinguish between them without state-ownership tracking.

### 2. Run-State Ownership & The 3 Yield Triggers
While the Companion Display UI is on the `TAPER` mode setup screen, it preserves `state.mode = 'TAPER'` and monitors incoming telemetry. The UI yields control and transitions to `GEARBOX (Z+X)` upon detecting any of the following three triggers:

1. **Trigger A: External Motion Inception (`UI.status === 'ON'`)**:
   - The UI's automated cycle was stopped (`state.runState !== 'running'`).
   - Incoming telemetry reports `UI.status=ON` (or `SYN`) with `UI.mode=CONE`.
   - **Inference**: The operator physically engaged feed at the lathe apron or Nextion screen. Because the lathe hardware only natively executes continuous single-pass cone feed, the UI immediately flips to `GEARBOX (Z+X)` to render active feed telemetry, live limit envelopes, and the illuminated `STOP` button.

2. **Trigger B: External Parameter Mutation (Nextion / Keypad Adjustment)**:
   - While idle in `TAPER`, incoming telemetry reflects an external change to a `CONE` parameter (e.g., `pitch`, `coneRatio`, or dedicated stop registers altered from the Nextion display).
   - **Inference**: The machinist is physically dialing in a manual cut on the machine. The UI yields to `GEARBOX (Z+X)` to reflect those adjustments.

3. **Trigger C: Mode Shift (Non-Cone Broadcast)**:
   - The controller reports any mode other than `CONE` (`GEAR`, `XGEAR`, `TURN`, `FACE`, `THREAD`, etc.).
   - The UI immediately leaves `TAPER` and tracks the new physical mode.

### 3. Cycle Execution Guarantee
When the operator taps `START CYCLE` on the Companion Display in `TAPER` mode:
* The UI sets `state.runState = 'running'`.
* Telemetry packets with `UI.status=ON` and `UI.mode=CONE` are recognized as expected cycle execution passes.
* The UI remains locked in `TAPER` mode, presenting the real-time pass monitor (`PASS 02 / 06`, current cut depth) with all setup fields locked out.


### 4. Multi-Client Coexistence Edge Case
When multiple companion displays are attached to the same ESP32:
* If Client A initiates an automated `TAPER` cycle, raw telemetry broadcast over WebSocket contains `UI.mode=CONE` and `UI.status=ON`.
* Client B (which did not initiate the cycle) follows Trigger A and displays `GEARBOX (Z+X)` cutting a live pass.
* **Safety Invariant**: Client B still accurately displays the tool cutting an angled feed, displays real-time DRO coordinates, and maintains a fully operational, high-priority `STOP` button capable of terminating motion instantly. Future firmware updates can optionally broadcast `UI.cycle=TAPER` for distributed multi-screen canned cycle synchronization.

---

## 27. Travel Limit Envelopes: Distance-To-Boundary & Vector Stop Indicator Standard

### 1. Conceptual Disconnect & Firmware Architecture
In benchtop electronic leadscrews like NanoELS, software limits (`leftStop`, `rightStop`) are fundamentally **work envelope boundaries** for multi-pass turning, facing, and threading cycles. 

In stock NanoELS firmware ([docs/h5.ino](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/docs/h5.ino)), the stop values broadcast over telemetry (`UI.zLeft`, `UI.zRight`, `UI.xLeft`, `UI.xRight`) and displayed on the Nextion hardware are **distances remaining from the current tool position to the limit**, not absolute machine coordinates:
$$\text{zStopLeft} = \text{stepsToDu}(z.\text{leftStop} - z.\text{pos})$$
$$\text{zStopRight} = \text{stepsToDu}(z.\text{pos} - z.\text{rightStop})$$

Previously, the Web UI prototype erroneously added these distances to the carriage position (`state.z + data.zRight`), producing arbitrary coordinate readouts (e.g. `16.881`) that clashed with the physical Nextion screen (`5.843`).

### 2. The Ergonomic Decision: Distance-to-Boundary
1. **Immediate Apron Utility**: At the lathe apron, an operator does not want to read an axis position of `-18.426`, look at a limit chip of `-24.269`, and perform mental subtraction during rapid feed. A distance readout of **`5.843`** counting down to `0.000` is immediately actionable.
2. **Direct Synergy with Mode Dimensions**: In `TURNING`, `FACING`, and `THREADING`, the operator enters a `LENGTH` or `STROKE` (e.g. `25.000 mm`). That length is identical to the distance between the envelope stops.
3. **1:1 Mirror of Nextion**: The Web UI, Nextion screen, and WebSocket telemetry stream show identical numbers with zero rounding or synchronization drift.

### 3. Vector Stop Indicators: Arrow-Into-Wall
To remove ambiguity between travel direction and stop location, all four limit chips render dedicated vector SVG icons where the arrow points **in the direction of carriage travel, hitting the boundary wall bar at the tip**:
* **Z Left (Chuck side)**: Vertical wall bar on the left with arrow pointing left into it (`|←`)
* **Z Right (Tailstock side)**: Vertical wall bar on the right with arrow pointing right into it (`→|`)
* **X Inward (Center / Into cut)**: Horizontal ceiling bar on top with arrow pointing up into it (`⤒`)
* **X Outward (Retract / Clearance)**: Horizontal floor bar on bottom with arrow pointing down into it (`⭳`)

### 4. Interactive Behaviors & Arrival Feedback
* **Active Display**: Left chip renders `|← 23.574`; right chip renders `14.426 →|`.
* **At Limit ($0.000$)**: When distance $\le 0.001\text{ mm}$, the chip illuminates amber (`.at-limit`) and the corresponding DPAD jog direction is automatically blocked with `.limit-blocked` visual feedback.
* **Manual Entry**: Tapping "SET DISTANCE…" opens the keypad and inputs the stroke distance from the current carriage position, directly aligning with the controller firmware numpad convention (`// Treat newDu as a relative distance from current position`).

---

## 28. Decoupled MachineModel & Multi-Domain Round-Trip Protocol Specification

### 1. Architectural Motivation & Decoupling Invariants
To achieve high reliability, modularity, and future extensibility across diverse displays (Web UI, future Nextion v2 rewrite, serial pendants, or native LVGL displays), the firmware's internal kinematic core is completely decoupled from presentation and transport layers:
1. **The Controller is the Sole Authority**: Connected clients (browsers, displays, pendants) are pure reactive observers. Setpoints and mode changes flow into the controller via authoritative intent commands (`CMD.*`), and state flows outward via domain-namespaced telemetry.
2. **Zero In-Server Math**: All string parsing, floating-point arithmetic, unit conversions, and pitch-to-DURP translations are removed from transport code ([src/WebServerModule.cpp](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/WebServerModule.cpp)) and encapsulated into the core kinematics API ([include/Motion.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Motion.h), [src/Motion.cpp](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/Motion.cpp)).
3. **Preservation of Authoritative Intent**: To prevent floating-point round-trip display drift (such as an operator entering taper `1:3.5` and having it return as `1:3.4985`, or entering `20 TPI` and having it return as `19.9998`), the controller stores the operator's exact typed intent alongside the hardware kinematics value.
4. **Layered Transport Separation**:
   * **Layer 1 (Core Kinematics)**: [include/Motion.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Motion.h), [include/Axis.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/Axis.h) — Hardware timers, step generators, integer deci-micron (`du`) coordinate tracking.
   * **Layer 2 (State & Serialization Adapter)**: [include/MachineModel.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/MachineModel.h), [include/RichClientAdapter.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/RichClientAdapter.h) — Structured domain capture and line-oriented streaming.
   * **Layer 3 (Command Dispatcher)**: [include/CommandDispatcher.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/CommandDispatcher.h) — Unified command dispatcher handling all transport sources (WebSocket, Serial, Web API).

---

### 2. Domain State Architecture (`MachineModel`)
The machine state is partitioned into four independent, cohesive domains:

```
+-------------------------------------------------------------------------+
|                              MachineModel                               |
+-------------------+-------------------+----------------+----------------+
|    Kinematics     |      Intent       |     Cycle      |   Properties   |
|   (Domain 'K')    |   (Domain 'I')    |  (Domain 'C')  |  (Domain 'P')  |
|     Bit: 0x01     |     Bit: 0x02     |   Bit: 0x04    |   Bit: 0x08    |
+-------------------+-------------------+----------------+----------------+
| zDu, xDu, yDu     | thFmt, thPitch    | passes         | measure        |
| zLeftDu, zRightDu | dupr              | opIdx (curr)   | dia, abs       |
| xUpDu, xDownDu    | tpMode, tpVal     | setupIdx       | zDro, xDro     |
| rpm, turns, angle | coneRatio         | starts         | screwZDu       |
| mode, on, estop   | feedStepDu        |                | screwXDu       |
| zDis, xDis, yDis  | jogStepDu         |                | motorStepsZ/X  |
| dupr, nextDupr    | feedRev           |                | encoderPpr     |
+-------------------+-------------------+----------------+----------------+
```

#### Domain Mask Flags ([include/MachineModel.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/MachineModel.h))
```cpp
enum DomainFlags : uint8_t {
  DOMAIN_NONE       = 0x00,
  DOMAIN_KINEMATICS = 0x01,  // Dynamic physical axes, limits, spindle, mode, on/off
  DOMAIN_INTENT     = 0x02,  // Thread format/pitch, taper ratio/angle, steps, feed direction
  DOMAIN_CYCLE      = 0x04,  // Multi-pass counts, current pass, operation step, multi-starts
  DOMAIN_PROPERTIES = 0x08,  // Unit system, diameter mode, DRO active, leadscrew pitches
  DOMAIN_ALL        = 0x0F   // All domains combined (0x01 | 0x02 | 0x04 | 0x08)
};
```

---

### 3. Flexible Multi-Domain Query Protocol
To eliminate multiple round-trips when a client initializes or transitions pages, the client can request any arbitrary combination of domains in a single command.

#### Query Syntax:
* **Compound Request**: `?get=K,I` or `CMD.get=K,I`
* **Single Domain**: `?get=P` or `?get=properties`
* **All Domains**: `?get=ALL` or `?state` or `?get=*`

#### Token Parser Mapping:
| Query Token | Target Domain Flag | Description |
| :--- | :--- | :--- |
| `K`, `kinematics`, `kin` | `DOMAIN_KINEMATICS` (`0x01`) | High-frequency physical axis positions and limits |
| `I`, `intent` | `DOMAIN_INTENT` (`0x02`) | Operator input setpoints (exact typed values) |
| `C`, `cycle` | `DOMAIN_CYCLE` (`0x04`) | Automated multi-pass and multi-start cycle counters |
| `P`, `properties`, `props` | `DOMAIN_PROPERTIES` (`0x08`) | Hardware leadscrew parameters and mode toggles |
| `ALL`, `*` | `DOMAIN_ALL` (`0x0F`) | Full state capture across all four domains |

---

### 4. Wire Telemetry Schema Reference

#### Domain 'K' — Kinematics (`K.*`)
| Key | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `K.zDu` | `long` | `du` (0.1 µm) | Canonical Z carriage position (DRO scale or stepper pos) |
| `K.xDu` | `long` | `du` (0.1 µm) | Canonical X cross-slide position |
| `K.yDu` | `long` | `du` (0.1 µm) | Canonical Y auxiliary position |
| `K.zLeftDu` | `long` | `du` (0.1 µm) | Z left/chuck limit position (`LONG_MAX` if unset) |
| `K.zRightDu` | `long` | `du` (0.1 µm) | Z right/tailstock limit position (`LONG_MIN` if unset) |
| `K.xUpDu` | `long` | `du` (0.1 µm) | X upper/inward limit position (`LONG_MAX` if unset) |
| `K.xDownDu` | `long` | `du` (0.1 µm) | X lower/outward limit position (`LONG_MIN` if unset) |
| `K.zDis` | `0` or `1` | boolean | Z axis motor disabled |
| `K.xDis` | `0` or `1` | boolean | X axis motor disabled |
| `K.yDis` | `0` or `1` | boolean | Y axis motor disabled |
| `K.rpm` | `int` | RPM | Filtered spindle speed |
| `K.turns` | `long` | revolutions | Spindle revolutions counter |
| `K.angle` | `long` | 0.1° (0..3599) | Spindle rotary angle |
| `K.mode` | `int` | enum | Operating mode (`0`=GEARBOX, `1`=THREAD, `2`=CONE, etc.) |
| `K.on` | `0` or `1` | boolean | Kinematic spindle-feed sync engaged |
| `K.estop` | `0` or `1` | boolean | Hardware emergency stop tripped |
| `K.dupr` | `long` | `du/rev` | Active kinematic feedrate |
| `K.nextDupr` | `long` | `du/rev` | Pending/buffered next feedrate (if changed while moving) |

#### Domain 'I' — Intent (`I.*`)
| Key | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `I.thFmt` | `int` | enum | Thread format (`0`=Metric mm, `1`=Imperial TPI, `2`=Imperial Inch) |
| `I.thPitch` | `float` | user unit | Exact authoritative pitch entered by operator (e.g. `20.0000` for 20 TPI) |
| `I.dupr` | `long` | `du/rev` | Internal feedrate equivalent |
| `I.tpMode` | `int` | enum | Taper input mode (`0`=Ratio 1:K, `1`=Angle °, `2`=Standard Preset) |
| `I.tpVal` | `float` | ratio / deg | Exact authoritative taper value (e.g. `3.5000` for 1:3.5, `8.1300` for 8.13°) |
| `I.coneRatio` | `float` | dimensionless | Resolved kinematic cone ratio ($X / Z$) |
| `I.tpPreset` | `int` | id | Preset identifier (-1 if custom ratio/angle) |
| `I.feedStepDu` | `long` | `du` (0.1 µm) | Discontinuous feed step size (100, 500, 1000, 2000 du) |
| `I.jogStepDu` | `long` | `du` (0.1 µm) | Continuous manual jog step size (100, 1000, 10000 du) |
| `I.feedRev` | `0` or `1` | boolean | Feed direction reversal state |

#### Domain 'C' — Cycle (`C.*`)
| Key | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `C.passes` | `int` | count | Total programmed passes for automated cycle |
| `C.opIdx` | `int` | index | Active cut pass index |
| `C.setupIdx` | `int` | index | Active step in setup wizard |
| `C.starts` | `int` | count | Multi-start thread count |

#### Domain 'P' — Properties (`P.*`)
| Key | Type | Unit | Description |
| :--- | :--- | :--- | :--- |
| `P.measure` | `0` or `1` | enum | Unit system (`0`=Metric, `1`=Imperial) |
| `P.dia` | `0` or `1` | boolean | Lathe X cross-slide diameter mode enabled |
| `P.abs` | `0` or `1` | boolean | Coordinate system (`1`=Absolute, `0`=Incremental) |
| `P.zDro` | `0` or `1` | boolean | Physical optical/magnetic DRO scale active on Z |
| `P.xDro` | `0` or `1` | boolean | Physical optical/magnetic DRO scale active on X |
| `P.screwZDu` | `long` | `du` (0.1 µm) | Hardware Z leadscrew pitch |
| `P.screwXDu` | `long` | `du` (0.1 µm) | Hardware X cross-slide leadscrew pitch |
| `P.motorStepsZ` | `long` | steps/rev | Z stepper driver microsteps per motor revolution |
| `P.motorStepsX` | `long` | steps/rev | X stepper driver microsteps per motor revolution |
| `P.encoderPpr` | `long` | pulses/rev | Spindle encoder pulses per revolution |

---

### 5. Backward Compatibility Guarantee
During the UI transition, [RichClientAdapter::buildTelemetry()](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/RichClientAdapter.cpp) simultaneously emits both the new domain keys (`K.*`, `I.*`, `C.*`, `P.*`) and the legacy mirror keys (`M.*`) whenever `DOMAIN_ALL` or `DOMAIN_KINEMATICS` is transmitted. This ensures zero regression in existing test clients or background scripts while enabling modern modular UI subscriptions.

---

### 6. Generic Client Architecture: Transport / Serializer / Translator Pipeline
To ensure any future transport (physical Nextion v2 UART driver, USB-UART serial pendant, Bluetooth Low Energy, or CAN/Modbus bus) can be added cleanly without modifying core firmware or existing web clients, the system implements a composable client pipeline ([include/ClientEndpoint.h](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/ClientEndpoint.h)):

$$\text{ClientEndpoint} = \text{Transport} + \text{Serializer} + \text{Translator}$$

```
   Raw Wire (Bytes/Text)              Inbound Translation                   Core Controller
[ Physical I/O Channel ] ──(Inbound)──► [ ICommandTranslator ] ──(Dispatch)──► [ Motion / Model ]
           ▲                                                                           │
           │                                                                           │
           └──────────────(Outbound)── [ ITelemetrySerializer ] ◄──(Capture)───────────┘
```

1. **`ITransport` (Physical I/O)**:
   * Pure virtual wire interface: `isConnected()`, `sendText(str)`, `sendBytes(buf, len)`.
   * Concrete implementations: `WebSocketTransport`, `NextionUartTransport`, `SerialPendantTransport`, `BleTransport`.
2. **`ITelemetrySerializer` (Outbound Encoding)**:
   * Transforms typed [MachineModel](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/MachineModel.h) structs into the client's expected wire format.
   * `RichClientAdapter` implements this to produce line-oriented ASCII `K.*`, `I.*`, etc.
   * A future `NextionSerializer` will produce Nextion HMI display commands (e.g. `t0.txt="..."`).
3. **`ICommandTranslator` (Inbound Decoding)**:
   * Decodes raw inbound wire payloads into authoritative controller actions.
   * `DefaultCommandTranslator` passes text commands directly to [CommandDispatcher](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/CommandDispatcher.h).
   * A future `NextionTranslator` or `PendantTranslator` can decode binary touch packets (`0x65 0x01...`) into canonical commands (`CMD.mode=...`).
4. **`ClientEndpoint` (Composite Channel)**:
   * Manages the lifecycle of a connected client, routing incoming messages through the translator and transmitting replies and change-triggered telemetry broadcasts over the transport.

---

## 29. Monolith Decomposition, Alpine.js Store & Semantic Intent Architecture

### 1. Architectural Motivation
The monolithic prototype HTML file grew to over 5,700 lines containing embedded CSS stylesheets, math utilities, modal controllers, and DOM manipulation scripts. To make the architecture scalable, modular, and compatible with ESP32 LittleFS and LVGL 9, the monolithic structure was decomposed into decoupled layers, and the data model was unified around **Semantic Operator Intent**.

### 2. File Organization & Zero-Build Runtime
Everything runs **100% offline** with **zero build step**, operating identically whether served from ESP32 LittleFS or opened directly via `file:///`:

| File | Purpose | Lines |
| :--- | :--- | :--- |
| [`lathe_dro_nanoels_controller.html`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/lathe_dro_nanoels_controller.html) | Declarative HTML5 markup bound to Alpine store | ~530 |
| [`alpine.min.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/alpine.min.js) | Alpine.js v3.17.4 local offline runtime | ~43 KB |
| [`lathe-controller.css`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/lathe-controller.css) | Extracted modular styles, typography, and layout | ~1,250 |
| [`nanoels-presets.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-presets.js) | Standard engineering thread families & taper data | ~350 |
| [`nanoels-formatters.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-formatters.js) | Pure render-time formatters (deci-microns $\rightarrow$ human strings) | ~90 |
| [`nanoels-state.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-state.js) | `Alpine.store('machine')` reactive 4-domain model | ~240 |
| [`lathe-controller.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/lathe-controller.js) | UI modal controllers, wizard engines, and dev console | ~4,000 |

---

### 3. Display Units & Diameter Mode Decoupling
A crucial architectural decision was made to **completely decouple display presentation from the controller core**:

1. **Physical Invariance in the Core**:
   * The ESP32 kinematics and stepper pulse generators deal strictly in canonical **deci-microns (`du`)** and microsteps.
   * The core controller does not care whether a user is looking at inches or millimeters.
2. **Multi-Display Independence**:
   * One operator on an apron tablet can view the DRO in **Inches** (reading an imperial print), while a second display on the headstock or phone displays **Millimeters**.
   * Tapping `unitToggle` (`mm/in`) or `repBadge` (`Ø DIA / R RAD`) **does not transmit wire commands to the controller**.
   * It purely updates the client-local presentation store (`$store.machine.measure` and `$store.machine.dia`), eliminating round-trip latency and cross-client interference.

---

### 4. Semantic Intent vs. Raw Directional Signs
Hardcoding $+/-$ coordinate math into a UI creates serious safety hazards (e.g. rear-toolpost parting blades or slant-bed lathes moving opposite to front toolposts).

Instead of commanding raw coordinate signs, clients transmit **operator semantic intent**:

```text
CMD.jogHold=X,INFEED       // Operator wants to move tool into the cut / towards centerline
CMD.jogHold=X,RETRACT      // Operator wants to back the tool out of the cut
CMD.jogHold=Z,CHUCK        // Operator wants to feed carriage toward the chuck
CMD.jogHold=Z,TAIL         // Operator wants to feed carriage toward the tailstock
CMD.jogRelease=<axis>      // Operator released the jog key
CMD.jogStep=<axis>,<dir>   // Discrete 1-step movement in semantic direction
```

#### Example: Cutting an Inverted Taper
When turning an inverted taper (narrow at the chuck, wide at the tailstock) from the narrow end to the wide end:
* **`TAPER_PROFILE`**: `INVERTED` (Narrow at Chuck $\rightarrow$ Wide at Tailstock).
* **`SURFACE`**: `EXTERNAL` (OD Turning) vs `INTERNAL` (Boring).
* **`FEED_DIR`**: `TO_TAIL` (traveling from Chuck toward Tailstock).
* **Controller Kinematics Execution**:
  * Primary axis $Z$ feeds `TO_TAIL`.
  * Synchronized cross-slide $X$ is calculated by the core: since diameter expands toward the tailstock on an external cut, $X$ automatically moves **`RETRACT`** at the precise taper ratio $K$.
  * The physical motor pin polarities are resolved exclusively in firmware configuration (`Config.h`), protecting against crashes regardless of toolpost orientation.

---

### 5. Verification Status
* **PlatformIO ESP32-S3 Build**: Succeeded in 11.97s, 0 errors, 0 warnings (Flash 19.4%, RAM 25.0%).
* **Browser Runtime Verification**: Verified against local emulator, 0 console errors, instantaneous client-side unit toggles, and clean semantic packet streams.

---

## 30. Comprehensive Display Presentation Decoupling (Feeds, Spindle Surface Speed, Threads, Tapers)

### 1. Architectural Motivation
Decoupling units is not merely about toggling `mm` and `in` labels on coordinates. A high-performance digital lathe interface must allow an operator to view machining parameters in the units that match their tooling, stock material, and mental model without mutating the controller's underlying canonical kinematics or creating network thrashing.

### 2. Decoupled Display Domains

#### A. Feed & Rapid Rates (`formatFeed`)
* **Controller Canonical Invariant**: Synchronized feed is stored as `dupr` (deci-microns per spindle revolution, where $10{,}000\text{ dupr} = 1.000\text{ mm/rev}$).
* **Client Presentation**:
  * Metric mode: Formatted to 3 decimal places as `mm/rev` (e.g. `0.100 mm/rev`).
  * Imperial mode: Computed as $\text{dupr} / 254{,}000.0$ and formatted to 4 decimal places as `in/rev` (e.g. `0.0039 in/rev`).
  * Time-based Async Rapid/Feed:
    * Metric mode: `mm/min` (e.g. `150 mm/min`).
    * Imperial mode: Converted via $\text{speed} / 25.4$ to `in/min` (e.g. `5.9 in/min`).
  * When the operator toggles the unit chip in the topbar, the feed display converts instantly with zero lag and zero network traffic.

#### B. Live Spindle Surface Speed Readout (`formatSurfaceSpeed`)
* **4-Way Spindle Readout Cycling**:
  Tapping `#spindleBox` cycles through 4 real-time telemetry modes:
  1. `RPM`: Spindle revolutions per minute.
  2. `TURNS`: Cumulative spindle revolutions (useful for coil winding, precise manual indexing).
  3. `°`: Spindle rotational position ($0.0^\circ$ to $359.9^\circ$).
  4. `SMM / SFM`: Live Cutting Surface Speed.
* **Surface Speed Calculation**:
  Derived at 10 Hz from the current workpiece diameter (live $X$ DRO position in diameter mode) and spindle RPM:
  * Metric mode ($SMM$, Surface Meters per Minute):
    $$SMM = \frac{\pi \times \text{Dia}_{\text{mm}} \times \text{RPM}}{1000.0}$$
  * Imperial mode ($SFM$, Surface Feet per Minute):
    $$SFM = \frac{\pi \times \text{Dia}_{\text{in}} \times \text{RPM}}{12.0} \approx \frac{\text{Dia}_{\text{in}} \times \text{RPM}}{3.8197}$$
  * Toggling `mm` $\leftrightarrow$ `in` instantly flips the readout between `SMM` and `SFM`.

#### C. Dual Thread Companion Readouts (`formatPitchWithCompanion`)
* Machinists frequently work from drawings specified in metric pitch ($P$ in mm) while using imperial leadscrews or vice versa.
* The decoupled display shows the primary pitch plus the companion metric in parentheses:
  * Imperial Primary: `20 TPI (1.270 mm)`
  * Metric Primary: `1.500 mm (16.93 TPI)`
  * Fractional Inch Primary: `0.0500 in (20.0 TPI)`

#### D. Multi-Format Taper Companions (`formatTaperCompanions`)
* A lathe taper is physically defined by a single slope ratio $C = \Delta \text{Dia} / \Delta Z$.
* The display simultaneously provides companion conversions across all 3 machining conventions:
  * **Ratio**: $1 : K$ (e.g. `1 : 20.00` or `1 : 19.25` for MT2).
  * **Angle**: Half-angle ($\theta = \arctan(C/2)$) and Included Angle ($2\theta$).
  * **Taper Per Foot (TPF)**: $C \times 12.0\text{ in/ft}$ (e.g. `0.5994 in/ft` for MT2, `0.7500 in/ft` for NPT).
  * **Metric Taper**: $C \times 1000.0\text{ mm/m}$ (e.g. `50.0 mm/m`).

#### E. Ergonomic Jog Step Presets
* Metric mode cycles: `1.000 mm`, `0.100 mm`, `0.010 mm`.
* Imperial mode cycles: `0.1000 in`, `0.0100 in`, `0.0010 in`.
* When switching units, the active step snaps to the equivalent ergonomic preset index rather than an awkward fractional conversion (e.g. `0.0010 in` instead of `0.00039 in`).

---

## 31. Lossless Intent State Architecture (`IntentState` / `I.*`)

### 1. The Problem: Floating-Point Round-Trip Degradation
In traditional electronic leadscrew implementations, operator authoring values are immediately flattened into a single kinematic float (e.g. `dupr` for pitch or $C = \Delta \text{Dia}/\Delta Z$ for taper).
When these values are transmitted back across telemetry or parsed by a secondary display:
1. Entering **`1 : 19`** taper ratio ($C = 1/19 \approx 0.052631578...$) degrades to **`1 : 18.99`** or **`1 : 19.0002`** when backwards-calculated from $C$.
2. Selecting standard preset **`MT2`** ($C \approx 0.04995$) degrades into an arbitrary decimal `0.0499` without memory that the operator chose Morse Taper #2.
3. Entering **`13 TPI`** ($P = 25.4 / 13 \approx 1.953846...$) degrades into `13.0003 TPI` or `12.9998 TPI`.

### 2. The Solution: First-Class Intent Domain
The controller maintains an authoritative `IntentState` structure in firmware (`MachineModel.h`) and telemetry broadcasts (`I.*` / `M.*`):
* `I.tpPreset`: Selected preset ID (`-1` for custom, $\ge 0$ for Morse, Brown & Sharpe, R8, etc.).
* `I.tpMode`: Authoring mode (`0` = 1:K Ratio, `1` = Angle degrees, `2` = Preset).
* `I.tpVal`: Exact user-entered scalar (e.g. `19.0` for 1:19, `1.79` for 1.79°).
* `I.thFmt`: Thread format (`0` = Metric mm, `1` = Imperial TPI, `2` = Imperial Inch).
* `I.thPitch`: Exact user-entered pitch (e.g. `13.0` for 13 TPI, `1.500` for 1.5mm).

### 3. End-to-End Pipeline Guarantees
1. **Keypad & Wizard Entry**: `kpConfirm` and `commitThreadSettingsFromWiz` capture exact numeric user input into `state.threadTpiVal`, `state.gearPitchZ_tpi`, `state.gearTaperAuthVal`, and `state.gearTaperPresetIdx`.
2. **Wire Dispatch**: Commands explicitly serialize intent:
   * `CMD.feedTpi=13`
   * `CMD.feedMm=1.5`
   * `CMD.taperPreset=2,1.4306,20.0196`
   * `CMD.taperRatio=19`
   * `CMD.taperAngle=1.79`
4. **Render-Time Formatters**: Formatters in `nanoels-formatters.js` render primary strings directly from the intent registers, and calculate companions on demand, guaranteeing zero decimal flicker and zero cumulative drift.

---

## 32. Multi-Client WebSocket Server Architecture & Shop-Hardened Resilience

### 1. Heterogeneous Channel Routing (`/canonical`, `/grbl`, `/dro`)
Different clients require different wire protocols, but runtime packet parsing introduces CPU and latency overhead:
* **HTTP Upgrade URL Routing**:
  * `ws://<ip>:81/` or `ws://<ip>:81/canonical` $\rightarrow$ `CHANNEL_CANONICAL`: Full 4-domain semantic state (`K.*`, `I.*`, `C.*`, `P.*`) for the rich Web UI and companion touchscreens.
  * `ws://<ip>:81/grbl` $\rightarrow$ `CHANNEL_GRBL`: Emits `<Idle|WPos:...|FS:...>` format compatible with standard CNC senders (Universal Gcode Sender, CNCjs).
  * `ws://<ip>:81/dro` $\rightarrow$ `CHANNEL_DRO`: Emits lightweight coordinate stream (`K.zDu`, `K.xDu`, `K.rpm`) for minimal wireless digital readouts.
* **Zero Runtime Packet Overhead**: Routing is established once during the WebSocket handshake (`WStype_CONNECTED`). Subscriptions do not require dynamic parsing or filtering per frame.
* **Active Channel Masking**: Channels without active client connections are never serialized, burning zero CPU cycles.

### 2. Change-Driven Telemetry & Quiescence
* **Zero Minimum Rate**: When the lathe is idle and spindle is stationary, the telemetry engine enters total quiescence (`changedDomains == DOMAIN_NONE`). No packets are emitted, conserving radio bandwidth and CPU cycles.
* **35 ms Rate-Ceiling Clamp (~28 Hz)**: Rapid motion (2,500+ RPM spindle rotation or fast carriage rapids) is clamped to a clean 35 ms minimum interval, preventing TCP buffer bloat and ensuring Core 1 stepper timing (`taskMoveZ`, `taskMoveX`) is never starved.

### 3. Dedicated `SettingsService` & Unicast Transaction Protocol
* **Strict Unicast Request/Reply**: Commands (`SETTINGS.get`, `SETTINGS.set`, `?sysinfo`, `?get`) reply exclusively to the requesting client ID via `webSocket.sendTXT(clientId, ...)` rather than broadcasting to all connected screens.
* **Settings Bounds Checking & NVS Storage**:
  * Handled by dedicated [`SettingsService.h`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/include/SettingsService.h) and [`SettingsService.cpp`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/src/SettingsService.cpp).
  * Validates leadscrew pitches, microsteps, encoder PPR, backlash compensation, and DRO scale sources against strict physical bounds.
  * Mutates `Preferences` in non-volatile flash and sets `DOMAIN_PROPERTIES` dirty so all canonical clients synchronize automatically.
* **Client-Side Promise Transactions**:
  * [`nanoels-service.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-service.js) implements `getSettings(key)` and `setSetting(key, val)` using asynchronous Promise queues matching incoming `SETTING.<key>`, `OK:SETTINGS.<key>`, or `ERR:SETTINGS.<key>`.
  * [`nanoels-emulator.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-emulator.js) mirrors this interface for seamless offline UI development.

### 4. Multi-Core Concurrency & Recursive Mutex (`wsMutex`)
* Stepper pulse generation runs on Core 1 at maximum FreeRTOS priority with zero network mutex acquisition.
* Display updates (`taskDisplay`) and WebSocket networking (`taskWiFi`) run on Core 0.
* WebSocket loop and transmission are protected by a FreeRTOS recursive mutex (`xSemaphoreCreateRecursiveMutex`), preventing memory corruption across tasks while safely allowing synchronous command replies from inside the socket loop without self-deadlock.

### 5. Shop-Hardened Transient Network & EMI Resilience
* **2.5s Disconnect Debounce**: Machine shop electrical noise (spindle VFD switching, contactors) can cause brief WiFi frame drops. The firmware debounces disconnections for 2,500 ms before resetting the connection, avoiding false alarms and socket teardowns.
* **Exponential Backoff Reconnect (3s $\rightarrow$ 30s)**: On genuine AP disconnect, retries back off exponentially (3s $\rightarrow$ 6s $\rightarrow$ 12s $\rightarrow$ 24s $\rightarrow$ 30s max cap) to prevent radio saturation.
* **Nextion Hardware Status Integration**: Physical Nextion display widget `t3` displays real-time countdown status (`"WiFi retry (Xs)"`) and recovers to `"See <ip>"` upon reconnection.






