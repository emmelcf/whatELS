# NanoELS H5 — Rich Touch HMI Profile (800×480 Capacitive Touch)
## Industrial Ergonomics, Canvas Topology & Human-Machine Interface Standards
*Target Devices:* 5.0"–7.0" 800×480 Landscape Capacitive Panels (Web UI Browser, Native ESP32-S3 LVGL 9, Companion Tablets)

> **Architectural Scope:** This document defines the concrete **800×480 Capacitive Touch HMI Profile**, extending the universal principles defined in [`HMI_DESIGN_GUIDELINES.md`](./HMI_DESIGN_GUIDELINES.md). It governs canvas geometry, touch hitbox thresholds, visual hierarchy, typography, keypad mechanics, and color semantics across all rich graphics implementations (whether rendered in HTML/CSS/Alpine or native C++/LVGL).

---

## 1. Core HMI Philosophy & Safety Invariants

Operating an electronic leadscrew on a manual metal lathe involves high spindle speeds, sharp carbide tooling, and tight clearances near chuck jaws. The HMI enforces four non-negotiable physical safety invariants:

1. **Radical Hardware Transparency:**
   * NanoELS firmware maintains strictly a single pair of physical hardware limit registers per axis (`z_min`/`z_max`, `x_min`/`x_max`).
   * The HMI must never render "ghost" limits or synthetic software safety envelopes that the motion controller does not physically enforce. What displays on the DRO limit chips reflects the exact hardware boundary active in the motion core.
2. **Dimension-First Machining Intent:**
   * Machinists think in drawing dimensions (e.g., "turn a 25.0 mm shoulder" or "face 1.5 mm off the stock"), not arbitrary machine coordinates.
   * Setting a feature dimension (`LENGTH` in turning, `STROKE` in facing) automatically maps relative to tool touch-off into the required hardware limit register, eliminating manual jog-and-subtract math at the apron.
3. **Opt-In Limit Restoration:**
   * Because automated canned cycles overwrite limits destructively, the HMI provides an opt-in `RESTORE LIMITS ON COMPLETION` toggle. The host controller caches pre-existing safety envelopes (e.g., chuck safety stops) and restores them upon cycle termination without firmware modification.
4. **Permanent, Unobstructed Emergency Stop & Running Lockout:**
   * While an axis is feeding or a cycle is executing, modal popups, keypad sheets, and warning dialogs are **strictly prohibited**.
   * The red **`STOP`** button is permanently anchored in the bottom-right corner, always visible, always tactile, and physically separated from affirmative triggers. Muscle memory must never hesitate or miss during an emergency.

---

## 2. Screen Canvas Topology & 3-Column Spatial Hierarchy

The target physical canvas is a 5.0"–7.0" landscape display ($800 \times 480$ pixels, $\approx 187\text{ DPI}$, $0.135\text{ mm/px}$). The vertical canvas is budgeted into:
* **Top Bar (42px):** Machine brand, connection status dot, system units chip (`MM` / `IN`), active mode chip, and utility shortcuts (`DOCS`, `LOG`, `SETTINGS`).
* **Active Main Viewport (408px):** The primary operational surface divided into 3 functional columns.
* **Status Bar (30px):** Communication health indicators (`ENC ●`, `DRIVE ●`), WiFi/IP telemetry, and real-time controller heartbeat.

