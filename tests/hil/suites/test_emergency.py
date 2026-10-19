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


class TestSimWatchdogTimeoutHalt(HilTestCase):
    name = "sim_watchdog_timeout_halt"
    description = "Verify Core 1 sim watchdog disengages feed and zeroes RPM when heartbeats cease for >1.5s"

    async def run(self) -> None:
        import asyncio

        # 1. Start continuous simulated spindle and engage feed
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle stable at 600 RPM")
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=3.0, description="feed actively moving")

        # 2. Intentionally suppress client heartbeat for 1.8s (> 1.5s watchdog timeout)
        t_start = time.perf_counter()
        self.client.suppress_heartbeat(True)
        await asyncio.sleep(1.8)
        self.client.suppress_heartbeat(False)

        # 3. Wait for controller to register halted state and zeroed RPM
        state = await self.client.wait_for(
            lambda s: not s.is_on and s.rpm == 0,
            timeout_s=2.0,
            description="watchdog failsafe halt and zero RPM",
        )
        elapsed_ms = (time.perf_counter() - t_start) * 1000.0

        assert not state.is_on, "Controller is still in motion after watchdog timeout!"
        assert state.rpm == 0, f"Simulated RPM not zeroed after watchdog timeout: {state.rpm}"
        self.details = {
            "timeout_trip_ms": round(elapsed_ms, 2),
            "halted_is_on": state.is_on,
            "halted_rpm": state.rpm,
        }

