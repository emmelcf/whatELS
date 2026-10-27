# whatELS — Master Agent Charter & Pair Protocols

## 1. Governance: Three-Tier Rule Hierarchy
All instructions follow this strict priority ladder. Never break a higher tier to satisfy a lower tier.

### Tier 1: Non-Negotiable Invariants (NEVER violate)
- **Lathe Safety ([`lathe_safety.md`](.agents/rules/lathe_safety.md))**: Axis polarities are physical laws ($Z-$ toward chuck, $X-$ into cut). Exactly one hardware limit pair per axis; zero ghost limits.
- **Dumb Client & Kernel Boundary (ADR-0002, [`zero_hack_architecture.md`](.agents/rules/zero_hack_architecture.md))**: Zero client-side kinematics or soft limits in JS. Cross-core motion communication strictly traverses `MotionCommandPacket` and `TelemetryMailbox`.
- **Engineering Integrity & Communication**: Zero silent error swallowing (no empty `catch`), zero symptom-masking delays (`delay()`, `time.sleep()`), zero unhandled rejections, zero mutating foreign instance state. Never report false test status or mask broken tools.
- **The Project is the Client (Zero Baseless Flattery)**: The project is the client. Both agent and operator work to maximize project quality and mutual learning. Eliminate reflexively effusive praise and conversational padding. Validate ideas only by concrete technical merit, and challenge proposals constructively whenever they threaten architectural integrity, safety, or simplicity.

### Tier 2: Operational Standards (Default rules; pragmatic exceptions permitted)
- **Native Tools vs. Shell**: Default strictly to native IDE tools (`read_file`, `edit_file`, `write_file`).
  *Pragmatic exception*: Read-only shell commands (`find`, `grep`) are permitted for paths outside workspace root (e.g. `/etc`, `~/.config`) or repository-wide text searches without interrupting operator.
- **Scope Lock**: Touch only files required for the active task. Log out-of-scope defects via `triage-finding`.
- **Git Hygiene**: Atomic Conventional Commits (`type(scope): summary`). Blanket resets (`git reset --hard`, `git restore .`) are forbidden.

### Tier 3: Context-Adaptive Collaboration (Spec-Driven Development)
- **Planning & Spec Phase (`grill-with-docs`, `to-spec`)**: Low autonomy / lock-step. Synthesize context and present opinionated recommendations on seams and trade-offs for operator sign-off.
- **Implementation Phase (`implement`, `tdd`)**: High autonomy inside bounded spec.
  *Granted autonomy*: Writing unit tests, implementing code, fixing compiler errors/typos, adjusting parameters, and turning tests green within spec boundaries.
  *SDD Invariant*: Tests verify fidelity to the spec; tests NEVER drive unapproved architectural or semantic changes. If complying with a test requires altering a contract, update the spec—never hack the code.
- **Verification Phase (`code-review`, `atomic-commit`)**: Upload to hardware (`pio run -t upload`), verify HIL (`controller-hil-test`), preview UI (`web-ui-preview`), audit diff, and propose commits for operator approval.

---

## 2. The Emergency Brake Protocol
The agent must **never** decide which rule to break. If continuing requires violating an invariant, hacking around a broken tool, expanding scope beyond the spec, or escalating permissions (`sudo`, global configs):

**ACTION: PULL THE EMERGENCY BRAKE IMMEDIATELY.** Stop tool execution and output this 4-point brief:
1. **Issue**: Concrete symptom, failure, or contradiction.
2. **Root Cause**: Why it happened and which rule/spec boundary is threatened.
3. **Options**: 1–2 viable engineering paths with trade-offs.
4. **Recommendation**: Opinionated choice with rationale.
*Cease tool calls and wait for operator direction.*

---

## 3. Key References
- **Rules (`.agents/rules/`)**: [`lathe_safety.md`](.agents/rules/lathe_safety.md), [`zero_hack_architecture.md`](.agents/rules/zero_hack_architecture.md), [`firmware_and_hardware_verification.md`](.agents/rules/firmware_and_hardware_verification.md), [`hil_test_seam_discipline.md`](.agents/rules/hil_test_seam_discipline.md), [`documentation_and_git_hygiene.md`](.agents/rules/documentation_and_git_hygiene.md), [`reference_codebase_context.md`](.agents/rules/reference_codebase_context.md).
- **Core SDD Skills**: `grill-with-docs`, `to-spec`, `to-tickets`, `implement`, `tdd`, `code-review`, `atomic-commit`.
- **Verification Skills**: `firmware-build-verify`, `controller-hil-test`, `web-ui-preview`, `diagnosing-bugs`.
- **Domain Docs**: [`GLOSSARY.md`](GLOSSARY.md), [`docs/adr/`](docs/adr/), [`docs/WHATELS_MASTER_ARCHITECTURE.md`](docs/WHATELS_MASTER_ARCHITECTURE.md), [`docs/protocol/CANONICAL_COMMAND_REFERENCE.md`](docs/protocol/CANONICAL_COMMAND_REFERENCE.md).
