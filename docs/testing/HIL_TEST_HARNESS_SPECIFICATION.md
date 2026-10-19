# NanoELS Hardware-in-the-Loop (HIL) & WebSocket Test Harness Specification

> **Status:** Authoritative Specification & Implementation Blueprint  
> **Target Subsystems:** Subsystem 1 (HAL Drivers), Subsystem 5 (Command Dispatcher), Subsystem 6 (Client Adapters & Testing)  
> **Master Blueprint:** [`NANOELS_MASTER_ARCHITECTURE.md`](../NANOELS_MASTER_ARCHITECTURE.md)  
> **Client Specification:** [`../clients/ELS_CLIENT_ARCHITECTURE.md`](../clients/ELS_CLIENT_ARCHITECTURE.md)  
> **Motion Engine Spec:** [`../motion/MOTION_ENGINE_SPECIFICATION.md`](../motion/MOTION_ENGINE_SPECIFICATION.md)  
> **Date:** October 2026  
> **Target Platform:** Host Python 3.10+ (WSL/Linux/macOS) $\longleftrightarrow$ ESP32-S3 Firmware via WebSocket (:81/canonical) or Serial UART  

---

## 1. Executive Summary & Objective

This specification defines the architecture, wire protocols, test authoring DSL, and firmware simulation hooks for the **NanoELS Hardware-in-the-Loop (HIL) Test Harness**.

While unit tests (`test/test_motion_e2e.cpp`) verify kinematic algorithms in synthetic isolation on the host, this harness verifies the **complete, live compiled firmware** running on physical ESP32-S3 hardware across the external network boundary (`ws://<ip>:81/canonical`). It treats the controller as a black box, verifying:
1. Inbound network framing, sanitization, and command dispatching on Core 0.
2. Inter-core command queue handoff from Core 0 to Core 1.
3. Real-time deterministic kinematics, trajectory planning, and stepper step generation on Core 1.
4. Tear-free lock-free telemetry sampling ([`TelemetryMailbox`](../../src/motion/contract/TelemetryMailbox.h)).
5. 50 Hz outbound WebSocket serialization and streaming back to the client.
6. Synchronous bidirectional command acknowledgments (`OK:CMD.*` / `ERR:*`) and background client failsafe heartbeats.

To test spindle-synchronized and handwheel motion without requiring physical lathe rotation or external pulse generator hardware, the specification establishes a **Firmware Simulation Injection Hook** within the Hardware Abstraction Layer (HAL).

```
┌────────────────────────────────────────────────────────────────────────┐
│                        END-TO-END HIL TOPOLOGY                         │
├───────────────────────────────────┬────────────────────────────────────┤
│ HOST ENVIRONMENT (Python Test)    │ ESP32-S3 TARGET CONTROLLER         │
│                                   │                                    │
│  tests/hil/run_tests.py           │  Core 0 (Networking & Dispatch):   │
│   ├── TestRunner / CLI Filter     │   ├── WebServerModule (:81/ws)     │
│   ├── TestSuites (Modular DSL)    │   ├── CommandDispatcher.cpp        │
│   └── NanoElsClient (Async WS)    │   └── MachineModel / Serializer    │
│        ├── Background Heartbeat   │            │ (Inter-core Queue)    │
│        └── Synchronous ACK Future │            ▼                       │
│            │                      │  Core 1 (Real-Time Motion):        │
│            │  ws://<ip>:81        │   ├── MotionEngine (prio 22)       │
│            │  /canonical          │   │    └── 1.5s Sim Watchdog       │
│            ▼                      │   ├── Esp32SpindleEncoderHAL       │
│      [ Outbound Commands ] ───────┼──►│    ├── Pulse Slew Clamper      │
│       CMD.startFeed=...           │   │    └── Core 1 Sync Transition  │
│       CMD.simPulses=4800          │   ├── Esp32StepperHAL (GPTimer)    │
│                                   │   └── TelemetryMailbox (Seqlock)   │
│      [ Inbound Telemetry & ACK ] ◄┼────────────────────────────────────┘
│       OK:CMD.simEncoder           │
│       K.zDu=-15000, K.on=0        │
└───────────────────────────────────┴────────────────────────────────────┘
```

---

## 2. Firmware Simulation Injection Contract

To execute automated tests against physical hardware sitting on a developer's bench without mechanical spindle rotation, the firmware provides a software injection bridge directly into `ISpindleEncoderHAL`.

### 2.1 Virtual Spindle Encoder HAL Interface

`ISpindleEncoderHAL` is extended with non-blocking virtual injection primitives:

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
     *               Thread-safe: sets an atomic flag on Core 0. Core 1 synchronously resets integration
     *               state and PCNT hardware baseline on mode transition inside getPulseDelta().
     */
    virtual void setSimulationActive(bool active) = 0;

    /**
     * @brief Checks if virtual injection mode is currently active.
     */
    virtual bool isSimulationActive() const = 0;

    /**
     * @brief Injects an exact discrete pulse delta.
     * @param delta Signed pulse count. Accumulates atomically; slewed smoothly in getPulseDelta()
     *              up to a safe maximum chunk per tick to preserve realistic acceleration.
     */
    virtual void injectSimulatedPulses(int32_t delta) = 0;

    /**
     * @brief Sets a constant simulated spindle RPM.
     * @param rpm Signed rotational speed in RPM (+ forward / - reverse). 0.0 = stopped.
     */
    virtual void setSimulatedRpm(float rpm) = 0;
};
```

### 2.1.1 Concrete `Esp32SpindleEncoderHAL` Implementation Details

In `src/motion/hal/Esp32SpindleEncoderHAL.h` and `.cpp`, simulation mode introduces thread-safe atomic members, Core 1 synchronous transition detection, and pulse-slewing integration math:

```cpp
// Member variables added to Esp32SpindleEncoderHAL:
std::atomic<bool>     m_simActive{false};
std::atomic<float>    m_simRpm{0.0f};
std::atomic<int32_t>  m_pendingSimPulses{0};
bool                  m_wasSimActive{false};       // Core 1 local state transition tracker
int32_t               m_maxSimPulseChunk{96};      // Max pulses consumed per tick (~1200 RPM at 4800 CPR)
int64_t               m_lastSimTimeUs{0};          // Core 1 integration timestamp
float                 m_simFraction{0.0f};         // Core 1 sub-pulse fractional accumulator
```

**Pulse Integration & Kinematic Slewing Math in `Esp32SpindleEncoderHAL::getPulseDelta()`:**

To ensure complete thread safety across ESP32-S3 cores without mutexes:
1. Core 0 only toggles `m_simActive` atomically.
2. Core 1 detects the edge `(simActive != m_wasSimActive)` inside `getPulseDelta()`, safely re-zeroing its own integration timestamp, fractional accumulator, and physical PCNT register baseline without race conditions.
3. Discrete pulses (`CMD.simPulses`) are slewed in chunks of at most `m_maxSimPulseChunk` counts per tick (e.g. $\le 96$ counts/tick at $1\,\text{ms}$, corresponding to a maximum spindle speed equivalent of $1,200\,\text{RPM}$ at $4,800\,\text{cpr}$). This prevents instantaneous velocity shock spikes ($\sim 120,000\,\text{RPM}$) while ensuring 100% mathematical conservation of total counts.

```cpp
int32_t Esp32SpindleEncoderHAL::getPulseDelta() {
    bool simActive = m_simActive.load(std::memory_order_relaxed);
    int64_t nowUs = esp_timer_get_time();

    // 1. Core 1 Synchronous Mode Transition Handler:
    if (simActive != m_wasSimActive) {
        m_lastSimTimeUs = nowUs;
        m_simFraction = 0.0f;
        if (!simActive && m_unit) {
            // Re-zero PCNT baseline synchronously on Core 1 when leaving simulation
            int rawCount = 0;
            pcnt_unit_get_count(m_unit, &rawCount);
            m_lastCount16 = static_cast<int16_t>(rawCount);
        }
        m_wasSimActive = simActive;
    }

    if (simActive) {
        float dt = (m_lastSimTimeUs > 0) ? static_cast<float>(nowUs - m_lastSimTimeUs) * 1e-6f : 0.0f;
        m_lastSimTimeUs = nowUs;
        if (dt > 0.05f) dt = 0.05f; // Clamp delta-time ceiling to prevent runaway bursts

        // 2. Integrate continuous rotational RPM:
        float rpm = m_simRpm.load(std::memory_order_relaxed);
        float counts = (rpm / 60.0f) * m_countsPerSpindleRev * dt + m_simFraction;
        int32_t delta = static_cast<int32_t>(counts);
        m_simFraction = counts - static_cast<float>(delta);

        // 3. Kinematic pulse slewing: drain discrete manual pulses in safe chunks
        // to prevent instantaneous acceleration spikes from overflowing GPTimer:
        int32_t pending = m_pendingSimPulses.load(std::memory_order_relaxed);
        if (pending != 0) {
            int32_t step = std::clamp(pending, -m_maxSimPulseChunk, m_maxSimPulseChunk);
            m_pendingSimPulses.fetch_sub(step, std::memory_order_relaxed);
            delta += step;
        }

        m_accumulatedPulses += delta;
        updateRpmEstimate(delta, nowUs);
        return delta;
    }

    // ... standard physical PCNT hardware sampling when simulation is inactive ...
}

