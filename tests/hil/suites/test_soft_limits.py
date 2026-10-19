"""Soft limit lookahead, boundary deceleration, and directional escape tests.

Tests boundary enforcement under full-speed spindle rotation across both Z and X axes,
ensuring zero overrun past virtual stops, zero post-halt creep, and safe directional escape.
"""

from __future__ import annotations

import asyncio
from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase


class TestSoftLimitAtSpeedChuckHalt(HilTestCase):
    name = "soft_limit_at_speed_chuck_halt"
    description = "Verify feed halts cleanly at -50,000 du Left limit when engaged under full 600 RPM rotation"

    async def run(self) -> None:
        # 1. Set Left limit at -50,000 du (-5.000 mm)
        limit_du = -50000
        await self.client.send(f"CMD.axisLimitDu=Z,L,{limit_du}", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.z_left_du == limit_du,
            timeout_s=3.0,
            description="left limit registered",
        )

        # 2. Bring spindle all the way up to 600 RPM first
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 3. Engage Z feed towards Chuck on-the-fly at speed
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged at speed")

        # 4. Wait for controller to automatically disengage on reaching limit
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=8.0,
            description="automatic halt at soft shoulder",
        )

        # 5. Anti-Creep Hold Window: keep spindle turning at 600 RPM for 1.0 second post-halt
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 6. Shut down spindle and clear limit
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        await asyncio.sleep(0.3)

        # 7. Assert carriage stopped cleanly at limit with zero overrun or creep
        assert_halted(hold_state, "soft limit shoulder reached while spindle ran at 600 RPM")
        assert_pos_du(hold_state.z_du, limit_du, tolerance_du=2, axis="Z")
        self.details = {
            "limit_du": limit_du,
            "halted_z_du": hold_state.z_du,
            "overshoot_du": max(0, -(hold_state.z_du - limit_du)),
            "anti_creep_hold_ms": 1000,
        }


class TestSoftLimitAtSpeedTailHalt(HilTestCase):
    name = "soft_limit_at_speed_tail_halt"
    description = "Verify feed halts cleanly at +50,000 du Right limit when engaged under full 600 RPM rotation"

    async def run(self) -> None:
        # 1. Set Right limit at +50,000 du (+5.000 mm)
        limit_du = 50000
        await self.client.send(f"CMD.axisLimitDu=Z,R,{limit_du}", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.z_right_du == limit_du,
            timeout_s=3.0,
            description="right limit registered",
        )

        # 2. Bring spindle to 600 RPM first
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 3. Engage Z feed towards Tailstock on-the-fly at speed
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged at speed")

        # 4. Wait for automatic halt at right soft stop
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=8.0,
            description="automatic halt at tailstock soft stop",
        )

        # 5. Anti-Creep Hold Window (1.0s at 600 RPM)
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 6. Shut down spindle and clear limit
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,R", wait_ack=True)
        await asyncio.sleep(0.3)

        # 7. Assert carriage stopped cleanly at limit
        assert_halted(hold_state, "right soft limit reached while spindle ran at 600 RPM")
        assert_pos_du(hold_state.z_du, limit_du, tolerance_du=2, axis="Z")
        self.details = {
            "limit_du": limit_du,
            "halted_z_du": hold_state.z_du,
            "overshoot_du": max(0, hold_state.z_du - limit_du),
            "anti_creep_hold_ms": 1000,
        }


class TestSoftLimitXAxisCenterAndRetract(HilTestCase):
    name = "soft_limit_x_axis_center_and_retract"
    description = "Verify Cross-Slide X center stop (-15,000 du) and retract stop (+15,000 du) at 600 RPM"

    async def run(self) -> None:
        # 1. Set X inward (Center) limit at -15,000 du and outward (Retract) limit at +15,000 du
        limit_inward = -15000
        limit_outward = 15000
        await self.client.send(f"CMD.axisLimitDu=X,U,{limit_inward}", wait_ack=True)
        await self.client.send(f"CMD.axisLimitDu=X,D,{limit_outward}", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.x_up_du == limit_inward and s.x_down_du == limit_outward,
            timeout_s=3.0,
            description="X soft limits registered",
        )

        # 2. Spin up spindle
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 3. Cut inward towards centerline
        await self.client.send("CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="X center feed engaged")
        state_in = await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="halt at center stop")
        await asyncio.sleep(0.5)
        assert_halted(state_in, "halted at X center limit")
        assert_pos_du(self.client.state.x_du, limit_inward, tolerance_du=2, axis="X")
        assert_pos_du(self.client.state.z_du, 0, tolerance_du=0, axis="Z")

        # 4. Retract outwards towards retract stop
        await self.client.send("CMD.startFeed=axis:X,sync:1,feed:5000,dir:RETRACT", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="X retract feed engaged")
        state_out = await self.client.wait_for(lambda s: not s.is_on, timeout_s=12.0, description="halt at retract stop")
        await asyncio.sleep(0.5)
        assert_halted(state_out, "halted at X retract limit")
        assert_pos_du(self.client.state.x_du, limit_outward, tolerance_du=2, axis="X")
        assert_pos_du(self.client.state.z_du, 0, tolerance_du=0, axis="Z")

        # 5. Clean up
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=X,U", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=X,D", wait_ack=True)
        await self.client.send("CMD.axisZero=X", wait_ack=True)

        self.details = {
            "up_halt": state_in.x_du,
            "down_halt": state_out.x_du,
        }


class TestSoftLimitAtLimitDirectionalLockAndEscape(HilTestCase):
    name = "soft_limit_at_limit_directional_lock_and_escape"
    description = "Verify [KIN-03] directional lockout when resting on limit, and reverse escape away from boundary"

    async def run(self) -> None:
        # 1. Set Left soft limit at -20,000 du (-2.000 mm)
        limit_du = -20000
        await self.client.send(f"CMD.axisLimitDu=Z,L,{limit_du}", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_left_du == limit_du, timeout_s=3.0)

        # 2. Feed into Left limit at 600 RPM
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=2.0, description="feed active")
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="halted at Left stop")
        assert_pos_du(self.client.state.z_du, limit_du, tolerance_du=2, axis="Z")

        # 3. Directional Lockout Check: Attempting to command feed or jog towards CHUCK must be blocked
        try:
            await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        except Exception:
            pass  # Rejection or no-op is expected
        await asyncio.sleep(0.3)
        assert self.client.state.z_du >= (limit_du - 2), (
            f"Overrun occurred past limit on re-feed! Current Z: {self.client.state.z_du}"
        )

        try:
            await self.client.send("CMD.jogStep=Z,CHUCK", wait_ack=True)
        except Exception:
            pass
        await asyncio.sleep(0.3)
        assert self.client.state.z_du >= (limit_du - 2), (
            f"Overrun occurred past limit on jog! Current Z: {self.client.state.z_du}"
        )

        # 4. Reverse Escape: Commanding feed away towards TAIL must immediately work
        escape_dist = 10000
        expected_z = limit_du + escape_dist  # -20000 + 10000 = -10000 du
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL,dist:{escape_dist}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="escape feed engaged")
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=6.0, description="escape feed completed")

        assert_pos_du(self.client.state.z_du, expected_z, tolerance_du=2, axis="Z")

        # 5. Clean up
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        await self.client.send("CMD.axisZero=Z", wait_ack=True)

        self.details = {
            "locked_at_du": limit_du,
            "escaped_du": self.client.state.z_du,
        }
