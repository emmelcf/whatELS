# ADR 0005: Protocol Rationalization, Subsystem 5 Decoupling, and Core Command Service

* **Status:** Accepted
* **Date:** October 28, 2026
* **Authors:** Emil & Antigravity
* **Deciders:** Emil & Antigravity
* **Context:** Decommissioning legacy `?` queries, eliminating `WebServerModule.cpp`, rationalizing wire namespaces into 5 domains, formalizing atomic canned cycles, and enforcing compile-time motion safety boundaries.
* **Supercedes:** Legacy query routing in `CommandDispatcher.cpp` and monolithic server routing in `WebServerModule.cpp`.
* **Complements:** [ADR 0002: Command Surface Rationalization](./0002-command-surface-rationalization-and-dumb-client-decoupling.md), [ADR 0003: Command Result & Baseline Client Architecture](./0003-command-result-and-baseline-client-architecture.md), and [ADR 0004: Pluggable Transports](./0004-pluggable-transports-and-webserial.md).

---

## 1. Context & Problem Statement

The whatELS firmware inherited two major architectural bottlenecks from early monolithic Arduino sketches (`h5_monolithic.ino`):

1. **`CommandDispatcher.cpp` Conflation:**  
   `CommandDispatcher.cpp` served simultaneously as a text tokenizer, ad-hoc query inspector (`?sysinfo`, `?state`, `?get`), `[SAFE-01]` in-motion safety gatekeeper, and modal parameter staging cache (`CMD.feedMm`, `CMD.gearAxis`). This leaked string parsing directly into core motion safety and allowed client adapters to bypass safety checks.

2. **`WebServerModule.cpp` Monolith (979 lines):**  
   `WebServerModule.cpp` entangled four distinct concerns in a single file:
   * Wi-Fi AP/STA network lifecycle and mDNS.
   * HTTP Port 80 file serving, LittleFS asset delivery, and OTA firmware updates.
   * WebSocket Port 81 client transport.
   * Uncoordinated telemetry pushes that bypassed `ClientManager`.

3. **Incoherent Wire Surface & Ghost Limits:**  
   Clients relied on ad-hoc `?` queries (`?sysinfo`, `?state`, `?get`). Canned cycles lacked atomic initiation, feed hold radial retract (`CMD.hold`), and single-pass stepping (`CMD.step`), while temporary cycle envelopes created "ghost limits" that confused lathe operators upon cycle completion.

---

## 2. Decision Details: The 5 Non-Negotiable Pillars

### Pillar 1: Strict 6-Namespace Grammar Matrix
The external wire surface is rationalized into six distinct namespaces:
1. **`CMD` (Physical Motion Intents):** `CMD.startFeed`, `CMD.startCycle`, `CMD.hold`, `CMD.resume`, `CMD.step`, `CMD.stop`, `CMD.jogStep`, `CMD.jogHold`, `CMD.jogRelease`, `CMD.axisZero`, `CMD.axisPosDu`, `CMD.axisLimitDu`, `CMD.axisClearLimit`, `CMD.ping`, `CMD.simEncoder`, `CMD.simRpm`, `CMD.simPulses`.
2. **`STATE` (Machine Dynamic State):** `STATE.get[=<domains>]` (e.g. `STATE.get=ALL` or `STATE.get=K,I`).
3. **`SETTINGS` (User Preferences & Machine Config):** `SETTINGS.get[=<path>]`, `SETTINGS.set=<key>:<val>`, `SETTINGS.reset`, `SETTINGS.presets`, `SETTINGS.schema`.
4. **`PROPERTIES` (Machine Iron Invariants & Profiles):** `PROPERTIES.get[=<path>]`, `PROPERTIES.modes`, `PROPERTIES.profiles`.
5. **`SYS` (Appliance & Host Diagnostics):** `SYS.get[=<path>]` (e.g. `SYS.get=info`, `SYS.get=health`).
6. **`CLIENT` (Session & Capability Handshake):** `CLIENT.hello=type:<profile>,ver:<version>,label:"<friendly_label>"`.

**Clean Break Rule:** All legacy `?` queries (`?sysinfo`, `?state`, `?get`, and bare `?`) are completely deleted with zero deprecation cruft. Any attempt to send a `?` query receives `ERR:UNKNOWN_COMMAND`.

