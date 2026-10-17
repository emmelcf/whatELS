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
│            │                      │            │ (Inter-core Queue)    │
│            │  ws://<ip>:81        │            ▼                       │
│            │  /canonical          │  Core 1 (Real-Time Motion):        │
│            ▼                      │   ├── MotionEngine (prio 22)       │
│      [ Outbound Commands ] ───────┼──►├── Esp32SpindleEncoderHAL       │
│       CMD.startFeed=...           │   │    └── Virtual Injection Mode  │
│       CMD.simPulses=4096          │   ├── Esp32StepperHAL (GPTimer)    │
│                                   │   └── TelemetryMailbox (Seqlock)   │
│      [ Inbound 50Hz Telemetry ] ◄─┼────────────────────────────────────┘
│       K.zDu=-15000, K.on=0        │
└───────────────────────────────────┴────────────────────────────────────┘
```

---

## 2. Firmware Simulation Injection Contract

To execute automated tests against physical hardware sitting on a developer's bench without mechanical spindle rotation, the firmware provides a software injection bridge directly into `ISpindleEncoderHAL`.

### 2.1 Virtual Spindle Encoder HAL Interface

`ISpindleEncoderHAL` is extended with three non-blocking virtual injection primitives:

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
     *               When transitioning from true to false, re-zeros the hardware PCNT baseline.
     */
    virtual void setSimulationActive(bool active) = 0;

    /**
     * @brief Checks if virtual injection mode is currently active.
     */
    virtual bool isSimulationActive() const = 0;

    /**
     * @brief Injects an exact discrete pulse delta.
     * @param delta Signed pulse count (e.g. 4096 = 1 spindle rev in forward direction).
     *              Accumulates atomically; consumed on subsequent getPulseDelta() calls.
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

In `src/motion/hal/Esp32SpindleEncoderHAL.h` and `.cpp`, simulation mode introduces thread-safe atomic members and precision integration math:

```cpp
// Member variables added to Esp32SpindleEncoderHAL:
std::atomic<bool>     m_simActive{false};
std::atomic<float>    m_simRpm{0.0f};
std::atomic<int32_t>  m_pendingSimPulses{0};
int64_t               m_lastSimTimeUs{0};
float                 m_simFraction{0.0f};
```

**Pulse Integration Math in `Esp32SpindleEncoderHAL::getPulseDelta()`:**
When `m_simActive.load()` is true, `getPulseDelta()` computes pulses from continuous RPM and drains any discrete manual pulse additions:

```cpp
int32_t Esp32SpindleEncoderHAL::getPulseDelta() {
    if (m_simActive.load(std::memory_order_relaxed)) {
        int64_t nowUs = esp_timer_get_time();
        float dt = (m_lastSimTimeUs > 0) ? static_cast<float>(nowUs - m_lastSimTimeUs) * 1e-6f : 0.0f;
        m_lastSimTimeUs = nowUs;
        if (dt > 0.05f) dt = 0.05f; // Clamp delta-time to 50ms ceiling to prevent runaway bursts after delays

        // Integrate continuous rotational RPM:
        float rpm = m_simRpm.load(std::memory_order_relaxed);
        float counts = (rpm / 60.0f) * m_countsPerSpindleRev * dt + m_simFraction;
        int32_t delta = static_cast<int32_t>(counts);
        m_simFraction = counts - static_cast<float>(delta);

        // Atomically drain and add discrete pulse injection (e.g., from CMD.simPulses):
        int32_t manual = m_pendingSimPulses.exchange(0, std::memory_order_relaxed);
        delta += manual;

        m_accumulatedPulses += delta;
        updateRpmEstimate(delta, nowUs);
        return delta;
    }

    // ... standard physical PCNT hardware sampling when simulation is inactive ...
}
```

---

### 2.2 Wire Commands (`CMD.sim*`)

The test harness commands the simulation hooks using standard canonical text frames parsed by `CommandDispatcher.cpp`:

| Command Frame | Parameters | Description | Controller Response |
| :--- | :--- | :--- | :--- |
| `CMD.simEncoder=<0\|1>` | `0` or `1` | Enables or disables virtual encoder injection mode. | `OK:CMD.simEncoder\n` |
| `CMD.simPulses=<delta>` | Integer $\pm$ pulses | Injects a discrete number of spindle quadrature counts into Core 1. | `OK:CMD.simPulses\n` |
| `CMD.simRpm=<rpm>` | Float $\pm$ RPM | Commands a continuous spindle speed. The HAL automatically integrates `delta = rpm / 60 * ppr * dt` on every motion tick. | `OK:CMD.simRpm\n` |
| `CMD.simMpg=<axis>,<counts>` | Axis `Z\|X`, $\pm$ counts | Simulates discrete MPG handwheel detent pulses. | `OK:CMD.simMpg\n` |

### 2.2.1 Firmware Linkage (`CommandDispatcher` $\to$ `MotionEngine` $\to$ `ISpindleEncoderHAL`)

To route `CMD.sim*` frames without global pointer leaks:
1. `src/motion/kernel/MotionEngine.h` provides an explicit non-const getter:
   ```cpp
   ISpindleEncoderHAL* getEncoderHAL() { return m_encoderHal; }
   ```
2. `src/web/CommandDispatcher.cpp` dispatches directly through the singleton:
   ```cpp
   // CMD.simEncoder=<0|1>
   if (cmdLen >= 11 && strncmp(cmd, "simEncoder=", 11) == 0) {
       bool active = atoi(cmd + 11) != 0;
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

To guarantee that a disconnected or crashed test script never leaves physical steppers or simulated feeds running unattended:
1. **Heartbeat Registration:** Every `CMD.sim*` or `CMD.ping` command invokes `MotionEngine::instance().touchSimHeartbeat()`, updating an atomic millisecond timestamp `m_lastSimHeartbeatMs`.
2. **Core 1 Watchdog Evaluation:** Evaluated synchronously inside `MotionEngine::onMotionTick()` on Core 1:
   ```cpp
   if (m_encoderHal && m_encoderHal->isSimulationActive()) {
       int64_t nowMs = esp_timer_get_time() / 1000;
       if (nowMs - m_lastSimHeartbeatMs > 1500) {
           // Harness dropped offline or timed out: force immediate failsafe stop
           m_encoderHal->setSimulatedRpm(0.0f);
           m_encoderHal->setSimulationActive(false);
           stopAll(false); // Orderly normal deceleration stop
       }
   }
   ```

---

## 3. Test Harness Architecture & Python Framework

The test harness is implemented in Python 3.10+ using `asyncio` and `websockets`. It contains no heavy dependencies and requires only `pip install websockets`.

### 3.1 Directory Layout

```
tests/hil/
├── requirements.txt            # websockets>=12.0
├── run_tests.py                # Main test runner CLI and test discoverer
├── core/
│   ├── __init__.py
│   ├── client.py               # NanoElsClient: Async WebSocket connection & state engine
│   ├── assertions.py           # Domain assertion helpers (within_du, halted_at, ratio_equals)
│   ├── base_test.py            # HilTestCase abstract base class
│   └── reporter.py             # ANSI Terminal & JSON reporting formatters
└── suites/
    ├── __init__.py
    ├── test_gearing.py         # Spindle-synchronized feed & pitch accuracy tests
    ├── test_soft_limits.py     # Soft limits, shoulder lookahead, and halt reason tests
    ├── test_threading.py       # Threading passes, multi-start index synchronization
    ├── test_jogging.py         # Continuous and discrete jog increments
    ├── test_emergency.py       # Stop command latency and E-stop state lockout
    └── test_settings.py        # Settings persistence and hydration verification
```

---

## 4. `NanoElsClient` Specification & State Engine

`NanoElsClient` manages the asynchronous transport, incoming packet deserialization, state maintenance, and condition waiting.

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
    z_left_du: int = 2147483647
    z_right_du: int = -2147483648
    x_up_du: int = 2147483647
    x_down_du: int = -2147483648
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
    encoder_ppr: int = 2400
    
    # Raw dictionary of all received keys
    raw: dict[str, str] = field(default_factory=dict)
```

### 4.2 Core Async Client API

```python
class NanoElsClient:
    def __init__(self, host: str = "nanoels.local", port: int = 81):
        self.uri = f"ws://{host}:{port}/canonical"
        self.state = ControllerState()
        self._ws = None
        self._listeners = []
        self._rx_task = None
        
    async def connect(self, timeout_s: float = 5.0) -> None:
        """Connects to controller and waits for initial hydration frame."""
        ...

    async def disconnect(self) -> None:
        """Safely stops active feeds and closes socket cleanly."""
        ...

    async def send(self, cmd: str) -> None:
        """Transmits a newline-delimited command frame."""
        clean = cmd.strip() + "\n"
        await self._ws.send(clean)

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
        Calculates exact pulse counts for `revs` revolutions based on state.encoder_ppr,
        injects them, and waits for completion.
        """
        ...
```

### 4.3 Multi-Line Frame Ingestion & Parsing Details

The ESP32-S3 `WebServerModule` transmits WebSocket packets that batch multiple domain lines separated by newline (`\n`) characters within a single TCP/WebSocket frame payload (e.g. `K.zDu=-15000\nK.rpm=600\nK.on=1\n`).

`NanoElsClient._rx_loop()` handles this deterministically:

```python
# tests/hil/core/client.py

async def _rx_loop(self) -> None:
    """Continuously ingests WebSocket frames and splits batched newline-delimited lines."""
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
                if self._parse_telemetry_line(line):
                    changed = True

            if changed:
                # Notify any pending wait_for predicates
                for listener in list(self._listeners):
                    listener(self.state)
    except asyncio.CancelledError:
        pass

def _parse_telemetry_line(self, line: str) -> bool:
    """Parses canonical K.*, I.*, C.*, P.* lines into self.state."""
    if "=" not in line:
        return False
    key, val = line.split("=", 1)
    key = key.strip()
    val = val.strip()
    self.state.raw[key] = val

    # Kinematics Domain
    if key == "K.zDu":
        self.state.z_du = int(val)
    elif key == "K.xDu":
        self.state.x_du = int(val)
    elif key == "K.rpm":
        self.state.rpm = int(val)
    elif key == "K.turns":
        self.state.turns = float(val)
    elif key == "K.angle":
        self.state.angle = float(val)
    elif key == "K.on":
        self.state.is_on = (val == "1" or val.lower() == "true")
    elif key == "K.mode":
        self.state.mode = int(val)
    elif key == "K.estop":
        self.state.estop = int(val)
    elif key == "K.zLeftDu":
        self.state.z_left_du = int(val)
    elif key == "K.zRightDu":
        self.state.z_right_du = int(val)
    elif key == "K.xUpDu":
        self.state.x_up_du = int(val)
    elif key == "K.xDownDu":
        self.state.x_down_du = int(val)
    # Properties Domain
    elif key == "P.encoderPpr":
        self.state.encoder_ppr = int(val)
    elif key == "P.motorStepsZ":
        self.state.motor_steps_z = int(val)
    elif key == "P.screwZDu":
        self.state.screw_z_du = int(val)
    else:
        return False
    return True
```

---

## 5. Test Case Authoring DSL & Lifecycle

Every test inherits from `HilTestCase`. The runner guarantees that:
1. `setup()` prepares controller state (clears limits, stops motion, zeroes axes).
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
        """Default setup: stop any active feed, enable simulation mode, zero Z/X."""
        await self.client.send("CMD.stop")
        await self.client.send("CMD.simEncoder=1")
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.axisZero=Z")
        await self.client.send("CMD.axisZero=X")
        await self.client.wait_for(lambda s: s.z_du == 0 and s.x_du == 0 and not s.is_on)

    @abstractmethod
    async def run(self) -> None:
        """Test logic and assertions."""
        pass

    async def teardown(self) -> None:
        """Default teardown: stop feed, clear simulation mode."""
        await self.client.send("CMD.stop")
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.simEncoder=0")
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
  CMD.axisLimitDu=Z,1,-50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK
  CMD.simRpm=0
  CMD.axisClearLimit=Z,1
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
  CMD.axisLimitDu=Z,0,50000
  CMD.simRpm=600
  CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL
  CMD.simRpm=0
  CMD.axisClearLimit=Z,0
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

## 7. CLI Runner & Output Formats

The test harness is executed via `run_tests.py`:

```bash
# Run all tests against default hostname (nanoels.local)
python3 tests/hil/run_tests.py

# Run against a specific IP address
python3 tests/hil/run_tests.py --host 192.168.1.150

# Filter specific tests by keyword
python3 tests/hil/run_tests.py --filter gearing
python3 tests/hil/run_tests.py --filter soft_limit

# Output machine-readable JSON summary for AI agent analysis or CI
python3 tests/hil/run_tests.py --json > test_report.json
```

### 7.1 Human-Readable ANSI Terminal Output

```
================================================================================
                    NANOELS HARDWARE-IN-THE-LOOP TEST SUITE                    
Target: ws://192.168.1.150:81/canonical | Firmware: v5.38 (ESP32-S3)
================================================================================
TEST NAME                     STATUS   DURATION   DETAILS
--------------------------------------------------------------------------------
gearing_sync_1_5mm_pitch      [PASS]     1.24s    zDu: -30000 du (err: 0 du)
gearing_sync_reverse_pitch    [PASS]     1.18s    zDu: +15000 du (err: 0 du)
soft_limit_shoulder_halt      [PASS]     3.42s    Halted at -50000 du cleanly
soft_limit_no_overshoot       [PASS]     2.85s    Max excursion: -50000 du
stop_command_latency          [PASS]     0.08s    Halt latency: 34.2 ms
threading_multistart_index    [PASS]     4.10s    3 starts matched angle offsets
settings_hydration_check      [PASS]     0.45s    All 18 parameters verified
--------------------------------------------------------------------------------
SUMMARY: 7 Passed, 0 Failed, 0 Skipped | Total Time: 13.32s
RESULT: ALL TESTS PASSED (Exit code: 0)
================================================================================
```

### 7.2 Machine-Readable JSON Schema (`--json`)

```json
{
  "timestamp": "2026-10-16T16:35:00Z",
  "target": "ws://192.168.1.150:81/canonical",
  "firmware_version": "5.38",
  "total": 7,
  "passed": 7,
  "failed": 0,
  "duration_s": 13.32,
  "results": [
    {
      "name": "gearing_sync_1_5mm_pitch",
      "status": "PASS",
      "duration_s": 1.24,
      "details": {
        "expected_z_du": -30000,
        "actual_z_du": -30000,
        "error_du": 0
      }
    },
    {
      "name": "soft_limit_shoulder_halt",
      "status": "PASS",
      "duration_s": 3.42,
      "details": {
        "limit_du": -50000,
        "halted_z_du": -50000,
        "halt_reason": "REASON_SOFT_LIMIT"
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

    # Sort deterministically by name
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

    # Exit code: 0 if all tests passed, 1 if any failure
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
│ Simulation Bridge   │ • Implement virtual counts in Esp32SpindleEncoder│
│                     │ • Wire CMD.sim* in CommandDispatcher.cpp         │
│                     │ • Add 1.5s safety watchdog failsafe in firmware   │
│                     │ • Verify build via `pio run -e esp32s3`          │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 2: Python     │ • Implement `tests/hil/core/client.py`           │
│ Core Framework      │ • Implement `tests/hil/core/base_test.py`        │
│                     │ • Implement `tests/hil/core/assertions.py`       │
│                     │ • Implement `tests/hil/run_tests.py` CLI runner  │
│                     │ • Verify connection against live ESP32-S3        │
├─────────────────────┼──────────────────────────────────────────────────┤
│ Slice 3: Test Suite │ • Implement `test_gearing.py`                    │
│ Catalog & Run       │ • Implement `test_soft_limits.py`                │
│                     │ • Implement `test_emergency.py`                  │
│                     │ • Execute full test suite against hardware        │
│                     │ • Record baseline test report in docs/testing/   │
└─────────────────────┴──────────────────────────────────────────────────┘
```

---

## 9. Acceptance Criteria (Definition of Done)

The HIL test harness is considered complete and operational when:
1. `pio run -e esp32s3` compiles cleanly with zero warnings or errors with `CMD.sim*` hooks enabled.
2. The firmware automatically times out and halts if the test runner abruptly terminates (failsafe watchdog verified).
3. The Python harness connects via WebSocket, executes all test suites, and outputs both ANSI formatted console logs and `--json` reports.
4. Gearing pitch accuracy assertion confirms **0 step drift** on synchronized feeds over physical hardware.
5. Soft limit assertion confirms carriage halts cleanly at the shoulder without step overshoot.
