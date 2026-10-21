---
name: atomic-commit
description: Stage decoupled Conventional Commits, verifying doc parity, test status, and generated asset sync before proposing commands.
---

# Atomic Commit & Git Hygiene Skill

## Workflow
1. **Inspect Working Tree & Diffs:**
   - Run `git status --short` and `git diff --stat`.
   - Identify all modified domains (`docs/`, `src/motion/`, `src/core/`, `src/web/`, `clients/web_ui/`, `data/`, `tests/hil/`).

2. **Enforce Pre-Commit Quality Gates:**
   - **Generated Asset Check:** If `data/presets/*` or `clients/web_ui/*` (embedded targets) changed, verify whether `scripts/generate_presets.py` or `scripts/embed_controller_assets.py` needs to run.
   - **Documentation Parity Check:** Verify that any protocol, state, or mode behavior change has a matching update in `docs/`. If missing, halt and update the documentation first.
   - **Verification Gate:** Confirm `firmware-build-verify` and/or `controller-hil-test` have passed for the current changes.

3. **Decompose into Atomic Commit Units:**
   Group files so each commit does **one logical thing** and leaves the repository buildable:
   - **Group 1:** `docs/adr/*`, `docs/GLOSSARY.md`, active `docs/` -> `docs(<scope>): ...`
   - **Group 2:** `src/*` (Firmware core/kernel/modes) + `docs/motion|modes|protocol/*` -> `feat|fix|refactor(<scope>): ...`
   - **Group 3:** `clients/web_ui/*`, `data/www/*` -> `feat|fix(webui): ...`
   - **Group 4:** `tests/hil/*`, `test/*` -> `test(hil): ...`

4. **Propose & Execute:**
   - Present the proposed sequence of `git add <files>` and `git commit -m "<type>(<scope>): <summary> (Refs #<issue>)"` commands.
   - Propose commands to the operator for review and confirmation. Do NOT perform blanket `git add .` or blanket rollbacks (`git restore .`, `git reset --hard`).