void Esp32SpindleEncoderHAL::setSimulationActive(bool active) {
    // Pure atomic toggle from Core 0: Core 1 handles baseline and state reset synchronously
    m_simActive.store(active, std::memory_order_release);
    if (!active) {
        m_simRpm.store(0.0f, std::memory_order_relaxed);
    }
}
```

---

### 2.2 Wire Commands (`CMD.sim*`)

The test harness commands the simulation hooks using standard canonical text frames parsed by `CommandDispatcher.cpp`:

| Command Frame | Parameters | Description | Controller Response |
| :--- | :--- | :--- | :--- |
| `CMD.simEncoder=<0\|1>` | `0` or `1` | Enables or disables virtual encoder injection mode. | `OK:CMD.simEncoder\n` |
| `CMD.simPulses=<delta>` | Integer $\pm$ pulses | Injects a discrete number of spindle quadrature counts into Core 1. Slewed smoothly across ticks. | `OK:CMD.simPulses\n` |
| `CMD.simRpm=<rpm>` | Float $\pm$ RPM | Commands a continuous spindle speed. The HAL automatically integrates `delta = rpm / 60 * ppr * dt` on every motion tick. | `OK:CMD.simRpm\n` |
| `CMD.simMpg=<axis>,<counts>` | Axis `Z\|X`, $\pm$ counts | *(Phase 2 Scope)* Simulates discrete MPG handwheel detent pulses. | `OK:CMD.simMpg\n` |

#### Production Safety Interlock & Compile Guard

To ensure simulation commands cannot accidentally override physical lathe operation in production:
1. **Compile-Time Guard:** `CMD.sim*` handling is conditionally compiled under `#if HIL_SIMULATION_ENABLED` (or enabled in non-production test builds).
2. **Runtime Safety Invariant:** When receiving `CMD.simEncoder=1`, the controller asserts that physical spindle rotation is zero (`getFilteredRpm() == 0`) and all axis motion is stopped (`!isMoving()`). If violated, the command is rejected immediately with `ERR:SAFETY_INTERLOCK_ACTIVE\n`.

### 2.2.1 Firmware Linkage (`CommandDispatcher` $\to$ `MotionEngine` $\to$ `ISpindleEncoderHAL`)

To route `CMD.sim*` frames without global pointer leaks:
1. `src/motion/kernel/MotionEngine.h` provides an explicit non-const getter:
   ```cpp
   ISpindleEncoderHAL* getEncoderHAL() { return m_encoderHal; }
   ```
2. `src/web/CommandDispatcher.cpp` dispatches directly through the singleton and updates the atomic millisecond heartbeat timestamp `m_lastSimHeartbeatMs` (`std::atomic<uint32_t>`):
   ```cpp
   // CMD.simEncoder=<0|1>
   if (cmdLen >= 11 && strncmp(cmd, "simEncoder=", 11) == 0) {
       bool active = atoi(cmd + 11) != 0;
       // Safety check: reject if machine is actively cutting on physical spindle
       if (active && (MotionEngine::instance().isAnyAxisMoving() || 
                      MotionEngine::instance().getEncoderHAL()->getFilteredRpm() > 0)) {
           if (reply) *reply = "ERR:SAFETY_INTERLOCK_ACTIVE\n";
           return true;
       }
       ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
       if (hal) hal->setSimulationActive(active);
       MotionEngine::instance().touchSimHeartbeat();
       if (reply) *reply = "OK:CMD.simEncoder\n";
       return true;
   }
   // CMD.simPulses=<count>
   if (cmdLen >= 10 && strncmp(cmd, "simPulses=", 10) == 0) {
       int32_t pulses = atol(cmd + 10);
       ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
       if (hal) hal->injectSimulatedPulses(pulses);
       MotionEngine::instance().touchSimHeartbeat();
       if (reply) *reply = "OK:CMD.simPulses\n";
       return true;
   }
   // CMD.simRpm=<rpm>
   if (cmdLen >= 7 && strncmp(cmd, "simRpm=", 7) == 0) {
       float rpm = atof(cmd + 7);
       ISpindleEncoderHAL* hal = MotionEngine::instance().getEncoderHAL();
       if (hal) hal->setSimulatedRpm(rpm);
       MotionEngine::instance().touchSimHeartbeat();
       if (reply) *reply = "OK:CMD.simRpm\n";
       return true;
   }
   ```

---

### 2.3 Safety Watchdog & Core 1 Failsafe Invariant

To guarantee that a disconnected, hanging, or crashed test script never leaves physical steppers or simulated feeds running unattended:
1. **Heartbeat Registration:** Every `CMD.sim*` or `CMD.ping` command invokes `MotionEngine::instance().touchSimHeartbeat()`, updating `std::atomic<uint32_t> m_lastSimHeartbeatMs`:
   ```cpp
   void MotionEngine::touchSimHeartbeat() {
       m_lastSimHeartbeatMs.store(static_cast<uint32_t>(esp_timer_get_time() / 1000), std::memory_order_relaxed);
   }
   ```
2. **Core 1 Watchdog Evaluation:** Evaluated synchronously inside `MotionEngine::onMotionTick()` on Core 1:
   ```cpp
   if (m_encoderHal && m_encoderHal->isSimulationActive()) {
       uint32_t nowMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
       uint32_t lastHb = m_lastSimHeartbeatMs.load(std::memory_order_relaxed);
       if (nowMs - lastHb > 1500) {
           // Harness dropped offline or timed out (>1.5s): force immediate failsafe stop
           m_encoderHal->setSimulatedRpm(0.0f);
           m_encoderHal->setSimulationActive(false);
           stopAll(false); // Orderly normal deceleration stop
       }
   }
   ```
