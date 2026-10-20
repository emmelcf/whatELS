# whatELS Development Protocols & Agent Rules

## 1. True Pair Programming Protocol
- **Discuss Before Writing Code**: On exploratory questions, ideas, or architectural topics: STOP, discuss trade-offs in chat, present choices, and wait for explicit alignment before touching code.
- **Lock-Step Alignment**: Agree on Concept $\longrightarrow$ Approve Plan $\longrightarrow$ Execute Slice.
- **Workflow Integrity & Honesty**: Report unaltered truth. If a feature, edge case, or decision was missed, state it plainly ("We missed [X]") and propose the fix before executing.
- **Micro-Commits & Revert Safety**: Commit focused milestones upon explicit agreement. Indiscriminate blanket rollbacks (`git restore .`, `git reset --hard`) are strictly prohibited.
- **Systemic Persistence**: When behavioral constraints or workflow agreements are established, codify them immediately into rules or docs.

## 2. Tooling & Execution Discipline
- **Native Tools Over Shell**: Use native IDE tools (`grep_search`, `view_file`, `replace_file_content`, `write_to_file`) for all file inspection and editing. Reserve `run_command` strictly for `git`, `pio run`, etc.
- **Grounding in Documentation**: Consult authoritative documents in `docs/` before suggesting designs or changes.

## 3. Domain Rules & Skills
- **Lathe Safety & Operator Invariants**: Follow [`.agents/rules/lathe_safety.md`](.agents/rules/lathe_safety.md) (`always_on`).
- **Web UI & 800×480 Ergonomics**: Follow [`docs/clients/web_ui/ERGONOMICS.md`](docs/clients/web_ui/ERGONOMICS.md).
- **Firmware Invariants**: Follow [`.agents/rules/firmware_and_hardware_verification.md`](.agents/rules/firmware_and_hardware_verification.md) (on demand).
- **Reference Codebase**: Follow [`.agents/rules/reference_codebase_context.md`](.agents/rules/reference_codebase_context.md) (on demand).
- **Skills**: `firmware-build-verify`, `web-ui-preview`.
