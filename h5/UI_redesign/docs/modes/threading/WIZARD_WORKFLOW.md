# Operational Mode: Threading — Wizard Workflow & Conversational Engine
## Multi-Stage Touch Workflow, Standard Fastener Datasets & Skim Pass Verification

> **Companion Architecture Documents:**
> * For the Threading kinematic and state specification: see [`SPECIFICATION.md`](./SPECIFICATION.md).
> * For the 800×480 Rich Touch HMI Profile standards: see [`../../hmi/RICH_TOUCH_800X480.md`](../../hmi/RICH_TOUCH_800X480.md).

---

## 1. Conversational Threading Architecture

Authoring a thread on a lathe is traditionally prone to calculation errors: machinists must consult external machinery handbooks for target stock diameters, pitch depths, minor diameters, and pass schedules.

The **Conversational Threading Wizard** integrates this domain knowledge directly into the HMI through a structured 4-stage modal workflow:

```text
┌────────────────────────────────────────────────────────────────────────┐
│ STAGE 1: STANDARD & FAMILY SELECTION                                   │
│  [ METRIC ISO ]  [ UN / IMPERIAL ]  [ NPT / PIPE ]  [ CUSTOM / LEAD ]  │
├────────────────────────────────────────────────────────────────────────┤
│ STAGE 2: SIZE & PITCH RESOLUTION                                       │
│  Nominal Dia: [ M12 ] ──► Standard Pitches: [ 1.75 COARSE ] [ 1.25 ]   │
│  Auto-Derived: Stock Prep OD (11.88 mm) | Thread Depth (1.073 mm)      │
├────────────────────────────────────────────────────────────────────────┤
│ STAGE 3: GEOMETRY & SAFETY ORIENTATION                                 │
│  Profile: [ OD EXTERNAL ] / [ ID INTERNAL ]                            │
│  Direction: [ CHUCK (Z-) Outside-In ] / [ TAIL (Z+) Blind-Hole Safe ]  │
│  Length: [ 25.000 mm ] (Sets Shoulder Limit) | Handedness: [ RH / LH ] │
├────────────────────────────────────────────────────────────────────────┤
│ STAGE 4: CUT STRATEGY & SKIM VERIFICATION REVIEW                       │
│  Infeed: [ 29.5° MODIFIED FLANK ] | Passes: [ 11 ] (auto DOC)          │
│  Safety Option: [ ☑ SKIM PASS VERIFICATION (Human-in-the-Loop) ]       │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Standard Fastener Presets & Dataset Structure

The wizard integrates standard engineering thread tables ([`nanoels-presets.js`](file:///c:/Users/Emil/Documents/Code/nanoels/h5/UI_redesign/prototype/nanoels-presets.js)) covering 4 primary standards:

### 1. ISO Metric (`THREAD_FAMILIES.METRIC`)
* Organized into diameter ranges: $\le \text{M6}$, $\text{M7–M12}$, $\text{M14–M24}$, $\text{M27–M42}$.
* Prioritized by ISO preference choice (1st Choice Primary vs. 2nd/3rd Choice Secondary sizes).
* Includes target stock preparation OD tolerance bounds (`stockMin` / `stockMax`) and available standard pitches (Coarse primary, Fine secondary).

### 2. Unified National (`THREAD_FAMILIES.UN`)
* Standard Imperial sizes from `#0` through `1-1/2"`.
* Provides coarse (UNC), fine (UNF), and extra-fine (UNEF) selections with exact TPI authoring registers.

### 3. Tapered Pipe Threads (`THREAD_FAMILIES.NPT` & `BSPT`)
* National Pipe Taper (NPT) and British Standard Pipe Taper (BSPT).
* Automatically injects coordinated cone slope ratio ($1:16$, $0.75\text{ in/ft}$, $1.7899^\circ$) into cross-slide synchronization (`coneRatio = 0.0625`).

### 4. Freeform / Custom Helix (`FREEFORM`)
* Allows arbitrary pitch ($mm$ or TPI), arbitrary profile angles ($60^\circ$, $55^\circ$ Whitworth, $29^\circ$ Acme, $30^\circ$ Trapezoidal), and user-defined pass schedules.

---

## 3. The 4-Stage Modal Wizard Workflow

### Stage 1: Standard & Family Selection
* The operator selects the fastener standard.
* Selection immediately filters available sizes and configures display units without mutating the underlying controller state.

