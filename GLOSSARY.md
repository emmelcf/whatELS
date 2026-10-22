# whatELS Electronic Lead Screw

The ubiquitous domain language for whatELS: electronic leadscrew synchronization, multi-axis lathe motion, firmware safety, and dumb-client telemetry.

## Lathe Coordinates & Units

**Deci-micron (du)**:
The canonical metric radius unit of motion in controller firmware, equal to 0.0001 mm (0.1 µm).
_Avoid_: Step, pulse, micron, thou

**dupr**:
Deci-microns per spindle revolution, the canonical representation of thread pitch and carriage feed rate.
_Avoid_: Pitch value, feed per rev, mm/rev

**Z Carriage**:
The primary longitudinal axis moving parallel to the lathe spindle centerline (Z− toward headstock/chuck, Z+ toward tailstock).
_Avoid_: Bed axis, longitudinal slide, Z bed

**X Cross-Slide**:
The radial axis moving perpendicular to the lathe spindle centerline (X− into cut / center, X+ retract).
_Avoid_: Infeed slide, cross axis, transverse axis

**Diameter Mode**:
A client-local presentation setting that displays X cross-slide dimensions multiplied by 2.
_Avoid_: Diametral cut, 2x mode

**Radius Mode**:
The native coordinate representation where X values represent true tool distance from spindle centerline.
_Avoid_: 1x mode, radial coordinate

## Client Architecture & Boundaries (Disambiguation)

**Operator Client**:
An external user interface or test harness operating outside the controller MCU (such as the Touch DRO Web UI, a tablet, or the Python HIL suite).
_Avoid_: Core client, device client, client endpoint

**Core Client Endpoint**:
A controller-side session adapter running inside firmware (`src/core_client/`) that binds an inbound transport connection to the command dispatcher and serializes outgoing telemetry.
_Avoid_: Device client, web client, client app

**Native Protocol Endpoint**:
The zero-translation Core Client Endpoint that processes canonical JSON commands directly without G-code or external translation.
_Avoid_: Native client app, default client

**Alien Adapter**:
A Core Client Endpoint that translates foreign protocols (such as GRBL G-code streaming) into canonical controller commands.
_Avoid_: Shim, bridge, wrapper

**Client Transport (External)**:
The communications driver running within an Operator Client (e.g., browser WebSocket or WebSerial) that moves raw frames to and from the controller.
_Avoid_: Firmware transport, ITransport, controller transport

**Controller Transport**:
A firmware-side hardware or network driver (`ITransport`) managing physical byte streams (USB CDC, UART, WebSocket server) on the MCU.
_Avoid_: Client transport, web transport

**Dumb Client**:
An architectural invariant where the Operator Client contains zero motion kinematics, soft-limit calculations, or ratio math, functioning solely as a reactive intent-dispatcher and display.
_Avoid_: Thin client, smart UI

## Motion Kernel & Lathe Kinematics

**isOn**:
The authoritative binary motion execution state (`STOPPED` vs `RUNNING`) on the controller.
_Avoid_: Machine state, moving flag, run status

**Spindle Watchdog**:
The real-time safety mechanism monitoring spindle encoder pulses that halts or suspends infeed motion if spindle speed drops below the threshold.
_Avoid_: Stall detector, encoder timeout

**Virtual Gearbox**:
The deterministic Bresenham step generator synchronizing stepper pulses directly to spindle encoder ticks.
_Avoid_: Electronic gearing, pulse multiplier, software gear

**Hardware Limit Clamp**:
The real-time boundary safety in the motion kernel that immediately clamps motor pulses to zero upon reaching a hardware limit while allowing opposing motion.
_Avoid_: Soft limit, emergency stop, limit fault

**Anti-Trap Jog**:
The safety invariant ensuring an operator can always jog an axis away from a contacted limit switch even when forward travel is clamped.
_Avoid_: Limit override, jog recovery mode

## Operations & Canned Cycles

**Canned Cycle**:
An autonomous multi-pass machining sequence (such as threading, turning, or facing) executed entirely on the controller MCU.
_Avoid_: G-code cycle, auto routine, macro

**Pass**:
A single linear cutting traversal of the tool across the workpiece at a specific depth of cut.
_Avoid_: Cut, stroke, sweep

**Spring Pass**:
A final cutting pass at the same depth of cut as the previous pass to relieve tool deflection and achieve final dimension.
_Avoid_: Cleanup cut, zero pass, dry pass

**Infeed Angle**:
The compound angle (such as 29.5° or 30°) along which the cross-slide and carriage plunge into successive threading passes.
_Avoid_: Plunge angle, flank angle, compound feed

## Protocol & State Wire

**Canonical Command Packet**:
A structured JSON object transmitted to the controller representing explicit operator intent (`CMD.*`, `SETTINGS.*`).
_Avoid_: G-code line, raw packet, message

**CommandResult**:
The strongly typed domain response returned deterministically for every received command, reporting OK, FAILED, or UNRECOGNIZED.
_Avoid_: Ack, reply string, status code

**Domain Mask**:
A bitmask specifying which state domains (Kinematics, Inputs, Configuration, Presets) changed and need telemetry broadcast.
_Avoid_: Update flag, state dirty bit

**Telemetry Mailbox**:
The lock-free cross-core buffer used to safely transfer real-time motion snapshots from Core 1 ISR to Core 0 web/serial tasks.
_Avoid_: Shared memory, state queue

