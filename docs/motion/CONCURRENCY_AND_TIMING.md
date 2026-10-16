# NanoELS Concurrency, Real-Time Timing & Core Architecture

> **Status:** Authoritative Engineering Specification  
> **Target Version:** NanoELS H5 Modular Architecture (ESP32-S3)  
> **Audience:** Firmware Engineers, Systems Architects, Motion Core  
> **Location:** `docs/motion/CONCURRENCY_AND_TIMING.md`  
> **Related Specifications:** [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md), [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md), [`MODE_INTEGRATION_CONTRACT.md`](./MODE_INTEGRATION_CONTRACT.md)

---

## 1. Executive Summary & Problem Statement

The ESP32-S3 is a dual-core Xtensa LX7 microcontroller running at 240 MHz with FreeRTOS.
A lathe electronic leadscrew controller has two conflicting operational profiles:

1. **Deterministic Microsecond Real-Time Domain (Core 1):**  
   - Spindle encoder sampling, quadrature counter phase tracking.
   - Stepper step pulse dispatch, hardware timer management, acceleration profiling.
   - Real-time limit lookahead and stopping distance enforcement.
   - Emergency stop lines and hardware alarms.
   - *Failure Mode:* A jitter spike of even $20\,\mu\text{s}$ can cause stepper motor resonance, lost steps, or thread pitch lead error.

2. **Asynchronous Soft Real-Time Supervisory Domain (Core 0):**  
   - Web server (HTTP, WebSocket broadcast at 50 Hz - 100 Hz).
   - WiFi/Ethernet network stack (LwIP).
   - JSON serialization, parameter parsing, and HMI state updates.
   - Flash filesystem reads/writes (LittleFS settings storage).
   - Mode state machine transitions and canned cycle orchestration.
   - *Characteristic:* Network packet processing can block for hundreds of microseconds or milliseconds.

**Core Invariant:**  
Core 0 (Network & Supervisory) must **never, under any circumstance, block, stall, or delay Core 1 (Motion Execution)**. Motion execution must remain 100% deterministic even when the web browser reconnects, transfers large JSON payloads, or reloads assets.

---

## 2. Dual-Core Allocation & Task Architecture

To achieve zero-jitter shoulder retraction and avoid cross-core lockouts, **Option A** is enforced: the `IELSMode` state machine executes directly on **Core 1** inside the real-time motion loop, while Core 0 handles supervisory, Web UI, and communications.

