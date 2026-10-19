"""Coordinated Dual-Axis Z+X Taper and Cone Mode integration tests.

Verifies [KIN-04] multi-axis synchronized and asynchronous motion across shallow tapers,
steep cones (2D Bresenham major-axis swap), and synchronized secondary-axis soft limit halts.
"""

from __future__ import annotations

import asyncio
from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase


class TestGearboxZXShallowTaperSync(HilTestCase):
    name = "gearbox_zx_shallow_taper_sync"
    description = "Verify [KIN-04] coordinated Z+X shallow taper cut (0.10 ratio) over 4 spindle revs"

    async def run(self) -> None:
        # 1. Set taper ratio = 0.10 (radial delta / axial delta)
        taper_ratio = 0.10
        await self.client.send(f"CMD.taperRatio={taper_ratio}", wait_ack=True)

        # 2. Start Gearbox feed on dual-axis ZX (Z lead = 10,000 dupr)
        await self.client.send("CMD.startFeed=axis:ZX,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="dual-axis feed active")

        # 3. Inject 4 spindle revolutions (Z lead = 10,000 dupr -> Z = -40,000 du, X = -4,000 du)
        pulses_4revs = self.client.revs_to_pulses(4.0)
        expected_z = -40000
        expected_x = -4000
        await self.client.send(f"CMD.simPulses={pulses_4revs}", wait_ack=True)

        state = await self.client.wait_for(
            lambda s: abs(s.z_du - expected_z) <= 2 and abs(s.x_du - expected_x) <= 2,
            timeout_s=6.0,
            description="both axes reached taper coordinates",
        )

        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")
        assert_pos_du(state.x_du, expected_x, tolerance_du=2, axis="X")

        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "taper_ratio": taper_ratio,
            "z_du": state.z_du,
            "x_du": state.x_du,
        }


class TestGearboxZXSteepConeMajorAxisSwap(HilTestCase):
    name = "gearbox_zx_steep_cone_major_axis_swap"
    description = "Verify steep cone (2.0 ratio) 2D Bresenham major-axis swap maintaining exact 1:2 ratio"

    async def run(self) -> None:
        cone_ratio = 2.0
        await self.client.send(f"CMD.taperRatio={cone_ratio}", wait_ack=True)

        # Start Gearbox feed on dual-axis ZX (Z lead = 5,000 dupr)
        await self.client.send("CMD.startFeed=axis:ZX,sync:1,feed:5000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="steep cone active")

        # 4 revolutions at 5,000 dupr Z lead:
        # Z advances by 20,000 du -> -20,000 du
        # X advances by 2.0 * 20,000 du -> -40,000 du
        pulses_4revs = self.client.revs_to_pulses(4.0)
        expected_z = -20000
        expected_x = -40000
        await self.client.send(f"CMD.simPulses={pulses_4revs}", wait_ack=True)

        state = await self.client.wait_for(
            lambda s: abs(s.z_du - expected_z) <= 2 and abs(s.x_du - expected_x) <= 2,
            timeout_s=6.0,
            description="steep cone reached coordinates",
        )

        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")
        assert_pos_du(state.x_du, expected_x, tolerance_du=2, axis="X")

        # Verify geometric ratio |X / Z| matches 2.0 within tolerance
        ratio_actual = abs(float(state.x_du) / float(state.z_du))
        assert abs(ratio_actual - 2.0) <= 0.001, f"Cone ratio diverged: {ratio_actual:.4f} vs 2.0"

        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "cone_ratio": cone_ratio,
            "z_du": state.z_du,
            "x_du": state.x_du,
            "ratio_actual": round(ratio_actual, 4),
        }


class TestGearboxZXAsyncTaperFeed(HilTestCase):
    name = "gearbox_zx_async_taper_feed"
    description = "Verify coordinated Z+X taper move in asynchronous velocity mode (sync:0, 0 RPM)"

    async def run(self) -> None:
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        taper_ratio = 0.25
        await self.client.send(f"CMD.taperRatio={taper_ratio}", wait_ack=True)

        # Asynchronous feed: Z travel = 20,000 du, X travel = 0.25 * 20,000 = 5,000 du
        expected_z = -20000
        expected_x = -5000
        await self.client.send("CMD.startFeed=axis:ZX,sync:0,feed:3000000,dir:CHUCK,dist:20000", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="async taper feed active")

        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=6.0, description="async taper completed")

        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")
        assert_pos_du(state.x_du, expected_x, tolerance_du=2, axis="X")
        assert state.rpm == 0

        self.details = {
            "taper_ratio": taper_ratio,
            "z_du": state.z_du,
            "x_du": state.x_du,
        }


class TestGearboxZXSecondaryAxisSoftLimitHalt(HilTestCase):
    name = "gearbox_zx_secondary_axis_soft_limit_halt"
    description = "Verify both axes halt simultaneously when cross-slide X soft limit is reached first"

    async def run(self) -> None:
        # Arm primary limit far away (-100,000 du), but secondary X limit close (-5,000 du)
        z_limit = -100000
        x_limit = -5000
        await self.client.send(f"CMD.axisLimitDu=Z,L,{z_limit}", wait_ack=True)
        await self.client.send(f"CMD.axisLimitDu=X,U,{x_limit}", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_left_du == z_limit and s.x_up_du == x_limit, timeout_s=3.0)

        # Taper ratio = 0.25 (when X moves 5,000 du, Z moves 20,000 du)
        taper_ratio = 0.25
        await self.client.send(f"CMD.taperRatio={taper_ratio}", wait_ack=True)

        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        # Start continuous taper cut
        await self.client.send("CMD.startFeed=axis:ZX,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="taper cut engaged")

        # X reaches -5,000 du first -> controller must disengage BOTH axes synchronously
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="secondary limit auto-halt")

        # Anti-creep check while spindle continues spinning at 600 RPM
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=X,U", wait_ack=True)

        assert_halted(hold_state, "both axes halted on secondary X soft limit")
        assert_pos_du(hold_state.x_du, x_limit, tolerance_du=2, axis="X")
        # Z should halt at proportional coordinate: -5000 / 0.25 = -20,000 du
        assert_pos_du(hold_state.z_du, -20000, tolerance_du=2, axis="Z")

        self.details = {
            "x_limit_du": x_limit,
            "halted_x_du": hold_state.x_du,
            "halted_z_du": hold_state.z_du,
        }
