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

        # 2. Inject 2 complete revolutions of spindle encoder pulses
        two_revs = self.client.revs_to_pulses(2.0)
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

        # 2. Inject 1 revolution of spindle pulses
        one_rev = self.client.revs_to_pulses(1.0)
        await self.client.send(f"CMD.simPulses={one_rev}")

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


class TestSpindleSyncMultipassIndexSync(HilTestCase):
    name = "gearing_multipass_index_sync"
    description = "Verify spindle angle phase lock aligns within +/-1.0 deg across consecutive threading passes"

    async def run(self) -> None:
        # 1. Start continuous spindle at 600 RPM
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle stable at speed")

        # 2. Pass 1: engage feed towards chuck for 15.0 mm
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:15000", wait_ack=True)
        pass1_state = await self.client.wait_for(lambda s: s.is_on, timeout_s=2.0)
        pass1_angle = pass1_state.angle

        # Wait for Pass 1 stroke to complete
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=5.0, description="Pass 1 completed")
        assert_pos_du(self.client.state.z_du, -15000, tolerance_du=2, axis="Z")

        # 3. Reset Z position to 0 while spindle continues spinning
        await self.client.send("CMD.axisPosDu=Z,0", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_du == 0, timeout_s=2.0)

        # 4. Pass 2: re-engage feed
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:15000", wait_ack=True)
        pass2_state = await self.client.wait_for(lambda s: s.is_on, timeout_s=2.0)
        pass2_angle = pass2_state.angle

        # Wait for Pass 2 to complete
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=5.0, description="Pass 2 completed")
        assert_pos_du(self.client.state.z_du, -15000, tolerance_du=2, axis="Z")

        # Stop spindle
        await self.client.send("CMD.simRpm=0", wait_ack=True)

        # Compute phase difference modulo 360
        angle_diff = abs(pass1_angle - pass2_angle)
        if angle_diff > 180.0:
            angle_diff = 360.0 - angle_diff

        assert angle_diff <= 1.0, f"Spindle sync phase angle diverged between passes: {angle_diff:.2f} deg (max 1.0 deg)"
        self.details = {
            "pass1_angle": round(pass1_angle, 1),
            "pass2_angle": round(pass2_angle, 1),
            "angle_diff_deg": round(angle_diff, 2),
        }

