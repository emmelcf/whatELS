# Operational Mode: Electronic Gearbox (ELS & Power Feed)
## Kinematics, Physics, Canonical Protocol & Deck Interaction Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_GEARBOX` (Legacy Code `0` / Canonical Enum `ELSModeId::Gearbox`).
* **Required Machine Capabilities:**
  * Synchronized Mode: `CAP_SPINDLE_ENCODER`, plus either `CAP_Z_STEPPER` or `CAP_X_STEPPER`.
  * Asynchronous Mode: Either `CAP_Z_STEPPER` or `CAP_X_STEPPER` (operates independently of spindle).

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Sync Type** | `sync` | Boolean | enum | `SYNC (true)`, `ASYNC (false)` | `SYNC` | Spindle synchronized vs. time-based feed. |
| **Active Axis** | `axis` | Enum | char | `'Z'`, `'X'`, `'ZX'` | `'Z'` | Governed leadscrew or cross-slide motor. |
| **Feed Rate (Z)** | `zDupr` | Int32 | deci-microns/rev | $10 \dots 500{,}000$ | $1{,}000$ ($0.100\text{ mm/rev}$) | Canonical longitudinal feed. |
| **Feed Rate (X)** | `xDupr` | Int32 | deci-microns/rev | $10 \dots 250{,}000$ | $500$ ($0.050\text{ mm/rev}$) | Canonical transverse cross-feed. |
| **Async Speed (Z)**| `zSpeed` | Int32 | deci-microns/min | $1{,}000 \dots 100{,}000{,}000$ | $1{,}500{,}000$ ($150\text{ mm/min}$) | Time-based rapid/feed speed. |
| **Direction** | `dir` | Enum | semantic | `CHUCK` / `TAIL` ($Z$); `CENTER` / `RETRACT` ($X$) | `CHUCK` | Direction of active axis travel. |
| **TPI Intent** | `thPitch`| Float | threads/inch | $1.0 \dots 128.0$ | $20.0$ | Exact operator authoring intent when in TPI mode. |

### 3. Kinematic Formulas & Physical Execution

#### A. Spindle-Synchronized Feed (Electronic Leadscrew)
Synchronized feed slaves stepper motor steps directly to spindle encoder pulses:

$$\Delta\text{Steps}_{\text{motor}} = \Delta\text{Pulses}_{\text{encoder}} \times \frac{\text{dupr} \times \text{STEPS\_PER\_REV}_{\text{motor}}}{\text{ENCODER\_PPR} \times \text{LEADSCREW\_LEAD}_{\text{du}}}$$

Where:
* $\text{dupr}$ is deci-microns per revolution ($10{,}000\text{ dupr} = 1.000\text{ mm/rev}$).
* When operator authors in Threads Per Inch (TPI):
  $$\text{dupr} = \text{round}\left(\frac{254{,}000.0}{\text{TPI}}\right)$$
* Conversion between encoder pulses and motor steps is executed via Bresenham fractional step accumulation in high-priority Core 1 timer interrupts (`taskMoveZ` / `taskMoveX`).

#### B. Asynchronous Time-Based Feed
Stepper pulses are driven by a high-resolution FreeRTOS timer interrupt independent of spindle rotation:

$$\text{Frequency}_{\text{step}} = \frac{\text{Speed}_{\text{du/min}} \times \text{STEPS\_PER\_REV}_{\text{motor}}}{60.0 \times \text{LEADSCREW\_LEAD}_{\text{du}}}$$

### 4. Hardware Limit Envelopes & Collision Safety
* Motion automatically terminates when carriage position reaches the active limit register (`z_min` or `z_max` for Z; `x_min` or `x_max` for X).
* The motion engine halts step pulses immediately without disabling motor holding torque.
* When at limit ($0.000$ distance remaining), the tripping directional jog/feed is locked, but the reverse direction remains immediately operable to escape the limit.

### 5. Canonical Wire Protocol

* **Mode Activation:** `CMD.mode=GEARBOX`
* **Feed Setpoints:**
  * `CMD.feedDupr=Z,1000` (Sets Z feed to $0.100\text{ mm/rev}$)
  * `CMD.feedTpi=20` (Sets Z pitch to $20\text{ TPI}$ and updates intent register)
  * `CMD.feedMm=1.5` (Sets Z pitch to $1.500\text{ mm/rev}$)
  * `CMD.asyncSpeed=Z,1500000` (Sets Z async speed to $150\text{ mm/min}$)
* **Direction:** `CMD.feedDir=Z,CHUCK` / `CMD.feedDir=Z,TAIL`
* **Motion Execution:** `CMD.run=1` (Engage feed) / `CMD.run=0` (Disengage feed)

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology

