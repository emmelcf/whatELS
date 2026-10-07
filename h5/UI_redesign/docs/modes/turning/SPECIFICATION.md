# Operational Mode: Turning (Multi-Pass Longitudinal Cycle)
## Kinematics, Dimension-First Limit Mapping, Pass Derivation & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_TURNING` (Legacy Code `3` / Canonical Enum `ELSModeId::Turning`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_Z_STEPPER`, `CAP_SPINDLE_ENCODER`.
  * Optional (for automated cross-slide infeed): `CAP_X_STEPPER` (manual infeed prompt supported if X is manual).

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Cut Feed** | `cutPitch` | Int32 | deci-microns/rev | $10 \dots 500{,}000$ | $800$ ($0.080\text{ mm/rev}$) | Synchronized longitudinal cutting feed. |
| **Shoulder Length**| `zLength` | Int32 | deci-microns | $100 \dots 10{,}000{,}000$ | $250{,}000$ ($25.0\text{ mm}$) | Axial cutting stroke along $Z$. |
| **Feed Direction** | `turnDir` | Enum | semantic | `CHUCK` ($Z-$), `TAIL` ($Z+$) | `CHUCK` | Carriage travel direction during cut. |
| **Total Stock** | `totalDepth`| Int32 | deci-microns (dia) | $10 \dots 500{,}000$ | $24{,}000$ ($2.400\text{ mm}$) | Total diameter reduction $\Delta X$. |
| **Max DOC** | `maxDoc` | Int32 | deci-microns (dia) | $10 \dots 100{,}000$ | $4{,}000$ ($0.400\text{ mm}$) | Depth of cut ceiling per pass. |
| **Pass Count** | `passes` | UInt16 | count | $1 \dots 99$ | $6$ | Total passes (two-way coupled with DOC). |
| **Retract Dist** | `retract` | Int32 | deci-microns (rad) | $100 \dots 50{,}000$ | $5{,}000$ ($0.500\text{ mm}$) | Radial clearance off OD for return stroke. |
| **Turning Type** | `turnType` | Enum | semantic | `EXTERNAL` (OD), `INTERNAL` (ID) | `EXTERNAL` | OD turning vs. boring bar operation. |
| **Restore Limits** | `restoreLimits`| Boolean | flag | `true`, `false` | `false` | Restore pre-existing envelope on completion. |

### 3. Kinematic Execution State Machine (FSM)

```text
       ┌───────────┐
       │   IDLE    │ ◄── Operator taps START CYCLE
       └─────┬─────┘
             │ Pre-run: Cache limits; assign Z_SHOULDER = Z_START - zLength
             ▼
       ┌───────────┐
┌────► │  INFEED   │ Tool steps X to pass depth: X_pass = X_start - (n * DOC)
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │ CUT_PASS  │ Synchronized Z feed at cutPitch until Z reaches Z_SHOULDER
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │  RETRACT  │ Tool pulls away radially: X_retract = X_pass + retract
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │RAPID_RET  │ Rapid carriage return along Z to Z_START
│      └─────┬─────┘
│            │
└─────── [n < passes] ───► [n == passes]
                               │
                               ▼
                         ┌───────────┐
                         │ COMPLETED │ Reinstate cached limits (if opt-in)
                         └───────────┘
```

### 4. Two-Way Pass Derivation Macro Bridge
NanoELS core firmware executes discrete passes based on an integer `turnPasses`. Machinists think in maximum tool chip load (`MAX DOC`) and total stock to remove (`TOTAL DEPTH`). The system implements a **two-way coupled macro bridge**:

1. **When Operator Modifies `MAX DOC` or `TOTAL DEPTH`:**
   $$\text{turnPasses} = \left\lceil \frac{\text{totalDepth}}{\text{maxDoc}} \right\rceil$$
   $$\text{actualDoc} = \frac{\text{totalDepth}}{\text{turnPasses}}$$
2. **When Operator Taps `PASSES` to Override:**
   $$\text{actualDoc} = \frac{\text{totalDepth}}{\text{turnPasses}}$$
   `maxDoc` is updated to reflect the new per-pass chip load, preventing rounding artifacts.

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology

```text
┌───────────────────────────┬───────────────────────────────┐
│ CENTER COLUMN: DYNAMIC    │ RIGHT COLUMN: MODE CARD       │
│ ACTION DECK (202px)       │ CONFIGURATION (260px)         │
├───────────────────────────┼───────────────────────────────┤
│ [ BADGE: TURNING ]        │ [ OD EXTERNAL ] [ ID BORING ] │
│                           │                               │
│ [ PASS 03 / 06 ]          │ [ DIMENSION PARAMETERS ]      │
│   DOC: 0.400 mm / pass    │   LENGTH:      25.000 mm      │
│                           │   TOTAL DEPTH: 2.400 mm (dia) │
│ [ - ]  0.080 mm/rev  [ + ]│   MAX DOC:     0.400 mm       │
│ (Live Cut Feed Override)  │   PASSES:      6 [PASSES]     │
│                           │                               │
│ [ DIR: ← TO CHUCK (Z-) ]  │ [ OPTIONS ]                   │
│                           │   RETRACT:     0.500 mm       │
│ [ STEP: ±0.05 mm ][± CUST]│   RESTORE LIM: [ YES ] / [NO] │
│ (Feed Increment Cycling)  │                               │
│                           │ [ UNIVERSAL RUNSTACK ]        │
│ [ APRON JOG D-PAD ]       │   [ START CYCLE ] (Green)     │
│                           │   [ STOP ]        (Perm Red)  │
└───────────────────────────┴───────────────────────────────┘
```

### 2. Operational Ergonomics
1. **Dimension-First Entry:** Tapping `LENGTH` or `TOTAL DEPTH` opens the calculator keypad. Entering `25.0 mm` shoulder sets the hardware limit relative to current touch-off automatically.
2. **Live Feed Trimming During Pass:** While cutting, the `[ − ]` and `[ + ]` nudge buttons in the dynamic deck remain active, allowing immediate feed rate adjustment without halting the spindle or breaking the carbide insert.
3. **Running Telemetry & Lockout:** During active execution, all dimension setup fields are locked. The center deck displays live progress (`PASS 03 / 06`), and the permanent red `STOP` button is the sole exit path.
