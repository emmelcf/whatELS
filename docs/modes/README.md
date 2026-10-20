# whatELS — Operational Mode Catalog & Specifications
## Architecture, Kinematics, Execution FSMs & Deck Interaction Specifications

> **Companion Architecture Documents:**
> * For the general mode interface (`IELSMode`), execution FSM, composite cycles, and replacement vs. variant rules: see [`./ELS_MODE_ARCHITECTURE.md`](./ELS_MODE_ARCHITECTURE.md).
> * For the central state model (`MachineModel`), invariants, and properties taxonomy: see [`../state/ELS_CORE_STATE_MODEL.md`](../state/ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile (typography, layout, keypad, runstack safety): see [`../clients/hmi/RICH_TOUCH_800X480.md`](../clients/hmi/RICH_TOUCH_800X480.md).

---

## 1. Specification Structure per Mode

To cleanly decouple physical motion logic from presentation while ensuring high developer cohesion, each mode specification in this directory adheres to a standard two-part blueprint:

### Part 1: Kinematic & State Specification (Controller & Firmware Domain)
* **Required Machine Capabilities:** Hardware bitmask prerequisites (e.g. `CAP_SPINDLE_ENCODER`, `CAP_X_STEPPER`).
* **Parameter Manifest:** Authoritative typed parameters, units (canonical deci-microns `du`, `dupr`), defaults, and validation bounds.
* **Kinematics & Physics:** Mathematical formulas relating spindle pulse counts, encoder quadrature, leadscrew pitch, and cross-slide ratios.
* **Execution State Machine (FSM):** Internal sub-states (`IDLE`, `FEEDING`, `RETRACTING`, `COMPLETED`, `ABORTED`).
* **Canonical Wire Protocol:** Emitted telemetry domain keys (`K.*`, `I.*`, `C.*`) and controlling commands (`CMD.*`).
* **Hardware Limit & Collision Rules:** Limit register interactions, boundary stops, and opt-in restoration behavior.

### Part 2: Deck Presentation & Touch Interaction (HMI Profile Domain)
* **Dynamic Center Deck Controls:** Mode-active buttons, nudge controls, direction chips, and live telemetry readouts.
* **Mode Card Setup Configuration:** Right-column parameter fields, toggle switches, and preset menus.
* **Modal Wizards (if applicable):** Multi-stage touch workflows guiding the operator through parameter setup.
* **Active Execution Lockdown:** UI elements dimmed, stripped of touch handlers, or locked during active motion.

---

## 2. Mode Catalog

| Mode Name | Specification Document | Category | Motion Type | Primary Axis | Key Machine Capabilities Required |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Electronic Gearbox** | [`gearbox/SPECIFICATION.md`](./gearbox/SPECIFICATION.md) | Continuous Geared | Spindle-Synchronized or Time Async | $Z$ or $X$ | `CAP_SPINDLE_ENCODER`, `CAP_MOTOR_Z` |
| **Threading** | [`threading/SPECIFICATION.md`](./threading/SPECIFICATION.md)<br>• [`WIZARD_WORKFLOW.md`](./threading/WIZARD_WORKFLOW.md) | Multi-Pass Cycle | Rigid Spindle Sync (Modulo Phase Lock) | $Z$ (sync) + $X$ (infeed) | `CAP_SPINDLE_ENCODER`, `CAP_MOTOR_Z` |
| **Turning** | [`turning/SPECIFICATION.md`](./turning/SPECIFICATION.md) | Multi-Pass Cycle | Canned Longitudinal Turning Cycle | $Z$ (cut) + $X$ (DOC) | `CAP_MOTOR_Z`, `CAP_MOTOR_X` (or manual $X$) |
| **Facing** | [`facing/SPECIFICATION.md`](./facing/SPECIFICATION.md) | Multi-Pass Cycle | Canned Transverse Facing Cycle | $X$ (cut) + $Z$ (DOC) | `CAP_MOTOR_X`, `CAP_MOTOR_Z` (or manual $Z$) |
| **Taper & Chamfer** | [`taper/SPECIFICATION.md`](./taper/SPECIFICATION.md) | Coordinated Angled | Dual-Axis Synchronized Continuous / Multi-Pass | $Z + X$ Coordinated | `CAP_MOTOR_Z`, `CAP_MOTOR_X` |
| **Cutoff / Parting** | [`cutoff/SPECIFICATION.md`](./cutoff/SPECIFICATION.md) | Specialized Cycle | Controlled Transverse Plunge | $X$ Cross-Slide | `CAP_MOTOR_X` |
| **Slotting / Keyway** | [`slotting/SPECIFICATION.md`](./slotting/SPECIFICATION.md) | Reciprocating Cycle | Linear Broaching Strokes | $Z$ Carriage | `CAP_MOTOR_Z`, `CAP_SPINDLE_INDEX` |
| **Half-Spheres** | [`spheres/SPECIFICATION.md`](./spheres/SPECIFICATION.md) | Contouring Cycle | Coordinated Circular Interpolation | $Z + X$ Coordinated | `CAP_MOTOR_Z`, `CAP_MOTOR_X` |
| **Joystick / Pendant** | [`joystick/SPECIFICATION.md`](./joystick/SPECIFICATION.md) | Manual Rapid/Feed | Deflection-Proportional Velocity | $Z$ and/or $X$ | `CAP_JOYSTICK` or `CAP_MPG_Z` / `CAP_MPG_X` |

