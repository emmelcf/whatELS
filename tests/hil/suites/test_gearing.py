"""Spindle-synchronized gearing and pitch accuracy test cases."""

from __future__ import annotations

from core.assertions import assert_pos_du
from core.base_test import HilTestCase


class TestSpindleSyncGearing1_5mm(HilTestCase):
    name = "gearing_sync_1_5mm_pitch"
    description = "Verify 1.5mm pitch travels exactly -30,000 du over 2 spindle revs"

    async def run(self) -> None:
        # 1. Engage Z-axis gearbox feed towards chuck at 1.500 mm (15,000 du)
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK")
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged")

        # 2. Inject 2 complete revolutions of spindle encoder pulses (4x quadrature counts)
        counts_per_rev = (self.client.state.encoder_ppr * 4) if self.client.state.encoder_ppr > 0 else 4800
        two_revs = counts_per_rev * 2
        await self.client.send(f"CMD.simPulses={two_revs}")

        # 3. Wait for carriage coordinate to reach target position
        expected_z = -30000
        state = await self.client.wait_for(
            lambda s: abs(s.z_du - expected_z) <= 5,
            timeout_s=5.0,
            description="carriage reached -30,000 du",
        )

        # 4. Assert accuracy within +/- 2 du
        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")
        await self.client.send("CMD.stop")
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)
        self.details = {"expected_z_du": expected_z, "actual_z_du": state.z_du, "error_du": abs(state.z_du - expected_z)}


class TestSpindleSyncGearingReverse(HilTestCase):
    name = "gearing_sync_reverse_1_0mm"
    description = "Verify 1.0mm pitch travels +10,000 du towards tailstock over 1 spindle rev"

    async def run(self) -> None:
        # 1. Engage Z-axis gearbox feed towards tailstock at 1.000 mm (10,000 du)
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL")
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged")

        # 2. Inject 1 revolution (4x quadrature counts)
        counts_per_rev = (self.client.state.encoder_ppr * 4) if self.client.state.encoder_ppr > 0 else 4800
        await self.client.send(f"CMD.simPulses={counts_per_rev}")

        # 3. Wait for coordinate
        expected_z = 10000
        state = await self.client.wait_for(
            lambda s: abs(s.z_du - expected_z) <= 5,
            timeout_s=5.0,
            description="carriage reached +10,000 du",
        )

        # 4. Assert accuracy
        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")
        await self.client.send("CMD.stop")
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)
        self.details = {"expected_z_du": expected_z, "actual_z_du": state.z_du, "error_du": abs(state.z_du - expected_z)}
