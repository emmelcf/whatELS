# ADR 0004: Pluggable Client Transports, W3C WebSerial Support, and Decoupled Controller SDK

* **Status:** Accepted  
* **Date:** October 26, 2026  
* **Authors:** Emil & Antigravity  
* **Deciders:** Emil & Antigravity  
* **Context:** Modularization of the client communication stack, W3C WebSerial transport implementation for latency-critical wired machine control, decoupled reusable Controller SDK, and firmware dual-core serial concurrency.  
* **Complements:**
  - [ADR 0001: Consolidated Motion Invocation, Cartesian Polarity, and Kinematic Kernel Refactoring](./0001-motion-invocation-and-gearbox-consolidation.md)
  - [ADR 0002: Command Surface Rationalization and Decoupling of Dumb Client State](./0002-command-surface-rationalization-and-dumb-client-decoupling.md)
  - [ADR 0003: CommandResult Domain Object and Baseline Native Client Architecture](./0003-command-result-and-baseline-client-architecture.md)
  - [whatELS Client Architecture Specification](../clients/ELS_CLIENT_ARCHITECTURE.md)
  - [whatELS Web UI Overview & Specification](../clients/web_ui/OVERVIEW.md)

---

## 1. Context & Problem Statement

The whatELS master architecture mandates a **Zero-WiFi Control Plane** (`ELS_CLIENT_ARCHITECTURE.md`, Tenet 6) for safety-critical cutting operations in high-EMI machine shop environments (spindle VFD switching, motor contactor arcing). Wireless Wi-Fi/WebSocket is reserved for supervision, settings, and readouts, while wired physical links (USB CDC, hardware UART) provide deterministic, jitter-free operator control.

However, the existing client and firmware implementations exhibited three key architectural limitations:

1. **Coupled & Monolithic Client Communication**:  
   In `clients/web_ui/whatels-service.js`, WebSocket connections and the in-browser physics emulator were tightly coupled inside the service class. There was no modular transport abstraction, making it impossible to add new transports (such as W3C WebSerial) without rewriting connection lifecycles, heartbeat watchdogs, and settings transaction management.

2. **Absence of a Reusable Controller SDK**:  
   Command formatting, response parsing, and safety heartbeat loops were entangled with the Web UI presentation layer. External clients (automated test harnesses, CLI tools, Electron desktop wrappers, or secondary displays) had no clean, language-level SDK to interact with the controller.

3. **Incomplete Firmware Client Endpoint Registration**:  
   While `ELS_CLIENT_ARCHITECTURE.md` Section 3 specified `ClientEndpoint = Transport + Serializer + Translator`, the firmware `ClientManager` (`src/core_client/ClientManager.h`) only instantiated fixed WebSocket slots. No `SerialTransport` was registered on the ESP32 to receive, parse, and respond to canonical commands over USB CDC or UART.

4. **USB CDC Logging Collision**:  
   On the ESP32-S3, the native USB CDC port (`Serial`) was shared between ESP-IDF diagnostic log output (`ESP_LOGI`) and potential serial communications. Without dedicated routing, debug logs would collide with the machine command and telemetry stream.

---

## 2. Decision Details & System Architecture

### 2.1 Three-Tier Client Decomposition (Transport $\to$ SDK $\to$ Presentation)

The client architecture is decoupled into three strictly isolated tiers:

```
┌────────────────────────────────────────────────────────────────────────┐
│ Tier 3: Presentation & View Logic (clients/web_ui/)                    │
│ - lathe-controller.js (Wizards, Keypad, Modal Dialogs)                 │
│ - whatels-state.js    (Alpine.store('machine') Reactive Formatters)     │
│ - whatels-service.js  (UI Adapter bridging WhatelsClient to DOM)       │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ Method Calls & Domain Events
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│ Tier 2: Controller Protocol SDK (clients/sdk/js/whatels-client.js)     │
│ - Strongly-typed command surface (startFeed, stop, jogHold, axisZero)  │
│ - Promise-based request-response matching (OK:CMD... vs ERR:...)        │
│ - 200ms Safety Heartbeat Watchdog for continuous jogs                  │
│ - Line-oriented telemetry parsing into domain snapshots (K, I, C, P)   │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ send(line) & onLine(callback)
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│ Tier 1: Pluggable Transports (clients/sdk/js/transports/)              │
│ - ClientTransport (Abstract Base Contract)                             │
│   ├── WebSocketTransport (ws://host:81/canonical + exponential retry)  │
│   ├── WebSerialTransport (W3C navigator.serial + newline chunking)    │
│   └── EmulatorTransport  (In-browser whatels-emulator.js bridge)      │
└────────────────────────────────────────────────────────────────────────┘
```

#### Multi-Language Client SDK Layout:
To avoid the "shared junk drawer" anti-pattern and provide a clean distribution boundary, the SDK is housed under `clients/sdk/` partitioned by development language (`js/`, with explicit paths reserved for future `cpp/` pendants and `python/` tooling):
```text
clients/
├── sdk/
│   ├── js/                           # JavaScript Client SDK
│   │   ├── transports/
│   │   │   ├── client-transport.js   # Base ITransport interface
│   │   │   ├── websocket-transport.js# WebSocket client implementation
│   │   │   ├── webserial-transport.js# W3C WebSerial implementation
│   │   │   └── emulator-transport.js # Offline physics emulator bridge
│   │   └── whatels-client.js         # Reusable Controller SDK & Protocol Gateway
│   ├── cpp/                          # Reserved for future C++ Client SDK (pendant/SBC)
│   └── python/                       # Reserved for future/unified Python Client SDK
└── web_ui/
    ├── lathe_dro_whatels_controller.html
    ├── whatels-service.js            # UI Service adapting WhatelsClient to Alpine
    ├── whatels-state.js              # Reactive 4-domain presentation store
    └── lathe-controller.js           # Ergonomics & wizard controllers
```

