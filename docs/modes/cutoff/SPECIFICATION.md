# Operational Mode: Cutoff & Parting
## Kinematics, Peck Parting Schedules, Chip Clearance & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../state/ELS_CORE_STATE_MODEL.md`](../state/ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../clients/hmi/RICH_TOUCH_800X480.md`](../../clients/hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_CUTOFF` (Legacy Code `5` / Canonical Enum `ELSModeId::Cutoff`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_X_STEPPER`, `CAP_SPINDLE_ENCODER`.

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Plunge Feed** | `cutoffFeed` | Int32 | deci-microns/rev | $5 \dots 100{,}000$ | $300$ ($0.030\text{ mm/rev}$) | Synchronized radial cross-feed. |
| **Peck Depth** | `cutoffPeck` | Int32 | deci-microns | $100 \dots 50{,}000$ | $12{,}000$ ($1.200\text{ mm}$) | Infeed depth before chip break retract. |
| **Retract Dist** | `cutoffRetract`| Int32 | deci-microns | $100 \dots 20{,}000$ | $5{,}000$ ($0.500\text{ mm}$) | Radial retract clearance for chip clearance. |
| **Final Diameter** | `finalDia` | Int32 | deci-microns | $-10{,}000 \dots 100{,}000$ | $-5{,}000$ ($-0.500\text{ mm}$) | Bottom-of-cut cutoff depth (past center). |

### 3. Kinematics & Peck Parting Cycle Mechanics
Parting off a workpiece on a manual lathe is one of the most chatter-prone operations. Long stringy chips jam inside the narrow kerf, causing blade deflection and tool breakage. The automated peck parting cycle enforces periodic chip breaking:

```text
       ┌───────────┐
       │   IDLE    │ ◄── Operator taps START CYCLE
       └─────┬─────┘
             ▼
       ┌───────────┐
┌────► │PLUNGE_FEED│ Synchronized X- feed at cutoffFeed for distance cutoffPeck
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │CHIP_BREAK │ Rapid X+ retract by cutoffRetract (breaks chip, admits oil)
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │RAPID_RE-ENTRY│ Rapid X- return to (previous depth - 0.1mm clearance)
│      └─────┬─────┘
│            │
└─────── [X > finalDia] ───► [X <= finalDia]
                               │
                               ▼
                         ┌───────────┐
                         │FULL_RETRACT│ Rapid X+ retract out of kerf to start OD
                         └─────┬─────┘
                               ▼
                         ┌───────────┐
                         │ COMPLETED │ Spindle stop or feed disengaged
                         └───────────┘
```

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology
* **Tool Orientation Icon:** The Cutoff icon arrow points **away from the operator into the cut** (`↑` / `X-`), matching physical parting blade plunge trajectory.
* **Dynamic Center Deck:**
  * Top Badge: `CUTOFF / PARTING` (Cyan).
  * Center Readout: Live plunge depth and radial distance remaining to center.
  * Feed Trims: `[ − ]` and `[ + ]` nudge buttons for reducing feed rate as the cut nears center.
* **Mode Card Setup:**
  * `PLUNGE FEED`: $0.030\text{ mm/rev}$ default (fine feed to prevent blade stall).
  * `PECK DEPTH`: $1.200\text{ mm}$ default.
  * `RETRACT`: $0.500\text{ mm}$ default.
  * `FINAL DIA`: $-0.500\text{ mm}$ (guarantees part drops cleanly off stock).
