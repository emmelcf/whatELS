# whatELS — Master Agent Charter & Pair Protocols

## 1. Operating Protocols
- **Discuss Before Code**: On concepts, trade-offs, or architectures: STOP, discuss, and align before writing code.
- **Workflow Honesty**: Report unaltered truth. If a flaw, edge case, or assumption was missed, state it plainly ("We missed [X]").
- **Micro-Commits & Revert Safety**: Propose focused commits for operator review. Blanket rollbacks (`git restore .`, `git reset --hard`) are strictly forbidden.
- **Native Tools Over Shell**: Inspect and edit files strictly with native IDE tools. Never use shell commands (`cat`, `sed`, redirects) on files; reserve `run_command` for `git`, `gh`, and `pio`.
- **Active Docs Grounding**: Consult active `docs/` (e.g. [`docs/WHATELS_MASTER_ARCHITECTURE.md`](docs/WHATELS_MASTER_ARCHITECTURE.md)). Never use `docs/archive/` as architectural truth.
- **Systemic Persistence**: Codify agreed behavioral rules or architecture immediately into `docs/` or `.agents/`.

## 2. Zero-Hack Mandate & Scope Lock
- Adhere strictly to ADR-0001 (Unified Gearbox), ADR-0002 (Dumb Client), and ADR-0003 (Command Result). Zero inline hacks, zero client kinematics, zero arbitrary delays (`delay()`, `time.sleep()`).
- **Scope Lock**: Only edit files required by the active task. For out-of-scope bugs or ideas, run `triage-finding` to log a GitHub Issue and resume immediately.

## 3. Four-Gate Engineering Pipeline
1. **Grill & Align (`grill-with-docs`)**: Pre-read repo/docs. Run Frontier Batch rounds (3–5 questions `❓` with recommendations `➡️`). Zero code until operator aligns.
2. **Spec & Tickets (`to-spec` $\longrightarrow$ `to-tickets`)**: Publish specification to GitHub Issues with explicit HIL test seams (`tests/hil/suites/test_*.py`). Decompose into atomic child tickets.
3. **Execution (`implement`)**: Execute ticket via `tdd` against HIL seams, PlatformIO build, Live Preview (`web-ui-preview`), and WebSocket HIL verify (`controller-hil-test`).
4. **Verification & Commit (`code-review` $\longrightarrow$ `atomic-commit`)**: Audit diff against spec/standards. Propose Conventional Commits for operator approval.

## 4. Key References
- **Rules (`.agents/rules/`)**: [`lathe_safety.md`](.agents/rules/lathe_safety.md), [`zero_hack_architecture.md`](.agents/rules/zero_hack_architecture.md), [`documentation_and_git_hygiene.md`](.agents/rules/documentation_and_git_hygiene.md), [`hil_test_seam_discipline.md`](.agents/rules/hil_test_seam_discipline.md), [`firmware_and_hardware_verification.md`](.agents/rules/firmware_and_hardware_verification.md).
- **Skills**: `ask-matt`, `grill-with-docs`, `to-spec`, `to-tickets`, `implement`, `tdd`, `code-review`, `atomic-commit`, `diagnosing-bugs`, `triage`, `triage-finding`, `firmware-build-verify`, `controller-hil-test`, `web-ui-preview`, `sync-todo-to-gh`.
- **Docs**: [`WHATELS_MASTER_ARCHITECTURE.md`](docs/WHATELS_MASTER_ARCHITECTURE.md), [`CANONICAL_COMMAND_REFERENCE.md`](docs/protocol/CANONICAL_COMMAND_REFERENCE.md), [`HIL_TEST_HARNESS_SPECIFICATION.md`](docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md), [`ERGONOMICS.md`](docs/clients/web_ui/ERGONOMICS.md).
