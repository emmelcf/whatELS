# NanoELS Documentation Index & Architecture Map

> **Target Platform:** NanoELS H5 Electronic Leadscrew & Lathe Controller (ESP32-S3)  
> **Status:** Living Master Specification Index  
> **Audience:** Firmware Developers, Motion Specialists, HMI/Client Engineers

---

## 1. System Master Blueprint
- **[`NANOELS_MASTER_ARCHITECTURE.md`](./NANOELS_MASTER_ARCHITECTURE.md):** The overarching system design, 6-subsystem topology, core partitioning invariants (Core 0 vs. Core 1), and inter-subsystem data flow.

---

## 2. Subsystem Documentation Map

The active documentation is organized strictly by subsystem to match our C++ firmware architecture:

```
docs/
├── motion/     --> Real-Time Motion Core & HAL (Layers 1 & 2, Core 1)
├── modes/      --> Machining Canned Cycles & Mode Orchestration (Layer 3, Core 1)
├── state/      --> Canonical State Model & Settings Engine (Core 0 / Shared)
├── machine/    --> Lathe Iron Profiles & Leadscrew Mechanics
├── clients/    --> Heterogeneous Client Suite & Wire Protocols (Web DRO, Nextion)
├── adr/        --> Formal Architecture Decision Records
└── archive/    --> Frozen Peer Reviews & Historical Working Notes
```

### [Motion Core (`docs/motion/`)](./motion/README.md)
*Core 1 Hard Real-Time Domain ($250\,\mu\text{s}$ tick, $\le 35\,\mu\text{s}$ execution budget).*
- **[`HAL_SPECIFICATION.md`](./motion/HAL_SPECIFICATION.md):** Layer 1 Hardware Abstraction (`IStepperHAL`, `ISpindleEncoderHAL`, `IDROScaleHAL`, electrical pulse timing, PCNT zero-loss counter).
- **[`MOTION_ENGINE_SPECIFICATION.md`](./motion/MOTION_ENGINE_SPECIFICATION.md):** Layer 2 Kinematic Engine (`MotionEngine`, `SpindleSynchronizer`, `SoftLimitTable`, stopping distance window $d_{\text{stop}} = v^2/2a$, deci-micron units).
- **[`CONCURRENCY_AND_TIMING.md`](./motion/CONCURRENCY_AND_TIMING.md):** Core 0/1 isolation, provably race-free `TelemetryMailbox` seqlock, flash cache freeze lockout, jitter budgets.
- **[`MODE_INTEGRATION_CONTRACT.md`](./motion/MODE_INTEGRATION_CONTRACT.md):** Layer 2 $\to$ Layer 3 boundary contract, event listeners (`onMotionHalted`), dual-axis compound taper parameters.
- **[`PHASE1_EXECUTION_PLAN.md`](./motion/PHASE1_EXECUTION_PLAN.md):** Phase 1 execution runbook dividing implementation into 4 slices, with starter kickoff prompts and living handover ledger.

### [Mode Orchestration (`docs/modes/`)](./modes/README.md)
*Layer 3 Machining Cycles (Orchestrators running on Core 1).*
- **[`ELS_MODE_ARCHITECTURE.md`](./modes/ELS_MODE_ARCHITECTURE.md):** `IELSMode` interface, lifecycle FSM, default-deny safety gatekeeper (`canExecuteCommand`), in-motion permissions matrix.
- **Machining Cycles:** Subdirectories for `gearbox/`, `turning/`, `threading/`, `facing/`, `cutoff/`, `taper/`, `slotting/`, `spheres/`, and `joystick/`.

### [State & Settings Subsystem (`docs/state/`)](./state/README.md)
*Canonical Data Models & Persistence.*
- **[`ELS_CORE_STATE_MODEL.md`](./state/ELS_CORE_STATE_MODEL.md):** The 4 canonical domains: Kinematics (`K`), Intent (`I`), Cycle (`C`), Properties (`P`). Change bitmasks and telemetry serialization.
- **[`ELS_PROPERTIES_AND_SETTINGS.md`](./state/ELS_PROPERTIES_AND_SETTINGS.md):** 4-tier property taxonomy (`machine.*`, `sys.*`, `prefs.*`, `client.*`), sparse NVS overrides, flash lockout safety.
- **[`ELS_PRESET_CATALOG.md`](./state/ELS_PRESET_CATALOG.md):** ROM factory defaults and machine presets.
- **[`TIER2_STATE_MAPPING.md`](./archive/TIER2_STATE_MAPPING.md) (Archived):** Historical audit and variable mapping ledger between legacy globals and canonical domains.

### [Machine Profile (`docs/machine/`)](./machine/LATHE_MACHINE_PROFILE.md)
- **[`LATHE_MACHINE_PROFILE.md`](./machine/LATHE_MACHINE_PROFILE.md):** Physical machine iron parameters, leadscrew leads, motor microsteps, encoder PPR, travel boundaries.

### [Clients & HMI (`docs/clients/`)](./clients/ELS_CLIENT_ARCHITECTURE.md)
*Supervisory Domain (Core 0).*
- **[`ELS_CLIENT_ARCHITECTURE.md`](./clients/ELS_CLIENT_ARCHITECTURE.md):** Multi-client architecture, WebSocket & Serial wire protocols, Tier 3 capability handshake, Safe Monitor Lockout.
- **[`hmi/`](./clients/hmi/HMI_DESIGN_GUIDELINES.md):** 800×480 rich touch design principles, physical lathe ergonomics, high-contrast dark mode.
- **[`web_ui/`](./clients/web_ui/OVERVIEW.md):** Touch DRO Web Client overview and interaction ledger.

### [Architecture Decision Records (`docs/adr/`)](./adr/0001-motion-invocation-and-gearbox-consolidation.md)
- **[`ADR-0001`](./adr/0001-motion-invocation-and-gearbox-consolidation.md):** Motion Invocation & Gearbox Consolidation.
- **[`ADR-0002`](./adr/0002-command-surface-rationalization-and-dumb-client-decoupling.md):** Command Surface Rationalization & Decoupling.

### [Historical & Review Archive (`docs/archive/`)](./archive/README.md)
- **[`reviews/argon/`](./archive/reviews/argon/argon_response.md):** Google Argon Principal Peer Review report (`argon_response.md`), review dossier (`CONSOLIDATED_REVIEW_DOSSIER.md`), and unabridged snapshot (`UNABRIDGED_ARCHITECTURE_COMPENDIUM.md`).
- **[`ELS_DESIGN_NOTEBOOK.md`](./archive/ELS_DESIGN_NOTEBOOK.md):** Initial architectural design notes and brainstorms.
- **[`IMPLEMENTATION_PLAN_V1.md`](./archive/IMPLEMENTATION_PLAN_V1.md):** Early procedural implementation outline.
