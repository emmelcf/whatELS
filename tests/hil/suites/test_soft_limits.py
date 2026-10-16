"""Soft limit lookahead and shoulder deceleration halting tests."""

from __future__ import annotations

from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase


class TestSoftLimitShoulderHalt(HilTestCase):
    name = "soft_limit_shoulder_halt"
    description = "Verify feed halts cleanly at -50,000 du soft limit during continuous spindle rotation"

    async def run(self) -> None:
        # 1. Set Left limit at -50,000 du (-5.000 mm)
        limit_du = -50000
        await self.client.send(f"CMD.axisLimitDu=Z,1,{limit_du}")
        await self.client.wait_for(
            lambda s: s.z_left_du == limit_du,
            timeout_s=3.0,
            description="left limit registered",
        )

        # 2. Engage Z feed towards chuck at 1.000 mm (10,000 du)
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK")
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged")

        # 3. Spin virtual spindle continuously at 600 RPM
        await self.client.send("CMD.simRpm=600")

        # 4. Wait for controller to automatically disengage on reaching limit
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=8.0,
            description="automatic halt at soft shoulder",
        )

        # 5. Assert carriage stopped cleanly at limit
        assert_halted(state, "soft limit shoulder reached")
        assert_pos_du(state.z_du, limit_du, tolerance_du=2, axis="Z")
        self.details = {"limit_du": limit_du, "halted_z_du": state.z_du, "overshoot_du": abs(state.z_du - limit_du)}
