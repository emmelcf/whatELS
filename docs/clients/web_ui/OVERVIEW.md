# NanoELS Web UI Client: Architecture & Implementation Specification

> **Status:** Authoritative Reference Implementation  
> **Client Type:** Modern Browser / Touchscreen DRO Interface (HTML5 / ES Modules / Alpine.js)  
> **Author:** Antigravity & Emil  
> **Date:** October 7, 2026  
> **Location:** `docs/clients/web_ui/OVERVIEW.md`  
> **Primary Prototype File:** [`lathe_dro_nanoels_controller.html`](../../../clients/web_ui/lathe_dro_nanoels_controller.html)  
> **Companion Document:** [`INTERACTION_LEDGER.md`](./INTERACTION_LEDGER.md)  
> **Core Architecture References:** [`ELS_CLIENT_ARCHITECTURE.md`](../../ELS_CLIENT_ARCHITECTURE.md), [`ELS_CORE_STATE_MODEL.md`](../../ELS_CORE_STATE_MODEL.md)

---

## 1. Executive Summary & Design Principles

The **NanoELS Web UI** is the primary rich client interface for the NanoELS electronic leadscrew platform. Designed for modern touch displays ($800 \times 480$ landscape up to full $1080\text{p}$ shop tablets), it delivers a responsive digital readout (DRO), conversational machining wizards, electronic gearbox control, and a real-time digital twin visualizer.

### Governing Tenets
1. **Zero-Build, 100% Offline Runtime:**  
   The application requires no bundler, npm scripts, or node build pipeline. It runs directly in any modern browser via local files (`file:///`), HTTP from an ESP32 LittleFS server, or an external companion server.
2. **Deterministic Vanilla / Reactive Boundary:**  
   Complex modal wizards, numeric keypad input, and sub-300ms hardware touch gestures are managed imperatively in Vanilla JS (`lathe-controller.js`), while the high-frequency DRO column and status badges update reactively via Alpine.js (`$store.machine`).
3. **Strict Client-Local Presentation Independence:**  
   Preferences like Metric vs. Imperial (`measure`) and Diameter vs. Radius (`dia`) are managed purely within the client store. Toggling display formats causes zero network traffic and zero cross-client side effects.
4. **Isomorphic Simulator & Hardware Parity:**  
   The client integrates an in-browser physics emulator (`nanoels-emulator.js`) that mirrors ESP32 kinematics and wire protocol, allowing 100% offline development and testing without physical lathe hardware.

---

## 2. Codebase Organization & ES Module Decomposition

The client codebase is structured into cohesive, decoupled ES modules:

```
UI_redesign/prototype/
├── lathe_dro_nanoels_controller.html  # Declarative HTML5 markup & Alpine bindings (~530 lines)
├── alpine.min.js                      # Alpine.js v3.17.4 local offline runtime (~43 KB)
├── lathe-controller.css               # Design system tokens, styles, and touch targets (~1,250 lines)
├── nanoels-service.js                 # Network transport abstraction & backend switcher (~380 lines)
├── nanoels-state.js                   # Alpine.store('machine') 4-domain reactive model (~240 lines)
├── nanoels-presets.js                 # Standard fastener thread tables & taper geometry (~350 lines)
├── nanoels-formatters.js              # Pure render-time math formatters (du -> strings) (~90 lines)
├── lathe-controller.js                # Modal controllers, wizards, keypad & touch gestures (~4,000 lines)
├── nanoels-emulator.js                # Offline physics & kinematics simulator (~500 lines)
└── dev/
    ├── lathe-viz.js                   # Digital twin SVG canvas renderer (~450 lines)
    └── lathe-viz-profiles.js          # Machine geometry profile library (~150 lines)
```

### Module Responsibilities:

