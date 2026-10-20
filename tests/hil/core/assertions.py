"""Domain assertion primitives for whatELS HIL tests."""

from __future__ import annotations
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from .client import ControllerState


def assert_pos_du(
    actual_du: int,
    expected_du: int,
    tolerance_du: int = 2,
    axis: str = "Z",
) -> None:
    """Asserts that an axis coordinate matches expected value within +/- tolerance_du."""
    diff = abs(actual_du - expected_du)
    assert diff <= tolerance_du, (
        f"Axis {axis} position mismatch! Expected: {expected_du} du, "
        f"Actual: {actual_du} du, Error: {diff} du (Allowed tolerance: +/-{tolerance_du} du)"
    )


def assert_halted(state: ControllerState, reason: str = "") -> None:
    """Asserts that the controller has halted and is_on is False."""
    assert not state.is_on, (
        f"Expected controller to be halted, but is_on is True! (Context: {reason})"
    )


def assert_moving(state: ControllerState, reason: str = "") -> None:
    """Asserts that the controller is in active motion."""
    assert state.is_on, (
        f"Expected controller to be in active feed (is_on=True), but is_on is False! (Context: {reason})"
    )


def assert_rpm(
    actual_rpm: int,
    expected_rpm: int,
    tolerance_rpm: int = 15,
) -> None:
    """Asserts that reported spindle speed matches expected value within tolerance."""
    diff = abs(actual_rpm - expected_rpm)
    assert diff <= tolerance_rpm, (
        f"Spindle RPM mismatch! Expected: {expected_rpm} RPM, "
        f"Actual: {actual_rpm} RPM, Error: {diff} RPM (Tolerance: +/-{tolerance_rpm} RPM)"
    )