### Pillar 2: Subsystem 5 Architectural Partitioning
We cleanly decouple text parsing from domain execution:
* **`StandardCommandParser` (`src/core_client/`):** Pure string tokenizer. Tokenizes the wire text into strongly-typed C++ command structs. Has zero motion execution knowledge. Hardware diagnostics (`SYS.get`) query an injected `ISystemInfoHAL` seam, isolating bare-metal ESP32 calls from parser logic.
* **`CoreCommandService` (`src/core/`):** Pure C++ domain service. Receives typed command structs (`StartFeedCmd`, `JogStepCmd`, `AxisLimitCmd`, etc.) and the calling `ClientContext`. Enforces the authoritative `[SAFE-01]` in-motion safety gate. Has zero string parsing.
* **Compile-Time Safety Gate:** `MotionEngine` declares `friend class whatels::core::CoreCommandService;` while keeping sensitive stepping methods inaccessible to arbitrary callers.

### Pillar 3: Progressive Decomposition of `WebServerModule.cpp` (In-Progress)
The 979-line `WebServerModule.cpp` monolith is being decomposed into three single-responsibility classes:
1. **`WifiService` (`src/core/WifiService.h/.cpp`):** AP/STA connection state machine, exponential-backoff reconnect watchdog (3s to 30s), disabled modem sleep for real-time telemetry, and mDNS responder (`whatels.local`).
2. **`HttpServer` (`src/core/network/HttpServer.h/.cpp`):** Dedicated Port 80 HTTP server for web assets, transparent `.gz` compression, SPA routing, and multipart OTA updates (`POST /firmware/upload`). *(Note: `WebServerModule.cpp` remains temporarily active until embedded `PROGMEM` asset serving is migrated into `HttpServer.cpp`).*
3. **`WebSocketTransport` (`src/core_client/transports/`):** Dedicated Port 81 client transport implementing `ITransport` on `/canonical`. Status telemetry is managed exclusively by `ClientManager::broadcastIfChanged()` at the 10 Hz global cadence.

### Pillar 4: Formal Cycle Start Contract & Zero Ghost Limits
* **Atomic Single-Frame Initiation:** Canned cycles are initiated atomically in a single frame via `CMD.startCycle=mode:<type>,<key1>:<val1>,...` using order-independent key-value tokens.
* **Mode Layer Implementation (Refs Issue #10):** Multi-pass canned cycles (`Threading`, `Turning`, `Facing`, `Taper`, `Cutoff`) are implemented as `IELSMode` classes in `src/motion/modes/` registered with `ModeRegistry`. Until each cycle mode is implemented, `CMD.startCycle` and `CMD.step` deterministically return `UNAVAILABLE`.
* **In-Flight Lifecycle:** 
  * `CMD.hold`: Pauses active feed while retaining spindle sync and latches state to `CYCLE_HOLD` / `PAUSED`.
  * `CMD.resume`: Resumes feed from `CYCLE_HOLD`.
  * `CMD.step`: When `stepMode` is active, the engine completes a pass, returns to `zStart`, latches `STEP_WAIT`, and waits for `CMD.step` to fire the next pass.
  * `CMD.stop`: Universal emergency stop.
* **Zero Ghost Limits Invariant (ADR-15):** The cycle's shoulder limit (`zEnd`) is written into the active soft limit table (`z_min`/`z_max`) and persists after cycle completion. There are zero hidden limits.

### Pillar 5: Two-Way Verified Client Handshake
A client must never enable motion controls or report "Connected" based solely on transport link open:
1. Client opens transport and enters `HANDSHAKE_PENDING` with motion controls locked.
2. Client sends `CLIENT.hello=type:<profile>,ver:<version>,label:"<friendly_label>"`.
3. Controller validates profile, assigns authoritative `session_id`, and responds:
   `OK:CLIENT.hello:session_id=...,type=...,controllerVer=...,perms=...`
4. Only upon parsing this ACK does the client transition to `CONNECTED` and unlock controls.
5. Backed by a 2000 ms watchdog timer to fail safe on hung firmware.

---

## 3. Consequences

### Positive
* **Zero Ambiguity:** Eliminates the dual query/command dialects; all interactions follow five standard namespaces.
* **Compile-Time Safety:** `CoreCommandService` guarantees that external client layers cannot bypass the `[SAFE-01]` motion safety gate.
* **Modular Infrastructure:** Port 80 web traffic and Wi-Fi state machines are completely isolated from Port 81 real-time motion control.
* **Operator Safety:** Tool rubbing on hold is prevented via automatic radial retract, and finished shoulders remain protected via persistent limits.
* **Deterministic Hydration:** Two-way verified handshake prevents "half-open" UI phantom control states.

### Negative / Trade-offs
* All client software (`whatels-service.js`, `whatels-emulator.js`, HIL test harnesses) must be updated to implement the `CLIENT.hello` handshake and remove legacy `?` queries.