### Stage 2: Nominal Size & Pitch Selection
* **Two-Column Filter Layout:**
  * Left: Ascending size list with touch chips ($\ge 38\text{px}$ height).
  * Right: Pitch chips for the selected size, clearly highlighting the primary coarse pitch (e.g. `1.75 mm` for M12) alongside optional fine pitches (`1.50`, `1.25`, `1.00 mm`).
* **Instant Parameter Derivation:**
  * Theoretical thread depth: $H = 0.866025 \times P$; Radial depth $h_3 = 0.61343 \times P$.
  * Recommended stock prep OD: $\text{Nominal} - (0.05 \dots 0.15 \times P)$.

### Stage 3: Geometry & Direction Safety
* **External (OD) vs. Internal (ID):** Configures cross-slide infeed direction:
  * External: Cuts inward toward centerline ($X-$). Retracts outward ($X+$).
  * Internal (Boring): Cuts outward away from centerline ($X+$). Retracts inward ($X-$).
* **Blind-Hole Safe Feed Direction (`threadCutDir`):**
  * **`CHUCK` (Z− Outside-In):** Standard turning from tailstock toward headstock. Requires relief groove or manual stop clearance.
  * **`TAIL` (Z+ Inside-Out):** Starts inside the relief groove or blind bore bottom and feeds outward toward the tailstock. **Eliminates blind-hole shoulder crashes.**
* **Thread Length (`zLength`):**
  * Tapping opens the numeric keypad. Entering length automatically maps to the Z limit register relative to the start touch-off.

### Stage 4: Cut Strategy & Skim Pass Verification
* **Infeed Strategy:** Toggle between $29.5^\circ$ Modified Flank, $30^\circ$ Flank, or Radial Plunge.
* **Pass Count:** Auto-calculated using $\lceil \text{Depth} / \text{MaxDOC} \rceil$, editable via keypad.
* **Skim Pass Verification Toggle (`threadSkimVerify`):** Default `ON`.

---

## 4. Human-in-the-Loop Skim Pass Verification

Cutting an incorrect thread pitch (e.g., cutting 13 TPI on an expensive 1/2-20 shaft) scraps the workpiece immediately. The HMI provides an integrated **Skim Pass Verification**:

```text
[ ARMED: SKIM PASS ] ──► Tool cuts ultra-light 0.02 mm scratch mark along Z
                              │
                              ▼
┌────────────────────────────────────────────────────────┐
│ ⚠ SKIM PASS COMPLETE — VERIFY THREAD PITCH             │
│                                                        │
│ Tool is retracted to safe clearance.                   │
│ Use a thread pitch gauge or optical loupe to confirm   │
│ that the scratch line matches your target pitch:       │
│                                                        │
│         TARGET: 1.500 mm (16.93 TPI)                   │
│                                                        │
│   [ ABORT CYCLE ]             [ CONFIRM & PROCEED ]    │
└────────────────────────────────────────────────────────┘
                              │
               ┌──────────────┴──────────────┐
               ▼                             ▼
        [ ABORT CYCLE ]            [ CONFIRM & PROCEED ]
   Carriage returns to start;     Cycle enters full autonomous
   Zero stock damage              multi-pass infeed schedule
```

* **Safety Invariant:** The controller halts automatically after the single scratch stroke and waits for explicit operator confirmation. It never proceeds to deep roughing passes without manual clearance.

---

## 5. "Prepare Stock" Cross-Mode Workflow Bridge

A major source of scrapped parts in lathe screw cutting is improper stock diameter prior to threading. The `[ PREPARE STOCK ➔ ]` action button bridges conversational threading into roughing preparation:

* **For Straight Threads (Metric, UN, Freeform):**
  1. Caches thread specification and target stock OD (e.g. $\varnothing 11.88\text{ mm}$ for M12).
  2. Automatically transitions the controller to **`TURNING` mode**.
  3. Pre-populates `TOTAL DEPTH` based on the delta between active measured X diameter and target prep OD ($\Delta X = X_{\text{current}} - X_{\text{target}}$).
  4. Derives pass count from `MAX DOC`.
  5. Displays advisory telemetry: `PREPARE STOCK: SWITCHED TO TURNING (TARGET OD: Ø 11.88 mm FOR M12 × 1.75)`.
* **For Pipe Threads (NPT, BSPT):**
  1. Caches pipe thread standard.
  2. Automatically transitions the controller to **`TAPER` mode**.
  3. Pre-populates the exact $1:16$ cone angle ($1.7899^\circ$).
  4. Pre-populates taper stroke length to standard pipe engagement length ($L_2$).
  5. Displays advisory telemetry: `PREPARE STOCK: SWITCHED TO TAPER (1:16 CONE, L=13.6mm FOR 1/2" NPT)`.