3. **Long-Running Cut Protection & Failsafe Test Hook:**
   - To prevent long feeds ($>1.5\,\text{s}$) from starving the watchdog while passively waiting for motion completion, `NanoElsClient` runs a background task transmitting `CMD.ping\n` every $500\,\text{ms}$.
   - To verify the $1.5\,\text{s}$ failsafe watchdog in automated tests, `NanoElsClient` provides an explicit hook `client.suppress_heartbeat(True)` so test cases can intentionally starve the controller and confirm automatic deceleration to zero.

---

## 3. Test Harness Architecture & Python Framework

The test harness is implemented in Python 3.10+ using `asyncio` and `websockets`. It contains no heavy dependencies and requires only `pip install websockets`.

### 3.1 Directory Layout

```
tests/hil/
├── requirements.txt            # websockets>=12.0
├── run_tests.py                # Main test runner CLI and dynamic test discoverer
├── core/
│   ├── __init__.py
│   ├── client.py               # NanoElsClient: Async WebSocket connection & reactive state mirror
│   ├── assertions.py           # Domain assertion helpers (assert_pos_du, assert_halted, etc.)
│   ├── base_test.py            # HilTestCase abstract base class
│   └── reporter.py             # ANSI Terminal & JSON reporting formatters
└── suites/
    ├── __init__.py
    ├── test_gearbox.py         # Electronic gearbox at-speed and cold-start feed tests
    ├── test_gearing.py         # Discrete pulse-synchronized gearing tests (forward & reverse)
    ├── test_soft_limits.py     # High-speed soft limits, lookahead, and shoulder halt tests
    ├── test_coords_and_presets.py # Axis coordinate zeroing and DRO preset tests
    ├── test_jogging.py         # Discrete step jogging and continuous hold/release tests
    └── test_emergency.py       # Stop command latency and feed interrupt tests
```

---

## 4. `NanoElsClient` Specification & State Engine

`NanoElsClient` manages the asynchronous transport, incoming packet deserialization, state maintenance, synchronous command acknowledgment (`OK:` / `ERR:`), and condition waiting.

### 4.1 Internal State Representation

The client continuously parses incoming 50 Hz text frames and maintains a local cache:

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
    encoder_ppr: int = 1200  # Spindle pulses per rev (4x quadrature = 4800 counts/rev)
    
    # Raw dictionary of all received keys
    raw: dict[str, str] = field(default_factory=dict)
```

> [!NOTE]
> **PPR vs. Quadrature Counts:** Standard optical spindle encoders report optical pulses per revolution ($PPR$). Physical quadrature decoding on ESP32-S3 hardware PCNT multiplies this by 4 ($CPR = 4 \times PPR$). For a 1200 PPR encoder, $1\text{ spindle rev} = 4,800\text{ counts}$; for 2400 PPR, $1\text{ rev} = 9,600\text{ counts}$. Test cases must never hardcode raw pulse counts; always compute counts dynamically from `client.state.encoder_ppr` or use `client.inject_spindle_revs()`.

### 4.2 Core Async Client API & Heartbeat Management

```python
class NanoElsClient:
    def __init__(self, host: str = "nanoels.local", port: int = 81):
        self.uri = f"ws://{host}:{port}/canonical"
        self.state = ControllerState()
        self._ws = None
        self._listeners: list[Callable[[ControllerState], bool]] = []
        self._pending_acks: dict[str, list[asyncio.Future]] = {}
        self._rx_task: asyncio.Task | None = None
        self._heartbeat_task: asyncio.Task | None = None
        self._heartbeat_suppressed: bool = False
        
    async def connect(self, timeout_s: float = 5.0) -> None:
        """Connects to controller, starts receiver loop and background 500ms heartbeat."""
        ...

    async def disconnect(self) -> None:
        """Cancels heartbeat, stops active feeds, clears simulation, and closes socket."""
        ...

    async def send(self, cmd: str, wait_ack: bool = True, timeout_s: float = 2.0) -> None:
        """
        Transmits a newline-delimited command frame.
        When wait_ack is True, awaits matching 'OK:CMD.<name>' response from controller,
        raising RuntimeError immediately if 'ERR:<reason>' is returned.
        This prevents false-positive test passes on stale cached state.
        """
        ...

    def suppress_heartbeat(self, suppress: bool = True) -> None:
        """Enables or disables the background 500ms heartbeat (used for watchdog timeout testing)."""
        self._heartbeat_suppressed = suppress

    async def wait_for(
        self, 
        predicate: Callable[[ControllerState], bool], 
        timeout_s: float = 5.0, 
        description: str = "condition"
    ) -> ControllerState:
        """
        Asynchronously yields control until predicate(self.state) evaluates to True.
        Raises TimeoutError with diagnostic state dump if timeout expires.
        """
        ...

    async def inject_spindle_revs(self, revs: float, rpm: float = 600.0) -> None:
        """
        Calculates exact quadrature counts for `revs` revolutions based on state.encoder_ppr * 4,
        injects them via CMD.simPulses, and confirms receipt.
        """
        cpr = self.state.encoder_ppr * 4 if self.state.encoder_ppr > 0 else 4800
        total_pulses = int(round(revs * cpr))
        await self.send(f"CMD.simPulses={total_pulses}", wait_ack=True)
```

### 4.3 Multi-Line Frame Ingestion & ACK Resolution

The ESP32-S3 `WebServerModule` transmits WebSocket packets that batch multiple domain lines separated by newline (`\n`) characters within a single TCP/WebSocket frame payload (e.g. `OK:CMD.axisZero\nK.zDu=0\nK.rpm=600\nK.on=1\n`).

`NanoElsClient._rx_loop()` processes both asynchronous telemetry updates and synchronous command ACKs:

```python
# tests/hil/core/client.py

async def _rx_loop(self) -> None:
    """Continuously ingests WebSocket frames, splitting telemetry and resolving ACKs."""
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

                # 1. Synchronous Command Acknowledgment Handling:
                if line.startswith("OK:CMD."):
                    cmd_name = line[7:].strip()
                    if cmd_name in self._pending_acks and self._pending_acks[cmd_name]:
                        fut = self._pending_acks[cmd_name].pop(0)
                        if not fut.done():
                            fut.set_result(True)
                    continue

                if line.startswith("ERR:"):
                    err_msg = line[4:].strip()
                    # Reject all pending command futures with error
                    for cmd_name, fut_list in self._pending_acks.items():
                        for fut in fut_list:
                            if not fut.done():
                                fut.set_exception(RuntimeError(f"Firmware error: {err_msg}"))
                    self._pending_acks.clear()
                    continue

                # 2. Asynchronous Domain Telemetry Ingestion:
                if self._parse_telemetry_line(line):
                    changed = True

            if changed:
                for check_fn in list(self._listeners):
                    check_fn(self.state)
    except asyncio.CancelledError:
        pass

