---
trigger: model_decision
description: Documentation co-execution requirements (docs/ sync on protocol/state changes) and Conventional Commit scopes.
---

# Documentation & Git Hygiene Rules

## 1. Documentation Co-Execution Gate
A task is **incomplete** if code behavior changes without updating its corresponding documentation in `docs/`:
- **Protocol / Command changes (`src/core/CommandDispatcher.*`, `src/web/shared_protocol/ui_telemetry.h`):** Must update `docs/protocol/CANONICAL_COMMAND_REFERENCE.md` and `docs/state/ELS_CORE_STATE_MODEL.md`.
- **Mode behavior changes (`src/motion/modes/*`):** Must update the corresponding `docs/modes/<mode>/SPECIFICATION.md` and `docs/motion/MODE_INTEGRATION_CONTRACT.md`.
- **Web UI ergonomic or workflow changes (`clients/web_ui/*`):** Must update `docs/clients/web_ui/INTERACTION_LEDGER.md` or `ERGONOMICS.md`.
- **Architectural shifts:** Must record a new ADR in `docs/adr/NNNN-<slug>.md` before implementation.

## 2. Conventional Commits & Atomic Staging
All commits must follow:
`<type>(<scope>): <imperative summary> (#<issue-id>)`

- **Allowed `<type>`:** `feat`, `fix`, `refactor`, `test`, `docs`, `perf`, `build`, `chore`
- **Allowed `<scope>`:** `kernel`, `hal`, `modes`, `protocol`, `core`, `webui`, `presets`, `hil`, `docs`, `agents`
- **Decoupling Mandate:** Never mix a refactoring change, a firmware feature, and a test harness framework change in one monolithic commit. Stage and commit logical units separately using the `atomic-commit` skill:
  1. Spec / ADR / Documentation foundation (`docs(...)`)
  2. Firmware or WebUI implementation (`feat(...)` or `fix(...)`)
  3. Regression / HIL verification suite (`test(hil): ...`)
- **Operator Confirmation:** Always propose commit chunks and messages for operator review before committing.
