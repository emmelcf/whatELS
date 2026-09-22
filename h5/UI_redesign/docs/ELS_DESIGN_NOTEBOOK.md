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

## 13. Safety-Critical Running Lockout: Silent Disable vs. Warning Banners

### Safety Philosophy
On an operating metal lathe, visual occlusion and surprise modal dialogs are severe safety hazards:
* If a tool is cutting near a chuck or shoulder, an operator who accidentally touches the mode header must **never** be met with a modal dialog, toast notification, or warning banner.
* Warning banners occlude the live coordinate displays, distract the operator's eye, and can block or delay access to the red **STOP** button.

### Implementation Standard
1. **Visual Indication Without Occlusion**:
   * When an operation is active (`state.runState === 'running'`), the mode header (`#modeHead`) is rendered in a locked state:
     * Subtly dimmed background and opacity (`0.55`).
     * Explicit padlock icon appended to the header title: `GEARBOX 🔒`.
     * Non-interactive pointer cursor (`cursor: not-allowed`).
2. **Silent No-Op Handler**:
   * Tapping `#modeHead` while running performs a silent `return;`.
   * Zero toasts, zero alerts, and zero popup banners appear.
3. **Unobstructed Operator Control**:
   * The operator's only options during active motion are to trim allowed runtime parameters (such as feed rate via `+`/`-`) or hit the large, permanently anchored red **STOP** button.

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
