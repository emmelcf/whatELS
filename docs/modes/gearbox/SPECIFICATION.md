# Operational Mode: Electronic Gearbox (ELS & Power Feed)
## Kinematics, Physics, Canonical Protocol & Deck Interaction Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../state/ELS_CORE_STATE_MODEL.md`](../state/ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../clients/hmi/RICH_TOUCH_800X480.md`](../../clients/hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `ModeIds::GEARBOX` (`50`, slug: `"gearbox"`; legacy code `0`). Continuous coupled `ZX` taper feed executes within `ModeIds::GEARBOX (50)` (planned `#10` multi-pass taper canned cycle is `ModeIds::CONE (57)`).
* **Required Machine Capabilities:**
  * Synchronized Mode (`sync:1`): `CAP_SPINDLE_ENCODER`, plus `CAP_MOTOR_Z` and/or `CAP_MOTOR_X` (both required for `axis:ZX`).
  * Asynchronous Mode (`sync:0`): `CAP_MOTOR_Z` and/or `CAP_MOTOR_X` (operates independently of spindle).

### 2. Parameter Manifest (`CMD.startFeed`)

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Active Axis** | `axis` | Enum | string | `Z`, `X`, `ZX` | *Mandatory* | Governed leadscrew (`Z`), cross-slide (`X`), or coupled taper (`ZX`). |
| **Sync Type** | `sync` | Binary | `0\|1` | `1` (sync), `0` (async) | `1` | Spindle-synchronized (`1`) vs. time-based velocity (`0`). |
| **Direction** | `dir` | Enum/Sign | semantic | `-1`/`+1`, `CHUCK`/`TAIL` ($Z$), `CENTER`/`RETRACT` ($X$) | *Mandatory* | Primary axis Cartesian travel direction (`FWD`/`REV` forbidden). |
| **Feed Rate** | `feed` | Int32 | `dupr` or `du/min` | `> 0` | *Mandatory* | Positive magnitude: `dupr` when `sync:1`; `du/min` when `sync:0`. |
| **Distance** | `dist` | Int32 | `du` | `-1` or `> 0` | `-1` | Target travel distance in integer `du` (`-1` = continuous to soft limit). |
| **X Direction** | `xdir` | Enum/Sign | semantic | `-1`/`+1`, `CENTER`/`RETRACT`/`IN`/`OUT` | *Mandatory if `ZX`* | Secondary $X$ axis direction (mandatory for `axis:ZX`; rejected on `Z`/`X`). |
| **Radial Taper** | `taper` | Int32 | `du / 10000 du` | `> 0` | *Mandatory if `ZX`* | Radial $X$ travel in `du` per $10{,}000\text{ du}$ ($1.0\text{ mm}$) of $Z$ (mandatory for `axis:ZX`). |

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

### 5. Canonical Wire Protocol (`CANONICAL_COMMAND_REFERENCE.md`)

* **Engage Synchronized Z Feed:** `CMD.startFeed=axis:Z,feed:1500,dir:CHUCK,sync:1` ($0.150\text{ mm/rev}$ toward chuck, continuous `dist:-1`)
* **Engage Asynchronous X Feed (Finite Stroke):** `CMD.startFeed=axis:X,feed:500000,dir:CENTER,sync:0,dist:20000` ($50\text{ mm/min}$ toward centerline for $2.0\text{ mm}$)
* **Engage Coupled ZX Taper Feed:** `CMD.startFeed=axis:ZX,feed:10000,dir:CHUCK,xdir:RETRACT,taper:1000,sync:1` ($1.0\text{ mm/rev}$ $Z$, $0.1000$ radial slope)
* **Lifecycle Controls:** `CMD.stop` (halt and return to `IDLE`), `CMD.hold` (feed hold), `CMD.resume` (resume from hold)

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
