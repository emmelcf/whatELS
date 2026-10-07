# Operational Mode: Taper & Chamfer (Coordinated Dual-Axis Motion)
## Kinematics, Slope Ratios, Inverted Profiles, Presets & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../../ELS_MODE_ARCHITECTURE.md`](../../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../../ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_CONE` (Legacy Code `100` / Canonical Enum `ELSModeId::Taper` & `ELSModeId::GearboxZX`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_Z_STEPPER`, `CAP_X_STEPPER`, `CAP_SPINDLE_ENCODER`.

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Z Primary Feed**| `zDupr` | Int32 | deci-microns/rev | $10 \dots 500{,}000$ | $1{,}000$ ($0.100\text{ mm/rev}$) | Primary carriage feed rate. |
| **Taper Ratio** | `coneRatio`| Float | scalar ($\Delta\text{Dia}/\Delta Z$) | $0.0001 \dots 10.0$ | $0.04995$ (MT2) | Physical slope ratio $C$. |
| **Authoring Mode** | `tpMode` | Enum | int | `RATIO (0)`, `ANGLE (1)`, `PRESET (2)` | `PRESET` | Exact operator intent mode. |
| **Authoring Scalar**| `tpVal` | Float | scalar | $0.001 \dots 1000.0$ | $19.25$ | User-typed ratio or angle degrees. |
| **Preset Index** | `tpPreset` | Int16 | ID | $-1$ (Custom) or $\ge 0$ | $1$ (MT2) | Selected standard preset ID. |
| **Slope Direction**| `invert` | Boolean | flag | `NORMAL (false)`, `INVERT (true)` | `NORMAL` | Normal (wide at chuck) vs. Inverted. |
| **Surface Type** | `surface` | Enum | semantic | `EXTERNAL` (OD), `INTERNAL` (ID) | `EXTERNAL` | OD turning vs. internal taper boring. |
| **Taper Length** | `zLength` | Int32 | deci-microns | $100 \dots 10{,}000{,}000$ | $250{,}000$ ($25.0\text{ mm}$) | Axial taper cut length along $Z$. |

### 3. Kinematic Formulas & Dual-Axis Slaving

A lathe taper is fundamentally defined by its diametral slope ratio $C$:

$$C = \frac{\Delta\text{Dia}}{\Delta Z} = 2 \tan(\theta)$$

Where $\theta$ is the half-angle off the spindle centerline.

#### Coordinated Cross-Slide Slaving
During synchronized spindle rotation, the longitudinal carriage $Z$ advances at $\text{dupr}_Z$. The transverse cross-slide $X$ is slaved proportionally:

$$\text{dupr}_X = \frac{C}{2.0} \times \text{dupr}_Z = \tan(\theta) \times \text{dupr}_Z$$

#### Direction & Inversion Sign Logic
Cross-slide motion direction is automatically resolved based on feed direction, surface type, and cone profile:

| Profile | Surface | $Z$ Feed Direction | $X$ Cross-Slide Motion | Physical Effect |
| :--- | :--- | :--- | :--- | :--- |
| **NORMAL** (Wide at Chuck) | **EXTERNAL** (OD) | Toward Chuck ($Z-$) | **`RETRACT`** ($X+$) | Diameter expands as tool approaches chuck. |
| **NORMAL** (Wide at Chuck) | **EXTERNAL** (OD) | Toward Tailstock ($Z+$) | **`INFEED`** ($X-$) | Diameter contracts as tool approaches tail. |
| **INVERTED** (Narrow at Chuck) | **EXTERNAL** (OD) | Toward Chuck ($Z-$) | **`INFEED`** ($X-$) | Diameter contracts as tool approaches chuck. |
| **INVERTED** (Narrow at Chuck) | **EXTERNAL** (OD) | Toward Tailstock ($Z+$) | **`RETRACT`** ($X+$) | Diameter expands as tool approaches tail. |

### 4. Mode Disambiguation: Continuous Gearing vs. Multi-Pass Canned Cycle
NanoELS firmware implements two operational workflows using `MODE_CONE`:
1. **Continuous Angled Gearing (`GEARBOX -> AXIS: Z+X`):** Single continuous cut along the taper angle, bounded by machine soft limits.
2. **Automated Multi-Pass Cycle (`TAPER`):** Autonomous multi-pass cycle taking multiple roughing passes parallel to the taper slope, followed by automated retract and rapid return.

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology

```text
┌───────────────────────────┬───────────────────────────────┐
│ CENTER COLUMN: DYNAMIC    │ RIGHT COLUMN: MODE CARD       │
│ ACTION DECK (202px)       │ CONFIGURATION (260px)         │
├───────────────────────────┼───────────────────────────────┤
│ [ BADGE: Z+X TAPER ]      │ [ TAPER PRESET SELECTOR ]     │
│                           │   MT2 MORSE TAPER #2          │
│ [ 1 : 19.25 (1.4306°) ]   │   [ ✎ SELECT PRESET ]         │
│ (Multi-Format Companion)  │                               │
│                           │ [ SLOPE & PROFILE ]           │
│ [ - ]  0.100 mm/rev  [ + ]│   [ NORMAL ◺ ] / [ INVERT ◿ ] │
│ (Live Z-Feed Trim)        │   [ OD EXTERNAL ] / [ ID INT ]│
│                           │                               │
│ [ DIR: ← TO CHUCK (Z-) ]  │ [ LENGTH & RUNSTACK ]         │
│                           │   LENGTH:      25.000 mm      │
│ [ STEP: ±0.05 mm ][± CUST]│                               │
│                           │ [ UNIVERSAL RUNSTACK ]        │
│ [ APRON JOG D-PAD ]       │   [ START FEED / CYCLE ] (Grn)│
│                           │   [ STOP ]       (Perm Red)   │
└───────────────────────────┴───────────────────────────────┘
```

### 2. Operational Ergonomics
1. **Multi-Format Companion Readout:** Displays the primary authoring format plus companion conversions simultaneously (e.g. `1 : 19.25 (1.4306° | 0.5994 in/ft)`).
2. **Cone Slope Direction Glyphs:**
   * **`NORMAL` (Taller toward Chuck):** Rendered with an SVG trapezoid whose left side is taller than its right side (`◺`), visually indicating expansion toward the spindle.
   * **`INVERT` (Taller toward Tailstock):** Rendered with an inverted SVG trapezoid (`◿`).
   * Tapping the chip toggles `invert` and instantly inverts the graphic and cross-feed sign kinematics.
3. **Preset Library Modal:** Tapping `[ ✎ SELECT PRESET ]` opens the catalog of standard engineering tapers categorized into:
   * Collets & Spindles (MT1–MT5, R8, 5C, ER, 7:24 Steep)
   * Ratios & Pins (1:50 Metric Pin, 1:48 US Pin, 1:20, 1:10, 1:5, NPT/BSPT Pipe 1:16)
   * Chamfers & Centers (60° Lathe Center, 45° Chamfer, 30° Bevel)
