"""Web UI Gearbox Mode end-to-end integration tests over WebSocket.

Tests both the primary production case (engaging motion while spindle is already
spinning at full cutting speed) and the cold-start edge case (arming motion prior
to spindle acceleration).
"""

from __future__ import annotations

import asyncio
from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase


class TestGearboxAtSpeedZFeed(HilTestCase):
    name = "gearbox_at_speed_z_feed"
    description = "Normal Case: Spindle stabilized at 600 RPM first, then Z feed engaged on-the-fly"

    async def run(self) -> None:
        # 1. Bring spindle all the way up to operating speed (600 RPM)
        target_rpm = 600
        await self.client.send(f"CMD.simRpm={target_rpm}")
        await self.client.wait_for(
            lambda s: s.rpm >= 580,
            timeout_s=3.0,
            description="spindle up to speed (600 RPM)",
        )

        # 2. Engage Z feed towards Chuck on-the-fly at 1.500 mm (15,000 du) pitch with 45,000 du target
        target_dist_du = 45000
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:{target_dist_du}")
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged at speed")

        # 3. Wait for controller to track at full speed and automatically halt at target distance
        expected_z = -target_dist_du
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=6.0,
            description="automatic halt at stroke target",
        )

        # 4. Anti-Creep Hold Window: Keep spindle spinning at full 600 RPM for 1.0 second post-halt
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 5. Shut down spindle
        await self.client.send("CMD.simRpm=0")
        await asyncio.sleep(0.3)

        # 6. Assert exact position with zero creep under continuous high-speed rotation
        assert_halted(hold_state, "feed halted at target while spindle ran at 600 RPM")
        assert_pos_du(hold_state.z_du, expected_z, tolerance_du=2, axis="Z")
        self.details = {
            "spindle_rpm": target_rpm,
            "expected_z_du": expected_z,
            "actual_z_du": hold_state.z_du,
            "anti_creep_hold_ms": 1000,
            "creep_du": abs(hold_state.z_du - expected_z),
        }


class TestGearboxAtSpeedXFeed(HilTestCase):
    name = "gearbox_at_speed_x_feed"
    description = "Normal Case: Spindle stabilized at 600 RPM first, then X cross-feed engaged on-the-fly"

    async def run(self) -> None:
        # 1. Bring spindle all the way up to operating speed (600 RPM)
        target_rpm = 600
        await self.client.send(f"CMD.simRpm={target_rpm}")
        await self.client.wait_for(
            lambda s: s.rpm >= 580,
            timeout_s=3.0,
            description="spindle up to speed (600 RPM)",
        )

        # 2. Engage X cross-slide feed towards Center on-the-fly at 0.500 mm pitch with 15,000 du target
        target_dist_du = 15000
        await self.client.send(f"CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER,dist:{target_dist_du}")
        await self.client.wait_for(lambda s: s.is_on, description="X feed engaged at speed")

        # 3. Wait for auto-halt at target distance
        expected_x = -target_dist_du
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=6.0,
            description="automatic halt at X stroke target",
        )

        # 4. Anti-Creep Hold Window: Keep spindle spinning at 600 RPM for 1.0 second post-halt
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 5. Shut down spindle
        await self.client.send("CMD.simRpm=0")
        await asyncio.sleep(0.3)

        # 6. Assert exact position with zero creep
        assert_halted(hold_state, "X feed halted at target while spindle ran at 600 RPM")
        assert_pos_du(hold_state.x_du, expected_x, tolerance_du=2, axis="X")
        self.details = {
            "spindle_rpm": target_rpm,
            "expected_x_du": expected_x,
            "actual_x_du": hold_state.x_du,
            "anti_creep_hold_ms": 1000,
            "creep_du": abs(hold_state.x_du - expected_x),
        }


class TestGearboxColdStartRamp(HilTestCase):
    name = "gearbox_cold_start_ramp"
    description = "Edge Case: Motion armed while stationary (0 RPM), then spindle accelerates to 600 RPM"

    async def run(self) -> None:
        # 1. Arm Z feed while spindle is completely stationary (0 RPM)
        target_dist_du = 20000
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK,dist:{target_dist_du}")
        await self.client.wait_for(lambda s: s.is_on, description="feed armed at 0 RPM")

        # 2. Accelerate virtual spindle to 600 RPM
        await self.client.send("CMD.simRpm=600")

        # 3. Synchronizer must smoothly track through acceleration ramp to target distance
        expected_z = -target_dist_du
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=6.0,
            description="halted at stroke distance",
        )

        # 4. Hold at 600 RPM for 1.0s to verify anti-creep
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 5. Shut down spindle
        await self.client.send("CMD.simRpm=0")
        await asyncio.sleep(0.3)

        assert_halted(hold_state, "feed halted after acceleration ramp")
        assert_pos_du(hold_state.z_du, expected_z, tolerance_du=2, axis="Z")
        self.details = {
            "expected_z_du": expected_z,
            "actual_z_du": hold_state.z_du,
            "anti_creep_hold_ms": 1000,
        }


class TestGearboxStop(HilTestCase):
    name = "gearbox_stop_command"
    description = "Verify CMD.stop disengages active feed while spindle is running at full speed"

    async def run(self) -> None:
        # 1. Bring spindle to full speed first
        await self.client.send("CMD.simRpm=600")
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 2. Engage continuous Z feed towards Tailstock
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL")
        await self.client.wait_for(lambda s: s.is_on, description="feed active")

        # 3. Issue stop command while spindle continues spinning
        await self.client.send("CMD.stop")

        # 4. Wait for controller to register halt
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=1.0,
            description="feed stopped confirmation",
        )

        # 5. Settling Window (500 ms) while spindle is still turning at 600 RPM
        halted_z = state.z_du
        await asyncio.sleep(0.5)
        settled_z = self.client.state.z_du

        # 6. Shut down spindle
        await self.client.send("CMD.simRpm=0")
        await asyncio.sleep(0.3)

        assert_halted(self.client.state, "CMD.stop disengaged feed at speed")
        assert settled_z == halted_z, f"Carriage crept after stop! Halted: {halted_z}, Settled: {settled_z}"
        self.details = {"halted_z_du": halted_z, "settled_z_du": settled_z, "settle_window_ms": 500}