| File | Primary Role | Architectural Invariants |
| :--- | :--- | :--- |
| **`nanoels-service.js`** | **Transport & Network Adapter** | Manages WebSocket connection lifecycle, exponential reconnect backoff, backend routing (`mock` vs `bridge` vs `live`), command queuing, and promise-based settings transactions. |
| **`nanoels-state.js`** | **Authoritative Reactive Store** | Implements `Alpine.store('machine')`. Ingests line-oriented domain telemetry, handles client-local presentation preferences, and triggers reactive DOM updates with zero DOM thrashing. |
| **`nanoels-formatters.js`** | **Pure Presentation Math** | Pure functions converting integer deci-microns into formatted strings (`duToMm`, `duToInch`, `formatFeed`, `formatSurfaceSpeed`). Zero state mutations. |
| **`nanoels-presets.js`** | **Machining Knowledge Base** | Immutable lookup tables for ISO Metric, Unified (UNC/UNF/UNEF), Pipe (NPT/BSPT) threads, and standard machine tapers (Morse, Brown & Sharpe, R8, ISO). |
| **`lathe-controller.js`** | **Ergonomics & Wizard Controller** | Manages calculator keypad buffer, conversational threading/turning wizards, settings modal, and sub-300ms D-pad gesture timing loops. |
| **`nanoels-emulator.js`** | **Offline Simulator Engine** | Full software model of the ESP32 motion controller. Emulates stepper timing, encoder ticks, soft limits, and tool kinematics for offline browser testing. |
| **`lathe-viz.js`** | **Digital Twin Visualizer** | Passive reactive SVG renderer that ingests canonical coordinates and draws the lathe bed, carriage, toolpost, and workpiece to true physical scale. |

---

## 3. Client-Side Transport Layer (`nanoels-service.js`)

The Web UI communicates with the controller via `NanoElsService`, which provides an event-driven network abstraction:

```
[ Web UI View Logic ] 
        ▲
        │ (Events: 'telemetry', 'connected', 'status')
        ▼
[ NanoElsService (nanoels-service.js) ]
        │
   ┌────┴───────────────────────────┬───────────────────────────────┐
   ▼                                ▼                               ▼
[ Live WebSocket Client ]   [ Local Serial Bridge ]     [ Software Emulator ]
(ws://<esp32-ip>:81/canonical)  (ws://127.0.0.1:8765)    (nanoels-emulator.js)
```

### 3.1 Backend Selection & Switching
The client dynamically switches between three backends at runtime via `#devBackendSelect`:
* **`'emulator'` (Default):** Connects to `window.nanoelsEmulator` in-memory. Zero network requirements.
* **`'bridge'`:** Connects to `ws://127.0.0.1:8765/` (local USB-UART bridge for bench testing).
* **`'live'`:** Connects to `ws://<host>:81/canonical` on the physical ESP32-S3 controller.

### 3.2 Transient EMI & Network Resilience
Machine shop electrical noise (spindle VFD switching, contactors) can induce transient WiFi frame drops:
* **Exponential Backoff Reconnect:** On disconnect, retries back off progressively ($1\text{s} \to 2\text{s} \to 4\text{s} \to 8\text{s} \to 15\text{s}$ ceiling).
* **State Resynchronization (Hydration):** Upon successful socket reconnection, the client automatically dispatches:
  ```text
  CMD.get=K,I,C,P\n
  ```
  This immediately repopulates all 4 domains in a single atomic snapshot, preventing stale UI state.

---

## 4. Inbound Telemetry Ingestion & Stream Translation

### 4.1 Telemetry Parsing Pipeline
The controller pushes line-oriented ASCII key-value telemetry. `nanoels-service.js` parses incoming messages into typed state updates:

```javascript
// nanoels-service.js inbound line parsing
handleIncomingMessage(line) {
  const [key, val] = line.trim().split('=');
  if (!key || val === undefined) return;

  const [domain, prop] = key.split('.');
  switch (domain) {
    case 'K': this.updateKinematics(prop, val); break;
    case 'I': this.updateIntent(prop, val); break;
    case 'C': this.updateCycle(prop, val); break;
    case 'P': this.updateProperties(prop, val); break;
    case 'SETTING': this.resolveSettingPromise(prop, val); break;
  }
}
```

### 4.2 Legacy Compatibility & Inbound Filtering Invariants
1. **Filtering Legacy Diameter (`dia`):** Inbound telemetry containing `dia` or `isDiameterMode` (emitted by legacy controllers or physical Nextion LCDs) is explicitly **ignored**. Cross-slide display mode is strictly client-local.
2. **Soft Limit Sanitization:** Unset limits (`LONG_MAX = 2147483647`, `LONG_MIN = -2147483648`) are sanitized before formatting to guarantee zero `NaN` or overflow displays on limit chips.
3. **Continuous Spindle Telemetry:** Floating-point spindle turns and angles are ingested directly into `$store.machine.turns` and `$store.machine.angle` without integer truncation.

---

## 5. Reactive State Architecture & Boundary Contract

To achieve zero display lag, 100% visual fidelity, and exact hardware gesture timing on low-power touchscreens, the Web UI enforces a strict architectural boundary between Alpine.js and Vanilla JS:

