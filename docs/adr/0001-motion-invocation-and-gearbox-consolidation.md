# ADR 0001: Consolidated Motion Invocation, Cartesian Polarity, and Kinematic Kernel Refactoring

* **Status:** Accepted
* **Date:** October 15, 2026
* **Authors:** Emil & Antigravity
* **Deciders:** Emil & Antigravity
* **Context:** Motion engine refactoring, Async/Gearbox feed stall resolution, and multi-client command interface design.
* **Supercedes:** Initial "Primitive Modes" wrapper proposal in `docs/ELS_MODE_ARCHITECTURE.md`.

---

## 1. Context & Problem Statement

During testing of the Electronic Gearbox in asynchronous mode ($Z$-axis feeding toward the chuck at $150\text{ mm/min}$), the carriage failed to move, and no status telemetry was emitted to clients. Investigation revealed four structural architectural issues in the codebase:

1. **Overloaded Pitch Variable (`dupr`) & Sign-Loss**:  
   The legacy firmware inherited from `h5_monolithic.ino` overloaded `dupr` (pitch in deci-microns/spindle revolution) to store both spindle pitch and velocity direction. When in asynchronous (velocity) mode, `dupr` was $0$. Because direction was derived from $\text{sign}(\text{dupr})$, negating $0$ produced $0$, destroying directional intent and deadlocking the timer interrupt (`onAsyncTimer()`).

2. **Pseudo-Mode Over-Abstraction**:  
   An earlier iteration attempted to wrap low-level stepper timing into separate `IELSMode` classes (`GearZPrimitive`, `GearXPrimitive`, `AsyncPrimitive`). This blurred the boundary between **Kinematic Functions** (how motors step) and **Operational Modes** (what task the operator is performing). It forced `UnifiedGearboxMode` to act as an awkward proxy container, resulting in missing lifecycle hooks, dropped timer alarms, and orphaned mode registrations.

3. **Ambiguity of "Forward" and "Reverse"**:  
   Using "forward" and "reverse" in the core state model created cognitive and physical ambiguity across operations:
   * Longitudinal turning cuts toward the chuck ($-Z$), whereas internal boring cuts toward the tailstock ($+Z$).
   * Cross-slide infeeding ($-X$) moves in the opposite physical direction from a rear toolpost.
   * Spindle reverse (M4) inverts physical feed relative to rotation.

4. **Temporal Inconsistency from Drip-Fed Commands**:  
   Clients drip-fed individual parameters over WebSockets as form inputs were touched (`CMD.gearAxis=Z`, `CMD.gearSync=0`, `CMD.gearDir=...`), followed by an unparameterized `CMD.on=1`. This introduced network race conditions, multi-client state collisions, and the hazard of starting motion with mismatched parameters.

5. **Abrupt Step Cutoff at Limits**:  
   Motion lacked an explicit target distance. Axes stepped until `pos == limit`, instantly halting step generation at full velocity without planned deceleration, risking motor stall and mechanical shock.

---

## 2. Decision Drivers

* **Radical Simplicity**: Eliminate unnecessary wrappers, ghost modes, and polymorphic string-sniffing.
* **Machine Tool Standards**: Align with time-tested CNC standards (ISO 841 / ASME coordinates, G94/G95 feed regimes).
* **Deterministic Concurrency**: Core 1 motion execution must be atomic, validated, and decoupled from client form-editing state.
* **Safety First**: Universal soft limits enforced at the lowest kinematic layer; smooth planned deceleration to stopping boundaries.

---

## 3. Considered Options

* **Option 1 (Rejected - Point Fix):** Patch `main.cpp` with `currentMode != LatheMode::ASYNC`, force a dummy non-zero `dupr`, and continue using `AsyncPrimitive` as an `IELSMode`.  
  * *Reason for rejection:* Leaves the root architectural confusion intact; sign-loss and unit ambiguity remain ticking time bombs.
* **Option 2 (Rejected - Signed Velocity Polyglot):** Unify all feed commands into a single signed velocity command (`CMD.feedRate=±val`).  
  * *Reason for rejection:* Conflates spindle gearing ($\text{mm/rev}$) with time-based velocity ($\text{mm/min}$); a $150$ in the wrong mode would destroy the machine tool.
* **Option 3 (Accepted):** Clean separation of concerns:
  * Demote primitives to pure kinematic functions in Subsystem 2.
  * Consolidate Gearbox into a single operational mode in Subsystem 3.
  * Standardize direction on Cartesian coordinate polarity ($-1$ / $+1$).
  * Adopt G94/G95 "Feed is Feed" rate context.
  * Implement atomic motion invocation (`CMD.startFeed`) for rich clients, isolating legacy modal setters in an ingress staging adapter.

