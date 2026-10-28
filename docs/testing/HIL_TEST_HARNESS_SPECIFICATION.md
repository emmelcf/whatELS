# whatELS Hardware-in-the-Loop (HIL) & WebSocket Test Harness Specification

> **Status:** Authoritative Specification & Implementation Blueprint (v2.0)  
> **Target Subsystems:** Subsystem 1 (HAL Drivers), Subsystem 2 (Motion Engine), Subsystem 3 (Unified Gearbox Mode), Subsystem 5 (Command Dispatcher), Subsystem 6 (Client Adapters & Testing)[cite: 1, 2, 5]  
> **Master Blueprint:** [`NANOELS_MASTER_ARCHITECTURE.md`](../NANOELS_MASTER_ARCHITECTURE.md)[cite: 1]  
> **Mode Architecture:** [`../ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md)[cite: 2, 3]  
> **Client Specification:** [`../clients/ELS_CLIENT_ARCHITECTURE.md`](../clients/ELS_CLIENT_ARCHITECTURE.md)[cite: 1]  
> **Motion Engine Spec:** [`../motion/MOTION_ENGINE_SPECIFICATION.md`](../motion/MOTION_ENGINE_SPECIFICATION.md)[cite: 1]  
> **Date:** October 2026[cite: 1]  
> **Target Platform:** Host Python 3.10+ (WSL/Linux/macOS) $\longleftrightarrow$ ESP32-S3 Firmware via WebSocket (:81/canonical) or Serial UART[cite: 1]  

---

## 1. Executive Summary & Objective

This specification defines the architecture, wire protocols, firmware behavioral invariants, test authoring DSL, and simulation hooks for the **whatELS Hardware-in-the-Loop (HIL) Test Harness**[cite: 1].

While unit tests (`test/test_motion_e2e.cpp`) verify kinematic algorithms in synthetic isolation on the host, this harness verifies the **complete, live compiled firmware** running on physical ESP32-S3 hardware across the external network boundary (`ws://<ip>:81/canonical`)[cite: 1]. It treats the controller as a black box, verifying[cite: 1]:
1. Inbound network framing, sanitization, In-Motion Safety Gatekeeper (`[SAFE-01]`), and command dispatching on Core 0[cite: 1, 4].
2. Inter-core command queue handoff from Core 0 to Core 1[cite: 1].
3. Real-time deterministic kinematics, trajectory planning, 2D Bresenham multi-axis interpolation, and stepper step generation on Core 1[cite: 1, 2, 5].
4. Tear-free lock-free telemetry sampling ([`TelemetryMailbox`](../../src/motion/contract/TelemetryMailbox.h))[cite: 1].
5. 50 Hz outbound WebSocket serialization and command `OK:`/`ERR:` acknowledgment framing back to the client[cite: 1].

To test spindle-synchronized motion without requiring physical lathe rotation or external pulse generator hardware, the specification establishes a **Firmware Simulation Injection Hook** within the Hardware Abstraction Layer (HAL)[cite: 1].

```
┌────────────────────────────────────────────────────────────────────────┐
│                        END-TO-END HIL TOPOLOGY                         │
├───────────────────────────────────┬────────────────────────────────────┤
│ HOST ENVIRONMENT (Python Test)    │ ESP32-S3 TARGET CONTROLLER         │
│                                   │                                    │
│  tests/hil/run_tests.py           │  Core 0 (Networking & Dispatch):   │
│   ├── TestRunner / CLI Filter     │   ├── WebServerModule (:81/ws)     │
│   ├── TestSuites (26 Tests)       │   ├── CommandDispatcher.cpp        │
│   └── WhatElsClient (Async WS)    │   │    └── [SAFE-01] Gatekeeper    │
│        ├── 500ms Heartbeat Task   │   └── MachineModel / Serializer    │
│        └── ACK / ERR Synchronizer │            │ (Inter-core Queue)    │
│            │                      │            ▼                       │
│            │  ws://<ip>:81        │  Core 1 (Real-Time Motion):        │
│            │  /canonical          │   ├── MotionEngine (prio 22)       │
│            ▼                      │   ├── Esp32SpindleEncoderHAL       │
│      [ Outbound Commands ] ───────┼──►│    └── Slewed Virtual Pulses   │
│       CMD.startFeed=...           │   ├── Esp32StepperHAL (GPTimer)    │
│       CMD.simPulses=9600          │   └── TelemetryMailbox (Seqlock)   │
│                                   │            │                       │
│      [ OK:/ERR: & 50Hz State ] ◄──┼────────────┘                       │
│       OK:CMD.startFeed            │                                    │
│       K.zDu=-15000, K.on=0        │                                    │
└───────────────────────────────────┴────────────────────────────────────┘
```

---

## 2. Firmware Simulation Injection Contract & Protocol Invariants

To execute automated tests against physical hardware sitting on a developer's bench without mechanical spindle rotation, the firmware provides a thread-safe software injection bridge directly into `ISpindleEncoderHAL`[cite: 1].

### 2.1 Virtual Spindle Encoder HAL Interface

`ISpindleEncoderHAL` is extended with non-blocking virtual injection primitives[cite: 1]:

```cpp
// src/motion/hal/ISpindleEncoderHAL.h

class ISpindleEncoderHAL {
public:
    virtual ~ISpindleEncoderHAL() = default;

    // ... existing hardware methods: init, getPulseDelta, getAccumulatedPulses, getFilteredRpm ...

    // --- Simulation & Test Injection Hooks ---
    
    /**
     * @brief Toggles virtual injection mode.
     * @param active When true, getPulseDelta() serves injected virtual pulses instead of PCNT pins.
     *               State transition cleanup (PCNT zeroing & integrator reset) is executed
     *               synchronously on Core 1 inside getPulseDelta() to prevent cross-core races.
     */
    virtual void setSimulationActive(bool active) = 0;

    /**
     * @brief Checks if virtual injection mode is currently active.
     */
    virtual bool isSimulationActive() const = 0;

    /**
     * @brief Injects an exact discrete pulse delta.
     * @param delta Signed pulse count (e.g. 2 * effectivePpr = 1 spindle rev in forward direction).
     *              Accumulates atomically via fetch_add; metered out at a safe maximum slew rate
     *              across subsequent getPulseDelta() calls so velocity limits are not violated.
     */
    virtual void injectSimulatedPulses(int32_t delta) = 0;

    /**
     * @brief Sets a continuous simulated spindle RPM.
     * @param rpm Signed rotational speed in RPM (+ forward / - reverse). 0.0 = stopped.
     */
    virtual void setSimulatedRpm(float rpm) = 0;
};
```

### 2.1.1 Concrete `Esp32SpindleEncoderHAL` Implementation (Thread-Safe & Slewed)

Because `CommandDispatcher.cpp` executes on Core 0 while `getPulseDelta()` executes inside the high-priority real-time motion tick on Core 1[cite: 1], the implementation enforces strict cross-core safety:
1. **Core 1 State Transition Ownership:** Core 0 only flips `m_simActive`. Core 1 detects edges (`simActive != m_wasSimActive`) to reset `m_lastSimTimeUs`, `m_simFraction`, and the hardware PCNT baseline without race conditions.
2. **Discrete Pulse Slew Limiting:** When `CMD.simPulses=9600` is injected[cite: 1], dumping all $9600$ counts into a single $1\text{ ms}$ tick would simulate an instantaneous $\sim 120{,}000\text{ RPM}$ spike, overflowing stepper timer frequencies and trajectory acceleration limits. Instead, `m_pendingSimPulses` is drained at a realistic maximum chunk per tick (equivalent to $600\text{ RPM}$).

```cpp
// Member variables in Esp32SpindleEncoderHAL:
std::atomic<bool>     m_simActive{false};
std::atomic<float>    m_simRpm{0.0f};
std::atomic<int32_t>  m_pendingSimPulses{0};

// Owned and mutated exclusively by Core 1 inside getPulseDelta():
bool                  m_wasSimActive{false};
int64_t               m_lastSimTimeUs{0};
float                 m_simFraction{0.0f};
```

**Pulse Integration & Slew Math in `Esp32SpindleEncoderHAL::getPulseDelta()`:**

```cpp
void Esp32SpindleEncoderHAL::setSimulationActive(bool active) {
    if (!active) {
        m_simRpm.store(0.0f, std::memory_order_relaxed);
        m_pendingSimPulses.store(0, std::memory_order_relaxed);
    }
    m_simActive.store(active, std::memory_order_release);
}

void Esp32SpindleEncoderHAL::injectSimulatedPulses(int32_t delta) {
    m_pendingSimPulses.fetch_add(delta, std::memory_order_relaxed);
}

void Esp32SpindleEncoderHAL::setSimulatedRpm(float rpm) {
    m_simRpm.store(rpm, std::memory_order_relaxed);
}

int32_t Esp32SpindleEncoderHAL::getPulseDelta() {
    bool simNow = m_simActive.load(std::memory_order_acquire);

    // Handle state transitions synchronously on Core 1
    if (simNow != m_wasSimActive) {
        m_wasSimActive = simNow;
        m_lastSimTimeUs = esp_timer_get_time();
        m_simFraction = 0.0f;
        if (!simNow) {
            // Transitioned from Sim -> Physical: re-zero hardware PCNT baseline
            resetHardwarePcntBaseline();
        }
    }

    if (simNow) {
        int64_t nowUs = esp_timer_get_time();
        float dt = (m_lastSimTimeUs > 0) ? static_cast<float>(nowUs - m_lastSimTimeUs) * 1e-6f : 0.0f;
        m_lastSimTimeUs = nowUs;
        if (dt > 0.05f) dt = 0.05f; // Clamp delta-time to 50ms ceiling

        // 1. Integrate continuous rotational RPM:
        float rpm = m_simRpm.load(std::memory_order_relaxed);
        float counts = (rpm / 60.0f) * m_countsPerSpindleRev * dt + m_simFraction;
        int32_t delta = static_cast<int32_t>(counts);
        m_simFraction = counts - static_cast<float>(delta);

        // 2. Drain discrete pulse injections at a safe maximum slew rate (600 RPM equivalent):
        int32_t pending = m_pendingSimPulses.load(std::memory_order_relaxed);
        if (pending != 0) {
            int32_t maxChunk = std::max<int32_t>(1, static_cast<int32_t>((600.0f / 60.0f) * m_countsPerSpindleRev * (dt > 0.0f ? dt : 0.001f)));
            int32_t step = std::clamp(pending, -maxChunk, maxChunk);
            m_pendingSimPulses.fetch_sub(step, std::memory_order_relaxed);
            delta += step;
        }

        m_accumulatedPulses += delta;
        updateRpmEstimate(delta, nowUs);
        return delta;
    }

    // ... standard physical PCNT hardware sampling when simulation is inactive ...
}
```

---

### 2.2 Wire Commands (`CMD.sim*` & `CMD.ping`)

The test harness commands the simulation hooks using canonical text frames parsed by `CommandDispatcher.cpp`[cite: 1, 4]:

| Command Frame | Parameters | Description | Controller Response |
| :--- | :--- | :--- | :--- |
| `CMD.simEncoder=<0\|1>` | `0` or `1` | Enables or disables virtual encoder injection mode. Rejects with `ERR:SAFETY_INTERLOCK_ACTIVE` if physical spindle or axes are moving. | `OK:CMD.simEncoder\n`[cite: 1, 4] |
| `CMD.simPulses=<delta>` | Integer $\pm$ pulses | Atomically queues discrete spindle quadrature counts into Core 1 (slewed at $\le 600\text{ RPM}$). | `OK:CMD.simPulses\n`[cite: 1, 4] |
| `CMD.simRpm=<rpm>` | Float $\pm$ RPM | Commands a continuous spindle speed. The HAL integrates `delta = rpm / 60 * cpr * dt` on every motion tick. | `OK:CMD.simRpm\n`[cite: 1, 4] |
| `CMD.ping` | None | Refreshes the Core 1 simulation safety watchdog timestamp (`touchSimHeartbeat()`). | `OK:CMD.ping\n`[cite: 4] |

---

### 2.3 Safety Watchdog & Core 1 Failsafe Invariant

To guarantee that a disconnected or crashed test script never leaves physical steppers or simulated feeds running unattended[cite: 1]:
1. **32-Bit Atomic Heartbeat Registration:** On the 32-bit Xtensa LX7 (ESP32-S3), `m_lastSimHeartbeatMs` is stored as `std::atomic<uint32_t>` (wrapping safely via unsigned subtraction). Every `CMD.sim*` or `CMD.ping` command invokes `MotionEngine::instance().touchSimHeartbeat()`[cite: 1, 4]:
   ```cpp
   void touchSimHeartbeat() {
       uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
       m_lastSimHeartbeatMs.store(nowMs, std::memory_order_relaxed);
   }
   ```
2. **Core 1 Watchdog Evaluation:** Evaluated synchronously inside `MotionEngine::onMotionTick()` on Core 1[cite: 1]:
   ```cpp
   if (m_encoderHal && m_encoderHal->isSimulationActive()) {
       uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
       uint32_t lastMs = m_lastSimHeartbeatMs.load(std::memory_order_relaxed);
       if (static_cast<uint32_t>(nowMs - lastMs) > 1500U) {
           // Harness dropped offline or timed out: force immediate failsafe stop
           m_encoderHal->setSimulatedRpm(0.0f);
           m_encoderHal->setSimulationActive(false);
           stopAll(false); // Orderly normal deceleration stop
       }
   }
   ```
3. **Client Background Keepalive:** Because cutting moves regularly take $2.0\text{s}\text{–}5.0\text{s}$, `WhatElsClient` runs an asynchronous background task (`_heartbeat_loop`) that sends `CMD.ping\n` every $500\text{ ms}$ while simulation is active, unless explicitly paused via `client.suppress_heartbeat(True)` during watchdog timeout tests.

---

### 2.4 Firmware Protocol Invariants for HIL Execution

To ensure deterministic HIL execution and complete multi-axis test coverage, the firmware maintains the following core invariants:

1. **Consistent `OK:CMD.<name>\n` Acknowledgments Across All Handlers:**
   Every command dispatched through `CommandDispatcher` (`src/core/CommandDispatcher.cpp`) emits an immediate, deterministic acknowledgment (`OK:CMD.<name>\n` on success or `ERR:CMD.<name>=<reason>\n` on failure), allowing `WhatElsClient.send()` to await command completion deterministically before sampling telemetry.
2. **Dual-Axis `axis:ZX` Coordination in `handleStartFeed()`:**
   `CommandDispatcher` supports multi-axis feeds (`axis:ZX`, `axis:Z+X`, or `axis:TAPER`), activating `UnifiedGearboxMode` with dual-axis coordination enabled for synchronized conical threading and taper turning.
3. **Deterministic Soft Limit Mapping (`handleAxisLimitDu` & `handleAxisClearLimit`):**
   Semantic limit tokens (`L`/`R` for $Z$, `U`/`D` for $X$) and numeric indices (`0` vs `1`) map deterministically to `LimitDirection::LIMIT_LEFT_OR_UP` (`m_limits[0]`, negative travel stop) and `LimitDirection::LIMIT_RIGHT_OR_DOWN` (`m_limits[1]`, positive travel stop).
4. **Low-Latency Telemetry & Network Power Management:**
   Wi-Fi modem sleep is disabled (`WiFi.setSleep(false); esp_wifi_set_ps(WIFI_PS_NONE)`) on boot to prevent DTIM beacon latency spikes, ensuring 50 Hz telemetry frames and `CMD.stop` emergency packets arrive in sub-50 ms without buffering delays.

---

## 3. Test Harness Architecture & Python Framework

The test harness is implemented in Python 3.10+ using `asyncio` and `websockets` (`pip install websockets>=12.0`)[cite: 1].

### 3.1 Directory Layout

```
tests/hil/
├── requirements.txt               # websockets>=12.0
├── run_tests.py                   # Main test runner CLI and dynamic test discoverer
├── core/
│   ├── __init__.py
│   ├── client.py                  # WhatElsClient: Async WS, ACK sync, 500ms heartbeat & state mirror
│   ├── assertions.py              # Domain assertion helpers (assert_pos_du, assert_halted, etc.)
│   ├── base_test.py               # HilTestCase abstract base class
│   └── reporter.py                # ANSI Terminal & JSON reporting formatters
└── suites/
    ├── __init__.py
    ├── test_coords_and_presets.py # Axis coordinate zeroing, presets, and limit origin shift tests (1 test)
    ├── test_jogging.py            # Discrete step jogging and continuous hold/limit tests (2 tests)
    ├── test_gearing.py            # Discrete pulse-synchronized gearing accuracy tests (3 tests)
    ├── test_emergency.py          # Stop latency, [SAFE-01] gatekeeper, and watchdog tests (3 tests)
    ├── test_soft_limits.py        # Z and X soft limits, decel window, and directional escape (4 tests)
    ├── test_gearbox.py            # Single-axis Z and X sync/async, live nudge, TPI, and reversal (10 tests)
    └── test_gearbox_zx.py         # Coordinated Z+X taper/cone sync, async, and dual-limit halt (4 tests)
```

---

## 4. `WhatElsClient` Specification & State Engine

`WhatElsClient` manages the asynchronous WebSocket transport, command ACK synchronization, 500 ms simulation heartbeat, incoming 50 Hz packet deserialization, and predicate waiting[cite: 1].

### 4.1 Internal State Representation

```python
# tests/hil/core/client.py

@dataclass
class ControllerState:
    # Kinematics Domain (K.*)
    z_du: int = 0
    x_du: int = 0
    rpm: int = 0
    turns: float = 0.0
    angle: float = 0.0
    is_on: bool = False
    is_moving: bool = False
    mode: int = 0
    estop: int = 0
    perms: int = 0
    z_left_du: int | None = None
    z_right_du: int | None = None
    x_up_du: int | None = None
    x_down_du: int | None = None
    dupr: int = 0
    next_dupr: int = 0
    
    # Intent Domain (I.*)
    th_fmt: int = 0
    th_pitch: float = 0.0
    tp_mode: int = 0
    tp_val: float = 0.0
    
    # Properties Domain (P.*)
    screw_z_du: int = 40000
    motor_steps_z: int = 800
    encoder_ppr: int = 2400  # Effective quadrature counts per spindle revolution = encoder_ppr * 2
    
    # Raw dictionary of all received keys and monotonic frame counter
    raw: dict[str, str] = field(default_factory=dict)
    telemetry_seq: int = 0
```

### 4.2 Core Async Client API & Deterministic ACK Synchronization

To prevent stale-cache race conditions (where `wait_for()` evaluates cached state before the ESP32 has processed a newly transmitted command), `WhatElsClient.send()` awaits the controller's `OK:` or `ERR:` response frame and optionally waits for the next fresh 50 Hz telemetry snapshot (`telemetry_seq` increment).

```python
class CommandRejectedError(RuntimeError):
    """Raised when the controller replies with ERR:<reason>."""
    pass

class WhatElsClient:
    def __init__(self, host: str = "whatels.local", port: int = 81):
        self.uri = f"ws://{host}:{port}/canonical"
        self.state = ControllerState()
        self._ws = None
        self._listeners: list[Callable[[ControllerState], None]] = []
        self._ack_queue: asyncio.Queue[str] = asyncio.Queue()
        self._rx_task: asyncio.Task | None = None
        self._hb_task: asyncio.Task | None = None
        self._suppress_hb: bool = False

    async def connect(self, timeout_s: float = 5.0) -> None:
        """Connects to controller, starts RX and 500ms heartbeat tasks, and hydrates state."""
        self._ws = await asyncio.wait_for(websockets.connect(self.uri, ping_interval=None), timeout=timeout_s)
        self._rx_task = asyncio.create_task(self._rx_loop())
        self._hb_task = asyncio.create_task(self._heartbeat_loop())
        await self.send("CMD.state=ALL", expect_ack=False)
        await self.wait_for(lambda s: s.telemetry_seq > 0, timeout_s=timeout_s, description="initial hydration")

    async def disconnect(self) -> None:
        """Safely stops active feeds, disables simulation, and closes socket cleanly."""
        if self._hb_task:
            self._hb_task.cancel()
        try:
            await self.send("CMD.stop", expect_ack=False)
            await self.send("CMD.simRpm=0", expect_ack=False)
            await self.send("CMD.simEncoder=0", expect_ack=False)
        except Exception:
            pass
        if self._rx_task:
            self._rx_task.cancel()
        if self._ws:
            await self._ws.close()

    def suppress_heartbeat(self, suppress: bool = True) -> None:
        """Pauses or resumes the automatic 500ms CMD.ping keepalive (used to test watchdog timeout)."""
        self._suppress_hb = suppress

    async def send(self, cmd: str, expect_ack: bool = True, timeout_s: float = 2.0) -> str:
        """
        Transmits a newline-delimited command frame.
        If expect_ack is True, awaits the corresponding OK: or ERR: response from CommandDispatcher.
        Raises CommandRejectedError if the firmware responds with ERR:...
        """
        # Drain any stale ACKs
        while not self._ack_queue.empty():
            self._ack_queue.get_nowait()

        clean = cmd.strip() + "\n"
        await self._ws.send(clean)

        if not expect_ack:
            return ""

        reply = await asyncio.wait_for(self._ack_queue.get(), timeout=timeout_s)
        if reply.startswith("ERR:"):
            raise CommandRejectedError(f"Command '{cmd.strip()}' rejected by firmware: {reply}")
        return reply

    async def wait_next_telemetry(self, timeout_s: float = 1.0) -> ControllerState:
        """Waits for at least one fresh 50Hz telemetry broadcast frame to arrive."""
        start_seq = self.state.telemetry_seq
        return await self.wait_for(lambda s: s.telemetry_seq > start_seq, timeout_s=timeout_s, description="next telemetry frame")

    async def wait_for(
        self, 
        predicate: Callable[[ControllerState], bool], 
        timeout_s: float = 5.0, 
        description: str = "condition",
        require_fresh_frame: bool = True
    ) -> ControllerState:
        """
        Asynchronously yields control until predicate(self.state) evaluates to True.
        If require_fresh_frame is True, waits for at least one new telemetry packet after invocation.
        """
        if require_fresh_frame:
            await self.wait_next_telemetry(timeout_s=min(timeout_s, 0.5))
        if predicate(self.state):
            return self.state

        loop = asyncio.get_running_loop()
        fut = loop.create_future()

        def _check(st: ControllerState):
            if not fut.done() and predicate(st):
                fut.set_result(st)

        self._listeners.append(_check)
        try:
            return await asyncio.wait_for(fut, timeout=timeout_s)
        except asyncio.TimeoutError as e:
            raise TimeoutError(f"Timed out ({timeout_s}s) waiting for {description}. Last state: {self.state}") from e
        finally:
            self._listeners.remove(_check)

    def revs_to_pulses(self, revs: float) -> int:
        """Computes exact quadrature pulse counts for `revs` revolutions from controller P.encoderPpr."""
        cpr = self.state.encoder_ppr * 2
        return int(round(revs * cpr))

    async def inject_spindle_revs(self, revs: float) -> None:
        """
        Calculates exact pulse counts for `revs` revolutions based on state.encoder_ppr,
        injects them via CMD.simPulses, and waits for the slewed injection to settle.
        """
        pulses = self.revs_to_pulses(revs)
        await self.send(f"CMD.simPulses={pulses}")
        # At 600 RPM slew ceiling (10 rev/sec), wait for slewing + 200ms settle margin
        slew_duration_s = (abs(revs) / 10.0) + 0.25
        await asyncio.sleep(slew_duration_s)
        await self.wait_next_telemetry()
```

### 4.3 Multi-Line Frame Ingestion & Heartbeat Loop Details

```python
    async def _heartbeat_loop(self) -> None:
        """Transmits CMD.ping every 500ms so long cutting moves never trip the 1.5s Core 1 watchdog."""
        try:
            while True:
                await asyncio.sleep(0.5)
                if not self._suppress_hb and self._ws:
                    await self._ws.send("CMD.ping\n")
        except asyncio.CancelledError:
            pass

    async def _rx_loop(self) -> None:
        """Continuously ingests WebSocket frames, routes OK:/ERR: replies, and updates telemetry state."""
        try:
            async for message in self._ws:
                if isinstance(message, bytes):
                    message = message.decode("utf-8", errors="replace")
                
                lines = message.split("\n")
                changed = False
                for raw_line in lines:
                    line = raw_line.strip()
                    if not line:
                        continue
                    if line.startswith("OK:") or line.startswith("ERR:"):
                        # Ignore background ping ACKs so they don't pollute command ACK queue
                        if line != "OK:CMD.ping":
                            self._ack_queue.put_nowait(line)
                        continue
                    if self._parse_telemetry_line(line):
                        changed = True

                if changed:
                    self.state.telemetry_seq += 1
                    for listener in list(self._listeners):
                        listener(self.state)
        except asyncio.CancelledError:
            pass

    def _parse_telemetry_line(self, line: str) -> bool:
        """Parses canonical K.*, I.*, C.*, P.* lines into self.state."""
        if "=" not in line:
            return False
        key, val = line.split("=", 1)
        key, val = key.strip(), val.strip()
        self.state.raw[key] = val

        if key == "K.zDu": self.state.z_du = int(val)
        elif key == "K.xDu": self.state.x_du = int(val)
        elif key == "K.rpm": self.state.rpm = int(val)
        elif key == "K.turns": self.state.turns = float(val)
        elif key == "K.angle": self.state.angle = float(val)
        elif key == "K.on": self.state.is_on = (val == "1" or val.lower() == "true")
        elif key == "K.mode": self.state.mode = int(val)
        elif key == "K.estop": self.state.estop = int(val)
        elif key == "K.perms": self.state.perms = int(val, 0)
        elif key == "K.dupr": self.state.dupr = int(val)
        elif key == "K.nextDupr": self.state.next_dupr = int(val)
        elif key == "K.zLeftDu": self.state.z_left_du = None if val == "NONE" else int(val)
        elif key == "K.zRightDu": self.state.z_right_du = None if val == "NONE" else int(val)
        elif key == "K.xUpDu": self.state.x_up_du = None if val == "NONE" else int(val)
        elif key == "K.xDownDu": self.state.x_down_du = None if val == "NONE" else int(val)
        elif key == "P.encoderPpr": self.state.encoder_ppr = int(val)
        elif key == "P.motorStepsZ": self.state.motor_steps_z = int(val)
        elif key == "P.screwZDu": self.state.screw_z_du = int(val)
        else: return False
        return True
```

---

## 5. Test Case Authoring DSL & Lifecycle

Every test inherits from `HilTestCase`. The runner guarantees that[cite: 1]:
1. `setup()` prepares a clean controller state (stops motion, enables simulation, clears all 4 soft limits on $Z$ and $X$, resets taper ratio to `0`, and zeroes $Z$ and $X$)[cite: 1, 4].
2. `run()` executes the scenario and evaluates assertions[cite: 1].
3. `teardown()` **always executes**, even upon assertion failure or exception, restoring heartbeat keepalive, halting motion, and disabling simulation mode[cite: 1].

### 5.1 Test Case Skeleton

```python
# tests/hil/core/base_test.py

class HilTestCase(ABC):
    name: str = "Unnamed Test"
    description: str = ""
    timeout_s: float = 15.0

    def __init__(self, client: WhatElsClient):
        self.client = client

    async def setup(self) -> None:
        """Default setup: stop feed, enable sim mode, clear all limits/tapers, zero Z and X."""
        self.client.suppress_heartbeat(False)
        await self.client.send("CMD.stop")
        await self.client.send("CMD.simEncoder=1")
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.axisClearLimit=Z,L")
        await self.client.send("CMD.axisClearLimit=Z,R")
        await self.client.send("CMD.axisClearLimit=X,U")
        await self.client.send("CMD.axisClearLimit=X,D")
        await self.client.send("CMD.taperRatio=0")
        await self.client.send("CMD.axisZero=Z")
        await self.client.send("CMD.axisZero=X")
        await self.client.wait_for(lambda s: s.z_du == 0 and s.x_du == 0 and not s.is_on and s.rpm == 0)

    @abstractmethod
    async def run(self) -> dict[str, Any]:
        """Test logic and assertions. Returns a dict of key diagnostic metrics for reporting."""
        pass

    async def teardown(self) -> None:
        """Default teardown: restore heartbeat, stop feed, clear limits, disable simulation."""
        self.client.suppress_heartbeat(False)
        await self.client.send("CMD.stop", expect_ack=False)
        await self.client.send("CMD.simRpm=0", expect_ack=False)
        await self.client.send("CMD.simEncoder=0", expect_ack=False)
```

---

## 6. Authoritative Test Catalog & Real-Life Machining Scenarios (26 Tests)

Every test in the whatELS HIL suite verifies a specific physical machining behavior across the external WebSocket boundary (`ws://<ip>:81/canonical`)[cite: 1].

---

### Suite 1: `test_coords_and_presets.py` (Basic ELS Coordinate System)

#### 6.1 `axis_zero_and_preset`
* **Real-Life Machining Scenario (Touch-Off on Part Face, Gauge Block Offset & Soft Limit Origin Tracking):**  
  The operator touches off the tool on the end face of a newly chucked workpiece and taps `Ø Z` ($Z = 0.000\text{ mm}$)[cite: 1]. Next, they touch off against a $10.000\text{ mm}$ gauge block on the diameter and preset $X = +10.000\text{ mm}$[cite: 1]. Finally, with a soft limit active, shifting the workpiece coordinate origin (`CMD.axisZero`) must invoke `SoftLimitTable::updateOrigin()` so physical machine envelope protection remains aligned[cite: 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisPosDu=Z,250000
  CMD.axisZero=Z
  CMD.axisPosDu=X,100000
  CMD.axisZero=X
  CMD.axisLimitDu=Z,L,-50000
  CMD.axisPosDu=Z,10000
  ```
* **Lifecycle Sequence:**
  1. Preset $Z$ to $+250{,}000\text{ du}$; assert `s.z_du == 250000`[cite: 1].
  2. Zero $Z$; assert `s.z_du == 0`[cite: 1].
  3. Preset $X$ to $+100{,}000\text{ du}$; assert `s.x_du == 100000`[cite: 1].
  4. Zero $X$; assert `s.x_du == 0`[cite: 1].
  5. Set $Z$ Left limit at $-50{,}000\text{ du}$, then shift $Z$ coordinate to $+10{,}000\text{ du}$; verify `s.z_du == 10000` and `s.z_left_du` updates coherently via `updateOrigin()`[cite: 1, 5].
  6. Zero $Z$, clear limit, wait $300\text{ ms}$, and assert zero coordinate bounce[cite: 1].
* **Pass Criteria:**
  - Exact $0\text{ du}$ error on all presets and zeroes; zero coordinate jitter after $300\text{ ms}$ settle window[cite: 1].

---

### Suite 2: `test_jogging.py` (Basic ELS Manual Positioning)

#### 6.2 `manual_jog_step_precision`
* **Real-Life Machining Scenario (Incremental Touch-Off Jogging):**  
  The machinist steps the tool in using incremental jog steps ($0.10\text{ mm}$ and $1.00\text{ mm}$) across all four Cartesian directions (`TAIL` $+Z$, `CHUCK` $-Z$, `RETRACT` $+X$, `CENTER` $-X$) to kiss the workpiece surface[cite: 1, 2, 4].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.jogStepDu=1000
  CMD.jogStep=Z,TAIL
  CMD.jogStep=Z,CHUCK
  CMD.jogStepDu=10000
  CMD.jogStep=Z,TAIL
  CMD.jogStep=Z,CHUCK
  CMD.jogStep=X,RETRACT
  CMD.jogStep=X,CENTER
  ```
* **Lifecycle Sequence:**
  1. Set `CMD.jogStepDu=1000` ($0.100\text{ mm}$)[cite: 4]. Jog `Z,TAIL` $\to$ assert $Z = +1{,}000\text{ du}$. Jog `Z,CHUCK` $\to$ assert $Z = 0\text{ du}$.
  2. Set `CMD.jogStepDu=10000` ($1.000\text{ mm}$)[cite: 1, 4]. Jog `Z,TAIL` $\to$ assert $Z = +10{,}000\text{ du}$. Jog `Z,CHUCK` $\to$ assert $Z = 0\text{ du}$[cite: 1].
  3. Jog `X,RETRACT` $\to$ assert $X = +10{,}000\text{ du}$. Jog `X,CENTER` $\to$ assert $X = 0\text{ du}$[cite: 1, 2].
* **Pass Criteria:**
  - Every incremental jog lands within $\pm 1\text{ du}$ of the exact target coordinate, and the non-jogged axis remains at $0\text{ du}$.

#### 6.3 `manual_jog_hold_and_limits`
* **Real-Life Machining Scenario (Continuous Apron D-Pad Jog & Soft Stop Enforcement):**  
  The operator holds down the continuous jog button (`jogHold`) to traverse the bed, releases the button (`jogRelease`), and then jogs continuously into an active soft limit to verify that manual jogging cannot crash past a virtual bed stop[cite: 1, 4, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.jogHold=Z,TAIL
  CMD.jogRelease=Z
  CMD.axisLimitDu=Z,R,20000
  CMD.jogHold=Z,TAIL
  ```
* **Lifecycle Sequence:**
  1. Send `CMD.jogHold=Z,TAIL`; wait until `s.z_du >= 5000`[cite: 1, 4].
  2. Send `CMD.jogRelease=Z`; wait for `s.is_on == False`. Record $Z_{\text{release}}$; wait $300\text{ ms}$ and assert $0\text{ du}$ post-release creep[cite: 1, 4].
  3. Set Right soft limit `CMD.axisLimitDu=Z,R,20000`[cite: 4].
  4. Send `CMD.jogHold=Z,TAIL` and hold without releasing; wait for controller to auto-halt at the soft limit (`s.is_on == False`).
* **Pass Criteria:**
  - `jogRelease` decelerates cleanly without post-halt creep[cite: 1].
  - Unreleased `jogHold` automatically halts at $+20{,}000\text{ du} \pm 2\text{ du}$ without overrunning the soft limit[cite: 1, 5].

---

### Suite 3: `test_gearing.py` (Core Bresenham Synchronizer Accuracy)

#### 6.4 `gearing_sync_1_5mm_pitch`
* **Real-Life Machining Scenario (Discrete Forward Spindle Synchronization):**  
  Carriage engages a forward longitudinal feed of $1.500\text{ mm/rev}$ pitch towards the chuck[cite: 1]. Exactly $2.0$ revolutions of discrete spindle pulses (`client.revs_to_pulses(2.0)`) must advance the carriage by $-3.000\text{ mm}$ ($-30{,}000\text{ du}$)[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK
  CMD.simPulses=<2 * cpr>
  ```
* **Pass Criteria:**
  - Carriage coordinate lands at $Z = -30{,}000\text{ du} \pm 2\text{ du}$ ($0.2\,\mu\text{m}$), confirming zero rounding drift in `SpindleSynchronizer`[cite: 1, 5].

#### 6.5 `gearing_sync_reverse_1_0mm`
* **Real-Life Machining Scenario (Discrete Reverse Carriage Gearing):**  
  Carriage engages a $+Z$ feed of $1.000\text{ mm/rev}$ pitch towards the tailstock[cite: 1]. Exactly $1.0$ revolution of spindle pulses (`client.revs_to_pulses(1.0)`) must advance the carriage by $+1.000\text{ mm}$ ($+10{,}000\text{ du}$)[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.simPulses=<1 * cpr>
  ```
* **Pass Criteria:**
  - Carriage coordinate lands at $Z = +10{,}000\text{ du} \pm 2\text{ du}$[cite: 1].

#### 6.5.1 `gearing_sync_stroke_limit_auto_stop`
* **Real-Life Machining Scenario (Synchronized Stroke Auto-Stop):**  
  When feeding into a shoulder or cutting a fixed-length thread to a stop, the controller must auto-terminate motion precisely at the commanded distance (`dist:15000`) and disengage the active feed without requiring an explicit operator stop command, preventing tool crash. (Note: Microstep spindle index alignment is verified at the unit-test level in the real-time motion kernel).
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:15000
  CMD.simPulses=<12.0 revs worth of pulses>
  ```
* **Pass Criteria:**
  - Carriage motion terminates at $Z = -15{,}000\text{ du} \pm 2\text{ du}$.
  - Active feed disengages automatically (`is_on = False`) upon reaching target distance despite excess incoming spindle pulses.

---

### Suite 4: `test_emergency.py` (Safety Gatekeeper, Watchdog & Stop Latency)

#### 6.6 `stop_command_latency`
* **Real-Life Machining Scenario (Deterministic Emergency Feed Interrupt):**  
  During an active feed at $600\text{ RPM}$ (both synchronized and asynchronous), the operator hits **STOP**[cite: 1]. Stepper pulse generation must terminate in under $100\text{ ms}$[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.stop
  CMD.startFeed=axis:Z,sync:0,feed:3000000,dir:CHUCK
  CMD.stop
  ```
* **Pass Criteria:**
  - Round-trip stop latency $T_{\text{elapsed}} < 100.0\text{ ms}$ for both `sync:1` and `sync:0` feeds, and `s.is_on == False`[cite: 1].

#### 6.7 `in_motion_safety_gatekeeper_lockout`
* **Real-Life Machining Scenario (Operator Bumps Setup Controls During Active Cut — `[SAFE-01]`):**  
  While the carriage is actively cutting under chip load (`ModeState::RUNNING`), an accidental tap on axis zero, direction flip, axis swap, sync toggle, pitch rewrite, or mode switch must be blocked by `canExecuteCommand()` at the single-gate dispatcher (`CommandDispatcher.cpp`) so the tool and workpiece are never damaged[cite: 2, 3, 4].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.axisZero=Z          -> Expect ERR:Command locked during active motion
  CMD.gearDir=TAIL        -> Expect ERR:Command locked during active motion
  CMD.gearAxis=X          -> Expect ERR:Command locked during active motion
  CMD.gearSync=0          -> Expect ERR:Command locked during active motion
  CMD.feedMm=2.0          -> Expect ERR:Command locked during active motion
  CMD.mode=TURNING        -> Expect ERR:Command locked during active motion
  CMD.stop                -> Expect OK:CMD.stop
  ```
* **Pass Criteria:**
  - All 6 forbidden configuration commands raise `CommandRejectedError` (`ERR:Command locked during active motion`)[cite: 4].
  - Motion remains uninterrupted (`s.is_on == True`, `s.dupr == 10000`) until `CMD.stop` is sent, which succeeds immediately[cite: 1, 2, 4].

#### 6.8 `sim_safety_interlock_and_watchdog_halt`
* **Real-Life Machining Scenario (Network Drop / Crashed Test Runner Failsafe):**  
  If the Wi-Fi connection drops or the host script crashes while the virtual spindle is driving the physical lathe steppers at $600\text{ RPM}$, the Core 1 $1.5\text{s}$ watchdog must automatically extinguish simulated RPM and halt all axes[cite: 1].
* **Lifecycle Sequence:**
  1. Start continuous feed: `CMD.simRpm=600`, `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK`; wait for `s.is_on == True`[cite: 1].
  2. Call `client.suppress_heartbeat(True)` and sleep for $1.8\text{s}$ (exceeding the $1500\text{ ms}$ Core 1 timeout)[cite: 1].
  3. Re-enable heartbeat and inspect state.
* **Pass Criteria:**
  - Without receiving any stop command from the host, the controller has automatically set `s.rpm == 0`, `s.is_on == False`, and halted carriage motion[cite: 1].

---

### Suite 5: `test_soft_limits.py` (Universal Machine Envelope & Bed Dogs)

#### 6.9 `soft_limit_at_speed_chuck_halt`
* **Real-Life Machining Scenario (Turning up to Chuck / Shoulder Stop):**  
  With a Left soft stop set at $-50.000\text{ mm}$ (`-50000 du`) in front of the spinning chuck jaws ($600\text{ RPM}$), the motion engine looks ahead ($d_{\text{stop}} = v_i^2 / 2a$), decelerates smoothly to a halt at the shoulder, and holds position with zero overrun while the spindle keeps turning[cite: 1, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,L,-50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.simRpm=0
  ```
* **Pass Criteria:**
  - Carriage halts at $-50{,}000\text{ du} \pm 2\text{ du}$ with zero overrun past $-50{,}002\text{ du}$ and zero creep during a $1.0\text{s}$ post-halt hold window at $600\text{ RPM}$[cite: 1].

#### 6.10 `soft_limit_at_speed_tail_halt`
* **Real-Life Machining Scenario (Reverse Turning / Tailstock Crash Prevention):**  
  With a Right soft stop set at $+50.000\text{ mm}$ (`+50000 du`), the carriage feeds toward the tailstock at $600\text{ RPM}$ and halts cleanly at the boundary[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,R,50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.simRpm=0
  ```
* **Pass Criteria:**
  - Carriage halts at $+50{,}000\text{ du} \pm 2\text{ du}$ with zero post-halt creep[cite: 1].

#### 6.11 `soft_limit_x_axis_center_and_retract`
* **Real-Life Machining Scenario (Cross-Slide Centerline & Retract Stops):**  
  The machinist sets an inward cross-slide stop at centerline (`X,U,-15000`) and an outward retract stop (`X,D,15000`) so facing and grooving cuts never overrun centerline or bottom out the cross-slide nut[cite: 1, 3, 4].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=X,U,-15000
  CMD.axisLimitDu=X,D,15000
  CMD.simRpm=600
  CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER
  CMD.startFeed=axis:X,sync:1,feed:5000,dir:RETRACT
  ```
* **Pass Criteria:**
  - Cross-slide halts cleanly at $-15{,}000\text{ du} \pm 2\text{ du}$ on the inward cut and $+15{,}000\text{ du} \pm 2\text{ du}$ on the retract pass, with $Z$ remaining at $0\text{ du}$[cite: 1].

#### 6.12 `soft_limit_at_limit_directional_lock_and_escape`
* **Real-Life Machining Scenario (Resting on Bed Stop — Directional Lockout & Reverse Escape `[KIN-03]`):**  
  Once the carriage has halted at the Left soft limit ($-20{,}000\text{ du}$, $D_{\text{remaining}} = 0$), attempting to start a feed or jog further toward the chuck (`CHUCK`) must be blocked, whereas commanding a feed or jog toward the tailstock (`TAIL`) must immediately work to back the tool away from the shoulder[cite: 3, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,L,-20000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  # After halting at -20000 du:
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK  -> Blocked / 0 movement
  CMD.jogStep=Z,CHUCK                               -> Blocked / 0 movement
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL,dist:10000 -> Escapes to -10000 du
  ```
* **Pass Criteria:**
  - Zero movement past $-20{,}002\text{ du}$ when re-commanding `CHUCK` while resting on the limit[cite: 3, 5].
  - Reverse `TAIL` stroke cleanly moves the carriage back to $-10{,}000\text{ du} \pm 2\text{ du}$[cite: 3].

---

### Suite 6: `test_gearbox.py` (Single-Axis $Z$ & $X$ Electronic Gearbox)

#### 6.13 `gearbox_at_speed_z_feed`
* **Real-Life Machining Scenario (Longitudinal Turning to Dimension):**  
  Spindle stabilizes at $600\text{ RPM}$; operator engages $Z$ feed ($1.500\text{ mm/rev}$) toward the chuck for a $45.000\text{ mm}$ stroke[cite: 1]. Carriage disengages automatically at $-45{,}000\text{ du}$ while spindle continues spinning[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:45000
  ```
* **Pass Criteria:**
  - Halts at $Z = -45{,}000\text{ du} \pm 2\text{ du}$, `s.dupr == 15000`, and $0\text{ du}$ drift during $1.0\text{s}$ hold at $600\text{ RPM}$[cite: 1].

#### 6.14 `gearbox_at_speed_x_feed`
* **Real-Life Machining Scenario (Facing to Workpiece Center):**  
  Spindle spinning at $600\text{ RPM}$; operator engages $X$ cross-feed ($0.500\text{ mm/rev}$) toward centerline for $15.000\text{ mm}$[cite: 1].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER,dist:15000
  ```
* **Pass Criteria:**
  - Halts at $X = -15{,}000\text{ du} \pm 2\text{ du}$, $Z$ remains at $0\text{ du}$, and $0\text{ du}$ drift during $1.0\text{s}$ hold at $600\text{ RPM}$[cite: 1].

#### 6.15 `gearbox_cold_start_ramp`
* **Real-Life Machining Scenario (Threading Pass from Standstill & Mid-Cut RPM Dip):**  
  Operator arms synchronized feed ($1.000\text{ mm/rev}$, $20.000\text{ mm}$ stroke) with spindle stopped ($0\text{ RPM}$)[cite: 1]. Spindle accelerates through a multi-stage inertia curve ($150 \to 300 \to 600\text{ RPM}$), dips to $300\text{ RPM}$ under heavy cutting load, and finishes at $600\text{ RPM}$[cite: 1, 5].
* **Lifecycle Sequence:**
  1. Send `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK,dist:20000` at $0\text{ RPM}$[cite: 1].
  2. Step `CMD.simRpm` through `150`, `300`, `600`, `300`, `600` at $150\text{ ms}$ intervals.
  3. Wait for `s.is_on == False` and hold at $600\text{ RPM}$ for $1.0\text{s}$[cite: 1].
* **Pass Criteria:**
  - Carriage halts at $-20{,}000\text{ du} \pm 2\text{ du}$ with zero lost steps across the velocity transitions[cite: 1].

#### 6.16 `gearbox_stop_command`
* **Real-Life Machining Scenario (Operator Feed Hold on Tool Chatter & Immediate Reverse Retract):**  
  Operator stops a cut mid-pass (`CMD.stop`) while spindle spins at $600\text{ RPM}$, verifies zero creep, and immediately commands a reverse feed back to the starting point[cite: 1].
* **Pass Criteria:**
  - Post-stop position holds with $0\text{ du}$ drift for $500\text{ ms}$ at $600\text{ RPM}$, and subsequent reverse `CMD.startFeed` executes cleanly[cite: 1].

#### 6.17 `gearbox_async_velocity_feed_z_and_x`
* **Real-Life Machining Scenario (Motorized Power Feed / Milling Traverse with Spindle Stopped):**  
  With the spindle completely stationary ($0\text{ RPM}$), the machinist uses Asynchronous Gearbox mode (`sync:0`, `MotionMode::VELOCITY_FEED`) to traverse $Z$ at $600\text{ mm/min}$ ($10\text{ mm/s}$) for $20.000\text{ mm}$, and then $X$ at $300\text{ mm/min}$ ($5\text{ mm/s}$) for $10.000\text{ mm}$[cite: 2, 3, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=0
  CMD.startFeed=axis:Z,sync:0,feed:6000000,dir:CHUCK,dist:200000
  CMD.startFeed=axis:X,sync:0,feed:3000000,dir:CENTER,dist:100000
  ```
* **Pass Criteria:**
  - Both axes execute with `s.rpm == 0`, land at $Z = -200{,}000\text{ du} \pm 2\text{ du}$ and $X = -100{,}000\text{ du} \pm 2\text{ du}$, and elapsed duration is within $1.8\text{s}\text{–}3.2\text{s}$ (matching commanded velocity + accel/decel ramps)[cite: 2, 3, 5].

#### 6.18 `gearbox_live_feed_nudge_and_step_cycle`
* **Real-Life Machining Scenario (In-Cut Live Chip-Load Tuning — `[KIN-02]`):**  
  While actively cutting in Gearbox mode (`PERM_LIVE_FEED_TRIM`), the machinist taps `[ + ]` (`CMD.feedNudge=1`) twice to increase feedrate from $1.000\text{ mm/rev}$ to $1.100\text{ mm/rev}$ ($0.050\text{ mm}$ step) and cycles the step size (`CMD.feedStepCycle`)[cite: 2, 3, 4]. `SpindleSynchronizer::updatePitchLive()` must re-anchor cleanly at the current coordinate without a discontinuity jump[cite: 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.feedStepDu=500
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.simPulses=<5 * cpr>     -> Advances to -50,000 du
  CMD.feedNudge=1             -> Pitch becomes 10,500 dupr
  CMD.feedNudge=1             -> Pitch becomes 11,000 dupr
  CMD.feedStepCycle           -> Permitted live
  CMD.simPulses=<5 * cpr>     -> Advances by -55,000 du to -105,000 du
  CMD.stop
  ```
* **Pass Criteria:**
  - At the instant of `CMD.feedNudge=1`, $Z$ remains at $-50{,}000\text{ du} \pm 2\text{ du}$ (zero historical jump)[cite: 5].
  - After the second 5-rev pulse injection, $Z$ lands at $-105{,}000\text{ du} \pm 2\text{ du}$.

#### 6.19 `gearbox_tpi_imperial_pitch_conversion`
* **Real-Life Machining Scenario (Imperial 20 TPI Leadscrew Pitch):**  
  The machinist configures a $20\text{ TPI}$ pitch (`12,700 dupr`), which converts to $\text{dupr} = \text{round}(254{,}000 / 20) = 12{,}700\text{ dupr}$ ($1.2700\text{ mm/rev}$)[cite: 3, 4]. Over $4.0$ spindle revolutions, the carriage must advance by exactly $0.2000\text{ in}$ ($-5.0800\text{ mm} = -50{,}800\text{ du}$).
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:12700,dir:CHUCK
  CMD.simPulses=<4 * cpr>
  CMD.stop
  ```
* **Pass Criteria:**
  - `s.dupr == 12700` and $Z$ lands at $-50{,}800\text{ du} \pm 2\text{ du}$[cite: 1, 3].

#### 6.20 `gearbox_x_axis_gearing_0_25mm`
* **Real-Life Machining Scenario (Cross-Slide Gearing at 0.250 mm/rev):**  
  Tests synchronized cross-slide X-axis gearing at $0.250\text{ mm/rev}$ ($2{,}500\text{ dupr}$) advancing $+25{,}000\text{ du}$ ($+2.500\text{ mm}$) over $10.0$ spindle revolutions under canonical atomic invocation.
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:X,sync:1,feed:2500,dir:RETRACT
  CMD.simPulses=<10 * cpr>
  CMD.stop
  ```
* **Pass Criteria:**
  - Cross-slide advances to $+25{,}000\text{ du} \pm 2\text{ du}$ ($+2.500\text{ mm}$) and halts cleanly on `CMD.stop`[cite: 2, 4].

#### 6.21 `gearbox_spindle_reversal_and_shoulder_phase_tracking`
* **Real-Life Machining Scenario (Reversing Spindle Under Threading Lock — `[KIN-01]`):**  
  When cutting metric threads or tapping without disengaging the half-nut, the machinist stops the spindle at the bottom of the pass and reverses spindle rotation (`-RPM` / negative encoder counts) to back the tool out along the exact same thread helix back to $Z = 0.000\text{ mm}$[cite: 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:20000,dir:CHUCK
  CMD.simPulses=<10 * cpr>    -> Advances to -200,000 du
  CMD.simPulses=<-10 * cpr>   -> Reverses back to 0 du
  CMD.stop
  ```
* **Pass Criteria:**
  - After $+10$ revs, $Z = -200{,}000\text{ du} \pm 2\text{ du}$.
  - After $-10$ revs, $Z$ returns to $0\text{ du} \pm 2\text{ du}$ with zero cumulative phase slip[cite: 5].

#### 6.22 `gearbox_runway_too_short_guard`
* **Real-Life Machining Scenario (Engaging High-Speed Feed Too Close to Chuck Shoulder):**  
  Verifies Pre-Cut Runway Verification (`MOTION_ENGINE_SPECIFICATION.md` Section 4.2, item 4: $v_{\text{entry\_max}} = \sqrt{2 \cdot a \cdot D_{\text{remaining}}}$)[cite: 5]. If a soft limit is only $0.050\text{ mm}$ ($500\text{ du}$) ahead and the spindle is screaming at $1200\text{ RPM}$ with a $2.0\text{ mm/rev}$ pitch, the controller must either reject engagement or clamp velocity so the carriage never overruns $-500\text{ du}$[cite: 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,L,-500
  CMD.simRpm=1200
  CMD.startFeed=axis:Z,sync:1,feed:20000,dir:CHUCK
  ```
* **Pass Criteria:**
  - Either `CMD.startFeed` raises `CommandRejectedError` OR the carriage halts at $Z \ge -502\text{ du}$ without crashing past the $-500\text{ du}$ soft limit[cite: 1, 5].

---

### Suite 7: `test_gearbox_zx.py` (Coordinated Dual-Axis $Z+X$ Taper & Cone Mode)

#### 6.23 `gearbox_zx_shallow_taper_sync`
* **Real-Life Machining Scenario (Turning a Morse / Metric External Taper — `[KIN-04]`):**  
  The machinist configures Gearbox mode for coordinated $Z+X$ cutting (`axis:ZX`) with a taper ratio of `0.10` and a $Z$ feed of $1.000\text{ mm/rev}$ toward the chuck[cite: 2, 4, 5]. Over a $40.000\text{ mm}$ axial cut ($-40{,}000\text{ du}$), the cross-slide must advance proportionally in lockstep[cite: 2, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.taperRatio=0.10
  CMD.startFeed=axis:ZX,sync:1,feed:10000,dir:CHUCK
  CMD.simPulses=<4 * cpr>
  CMD.stop
  ```
* **Pass Criteria:**
  - $Z$ lands at $-40{,}000\text{ du} \pm 2\text{ du}$.
  - $X$ lands at its exact proportional taper delta ($\pm 2\text{ du}$) with zero post-halt creep[cite: 2, 5].

#### 6.24 `gearbox_zx_steep_cone_major_axis_swap`
* **Real-Life Machining Scenario (Turning a Steep $63.4^\circ$ Valve Seat / Center Point — 2D Bresenham Major-Axis Swap):**  
  When turning a steep cone where radial travel exceeds axial travel (`taperRatio=2.0`, $\Delta X = 2 \times \Delta Z$), the 2D Bresenham interpolator swaps the major/minor axes so $X$ steps smoothly without stair-stepping or violating $X$ max velocity[cite: 2, 5].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.taperRatio=2.0
  CMD.startFeed=axis:ZX,sync:1,feed:5000,dir:CHUCK
  CMD.simPulses=<4 * cpr>
  CMD.stop
  ```
* **Pass Criteria:**
  - Both $Z$ and $X$ maintain exact $1:2$ geometric proportionality ($\left\vert{}X_{\text{du}} / Z_{\text{du}}\right\vert{}$ matches `taperRatio` within $\pm 2\text{ du}$)[cite: 2, 5].

#### 6.25 `gearbox_zx_async_taper_feed`
* **Real-Life Machining Scenario (Electronic Compound Slide Power Feed at $0\text{ RPM}$):**  
  Executes a coordinated $Z+X$ taper move in asynchronous velocity mode (`sync:0`, `simRpm=0`) with `taperRatio=0.25`[cite: 2, 4].
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=0
  CMD.taperRatio=0.25
  CMD.startFeed=axis:ZX,sync:0,feed:3000000,dir:CHUCK,dist:20000
  ```
* **Pass Criteria:**
  - Move completes with `s.rpm == 0`; $Z$ halts at $-20{,}000\text{ du} \pm 2\text{ du}$ and $X$ halts at the exact $0.25$ proportional coordinate ($\pm 2\text{ du}$)[cite: 2, 5].

#### 6.26 `gearbox_zx_secondary_axis_soft_limit_halt`
* **Real-Life Machining Scenario (Cross-Slide Limit Tripping First During a Taper Cut):**  
  During a coordinated $Z+X$ taper cut, the cross-slide ($X$) reaches its active soft limit (`X,U,-5000`) *before* the carriage ($Z$) reaches its $Z$ limit (`Z,L,-100000`). Both axes must decelerate and halt simultaneously on the $X$ boundary so the tool does not continue feeding along $Z$ and gouge a cylindrical flat into the tapered part.
* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,L,-100000
  CMD.axisLimitDu=X,U,-5000
  CMD.taperRatio=0.25
  CMD.simRpm=600
  CMD.startFeed=axis:ZX,sync:1,feed:10000,dir:CHUCK
  ```
* **Pass Criteria:**
  - Motion halts automatically (`s.is_on == False`) when $X$ reaches $-5{,}000\text{ du} \pm 2\text{ du}$[cite: 1, 3].
  - $Z$ halts simultaneously at the corresponding proportional coordinate ($-20{,}000\text{ du} \pm 2\text{ du}$) with zero post-halt creep on either axis while the spindle continues spinning at $600\text{ RPM}$[cite: 1].

---

## 7. CLI Runner & Output Formats

The test harness is executed via `run_tests.py`[cite: 1]:

```bash
# Run all 26 tests against default hostname (whatels.local)
python3 tests/hil/run_tests.py

# Run against a specific IP address
python3 tests/hil/run_tests.py --host 192.168.10.22

# Filter specific suites or tests by keyword
python3 tests/hil/run_tests.py --filter gearbox_zx
python3 tests/hil/run_tests.py --filter soft_limit

# Output machine-readable JSON summary for AI agent analysis or CI
python3 tests/hil/run_tests.py --json > test_report.json
```

### 7.1 Human-Readable ANSI Terminal Output

```
========================================================================================
                      NANOELS HARDWARE-IN-THE-LOOP TEST SUITE (v2.0)                    
Target: ws://192.168.10.48:81/canonical | Firmware: v5.38 (ESP32-S3)
========================================================================================
TEST NAME                                          STATUS   DURATION  DETAILS
----------------------------------------------------------------------------------------
axis_zero_and_preset                               [PASS]   0.81s     tested_z_preset: 250000, tested_x_preset: 100000
gearbox_async_velocity_feed_z_and_x                [PASS]   4.46s     z_duration_s: 2.09, x_duration_s: 2.08
gearbox_at_speed_x_feed                            [PASS]   2.24s     spindle_rpm: 600, expected_x_du: -15000
gearbox_at_speed_z_feed                            [PASS]   2.37s     spindle_rpm: 600, expected_z_du: -45000
gearbox_cold_start_ramp                            [PASS]   2.53s     expected_z_du: -20000, actual_z_du: -20000
gearbox_live_feed_nudge_and_step_cycle             [PASS]   1.10s     mid_z_du: -50000, final_z_du: -105000
gearbox_runway_too_short_guard                     [PASS]   1.07s     limit_du: -500, rejected_by_dispatcher: False
gearbox_spindle_reversal_and_shoulder_phase_tracking [PASS] 1.85s     fwd_z_du: 0, rev_z_du: 0
gearbox_stop_command                               [PASS]   1.86s     halted_z_du: -11225, settled_z_du: -11225
gearbox_tpi_imperial_pitch_conversion              [PASS]   1.01s     expected_z_du: -50800, actual_z_du: -50800
gearbox_x_axis_gearing_0_25mm                      [PASS]   1.28s     expected_x_du: 25000, actual_x_du: 25000
gearbox_zx_async_taper_feed                        [PASS]   1.16s     taper_ratio: 0.25, z_du: -20000
gearbox_zx_secondary_axis_soft_limit_halt          [PASS]   2.02s     x_limit_du: -5000, halted_x_du: -5000
gearbox_zx_shallow_taper_sync                      [PASS]   0.98s     taper_ratio: 0.1, z_du: -40000
gearbox_zx_steep_cone_major_axis_swap              [PASS]   1.01s     cone_ratio: 2.0, z_du: -20000
gearing_sync_stroke_limit_auto_stop                [PASS]   1.38s     expected_z_du: -15000, actual_z_du: -15000
gearing_sync_1_5mm_pitch                           [PASS]   0.92s     expected_z_du: -30000, actual_z_du: -30000
gearing_sync_reverse_1_0mm                         [PASS]   0.80s     expected_z_du: 10000, actual_z_du: 10000
in_motion_safety_gatekeeper_lockout                [PASS]   1.09s     locked_cmds_rejected: 6
manual_jog_hold_and_limits                         [PASS]   2.28s     limit_du: 20000, halted_z_du: 0
manual_jog_step_precision                          [PASS]   2.19s     step_1000_ok: True, step_10000_ok: True
sim_safety_interlock_and_watchdog_halt             [PASS]   2.41s     timeout_trip_ms: 1801.07, watchdog_tripped: True
soft_limit_at_limit_directional_lock_and_escape    [PASS]   1.72s     locked_at_du: -20000, escaped_du: -10000
soft_limit_at_speed_chuck_halt                     [PASS]   2.92s     limit_du: -50000, halted_z_du: -50000
soft_limit_at_speed_tail_halt                      [PASS]   2.62s     limit_du: 50000, halted_z_du: 50000
soft_limit_x_axis_center_and_retract               [PASS]   2.83s     up_halt: 15000, down_halt: 15000
stop_command_latency                               [PASS]   1.07s     sync_stop_ms: 46.75, async_stop_ms: 47.5
----------------------------------------------------------------------------------------
SUMMARY: 27 Passed, 0 Failed, 0 Errors | Total Time: 48.00s
RESULT: ALL TESTS PASSED (Exit code: 0)
========================================================================================
```

### 7.2 Machine-Readable JSON Schema (`--json`)

```json
{
  "timestamp": "2026-10-19T16:35:00Z",
  "target": "ws://192.168.10.22:81/canonical",
  "firmware_version": "5.38",
  "total": 26,
  "passed": 26,
  "failed": 0,
  "duration_s": 45.01,
  "results": [
    {
      "name": "gearing_sync_1_5mm_pitch",
      "status": "PASS",
      "duration_s": 0.52,
      "details": {
        "expected_z_du": -30000,
        "actual_z_du": -30000,
        "error_du": 0
      }
    },
    {
      "name": "gearbox_zx_secondary_axis_soft_limit_halt",
      "status": "PASS",
      "duration_s": 2.30,
      "details": {
        "x_limit_du": -5000,
        "halted_x_du": -5000,
        "halted_z_du": -20000
      }
    }
  ]
}
```

---

## 8. Implementation Plan & Work Breakdown

```
┌────────────────────────────────────────────────────────────────────────┐
│                   HIL HARNESS IMPLEMENTATION PHASES                    │
├─────────────────────┬──────────────────────────────────────────────────┤
│ Phase               │ Deliverables & Scope                             │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 1: Firmware   │ • Add slewed virtual injection to Spindle HAL    │
│ Simulation Bridge & │ • Enforce Core 1 state transitions in getPulse   │
│ Dispatcher Fixes    │ • Use atomic<uint32_t> for 1.5s watchdog         │
│                     │ • Add OK:/ERR: replies to all CommandDispatcher  │
│                     │   handlers & fix axis:ZX in handleStartFeed()    │
│                     │ • Verify build via `pio run -e esp32s3`          │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 2: Python     │ • Implement `tests/hil/core/client.py` with      │
│ Core Framework      │   ACK queue sync & 500ms CMD.ping heartbeat task │
│                     │ • Implement `tests/hil/core/base_test.py`        │
│                     │ • Implement `tests/hil/core/assertions.py`       │
│                     │ • Implement `tests/hil/run_tests.py` CLI runner  │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 3: Test Suite │ • Implement `test_coords_and_presets.py` (1)     │
│ Catalog (27 Tests)  │ • Implement `test_jogging.py` (2)                │
│                     │ • Implement `test_gearing.py` (3)                │
│                     │ • Implement `test_emergency.py` (3)              │
│                     │ • Implement `test_soft_limits.py` (4)            │
│                     │ • Implement `test_gearbox.py` (10)               │
│                     │ • Implement `test_gearbox_zx.py` (4)             │
└─────────────────────┴──────────────────────────────────────────────────┘
```

---

## 9. Acceptance Criteria (Definition of Done)

The HIL test harness is considered complete and operational when[cite: 1]:
1. `pio run -e esp32s3` compiles cleanly with zero warnings or errors with `CMD.sim*` hooks and consistent `OK:`/`ERR:` replies enabled[cite: 1].
2. The firmware automatically times out and halts within $1.5\text{s}$ if the test runner suppresses heartbeats or terminates abruptly (`sim_safety_interlock_and_watchdog_halt` passes)[cite: 1].
3. The In-Motion Safety Gatekeeper (`[SAFE-01]`) deterministically rejects all locked configuration mutations during active cutting (`in_motion_safety_gatekeeper_lockout` passes)[cite: 2, 4].
4. Single-axis ($Z$, $X$) and coordinated dual-axis ($Z+X$) Gearbox tests verify **$\le 2\text{ du}$ ($0.2\,\mu\text{m}$) accuracy** across synchronized, asynchronous, live-nudge, and spindle-reversal operations[cite: 1, 2, 5].
5. Soft limit assertions confirm clean deceleration halts on both primary and secondary axes without step overshoot or post-halt creep[cite: 1, 5].

```