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

---

## 2. Display & Ergonomic Standards (800×480 Landscape)

* **DRO Layout & Typography:**
  * Coordinate readouts are locked to a static `50px` monospace font (`JetBrains Mono` / tabular lining figures). Dynamic font auto-shrinking is prohibited to prevent digit jumping during axis motion.
  * Inline unit labels (`mm`/`in`) are omitted from axis coordinate lines to guarantee full horizontal fit for negative 7-figure values (e.g., `-300.0000 in`). The active system unit is anchored prominently in the top bar.
  * Diameter symbol strictly utilizes the authentic uppercase Danish/European engineering glyph (`Ø`), matching uppercase `R`.
* **Directional Alignment (Operator Reference Frame):**
  * $Z$ Axis (Carriage): `← Z CHUCK` is toward the headstock ($Z-$); `→ Z TAIL` is toward the tailstock ($Z+$).
  * $X$ Axis (Cross-Slide): `↑ TO CENTER` is into the workpiece/plunge ($X-$); `↓ RETRACT` is toward the operator/backing out ($X+$).
* **Apron/Jog Cluster Placement:**
  * Jog D-pad and chamfer keys are pinned to the bottom of the center column, providing wrist/palm stability against lathe bed rims during manual stepping.
  * Step increment selector cycles on tap (`0.010` → `0.100` → `1.000`), with long-press (>600ms) opening the numeric keypad for custom increments.
* **Unified Universal Runstack:**
  * All modes consistently feature the primary action stack in the bottom-right corner: `START FEED` (green) directly above `STOP` (red).

---

## 3. Numeric Keypad Architecture

* **Standard Calculator/ATM Layout:**
```text
[ 7 ]  [ 8 ]  [ 9 ]
[ 4 ]  [ 5 ]  [ 6 ]
[ 1 ]  [ 2 ]  [ 3 ]
[ ± ]  [ 0 ]  [ . ]
[ CURR ] [ CLR ] [ ⌫ ]
```
* **Clean Opening State:** Keypad opens with an empty buffer (placeholder `0`). No pre-highlighted zero to delete.
* **Conditional `INSERT CURR`:** The `CURR` button is strictly gated—visible only when entering axis coordinates or soft limits; hidden for pitch, pass counts, and speeds.
* **Cursor Positioning & Direct Input:** Keypad display acts as an editable input allowing pointer focus and cursor editing.

---

## 4. Operational Modes & Dynamic Center Deck

* **Dynamic Center Action Deck:**
  * Situated above the jog D-pad. Houses large, high-visibility nudge buttons (`[ − ]` and `[ + ]`) to permit safe, rapid feed trimming while turning under chips.
  * **In Gearbox Mode:** Houses `SYNC / ASYNC` feed toggle and `AXIS: Z / X` mutually exclusive selector.
  * **In Turning / Facing Modes:** Strips away axis toggles; displays real-time cycle status (`PASS 02 / 06`) with large feed trim buttons active.
* **Turning Mode Parameters:**
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


