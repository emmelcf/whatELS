"""Electronic Gearbox Mode end-to-end integration tests over WebSocket.

Tests single-axis Z and X feeds across synchronized, cold-start, stop-and-retract,
asynchronous velocity, in-cut live pitch nudge, imperial TPI, parameter staging,
spindle reversal, and pre-cut runway protection.
"""

from __future__ import annotations

import asyncio
from core.assertions import assert_halted, assert_pos_du
from core.base_test import HilTestCase
from core.client import CommandRejectedError


class TestGearboxAtSpeedZFeed(HilTestCase):
    name = "gearbox_at_speed_z_feed"
    description = "Normal Case: Spindle stabilized at 600 RPM first, then Z feed engaged on-the-fly"

    async def run(self) -> None:
        # 1. Bring spindle all the way up to operating speed (600 RPM)
        target_rpm = 600
        await self.client.send(f"CMD.simRpm={target_rpm}", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle at 600 RPM")

        # 2. Engage Z feed towards Chuck at 1.500 mm (15,000 du) pitch with 45,000 du target
        target_dist_du = 45000
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK,dist:{target_dist_du}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed engaged at speed")

        # 3. Wait for controller to auto-halt at target distance
        expected_z = -target_dist_du
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="auto-halt at target")

        # 4. Anti-Creep Hold Window: Keep spindle spinning at 600 RPM for 1.0 second post-halt
        await asyncio.sleep(1.0)
        hold_state = self.client.state

        # 5. Shut down spindle
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await asyncio.sleep(0.3)

        assert_halted(hold_state, "feed halted at target while spindle ran at 600 RPM")
        assert_pos_du(hold_state.z_du, expected_z, tolerance_du=2, axis="Z")
        self.details = {
            "spindle_rpm": target_rpm,
            "expected_z_du": expected_z,
            "actual_z_du": hold_state.z_du,
            "anti_creep_hold_ms": 1000,
            "creep_du": abs(hold_state.z_du - expected_z),
        }


class TestGearboxAtSpeedXFeed(HilTestCase):
    name = "gearbox_at_speed_x_feed"
    description = "Normal Case: Spindle stabilized at 600 RPM first, then X cross-feed engaged on-the-fly"

    async def run(self) -> None:
        target_rpm = 600
        await self.client.send(f"CMD.simRpm={target_rpm}", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, timeout_s=3.0, description="spindle at 600 RPM")

        target_dist_du = 15000
        await self.client.send(f"CMD.startFeed=axis:X,sync:1,feed:5000,dir:CENTER,dist:{target_dist_du}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="X feed engaged at speed")

        expected_x = -target_dist_du
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="auto-halt at X target")

        await asyncio.sleep(1.0)
        hold_state = self.client.state

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await asyncio.sleep(0.3)

        assert_halted(hold_state, "X feed halted at target while spindle ran at 600 RPM")
        assert_pos_du(hold_state.x_du, expected_x, tolerance_du=2, axis="X")
        assert_pos_du(hold_state.z_du, 0, tolerance_du=0, axis="Z")
        self.details = {
            "spindle_rpm": target_rpm,
            "expected_x_du": expected_x,
            "actual_x_du": hold_state.x_du,
            "anti_creep_hold_ms": 1000,
        }


class TestGearboxColdStartRamp(HilTestCase):
    name = "gearbox_cold_start_ramp"
    description = "Edge Case: Motion armed at 0 RPM, then stepped through multi-stage RPM ramp"

    async def run(self) -> None:
        target_dist_du = 20000
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK,dist:{target_dist_du}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed armed at 0 RPM")

        # Step through multi-stage inertia curve
        for rpm in [150, 300, 600, 300, 600]:
            await self.client.send(f"CMD.simRpm={rpm}", wait_ack=True)
            await asyncio.sleep(0.15)

        expected_z = -target_dist_du
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=8.0, description="halted at stroke distance")

        await asyncio.sleep(1.0)
        hold_state = self.client.state

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await asyncio.sleep(0.3)

        assert_halted(hold_state, "feed halted after acceleration ramp")
        assert_pos_du(hold_state.z_du, expected_z, tolerance_du=2, axis="Z")
        self.details = {
            "expected_z_du": expected_z,
            "actual_z_du": hold_state.z_du,
            "anti_creep_hold_ms": 1000,
        }