```text
┌───────────────────────────────────────────────────────────────────────────────────┐
│ TOP BAR (42px): BRAND | IP/WIFI | [MM/IN] | [MODE: GEARBOX] | [DOCS] [LOG] [GEAR]  │
├───────────────────────┬───────────────────────────┬───────────────────────────────┤
│ COLUMN 1: DRO (270px) │ COLUMN 2: CENTER (202px)  │ COLUMN 3: RIGHT (260px)       │
│                       │                           │                               │
│  [ SPINDLE CARD ]     │  [ DYNAMIC CENTER DECK ]  │  [ MODE SETUP CARD ]          │
│   RPM / TURNS / SMM   │   Mode-Active Controls:   │   Top-Aligned Setup:          │
│                       │   Feed Nudges [ - ] [ + ] │   SYNC/ASYNC, AXIS Z/X,       │
│  [ Z CARRIAGE CARD ]  │   Dir Toggle [ ← CHUCK ]  │   Leadscrew Pitch,            │
│   50px Z Readout      │   Step Chips [ ±0.05 mm ] │   Wizards & Presets           │
│   Limit [ |← ] [ →| ] │                           │                               │
│                       │  [ APRON JOG D-PAD ]      │  [ UNIVERSAL RUNSTACK ]       │
│  [ X CROSS-SLIDE CARD]│   Bed-Rim Palm Alignment: │   12px Air Gap Safety:        │
│   50px X Readout      │   [ ▲ TO CTR ]            │   [ START FEED / CYCLE ] (Grn)│
│   [ Ø DIA / R RAD ]   │ [ ◀ ][ STEP ][ ▶ ]        │                               │
│   Limit [ ⤒ ] [ ⭳ ]   │   [ ▼ RETRACT ]           │   [ STOP ] (Perm Red Corner)  │
├───────────────────────┴───────────────────────────┴───────────────────────────────┤
│ STATUS BAR (30px): ENC ● DRIVE ● | LIMITS OK | TELEMETRY 28Hz                     │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### Column Roles:
1. **Left Column (DRO — 270px):** Pure continuous observation. Displays spindle telemetry, Z carriage coordinate, X cross-slide coordinate, diameter/radius representation badge, and distance-to-limit countdown chips.
2. **Center Column (Dynamic Deck & Apron Cluster — 202px):** Pure live physical intervention. Houses live mode controls only (feed trims, direction toggle) directly above the Apron Jog D-pad.
3. **Right Column (Mode Configuration & Universal Runstack — 260px):** Parameter setup and motion initiation. Houses setup toggles, pitch/depth selectors, canned cycle parameters, and the permanent runstack.

---

## 3. Industrial Touch Ergonomics & Minimum Hitbox Standards

In machine shop environments, operators work with bare, oily, or gloved hands while feeling vibration from lathe cutting forces. All interactive elements satisfy ISO 9241-410 touch thresholds:

### Tiered Sizing Floor

| Element Type | Minimum Height | Minimum Width | Minimum Inter-Button Gap | Physical Size (5" Screen) |
| :--- | :--- | :--- | :--- | :--- |
| **Primary Industrial Actions** (`START`, `STOP`, Jog D-Pad, Keypad Keys, Nudge Buttons, Zero Axis) | **44px – 48px** | **56px – 64px** | **6px** | $5.9\text{ mm} \times 8.6\text{ mm}$ |
| **Secondary Setup Chips** (Limit Chips, Unit Toggle, `Ø/R` Badge, Setup Toggles, Feed Step Chips) | **36px – 38px** | **50px** | **4px** | $4.9\text{ mm} \times 6.8\text{ mm}$ |
| **Tertiary Utility Chips** (Top Bar Shortcuts, Status Log Trigger) | **32px – 35px** | **44px** | **4px** | $4.3\text{ mm} \times 5.9\text{ mm}$ |

*Absolute Ergonomic Rule:* No interactive touch target may ever be rendered below 36px in height or 48px in width.

### Bed-Rim Palm Anchoring & Apron Cluster Placement
* The Jog D-Pad and Chamfer keys are pinned to the **bottom of the center column**.
* This allows the machinist to rest the heel of their hand or wrist physically against the front lathe bed casting or control box bezel while tapping jog increments, eliminating hand shake and accidental double-taps under cutting load.

### Pointer Event Timing Contracts
* **Discrete Jog Step (Tap `< 300ms`):** Fires a single discrete incremental pulse (`CMD.jogStep=<axis>,<dir>`).
* **Continuous Jog (Hold `> 300ms`):** Transitions into continuous hold-to-run (`CMD.jogHold=<axis>,<dir>`) with high-frequency repeat until pointer release (`CMD.jogRelease=<axis>`).
* **Custom Keypad Entry (Long-Press `> 600ms`):** Long-pressing the central jog step selector opens the numeric keypad for entering non-standard step distances.

---

## 4. Visual Design System & Design Tokens

### 1. Typography & Zero-Jitter Numerics
* **Coordinate Readouts (DRO):** Locked to a static **`50px` monospace font** ([`JetBrains Mono`](https://fonts.google.com/specimen/JetBrains+Mono) / tabular lining figures).
  * Dynamic font auto-shrinking is strictly prohibited: shrinking fonts during axis motion causes digit jumping and cognitive distress.
  * Inline unit labels (`mm`/`in`) are omitted from the axis number line to ensure negative 7-figure coordinates (e.g. `-300.0000`) fit horizontally without wrapping or truncation.
* **UI Chrome & Labels:** Condensed industrial sans-serif (`Barlow Condensed` / `Inter Condensed`) with high vertical legibility.
* **Authentic Engineering Glyphs:** Diameter strictly renders the authentic uppercase Danish/European engineering symbol **`Ø`**, matching uppercase **`R`**.

### 2. Semantic Color Hierarchy (Four Non-Overlapping Tiers)

To prevent visual noise and ensure instant situational awareness from 3 meters away across the lathe:

```text
Tier 1: Machine Safety & State  ───► Green = Running | Red = Danger/Stop | Amber = Caution
Tier 2: Kinematic Axis Identity ───► Green = Z Carriage | Cyan = X Cross-Slide | Amber = Z+X Taper
Tier 3: Dimensional Mode        ───► Cyan = Ø Diameter | Amber = R Radius
Tier 4: General Affordance      ───► High-Contrast White = Numerics | Dim Gray = Units/Chrome
```

#### Tier 1: Machine Safety & Motion State
* **Vibrant Green (`#3ddc84`):** Affirmative runtime execution:
  - Solid Fill: Stationary universal `START FEED` / `START CYCLE` button.
  - Telemetry: Top bar `RUNNING` status dot, real-time hardware pulse indicator, communication health pills (`ENC ●`, `DRIVE ●`).