async def _heartbeat_loop(self) -> None:
    """Background task sending CMD.ping every 500ms to keep Core 1 sim watchdog fed."""
    try:
        while True:
            await asyncio.sleep(0.5)
            if not self._heartbeat_suppressed and self._ws:
                # Transmit ping without blocking on ACK
                clean = "CMD.ping\n"
                await self._ws.send(clean)
    except asyncio.CancelledError:
        pass
```

---

## 5. Test Case Authoring DSL & Lifecycle

Every test inherits from `HilTestCase`. The runner guarantees that:
1. `setup()` prepares controller state (clears limits, stops motion, zeroes axes) with guaranteed sequential ACK delivery.
2. `run()` executes the scenario and evaluates assertions.
3. `teardown()` **always executes**, even upon assertion failure, syntax error, or keyboard interrupt, resetting the simulation mode and ensuring motion is halted.

### 5.1 Test Case Skeleton

```python
# tests/hil/core/base_test.py

class HilTestCase(ABC):
    name: str = "Unnamed Test"
    description: str = ""
    timeout_s: float = 10.0

    def __init__(self, client: NanoElsClient):
        self.client = client

    async def setup(self) -> None:
        """Default setup: stop any active feed, enable simulation mode, zero Z/X with ACK synchronization."""
        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.send("CMD.simEncoder=1", wait_ack=True)
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisZero=Z", wait_ack=True)
        await self.client.send("CMD.axisZero=X", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_du == 0 and s.x_du == 0 and not s.is_on)

    @abstractmethod
    async def run(self) -> None:
        """Test logic and assertions."""
        pass

    async def teardown(self) -> None:
        """Default teardown: restore heartbeat, stop feed, clear simulation mode."""
        self.client.suppress_heartbeat(False)
        await self.client.send("CMD.stop", wait_ack=False)
        await self.client.send("CMD.simRpm=0", wait_ack=False)
        await self.client.send("CMD.simEncoder=0", wait_ack=False)
```

### 5.2 Domain Assertion Primitives

To eliminate boilerplate, `tests/hil/core/assertions.py` provides high-level assertions:

```python
# Asserts value is within +/- tolerance_du (default +/- 2 du = 0.2 microns)
def assert_pos_du(actual_du: int, expected_du: int, tolerance_du: int = 2, axis: str = "Z"):
    diff = abs(actual_du - expected_du)
    assert diff <= tolerance_du, (
        f"Axis {axis} position mismatch! Expected: {expected_du} du, "
        f"Actual: {actual_du} du, Error: {diff} du (Allowed: +/-{tolerance_du} du)"
    )

# Asserts motion has halted cleanly with is_on == False
def assert_halted(state: ControllerState, reason: str = ""):
    assert not state.is_on, f"Expected controller to be halted, but is_on is True! ({reason})"
```

---

## 6. Authoritative Test Catalog & Real-Life Scenarios

Every test in the NanoELS HIL suite verifies a specific physical machining behavior across the external WebSocket boundary (`ws://<ip>:81/canonical`). This catalog defines the real-life scenario, inbound parameters, simulation stimulus, and exact pass/fail criteria for each test case.

---

### 6.1 `gearbox_at_speed_z_feed` (Suite: `test_gearbox.py`)

* **Real-Life Machining Scenario (Longitudinal Turning to Dimension):**  
  The machinist turns on the spindle drive and lets the lathe stabilize at cutting speed ($600\text{ RPM}$). They touch off the tool on the workpiece outside diameter and engage electronic power feed towards the chuck to turn a cylindrical journal. When the carriage reaches the programmed $45.000\text{ mm}$ stroke length, the feed automatically disengages. The spindle continues spinning at $600\text{ RPM}$ so the cutting tool does not leave a dwell gouge mark or fracture in the cut. Once the tool has stopped, the machinist powers down the spindle.

* **Objective:**  
  Verify on-the-fly synchronized feed engagement while the spindle is already rotating at full speed, followed by automatic disengagement at target stroke distance with zero post-halt creep under continuous spindle rotation.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:45000
  CMD.simRpm=0
  ```

* **Physical Simulation Stimulus:**  
  Virtual spindle acceleration to $600\text{ RPM}$ ($10\text{ rev/sec}$), steady-state rotation during the $45.0\text{ mm}$ cut ($30\text{ spindle revs}$ at $1.500\text{ mm/rev}$), and continuous $600\text{ RPM}$ rotation during a $1.0\text{s}$ post-halt hold window.

* **Lifecycle Sequence:**
  1. Send `CMD.simRpm=600`; wait for `s.rpm >= 580` (spindle confirmed stable at speed).
  2. Send `CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:45000`; wait for `s.is_on == True`.
  3. Wait for `s.is_on == False` (controller disengages motion upon reaching $-45,000\text{ du}$).
  4. **Anti-Creep Hold Window:** Maintain spindle spinning at $600\text{ RPM}$ for $1.0\text{s}$ post-halt.
  5. Send `CMD.simRpm=0`; wait $300\text{ ms}$ for spindle spin-down.

* **Pass Criteria:**
  - Carriage coordinate halts at $-45,000\text{ du} \pm 2\text{ du}$ ($\pm 0.2\ \mu\text{m}$).
  - `s.is_on` transitions to `False` upon reaching target distance.
  - Position remains completely stationary ($0\text{ du}$ drift) throughout the $1.0\text{s}$ hold window while the spindle continues spinning at $600\text{ RPM}$.

* **Fail Criteria:**
  - Carriage overshoots past $-45,002\text{ du}$ or stops short ($>2\text{ du}$ error).
  - Carriage creeps or advances while spindle continues spinning during the hold window.
  - Controller fails to disengage motion within $6.0\text{s}$ timeout.

---

### 6.2 `gearbox_at_speed_x_feed` (Suite: `test_gearbox.py`)

* **Real-Life Machining Scenario (Facing to Workpiece Center):**  
  The spindle is spinning at $600\text{ RPM}$. The machinist faces off the raw end of a part, feeding the cross-slide inward from the outside diameter towards workpiece centerline ($X = 0$). When the tool reaches the center, the cross-slide feed automatically trips out to prevent overrunning past center or loading the cross-slide leadscrew. The spindle continues spinning at $600\text{ RPM}$ while the cut is disengaged.

* **Objective:**  
  Verify on-the-fly synchronized cross-slide feed engagement at operating speed, accurate $0.500\text{ mm/rev}$ pitch tracking on Axis X, and zero post-halt creep.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER,dist:15000
  CMD.simRpm=0
  ```

* **Physical Simulation Stimulus:**  
  Virtual spindle at $600\text{ RPM}$ ($10\text{ rev/sec}$), steady-state rotation during the $15.0\text{ mm}$ cross-feed cut ($30\text{ revs}$ at $0.500\text{ mm/rev}$), and continuous $600\text{ RPM}$ during a $1.0\text{s}$ post-halt hold window.

* **Lifecycle Sequence:**
  1. Send `CMD.simRpm=600`; wait for `s.rpm >= 580`.
  2. Send `CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER,dist:15000`; wait for `s.is_on == True`.
  3. Wait for `s.is_on == False` (controller auto-halts at $-15,000\text{ du}$).
  4. **Anti-Creep Hold Window:** Hold spindle at $600\text{ RPM}$ for $1.0\text{s}$.
  5. Send `CMD.simRpm=0`; settle for $300\text{ ms}$.

* **Pass Criteria:**
  - Cross-slide halts at $-15,000\text{ du} \pm 2\text{ du}$.
  - Zero creep ($0\text{ du}$ drift) during the $1.0\text{s}$ post-halt hold window at $600\text{ RPM}$.
  - `s.is_on` is `False`.