class TestGearboxStopCommand(HilTestCase):
    name = "gearbox_stop_command"
    description = "Verify feed hold mid-cut at 600 RPM with anti-creep hold, followed by reverse retract"

    async def run(self) -> None:
        await self.client.send("CMD.simRpm=600", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 580, description="spindle at 600 RPM")

        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on and s.z_du <= -5000, timeout_s=4.0, description="feed in progress")

        await self.client.send("CMD.stop", wait_ack=True)
        state = await self.client.wait_for(lambda s: not s.is_on, timeout_s=1.0, description="halt confirmation")

        # Settle / anti-creep check at 600 RPM
        halted_z = state.z_du
        await asyncio.sleep(0.5)
        settled_z = self.client.state.z_du
        assert settled_z == halted_z, f"Carriage crept after stop! Halted: {halted_z}, Settled: {settled_z}"

        # Reverse retract stroke
        retract_dist = abs(halted_z)
        await self.client.send(f"CMD.startFeed=axis:Z,sync:1,feed:10000,dir:TAIL,dist:{retract_dist}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="retract feed engaged")
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=6.0, description="retract completed")
        assert_pos_du(self.client.state.z_du, 0, tolerance_du=2, axis="Z")

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        self.details = {
            "halted_z_du": halted_z,
            "settled_z_du": settled_z,
        }


class TestGearboxAsyncVelocityFeedZX(HilTestCase):
    name = "gearbox_async_velocity_feed_z_and_x"
    description = "Verify asynchronous power feed on Z (600 mm/min) and X (300 mm/min) with spindle stopped (0 RPM)"

    async def run(self) -> None:
        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm == 0, timeout_s=2.0)

        # 1. Z Asynchronous Traverse: 600 mm/min (6,000,000 du/min) for 20.0 mm (200,000 du)
        z_target_dist = 200000
        t0 = asyncio.get_event_loop().time()
        await self.client.send(f"CMD.startFeed=axis:Z,sync:0,feed:6000000,dir:CHUCK,dist:{z_target_dist}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="Z async feed engaged")
        state_z = await self.client.wait_for(lambda s: not s.is_on, timeout_s=6.0, description="Z async feed completed")
        z_duration = asyncio.get_event_loop().time() - t0

        assert_pos_du(state_z.z_du, -z_target_dist, tolerance_du=2, axis="Z")
        assert state_z.rpm == 0, "Spindle RPM was unexpectedly non-zero during async feed"

        # 2. X Asynchronous Traverse: 300 mm/min (3,000,000 du/min) for 10.0 mm (100,000 du)
        x_target_dist = 100000
        t1 = asyncio.get_event_loop().time()
        await self.client.send(f"CMD.startFeed=axis:X,sync:0,feed:3000000,dir:CENTER,dist:{x_target_dist}", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="X async feed engaged")
        state_x = await self.client.wait_for(lambda s: not s.is_on, timeout_s=6.0, description="X async feed completed")
        x_duration = asyncio.get_event_loop().time() - t1

        assert_pos_du(state_x.x_du, -x_target_dist, tolerance_du=2, axis="X")
        assert state_x.rpm == 0

        self.details = {
            "z_duration_s": round(z_duration, 2),
            "x_duration_s": round(x_duration, 2),
            "final_z_du": state_z.z_du,
            "final_x_du": state_x.x_du,
        }


class TestGearboxLiveFeedNudgeAndStepCycle(HilTestCase):
    name = "gearbox_live_feed_nudge_and_step_cycle"
    description = "Verify [KIN-02] live feedrate trim and step cycle without position discontinuity"

    async def run(self) -> None:
        # Step increment = 500 du (0.050 mm)
        await self.client.send("CMD.feedStepDu=500", wait_ack=True)
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:10000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed active")

        # 1. Inject 5 spindle revs at 10,000 dupr -> reaches -50,000 du
        pulses_5revs = self.client.revs_to_pulses(5.0)
        await self.client.send(f"CMD.simPulses={pulses_5revs}", wait_ack=True)
        await self.client.wait_for(lambda s: abs(s.z_du - (-50000)) <= 2, timeout_s=6.0)

        # 2. Live Nudge twice (+500 du each time -> 11,000 dupr) and cycle step size
        await self.client.send("CMD.feedNudge=1", wait_ack=True)
        mid_z1 = self.client.state.z_du
        assert_pos_du(mid_z1, -50000, tolerance_du=2, axis="Z")

        await self.client.send("CMD.feedNudge=1", wait_ack=True)
        mid_z2 = self.client.state.z_du
        assert_pos_du(mid_z2, -50000, tolerance_du=2, axis="Z")

        await self.client.send("CMD.feedStepCycle", wait_ack=True)

        # 3. Inject second 5 spindle revs at new 11,000 dupr pitch -> advances -55,000 du to -105,000 du
        await self.client.send(f"CMD.simPulses={pulses_5revs}", wait_ack=True)
        state = await self.client.wait_for(lambda s: abs(s.z_du - (-105000)) <= 2, timeout_s=6.0)
        assert_pos_du(state.z_du, -105000, tolerance_du=2, axis="Z")

        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "mid_z_du": mid_z2,
            "final_z_du": state.z_du,
        }


