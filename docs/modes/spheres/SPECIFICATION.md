# Operational Mode: Half-Spheres & Spherical Contouring
## Circular Interpolation, Convex/Concave Geometry & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../state/ELS_CORE_STATE_MODEL.md`](../state/ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../clients/hmi/RICH_TOUCH_800X480.md`](../../clients/hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_SPHERES` (Legacy Code `7` / Canonical Enum `ELSModeId::Spheres`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_Z_STEPPER`, `CAP_X_STEPPER`, `CAP_SPINDLE_ENCODER`.

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Sphere Radius** | `sphereRadius`| Int32 | deci-microns | $500 \dots 1{,}000{,}000$ | $120{,}000$ ($12.0\text{ mm}$) | Target ball or cup spherical radius $R$. |
| **Sphere Type** | `sphereType` | Enum | semantic | `CONVEX` (Ball), `CONCAVE` (Cup) | `CONVEX` | Outer ball vs. inner socket contour. |
| **Step Resolution**| `sphereStep` | Int32 | deci-microns | $10 \dots 5{,}000$ | $500$ ($0.050\text{ mm}$) | Step interpolation resolution. |
| **Cutting Feed** | `cutFeed` | Int32 | deci-microns/rev | $10 \dots 100{,}000$ | $500$ ($0.050\text{ mm/rev}$) | Synchronized contouring cutting feed. |

### 3. Kinematic Circular Interpolation Formulas

The tool tip traces a precise circular arc in the $Z$-$X$ coordinate plane governed by circle equation:

$$Z(u)^2 + X(u)^2 = R^2$$

#### A. Parametric Angle Interpolation
For angle parameter $\phi \in [0^\circ, 90^\circ]$:

$$Z(\phi) = Z_{\text{center}} - R \cos(\phi)$$
$$X(\phi) = X_{\text{center}} + R \sin(\phi)$$

* **Bresenham Circular Arc Stepping:** FreeRTOS motion generator coordinates $Z$ and $X$ step impulses dynamically, maintaining constant tool path velocity along the tangential cutting vector.
* **Insert Nose Radius Compensation:** The controller compensates for the physical insert radius $r_{\epsilon}$ so that the finished ball is mathematically spherical rather than flattened at the poles.

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology
* **Dynamic Center Deck:**
  * Top Badge: `HALF-SPHERES` (Amber).
  * Center Readout: Live radius tracking and tangential feed rate.
  * Contour Glyphs: Rendered with an SVG arc depicting convex ball (`◠`) vs. concave socket (`◡`).
* **Mode Card Setup:**
  * `RADIUS`: $12.000\text{ mm}$ default.
  * `TYPE`: Toggle between `[ CONVEX (BALL) ]` and `[ CONCAVE (CUP) ]`.
  * `STEP FINISH`: $0.050\text{ mm}$ default.
