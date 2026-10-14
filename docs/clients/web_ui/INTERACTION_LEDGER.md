# NanoELS Web UI: UI Interaction to Wire Command Ledger

> **Status:** Authoritative Specification  
> **Client:** NanoELS Web UI / Touch DRO Interface  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `docs/clients/web_ui/INTERACTION_LEDGER.md`  
> **Companion Document:** [`OVERVIEW.md`](./OVERVIEW.md)  
> **Parent Specifications:** [`ELS_CLIENT_ARCHITECTURE.md`](../../ELS_CLIENT_ARCHITECTURE.md), [`ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md)

---

## 1. Executive Summary & Safety Policy

This document provides the exhaustive, 1-to-1 mapping of every interactive element on the NanoELS Web UI prototype ([`lathe_dro_nanoels_controller.html`](../../../clients/web_ui/lathe_dro_nanoels_controller.html)) to its JavaScript handler, emitted wire command, and motion controller kinematic effect.

### Universal Safety Invariants:
1. **Running Motion Lockout (`state.runState === 'running'`):**  
   While axes or the spindle are actively driving a cut (`isOn == true`), **all modal dialogs, keypad popups, and setup toggles are unconditionally suppressed**. Tapping the screen during an active cut performs a silent no-op, preventing full-screen overlays from physically occluding the red **STOP** button.
2. **Permanent Red STOP Corner:**  
   The red `STOP` button is permanently anchored in the bottom-right corner across all modes. It is never covered, shifted, or unmounted under any circumstance.
3. **Semantic Intent Over Raw Signs:**  
   Clients transmit operator intent (`CHUCK`, `TAIL`, `INFEED`, `RETRACT`) rather than raw Cartesian $+/-$ signs, protecting against crashes across front/rear toolpost setups.

---

## 2. Comprehensive 40-Row Interaction Ledger

| # | UI Control / Action | Target Selector / DOM Element | Event Trigger | JS Handler | Emitted Wire Command | Controller Kinematic Effect |
| :-: | :--- | :--- | :--- | :--- | :--- | :--- |
| **1** | **D-Pad Tap Z-** | `#btnJogZMinus` | Pointerup (<300ms) | `jogStep('Z', 'CHUCK')` | `CMD.jogStep=Z,CHUCK\n` | `queueWebUiStep(B_LEFT)`. Moves carriage 1 step toward chuck. |
| **2** | **D-Pad Hold Z-** | `#btnJogZMinus` | Pointerdown (≥300ms) | `jog('Z', 'CHUCK', true)` | `CMD.jogHold=Z,CHUCK\n` | `queueWebUiEvent(B_LEFT, true)`. Continuous carriage feed until released. |
| **3** | **D-Pad Release Z-** | `#btnJogZMinus` | Pointerup / cancel | `jog('Z', 'CHUCK', false)` | `CMD.jogRelease=Z\n` | `queueWebUiEvent(B_LEFT, false)`. Decelerates carriage smoothly to stop. |
| **4** | **D-Pad Tap Z+** | `#btnJogZPlus` | Pointerup (<300ms) | `jogStep('Z', 'TAIL')` | `CMD.jogStep=Z,TAIL\n` | `queueWebUiStep(B_RIGHT)`. Moves carriage 1 step toward tailstock. |
| **5** | **D-Pad Hold Z+** | `#btnJogZPlus` | Pointerdown (≥300ms) | `jog('Z', 'TAIL', true)` | `CMD.jogHold=Z,TAIL\n` | `queueWebUiEvent(B_RIGHT, true)`. Continuous carriage feed until released. |
| **6** | **D-Pad Release Z+** | `#btnJogZPlus` | Pointerup / cancel | `jog('Z', 'TAIL', false)` | `CMD.jogRelease=Z\n` | `queueWebUiEvent(B_RIGHT, false)`. Decelerates carriage smoothly to stop. |
| **7** | **D-Pad Tap X+** | `#btnJogXPlus` | Pointerup (<300ms) | `jogStep('X', 'RETRACT')` | `CMD.jogStep=X,RETRACT\n` | `queueWebUiStep(B_DOWN)`. Steps cross-slide 1 step away from centerline. |
| **8** | **D-Pad Hold X+** | `#btnJogXPlus` | Pointerdown (≥300ms) | `jog('X', 'RETRACT', true)` | `CMD.jogHold=X,RETRACT\n` | `queueWebUiEvent(B_DOWN, true)`. Continuous cross-slide retract until released. |
| **9** | **D-Pad Release X+** | `#btnJogXPlus` | Pointerup / cancel | `jog('X', 'RETRACT', false)` | `CMD.jogRelease=X\n` | `queueWebUiEvent(B_DOWN, false)`. Decelerates cross-slide smoothly to stop. |
| **10** | **D-Pad Tap X-** | `#btnJogXMinus` | Pointerup (<300ms) | `jogStep('X', 'INFEED')` | `CMD.jogStep=X,INFEED\n` | `queueWebUiStep(B_UP)`. Steps cross-slide 1 step inward toward centerline. |
| **11** | **D-Pad Hold X-** | `#btnJogXMinus` | Pointerdown (≥300ms) | `jog('X', 'INFEED', true)` | `CMD.jogHold=X,INFEED\n` | `queueWebUiEvent(B_UP, true)`. Continuous cross-slide plunge until released. |
| **12** | **D-Pad Release X-** | `#btnJogXMinus` | Pointerup / cancel | `jog('X', 'INFEED', false)` | `CMD.jogRelease=X\n` | `queueWebUiEvent(B_UP, false)`. Decelerates cross-slide smoothly to stop. |
| **13** | **Chamfer NW (↖ Z- X-)** | `#btnChamferNW` | Pointerdown hold | `jogChamfer('NW', true)` | `CMD.jogChamfer=NW,1\n` | Coordinated dual-axis jog: Z toward chuck, X infeed. |
| **14** | **Chamfer Release NW** | `#btnChamferNW` | Pointerup / cancel | `jogChamfer('NW', false)` | `CMD.jogChamfer=NW,0\n` | Decelerates both axes to a coordinated stop. |
| **15** | **Chamfer SE (↘ Z+ X+)** | `#btnChamferSE` | Pointerdown hold | `jogChamfer('SE', true)` | `CMD.jogChamfer=SE,1\n` | Coordinated dual-axis jog: Z toward tailstock, X retract. |
| **16** | **Chamfer Release SE** | `#btnChamferSE` | Pointerup / cancel | `jogChamfer('SE', false)` | `CMD.jogChamfer=SE,0\n` | Decelerates both axes to a coordinated stop. |
| **17** | **Runstack: START** | `#btnRun` | Click / tap | `startCycle()` | `CMD.on=1\n` | `setIsOnFromTask(true)`. Arms/engages active kinematic cycle. |
| **18** | **Runstack: STOP** | `#btnStop` | Click / tap | `stopCycle()` | `CMD.on=0\n` | `setIsOnFromTask(false)`. Immediately halts stepping; disengages cycle. |
| **19** | **Zero Z Axis** | `#btnZeroZ` | Click / tap | `zeroAxis('Z')` | `CMD.axisZero=Z\n` | `setAxisZero(&z)`. Sets current carriage position as $Z=0.000$. |
| **20** | **Zero X Axis** | `#btnZeroX` | Click / tap | `zeroAxis('X')` | `CMD.axisZero=X\n` | `setAxisZero(&x)`. Sets current cross-slide position as $X=0.000$. |
| **21** | **Mode Selector** | Nav tabs / `.mode-chip` | Click / tap | `setMode(modeSlug)` | `CMD.mode=<slug>\n` | Activates canonical mode (`gearbox`, `turning`, `threading`, etc.). |
| **22** | **Reverse Feed / Pitch** | `#deckToggleDir` | Click / tap | `setGearDir(dir)` / `reverseFeed()` | `CMD.gearDir=<dir>\n` / `CMD.feedRev\n` | Flips motion travel direction (CHUCK/TAIL or INFEED/RETRACT). |
| **23** | **Jog Step Cycle** | `#stepMain` | Click / tap | `cycleJogStep()` | `CMD.jogStepDu=<du>\n` | Cycles in-memory `moveStep` through standard table (e.g. 100, 1000, 10000 du). |
| **24** | **Custom Jog Step** | `#stepMain` | Long-press (>600ms) | `openKeypad('step')` | `CMD.jogStepDu=<du>\n` | Opens keypad to input non-standard step increment. |
| **25** | **Feed Step Increment** | `#deckStepCycle` | Click / tap | `cycleFeedStep()` | `CMD.feedStepDu=<du>\n` | Advances feed override increment ($0.01 \to 0.05 \to 0.10 \to 0.20\text{ mm}$). |
| **26** | **Feed Nudge Minus** | `#deckNudgeMinus` | Click / tap | `nudgeFeed(-1)` | `CMD.feedNudge=-1\n` | Nudges active cutting feed downward by 1 `feedStepDu`. Permitted while `RUNNING`. |
| **27** | **Feed Nudge Plus** | `#deckNudgePlus` | Click / tap | `nudgeFeed(+1)` | `CMD.feedNudge=+1\n` | Nudges active cutting feed upward by 1 `feedStepDu`. Permitted while `RUNNING`. |
| **28** | **Gearbox Axis Selector** | `#deckAxisSelector` / `.axis-tab` | Click / tap | `setGearAxis(axis)` | `CMD.gearAxis=<Z\|X\|ZX>\n` | Switches active leadscrew axis (Carriage $Z$, Cross-slide $X$, or Taper $ZX$). |
| **29** | **Gearbox Sync Toggle** | `#deckSyncToggle` | Click / tap | `setGearSync(sync)` | `CMD.gearSync=<1\|0>\n` | Toggles between Spindle Synchronized feed and continuous Timer Velocity feed. |
| **30** | **Unit System Toggle** | `#unitToggle` | Click / tap | `cycleMeasure()` | *None (Client Local)* | Toggles `$store.machine.measure` (`0`: mm, `1`: in). Zero wire traffic. |
| **31** | **Diameter / Radius Toggle** | `#repBadge` | Click / tap | `toggleDiaRad()` | *None (Client Local)* | Toggles `$store.machine.dia` (`Ø` vs `R`). Core kinematics remain pure radius. |
| **32** | **Set Z Left Limit** | `#chipZLeft` | Click / tap | `openLimitModal('Z', 'L')` | `CMD.axisLimitDu=Z,L,<du>\n` | Programs hardware left limit register (`z.leftStop`). |
| **33** | **Set Z Right Limit** | `#chipZRight` | Click / tap | `openLimitModal('Z', 'R')` | `CMD.axisLimitDu=Z,R,<du>\n` | Programs hardware right limit register (`z.rightStop`). |
| **34** | **Clear Z Limits** | `#btnClearLimitZ` | Click / tap | `clearLimit('Z')` | `CMD.axisClearLimit=Z,L\n` | Clears `z.leftStop = LONG_MAX`, restoring unbounded carriage travel. |
| **35** | **Set X Infeed Limit** | `#chipXUp` | Click / tap | `openLimitModal('X', 'U')` | `CMD.axisLimitDu=X,L,<du>\n` | Programs hardware infeed limit register (`x.leftStop`). |
| **36** | **Set X Retract Limit** | `#chipXDown` | Click / tap | `openLimitModal('X', 'D')` | `CMD.axisLimitDu=X,R,<du>\n` | Programs hardware retract limit register (`x.rightStop`). |
| **37** | **Set Metric Pitch** | Pitch keypad confirm | Keypad SET tap | `setFeedrateMm(mm)` | `CMD.feedMm=<val>\n` | Computes deci-microns per rev and engages metric leadscrew ratio. |
| **38** | **Set TPI Pitch** | Pitch keypad confirm | Keypad SET tap | `setFeedrateTpi(tpi)` | `CMD.feedTpi=<val>\n` | Computes fractional leadscrew ratio for imperial threads per inch. |
| **39** | **Set Taper Angle** | Taper keypad confirm | Keypad SET tap | `setTaperAngle(deg)` | `CMD.taperAngle=<val>\n` | Computes $K = 2\tan(\theta)$ and configures coordinated cone kinematics. |
| **40** | **Set Taper Ratio** | Taper keypad confirm | Keypad SET tap | `setTaperRatio(1/K)` | `CMD.taperRatio=<val>\n` | Configures cone slope ratio directly (e.g. 1:10, 1:20). |
| **41** | **Spindle Sim RPM** | Visualizer Spindle HUD | Click / input | `setSpindleSim(1, rpm)` | `CMD.spindleSim=1,<rpm>\n` | Overrides physical PCNT with synthetic pulse generator for offline verification. |
| **42** | **Spindle Sim Turn** | Visualizer Chuck Wheel | Pointer drag / step | `spindleSimTurn(deg)` | `CMD.spindleSimTurn=<deg>\n` | Generates queued encoder pulses equivalent to specified hand rotation. |

---

## 3. Dynamic In-Motion Safety Guard & Permission Policy

Rather than applying an indiscriminate, blanket lockout that breaks runtime ergonomics, the Web UI client dynamically evaluates the authoritative **`K.perms`** (`LivePermissionFlags`) bitmask streamed continuously from the controller:

```javascript
// Live In-Motion Safety Guard in lathe-controller.js
function canExecuteAction(actionFlag) {
  const machine = Alpine.store('machine');
  // If machine is not running a cut, all standard setup actions are permitted
  if (!machine.isOn && machine.rpm === 0) return true;

  // If machine is running, only actions permitted by active mode perms are allowed
  return (machine.perms & actionFlag) !== 0;
}
```

### Safety & Lockout Rules:
1. **Live Feed Trimming Under Chip Load:**
   - When cutting under chip load (`PERM_LIVE_FEED_TRIM` active in `K.perms`), the Feed Nudge buttons (`#deckNudgeMinus`, `#deckNudgePlus`) and step cycle button (`#deckStepCycle`) remain **fully responsive**.
2. **Structural Modifications Strictly Suppressed:**
   - While `RUNNING`, modal overlays, keypad popups, axis swaps (`CMD.gearAxis`), sync mode toggles (`CMD.gearSync`), mode switches (`CMD.mode`), and coordinate zeroing (`CMD.axisZero`) are **visually dimmed and disabled**.
   - Attempting to tap dimmed structural controls emits a non-intrusive warning badge and zero wire traffic.
3. **Permanent Red STOP Corner:**
   - The red `STOP` button is permanently anchored in the bottom-right corner across all modes. It is never covered, shifted, or unmounted under any circumstance.
4. **Semantic Intent Over Raw Signs:**
   - Clients transmit operator intent (`CHUCK`, `TAIL`, `INFEED`, `RETRACT`) rather than raw Cartesian $+/-$ signs, protecting against crashes across front/rear toolpost setups.
