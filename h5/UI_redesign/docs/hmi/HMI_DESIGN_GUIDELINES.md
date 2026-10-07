# Universal HMI Design Guidelines for Lathe Electronic Leadscrew (ELS)

> **Status:** Authoritative Specification  
> **Scope:** Cross-Client Human-Machine Interface (HMI) Principles  
> **Applicable Clients:** All Clients (800×480 Capacitive Touch, Native LVGL Embedded Displays, Hardware Pendants, Physical Console Terminals, Web UI)  
> **Companion Document:** For concrete 800×480 capacitive touch sizing, typography, and layout geometry, see [`RICH_TOUCH_800X480.md`](./RICH_TOUCH_800X480.md).

---

## 1. Executive Summary & Design Philosophy

The NanoELS operator interface bridges industrial metal lathe kinematics with modern digital control. A lathe is an unforgiving machine tool: spindle motors develop tremendous torque, carbide tools take heavy chip loads, and rotating workpieces pose severe mechanical hazards.

An operator interface on a lathe must never feel like an experimental consumer gadget. It must function as a rock-solid, predictable industrial instrument. These guidelines establish the universal human factors and safety invariants governing **every client interface** interacting with the NanoELS controller.

---

## 2. Radical Hardware Fidelity & Transparency

A digital interface must never mislead an operator about the physical machine's true state.

### 1. Single Binary Motion Truth (`isOn`)
* The core motion firmware operates strictly on a binary state engine: `STOPPED` (`isOn == false`) or `RUNNING` (`isOn == true`).
* **Prohibition of Fabricated "Software Pause":** There is no native pause state in NanoELS core firmware. Fabricating a software pause in a client UI is strictly prohibited. Pausing spindle-synchronized motion risks tool breakage, workpiece gouging, and positional drift. Motion is either actively engaged or safely halted.

### 2. Limit Register Honesty (Zero "Ghost Limits")
* NanoELS firmware maintains strictly a single pair of physical hardware limit registers per axis (`z_min`/`z_max`, `x_min`/`x_max`).
* The UI must never display "ghost" limits or create a false sense of security. What is displayed on DRO limit chips must always reflect the exact boundary currently active in the motion controller.

### 3. No "Ghost Actions" During Autonomous Cycles
* When NanoELS executes an automated multi-pass cycle (such as `THREADING`, `TURNING`, or `FACING`), trajectory planning and pass progression are executed autonomously by the motion core.
* Presenting a "Next Pass" or "Micro-Step" button provides a false illusion of control that the firmware cannot safely support mid-cut. During active cycles, the UI strictly displays cycle telemetry (`PASS 05 / 10`, current depth) and leaves the `STOP` button as the sole operator intervention.

---

## 3. Safety-Critical Operational Invariants

### 1. Permanent, Stationary, Unoccluded Emergency STOP
* Muscle memory is the operator's primary line of defense.
* The primary `STOP` control must occupy a fixed, stationary position across all operating modes and screen layouts.
* It must **never** move, shrink, reflow, or be hidden behind tabs or scrolling views.
* During active motion, it must be visually prominent (vibrant red with active illumination).

### 2. Strict Running Lockout (No Modal Dialogs or Mode Shifts)
* **The Occlusion Hazard:** On an operating lathe, visual occlusion and surprise popups are severe safety hazards. If a tool is cutting near a chuck or shoulder, an accidental touch or bump must **never** trigger a modal dialog, keypad popup, toast notification, or full-screen sheet that blocks the DRO or covers the `STOP` button.
* **Lockout Rules During Active Motion (`isOn == true`):**
  1. **Mode Switching is Forbidden:** The operator must halt motion before changing modes.
  2. **Zero Popups / Modals:** All dialog and keypad triggers become silent no-ops while running.
  3. **Setup Parameters are Locked:** Pitch selection, cone angles, infeed strategies, and pass counts are non-interactive.
  4. **Allowed Runtime Actions:** Only non-modal live trims (feed overrides, live `+`/`−` nudges, and the `STOP` button) remain interactive.

### 3. Anti-Trap Limit Contact Inference & Asymmetric Retreat Freedom
* **Core Motion Behavior:** NanoELS firmware does not emit an alarm packet when a soft limit is contacted; it silently suppresses step pulses in the tripping direction (`delta = limit - pos = 0`).
* **Client Responsibility:** The client interface must evaluate limit proximity from telemetry (`distanceToStop == 0`).
* **Asymmetric Jog State Machine:** When a limit is hit, the UI blocks further jog commands into the boundary (styling the arrow red), while guaranteeing that the retreat jog control remains 100% enabled and accessible so the operator can immediately back away from the wall.

---

## 4. Machinist Mental Model & Operator Reference Frame

### 1. Physical Lathe Coordinate Perspective
All directional arrows, D-pads, and indicators must strictly align with the physical lathe apron perspective:

