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

        # 5. Set Z Left limit at -50,000 du, then shift Z coordinate to +10,000 du
        limit_z = -50000
        await self.client.send(f"CMD.axisLimitDu=Z,L,{limit_z}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_left_du == limit_z,
            timeout_s=3.0,
            description="Z Left limit registered",
        )
        assert state.z_left_du == limit_z, f"Z Left limit mismatch: {state.z_left_du}"

        shift_z = 10000
        await self.client.send(f"CMD.axisPosDu=Z,{shift_z}", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == shift_z,
            timeout_s=3.0,
            description="Z coordinate shifted with active limit",
        )
        assert_pos_du(state.z_du, shift_z, tolerance_du=0, axis="Z")
        assert state.z_left_du is not None, "Z Left limit was unexpectedly lost after coordinate shift"

        # 6. Zero Z, clear limit, wait 300 ms, and assert zero coordinate bounce
        await self.client.send("CMD.axisZero=Z", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        state = await self.client.wait_for(
            lambda s: s.z_du == 0 and s.z_left_du is None,
            timeout_s=3.0,
            description="Z zeroed and limit cleared",
        )
        assert_pos_du(state.z_du, 0, tolerance_du=0, axis="Z")

        import asyncio
        await asyncio.sleep(0.3)
        assert self.client.state.z_du == 0, f"Z drifted after settle window: {self.client.state.z_du}"
        assert self.client.state.x_du == 0, f"X drifted after settle window: {self.client.state.x_du}"

        self.details = {
            "tested_z_preset": preset_z,
            "tested_x_preset": preset_x,
            "shifted_z_du": shift_z,
            "soft_limit_tracked_du": limit_z,
            "settle_window_ms": 300,
        }