```
┌────────────────────────────────────────────────────────────────────────┐
│                        CORE 0: SUPERVISORY & HMI                       │
│                                                                        │
│   ┌───────────────────────┐  ┌───────────────────────┐                 │
│   │   Web/WS Server Task  │  │   Command Router /    │                 │
│   │   (Priority 3, 8KB)   │  │   HMI Dispatcher      │                 │
│   │   • LwIP & WebSockets │  │   (Priority 5, 8KB)   │                 │
│   │   • LittleFS/NVS I/O  │  │   • Validates intents │                 │
│   └───────────┬───────────┘  └───────────┬───────────┘                 │
│               │                          │                             │
│               │ Read 50Hz Snapshot       │ Enqueue Operator Commands   │
│               ▲                          ▼ (Run, Stop, Limit, Nudge)   │
└───────────────┼──────────────────────────┼─────────────────────────────┘
                │                          │
        Lock-Free Snapshot          Lock-Free Command
        Mailbox (Core 1 -> Core 0)  Ringbuffer (Core 0 -> Core 1)
                │                          │
┌───────────────┼──────────────────────────┼─────────────────────────────┘
│               │                          │
│   ┌───────────┴──────────────────────────▼───────────┐                 │
│   │   Core 1 Real-Time Motion & Mode Task            │                 │
│   │   (Priority 22, 12KB Internal SRAM, IRAM_ATTR)   │                 │
│   │                                                  │                 │
│   │   ┌──────────────────────────────────────────┐   │                 │
│   │   │   Layer 3: IELSMode State Machine        │   │                 │
│   │   │   • Autonomous cycle orchestration       │   │                 │
│   │   │   • Zero-latency shoulder stops/retracts │   │                 │
│   │   └────────────────────┬─────────────────────┘   │                 │
│   │                        │ Synchronous in-core     │                 │
│   │                        ▼ method calls            │                 │
│   │   ┌──────────────────────────────────────────┐   │                 │
│   │   │   Layer 2: Kinematic Motion Engine       │   │                 │
│   │   │   • 64-bit Bresenham Spindle Sync        │   │                 │
│   │   │   • Trajectory Accel/Decel Lookahead     │   │                 │
│   │   │   • SoftLimitTable stopping enforcement  │   │                 │
│   │   └────────────────────┬─────────────────────┘   │                 │
│   └────────────────────────┼─────────────────────────┘                 │
│                            │                                           │
│                            ▼                                           │
│   ┌──────────────────────────────────────────────────┐                 │
│   │   Layer 1: ESP32-S3 Hardware Timer / PCNT ISRs   │                 │
│   │   (Priority Level 3 / Level 5 Hardware ISR)      │                 │
│   └──────────────────────────────────────────────────┘                 │
│                                                                        │
│                        CORE 1: DETERMINISTIC MOTION                    │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Inter-Core Communication & Synchronization Primitives

Traditional mutexes (`std::mutex`, FreeRTOS `SemaphoreHandle_t`) are strictly prohibited in the real-time motion path because they cause **priority inversion** and unpredictable task preemption.

Instead, inter-core data transfer uses two lock-free communication patterns:

### 3.1 Lock-Free State Mailbox (Core 1 $\to$ Core 0 Telemetry)
To pass real-time coordinates, RPM, and motor state from Core 1 to the Web UI and display tasks on Core 0 without locking, a provably race-free seqlock is used. Explicit memory fences (`std::atomic_thread_fence`) prevent compiler and CPU memory reordering on the Xtensa LX7 dual-core pipeline:

```cpp
#pragma once
#include <cstdint>
#include <cstring>
#include <atomic>

struct MotionTelemetrySnapshot {
  int32_t  zPosSteps;        // Current leadscrew step position
  int32_t  zCoordDu;         // Workpiece coordinate in deci-microns
  int32_t  xPosSteps;        // Current cross-slide step position
  int32_t  xCoordDu;         // Workpiece coordinate in deci-microns
  uint32_t spindleRpm;       // Filtered RPM
  int64_t  spindlePulses;    // Raw continuous spindle encoder pulses
  uint16_t motionMode;       // Active MotionMode enum
  bool     isMoving;         // Active stepping flag
  bool     isFeedHold;       // Pause flag
  uint8_t  activeLimitSlot;  // Limit currently being approached or rested on (0 if none)
  int32_t  distToLimitDu;    // Remaining distance to stop
  uint32_t sequenceNumber;   // Monotonic sequence tag for atomic consistency
};

class TelemetryMailbox {
public:
  void IRAM_ATTR publish(const MotionTelemetrySnapshot& snapshot) {
    uint32_t seq = m_seq.load(std::memory_order_relaxed);
    m_seq.store(seq + 1, std::memory_order_relaxed); // Mark odd (write in progress)

    // Prevent compiler or CPU from reordering payload writes before (seq + 1) store
    std::atomic_thread_fence(std::memory_order_release);

    std::memcpy(const_cast<MotionTelemetrySnapshot*>(&m_data), &snapshot, sizeof(MotionTelemetrySnapshot));

    // Ensure all payload bytes are globally visible before sequence counter is marked even
    m_seq.store(seq + 2, std::memory_order_release);
  }

