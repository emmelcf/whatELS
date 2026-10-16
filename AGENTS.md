# NanoELS Development Protocols & Agent Rules

## 1. True Pair Programming Protocol
- **Discuss Before Writing Code**: When exploratory questions, ideas, or architectural topics are raised:
  - **STOP**: Do NOT edit files, run implementation tools, or treat ideas as pre-approved.
  - Review relevant specifications in `docs/` first.
  - Analyze trade-offs, ergonomic impacts, and options in chat.
  - Present choices and **wait for explicit user alignment and consent** before touching code.
- **Lock-Step Alignment**: Never jump ahead or assume direction. Work iteratively:
  $$\text{Agree on Concept} \longrightarrow \text{Approve Plan} \longrightarrow \text{Execute Slice}$$
- **Micro-Commits & Revert Safety**: Commit focused milestones upon explicit agreement. Never execute blanket rollbacks (`git restore .`, `git reset --hard`).

## 2. Tooling & Execution Discipline
- **Prefer IDE Tools Over Terminal Commands**:
  - Use native IDE tools (`grep_search`, `list_dir`, `view_file`, `replace_file_content`, `write_to_file`) for all file inspection, search, and editing.
  - Do NOT use bash commands (`cat`, `ls`, `find`, `grep`, `sed`) when native tools exist.
  - Reserve `run_command` strictly for operations that genuinely require shell execution (e.g., `git`, `pio run`).
- **Grounding in Documentation**:
  - Always consult authoritative documents in `docs/` (e.g., `NANOELS_MASTER_ARCHITECTURE.md`, `docs/clients/ELS_CLIENT_ARCHITECTURE.md`) before suggesting designs or changes.

## 3. Domain Rules & Specialized Runbooks
- **Web UI & 800×480 Ergonomics**: Follow [`.agents/rules/lathe_hmi_and_safety.md`](file:///home/emil/code/nanoels/.agents/rules/lathe_hmi_and_safety.md)
- **Firmware Safety & Motion Core**: Follow [`.agents/rules/firmware_and_hardware_verification.md`](file:///home/emil/code/nanoels/.agents/rules/firmware_and_hardware_verification.md)
- **Firmware Verification**: Use skill `firmware-build-verify` (`pio run -e esp32s3`)
- **Web UI Preview**: Use skill `web-ui-preview`
