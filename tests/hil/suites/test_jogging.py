"""Manual Jogging integration tests (step jog and continuous hold/release)."""

from __future__ import annotations

import asyncio
from core.assertions import assert_pos_du
from core.base_test import HilTestCase


class TestManualJogging(HilTestCase):
    name = "manual_jogging"
    description = "Verify CMD.jogStep (incremental) and CMD.jogHold / CMD.jogRelease (continuous)"

    async def run(self) -> None:
        # 1. Ensure any prior feed is disengaged and zero both axes
        if self.client.state.is_on:
            await self.client.send("CMD.stop")
            await self.client.wait_for(lambda s: not s.is_on, timeout_s=3.0)

        await self.client.send("CMD.axisZero=Z")
        await self.client.send("CMD.axisZero=X")
        await self.client.wait_for(lambda s: s.z_du == 0 and s.x_du == 0, timeout_s=3.0)

        # 2. Configure step distance to 1 mm (10,000 du)
        step_du = 10000
        await self.client.send(f"CMD.jogStepDu={step_du}")
        await asyncio.sleep(0.05)

        # 3. Test step jog Z toward TAIL (+Z)
        await self.client.send("CMD.jogStep=Z,TAIL")
        state = await self.client.wait_for(
            lambda s: s.z_du == step_du,
            timeout_s=6.0,
            description="Z jogged +5mm to TAIL",
        )
        assert_pos_du(state.z_du, step_du, tolerance_du=0, axis="Z")

        # 4. Test step jog Z back toward CHUCK (-Z)
        await self.client.send("CMD.jogStep=Z,CHUCK")
        state = await self.client.wait_for(
            lambda s: s.z_du == 0,
            timeout_s=6.0,
            description="Z jogged -5mm back to 0",
        )
        assert_pos_du(state.z_du, 0, tolerance_du=0, axis="Z")

        # 5. Test step jog X toward RETRACT (+X)
        await self.client.send("CMD.jogStep=X,RETRACT")
        state = await self.client.wait_for(
            lambda s: s.x_du == step_du,
            timeout_s=6.0,
            description="X jogged +5mm to RETRACT",
        )
        assert_pos_du(state.x_du, step_du, tolerance_du=0, axis="X")

        # 6. Test step jog X back toward INFEED (-X)
        await self.client.send("CMD.jogStep=X,INFEED")
        state = await self.client.wait_for(
            lambda s: s.x_du == 0,
            timeout_s=6.0,
            description="X jogged -5mm back to 0",
        )
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        # 7. Test continuous jog Z toward TAIL, then release
        await self.client.send("CMD.jogHold=Z,TAIL")
        await self.client.wait_for(
            lambda s: s.z_du >= 5000,
            timeout_s=3.0,
            description="Z continuous jog started",
        )
        await self.client.send("CMD.jogRelease=Z")
        await asyncio.sleep(0.2)
        pos_after_release = self.client.state.z_du
        assert pos_after_release >= 5000, f"Expected Z >= 5000 after hold, got {pos_after_release}"

        # Verify motion halted and held
        await asyncio.sleep(0.2)
        assert abs(self.client.state.z_du - pos_after_release) <= 100, "Z continued moving after jogRelease"

        # 8. Restore axis zeros
        await self.client.send("CMD.axisZero=Z")
        await self.client.send("CMD.axisZero=X")
        await self.client.wait_for(lambda s: s.z_du == 0 and s.x_du == 0, timeout_s=3.0)

        self.details = {
            "step_jog_du": step_du,
            "continuous_hold_reached_du": pos_after_release,
        }
