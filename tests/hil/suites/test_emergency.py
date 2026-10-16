"""Emergency stop and feed disengagement latency test cases."""

from __future__ import annotations

import time
from core.assertions import assert_halted
from core.base_test import HilTestCase


class TestStopCommandLatency(HilTestCase):
    name = "stop_command_latency"
    description = "Verify CMD.stop disengages active feed and transitions to halted state"

    async def run(self) -> None:
        # 1. Engage active feed and start continuous spindle
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK")
        await self.client.send("CMD.simRpm=600")
        await self.client.wait_for(
            lambda s: s.is_on,
            timeout_s=3.0,
            description="feed actively moving",
        )

        # 2. Issue immediate stop command
        t0 = time.perf_counter()
        await self.client.send("CMD.stop")

        # 3. Wait for controller to register halted state
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=1.0,
            description="halt confirmation",
        )
        elapsed_ms = (time.perf_counter() - t0) * 1000.0

        assert elapsed_ms < 200.0, f"Stop latency too high: {elapsed_ms:.1f} ms (expected <200 ms)"
        assert_halted(state, "stop command response")
        self.details = {"stop_latency_ms": round(elapsed_ms, 2)}
