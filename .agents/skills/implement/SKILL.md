---
name: implement
description: "Implement an approved ticket or spec via TDD, firmware/HIL verification, and atomic commit."
disable-model-invocation: true
---

# Implement

Executes the implementation of an approved ticket or specification.

## Workflow

1. **Fetch & Announce Context:**
   - If a ticket reference is provided (e.g. `#42`), fetch it via `gh issue view <number>`.
   - Announce the ticket title, target scope, and acceptance criteria before modifying code.

2. **TDD Loop (`tdd`):**
   - Identify or create the pre-agreed HIL test seam in `tests/hil/suites/test_*.py`.
   - Write the failing test case asserting expected telemetry or motion behavior.
   - Implement the minimal firmware (C++) or client (JS) code required to turn the test green.
   - For `clients/web_ui/` changes: verify ergonomics and layout in Antigravity Live Preview (`web-ui-preview`), maintaining asset parity (`scripts/embed_controller_assets.py` / `scripts/generate_presets.py`).

3. **Pre-Commit Quality Gates:**
   - **Build Verification:** Run `firmware-build-verify` (PlatformIO).
   - **HIL Regression:** Run `controller-hil-test` (`tests/hil/run_tests.py` over WebSocket).
   - **Documentation Parity:** Update any affected `docs/` files in lockstep with code diffs.

4. **Review & Commit:**
   - Run `code-review` to audit spec fidelity and check against Fowler code smells.
   - Run `atomic-commit` to propose staged Conventional Commits for operator review and confirmation.