```text
                     HEADSTOCK / CHUCK
                            ▲
                            │
               ┌────────────┴────────────┐
               │    SPINDLE ROTATION     │
               │    CCW (FWD) / CW (REV) │
               └────────────┬────────────┘
                            │
        ◄───────────────────┼───────────────────►
        ← CHUCK (Z−)        │       → TAILSTOCK (Z+)
   (Longitudinal Carriage)  │    (Longitudinal Carriage)
                            │
                 WORKPIECE CENTERLINE
                 ▲  ↑ TO CENTER / PLUNGE (X−)
                 │  ↓ RETRACT / CLEARANCE (X+)
                 ▼  (Transverse Cross-Slide)
                            │
                     OPERATOR / APRON
```

* **$Z$ Axis (Longitudinal Carriage):**
  * `← CHUCK` ($Z-$): Feed toward the headstock/chuck.
  * `→ TAIL` ($Z+$): Feed toward the tailstock.
* **$X$ Axis (Transverse Cross-Slide):**
  * `↑ TO CENTER` ($X-$): Plunge into workpiece / toward center (reducing diameter).
  * `↓ RETRACT` ($X+$): Back out toward operator (increasing diameter).

### 2. Dimension-First Machining Intent
* Machinists think in drawing dimensions (e.g. "turn a 25.0 mm shoulder" or "face 1.5 mm of stock"), not arbitrary machine pulse coordinates.
* Setting a feature dimension (`LENGTH` in turning, `STROKE` in facing) automatically maps to the required hardware limit relative to tool touch-off, eliminating manual jog-and-subtract math.

### 3. Distance-to-Boundary Travel Limits
* When running feeds or approaching shoulders, an operator needs to know **how much distance remains before the tool hits the stop**, not raw machine coordinates.
* DRO limit chips display **distance remaining to stop** ($5.843 \rightarrow 0.000\text{ mm}$), matching physical Nextion firmware convention.
* **Vector Stop Indicators:** Boundary chips utilize vector glyphs where the arrow points in the travel direction, hitting the boundary wall bar at the tip (`|←`, `→|`, `⤒`, `⭳`).

---

## 5. Engineering Unit & Fastener Fidelity

### 1. Prevention of Awkward Unit Conversions
* Machinists do not work in arbitrary converted decimals (e.g., $0.010\text{ mm} \rightarrow 0.00039''$).
* The UI maintains distinct, discrete standard tables for metric vs. imperial operations:
  * **Metric Jog:** $1.000\text{ mm}$, $0.100\text{ mm}$, $0.010\text{ mm}$.
  * **Imperial Jog:** $0.1000''$, $0.0100''$, $0.0010''$.
  * **Metric Feeds:** $0.020$, $0.050$, $0.100$, $0.200\text{ mm/rev}$.
  * **Imperial Feeds:** $0.001''$, $0.002''$, $0.005''$, $0.010''\text{/rev}$.

### 2. Exact Thread Pitch & TPI Lead Fidelity
* In imperial mode, screw threads are fundamentally specified in Threads Per Inch (TPI) as integers (e.g., `16 TPI`). Converting TPI to a floating-point pitch ($1.5875\text{ mm}$) introduces rounding errors and clashes with shop convention.
* When TPI is active, client interfaces preserve the exact integer across all displays, property sheets, and intent packets.

---

## 6. Separation of Setup vs. Live Apron Controls

Lathe operation consists of two distinct mental phases:
1. **Setup & Configuration (While Stopped):** The operator dials in parameters (thread standard, pitch, passes, cone ratio, dimensions).
2. **Execution & Trimming (While Running):** The operator engages the cut and monitors tool engagement, chips, and vibration.

**Universal Principle:** Keep setup controls segregated from live apron controls:
* Setup fields belong in dedicated parameter cards or configuration wizards, strictly locked out during motion.
* The live apron area contains only immediate operational controls: feed readout, feed trims/nudges, direction indicator, and the stationary `STOP` button.

---

## 7. Universal Semantic State & Color Hierarchy

To prevent cognitive clutter and ensure instant situational awareness from across the workshop, colors follow four non-overlapping semantic tiers:

### Tier 1: Machine Safety & Motion State
* **Vibrant Green:** Reserved strictly for affirmative runtime execution (`START`, running status indicators, encoder pulse activity).
* **Vibrant Red:** Reserved strictly for safety boundaries, emergency actions, and stops (`STOP` button active glow, limit contact block, abort actions).
* **Warm Amber:** Reserved for supervisory interventions (skim pass confirmation modals, blind-hole reverse spindle advisories, paused states).

### Tier 2: Kinematic Axis Identification
* **$Z$ Axis (Longitudinal Carriage):** Emerald Green (`#3ddc84`).
* **$X$ Axis (Transverse Cross-Slide):** Cyan Blue (`#5aa9ff`).
* **$Z+X$ Coordinated Axis (Cone / Taper):** Warm Amber (`#ffb454`).

### Tier 3: Dimensional Representation
* **Diameter Mode ($\varnothing$):** Styled in Cyan Blue, reinforcing cross-slide diametral measurement.
* **Radius Mode ($R$):** Styled in Warm Amber, warning the operator that radial infeed cuts 1:1 on tool radius.

### Tier 4: General Affordance & Telemetry
* High-contrast white for numeric coordinates and active dimensions.
* Subdued slate/grays for non-axis toggles, labels, and borders.