* **Vibrant Red (`#ff5a5f`):** Danger, emergency stop, and physical boundaries:
  - Solid Fill / Glowing Halo: Stationary universal `STOP` button during active motion.
  - Limit Contact: Jog arrows and chips that trip a software limit illuminate high-visibility red (`.limit-blocked`) with physical step lockout.
  - Telemetry: Top bar `STOPPED` status dot, modal `ABORT` buttons.
* **Warm Amber (`#ffb454`):** Supervisory caution and advisories:
  - Kinematic warnings: Reverse spindle warning (`⚠ Spindle: REVERSE (CW)`), skim pass prompts, soft-limit arrival glow.

#### Tier 2: Kinematic Axis Identification
* **$Z$ Axis (Longitudinal Carriage) $\rightarrow$ Emerald Green (`#3ddc84`, bg `#0d2116`, border `#1f4a2d`):**
  - Center Dynamic Deck Header Badge: `Z-AXIS`.
  - Direction Chips: Carriage feed direction (`← CHUCK (Z−)` / `→ TAIL (Z+)`).
  - Mode Card Framing: Z-axis leadscrew pitch card border and leadscrew action chips (`[ ✎ PITCH ]`).
* **$X$ Axis (Transverse Cross-Slide) $\rightarrow$ Cyan Blue (`#5aa9ff`, bg `#0c1c2a`, border `#234058`):**
  - Center Dynamic Deck Header Badge: `X-AXIS`.
  - Direction Chips: Cross-slide feed direction (`↑ TO CENTER (X−)` / `↓ RETRACT (X+)`).
  - Mode Card Framing: X-axis cross-feed blocks.
* **$Z+X$ Axis (Coordinated Cone / Taper) $\rightarrow$ Warm Amber (`#ffb454`, bg `#26190b`, border `#583e1c`):**
  - Center Dynamic Deck Header Badge: `Z+X TAPER`.
  - Mode Card Axis Toggle: Highlights amber when `AXIS: Z+X (TAPER)` is active.
  - Parameter Card Framing: Taper ratio card border and slope direction chips.

#### Tier 3: Dimensional Representation (DRO Column)
* **Diameter Mode (`Ø DIA`):** Styled in **Cyan Blue (`#5aa9ff`)** across the glyph `Ø` and badge, reinforcing that diameter is governed by the transverse X-axis.
* **Radius Mode (`R RAD`):** Styled in **Warm Amber (`#ffb454`)** across the glyph `R` and badge, alerting the operator that infeed cuts 1:1 on tool radius.

---

## 5. Universal ATM Keypad Architecture

Numeric entry utilizes a standardized ATM/Calculator modal overlay:

```text
┌───────────────────────────────┐
│ TARGET: [ Z POSITION (mm)   ] │
│ VALUE:  [ -125.450          ] │
├───────┬───────┬───────┬───────┤
│ [ 7 ] │ [ 8 ] │ [ 9 ] │ [CLR] │
├───────┼───────┼───────┼───────┤
│ [ 4 ] │ [ 5 ] │ [ 6 ] │ [ ⌫ ] │
├───────┼───────┼───────┼───────┤
│ [ 1 ] │ [ 2 ] │ [ 3 ] │ [ ÷2] │
├───────┼───────┼───────┼───────┤
│ [ ± ] │ [ 0 ] │ [ . ] │ [CURR]│
├───────┴───────┴───────┴───────┤
│   [ CANCEL ]     [ CONFIRM ]  │
└───────────────────────────────┘
```

