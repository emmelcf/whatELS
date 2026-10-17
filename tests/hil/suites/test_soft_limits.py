"""Soft limit lookahead and shoulder deceleration halting tests.

Tests boundary enforcement under full-speed spindle rotation, ensuring zero
overrun past virtual shoulder stops and zero post-halt creep.
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
        await self.client.send(f"CMD.axisLimitDu=Z,1,{limit_du}")
        await self.client.wait_for(
            lambda s: s.z_left_du == limit_du,
            timeout_s=3.0,
            description="left limit registered",
        )

        # 2. Bring spindle all the way up to 600 RPM first
        await self.client.send("CMD.simRpm=600")
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 3. Engage Z feed towards Chuck on-the-fly at speed
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK")
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
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.axisClearLimit=Z,1")
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
        await self.client.send(f"CMD.axisLimitDu=Z,0,{limit_du}")
        await self.client.wait_for(
            lambda s: s.z_right_du == limit_du,
            timeout_s=3.0,
            description="right limit registered",
        )

        # 2. Bring spindle to 600 RPM first
        await self.client.send("CMD.simRpm=600")
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # 3. Engage Z feed towards Tailstock on-the-fly at speed
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL")
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
        await self.client.send("CMD.simRpm=0")
        await self.client.send("CMD.axisClearLimit=Z,0")
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
