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

3. **Pre-Commit Quality Gates (Strict Sequence):**
   - **Phase A — Firmware Upload:** If any C++ files (`src/`), embedded assets (`data/presets/`, `SystemPresetsJson.h`), or `platformio.ini` changed, run `pio run -e esp32s3 -t upload` (builds and flashes in one command; wait 5–10s for reboot/Wi-Fi).
   - **Phase B — Controller HIL Verification:** Run `controller-hil-test` (`python3 tests/hil/run_tests.py` over WebSocket). Verify all motion and protocol assertions pass on live hardware.
   - **Phase C — UI Verification Gate:** ONLY after Phase B passes on live hardware, test `clients/web_ui/` via `web-ui-preview` against the live controller (`ws://whatels.local:81`). Never test UI against un-flashed or unverified firmware.
   - **Phase D — Documentation Parity:** Update any affected `docs/` files in lockstep with code diffs.

4. **Review & Commit:**
   - Run `code-review` to audit spec fidelity and check against Fowler code smells.
   - Run `atomic-commit` to propose staged Conventional Commits for operator review and confirmation.