### Keypad Rules & Behaviors:
1. **Pre-Filled & Pre-Selected Buffer:**
   * The keypad opens pre-populated with the target register's current value and **fully highlighted/selected**.
   * Typing any digit immediately overwrites the buffer without requiring the operator to tap `CLEAR`.
   * Tapping `CANCEL` dismisses the keypad and preserves the original value unmodified.
2. **`÷ 2` Half-Dimension Key:**
   * Immediately halves the current buffer value without opening a calculation sub-dialog.
   * Enables rapid translation of measured workpiece diameters into radius offsets and depth-of-cut values.
3. **Context-Gated `INSERT CURR` Key:**
   * Strictly gated: visible **only** when authoring axis coordinates (`Z`, `X`) or travel limits; hidden when entering pitch, pass counts, speeds, or ratios.
   * Copies the live physical machine coordinate into the input buffer.
4. **Sign Toggle (`±`):**
   * Prepend/toggles the negative sign dynamically without clearing the buffer.

---

## 6. Travel Limits & Vector Stop Indicator Standard

### 1. Distance-to-Boundary vs. Absolute Coordinates
In lathe operations, limit registers (`leftStop`, `rightStop`) represent physical chuck and shoulder boundaries. The DRO displays **distance remaining to the stop**, counting down toward `0.000`:
* At the apron, an operator does not want to subtract coordinate `-18.426` from limit `-24.269` at 800 RPM.
* Reading a countdown of **`5.843 mm`** approaching `0.000` is immediately actionable.
* When distance $\le 0.001\text{ mm}$, the limit chip illuminates amber (`.at-limit`).

### 2. Vector Arrow-Into-Wall Indicators
To remove ambiguity between carriage travel direction and boundary wall location, all four limit chips render dedicated vector SVG icons where the arrow points **in the direction of carriage travel, hitting the boundary wall bar at the tip**:

```text
Z Left (Chuck):     |←   Vertical wall on left; arrow points left toward chuck
Z Right (Tailstock): →|   Vertical wall on right; arrow points right toward tailstock
X Inward (Center):   ⤒    Horizontal ceiling bar; arrow points up toward spindle center
X Outward (Retract): ⭳    Horizontal floor bar; arrow points down toward operator
```

### 3. Anti-Trap Limit Contact & Asymmetric Jog Lockout
* When an axis reaches $0.000$ distance to boundary:
  * The tripping jog direction is visually styled in high-visibility red (`.limit-blocked`) and disabled from dispatching motion commands.
  * **Asymmetric Escape Guarantee:** The retreating jog direction remains fully enabled and visually prominent, guaranteeing the operator can immediately jog away from the boundary wall without alarms or reset procedures.

---

## 7. Universal Runstack Safety Contract

The bottom-right corner of the interface is universally reserved for motion initiation and termination:

```text
┌────────────────────────┐
│  [ START FEED ] (Green)│
├────────────────────────┤ <── 12px Permanent Air Gap
│  [ STOP ]       (Red)  │
└────────────────────────┘
```

### Operational Rules:
1. **Permanent Stationary STOP Anchor:**
   * The `STOP` button never moves, resizes, or unmounts regardless of active mode, wizard stage, or settings screen.
   * When idle: Rendered in subdued dark maroon (`#140d0e`, border `#3b1d20`).
   * When motion is active (`isOn == true`): Illuminates vibrant red (`#ff5a5f`) with an active pulsating glow.
2. **12px Vertical Air Gap:**
   * A generous 12px dead zone separates `START` from `STOP` to prevent fat-finger mis-activation when aiming for the stop button under stress.
3. **Autonomous Motion Defense-in-Depth:**
   * When `runState === 'running'`, all modal overlays (keypad, pitch selectors, axis setting dialogs) are unconditionally suppressed at the engine level.
   * Mode card toggles (`SYNC/ASYNC`, `AXIS Z/X`) are locked at $0.45$ opacity with `cursor: not-allowed`.
   * The live DRO column and `STOP` button remain 100% visible and unobstructed throughout all motion cycles.
