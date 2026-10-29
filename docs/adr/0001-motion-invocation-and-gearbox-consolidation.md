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

### 4.3 Standard DRO Cartesian Coordinate Polarity (-1 / +1)
* "Forward" and "Reverse" are removed from axis carriage feed motion entirely:
  * On a lathe, `FWD` and `REV` designate **Spindle Rotation** (Forward/CCW vs. Reverse/CW) and electrical VFD relays. Using them for axis carriage feed creates dangerous operator confusion and tool crash hazards. They are strictly banned from axis motion commands, kinematics, and telemetry.
* Motion direction is formalized into standard machine tool / DRO Cartesian coordinate signs:
  * **Negative Coordinate Feed (`-1`)**: Feed toward decreasing coordinates ($\Delta \text{pos} < 0$).
    * On $Z$ (longitudinal carriage): Feeds **toward the Chuck** ($-Z$).
    * On $X$ (transverse cross-slide): Feeds **toward the Spindle Centerline** ($-X$, infeed/plunge).
    * Kinematic bounding: Bounded by `leftStop` (Chuck limit / Infeed limit).
    * Hardware DIR: Pin logic driven to advance carriage toward the chuck or cross-slide toward centerline.
    * Synchronous gearing: Positive pitch calculation (`+feedRateDu`) driving carriage toward chuck.
  * **Positive Coordinate Feed (`+1`)**: Feed toward increasing coordinates ($\Delta \text{pos} > 0$).
    * On $Z$ (longitudinal carriage): Feeds **away toward Tailstock** ($+Z$).
    * On $X$ (transverse cross-slide): Feeds **outward away from Centerline** ($+X$, retract).
    * Kinematic bounding: Bounded by `rightStop` (Tailstock limit / Retract limit).
    * Hardware DIR: Pin logic driven to advance carriage toward tailstock or cross-slide toward operator retract.
    * Synchronous gearing: Negative pitch calculation (`-feedRateDu`) driving carriage toward tailstock.

### 4.4 Architectural Layer Separation of Concerns
* **Core Motion Engine (`Motion.cpp`):**
  * Stateless, generic, and coordinate-driven.
  * Knows **only** pure axes ($Z, X$), signed numeric coordinate direction (`-1` vs. `+1`), feed rate magnitude ($F > 0$), and machine coordinate limits (`leftStop`, `rightStop`).
  * Has **zero** knowledge or string references to lathe parts ("CHUCK", "TAIL", "CENTER", "RETRACT").
* **Client Translation Layer (`ICommandTranslator` / Ingress / `CommandDispatcher` / `MotionConversions`):**
  * Sits between external clients and the Core Engine.
  * Translates incoming client commands—accepting both pure Cartesian signs (`-1`, `+1`) and lathe operator semantics (`CHUCK`/`TAIL` for $Z$; `CENTER`/`RETRACT` or `IN`/`OUT` for $X$)—into the Core's pure numeric `-1` or `+1`.
* **Outbound Telemetry Serializer (`TelemetrySerializer.cpp` in `src/core/`):**
  * Projects core state to connected displays, serializing active direction into lathe semantics (`K.gearDir=CHUCK`, `K.gearDir=TAIL`) or numeric signs (`-1`/`+1`).
* **Presentation Layer / Touch UI (`lathe-controller.js`):**
  * Operator touch view controller.
  * Renders unambiguous lathe directional labels (`← CHUCK (Z−)` vs `→ TAIL (Z+)`, `↑ TO CENTER (X−)` vs `↓ RETRACT (X+)`).
  * Maintains client-local form state while idle and dispatches atomic motion intent upon `START`.

### 4.5 Motion Intent (Target Distance) & Universal Safety Envelope
* Soft limits (`leftStop`, `rightStop`) are universal machine bounds enforced in the Kinematic Kernel for all motion regimes.
* Motion commands accept an optional **Target Distance** (with `-1` for continuous feed):
  * **Finite move**: Targets $P_{\text{target}} = P_0 + (\text{dir} \times \text{distance})$, clamped by the soft limit.
  * **Continuous move**: Targets the active soft limit itself.
* Because the target boundary is known, the kinematic engine calculates deceleration ramps ($d = v^2 / 2a$), smoothly gliding to a stop without missed steps.

### 4.6 Dual Command Pathways & Ingress Staging Adapter
* **Atomic Motion Invocation (Canonical Rich Client Key-Value API)**:
  `CMD.startFeed=axis:<axis>,sync:<0|1>,feed:<feedRateDu>,dir:<dir>,[dist:<distanceDu>]`
  * **Order-Independent & Extensible:** Parameters are parsed strictly as Key-Value pairs (`axis`, `feed`, `dir` mandatory; `sync`, `dist` optional), allowing any order and clean optional arguments without comma placeholder tricks.
  * **Quoting & Delimiter Safety:** Values containing special characters (`:`, `,`) can be enclosed in quotes (e.g. `key:"value"`), guaranteeing tokenizer safety.
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
* [`docs/WHATELS_MASTER_ARCHITECTURE.md`](../WHATELS_MASTER_ARCHITECTURE.md)
* [`docs/ELS_MODE_ARCHITECTURE.md`](../ELS_MODE_ARCHITECTURE.md)
* [`docs/ELS_CORE_STATE_MODEL.md`](../ELS_CORE_STATE_MODEL.md)
* [`docs/ELS_CLIENT_ARCHITECTURE.md`](../ELS_CLIENT_ARCHITECTURE.md)