  bool read(MotionTelemetrySnapshot& outSnapshot) const {
    for (int retries = 0; retries < 4; ++retries) {
      uint32_t seq1 = m_seq.load(std::memory_order_acquire);
      if (seq1 & 1U) {
        continue; // Writer active; spin retry
      }

      std::memcpy(&outSnapshot, const_cast<const MotionTelemetrySnapshot*>(&m_data), sizeof(MotionTelemetrySnapshot));

      // Prevent payload reads from sinking below seq2 load
      std::atomic_thread_fence(std::memory_order_acquire);

      uint32_t seq2 = m_seq.load(std::memory_order_relaxed);
      if (seq1 == seq2) {
        return true; // Guaranteed tear-free snapshot
      }
    }
    return false;
  }

private:
  std::atomic<uint32_t>            m_seq{0};
  volatile MotionTelemetrySnapshot m_data{};
};
```

### 3.2 Lock-Free Command Queue (Core 0 $\to$ Core 1 Commands)
When the operator presses a button on the Web UI or Nextion display, commands are validated and posted into a fixed-size FreeRTOS ringbuffer consumed by the Core 1 Mode/Motion task:

```cpp
struct MotionCommandPacket {
  enum class Type : uint8_t {
    START_SPINDLE_SYNC,
    START_VELOCITY_FEED,
    EXECUTE_STROKE,
    EXECUTE_JOG,
    STOP_NORMAL,
    STOP_EMERGENCY,
    PAUSE_FEED_HOLD,
    RESUME_FEED_HOLD,
    UPDATE_FEED_PITCH,
    UPDATE_VELOCITY,
    UPDATE_SOFT_LIMITS,
    ZERO_AXIS
  } type;

  AxisId  axis;
  int8_t  direction;
  int32_t pitchDupr;
  int32_t speedDuMin;
  int32_t targetCoordDu;
  bool    respectSoftLimits;

