/**
 * NanoELS H5 Controller Firmware Emulator
 * Emulates the ESP32 NanoELS H5 firmware protocol (docs/h5.ino)
 * Provides authentic bidirectional text-frame communication over a virtual transport.
 */
class NanoelsEmulator {
  constructor() {
    this.onmessage = null; // Callback: function(textData)
    this.timer = null;

    // Hardware State Constants (matches docs/h5.ino)
    this.ENCODER_STEPS = 1200;
    this.HARDWARE_VERSION = 5;
    this.SOFTWARE_VERSION = 38;

    // Controller State
    this.isOn = false;
    this.mode = 'GEAR';      // GEAR, XGEAR, TURN, FACE, CONE, CUT, SLOT, THREAD, ASYNC, JOY
    this.measure = 'MM';     // MM, IN, TPI
    this.dupr = 15000;       // Pitch in deci-microns (1.5mm = 15000 du)
    this.moveStep = 1000;    // Manual step in deci-microns (0.1mm = 1000 du)
    this.starts = 1;
    this.coneRatio = 0.0500;
    this.turnPasses = 5;
    this.currentPass = 1;
    this.isDiameterMode = true;

    // Spindle Simulation
    this.spindleRunning = false;
    this.rpm = 0;
    this.targetRpm = 640;
    this.spindlePos = 0;
    this.turns = 0;
    this.angle = 0;

    // Axis Kinematics (in deci-microns: 10,000 du = 1.000 mm)
    this.z = -184260;        // -18.426 mm
    this.x = 264380;         // 26.438 mm
    this.zLeft = -420000;    // -42.000 mm
    this.zRight = -40000;    // -4.000 mm
    this.xLeft = 238000;     // 23.800 mm
    this.xRight = null;      // No limit
    this.zOrigin = 0;
    this.xOrigin = 0;

    // Movement & Heartbeat Watchdog
    this.heldMoveAction = 0;
    this.heldMoveMillis = 0;
    this.moveTimeoutMs = 500; // Firmware timeout

    // Pass cycle progression
    this.cycleStage = 0;

    this.start();
  }

  start() {
    if (this.timer) return;
    this.timer = setInterval(() => this._tick(), 50);
  }

