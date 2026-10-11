# Operational Mode: Joystick & MPG Pendant Control
## Proportional Velocity Curves, Hardware Handwheels & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_JOYSTICK` (Legacy Code `8` / Canonical Enum `ELSModeId::Joystick`).
* **Required Machine Capabilities:**
  * Either `CAP_ANALOG_JOYSTICK` (2-axis analog stick) or `CAP_MPG_WHEEL` (quadrature handwheel pendant).

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Deadzone** | `joyDeadzone` | UInt8 | percent (%) | $1 \dots 20\%$ | $5\%$ | Center neutral deadband rejecting jitter. |
| **Max Rapid Rate** | `joyMaxRate` | Int32 | deci-microns/min | $10{,}000 \dots 100{,}000{,}000$ | $8{,}000{,}000$ ($800\text{ mm/min}$) | Velocity ceiling at full stick deflection. |
| **MPG Multiplier** | `mpgScale` | Enum | scalar | `x1` (0.01mm), `x10` (0.1mm), `x100` (1.0mm) | `x10` | Hardware handwheel pulse multiplier. |
| **Velocity Curve** | `joyCurve` | Enum | int | `LINEAR (0)`, `CUBIC (1)`, `EXPONENTIAL (2)`| `CUBIC` | Deflection-to-velocity mapping curve. |

### 3. Kinematic Velocity Mapping Formulas

#### A. Cubic Proportional Response
A linear response feels jumpy and coarse at micro-jogging speeds. The controller applies a normalized cubic curve:

$$\text{deflection}_{\text{norm}} = \frac{|\text{ADC} - \text{Center}| - \text{Deadzone}}{\text{MaxDeflection} - \text{Deadzone}}$$

$$v(\text{deflection}) = \text{sgn}(\text{deflection}) \times \text{joyMaxRate} \times (\text{deflection}_{\text{norm}})^3$$

* **At 10% deflection:** Generates $\approx 0.1\%$ max speed for sub-micron manual tool touch-off.
* **At 100% deflection:** Reaches 100% max rapid speed for fast bed clearance.

#### B. Soft-Limit Proximity Braking
When jogging under joystick control toward an active limit register (`z_min`, `z_max`), the controller continuously calculates distance to boundary $d_{\text{stop}}$. If $d_{\text{stop}} < d_{\text{decel}}$, velocity is smoothly clamped:

$$v_{\text{clamped}} = \min\left(v_{\text{joystick}}, \sqrt{2 \times a_{\text{max}} \times d_{\text{stop}}}\right)$$

This guarantees the tool comes to a smooth halt precisely at the limit wall without harsh deceleration steps or motor stalling.

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology
* **Dynamic Center Deck:**
  * Top Badge: `JOYSTICK / PENDANT` (Blue).
  * Real-Time Vector Indicator: Deflection reticle rendering tool motion vector and live velocity in $mm/min$.
  * Max Speed Nudges: `[ − ]` and `[ + ]` nudge buttons to dial max rapid rate up or down on the fly.
* **Mode Card Setup:**
  * `MAX RAPID`: $800\text{ mm/min}$ default.
  * `DEADZONE`: $5\%$ default.
  * `MPG SCALE`: `[ ×1 ] [ ×10 ] [ ×100 ]` touch chips.
