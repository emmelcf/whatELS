"""Formatters for human-readable ANSI terminal output and machine-readable JSON."""

from __future__ import annotations

from datetime import datetime, timezone
import json
import sys
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .base_test import TestResult

GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"


def print_terminal_report(results: list[TestResult], target: str) -> None:
    """Prints a polished ANSI terminal table of test outcomes."""
    total = len(results)
    passed = sum(1 for r in results if r.status == "PASS")
    failed = sum(1 for r in results if r.status == "FAIL")
    errors = sum(1 for r in results if r.status == "ERROR")
    total_time = sum(r.duration_s for r in results)

    print()
    print("=" * 80)
    print(f"{BOLD}{CYAN}                    NANOELS HARDWARE-IN-THE-LOOP TEST SUITE{RESET}")
    print(f"Target: {target} | Timestamp: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 80)
    print(f"{BOLD}{'TEST NAME':<34} {'STATUS':<9} {'DURATION':<10} {'DETAILS'}{RESET}")
    print("-" * 80)

    for r in results:
        if r.status == "PASS":
            status_str = f"{GREEN}[PASS]{RESET}"
        elif r.status == "FAIL":
            status_str = f"{RED}[FAIL]{RESET}"
        else:
            status_str = f"{YELLOW}[ERR]{RESET}"

        duration_str = f"{r.duration_s:.2f}s"
        details_str = ""
        if r.error_message:
            details_str = f"{RED}{r.error_message[:35]}{RESET}"
        elif r.details:
            details_parts = [f"{k}: {v}" for k, v in list(r.details.items())[:2]]
            details_str = ", ".join(details_parts)

        print(f"{r.name:<34} {status_str:<18} {duration_str:<10} {details_str}")

    print("-" * 80)
    summary_color = GREEN if (failed == 0 and errors == 0) else RED
    print(
        f"{BOLD}SUMMARY:{RESET} {passed} Passed, {failed} Failed, {errors} Errors | "
        f"Total Time: {total_time:.2f}s"
    )
    if failed == 0 and errors == 0:
        print(f"{BOLD}{summary_color}RESULT: ALL TESTS PASSED (Exit code: 0){RESET}")
    else:
        print(f"{BOLD}{summary_color}RESULT: TEST SUITE FAILED (Exit code: 1){RESET}")
    print("=" * 80)
    print()


def emit_json_report(results: list[TestResult], target: str) -> None:
    """Emits structured JSON output to stdout for CI/CD and AI agent analysis."""
    payload = {
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "target": target,
        "total": len(results),
        "passed": sum(1 for r in results if r.status == "PASS"),
        "failed": sum(1 for r in results if r.status == "FAIL"),
        "errors": sum(1 for r in results if r.status == "ERROR"),
        "duration_s": round(sum(r.duration_s for r in results), 3),
        "results": [
            {
                "name": r.name,
                "status": r.status,
                "duration_s": r.duration_s,
                "details": r.details,
                "error": r.error_message,
            }
            for r in results
        ],
    }
    json.dump(payload, sys.stdout, indent=2)
    print()