  stop() {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = null;
    }
  }

  // Inbound command from UI client
  send(text) {
    if (!text) return;
    const lines = text.split('\n');
    for (const raw of lines) {
      const line = raw.trim();
      if (!line) continue;
      this._handleLine(line);
    }
  }

  _handleLine(line) {
    // 1. Direct Queries
    if (line === '?') {
      this._emitGrblStatus();
      return;
    }
    if (line === '?sysinfo') {
      this._emitSysInfo();
      return;
    }
    if (line === '!') {
      this.isOn = false;
      return;
    }
    if (line === '~') {
      this.isOn = true;
      return;
    }

    // 2. WebUI Action Command: @<actionCode>:<state>
    if (line.startsWith('@')) {
      const sep = line.indexOf(':') !== -1 ? ':' : (line.indexOf('=') !== -1 ? '=' : ',');
      if (sep !== -1) {
        const actionCode = parseInt(line.substring(1, line.indexOf(sep)), 10);
        const stateToken = line.substring(line.indexOf(sep) + 1).trim().toLowerCase();
        const isPress = stateToken === '1' || stateToken === 'p' || stateToken === 'press' || stateToken === 'down';
        this._handleAction(actionCode, isPress);
      }
    }
  }

  _handleAction(actionCode, isPress) {
    const isMove = [21, 22, 23, 24, 85, 74].includes(actionCode);

    // Watchdog handling for continuous jogs
    if (isMove) {
      if (isPress) {
        this.heldMoveAction = actionCode;
        this.heldMoveMillis = Date.now();
      } else if (this.heldMoveAction === actionCode) {
        this.heldMoveAction = 0;
      }
      return;
    }

    // Actions executed on press
    if (!isPress) return;

    switch (actionCode) {
      case 30: // B_ON: Start cycle / feed engage
        this.isOn = true;
        this.spindleRunning = true;
        break;
      case 27: // B_OFF: Stop cycle / feed disengage
        this.isOn = false;
        break;
      case 64: // B_STEP: cycle 1mm -> 0.1mm -> 0.01mm
        if (this.moveStep === 10000) this.moveStep = 1000;
        else if (this.moveStep === 1000) this.moveStep = 100;
        else this.moveStep = 10000;
        break;
      case 77: // B_MEASURE: cycle MM -> IN -> TPI
        if (this.measure === 'MM') this.measure = 'IN';
        else if (this.measure === 'IN') this.measure = 'TPI';
        else this.measure = 'MM';
        break;
      case 82: // B_REVERSE: Reverse pitch sign
        this.dupr = -this.dupr;
        break;
      case 95: // B_PLUS
        this.dupr += 500;
        break;
      case 60: // B_MINUS
        this.dupr = Math.max(100, this.dupr - 500);
        break;
      case 90: // B_Z: Zero Z
        this.zOrigin += this.z;
        this.z = 0;
        break;
      case 88: // B_X: Zero X
        this.xOrigin += this.x;
        this.x = 0;
        break;
      case 65: // B_STOPL: Toggle Z Left Stop
        this.zLeft = this.zLeft !== null ? null : this.z;
        break;
      case 68: // B_STOPR: Toggle Z Right Stop
        this.zRight = this.zRight !== null ? null : this.z;
        break;
      case 87: // B_STOPU: Toggle X Forward / Inner Stop
        this.xLeft = this.xLeft !== null ? null : this.x;
        break;
      case 83: // B_STOPD: Toggle X Rear / Outer Stop
        this.xRight = this.xRight !== null ? null : this.x;
        break;
      case 97:  // B_MODE_GEARS
        this.mode = 'GEAR'; break;
      case 109: // B_MODE_XGEAR
        this.mode = 'XGEAR'; break;
      case 98:  // B_MODE_TURN
        this.mode = 'TURN'; break;
      case 99:  // B_MODE_FACE
        this.mode = 'FACE'; break;
      case 100: // B_MODE_CONE
        this.mode = 'CONE'; break;
      case 102: // B_MODE_THREAD
        this.mode = 'THREAD'; break;
      case 103: // B_MODE_ASYNC
        this.mode = 'ASYNC'; break;
      case 108: // B_MODE_JOYSTICK
        this.mode = 'JOY'; break;
      case 101: // B_MODE_CUT
        this.mode = 'CUT'; break;
      case 104: // B_MODE_ELLIPSE
        this.mode = 'ELLIP'; break;
      case 110: // B_MODE_SLOT
        this.mode = 'SLOT'; break;
      case 202: // B_DIA_RAD
        this.isDiameterMode = !this.isDiameterMode; break;
      case 203: // B_ABS_INC
        this.isAbsoluteMode = !this.isAbsoluteMode; break;
    }
  }

  _tick() {
    const now = Date.now();

    // 1. Motion Watchdog (500ms safety timeout)
    if (this.heldMoveAction !== 0 && now - this.heldMoveMillis > this.moveTimeoutMs) {
      this.heldMoveAction = 0;
      this._emitRaw("WEBUI.warning=move released after lost heartbeat\n");
    }

    // 2. Spindle Simulation
    if (this.spindleRunning || this.isOn) {
      this.rpm = this.targetRpm + Math.round((Math.random() - 0.5) * 15);
      const revsPerSec = this.rpm / 60;
      const revsDelta = revsPerSec * 0.050; // 50ms tick
      this.turns += revsDelta;
      this.angle = (this.angle + revsDelta * 360) % 360;
      this.spindlePos += revsDelta * this.ENCODER_STEPS;
    } else {
      this.rpm = 0;
    }

    // 3. Stepper Jogging Motion
    if (this.heldMoveAction !== 0) {
      const step = this.moveStep; // Step in deci-microns
      if (this.heldMoveAction === 21) { // Z- (Left / to chuck)
        this.z -= step;
        if (this.zLeft !== null && this.z < this.zLeft) this.z = this.zLeft;
      } else if (this.heldMoveAction === 22) { // Z+ (Right / to tailstock)
        this.z += step;
        if (this.zRight !== null && this.z > this.zRight) this.z = this.zRight;
      } else if (this.heldMoveAction === 23) { // X- (Up / to center)
        this.x -= step;
        if (this.xLeft !== null && this.x < this.xLeft) this.x = this.xLeft;
      } else if (this.heldMoveAction === 24) { // X+ (Down / retract)
        this.x += step;
        if (this.xRight !== null && this.x > this.xRight) this.x = this.xRight;
      }
    }

    // 4. Synchronized Feed Motion (When Controller ON)
    if (this.isOn && this.rpm > 0) {
      const revsDelta = (this.rpm / 60) * 0.050;
      const feedDu = this.dupr * revsDelta;

      if (this.mode === 'GEAR') {
        this.z -= feedDu;
        if (this.zLeft !== null && this.z <= this.zLeft) {
          this.z = this.zLeft;
          this.isOn = false; // Stop at limit
        }
      } else if (this.mode === 'XGEAR') {
        this.x -= feedDu;
        if (this.xLeft !== null && this.x <= this.xLeft) {
          this.x = this.xLeft;
          this.isOn = false;
        }
      } else if (this.mode === 'CONE') {
        this.z -= feedDu;
        this.x -= feedDu * this.coneRatio;
        if (this.zLeft !== null && this.z <= this.zLeft) {
          this.z = this.zLeft;
          this.isOn = false;
        }
      } else if (this.mode === 'THREAD') {
        this.z -= feedDu;
        if (this.zLeft !== null && this.z <= this.zLeft) {
          this.z = this.zLeft;
          this.isOn = false;
        }
      }
    }

    // 5. Broadcast Periodic Telemetry (every ~100ms)
    this._broadcastTelemetry();
  }

  _broadcastTelemetry() {
    const now = Date.now();
    if (this._lastBroadcastTime && now - this._lastBroadcastTime < 100) {
      return;
    }

    const divisor = this.measure === 'IN' ? 254000.0 : 10000.0;
    const zMm = (this.z / divisor).toFixed(3);
    const xMult = this.isDiameterMode ? 2.0 : 1.0;
    const xMm = ((this.x * xMult) / divisor).toFixed(3);
    const zLeftStr = this.zLeft !== null ? (Math.abs(this.z - this.zLeft) / divisor).toFixed(3) : '';
    const zRightStr = this.zRight !== null ? (Math.abs(this.zRight - this.z) / divisor).toFixed(3) : '';
    const xLeftStr = this.xLeft !== null ? (Math.abs(this.x - this.xLeft) / divisor).toFixed(3) : '';
    const xRightStr = this.xRight !== null ? (Math.abs(this.xRight - this.x) / divisor).toFixed(3) : '';
    const pitchStr = (Math.abs(this.dupr) / 10000.0).toFixed(3) + (this.starts > 1 ? ` x${this.starts}` : '');
    const stepStr = (this.moveStep / divisor).toFixed(3);
    const statusStr = this.isOn ? 'ON' : 'OFF';

    let msg = `Diameter ${(Math.abs(this.x * 2) / divisor).toFixed(2)}`;
    if (this.mode === 'TURN' && this.isOn) msg = `Pass ${this.currentPass} of ${this.turnPasses}`;

    const telemetry =
      `UI.status=${statusStr}\n` +
      `UI.mode=${this.mode}\n` +
      `UI.pitch=${pitchStr}\n` +
      `UI.measure=${this.measure}\n` +
      `UI.step=${stepStr}\n` +
      `UI.rpm=${this.rpm}\n` +
      `UI.turns=${this.turns.toFixed(1)}\n` +
      `UI.angle=${this.angle.toFixed(1)}\n` +
      `UI.x=${xMm}\n` +
      `UI.diameter=${this.isDiameterMode ? '1' : '0'}\n` +
      `UI.xLeft=${xLeftStr}\n` +
      `UI.xRight=${xRightStr}\n` +
      `UI.z=${zMm}\n` +
      `UI.zLeft=${zLeftStr}\n` +
      `UI.zRight=${zRightStr}\n` +
      `UI.message=${msg}\n`;

    if (telemetry !== this._lastTelemetryStr) {
      this._lastTelemetryStr = telemetry;
      this._lastBroadcastTime = now;
      this._emitRaw(telemetry);
    }
  }

  _emitSysInfo() {
    const sysinfo =
      `SYSINFO.version=H${this.HARDWARE_VERSION}V${this.SOFTWARE_VERSION} (EMU)\n` +
      `SYSINFO.chip=ESP32-S3 (Virtual)\n` +
      `SYSINFO.flash=8MB\n` +
      `SYSINFO.heap=240KB\n`;
    this._emitRaw(sysinfo);
  }

  _emitGrblStatus() {
    const divisor = this.measure === 'IN' ? 254000.0 : 10000.0;
    const x = (this.x / divisor).toFixed(3);
    const z = (this.z / divisor).toFixed(3);
    const state = this.isOn ? 'Run' : 'Idle';
    this._emitRaw(`<${state}|WPos:${x},0.000,${z}|FS:0,${this.rpm}|Id:H${this.HARDWARE_VERSION}V${this.SOFTWARE_VERSION}>\n`);
  }

  _emitRaw(data) {
    if (typeof this.onmessage === 'function') {
      this.onmessage({ data });
    }
  }
}

window.NanoelsEmulator = NanoelsEmulator;
