"""whatELS WebSocket Client and Reactive State Mirror."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
import time
from typing import Callable
import websockets


class CommandRejectedError(RuntimeError):
    """Raised when the controller replies with ERR:<reason>."""
    pass


@dataclass
class ControllerState:
    # Kinematics Domain (K.*)
    z_du: int = 0
    x_du: int = 0
    rpm: int = 0
    turns: float = 0.0
    angle: float = 0.0
    is_on: bool = False
    is_moving: bool = False
    mode: int = 0
    estop: int = 0
    perms: int = 0
    z_left_du: int | None = None
    z_right_du: int | None = None
    x_up_du: int | None = None
    x_down_du: int | None = None
    dupr: int = 0
    next_dupr: int = 0

    # Intent Domain (I.*)
    th_fmt: int = 0
    th_pitch: float = 0.0
    tp_mode: int = 0
    tp_val: float = 0.0
    direction: int = 0
    target_dist_du: int = -1

    # Properties Domain (P.*)
    screw_z_du: int = 20000
    motor_steps_z: int = 800
    encoder_ppr: int = 2400  # Effective quadrature counts per spindle revolution = encoder_ppr * 2

    # Raw key-value dictionary of all received wire lines and monotonic frame counter
    raw: dict[str, str] = field(default_factory=dict)
    telemetry_seq: int = 0


class WhatElsClient:
    """Asynchronous WebSocket client connecting to ws://<host>:81/canonical."""

    def __init__(self, host: str = "whatels.local", port: int = 81):
        # Normalize in case a full URI like 'ws://whatels.local:81' or 'http://...' was passed
        clean_host = host.replace("http://", "").replace("https://", "").replace("ws://", "").replace("wss://", "")
        if "/" in clean_host:
            clean_host = clean_host.split("/")[0]
        if ":" in clean_host:
            parts = clean_host.split(":")
            clean_host = parts[0]
            try:
                port = int(parts[1])
            except ValueError:
                pass

        self.host = clean_host
        self.port = port
        self.uri = f"ws://{clean_host}:{port}/canonical"
        self.state = ControllerState()
        self._ws = None
        self._rx_task: asyncio.Task | None = None
        self._heartbeat_task: asyncio.Task | None = None
        self._heartbeat_suppressed: bool = False
        self._listeners: list[Callable[[ControllerState], bool]] = []
        self._pending_acks: dict[str, list[asyncio.Future]] = {}

    async def connect(self, timeout_s: float = 5.0) -> None:
        """Connects to the controller WebSocket, starts receiver and background heartbeat."""
        try:
            self._ws = await asyncio.wait_for(
                websockets.connect(self.uri, ping_interval=None, ping_timeout=None), timeout=timeout_s
            )
        except Exception as e:
            raise ConnectionError(f"Failed to connect to whatELS at {self.uri}: {e}") from e

        self._rx_task = asyncio.create_task(self._rx_loop())
        self._heartbeat_task = asyncio.create_task(self._heartbeat_loop())
        try:
            await self.send(
                'CLIENT.hello=type:hil,ver:1.0.0,label:"HIL Test Harness"',
                wait_ack=True,
                timeout_s=min(timeout_s, 2.0),
            )
            # Request full domain state hydration and wait for first telemetry frame
            await self.send("STATE.get=ALL", wait_ack=False)
            await self.wait_for(
                lambda s: s.telemetry_seq > 0,
                timeout_s=timeout_s,
                description="initial hydration",
            )
        except Exception:
            await self.disconnect()
            raise

    async def disconnect(self) -> None:
        """Stops active feeds, clears simulation, cancels heartbeat and closes socket cleanly."""
        self._heartbeat_suppressed = True
        if self._heartbeat_task:
            self._heartbeat_task.cancel()
            try:
                await self._heartbeat_task
            except (asyncio.CancelledError, Exception):
                pass
            self._heartbeat_task = None

        try:
            if self._ws:
                await self.send("CMD.stop", wait_ack=False)
                await self.send("CMD.simRpm=0", wait_ack=False)
                await self.send("CMD.simEncoder=0", wait_ack=False)
        except Exception:
            pass

        if self._rx_task:
            self._rx_task.cancel()
            try:
                await self._rx_task
            except (asyncio.CancelledError, Exception):
                pass
            self._rx_task = None

        if self._ws:
            try:
                await self._ws.close()
            except Exception:
                pass
            self._ws = None

    def suppress_heartbeat(self, suppress: bool = True) -> None:
        """Enables or disables background 500ms heartbeat (used for watchdog timeout testing)."""
        self._heartbeat_suppressed = suppress

    async def send(self, cmd: str, wait_ack: bool = True, timeout_s: float = 2.0, expect_ack: bool | None = None) -> str:
        """Transmits a newline-delimited command frame.
        
        When wait_ack is True (or expect_ack is True) and cmd starts with CMD. or CLIENT.,
        awaits matching 'OK:CMD.<name>' or 'OK:CLIENT.<name>' response.
        Raises CommandRejectedError if firmware rejects with ERR:<reason>.
        """
        if expect_ack is not None:
            wait_ack = expect_ack

        if not self._ws:
            raise RuntimeError("Client is not connected")

        clean_cmd = cmd.strip()
        tag = None
        fut: asyncio.Future | None = None

        if wait_ack and (clean_cmd.startswith("CMD.") or clean_cmd.startswith("CLIENT.")):
            tag = clean_cmd.split("=", 1)[0].strip()

            loop = asyncio.get_running_loop()
            fut = loop.create_future()
            if tag not in self._pending_acks:
                self._pending_acks[tag] = []
            self._pending_acks[tag].append(fut)

        wire_msg = clean_cmd + "\n"
        await self._ws.send(wire_msg)

        if fut is not None and tag is not None:
            try:
                res = await asyncio.wait_for(fut, timeout=timeout_s)
                return str(res) if isinstance(res, str) else f"OK:{tag}"
            except asyncio.TimeoutError as exc:
                if tag in self._pending_acks and fut in self._pending_acks[tag]:
                    self._pending_acks[tag].remove(fut)
                raise TimeoutError(f"Timeout ({timeout_s}s) awaiting ACK for '{cmd}'") from exc
        return ""

    async def wait_next_telemetry(self, timeout_s: float = 1.0) -> ControllerState:
        """Waits for at least one fresh 50Hz telemetry broadcast frame to arrive."""
        start_seq = self.state.telemetry_seq
        return await self.wait_for(
            lambda s: s.telemetry_seq > start_seq,
            timeout_s=timeout_s,
            description="next telemetry frame",
            require_fresh_frame=False,
        )

    async def wait_for(
        self,
        predicate: Callable[[ControllerState], bool],
        timeout_s: float = 5.0,
        description: str = "condition",
        require_fresh_frame: bool = False,
    ) -> ControllerState:
        """Asynchronously waits until predicate(self.state) evaluates to True.

        If require_fresh_frame is True, waits for at least one new telemetry packet after invocation.
        Raises TimeoutError with diagnostic state snapshot if timeout expires.
        """
        if require_fresh_frame:
            await self.wait_next_telemetry(timeout_s=min(timeout_s, 0.5))

        # Fast path check
        if predicate(self.state):
            return self.state

        loop = asyncio.get_running_loop()
        future = loop.create_future()

        def check_condition(state: ControllerState) -> bool:
            if not future.done() and predicate(state):
                future.set_result(state)
                return True
            return False

        self._listeners.append(check_condition)
        try:
            return await asyncio.wait_for(future, timeout=timeout_s)
        except asyncio.TimeoutError as exc:
            state_summary = (
                f"zDu={self.state.z_du}, xDu={self.state.x_du}, rpm={self.state.rpm}, "
                f"isOn={self.state.is_on}, mode={self.state.mode}"
            )
            raise TimeoutError(
                f"Timeout ({timeout_s}s) waiting for '{description}'. Current state: [{state_summary}]"
            ) from exc
        finally:
            if check_condition in self._listeners:
                self._listeners.remove(check_condition)

    def revs_to_pulses(self, revs: float) -> int:
        """Computes exact quadrature pulse counts for `revs` revolutions from controller P.encoderPpr."""
        cpr = self.state.encoder_ppr * 2 if self.state.encoder_ppr > 0 else 4800
        return int(round(revs * cpr))

    async def inject_spindle_revs(self, revs: float, rpm: float = 600.0) -> None:
        """Calculates exact pulse counts for `revs` revolutions based on state.encoder_ppr * 2,
        injects them, and waits for command confirmation.
        """
        total_pulses = self.revs_to_pulses(revs)
        await self.send(f"CMD.simPulses={total_pulses}", wait_ack=True)

    async def _heartbeat_loop(self) -> None:
        """Background task transmitting CMD.ping every 500ms to keep Core 1 sim watchdog fed."""
        try:
            while True:
                await asyncio.sleep(0.5)
                if not self._heartbeat_suppressed and self._ws:
                    try:
                        await self._ws.send("CMD.ping\n")
                    except Exception:
                        break
        except asyncio.CancelledError:
            pass

    async def _rx_loop(self) -> None:
        """Continuously ingests WebSocket frames, splitting telemetry and resolving ACKs."""
        try:
            async for message in self._ws:
                if isinstance(message, bytes):
                    message = message.decode("utf-8", errors="replace")

                lines = message.split("\n")
                changed = False
                for raw_line in lines:
                    line = raw_line.strip()
                    if not line:
                        continue

                    # 1. Synchronous Command Acknowledgment Handling:
                    if line.startswith("OK:CMD.") or line.startswith("OK:CLIENT."):
                        payload = line[3:].strip()
                        cmd_key = payload.split(":", 1)[0].split("=", 1)[0].strip()
                        if cmd_key in self._pending_acks and self._pending_acks[cmd_key]:
                            fut = self._pending_acks[cmd_key].pop(0)
                            if not fut.done():
                                fut.set_result(line)
                        continue

                    if line == "PONG":
                        if "CMD.ping" in self._pending_acks and self._pending_acks["CMD.ping"]:
                            fut = self._pending_acks["CMD.ping"].pop(0)
                            if not fut.done():
                                fut.set_result(line)
                        continue

                    if line.startswith("ERR:"):
                        err_msg = line[4:].strip()
                        for cmd_name, fut_list in self._pending_acks.items():
                            for fut in fut_list:
                                if not fut.done():
                                    fut.set_exception(CommandRejectedError(f"Firmware error: {err_msg}"))
                        self._pending_acks.clear()
                        continue

                    # 2. Asynchronous Domain Telemetry Ingestion:
                    if self._parse_telemetry_line(line):
                        changed = True

                if changed:
                    self.state.telemetry_seq += 1
                    # Notify active condition listeners
                    for check_fn in list(self._listeners):
                        check_fn(self.state)
        except asyncio.CancelledError:
            pass
        except Exception:
            pass

    def _parse_telemetry_line(self, line: str) -> bool:
        """Parses canonical K.*, I.*, C.*, P.* lines into self.state."""
        if "=" not in line:
            return False
        key, val = line.split("=", 1)
        key = key.strip()
        val = val.strip()
        self.state.raw[key] = val

        try:
            # Kinematics Domain
            if key == "K.zDu":
                self.state.z_du = int(val)
            elif key == "K.xDu":
                self.state.x_du = int(val)
            elif key == "K.rpm":
                self.state.rpm = int(val)
            elif key == "K.turns":
                self.state.turns = float(val)
            elif key == "K.angle":
                self.state.angle = float(val)
            elif key == "K.on":
                self.state.is_on = val == "1" or val.lower() == "true"
            elif key == "K.mode":
                self.state.mode = int(val)
            elif key == "K.estop":
                self.state.estop = int(val)
            elif key == "K.perms":
                self.state.perms = int(val, 0)
            elif key == "K.zLeftDu":
                self.state.z_left_du = None if val == "NONE" else int(val)
            elif key == "K.zRightDu":
                self.state.z_right_du = None if val == "NONE" else int(val)
            elif key == "K.xUpDu":
                self.state.x_up_du = None if val == "NONE" else int(val)
            elif key == "K.xDownDu":
                self.state.x_down_du = None if val == "NONE" else int(val)
            elif key == "K.dupr":
                self.state.dupr = int(val)
            elif key == "K.nextDupr":
                self.state.next_dupr = int(val)
            # Intent Domain
            elif key == "I.thFmt":
                self.state.th_fmt = int(val)
            elif key == "I.thPitch":
                self.state.th_pitch = float(val)
            elif key == "I.tpMode":
                self.state.tp_mode = int(val)
            elif key == "I.tpVal":
                self.state.tp_val = float(val)
            elif key == "I.direction":
                self.state.direction = int(val)
            elif key == "I.targetDistDu":
                self.state.target_dist_du = int(val)
            # Properties Domain
            elif key == "P.encoderPpr":
                self.state.encoder_ppr = int(val)
            elif key == "P.motorStepsZ":
                self.state.motor_steps_z = int(val)
            elif key == "P.screwZDu":
                self.state.screw_z_du = int(val)
            else:
                return False
            return True
        except ValueError:
            return False
