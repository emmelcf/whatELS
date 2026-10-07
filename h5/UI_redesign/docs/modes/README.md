# NanoELS H5 — Operational Mode Catalog & Specifications
## Architecture, Kinematics, Execution FSMs & Deck Interaction Specifications

> **Companion Architecture Documents:**
> * For the general mode interface (`IELSMode`), execution FSM, composite cycles, and replacement vs. variant rules: see [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md).
> * For the central state model (`MachineModel`), invariants, and properties taxonomy: see [`../ELS_CORE_STATE_MODEL.md`](../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile (typography, layout, keypad, runstack safety): see [`../hmi/RICH_TOUCH_800X480.md`](../hmi/RICH_TOUCH_800X480.md).

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

| Mode Name | Directory | Category | Motion Type | Primary Axis | Key Machine Capabilities Required |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Electronic Gearbox** | [`gearbox/`](./gearbox/) | Continuous Geared | Spindle-Synchronized or Time Async | $Z$ or $X$ | `CAP_SPINDLE_ENCODER`, `CAP_Z_STEPPER` |
| **Threading** | [`threading/`](./threading/) | Multi-Pass Cycle | Rigid Spindle Sync (Modulo Phase Lock) | $Z$ (sync) + $X$ (infeed) | `CAP_SPINDLE_ENCODER`, `CAP_Z_STEPPER` |
| **Turning** | [`turning/`](./turning/) | Multi-Pass Cycle | Canned Longitudinal Turning Cycle | $Z$ (cut) + $X$ (DOC) | `CAP_Z_STEPPER`, `CAP_X_STEPPER` (or manual $X$) |
| **Facing** | [`facing/`](./facing/) | Multi-Pass Cycle | Canned Transverse Facing Cycle | $X$ (cut) + $Z$ (DOC) | `CAP_X_STEPPER`, `CAP_Z_STEPPER` (or manual $Z$) |
| **Taper & Chamfer** | [`taper/`](./taper/) | Coordinated Angled | Dual-Axis Synchronized Continuous / Multi-Pass | $Z + X$ Coordinated | `CAP_Z_STEPPER`, `CAP_X_STEPPER` |
| **Cutoff / Parting** | [`cutoff/`](./cutoff/) | Specialized Cycle | Controlled Transverse Plunge | $X$ Cross-Slide | `CAP_X_STEPPER` |
| **Slotting / Keyway** | [`slotting/`](./slotting/) | Reciprocating Cycle | Linear Broaching Strokes | $Z$ Carriage | `CAP_Z_STEPPER`, `CAP_SPINDLE_INDEX` |
| **Half-Spheres** | [`spheres/`](./spheres/) | Contouring Cycle | Coordinated Circular Interpolation | $Z + X$ Coordinated | `CAP_Z_STEPPER`, `CAP_X_STEPPER` |
| **Joystick / Pendant** | [`joystick/`](./joystick/) | Manual Rapid/Feed | Deflection-Proportional Velocity | $Z$ and/or $X$ | `CAP_ANALOG_JOYSTICK` or `CAP_MPG_WHEEL` |
