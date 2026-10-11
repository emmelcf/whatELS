# Operational Mode: Facing (Multi-Pass Transverse Cycle)
## Kinematics, Center Crossover, Axial DOC Derivation & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_FACING` (Legacy Code `4` / Canonical Enum `ELSModeId::Facing`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_X_STEPPER`, `CAP_SPINDLE_ENCODER`.
  * Optional (for automated axial DOC infeed): `CAP_Z_STEPPER` (manual carriage infeed prompt supported if Z is manual).

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Facing Feed** | `facePitch` | Int32 | deci-microns/rev | $10 \dots 250{,}000$ | $500$ ($0.050\text{ mm/rev}$) | Synchronized transverse cross-feed. |
| **Facing Stroke** | `faceStroke` | Int32 | deci-microns | $100 \dots 5{,}000{,}000$ | $140{,}000$ ($14.0\text{ mm}$) | Radial cross-slide cutting travel. |
| **Feed Direction** | `faceDir` | Enum | semantic | `CENTER` ($X-$), `RETRACT` ($X+$) | `CENTER` | Cross-slide travel direction during cut. |
| **Total Stock (Z)**| `totalDepth`| Int32 | deci-microns | $10 \dots 100{,}000$ | $10{,}000$ ($1.000\text{ mm}$) | Total axial face cleanup stock $\Delta Z$. |
| **Max Axial DOC** | `maxDoc` | Int32 | deci-microns | $10 \dots 50{,}000$ | $2{,}500$ ($0.250\text{ mm}$) | Axial depth of cut ceiling per pass. |
| **Pass Count** | `passes` | UInt16 | count | $1 \dots 99$ | $4$ | Total passes (two-way coupled with DOC). |
| **Retract Dist (Z)**| `retract` | Int32 | deci-microns | $100 \dots 50{,}000$ | $5{,}000$ ($0.500\text{ mm}$) | Axial clearance off face for rapid return. |
| **Restore Limits** | `restoreLimits`| Boolean | flag | `true`, `false` | `false` | Restore pre-existing envelope on completion. |

### 3. Centerline Crossover & Parting Nib Prevention
In manual lathe facing, feeding tool tip strictly to $X = 0.000$ frequently leaves an unsightly un-cut pip or nib at the center due to insert nose radius and slight tool height misalignment:
* **Over-Center Clearance ($X_{\text{nib}}$):** The facing cycle automatically extends cross-slide travel slightly past spindle center (typically by $-0.3\text{ mm}$ to $-0.5\text{ mm}$ radial equivalent):
  $$X_{\text{STOP}} = -\text{toolNoseRadius}$$
* This guarantees the face is completely clean, flat, and square across the entire center axis without requiring the operator to manually jog past zero.

### 4. Kinematic Execution State Machine (FSM)

```text
       ┌───────────┐
       │   IDLE    │ ◄── Operator taps START CYCLE
       └─────┬─────┘
             │ Pre-run: Cache limits; assign X_INNER_STOP = -nibClearance
             ▼
       ┌───────────┐
┌────► │AXIAL_INFEED│ Tool advances Z into face: Z_pass = Z_start - (n * DOC)
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │ FACE_PASS │ Synchronized X feed at facePitch toward spindle centerline
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │ Z_RETRACT │ Carriage backs away axially off face: Z_retract = Z_pass + retract
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │RAPID_RET_X│ Rapid cross-slide return outward to X_START OD
│      └─────┬─────┘
│            │
└─────── [n < passes] ───► [n == passes]
                               │
                               ▼
                         ┌───────────┐
                         │ COMPLETED │ Reinstate cached limits (if opt-in)
                         └───────────┘
```

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology

```text
┌───────────────────────────┬───────────────────────────────┐
│ CENTER COLUMN: DYNAMIC    │ RIGHT COLUMN: MODE CARD       │
│ ACTION DECK (202px)       │ CONFIGURATION (260px)         │
├───────────────────────────┼───────────────────────────────┤
│ [ BADGE: FACING ] (Blue)  │ [ FACING PARAMETERS ]         │
│                           │   STROKE:      14.000 mm      │
│ [ PASS 02 / 04 ]          │   TOTAL DEPTH: 1.000 mm (Z)   │
│   DOC: 0.250 mm / pass    │   MAX DOC:     0.250 mm       │
│                           │   PASSES:      4 [PASSES]     │
│ [ - ]  0.050 mm/rev  [ + ]│                               │
│ (Live Cross-Feed Override)│ [ OPTIONS ]                   │
│                           │   RETRACT:     0.500 mm       │
│ [ DIR: ↑ TO CENTER (X-) ] │   RESTORE LIM: [ YES ] / [NO] │
│                           │                               │
│ [ STEP: ±0.01 mm ][± CUST]│ [ UNIVERSAL RUNSTACK ]        │
│                           │   [ START CYCLE ] (Green)     │
│ [ APRON JOG D-PAD ]       │   [ STOP ]        (Perm Red)  │
└───────────────────────────┴───────────────────────────────┘
```

### 2. Operational Ergonomics
1. **Dynamic Deck Feed Nudges:** `[ − ]` and `[ + ]` nudge buttons allow active trimming of the synchronized cross-feed rate while facing under load, especially critical when approaching the center where surface speed drops to zero.
2. **Dimension-First Stroke:** Entering part radius or OD into `STROKE` automatically configures the travel limit relative to the outer edge touch-off.
3. **Safety Lockout:** Parameter inputs are suppressed while facing. The red `STOP` button terminates cross-slide motion immediately.
4. **Coupled Parameter Visual Affordance (Right Bracket):** Similar to Turning, `MAX DOC` and `PASSES` are enclosed in an engineering right bracket (`]`) container to clarify two-way pass/DOC recalculation.