```text
┌───────────────────────────┬───────────────────────────────┐
│ CENTER COLUMN: DYNAMIC    │ RIGHT COLUMN: MODE CARD       │
│ ACTION DECK (202px)       │ CONFIGURATION (260px)         │
├───────────────────────────┼───────────────────────────────┤
│ [ BADGE: Z-AXIS ] (Green) │ [ SYNC (SPINDLE) ][ ASYNC ]   │
│                           │ [ AXIS: Z ]       [ AXIS: X ] │
│ [ - ]  0.100 mm/rev  [ + ]│                               │
│ (Large Feed Nudge Buttons)│ [ Z CARRIAGE BLOCK ]          │
│                           │   0.100 mm/rev   [ ✎ PITCH ]  │
│ [ DIR: ← TO CHUCK (Z-) ]  │   [ FEED ] [ TPI ] (Inline)   │
│ (High-Visibility Direction│                               │
│                           │ [ X CROSS-SLIDE BLOCK ]       │
│ [ STEP: ±0.05 mm ][± CUST]│   0.050 mm/rev                │
│ (2-Button Step Progression│                               │
│                           │ [ UNIVERSAL RUNSTACK ]        │
│ [ APRON JOG D-PAD ]       │   [ START FEED ] (Green)      │
│                           │   [ STOP ]       (Perm Red)   │
└───────────────────────────┴───────────────────────────────┘
```

### 2. Dynamic Center Action Deck Components
1. **Axis Identification Header Badge:**
   * Shows `Z-AXIS` in vibrant green (`#3ddc84`), `X-AXIS` in cyan blue (`#5aa9ff`), or `Z+X TAPER` in amber (`#ffb454`).
2. **Primary Feed Trim Readout & Nudge Cluster:**
   * Center readout (`#deckReadout`): Warm amber monospace text showing current active feed rate. Tapping while stopped opens the calculator keypad for direct value entry.
   * Nudge Buttons (`[ − ]` and `[ + ]`): Generous $46\text{px} \times 46\text{px}$ touch targets. Decrements or increments feed rate by the active step increment. **Remains active and responsive while feeding under chip load.**
3. **Active Axis Direction Toggle (`#deckToggleDir`):**
   * Prominently displays motion vector relative to lathe bed:
     * $Z$ Axis: `DIR: ← TO CHUCK (Z−)` vs. `DIR: → TO TAIL (Z+)`
     * $X$ Axis: `DIR: ↑ TO CENTER (X−)` vs. `DIR: ↓ RETRACT (X+)`
   * Tapping while stopped flips direction instantly. Locked during motion to prevent mid-cut reversal.
4. **Two-Button Step Progression Layout:**
   * Generous 2-button layout ($96\text{px}$ width $\times 38\text{px}$ height each):
     * **`#deckStepCycle` (`STEP: ±0.05 mm`):** Cycles through curated standard progression:
       * Metric Feeds: `[0.010, 0.050, 0.100, 0.200 mm/rev]`
       * Imperial Feeds: `[0.0010, 0.0020, 0.0050, 0.0100 in/rev]`
       * TPI Steps: `[1, 2, 4, 8 TPI]`
       * Async Speeds: `[10, 25, 50, 100 mm/min]` / `[0.5, 1.0, 2.0, 5.0 in/min]`
     * **`#deckStepCust` (`± CUST STEP`):** Opens calculator keypad to define a non-standard step increment (e.g. $0.035\text{ mm}$). When active, the cycle button displays `STEP: ±0.035* mm`.

### 3. Mode Card Configuration Rules
1. **Zero Layout Shift (Static Height Architecture):**
   * The `[ FEED ] [ TPI ]` unit toggle is housed permanently inside the Z carriage block.
   * When $Z$ is inactive (operating in $X$ axis) or in `ASYNC` mode, the toggle row remains strictly rendered in place with dimmed styling (`opacity: 0.35; pointer-events: none;`). It is never unmounted, guaranteeing **zero layout shift**.
2. **Axis Card Dimming:**
   * The non-active axis block is dimmed (`opacity: 0.45`), clearly communicating which motor is armed without cluttering the screen with "INACTIVE" text badges.

### 4. Running Lockout & Safety State
* When feed is active (`isOn == true`):
  * Setup toggles (`SYNC/ASYNC`, `AXIS Z/X`, `FEED/TPI`) are locked (`opacity: 0.45; cursor: not-allowed;`).
  * Direction toggle (`#deckToggleDir`) is locked (`cursor: not-allowed;`).
  * Modal entry points (tapping feed readouts, limit chips, unit switches) are suppressed.
  * **Permitted live interventions:** `[ − ]` and `[ + ]` nudge buttons and in-place step increment cycling (`#deckStepCycle`) remain fully operational for live chip-load tuning, alongside the illuminated red `STOP` button.
