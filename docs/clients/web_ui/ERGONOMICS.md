# whatELS Touch DRO — 800×480 Layout & Ergonomics Specification

> **Target Display:** 5.0" / 7.0" 800×480 Capacitive Touch Screen (Landscape)  
> **Client Runtime:** Vanilla JS + Alpine.js (`clients/web_ui/`)  
> **Protocol Standard:** ADR 15 (Zero-Idle Wire Decoupling & Atomic Cycle Inception)

---

## 1. Fixed Viewport & Zero-Scroll Invariant

1. **Resolution Lock:**
   * The entire application is engineered strictly for **800×480** pixels.
   * **Zero Window Scrolling:** Horizontal and vertical scrollbars on the main window are strictly prohibited. Every operational control, readout, and status badge must fit within the 480px vertical height.
   * Modals and slide-out drawers must be self-contained and manage their own internal overflow scrolling if required.

2. **Touch Target Dimensions (5.0" Screen Standard):**
   * **Primary Action Buttons** (`START`, `STOP`, D-pad, major toggles):
     - Minimum height: **44px – 48px**
     - Minimum width: **56px – 64px**
   * **Secondary / Setup Chips** (Mode tabs, sub-toggles, unit selectors):
     - Minimum height: **36px – 38px**
     - Minimum width: **50px**
   * **Touch Separation:** Minimum **4px – 6px** physical gap between interactive zones to prevent accidental dual-contact trips.
   * **Absolute Floor:** No interactive element may be rendered smaller than **36px** in height or **48px** in width.

---

## 2. DRO Readouts & Typography

1. **Coordinate Readouts (Z & X):**
   * **Font:** Locked to fixed `50px` `JetBrains Mono` with tabular lining figures (`font-variant-numeric: tabular-nums`).
   * **No Dynamic Auto-Shrink:** Dynamic font shrinking during rapid coordinate changes is prohibited to eliminate visual jitter while the lathe carriage is in rapid motion.
   * **Unit Header Placement:** Coordinate lines omit inline unit strings (`mm` or `in`) to ensure negative 7-figure coordinates (e.g. `-325.0450`) fit comfortably. Units are declared in the column header.
2. **Authentic Danish Diameter Glyph:**
   * Diameter notation strictly uses the Danish uppercase slashed O (`Ø`, `U+00D8`), matching the visual height of uppercase `R` (Radius):
     - Example: `Ø 45.000 mm` vs `R 22.500 mm`.

---

## 3. Physical Layout & Apron Ergonomics

```
┌───────────────────────────────┬───────────────────────────────┬───────────────────────────────┐
│         LEFT COLUMN           │         CENTER COLUMN         │         RIGHT COLUMN          │
│                               │                               │                               │
│  [ Spindle / RPM / SFM Card ] │  [ Dynamic Center Deck ]      │  [ Mode Selector / Setup ]    │
│                               │    (Live feed nudge, pass     │    (Conversational Wizard,    │
│  [ Z Coordinate (50px mono) ] │     progress, lead trim)      │     pitch, DOC, passes)       │
│  [ X Coordinate (50px mono) ] │                               │                               │
│                               │  [ Apron Jog D-Pad (Pinned) ] │  [ Permanent Runstack ]       │
│  [ Unit / Mode Status Badges ]│    (3×3 step cluster,         │    [ START FEED ] (Green)     │
│                               │     increment selector)       │    [ STOP ]       (Red)       │
└───────────────────────────────┴───────────────────────────────┴───────────────────────────────┘
```

1. **Pinned Apron Jog D-Pad:**
   * The 3×3 jog cluster is **pinned to the bottom edge of the center column**.
   * This anchors the operator's palm against the lathe front rim during incremental stepping.
   * Step increment selector cycles on tap through discrete standard tables:
     - **Metric:** `0.010 mm → 0.100 mm → 1.000 mm`
     - **Imperial:** `0.0010 in → 0.0100 in → 0.1000 in`
     - Long-press (>600ms) summons the numeric keypad for custom step values.
2. **Permanent Universal Runstack:**
   * Located permanently in the bottom-right quadrant across all modes.
   * `START FEED` / `START CYCLE` (Green `#3ddc84`) is stacked directly above `STOP` (Red `#ff5252`).
   * **Safety Gap:** Separated by a minimum **12px** safety margin to eliminate mis-strikes.
   * `STOP` is permanent and stationary across all operational modes; visually subdued when idle, illuminating vibrant red with an active glow during motion.
3. **Dynamic Center Action Deck:**
   * Directly above the jog D-pad. Houses live apron adjustments:
     - **Gearbox:** Live feed nudge buttons `[ − ]` / `[ + ]`, active feed readout, and live direction toggle.
     - **Turning / Facing:** Real-time pass status (`PASS 01 / 06`) flanked by live feed nudge controls.
     - **Threading:** Thread lead trim when stopped; switches to locked cycle telemetry during active cutting.

---

## 4. Keypad Architecture & Conversational Wizards

1. **Touch Calculator Layout:**
   * Standard 3×4 numeric pad with dedicated quick-math bottom row:
     `[ CURR ] [ ÷ 2 ] [ CLR ] [ ⌫ ]`
   * Opens pre-filled with the active target value, fully selected for instant single-tap overwrite.
   * `÷ 2` provides single-tap halving of measured workpiece diameters for radius calculations.
   * `INSERT CURR` is conditionally enabled only when editing machine coordinates or soft limit boundaries.
2. **ADR 15 Zero-Idle Decoupling & Atomic Inception:**
   * While idle (`runState === 'stopped'`), wizard parameter inputs, mode switching, axis toggles, and pitch edits are **100% client-local presentation state**. Zero wire commands are emitted during drafting.
   * Motion is initiated exclusively by tapping `START`. The client dispatches an atomic inception bundle containing all mode, gear, direction, and shoulder limit parameters.
   * Feature shoulder limits persist in hardware registers following cycle completion or manual `STOP`.
