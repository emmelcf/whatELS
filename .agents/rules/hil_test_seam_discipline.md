---
trigger: model_decision
description: Apply whenever creating, modifying, or running Python HIL tests in tests/hil/ or C++ tests in test/.
---

# HIL & Regression Test Seam Discipline

1. **Specification Alignment:** All tests in `tests/hil/suites/test_*.py` must adhere to `docs/testing/HIL_TEST_HARNESS_SPECIFICATION.md`.
2. **Deterministic Synchronization:** Use `tests/hil/core/client.py` and `assertions.py` state/telemetry waiters. Never use arbitrary `asyncio.sleep()` or `time.sleep()` to guess when motion or mode transitions settle.
3. **Safety & Clean Teardown:** Every test in `BaseTest` (`tests/hil/core/base_test.py`) must leave the controller in a known safe state (spindle/motion halted, emergency stop cleared, soft limits restored) so subsequent suites in `run_tests.py` remain isolated and idempotent.
4. **Bug Reproduction First:** When fixing a firmware or protocol defect, first add a failing test case in the relevant `tests/hil/suites/test_*.py` suite, verify it fails against the live ESP32-S3 via `controller-hil-test`, apply the root-cause fix, and re-verify.
