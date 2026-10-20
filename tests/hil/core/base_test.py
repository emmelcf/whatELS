"""Abstract base class and execution envelope for HIL test cases."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
import time
import traceback
from typing import Any

from .client import WhatElsClient


@dataclass
class TestResult:
    name: str
    status: str  # "PASS" | "FAIL" | "ERROR"
    duration_s: float
    details: dict[str, Any] = field(default_factory=dict)
    error_message: str = ""
    traceback: str = ""
    state_snapshot: dict[str, Any] = field(default_factory=dict)


class HilTestCase(ABC):
    """Abstract base class for all whatELS hardware-in-the-loop test cases."""

    name: str = "unnamed_test"
    description: str = ""
    timeout_s: float = 10.0

    def __init__(self, client: WhatElsClient):
        self.client = client
        self.details: dict[str, Any] = {}

    async def setup(self) -> None:
        """Default setup: stop any active feed, enable simulation mode, zero Z/X axes with ACK synchronization."""
        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm == 0 and not s.is_on, timeout_s=4.0, description="spindle stopped before sim engage")
        await self.client.send("CMD.simEncoder=1", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,R", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=X,U", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=X,D", wait_ack=True)
        await self.client.send("CMD.taperRatio=0", wait_ack=True)
        await self.client.send("CMD.axisZero=Z", wait_ack=True)
        await self.client.send("CMD.axisZero=X", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.z_du == 0 and s.x_du == 0 and not s.is_on,
            timeout_s=3.0,
            description="axes zeroed and motion stopped in setup",
        )

    @abstractmethod
    async def run(self) -> None:
        """Core scenario logic and assertions. Must be implemented by subclasses."""
        pass

    async def teardown(self) -> None:
        """Guaranteed teardown: restore heartbeat, stop feed, clear simulation RPM, disable simulation mode."""
        try:
            self.client.suppress_heartbeat(False)
            await self.client.send("CMD.stop", wait_ack=False)
            await self.client.send("CMD.simRpm=0", wait_ack=False)
            await self.client.send("CMD.simEncoder=0", wait_ack=False)
        except Exception:
            pass

    async def execute(self) -> TestResult:
        """Executes setup(), run(), and teardown() with timing and safety guards."""
        start_time = time.perf_counter()
        status = "PASS"
        error_msg = ""
        tb = ""
        state_snap: dict[str, Any] = {}

        try:
            await self.setup()
            await self.run()
        except AssertionError as e:
            status = "FAIL"
            error_msg = str(e)
            tb = traceback.format_exc()
            state_snap = {
                "z_du": self.client.state.z_du,
                "x_du": self.client.state.x_du,
                "rpm": self.client.state.rpm,
                "turns": self.client.state.turns,
                "is_on": self.client.state.is_on,
                "mode": self.client.state.mode,
                "estop": self.client.state.estop,
                "z_left_du": self.client.state.z_left_du,
                "z_right_du": self.client.state.z_right_du,
            }
        except Exception as e:
            status = "ERROR"
            error_msg = f"{type(e).__name__}: {str(e)}"
            tb = traceback.format_exc()
            state_snap = {
                "z_du": self.client.state.z_du,
                "x_du": self.client.state.x_du,
                "rpm": self.client.state.rpm,
                "turns": self.client.state.turns,
                "is_on": self.client.state.is_on,
                "mode": self.client.state.mode,
                "estop": self.client.state.estop,
                "z_left_du": self.client.state.z_left_du,
                "z_right_du": self.client.state.z_right_du,
            }
        finally:
            try:
                await self.teardown()
            except Exception as td_err:
                if status == "PASS":
                    status = "ERROR"
                    error_msg = f"Teardown error: {td_err}"
                    tb = traceback.format_exc()

        duration = time.perf_counter() - start_time
        return TestResult(
            name=self.name,
            status=status,
            duration_s=round(duration, 3),
            details=self.details,
            error_message=error_msg,
            traceback=tb,
            state_snapshot=state_snap,
        )
