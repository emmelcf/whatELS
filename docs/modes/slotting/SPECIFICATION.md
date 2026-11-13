# Operational Mode: Slotting & Keyway Broaching
## Reciprocating Linear Strokes, Spindle Lockout & Deck Specification

> **Companion Architecture Documents:**
> * For the general mode lifecycle and interfaces: see [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md).
> * For the central state model and properties: see [`../state/ELS_CORE_STATE_MODEL.md`](../state/ELS_CORE_STATE_MODEL.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../clients/hmi/RICH_TOUCH_800X480.md`](../../clients/hmi/RICH_TOUCH_800X480.md).

---

## Part 1: Kinematic & State Specification (Controller Domain)

### 1. Mode Identity & Hardware Capabilities
* **Mode Identifier:** `MODE_SLOTTING` (Legacy Code `6` / Canonical Enum `ELSModeId::Slotting`).
* **Required Machine Capabilities:**
  * Mandatory: `CAP_Z_STEPPER`.
  * Optional: `CAP_X_STEPPER` (for automated incremental cross-slide infeed).
  * Safety Prerequisite: Spindle must be stationary (`rpm == 0`) and mechanically locked or indexed.

### 2. Parameter Manifest

| Parameter | Key | Type | Unit | Range | Default | Description |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Stroke Count** | `slotStrokes` | UInt16 | count | $1 \dots 500$ | $12$ | Total cutting strokes in slotting cycle. |
| **Feed per Stroke**| `slotInfeed` | Int32 | deci-microns | $10 \dots 1{,}000$ | $200$ ($0.020\text{ mm}$) | Infeed depth increment per stroke. |
| **Stroke Length** | `slotLen` | Int32 | deci-microns | $500 \dots 2{,}000{,}000$ | $150{,}000$ ($15.0\text{ mm}$) | Linear carriage cutting stroke along $Z$. |
| **Stroke Speed** | `slotSpeed` | Int32 | deci-microns/min | $10{,}000 \dots 10{,}000{,}000$ | $600{,}000$ ($60\text{ mm/min}$) | Carriage cutting velocity. |
| **Retract Clearance**| `slotRetract` | Int32 | deci-microns | $50 \dots 5{,}000$ | $200$ ($0.020\text{ mm}$) | Relief clearance on return stroke. |

### 3. Kinematic Reciprocating Cycle Mechanics
Slotting cuts keyways, internal splines, and square corners using a single-point broaching tool mounted in the toolpost. The spindle does **not** rotate; instead, the $Z$ carriage reciprocates back and forth:

```text
       ┌───────────┐
       │   IDLE    │ ◄── Operator taps START CYCLE (Spindle MUST be stopped)
       └─────┬─────┘
             ▼
       ┌───────────┐
┌────► │  INFEED   │ Tool infeeds radially (X) by slotInfeed
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │CUT_STROKE │ Rapid linear Z- feed across keyway length at slotSpeed
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │TOOL_RELIEF│ Slight X- relief retract (prevents insert dragging on return)
│      └─────┬─────┘
│            ▼
│      ┌───────────┐
│      │RETURN_RAPID│ Rapid Z+ return stroke to start position
│      └─────┬─────┘
│            │
└─────── [stroke < slotStrokes] ───► [stroke == slotStrokes]
                                         │
                                         ▼
                                   ┌───────────┐
                                   │ COMPLETED │
                                   └───────────┘
```

---

## Part 2: Deck Presentation & Touch Interaction (HMI Profile)

### 1. Spatial Layout Topology
* **Spindle Interlock Alert:** If spindle RPM $> 0$, the `START CYCLE` button is disabled with an amber warning badge: `⚠ SPINDLE ROTATING — LOCK SPINDLE`.
* **Dynamic Center Deck:**
  * Top Badge: `SLOTTING / KEYWAY` (Green).
  * Center Readout: Real-time stroke progress (`STROKE 04 / 12`), cumulative depth cut.
  * Infeed Trims: `[ − ]` and `[ + ]` nudge buttons to alter per-stroke infeed under cutting load.
* **Mode Card Setup:**
  * `STROKES`: 12 strokes default.
  * `STROKE LEN`: $15.0\text{ mm}$ default.
  * `FEED / STROKE`: $0.020\text{ mm}$ default.