* **Fail Criteria:**
  - Cross-slide position error exceeds $\pm 2\text{ du}$.
  - Cross-slide creeps past target while spindle rotates.
  - Timeout exceeding $6.0\text{s}$.

---

### 6.3 `gearbox_cold_start_ramp` (Suite: `test_gearbox.py`)

* **Real-Life Machining Scenario (Threading Pass from Standstill / Foot-Brake Restart):**  
  The machinist sets up an external single-point threading pass. With the spindle stopped ($0\text{ RPM}$), they engage electronic threading synchronization. They switch the motor switch ON, and the motor accelerates through its inertia curve to $600\text{ RPM}$. The electronic leadscrew must track the encoder pulses through every degree of the startup ramp, maintaining exact thread pitch synchronization without losing steps or cross-threading the existing thread groove.

* **Objective:**  
  Verify synchronized electronic gearing when motion is armed while stationary and the spindle accelerates through a dynamic velocity ramp.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK,dist:20000
  CMD.simRpm=600
  CMD.simRpm=0
  ```

* **Physical Simulation Stimulus:**  
  Spindle begins at $0\text{ RPM}$, then accelerates to $600\text{ RPM}$ while feed is already engaged. Spindle continues spinning until $20.0\text{ mm}$ ($20\text{ revs}$ at $1.000\text{ mm/rev}$) is completed, followed by a $1.0\text{s}$ anti-creep hold at $600\text{ RPM}$.

* **Lifecycle Sequence:**
  1. Send `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK,dist:20000`; wait for `s.is_on == True` at $0\text{ RPM}$.
  2. Send `CMD.simRpm=600` (acceleration ramp begins).
  3. Wait for `s.is_on == False` (auto-halt at $-20,000\text{ du}$).
  4. Hold at $600\text{ RPM}$ for $1.0\text{s}$.
  5. Send `CMD.simRpm=0`; settle for $300\text{ ms}$.

* **Pass Criteria:**
  - Carriage halts at $-20,000\text{ du} \pm 2\text{ du}$.
  - Zero lost steps or pitch distortion during acceleration ramp.
  - Zero creep during $1.0\text{s}$ post-halt hold window.

* **Fail Criteria:**
  - Coordinate mismatch $>2\text{ du}$.
  - Motion fails to start on spindle acceleration.
  - Timeout exceeding $6.0\text{s}$.

---

### 6.4 `gearbox_stop_command` (Suite: `test_gearbox.py`)

* **Real-Life Machining Scenario (Operator Feed Hold on Tool Chatter):**  
  While taking a heavy roughing cut at $600\text{ RPM}$, the operator notices severe tool chatter or poor chip curling and taps **STOP / FEED HOLD** on the touchscreen. The carriage leadscrew must stop instantly to prevent tool breakage, while the spindle continues spinning at full cutting speed so the workpiece does not stall or weld onto the carbide insert.

* **Objective:**  
  Verify that `CMD.stop` disengages physical stepping immediately ($<100\text{ ms}$) while the spindle continues spinning at full cutting speed, with zero post-stop carriage creep.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.stop
  CMD.simRpm=0
  ```

* **Physical Simulation Stimulus:**  
  Continuous $600\text{ RPM}$ spindle rotation throughout the entire test, remaining at $600\text{ RPM}$ during both active cutting and after the stop command is received.

* **Lifecycle Sequence:**
  1. Send `CMD.simRpm=600`; wait for `s.rpm >= 580`.
  2. Send `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL`; wait for `s.is_on == True`.
  3. Send `CMD.stop` while spindle continues rotating.
  4. Wait for `s.is_on == False` (halt confirmation).
  5. Record halted position $Z_0$; settle for $500\text{ ms}$ while spindle continues turning at $600\text{ RPM}$.
  6. Assert $Z_{500\text{ms}} == Z_0$.
  7. Send `CMD.simRpm=0`; settle for $300\text{ ms}$.

* **Pass Criteria:**
  - `s.is_on` transitions to `False` in $<1.0\text{s}$.
  - Post-halt carriage position is rock-solid ($Z_{500\text{ms}} == Z_0$, exactly $0\text{ du}$ drift) while spindle spins at $600\text{ RPM}$.

* **Fail Criteria:**
  - Carriage continues creeping after stop confirmation.
  - `is_on` remains `True`.

---

### 6.5 `soft_limit_at_speed_chuck_halt` (Suite: `test_soft_limits.py`)

* **Real-Life Machining Scenario (Turning up to Chuck / Shoulder Stop):**  
  The machinist mounts a short workpiece in a 3-jaw chuck. To turn the entire length without crashing into the spinning chuck jaws, they set a virtual Left soft stop $5\text{ mm}$ in front of the chuck face ($-50.000\text{ mm}$). With the spindle running at $600\text{ RPM}$, they engage feed towards the chuck. The motion engine continuously evaluates lookahead distance, cleanly decelerates to a halt at the soft stop, and transitions to disengaged without striking the chuck jaws.

* **Objective:**  
  Verify soft limit lookahead enforcement, clean boundary deceleration, and zero overrun when feeding towards a Left boundary stop under continuous spindle rotation.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,L,-50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.simRpm=0
  CMD.axisClearLimit=Z,L
  ```

* **Physical Simulation Stimulus:**  
  Continuous $600\text{ RPM}$ virtual spindle rotation throughout approach, boundary arrival, and a $1.0\text{s}$ post-halt dwell window.

* **Lifecycle Sequence:**
  1. Set Left limit at $-50,000\text{ du}$; wait for `s.z_left_du == -50000`.
  2. Send `CMD.simRpm=600`; wait for `s.rpm >= 580`.
  3. Send `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK`; wait for `s.is_on == True`.
  4. Wait for `s.is_on == False` (automatic halt at soft shoulder).
  5. **Anti-Creep Hold Window:** Hold at $600\text{ RPM}$ for $1.0\text{s}$.
  6. Send `CMD.simRpm=0`; clear limit.

* **Pass Criteria:**
  - Carriage halts at $-50,000\text{ du} \pm 2\text{ du}$ without touching or overrunning past $-50,002\text{ du}$.
  - Zero creep during $1.0\text{s}$ hold window at $600\text{ RPM}$.
  - `s.is_on` is `False`.

* **Fail Criteria:**
  - Carriage overruns past $-50,002\text{ du}$ (simulated chuck crash).
  - Controller fails to halt within $8.0\text{s}$.
  - Carriage creeps past $-50,000\text{ du}$ during post-halt hold window.

---

### 6.6 `soft_limit_at_speed_tail_halt` (Suite: `test_soft_limits.py`)

* **Real-Life Machining Scenario (Reverse Turning / Tailstock Crash Prevention):**  
  The machinist is boring out an internal cavity or reverse-turning towards the tailstock. To prevent the carriage from colliding with the tailstock casting or running off the leadscrew threads at the end of the lathe bed, a Right soft stop is set at $+50.000\text{ mm}$. The carriage feeds in reverse at $600\text{ RPM}$ and halts cleanly at the boundary stop.

* **Objective:**  
  Verify soft limit enforcement and zero overrun in the positive travel direction ($+Z$, towards tailstock).

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisLimitDu=Z,R,50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.simRpm=0
  CMD.axisClearLimit=Z,R
  ```

