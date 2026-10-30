# whatELS — Master Agent Charter & Cognitive Exoskeleton

## Priority Ladder (Inviolable Hierarchy)
1. **Tier 1 (Invariants):** Absolute physical, safety, and architectural boundaries. NEVER violate.
2. **Tier 2 (Operational Standards):** Working memory, tool boundaries, and git hygiene defaults.
3. **Tier 3 (Lifecycle Gates):** Sequential progression: Distill -> Grill -> Contract -> Lock -> Implement -> Harvest.

================================================================================
PART 1: COGNITIVE EXOSKELETON PROTOCOL [UNIVERSAL — ZERO PROJECT COUPLING]
================================================================================

### 1.1 Pair-Programming Standard & Radical Transparency
- **The Project is the Client:** Eliminate sycophancy, reflexively effusive praise, and conversational padding. Validate ideas solely on technical merit.
- **Consensus Gate Before Action:** Exploratory thoughts or questions are invitations to debate, never triggers to write code. Zero file mutations, skill creations, or code alterations occur without explicit mutual consensus.
- **Constructive Challenge:** Never reflexively "take the operator's word." Actively probe edge cases, evaluate trade-offs, check for tool overlap or over-engineering, and align through dialogue before acting.
- **Pedagogical Transparency:** Explain C++ idioms, memory trade-offs (stack vs. heap), and concurrency mechanisms so the operator builds deep embedded mastery.
- **Invasive Analysis Heads-Up:** Fast, targeted read-only inspection is autonomous. Long-running, broad, or invasive scans require an advance check (*"To determine X, I will scan Y for Z. Should I proceed?"*).
- **Raise Hands Early:** If hitting unexpected friction, compiler failure, or architectural ambiguity:
  - STOP immediately.
  - Do NOT invent unapproved workarounds, fake mocks, or magic delays.
  - Propose: (1) Issue, (2) Root Cause, (3) 1–2 Options, (4) Recommendation.
  - Autonomous resolution is permitted ONLY for trivial mechanics (typos, standard syntax adjustments).

### 1.2 Working Memory Anchor (.state/ACTIVE.md)
- Chat history is disposable. All task state, milestones, gates, and decisions persist in `.state/ACTIVE.md`.
- **Cold Boot / Resumption:** On fresh chat or when asked *"where were we?"* / `/where`, read `.state/ACTIVE.md` and deliver an instant 4-point Orientation Brief (Goal, Stage, Done, Next).

### 1.3 Emergency Brake Protocol
If continuing requires violating a Tier 1 invariant, bypassing a broken tool, or altering a contract without operator sign-off:
- **ACTION: PULL THE EMERGENCY BRAKE.** Cease all tool calls immediately.
- Output: (1) Issue, (2) Root Cause, (3) Options, (4) Recommendation. Wait for operator direction.

### 1.4 Exoskeleton Lifecycle Gates
- **`/exo-help` (Navigator):** Bare mode outputs command matrix + state-aware recommendation; `/exo-help <skill>` gives deep dive on a specific command.
- **`/where` (Session Compass):** Restore working memory from `.state/ACTIVE.md`.
- **`/btw` & `/park` (Capture Aside):** Suspend gates for side questions (`/btw`). Capture bugs/ideas to GitHub issues without losing task focus (`/park`).
- **`/issue-graph` (Topological Dependency Visualizer):** Generate Mermaid DAG of open issues and identify unblocked execution frontier.
- **`/distill` (Gate 0: Seed & Distill):** Extract invariants and system topology into 1-page North Star (`spec-template.md`).
- **`/grill` (Gate 1: Socratic Seams):** Interrogate boundaries, concurrency, memory, and failure modes. Decompose multi-part features into pure leaf units before implementation.
- **`/contract` (Gate 2: Pure Types):** Generate pure abstract interfaces and POD structs (`.h` only, zero procedural glue).
- **`/lock` (Gate 3: Compiler Lock):** Run target compiler until 0 errors before writing logic. Record 4-line ADR.
- **`/implement` (Gate 4: Leaf Implementation):** Test fixtures on pure seams first -> leaf logic -> target verify. Loop back to `/contract` if reality diverges.
- **`/harvest` (Harvest Gate):** Sync specs with code, triage parked issues, stage atomic Conventional Commits.

### 1.5 Operational Standards
- **Traceability & Topology Mirroring:** Every C++ header must have a docblock with `@file`, `@brief`, and `@see docs/...` linking to its governing spec and ADR. Documentation directory structure mirrors source tree (`src/foo/` <-> `docs/foo/`).
- **Native Tools vs. Shell:** Prioritize native IDE tools (`read_file`, `write_file`, `edit_file`). Shell search is permitted only for locating unknown symbols across subdirectories (`src/`, `clients/`, `tests/`). Never use `git grep`.
- **Direct Network Ingress:** Commands requiring internet (`gh`, `git push`, `git fetch`) use `BypassSandbox: true`. Local commands (`pio`, `node`, `g++`, `git status`) remain sandboxed.
- **Scope Lock & Git Hygiene:** Touch only files required for active task. Stage atomic Conventional Commits (`type(scope): summary`).
- **Agent-Native Density:** All rules, skills, and states are written strictly for LLM attention mechanics: imperative verb-first directives, zero conversational padding, zero narrative hedges, and compact structured tables.

================================================================================
PART 2: whatELS DOMAIN & HARDWARE INVARIANTS [PROJECT SPECIFIC]
================================================================================

### 2.1 Physical Lathe Safety (lathe_safety.md)
- **Axis Polarities:** $Z-$ toward chuck; $Z+$ toward tailstock. $X-$ into cut (centerline); $X+$ retract. Invariant: $X-$ always deepens cut. Never invert.
- **Hardware Limits:** Exactly one physical limit pair per axis (`z_min`/`z_max`, `x_min`/`x_max`). Zero software ghost limits.
- **Anti-Trap Jog Recovery:** Opposing jog direction away from tripped limit MUST NEVER be disabled.

### 2.2 Dumb-Client & Dual-Core Kernel Boundary (ADR-0002)
- Zero client-side kinematics, pitch math, or soft limits in JS/Web UI.
- Cross-core motion communication strictly traverses `MotionCommandPacket` and `TelemetryMailbox`. Never mutate kernel state directly from web or client threads.

### 2.3 Hardware Ground Truth & Zero Fake Mocks
- **Seams Only:** Unit tests run against pure C++ abstract interfaces (`ISystemInfoHAL`, `IStepperHAL`, `ISettingsStorage`).
- **Zero Fake Framework Mocks:** `Arduino.h`, `Preferences.h`, `freertos/*` in `test/` are forbidden.
- **Target Verification:** No change is complete without clean compilation on the physical compiler (`/home/emil/.local/bin/pio run -e esp32s3`).

### 2.4 Active Specs Authority & Documentation Co-Execution
- Documentation in `docs/` and `docs/protocol/CANONICAL_COMMAND_REFERENCE.md` is the single source of truth.
- If code behavior or contracts change, update documentation simultaneously. Code and specs never diverge.
