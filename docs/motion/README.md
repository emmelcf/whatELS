# NanoELS Motion Subsystem Architecture & Specification Index

> **Status:** Authoritative Motion Subsystem Documentation  
> **Target Platform:** NanoELS H5 Modular Firmware (ESP32-S3)  
> **Location:** `docs/motion/README.md`

---

## 1. Overview & Architectural Hierarchy

The NanoELS motion subsystem provides deterministic, CNC-grade motion control for lathe leadscrew and cross-slide axes, supporting continuous power feeds, spindle-synchronized electronic gearing, multi-pass threading, and multi-stop electronic bed dogs.

The subsystem is architected into three strictly decoupled layers:

```
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 3: MODE ORCHESTRATION LAYER                        │
│          UnifiedGearboxMode, TurnCycle, ThreadCycle, FaceCycle         │
│          (High-level machining state machines and operator intent)     │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    [SPEC 3] MODE_INTEGRATION_CONTRACT.md
    • High-level command primitives (Sync, Feed, Stroke, Jog)
    • Event listener interface (IMotionEventListener)
    • In-motion live trim & safety permissions
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 2: KINEMATIC MOTION ENGINE                         │
│          MotionEngine, SoftLimitTable, TrajectoryPlanner               │
│          (Coordinates in deci-microns, accel/decel, stopping windows)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    [SPEC 2] MOTION_ENGINE_SPECIFICATION.md
    • AxisState & AxisGeometry model (steps <-> du conversions, backlash)
    • SoftLimitTable (multi-stop drum, zero magic sentinels)
    • Trajectory acceleration/deceleration stopping window: d_stop = v^2 / 2a
    • SpindleSynchronizer (64-bit Bresenham accumulator, thread phase sync)
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│               LAYER 1: HARDWARE ABSTRACTION LAYER (HAL)                │
│          IStepperHAL, ISpindleEncoderHAL, IDROScaleHAL, IEndstopHAL    │
│          (Direct silicon interface to timers, GPIOs, PCNT, and Optos)  │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │
    [SPEC 1] HAL_SPECIFICATION.md
    • StepTimingConfig (setup/hold/pulse widths, microsecond safety)
    • IStepperHAL (timed pulse trains, single steps, dynamic frequency)
    • ISpindleEncoderHAL (PCNT hardware counting, filtered RPM, index pulse)
    • IDROScaleHAL & IEndstopHAL (scales, crash stops, E-Stop circuit)
                                    │
                                    ▼
┌────────────────────────────────────────────────────────────────────────┐
│                     ESP32-S3 PHYSICAL HARDWARE                         │
│  Dual-Core Xtensa LX7 @ 240MHz, PCNT, GPTimer, RMT, Dedicated GPIOs   │
└────────────────────────────────────────────────────────────────────────┘
```

---

## 2. Authoritative Specification Documents

| Document | Purpose & Key Topics |
|---|---|
| [`HAL_SPECIFICATION.md`](./HAL_SPECIFICATION.md) | **Layer 1:** Electrical timing constraints (`StepTimingConfig`), abstract interfaces for steppers (`IStepperHAL`), spindle encoder (`ISpindleEncoderHAL`), optical DRO scales (`IDROScaleHAL`), and physical endstops (`IEndstopHAL`). |
| [`MOTION_ENGINE_SPECIFICATION.md`](./MOTION_ENGINE_SPECIFICATION.md) | **Layer 2:** Coordinate geometry in deci-microns (`du`), backlash compensation, multi-stop soft limit tables (sequential stepped shaft shoulders), continuous mechanical feed parity, CNC acceleration/deceleration profiling, and stopping distance window math ($d_{\text{stop}} = v^2 / 2a$). |
| [`MODE_INTEGRATION_CONTRACT.md`](./MODE_INTEGRATION_CONTRACT.md) | **Layer 2 $\to$ 3 Interface:** Contract between `MotionEngine` and `IELSMode`. Eliminates low-level step math and ISRs from modes. Establishes command primitives (`engageSpindleSync`, `engageVelocityFeed`, `executeStroke`), event callbacks (`onMotionHalted`, `onSoftLimitReached`), and live in-motion feed trimming. |
| [`CONCURRENCY_AND_TIMING.md`](./CONCURRENCY_AND_TIMING.md) | **Systems Architecture:** Dual-core task partitioning (Core 0 for UI/Network/Modes vs. Core 1 for deterministic motion execution). Lock-free atomic snapshot mailbox (`TelemetryMailbox`) and command queues. Real-time latency budgets ($\le 35\,\mu\text{s}$ per loop) and watchdog failsafe architecture. |

> [!NOTE]
> Completed historical implementation runbooks (`PHASE1_EXECUTION_PLAN.md`, `PHASE2_EXECUTION_PLAN.md`) have been archived under [`docs/archive/`](../archive/). Active implementation follows living specifications in `docs/` and pair-programming protocol milestones.

---

## 3. Structural File Organization (Planned Implementation)

When implementation begins, source code will reside in modular directory trees strictly mirroring these layer boundaries:

```
src/motion/
├── hal/                               # Layer 1: Hardware Abstraction Layer
│   ├── IStepperHAL.h
│   ├── ISpindleEncoderHAL.h
│   ├── IDROScaleHAL.h
│   ├── IEndstopHAL.h
│   ├── Esp32StepperHAL.h/.cpp        # Concrete GPTimer/RMT driver
│   ├── Esp32SpindleEncoderHAL.h/.cpp # Concrete PCNT hardware driver
│   └── Esp32EndstopHAL.h/.cpp        # Direct GPIO debounced reader
│
├── kernel/                            # Layer 2: Kinematic Motion Engine
│   ├── AxisGeometry.h
│   ├── AxisState.h
│   ├── SoftLimit.h
│   ├── SoftLimitTable.h/.cpp         # Multi-stop bed dog manager
│   ├── TrajectoryPlanner.h/.cpp      # Accel/decel & stopping window math
│   ├── SpindleSynchronizer.h/.cpp    # 64-bit Bresenham gearing accumulator
│   └── MotionEngine.h/.cpp           # Master orchestrator for Layer 2
│
├── contract/                          # Layer 2 <-> Layer 3 Boundary
│   ├── IMotionEventListener.h        # Asynchronous event callbacks
│   ├── MotionCommands.h              # Typed command parameter structs
│   └── TelemetryMailbox.h            # Lock-free seqlock snapshot transfer
│
└── modes/                             # Layer 3: Mode Orchestration Layer
    ├── IELSMode.h                    # Cleaned mode interface
    ├── ModeRegistry.h/.cpp           # Dynamic mode discovery & factory
    ├── UnifiedGearboxMode.h/.cpp     # Electronic leadscrew & power feed
    └── cycles/                       # Canned multi-pass machining cycles
        ├── TurnCycle.h/.cpp
        ├── FaceCycle.h/.cpp
        └── ThreadCycle.h/.cpp
```
