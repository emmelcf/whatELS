"""Abstract base class and execution envelope for HIL test cases."""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
import time
from typing import Any

from .client import NanoElsClient


@dataclass
class TestResult:
    name: str
    status: str  # "PASS" | "FAIL" | "ERROR"
    duration_s: float
    details: dict[str, Any] = field(default_factory=dict)
    error_message: str = ""


class HilTestCase(ABC):
    """Abstract base class for all NanoELS hardware-in-the-loop test cases."""

    name: str = "unnamed_test"
    description: str = ""
    timeout_s: float = 10.0

    def __init__(self, client: NanoElsClient):
        self.client = client
        self.details: dict[str, Any] = {}

    async def setup(self) -> None:
        """Default setup: stop any active feed, enable simulation mode, zero Z/X axes."""
        await self.client.send("CMD.stop")
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.simEncoder=1")
        await self.client.send("CMD.axisLimitDu=Z,1,2147483647")
        await self.client.send("CMD.axisLimitDu=Z,0,-2147483648")
        await self.client.send("CMD.axisZero=Z")
        await self.client.send("CMD.axisZero=X")
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
        """Guaranteed teardown: stop feed, clear simulation RPM, disable simulation mode."""
        try:
            await self.client.send("CMD.stop")
            await self.client.send("CMD.simRpm=0")
            await self.client.send("CMD.simEncoder=0")
        except Exception:
            pass

    async def execute(self) -> TestResult:
        """Executes setup(), run(), and teardown() with timing and safety guards."""
        start_time = time.perf_counter()
        status = "PASS"
        error_msg = ""

        try:
            await self.setup()
            await self.run()
        except AssertionError as e:
            status = "FAIL"
            error_msg = str(e)
        except Exception as e:
            status = "ERROR"
            error_msg = f"{type(e).__name__}: {str(e)}"
        finally:
            try:
                await self.teardown()
            except Exception as td_err:
                if status == "PASS":
                    status = "ERROR"
                    error_msg = f"Teardown error: {td_err}"

        duration = time.perf_counter() - start_time
        return TestResult(
            name=self.name,
            status=status,
            duration_s=round(duration, 3),
            details=self.details,
            error_message=error_msg,
        )