* **Pass Criteria:**
  - Carriage halts at $+50,000\text{ du} \pm 2\text{ du}$.
  - Zero creep during $1.0\text{s}$ hold window at $600\text{ RPM}$.

* **Fail Criteria:**
  - Carriage overruns past $+50,002\text{ du}$.

---

### 6.7 `axis_zero_and_preset` (Suite: `test_coords_and_presets.py`)

* **Real-Life Machining Scenario (Touch-Off on Part Face & Gauge Block Offset):**  
  The operator edges up the tool to kiss the end face of a newly chucked workpiece. They tap `Ø Z` on the touch DRO, establishing the part zero origin ($Z = 0.000\text{ mm}$). Then, to calibrate cross-slide tool offset, they touch off against a $10.000\text{ mm}$ gauge block on the part diameter and use the DRO numeric preset modal to enter $+10.000\text{ mm}$ directly on the X axis.

* **Objective:**  
  Verify that coordinate presetting (`CMD.axisPosDu`) and coordinate zeroing (`CMD.axisZero`) update workpiece coordinates instantly and accurately on both axes with zero position bounce.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisPosDu=Z,250000
  CMD.axisZero=Z
  CMD.axisPosDu=X,100000
  CMD.axisZero=X
  ```

* **Lifecycle Sequence:**
  1. Preset Z to $+250,000\text{ du}$ ($+25.000\text{ mm}$); verify `s.z_du == 250000`.
  2. Zero Z; verify `s.z_du == 0`.
  3. Preset X to $+100,000\text{ du}$ ($+10.000\text{ mm}$); verify `s.x_du == 100000`.
  4. Zero X; verify `s.x_du == 0`.
  5. **Settle Window:** Wait $300\text{ ms}$; verify both axes remain rock-solid at $0\text{ du}$.

* **Pass Criteria:**
  - Exact coordinate matching ($0\text{ du}$ error) on all presets and zeroes.
  - Zero coordinate bounce or jitter after $300\text{ ms}$ settle window.

* **Fail Criteria:**
  - Coordinate does not update within $3.0\text{s}$.
  - Position reports non-zero after `axisZero`.

---

### 6.8 `stop_command_latency` (Suite: `test_emergency.py`)

* **Real-Life Machining Scenario (Deterministic Emergency Feed Interrupt):**  
  During an active feed at $600\text{ RPM}$, the operator hits emergency stop / feed hold. For operator safety and machine protection, the motion engine must extinguish stepper pulses in under $100\text{ ms}$ (less than one single spindle revolution at $600\text{ RPM}$).

* **Objective:**  
  Measure the round-trip latency from dispatching `CMD.stop` to the moment physical stepping is terminated (`is_on == False`).

* **Latency Budget & Network Considerations:**  
  The measured time $T_{\text{elapsed}}$ spans:
  1. Client dispatch and Wi-Fi transmission.
  2. Core 0 WebSocket frame ingestion and `CommandDispatcher` processing.
  3. Core 0 to Core 1 command queue delivery and motion deceleration.
  4. Core 1 to Core 0 `TelemetryMailbox` commit and 50 Hz outbound serialization.  
  Because the outbound telemetry rate is 50 Hz, state reporting has a discrete quantization floor of $20\text{ ms}$. To minimize Wi-Fi stack latency and jitter during HIL runs, the ESP32-S3 Wi-Fi power-save mode must be disabled (`esp_wifi_set_ps(WIFI_PS_NONE)`) and `TCP_NODELAY` must be configured on the WebSocket server socket.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.simRpm=600
  CMD.stop
  ```

* **Pass Criteria:**
  - Stop latency $T_{\text{elapsed}} < 100.0\text{ ms}$.
  - Controller state reports `is_on == False`.

* **Fail Criteria:**
  - Stop latency exceeds $100.0\text{ ms}$.
  - Controller remains in motion.

---

### 6.9 `gearing_sync_1_5mm_pitch` (Suite: `test_gearing.py`)

* **Real-Life Machining Scenario (Discrete Forward Spindle Synchronization):**  
  Carriage engages a forward longitudinal feed of $1.5\text{ mm/rev}$ pitch towards the chuck. Two complete revolutions of the spindle ($2 \times 4 \times PPR$ counts, e.g. $9600$ quadrature counts for $1200\text{ PPR}$) must advance the carriage by exactly $-3.0\text{ mm}$ ($-30{,}000\text{ du}$).

* **Objective:**  
  Verify mathematical accuracy and step generation of the Bresenham synchronizer over exact discrete pulse injection without accumulated floating-point round-off, while testing the pulse slewer under large discrete count dumps.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK
  CMD.simPulses=9600
  ```
  *(Counts computed dynamically as `int(2.0 * client.state.encoder_ppr * 4)`)*

* **Pass Criteria:**
  - Carriage coordinate lands at $Z = -30{,}000\text{ du}$ with $\le 2\text{ du}$ tolerance ($0.2\,\mu\text{m}$).
  - Discrete pulses are consumed smoothly across motion ticks without velocity overshoot or step loss.

* **Fail Criteria:**
  - $Z$ coordinate deviates by more than $2\text{ du}$.

---

### 6.10 `gearing_sync_reverse_1_0mm` (Suite: `test_gearing.py`)

* **Real-Life Machining Scenario (Discrete Reverse Spindle Synchronization):**  
  Carriage engages a reverse feed of $1.0\text{ mm/rev}$ pitch towards the tailstock ($+Z$). One complete revolution ($1 \times 4 \times PPR$ counts, e.g. $4800$ quadrature counts) must advance the carriage by exactly $+1.0\text{ mm}$ ($+10{,}000\text{ du}$).

* **Objective:**  
  Verify reverse direction gearing accuracy and signed step pulse generation.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.simPulses=4800
  ```
  *(Counts computed dynamically as `int(1.0 * client.state.encoder_ppr * 4)`)*

* **Pass Criteria:**
  - Carriage coordinate lands at $Z = +10{,}000\text{ du}$ with $\le 2\text{ du}$ tolerance.

* **Fail Criteria:**
  - $Z$ coordinate deviates by more than $2\text{ du}$.

---

### 6.11 `manual_jogging` (Suite: `test_jogging.py`)

* **Real-Life Machining Scenario (Manual Machine Setup & Positioning):**  
  The machinist uses touch jog controls on the HMI to position the tool. Incremental single-step jogging must advance the carriage or cross-slide by exact fixed increments ($1.0\text{ mm}$ / $10{,}000\text{ du}$) on both axes ($Z$ and $X$). Continuous jogging (`jogHold`) must feed until released (`jogRelease`) and halt cleanly without creeping.