class TestGearboxTpiImperialPitchConversion(HilTestCase):
    name = "gearbox_tpi_imperial_pitch_conversion"
    description = "Verify imperial 20 TPI pitch configuration (12,700 dupr) travels exactly 0.2000 in over 4 revs"

    async def run(self) -> None:
        await self.client.send("CMD.mode=gearbox", wait_ack=True)
        await self.client.send("CMD.gearAxis=Z", wait_ack=True)
        await self.client.send("CMD.gearSync=1", wait_ack=True)
        await self.client.send("CMD.gearDir=CHUCK", wait_ack=True)
        await self.client.send("CMD.feedTpi=20", wait_ack=True)
        await self.client.send("CMD.on=1", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="gearbox on")

        # 4 revolutions at 20 TPI (12,700 dupr) = 50,800 du
        pulses_4revs = self.client.revs_to_pulses(4.0)
        expected_z = -50800
        await self.client.send(f"CMD.simPulses={pulses_4revs}", wait_ack=True)
        state = await self.client.wait_for(lambda s: abs(s.z_du - expected_z) <= 2, timeout_s=5.0)

        assert_pos_du(state.z_du, expected_z, tolerance_du=2, axis="Z")

        await self.client.send("CMD.on=0", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "expected_z_du": expected_z,
            "actual_z_du": state.z_du,
            "dupr": state.dupr,
        }


class TestGearboxIngressStagingWorkflow(HilTestCase):
    name = "gearbox_ingress_staging_workflow"
    description = "Verify modal parameter staging (axis, sync, dir, feedMm) triggered via CMD.on=1 / CMD.on=0"

    async def run(self) -> None:
        await self.client.send("CMD.mode=gearbox", wait_ack=True)
        await self.client.send("CMD.gearAxis=X", wait_ack=True)
        await self.client.send("CMD.gearSync=1", wait_ack=True)
        await self.client.send("CMD.gearDir=RETRACT", wait_ack=True)
        await self.client.send("CMD.feedMm=0.25", wait_ack=True)
        await self.client.send("CMD.on=1", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="staged motion running")

        # 10 revs at 0.250 mm/rev (2,500 dupr) = +25,000 du
        pulses_10revs = self.client.revs_to_pulses(10.0)
        expected_x = 25000
        await self.client.send(f"CMD.simPulses={pulses_10revs}", wait_ack=True)
        state = await self.client.wait_for(lambda s: abs(s.x_du - expected_x) <= 2, timeout_s=6.0)

        assert_pos_du(state.x_du, expected_x, tolerance_du=2, axis="X")

        await self.client.send("CMD.on=0", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "expected_x_du": expected_x,
            "actual_x_du": state.x_du,
        }