  // Secondary axis extension for compound tapers / cones
  bool    enableSecondaryAxis;
  AxisId  secondaryAxis;
  int32_t secondaryRatioNum;
  int32_t secondaryRatioDen;
};
```
- Core 1 consumes packets at the start of each motion service cycle.
- High-priority Emergency Stop uses direct-to-task notification or hardware atomic flags to trigger instantaneous abort without waiting for queue processing.

---

## 4. Hardware Peripherals & Real-Time Timing Budgets

### 4.1 Spindle Quadrature Tracking (PCNT Unit)
- **Hardware Module:** ESP32-S3 dedicated **Pulse Counter (PCNT)** peripheral.
- **Physical Rate:** At $3{,}000\text{ RPM}$ with a $1{,}024\text{ PPR}$ optical encoder ($4{,}096$ quadrature edges/rev):
  $$\text{Edge Rate} = \frac{3000}{60} \times 4096 = 204{,}800\text{ edges/second} \quad (4.88\,\mu\text{s per edge})$$
- **Zero-Loss Sampling (Native 16-Bit Modulo Wrap):**
  - **Never clear the hardware PCNT counter** (`pcnt_unit_clear_count()`), as clearing register causes an unrecoverable blind spot where $204.8\text{ kHz}$ pulses are dropped.
  - Let the 16-bit signed hardware counter free-run and wrap natively:
    ```cpp
    int16_t current16 = static_cast<int16_t>(rawCount);
    int16_t delta16   = static_cast<int16_t>(current16 - m_lastCount16);
    m_lastCount16     = current16;
    m_accumulatedPulses += delta16;
    ```
- **Glitch Filter:** Configure ESP32-S3 PCNT hardware glitch filter to $\approx 500\text{ ns}$ (40 APB clock cycles at 80 MHz) to reject industrial VFD switching noise without attenuating valid quadrature edges.

### 4.2 Stepper Pulse Generation Strategies

| Technique | Peripheral | Maximum Pulse Rate | Jitter | Use Case in NanoELS |
|---|---|---|---|---|
| **Hardware RMT** | ESP32-S3 RMT | Up to $1\text{ MHz}$ | $< 50\text{ ns}$ | Asynchronous velocity rapids & smooth continuous feed |
| **GPTimer ISR** | ESP32-S3 64-bit Timer | Up to $100\text{ kHz}$ | $< 0.5\,\mu\text{s}$ | Spindle-synchronized Bresenham gearing & multi-axis coordinated vectoring |
| **High-Precision Direct Toggle** | Fast GPIO (`gpio_set_level`) | Up to $20\text{ MHz}$ | Bounded by ISR latency | Fine single-step discrete jogs & index homing touch-off |

### 4.3 Worst-Case Latency Budgets (Core 1)

| Subsystem / Operation | Execution Budget | Maximum Permitted Jitter |
|---|---|---|
| Spindle Delta Calculation & RPM Filter | $\le 8\,\mu\text{s}$ | $\pm 2\,\mu\text{s}$ |
| Trajectory Deceleration Window Evaluation ($d_{\text{stop}}$) | $\le 12\,\mu\text{s}$ | $\pm 3\,\mu\text{s}$ |
| Bresenham 64-bit Fractional Gearing Step Dispatch | $\le 6\,\mu\text{s}$ | $\pm 1\,\mu\text{s}$ |
| Soft Limit Boundary Verification | $\le 5\,\mu\text{s}$ | $\pm 1\,\mu\text{s}$ |
| **Total Real-Time Service Loop Time** | **$\le 35\,\mu\text{s}$** | **Loop Period: $250\,\mu\text{s}$ ($14\%$ CPU load on Core 1)** |

### 4.4 ESP32-S3 SPI Flash Cache Freeze Mitigation (`[CONC-02]`)
- **Shared Flash Bus Hazard:** When Core 0 writes to NVS (`Preferences`) or LittleFS, the ESP32-S3 hardware automatically disables the shared SPI flash instruction/data cache on **both cores** for 1 to 40 ms.
- **Mandatory Protections:**
  1. **Strict IRAM/DRAM Allocation:** All Core 1 ISR handlers, `MotionEngine` stepping methods, `SpindleSynchronizer`, and `SoftLimitTable` routines must be marked `IRAM_ATTR`. All kinematic state structs and lookup tables must reside in internal SRAM (`DRAM_ATTR`).
  2. **In-Motion Flash Write Lockout:** `SettingsService::set()`, `reset()`, and LittleFS profile writes on Core 0 must immediately reject execution with `ERR_MOTION_ACTIVE` whenever `MotionEngine::isAnyAxisMoving()` is true:
     ```cpp
     if (MotionEngine::instance().isAnyAxisMoving()) {
       return "ERR:SETTINGS=cannot write to NVS flash while motion is active\n";
     }
     ```

---

## 5. Failsafe Mechanisms & Watchdog Architecture

To protect machine and operator in the event of hardware or software hangs:

1. **Hardware Task Watchdog (Core 1):**  
   A dedicated hardware watchdog timer is subscribed to the Core 1 Motion Engine loop. If Core 1 fails to service the watchdog within $50\text{ ms}$, the watchdog trips:
   - Stepper driver `ENABLE` lines are de-asserted instantly via GPIO register write.
   - VFD spindle run relay is opened.
   - The microcontroller performs a safe reboot and writes a diagnostic panic log to RTC memory.

2. **Decoupled Network Fault Isolation & Wi-Fi Jog Lease Watchdog:**  
   - If WiFi drops, WebSocket disconnects, or Core 0 experiences an out-of-memory condition during browser activity:
     - Core 1 continues executing the active autonomous cycle or synchronized thread without interruption to its programmed stop.
     - Limit stops remain 100% active and enforced on Core 1.
     - The physical Emergency Stop button and limit switches are wired directly to Core 1 hardware interrupts.
   - **Continuous Manual Jogging (`CMD.jogHold`):** Requires a 200 ms hardware lease watchdog on Core 1. If subsequent lease packets or `CMD.jogRelease` drop over Wi-Fi, Core 1 automatically halts jogging within $200\text{ ms}$ to prevent runaway carriage motion.