* **Objective:**  
  Verify `CMD.jogStep` incremental moves, `CMD.jogHold` continuous motion, and `CMD.jogRelease` termination via the thread-safe inter-core queue across both axes ($Z$ and $X$) with explicit intermediate step verifications.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.axisZero=Z
  CMD.axisZero=X
  CMD.jogStepDu=10000
  CMD.jogStep=Z,TAIL
  CMD.jogStep=Z,CHUCK
  CMD.jogStep=X,RETRACT
  CMD.jogStep=X,INFEED
  CMD.jogHold=Z,TAIL
  CMD.jogRelease=Z
  ```

* **Lifecycle Sequence & Intermediate Checks:**
  1. `CMD.jogStep=Z,TAIL` $\to$ wait for motion stop; assert $Z == +10{,}000\text{ du} \pm 0\text{ du}$.
  2. `CMD.jogStep=Z,CHUCK` $\to$ wait for motion stop; assert $Z == 0\text{ du} \pm 0\text{ du}$.
  3. `CMD.jogStep=X,RETRACT` $\to$ wait for motion stop; assert $X == +10{,}000\text{ du} \pm 0\text{ du}$.
  4. `CMD.jogStep=X,INFEED` $\to$ wait for motion stop; assert $X == 0\text{ du} \pm 0\text{ du}$.
  5. `CMD.jogHold=Z,TAIL` $\to$ monitor progress; assert $Z \ge 5{,}000\text{ du}$.
  6. `CMD.jogRelease=Z` $\to$ record position $Z_{\text{rel}}$, settle $300\text{ ms}$; assert creep $|Z_{300\text{ms}} - Z_{\text{rel}}| \le 100\text{ du}$.

* **Pass Criteria:**
  - All four incremental step jogs land at exact commanded coordinates without cumulative step drift.
  - Continuous jog travels $\ge 5{,}000\text{ du}$ and halts immediately upon `CMD.jogRelease` (creep $\le 100\text{ du}$).

* **Fail Criteria:**
  - Axis coordinate error exceeds $0\text{ du}$ on any step jog.
  - Creep after release exceeds $100\text{ du}$.

---

### 6.12 `sim_watchdog_timeout_halt` (Suite: `test_emergency.py`)

* **Real-Life Machining Scenario (Host Controller / Network Disconnect During Live Cut):**  
  While turning or threading with electronic spindle synchronization at $600\text{ RPM}$, the client UI or supervisory host crashes, hangs, or experiences an abrupt Wi-Fi link drop. The embedded motion engine must detect the loss of supervisory communication, automatically decelerate all active feeds to a complete halt within $1.5\text{s}$, and disengage virtual simulation injection to prevent runaway feed carriage crashes.

* **Objective:**  
  Verify that the Core 1 hardware failsafe watchdog automatically disengages motion (`is_on == False`) and zeroes simulated RPM when client heartbeats cease for $>1.5\text{s}$.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  # [Client suppresses all background heartbeats for 1.8s]
  # [Client reconnects / polls status]
  ```

* **Lifecycle Sequence:**
  1. Send `CMD.simRpm=600`; wait for `s.rpm >= 580`.
  2. Send `CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK`; wait for `s.is_on == True`.
  3. Call `client.suppress_heartbeat(True)` to intentionally cease all `CMD.ping` frames.
  4. Passively wait $1.8\text{s}$ (exceeding the $1.5\text{s}$ watchdog trip threshold).
  5. Call `client.suppress_heartbeat(False)` to resume telemetry polling.
  6. Query state; assert failsafe stop has executed.

* **Pass Criteria:**
  - Controller state reports `is_on == False` (feed disengaged).
  - Simulated spindle RPM is automatically zeroed (`s.rpm == 0`).
  - Carriage position remains frozen at the failsafe shutdown point without subsequent motion.

* **Fail Criteria:**
  - Controller remains in motion (`is_on == True`) after $1.8\text{s}$ without heartbeat.
  - Watchdog fails to trip, or causes an unhandled panic/watchdog reset rather than an orderly deceleration halt.

---

### 6.13 `gearing_multipass_index_sync` (Suite: `test_gearing.py`)

* **Real-Life Machining Scenario (Multi-Pass Thread Tracking & Phase Lock):**  
  When single-point threading, the tool takes multiple cutting passes across the same thread groove. After pass 1, the machinist retracts $X$, rapids $Z$ back to start ($Z = 0.000\text{ mm}$), and engages pass 2 while the spindle continuously turns at $600\text{ RPM}$. The controller must not start stepping immediately upon engagement; it must enter a phase-lock wait state (`WAITING_FOR_INDEX`), arming motion only when the spindle reaches the exact same rotational index angle ($\pm 1^{\circ}$) as pass 1.

* **Objective:**  
  Verify spindle angle synchronization and phase-lock re-engagement accuracy across multiple consecutive threading passes under continuous spindle rotation.

* **Parameters & Inbound Wire Commands:**
  ```text
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:15000
  # [Pass 1 completes at -15.000 mm]
  CMD.axisPosDu=Z,0
  CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:15000
  # [Pass 2 re-engages and completes at -15.000 mm]
  CMD.simRpm=0
  ```

* **Pass Criteria:**
  - Pass 1 and Pass 2 both complete at $-15{,}000\text{ du} \pm 2\text{ du}$.
  - The spindle angle at motion commencement on Pass 2 matches Pass 1 within $\pm 0.5^{\circ}$.
  - Zero lost steps or cumulative phase drift across consecutive passes.

* **Fail Criteria:**
  - Spindle engagement angle diverges by $>1.0^{\circ}$ between passes.
  - Final position differs between passes by $>2\text{ du}$.

---

## 7. CLI Runner & Output Formats

The test harness is executed via `run_tests.py`:

```bash
# Run all 13 tests against default hostname (nanoels.local)
python3 tests/hil/run_tests.py

# Run against a specific IP address
python3 tests/hil/run_tests.py --host 192.168.10.22

# Filter specific tests by keyword
python3 tests/hil/run_tests.py --filter gearing
python3 tests/hil/run_tests.py --filter watchdog

# Output machine-readable JSON summary for CI or analysis
python3 tests/hil/run_tests.py --json > test_report.json
```

### 7.1 Human-Readable ANSI Terminal Output

```
================================================================================
                    NANOELS HARDWARE-IN-THE-LOOP TEST SUITE                    
Target: ws://192.168.10.22:81/canonical | Firmware: v5.38 (ESP32-S3)
================================================================================
TEST NAME                          STATUS    DURATION   DETAILS
--------------------------------------------------------------------------------
axis_zero_and_preset               [PASS]    0.68s      tested_z_preset: 250000, tested_x_preset: 100000
gearbox_at_speed_x_feed            [PASS]    2.62s      spindle_rpm: 600, expected_x_du: -15000
gearbox_at_speed_z_feed            [PASS]    2.20s      spindle_rpm: 600, expected_z_du: -45000
gearbox_cold_start_ramp            [PASS]    1.80s      expected_z_du: -20000, actual_z_du: -20000
gearbox_stop_command               [PASS]    1.41s      halted_z_du: 2400, settled_z_du: 2400
gearing_multipass_index_sync       [PASS]    2.85s      pass1_angle: 142.3 deg, pass2_angle: 142.4 deg
gearing_sync_1_5mm_pitch           [PASS]    0.34s      expected_z_du: -30000, actual_z_du: -30000
gearing_sync_reverse_1_0mm         [PASS]    0.37s      expected_z_du: 10000, actual_z_du: 10000
manual_jogging                     [PASS]    2.91s      step_jog_du: 10000, continuous_hold_reached_du: 6000
sim_watchdog_timeout_halt          [PASS]    2.15s      timeout_trip_ms: 1502, halted_is_on: False
soft_limit_at_speed_chuck_halt     [PASS]    2.41s      limit_du: -50000, halted_z_du: -50000
soft_limit_at_speed_tail_halt      [PASS]    2.37s      limit_du: 50000, halted_z_du: 50000
stop_command_latency               [PASS]    0.30s      stop_latency_ms: 62.58
--------------------------------------------------------------------------------
SUMMARY: 13 Passed, 0 Failed, 0 Errors | Total Time: 22.41s
RESULT: ALL TESTS PASSED (Exit code: 0)
================================================================================
```

