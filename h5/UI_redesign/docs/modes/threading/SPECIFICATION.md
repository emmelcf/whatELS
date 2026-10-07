# Operational Mode: Threading
## Kinematics, Modulo Phase Tracking, Parameter Manifest & Deck Interaction Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).
> * For the multi-stage touch wizard workflow: see [`WIZARD_WORKFLOW.md`](./WIZARD_WORKFLOW.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_THREADING` (Legacy Code `2` / Canonical Enum `ELSModeId::Threading`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_SPINDLE_ENCODER`, `CAP_Z_STEPPER`.
  * Optional (for automated cross-slide infeed): `CAP_X_STEPPER`.

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Thread Format** | `thFmt` | Enum | int | `METRIC (0)`, `TPI (1)`, `INCH (2)` | `METRIC` | Authoring unit convention. |
| **Pitch / Lead** | `thPitch` | Float | mm or TPI | $0.2 \dots 12.0\text{ mm}$ / $2 \dots 128\text{ TPI}$ | $1.500$ | Authoritative thread lead. |
| **Thread Starts** | `starts` | UInt8 | count | $1 \dots 12$ | $1$ | Multi-start thread count ($360^\circ / \text{starts}$ index). |
| **Thread Length** | `zLength` | Int32 | deci-microns | $100 \dots 10{,}000{,}000$ | $250{,}000$ ($25\text{ mm}$) | Axial thread cutting stroke. |
| **Total Cut Depth**| `depth` | Int32 | deci-microns | $10 \dots 100{,}000$ | $9{,}200$ ($0.920\text{ mm}$) | Radial thread crest-to-root depth. |
| **Pass Count** | `passes` | UInt16 | count | $1 \dots 99$ | $8$ | Total number of passes in cycle. |
| **Spring Passes** | `spring` | UInt8 | count | $0 \dots 5$ | $1$ | Zero-infeed finish clean-up passes. |
| **Infeed Strategy**| `infeed` | Enum | int | `RADIAL (0)`, `FLANK_29.5 (1)`, `MOD_FLANK (2)` | `FLANK_29.5` | Tool plunge trajectory angle. |

### 3. Kinematic Formulas & Rigid Spindle Phase Synchronization

#### A. Rigid Spindle Phase Modulo Lock
Unlike continuous electronic gearing where carriage motion starts the instant the feed button is pressed, threading requires **absolute positional repeatability**: the cutting tool must enter the exact same helical thread groove on every pass, regardless of spindle speed variations:

$$\text{spindlePosSync} = \text{spindlePos} \pmod{\text{ENCODER\_PPR}}$$

1. **Entry Synchronization:**
   * When `START CYCLE` is armed, the carriage waits stationary at the rapid clearance start point until `spindlePos` crosses the synchronization datum $\theta_0 = 0^\circ$ (or $\theta_k = k \times \frac{360^\circ}{\text{starts}}$ for multi-start pass $k$).
   * Stepper acceleration ramp engages smoothly, reaching synchronized velocity before the tool tip enters the workpiece stock.
2. **Helical Pitch Invariant:**
   * Longitudinal motion maintains rigid phase lock:

   $$Z_{\text{pos}}(t) = Z_{\text{start}} - \left(\frac{\text{spindlePos}(t) - \text{spindlePos}_0}{\text{ENCODER\_PPR}}\right) \times \text{Pitch}_{\text{du}}$$

#### B. Depth Progression & Infeed Strategies
Thread cutting force increases dramatically as tool contact area grows. Depth per pass diminishes progressively:

$$\Delta X_n = \text{TotalDepth} \times \sqrt{\frac{n}{\text{passes}}}$$

* **Radial Infeed (Plunge):** Tool feeds straight into the cut along $X$. Generates heavy V-shaped chips on both flanks.
* **Modified Flank Infeed ($29.5^\circ$):** Tool infeeds at $29.5^\circ$ (for $60^\circ$ threads), cutting primarily with the leading edge and leaving $0.5^\circ$ on the trailing flank to prevent rubbing.

#### C. Benchtop Lathe Cutting Force Optimization
* **Base Divisor ($0.10\text{ mm}$):** Standard industrial tooling catalogs suggest ~0.12 mm average DOC. On small benchtop lathes (Mini Lathes, Precision Matthews, Sieg) with 300–750W motors, the initial quadratic bite can induce belt slip or motor bogging. Shifting the base divisor to $0.10\text{ mm}$ (e.g. 11 passes on M12 instead of 9) drops peak cutting torque by ~25%, yielding cleaner threads and preventing motor stalls.
* **Internal Threading Offset (+2 Passes):** Internal single-point threading uses slender boring bars prone to cantilever deflection and chatter. The cycle automatically schedules **+2 passes** when internal boring is selected.

#### D. Directional Independence & Blind Hole Safety (Inside-Out Cutting)
* Thread Handedness (Right-Hand vs. Left-Hand) is fundamentally distinct from Cut Direction (Carriage feed vector: Toward Chuck vs. Toward Tailstock).
* **Blind Hole Collision Risk:** Cutting blind internal threads toward the chuck carries catastrophic risk of bottoming out the boring bar against the hole shoulder.
* **Inside-Out Safe Vector:** The operator starts the tool inside the relief groove at the bottom of the hole and feeds outward toward the tailstock into open clearance (`→ TO TAIL / Z+`).
* **Spindle Kinematics:** To cut an RH thread moving toward the tailstock, the spindle rotates in Reverse (CW) to maintain the right-hand helical lead angle.

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology

```text
┌───────────────────────────┬───────────────────────────────┐
│ CENTER COLUMN: DYNAMIC    │ RIGHT COLUMN: MODE CARD       │
│ ACTION DECK (202px)       │ CONFIGURATION (260px)         │
├───────────────────────────┼───────────────────────────────┤
│ [ BADGE: THREADING ]      │ [ METRIC ] [ IMPERIAL (TPI) ] │
│                           │                               │
│ [ 1.500 mm (16.93 TPI) ]  │ [ THREAD PRESETS & WIZARD ]   │
│ (Dual Companion Readout)  │   M10 × 1.5 ISO COARSE        │
│                           │   [ ✎ OPEN WIZARD ]           │
│ [ TRIM LEAD - ][TRIM LEAD+]│                               │
│ (Stopped: Micro Pitch)    │ [ CYCLE PARAMETERS ]          │
│                           │   LENGTH:   25.000 mm         │
│                           │   DEPTH:    0.920 mm (8 pass) │
│                           │   STARTS:   1 [SINGLE]        │
│                           │                               │
│ [ APRON JOG D-PAD ]       │ [ UNIVERSAL RUNSTACK ]        │
│                           │   [ START CYCLE ] (Green)     │
│                           │   [ STOP ]        (Perm Red)  │
└───────────────────────────┴───────────────────────────────┘
```

### 2. Stopped vs. Running State Transformation

#### A. Stopped State (Authoring & Setup)
* **Dual Companion Pitch Readout:** Primary pitch is displayed prominently with its companion conversion in parentheses (e.g. `1.500 mm (16.93 TPI)` or `20 TPI (1.270 mm)`), completely eliminating manual conversion charts.
* **Lead Trim Buttons:** While stopped, `[ TRIM LEAD − ]` and `[ TRIM LEAD + ]` allow fine adjustment of thread lead by discrete increments ($0.001\text{ mm}$ or $0.1\text{ TPI}$).
* **Preset Wizard Trigger:** Prominent `[ ✎ OPEN WIZARD ]` button launches the 4-stage Threading Wizard overlay.

#### B. Running State (Locked Autonomous Cycle Execution)
During active multi-pass threading, the motion core autonomously coordinates Z feeds, synchronized spindle tracking, rapid retracts, and infeed increments:

```text
┌───────────────────────────────────────┐
│ [ CYCLE ACTIVE — AUTO THREADING ]     │
│                                       │
│  PASS: 03 / 08       (Roughing)       │
│  CURRENT DEPTH:      -0.540 mm        │
│  TARGET DEPTH:       -0.920 mm        │
│  SPINDLE SYNC:       LOCKED (100%)    │
│                                       │
│ [ ALL PARAMETER EDITING LOCKED OUT ]  │
└───────────────────────────────────────┘
```

* **Complete Affordance Lockout:**
  - All lead trim and nudge buttons are completely removed from the center deck.
  - Tapping pitch readouts, limits, or mode toggles performs a silent no-op.
  - The live DRO column and digital twin visualizer continuously report real-time tool tracking.
* **Emergency Intervention:** The permanently anchored red **`STOP`** button is the sole interactive touch target, immediately terminating leadscrew feed and disengaging synchronization.
