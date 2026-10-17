"""Web UI Axis Zero and Coordinate Preset integration tests."""

from __future__ import annotations

from core.assertions import assert_pos_du
from core.base_test import HilTestCase


class TestAxisZeroAndPreset(HilTestCase):
    name = "axis_zero_and_preset"
    description = "Verify CMD.axisPosDu presets coordinate and CMD.axisZero resets coordinate to 0"

    async def run(self) -> None:
        # 1. Preset Z axis to +25.000 mm (250,000 du)
        preset_z = 250000
        await self.client.send(f"CMD.axisPosDu=Z,{preset_z}")
        state = await self.client.wait_for(
            lambda s: s.z_du == preset_z,
            timeout_s=3.0,
            description="Z preset applied",
        )
        assert_pos_du(state.z_du, preset_z, tolerance_du=0, axis="Z")

        # 2. Zero Z axis
        await self.client.send("CMD.axisZero=Z")
        state = await self.client.wait_for(
            lambda s: s.z_du == 0,
            timeout_s=3.0,
            description="Z zeroed",
        )
        assert_pos_du(state.z_du, 0, tolerance_du=0, axis="Z")

        # 3. Preset X axis to +10.000 mm (100,000 du)
        preset_x = 100000
        await self.client.send(f"CMD.axisPosDu=X,{preset_x}")
        state = await self.client.wait_for(
            lambda s: s.x_du == preset_x,
            timeout_s=3.0,
            description="X preset applied",
        )
        assert_pos_du(state.x_du, preset_x, tolerance_du=0, axis="X")

        # 4. Zero X axis
        await self.client.send("CMD.axisZero=X")
        state = await self.client.wait_for(
            lambda s: s.x_du == 0,
            timeout_s=3.0,
            description="X zeroed",
        )
        assert_pos_du(state.x_du, 0, tolerance_du=0, axis="X")

        # 5. Settle Window: confirm both axes stay rock-solid at zero
        import asyncio
        await asyncio.sleep(0.3)
        assert self.client.state.z_du == 0, f"Z drifted after zero: {self.client.state.z_du}"
        assert self.client.state.x_du == 0, f"X drifted after zero: {self.client.state.x_du}"

        self.details = {
            "tested_z_preset": preset_z,
            "tested_x_preset": preset_x,
            "settle_window_ms": 300,
        }
