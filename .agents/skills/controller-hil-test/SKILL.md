---
name: controller-hil-test
description: >-
  Runs Hardware-in-the-Loop (HIL) integration tests against the ESP32-S3 controller over WebSocket.
  Use to verify motion synchronization, limits, jogging, or live regressions.
---

# Controller Hardware-in-the-Loop (HIL) Testing Runbook

## 1. Prerequisites
- ESP32-S3 powered on, whatELS firmware running, WebSocket listening on port 81.
- Reachable over local network via `whatels.local` or direct IP.
- Python 3 with `websockets` installed.

## 2. Test Execution
All suites reside in `tests/hil/suites/` and are auto-discovered by the runner:

```bash
# Default (ws://whatels.local:81)
python3 tests/hil/run_tests.py

# Specify IP, filter by keyword, or output JSON
python3 tests/hil/run_tests.py --host 192.168.1.150
python3 tests/hil/run_tests.py --filter gearing
python3 tests/hil/run_tests.py --test test_gearing_sync_1_5mm
python3 tests/hil/run_tests.py --json
```

## 3. Failure Protocol (Pair Programming)
If any test fails during execution:
1. **Analyze, Never Auto-Fix**: Do **not** modify firmware or test code autonomously.
2. **Present Findings**: Report the failed test name, exact assertion failure, expected vs. actual values, and root cause hypothesis.
3. **Await Agreement**: Propose the minimal fix and obtain explicit user confirmation before applying any changes.