---

## 4. Decision Details

### 4.1 Kinematic Functions vs. Operational Modes
* **Subsystem 2 (Kinematic Kernel & Motion)** provides pure, stateless kinematic functions:
  * `stepSynchronized(axis, spindleDelta, pitchDupr)`: Slaved to spindle encoder ticks.
  * `stepVelocity(axis, speedDuPerMin, directionSign, targetDistDu)`: Driven by hardware clock timer interrupts.
* **Subsystem 3 (Operational Modes)** contains only user-facing lathe modes (`GearboxMode`, `TurnCycle`, `FaceCycle`, `ThreadCycle`).
* `AsyncPrimitive`, `GearZPrimitive`, and `GearXPrimitive` are retired as `IELSMode` classes.

### 4.2 "Feed is Feed" Principle (G94 / G95 Standard)
* Feed rate magnitude is strictly positive ($F > 0$).
* The physical unit is naturally defined by the synchronization context:
  * When `sync == true`: Feed per Revolution ($\text{mm/rev}$ or $\text{in/rev}$, canonical $\text{dupr}$).
  * When `sync == false`: Feed per Minute ($\text{mm/min}$ or $\text{in/min}$, canonical $\text{du/min}$).
* Display labels reflect the context (`mm/rev` vs. `mm/min`).

### 4.3 Cartesian Coordinate Polarity (-1 / +1)
* "Forward" and "Reverse" are removed from the Core Model.
* Direction is strictly a Cartesian coordinate sign:
  * **Negative (`-1`)**: Decreasing coordinate ($\Delta \text{pos} < 0$, towards $-Z$ chuck or $-X$ centerline).
  * **Positive (`+1`)**: Increasing coordinate ($\Delta \text{pos} > 0$, towards $+Z$ tailstock or $+X$ retract).
* The Ingress layer (`MotionConversions`) normalizes human semantic terms (`CHUCK`/`TAIL`/`IN`/`OUT`) to `-1` or `+1`.

### 4.4 Motion Intent (Target Distance) & Universal Safety Envelope
* Soft limits (`leftStop`, `rightStop`) are universal machine bounds enforced in the Kinematic Kernel for all motion regimes.
* Motion commands accept an optional **Target Distance** (with `-1` for continuous feed):
  * **Finite move**: Targets $P_{\text{target}} = P_0 + (\text{dir} \times \text{distance})$, clamped by the soft limit.
  * **Continuous move**: Targets the active soft limit itself.
* Because the target boundary is known, the kinematic engine calculates deceleration ramps ($d = v^2 / 2a$), smoothly gliding to a stop without missed steps.

### 4.5 Dual Command Pathways & Ingress Staging Adapter
* **Atomic Motion Invocation (Canonical Rich Client API)**:
  `CMD.startFeed=<axis>,<sync>,<feedRate>,<dir>,[distance]`
  Transmits the entire motion specification in a single atomic frame upon feed engagement.
* **Ingress Staging Adapter (Transitional / Dumb Client Support)**:
  Modal setters (`CMD.gearAxis=`, `CMD.gearSync=`, `CMD.gearDir=`, `CMD.on=1`) are isolated in a clearly delineated compatibility section in `CommandDispatcher.cpp` for legacy clients, keeping the core path pristine.

---

## 5. Consequences & Impact

### Positive
* **Zero Race Conditions:** Rich clients dispatch complete motion intent atomically.
* **Elimination of Sign-Loss:** Direction is independent of pitch magnitude; zero pitch no longer erases direction.
* **Smooth Deceleration:** Knowing target distances and soft limits allows planned deceleration ramps.
* **Reduced Code Surface:** Retires 6 redundant primitive files, removes proxying in `UnifiedGearboxMode`, and simplifies `main.cpp`.
* **Zero Multi-Client Clashes when Idle:** When idle, "selected mode" is client-local view state; the physical machine is simply `IDLE`.

### Trade-Offs & Mitigations
* *Client Migration:* The Web UI must be updated to use `CMD.startFeed` when starting gearbox feeds.  
  *Mitigation:* The Ingress Staging Adapter temporarily maintains backward compatibility for legacy commands.
* *Kinematic Timer Updating:* `onAsyncTimer()` must be adapted to read from the active kinematic velocity state instead of legacy globals.

---

## 6. References
* [`docs/NANOELS_MASTER_ARCHITECTURE.md`](../NANOELS_MASTER_ARCHITECTURE.md)
* [`docs/ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md)
* [`docs/ELS_CORE_STATE_MODEL.md`](../ELS_CORE_STATE_MODEL.md)
* [`docs/ELS_CLIENT_ARCHITECTURE.md`](../ELS_CLIENT_ARCHITECTURE.md)