```
[ Inbound Telemetry Frame ]
            │
            ▼
┌──────────────────────────────────────┐
│ Alpine.js Store ($store.machine)     │
├──────────────────────────────────────┤
│ Live Coordinates: zDisplay, xDisplay │
│ Spindle Dynamics: spindleDisplay     │
│ Limit Chips: zLeftDisplay, etc.      │
│ Status Bar & Unit Indicators         │
└──────────────────────────────────────┘
            │ (Reactive Getters)
            ▼
   [ Live DRO Display DOM ]
```

```
[ Operator Touch Event (Keypad, Wizard, D-Pad) ]
            │
            ▼
┌──────────────────────────────────────┐
│ Vanilla JS (lathe-controller.js)     │
├──────────────────────────────────────┤
│ 1. Evaluates Motion Lockout Guard    │
│ 2. Computes Input Math (Dia -> Rad)  │
│ 3. Dispatches Wire Command to MCU    │
│ 4. Updates Alpine Store Imperatively │
└──────────────────────────────────────┘
            │
            ▼
[ nanoelsService.send('CMD....') ]
```

### The Boundary Rules:
1. **Alpine.js (`$store.machine`) Exclusively Owns:**
   * DRO coordinate column ($Z$, $X$, and Spindle readouts).
   * Limit distance countdown chips (`|←`, `→|`, `⤒`, `⭳`).
   * Top bar status badges and system unit indicators (`unitStr`).
2. **Vanilla JS (`lathe-controller.js`) Exclusively Owns:**
   * Modal dialog overlays (`#axisOverlay`, `#limitOverlay`, `#settingsOverlay`, `#docOverlay`).
   * Conversational wizard flows (Threading, Turning, Facing).
   * Numeric calculator keypad buffer, math keys (`÷ 2`), and cursor positioning.
   * Hardware jog touch timing (tap $<300\text{ms}$ for single step vs hold $\ge 300\text{ms}$ for continuous feed).
3. **The Strict Unidirectional Rule:**
   * Vanilla JS **NEVER imperatively writes innerText or textContent to DRO readouts** (`#zValue`, `#xValText`, `#spindleValue`, limit chips).
   * All DRO text updates flow strictly through Alpine's reactive getters.

---

## 6. Client-Side Presentation & Input Translation

### 6.1 Coordinate Translation Math
* **Metric Display:** $\text{displayMm} = \text{du} / 10{,}000.0$
* **Imperial Display:** $\text{displayIn} = \text{du} / 254{,}000.0$
* **Diameter Doubling:** When `$store.machine.dia` is true:
  $$\text{displayDia} = \frac{\text{xDu} \times 2.0}{10{,}000.0} \quad (\text{or } \frac{\text{xDu} \times 2.0}{254{,}000.0})$$

### 6.2 Keypad Input Halving (Diameter $\to$ Canonical Radius)
When an operator keys in a workpiece diameter or infeed target in Diameter mode:
1. The operator enters target diameter $D_{\text{target}}$ in millimeters or inches.
2. Vanilla JS converts entered diameter to canonical radius: $R = D_{\text{target}} / 2.0$.
3. Converts to integer deci-microns: $\text{xDu} = \text{round}(R \times 10{,}000.0)$.
4. Transmits canonical radius: `CMD.axisPosDu=X,<xDu>`.
5. Updates local store: `$store.machine.xDu = xDu`.
* The controller motion engine is never exposed to diameter scaling.

---

## 7. Interactive Control & Wire Interaction Ledger

The complete mapping of every button, touch gesture, keypad entry, and modal trigger on the Web UI to its JavaScript handler, wire command, and MCU kinematic effect is maintained authoritatively in:

👉 **[`INTERACTION_LEDGER.md`](./INTERACTION_LEDGER.md)**

---

## 8. Development Tooling Integration

### 8.1 In-Browser Physics Simulator (`nanoels-emulator.js`)
* Fully emulates the ESP32 motion controller inside the browser tab.
* Simulates stepper step generation, spindle encoder quadrature PCNT pulses, limit register trips, and thread helix synchronization.
* Allows complete end-to-end testing of wizards, keypad interactions, and UI workflows without physical hardware.

### 8.2 Real-Scale Digital Twin Visualizer (`lathe-viz.js`)
* A pure reactive SVG canvas engine docked beneath the primary controller face.
* Subscribes to `window.nanoelsClient`'s `'telemetry'` event.
* Ingests canonical deci-micron positions (`zDu`, `xDu`) and draws the lathe bed casting, chuck, workpiece, toolpost, and soft limit envelopes to true physical millimetric scale.
* Operates strictly on canonical millimeters ($z\text{Du}/10000$, $x\text{Du}/10000$), completely immune to client-side display preferences ($\varnothing/R$, $\text{mm/in}$).