### 7.2 Machine-Readable JSON Schema (`--json`)

```json
{
  "timestamp": "2026-10-19T16:35:00Z",
  "target": "ws://192.168.10.22:81/canonical",
  "firmware_version": "5.38",
  "total": 13,
  "passed": 13,
  "failed": 0,
  "duration_s": 22.41,
  "results": [
    {
      "name": "gearing_sync_1_5mm_pitch",
      "status": "PASS",
      "duration_s": 0.34,
      "details": {
        "expected_z_du": -30000,
        "actual_z_du": -30000,
        "error_du": 0
      }
    },
    {
      "name": "soft_limit_at_speed_chuck_halt",
      "status": "PASS",
      "duration_s": 2.41,
      "details": {
        "limit_du": -50000,
        "halted_z_du": -50000,
        "error_du": 0
      }
    },
    {
      "name": "sim_watchdog_timeout_halt",
      "status": "PASS",
      "duration_s": 2.15,
      "details": {
        "timeout_trip_ms": 1502,
        "halted_is_on": false,
        "halted_rpm": 0
      }
    }
  ]
}
```

### 7.3 Dynamic Test Discovery & Execution Engine (`run_tests.py`)

`run_tests.py` dynamically scans the `tests/hil/suites/` directory to discover all subclasses of `HilTestCase`, allowing any developer to add a new test file without modifying runner registration logic:

```python
# tests/hil/run_tests.py

import argparse
import asyncio
import importlib
import inspect
from pathlib import Path
import pkgutil
import sys

from core.base_test import HilTestCase
from core.client import NanoElsClient
from core.reporter import print_terminal_report, emit_json_report

def discover_test_classes(filter_keyword: str = "") -> list[type[HilTestCase]]:
    """Discovers all HilTestCase subclasses inside tests/hil/suites/."""
    suites_dir = Path(__file__).parent / "suites"
    test_classes = []

    for _, module_name, _ in pkgutil.iter_modules([str(suites_dir)]):
        module = importlib.import_module(f"suites.{module_name}")
        for attr_name in dir(module):
            attr = getattr(module, attr_name)
            if (
                inspect.isclass(attr)
                and issubclass(attr, HilTestCase)
                and attr is not HilTestCase
            ):
                if not filter_keyword or filter_keyword.lower() in attr.name.lower():
                    test_classes.append(attr)

    test_classes.sort(key=lambda cls: cls.name)
    return test_classes

async def main_async() -> int:
    parser = argparse.ArgumentParser(description="NanoELS Hardware-in-the-Loop Test Runner")
    parser.add_argument("--host", default="nanoels.local", help="Controller IP/Hostname (default: nanoels.local)")
    parser.add_argument("--port", type=int, default=81, help="WebSocket port (default: 81)")
    parser.add_argument("--filter", default="", help="Case-insensitive test name filter")
    parser.add_argument("--json", action="store_true", help="Emit machine-readable JSON output")
    args = parser.parse_args()

    test_classes = discover_test_classes(args.filter)
    if not test_classes:
        print(f"No test cases found matching filter: '{args.filter}'")
        return 1

    client = NanoElsClient(host=args.host, port=args.port)
    await client.connect()

    results = []
    try:
        for test_cls in test_classes:
            test_instance = test_cls(client)
            result = await test_instance.execute()
            results.append(result)
    finally:
        await client.disconnect()

    if args.json:
        emit_json_report(results, host=args.host)
    else:
        print_terminal_report(results, host=args.host)

    return 0 if all(r.status == "PASS" for r in results) else 1

if __name__ == "__main__":
    sys.exit(asyncio.run(main_async()))
```

---

## 8. Implementation Plan & Work Breakdown

The implementation is partitioned into three sequential slices following the Pair Programming Protocol:

```
┌────────────────────────────────────────────────────────────────────────┐
│                   HIL HARNESS IMPLEMENTATION PHASES                    │
├─────────────────────┬──────────────────────────────────────────────────┤
│ Phase               │ Deliverables & Scope                             │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 1: Firmware   │ • Add virtual injection to ISpindleEncoderHAL    │
│ Simulation Bridge   │ • Implement pulse slewer (m_maxSimPulseChunk)    │
│                     │ • Implement Core 1 sync mode transition reset    │
│                     │ • Wire CMD.sim* in CommandDispatcher.cpp         │
│                     │ • Add 1.5s atomic safety watchdog in firmware    │
│                     │ • Add TCP_NODELAY & WIFI_PS_NONE latency flags   │
│                     │ • Add HIL_SIMULATION_ENABLED compile guard       │
│                     │ • Verify build via `pio run -e esp32s3`          │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 2: Python     │ • Implement `tests/hil/core/client.py`:          │
│ Core Framework      │   - Synchronous ACK (OK:CMD.* / ERR:*) handling  │
│                     │   - 500ms background heartbeat task              │
│                     │   - suppress_heartbeat() failsafe test hook      │
│                     │   - Dynamic CPR calculation (encoder_ppr * 4)    │
│                     │ • Implement `tests/hil/core/base_test.py`        │
│                     │ • Implement `tests/hil/core/assertions.py`       │
│                     │ • Implement `tests/hil/run_tests.py` CLI runner  │
│                     │ • Verify connection against live ESP32-S3        │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 3: Test Suite │ • Implement all 6 modular test suites (13 tests):│
│ Catalog & Run       │   - `test_gearbox.py` (at-speed Z/X, cold start) │
│                     │   - `test_gearing.py` (forward, reverse, sync)   │
│                     │   - `test_soft_limits.py` (chuck and tail halts) │
│                     │   - `test_coords_and_presets.py` (DRO zeroes)    │
│                     │   - `test_jogging.py` (step and continuous)      │
│                     │   - `test_emergency.py` (latency, watchdog halt) │
│                     │ • Execute full test suite against hardware       │
│                     │ • Record baseline test report in docs/testing/   │
└─────────────────────┴──────────────────────────────────────────────────┘
```

---

## 9. Acceptance Criteria (Definition of Done)

The HIL test harness is considered complete and operational when:
1. `pio run -e esp32s3` compiles cleanly with zero warnings or errors with `CMD.sim*` hooks and safety interlocks enabled.
2. The firmware automatically times out and executes an orderly deceleration stop if the test runner halts or drops heartbeats for $>1.5\text{s}$ (verified by `sim_watchdog_timeout_halt`).
3. Discrete pulse injection tests (`CMD.simPulses`) execute without kinematic step-rate shock or timer overflows due to pulse slewing.
4. The Python harness connects via WebSocket, executes all 13 test cases across the 6 modular suites with sequential ACK verification, and outputs both ANSI formatted console logs and `--json` reports.
5. Gearing pitch accuracy assertion confirms **0 step drift** on synchronized feeds over physical hardware.
6. Soft limit assertion confirms carriage halts cleanly at the shoulder without step overshoot.
7. Multi-pass threading test confirms repeatable phase lock synchronization within $\pm 0.5^{\circ}$ of spindle rotation.
8. Stop command latency tests confirm physical motion termination within $<100\text{ ms}$.