---

### 2.2 W3C WebSerial Transport & Reconnection Lifecycle

The WebSerial transport communicates over USB CDC or UART serial using the browser's native `navigator.serial` API.

#### Security & Permissions Lifecycle:
1. **First-time Pairing (Explicit User Gesture)**:
   Per W3C security policy, opening an ungranted serial device requires an explicit user gesture. The operator clicks "Connect USB Serial" in the Dev Console or Connection Settings modal, triggering `navigator.serial.requestPort()`.
2. **Silent Auto-Reconnect on Reload**:
   Granted ports are remembered by the browser sandbox. On subsequent page loads, `WebSerialTransport.connect()` queries `navigator.serial.getPorts()`. If an authorized port is present and WebSerial is the active backend in `localStorage`, the client opens the port and streams immediately without prompting the operator.
3. **Stream Framing**:
   WebSerial byte streams are decoded via `TextDecoderStream` and chunked via a newline transformer (`TransformStream`) so the transport emits clean, unchunked ASCII lines (`\n`) to the SDK.

---

### 2.3 Wire Protocol Parity (Canonical Line-Oriented Text)

WebSerial and WebSocket use the **exact same canonical text grammar**:
* **Commands**: `CMD.startFeed=axis:Z,sync:1,feed:1500,dir:-1\n`, `CMD.stop\n`, `STATE.get=ALL\n`
* **Telemetry**: `K.zDu=125400\n`, `K.rpm=620\n`, `I.thPitch=1.5\n`
* **Handshake**: `CLIENT.hello=type:webui,ver:1.0.0,label:"Web UI"\n`

Zero protocol translation is performed for native clients (`NativeClient`). WebSerial, WebSocket, and serial pendants share 100% of telemetry formatting in `TelemetrySerializer` and command parsing/dispatch in `StandardCommandParser` and `CoreCommandService`.

---

### 2.4 Firmware Dual-Core Serial Concurrency & Build Toggle

To satisfy real-time safety invariants (`docs/motion/CONCURRENCY_AND_TIMING.md`):

1. **Core 0 Execution for Serial I/O**:
   `SerialTransport` runs strictly on **Core 0** (as part of the non-real-time communications loop). Inbound serial lines are buffered and fed into `NativeClient`. Validated commands post packets to Core 1 via the FreeRTOS queue `m_cmdQueue`. Real-time motion stepping on Core 1 (priority 22) is never blocked by USB CDC or UART operations.
2. **Firmware Logging vs. Protocol Stream Routing (`ENABLE_WEBSERIAL_CDC`)**:
   - **Default (`ENABLE_WEBSERIAL_CDC=0`)**: Native USB CDC `Serial` carries `ESP_LOGI` debug output for single-cable bench development.
   - **WebSerial Mode (`ENABLE_WEBSERIAL_CDC=1`)**: Native USB CDC `Serial` is dedicated 100% to the binary-clean `NativeClient` protocol stream. `ESP_LOG` debug output is routed to the hardware MISC UART pins (`DEBUG_UART_TX_PIN 10`, `DEBUG_UART_RX_PIN 11` at 115,200 baud).

---

### 2.5 Web UI Component Architecture & Transport Configuration

The Web UI client formally establishes three distinct component tiers:
1. **Primary Control Interface**: The production Touch DRO and conversational lathe controller (`800x480` landscape up to full tablet display).
2. **Dev Console (Optional Dev Drawer)**: A collapsible bottom drawer for rapid backend toggling (`Emulator` vs `WebSocket` vs `WebSerial`), real-time packet inspection, and simulation controls. Available strictly during development.
3. **Lathe Visualizer (Optional Digital Twin)**: The SVG real-time physical simulation visualizer (`dev/lathe-viz.js`).

In production (when the Dev Console is hidden), the active transport (`WebSocket` vs `WebSerial`) and communication parameters (host IP, baud rate) are configured in a dedicated **Connection Settings Section** of the main Settings Modal (`#modalSettings`) and persisted to browser `localStorage`.

---

## 3. Consequences & Verification

### Positive:
- **True Wired Low-Latency Control**: Operators can run full machine control over a physical USB cable, eliminating shop EMI interference.
- **Reusable SDK**: Future clients (Electron apps, Python test scripts, Node.js tooling) can consume `clients/sdk/js/whatels-client.js` without copying protocol code.
- **Zero Duplication**: One canonical wire protocol across WebSerial, WebSocket, and serial pendants.
- **Deterministic Teardown**: Runtime transport switching cleanly cancels active safety loops and pending requests.

### Compliance:
- Verified against `docs/clients/ELS_CLIENT_ARCHITECTURE.md` (Subsystems 5 & 6).
- Obeying `docs/motion/CONCURRENCY_AND_TIMING.md` (Core 0 / Core 1 real-time isolation).

