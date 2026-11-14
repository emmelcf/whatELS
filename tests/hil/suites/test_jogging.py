"""Manual Jogging integration tests (discrete step precision and continuous hold/limit enforcement)."""

from __future__ import annotations

import asyncio
from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase


class TestManualJogStepPrecision(HilTestCase):
    name = "manual_jog_step_precision"
    description = "Verify incremental touch-off jogging precision (0.1mm and 1.0mm) across all 4 Cartesian directions"

    async def run(self) -> None:
        # 1. 0.100 mm (1,000 du) incremental step jog on Z
        step_1000 = 1000
        await self.client.send(f"CMD.jogStep=Z,{step_1000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == step_1000,
            timeout_s=4.0,
            description="Z step jog +0.1mm to TAIL",
        )
        assert_pos_du(state.z_du, step_1000, tolerance_du=1, axis="Z")
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        await self.client.send(f"CMD.jogStep=Z,{-step_1000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == 0,
            timeout_s=4.0,
            description="Z step jog -0.1mm to CHUCK back to 0",
        )
        assert_pos_du(state.z_du, 0, tolerance_du=1, axis="Z")
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        # 2. 1.000 mm (10,000 du) incremental step jog on Z
        step_10000 = 10000
        await self.client.send(f"CMD.jogStep=Z,{step_10000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == step_10000,
            timeout_s=5.0,
            description="Z step jog +1.0mm to TAIL",
        )
        assert_pos_du(state.z_du, step_10000, tolerance_du=1, axis="Z")
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        await self.client.send(f"CMD.jogStep=Z,{-step_10000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == 0,
            timeout_s=5.0,
            description="Z step jog -1.0mm to CHUCK back to 0",
        )
        assert_pos_du(state.z_du, 0, tolerance_du=1, axis="Z")
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        # 3. 1.000 mm (10,000 du) incremental step jog on X
        await self.client.send(f"CMD.jogStep=X,{step_10000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.x_du == step_10000,
            timeout_s=5.0,
            description="X step jog +1.0mm to RETRACT",
        )
        assert_pos_du(state.x_du, step_10000, tolerance_du=1, axis="X")
        assert_pos_du(state.z_du, 0, tolerance_du=0, axis="Z")

        await self.client.send(f"CMD.jogStep=X,{-step_10000}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.x_du == 0,
            timeout_s=5.0,
            description="X step jog -1.0mm to CENTER back to 0",
        )
        assert_pos_du(state.x_du, 0, tolerance_du=1, axis="X")
        assert_pos_du(state.z_du, 0, tolerance_du=0, axis="Z")

        self.details = {
            "step_1000_ok": True,
            "step_10000_ok": True,
        }


class TestManualJogHoldAndLimits(HilTestCase):
    name = "manual_jog_hold_and_limits"
    description = "Verify continuous jog hold, anti-creep release, and automatic halt on active soft limits"

    async def run(self) -> None:
        # 1. Continuous jog Z toward TAIL
        await self.client.send("CMD.jogHold=Z,TAIL", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.z_du >= 5000,
            timeout_s=4.0,
            description="Z continuous jog started",
        )

        # 2. Release continuous jog and assert zero post-halt creep
        await self.client.send("CMD.jogRelease=Z", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=3.0,
            description="Z continuous jog released",
        )
        z_release = state.z_du
        assert z_release >= 5000, f"Expected Z >= 5000 after hold, got {z_release}"

        await asyncio.sleep(0.3)
        assert abs(self.client.state.z_du - z_release) <= 2, (
            f"Carriage crept after jogRelease: {self.client.state.z_du} vs {z_release}"
        )

        # 3. Arm Right soft limit at +20,000 du (+2.000 mm)
        limit_du = 20000
        await self.client.send(f"CMD.axisLimitDu=Z,R,{limit_du}", wait_ack=True)
        await self.client.wait_for(
            lambda s: s.z_right_du == limit_du,
            timeout_s=3.0,
            description="Right limit registered",
        )

        # 4. Hold continuous jog into the soft limit without releasing
        await self.client.send("CMD.jogHold=Z,TAIL", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, timeout_s=2.0, description="continuous jog active")
        state = await self.client.wait_for(
            lambda s: not s.is_on,
            timeout_s=6.0,
            description="automatic halt at soft limit during continuous jog",
        )
        assert_halted(state, "jogHold reached soft limit")
        assert_pos_du(state.z_du, limit_du, tolerance_du=2, axis="Z")

        # 5. Anti-creep hold check at limit
        await asyncio.sleep(0.3)
        assert_pos_du(self.client.state.z_du, limit_du, tolerance_du=2, axis="Z")

        # 6. Clean up
        await self.client.send("CMD.axisClearLimit=Z,R", wait_ack=True)
        await self.client.send("CMD.axisZero=Z", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_du == 0, timeout_s=3.0)

        self.details = {
            "limit_du": limit_du,
            "halted_z_du": state.z_du,
            "overshoot_du": max(0, state.z_du - limit_du),
        }
