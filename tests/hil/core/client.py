"""NanoELS WebSocket Client and Reactive State Mirror."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
import time
from typing import Callable
import websockets


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

    # Properties Domain (P.*)
    screw_z_du: int = 40000
    motor_steps_z: int = 800
    encoder_ppr: int = 2400

    # Raw key-value dictionary of all received wire lines
    raw: dict[str, str] = field(default_factory=dict)


class NanoElsClient:
    """Asynchronous WebSocket client connecting to ws://<host>:81/canonical."""

    def __init__(self, host: str = "nanoels.local", port: int = 81):
        # Normalize in case a full URI like 'ws://nanoels.local:81' or 'http://...' was passed
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
        self._listeners: list[asyncio.Future] = []

    async def connect(self, timeout_s: float = 5.0) -> None:
        """Connects to the controller WebSocket and starts the receiver loop."""
        try:
            self._ws = await asyncio.wait_for(
                websockets.connect(self.uri), timeout=timeout_s
            )
        except Exception as e:
            raise ConnectionError(f"Failed to connect to NanoELS at {self.uri}: {e}") from e

        self._rx_task = asyncio.create_task(self._rx_loop())
        # Request sysinfo to ensure line is open and hydrated
        await self.send("?sysinfo")

    async def disconnect(self) -> None:
        """Stops active feeds and closes socket cleanly."""
        try:
            if self._ws:
                await self.send("CMD.stop")
                await self.send("CMD.simRpm=0")
                await self.send("CMD.simEncoder=0")
        except Exception:
            pass

        if self._rx_task:
            self._rx_task.cancel()
            try:
                await self._rx_task
            except asyncio.CancelledError:
                pass
            self._rx_task = None

        if self._ws:
            await self._ws.close()
            self._ws = None

    async def send(self, cmd: str) -> None:
        """Transmits a newline-delimited command frame."""
        if not self._ws:
            raise RuntimeError("Client is not connected")
        clean = cmd.strip() + "\n"
        await self._ws.send(clean)

    async def wait_for(
        self,
        predicate: Callable[[ControllerState], bool],
        timeout_s: float = 5.0,
        description: str = "condition",
    ) -> ControllerState:
        """Asynchronously waits until predicate(self.state) evaluates to True.

        Raises TimeoutError with diagnostic state snapshot if timeout expires.
        """
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

    async def inject_spindle_revs(self, revs: float, rpm: float = 600.0) -> None:
        """Calculates exact pulse counts for `revs` revolutions based on state.encoder_ppr (4x quadrature),

        injects them, and waits for completion.
        """
        ppr = self.state.encoder_ppr if self.state.encoder_ppr > 0 else 1200
        total_pulses = int(round(revs * ppr * 4))
        await self.send(f"CMD.simPulses={total_pulses}")

    async def _rx_loop(self) -> None:
        """Continuously ingests WebSocket frames and splits batched newline-delimited lines."""
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
                    if self._parse_telemetry_line(line):
                        changed = True

                if changed:
                    # Notify active condition listeners
                    for check_fn in list(self._listeners):
                        if check_fn(self.state):
                            # Future resolved inside check_fn
                            pass
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
            elif key == "K.zLeftDu":
                self.state.z_left_du = None if val == "undefined" else int(val)
            elif key == "K.zRightDu":
                self.state.z_right_du = None if val == "undefined" else int(val)
            elif key == "K.xUpDu":
                self.state.x_up_du = None if val == "undefined" else int(val)
            elif key == "K.xDownDu":
                self.state.x_down_du = None if val == "undefined" else int(val)
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
