#!/usr/bin/env python3
"""NanoELS Hardware-in-the-Loop Test Runner.

Discovers and executes end-to-end integration test suites over WebSocket.
"""

from __future__ import annotations

import argparse
import asyncio
import importlib
import inspect
from pathlib import Path
import pkgutil
import sys

# Ensure local 'core' and 'suites' packages are resolvable
sys.path.insert(0, str(Path(__file__).parent))

from core.base_test import HilTestCase, TestResult
from core.client import NanoElsClient
from core.reporter import emit_json_report, print_terminal_report


def discover_test_classes(filter_keyword: str = "") -> list[type[HilTestCase]]:
    """Dynamically discovers all HilTestCase subclasses inside tests/hil/suites/."""
    suites_dir = Path(__file__).parent / "suites"
    test_classes = []

    for _, module_name, _ in pkgutil.iter_modules([str(suites_dir)]):
        module = importlib.import_module(f"suites.{module_name}")
        for attr_name in dir(module):
            attr = getattr(module, attr_name)
            if (
                inspect.isclass(attr)
                and issubclass(attr, HilTestCase)
                and attr is not HilTestCase
            ):
                if not filter_keyword or filter_keyword.lower() in attr.name.lower():
                    test_classes.append(attr)

    # Sort deterministically by name
    test_classes.sort(key=lambda cls: cls.name)
    return test_classes


async def main_async() -> int:
    parser = argparse.ArgumentParser(description="NanoELS Hardware-in-the-Loop Test Runner")
    parser.add_argument(
        "--host",
        default="nanoels.local",
        help="Controller IP/Hostname (default: nanoels.local)",
    )
    parser.add_argument(
        "--port",
        type=int,
        default=81,
        help="WebSocket port (default: 81)",
    )
    parser.add_argument(
        "--filter",
        default="",
        help="Case-insensitive test name filter",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit machine-readable JSON output",
    )
    args = parser.parse_args()

    test_classes = discover_test_classes(args.filter)
    if not test_classes:
        print(f"No test cases found matching filter: '{args.filter}'", file=sys.stderr)
        return 1

    client = NanoElsClient(host=args.host, port=args.port)
    try:
        await client.connect(timeout_s=4.0)
    except Exception as conn_err:
        err_result = [
            TestResult(
                name="controller_connection",
                status="ERROR",
                duration_s=0.0,
                error_message=str(conn_err),
            )
        ]
        if args.json:
            emit_json_report(err_result, host=args.host)
        else:
            print_terminal_report(err_result, host=args.host)
        return 1

    results = []
    try:
        for test_cls in test_classes:
            test_instance = test_cls(client)
            result = await test_instance.execute()
            results.append(result)
    finally:
        await client.disconnect()

    if args.json:
        emit_json_report(results, host=args.host)
    else:
        print_terminal_report(results, host=args.host)

    # Exit code: 0 if all tests passed, 1 if any failure/error
    all_passed = all(r.status == "PASS" for r in results)
    return 0 if all_passed else 1


if __name__ == "__main__":
    sys.exit(asyncio.run(main_async()))