class TestGearboxSpindleReversalAndShoulderPhaseTracking(HilTestCase):
    name = "gearbox_spindle_reversal_and_shoulder_phase_tracking"
    description = "Verify [KIN-01] spindle reversal under gearing lock (+10 revs cut, -10 revs retract back to 0 du)"

    async def run(self) -> None:
        await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:20000,dir:CHUCK", wait_ack=True)
        await self.client.wait_for(lambda s: s.is_on, description="feed active")

        # 1. Forward cut: +10 revolutions -> lands at -200,000 du
        pulses_10revs = self.client.revs_to_pulses(10.0)
        await self.client.send(f"CMD.simPulses={pulses_10revs}", wait_ack=True)
        state_fwd = await self.client.wait_for(lambda s: abs(s.z_du - (-200000)) <= 2, timeout_s=8.0)
        assert_pos_du(state_fwd.z_du, -200000, tolerance_du=2, axis="Z")

        # 2. Reverse spindle: -10 revolutions -> returns to exactly 0 du
        await self.client.send(f"CMD.simPulses={-pulses_10revs}", wait_ack=True)
        state_rev = await self.client.wait_for(lambda s: abs(s.z_du) <= 2, timeout_s=8.0)
        assert_pos_du(state_rev.z_du, 0, tolerance_du=2, axis="Z")

        await self.client.send("CMD.stop", wait_ack=True)
        await self.client.wait_for(lambda s: not s.is_on, timeout_s=2.0)

        self.details = {
            "fwd_z_du": state_fwd.z_du,
            "rev_z_du": state_rev.z_du,
        }


class TestGearboxRunwayTooShortGuard(HilTestCase):
    name = "gearbox_runway_too_short_guard"
    description = "Verify pre-cut runway verification clamps acceleration or rejects feed when limit is only 500 du away"

    async def run(self) -> None:
        limit_du = -500
        await self.client.send(f"CMD.axisLimitDu=Z,L,{limit_du}", wait_ack=True)
        await self.client.wait_for(lambda s: s.z_left_du == limit_du, timeout_s=3.0)

        await self.client.send("CMD.simRpm=1200", wait_ack=True)
        await self.client.wait_for(lambda s: s.rpm >= 1150, description="spindle screaming at 1200 RPM")

        # Engage feed towards chuck with limit only 500 du away
        rejected = False
        try:
            await self.client.send("CMD.startFeed=axis:Z,sync:1,feed:20000,dir:CHUCK", wait_ack=True)
        except CommandRejectedError:
            rejected = True

        if not rejected:
            # If accepted, controller must auto-halt without overrunning past -502 du
            await self.client.wait_for(lambda s: not s.is_on, timeout_s=3.0)
            assert self.client.state.z_du >= (limit_du - 2), (
                f"Carriage crashed past limit! Position: {self.client.state.z_du}, Limit: {limit_du}"
            )

        await self.client.send("CMD.simRpm=0", wait_ack=True)
        await self.client.send("CMD.axisClearLimit=Z,L", wait_ack=True)
        await self.client.send("CMD.axisZero=Z", wait_ack=True)

        self.details = {
            "limit_du": limit_du,
            "rejected_by_dispatcher": rejected,
            "min_z_du": self.client.state.z_du,
        }
