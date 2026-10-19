"""Emergency stop, safety gatekeeper, and watchdog test cases."""

from __future__ import annotations

import asyncio
import time
from core.assertions import assert_halted
from core.base_test import HilTestCase
from core.client import CommandRejectedError


class TestStopCommandLatency(HilTestCase):
    name = "stop_command_latency"
    description = "Verify CMD.stop disengages both synchronous and asynchronous feeds in <100ms"

    async def run(self) -> None:
        # --- Part 1: Synchronized Feed Stop Latency ---
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle at 600 RPM")
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=3.0, description="sync feed actively moving")

        t0 = time.perf_counter()
        await self.client.send("CMD.stop", wait_ack=True)
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=1.0, description="sync halt confirmation")
        sync_stop_ms = (time.perf_counter() - t0) * 1000.0

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        assert_halted(state, "sync stop command response")
        assert sync_stop_ms < 100.0, f"Synchronized stop latency too high: {sync_stop_ms:.1f} ms (expected <100 ms)"

        # --- Part 2: Asynchronous Velocity Feed Stop Latency ---
        await self.client.send("CMD.startFeed=axis:Z,sync:0,feed:3000000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=3.0, description="async feed actively moving")

        t1 = time.perf_counter()
        await self.client.send("CMD.stop", wait_ack=True)
        state_async = await self.client.wait_for(lambda s: not s.is_on, timeout_s=1.0, description="async halt confirmation")
        async_stop_ms = (time.perf_counter() - t1) * 1000.0

        assert_halted(state_async, "async stop command response")
        assert async_stop_ms < 100.0, f"Asynchronous stop latency too high: {async_stop_ms:.1f} ms (expected <100 ms)"

        self.details = {
            "sync_stop_ms": round(sync_stop_ms, 2),
            "async_stop_ms": round(async_stop_ms, 2),
        }


class TestInMotionSafetyGatekeeperLockout(HilTestCase):
    name = "in_motion_safety_gatekeeper_lockout"
    description = "Verify [SAFE-01] single-gate dispatcher rejects configuration commands during active cutting"

    async def run(self) -> None:
        # 1. Engage active synchronized cutting feed at 600 RPM
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle stable at 600 RPM")
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=3.0, description="feed actively moving")

        # 2. Test the 6 forbidden mutations; all must raise CommandRejectedError
        forbidden_commands = [
            "CMD.axisZero=Z",
            "CMD.gearDir=TAIL",
            "CMD.gearAxis=X",
            "CMD.gearSync=0",
            "CMD.feedMm=2.0",
            "CMD.mode=TURNING",
        ]

        rejected_count = 0
        for cmd in forbidden_commands:
            try:
                await self.client.send(cmd, wait_ack=True, timeout_s=1.0)
                assert False, f"Safety violation: Command '{cmd}' was NOT rejected during active motion!"
            except CommandRejectedError:
                rejected_count += 1

        assert rejected_count == len(forbidden_commands), (
            f"Expected all {len(forbidden_commands)} commands to be rejected, but only {rejected_count} were rejected"
        )

        # 3. Motion must still be actively and safely cutting
        assert self.client.state.is_on, "Controller motion was unexpectedly aborted by rejected commands!"

        # 4. Stop command must succeed immediately
        await self.client.send("CMD.stop", wait_ack=True)
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=1.0, description="motion halted")
        await self.client.send("CMD.simRpm=0", wait_ack=True)

        assert_halted(state, "clean halt after safety gatekeeper test")
        self.details = {
            "locked_cmds_rejected": rejected_count,
        }


class TestSimSafetyInterlockAndWatchdogHalt(HilTestCase):
    name = "sim_safety_interlock_and_watchdog_halt"
    description = "Verify Core 1 sim watchdog disengages feed and zeroes RPM when heartbeats cease for >1.5s"

    async def run(self) -> None:
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
            timeout_s=2.5,
            description="watchdog failsafe halt and zero RPM",
        )
        elapsed_ms = (time.perf_counter() - t_start) * 1000.0

        assert not state.is_on, "Controller is still in motion after watchdog timeout!"
        assert state.rpm == 0, f"Simulated RPM not zeroed after watchdog timeout: {state.rpm}"

        self.details = {
            "timeout_trip_ms": round(elapsed_ms, 2),
            "watchdog_tripped": True,
            "final_rpm": state.rpm,
        }
