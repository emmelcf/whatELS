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
    this.moveStep = 10000;   // Jog step in deci-microns (1.0mm = 10000 du)
    this.feedStepDu = 500;   // Feed step in deci-microns (0.05mm/rev = 500 du)
    this.starts = 1;
    this.thFmt = 0;          // 0: Metric, 1: TPI, 2: Inch
    this.thPitch = 1.5;      // Author value
    this.tpMode = 0;         // 0: Ratio, 1: Angle, 2: Preset
    this.tpVal = 20.0;       // Author value (e.g. 1:20)
    this.tpPreset = 0;
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
    if (line.startsWith('?get') || line === '?state' || line.startsWith('CMD.get=')) {
      this._emitDomainQuery(line);
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

    // 2. Canonical CMD.* Handlers
    if (line.startsWith('CMD.')) {
      const cmd = line.substring(4);
      if (cmd.startsWith('get=')) {
        this._emitDomainQuery(cmd);
        return;
      }
      if (cmd.startsWith('feedDupr=')) {
        this.dupr = parseInt(cmd.substring(9), 10) || 0;
      } else if (cmd.startsWith('feedMm=')) {
        this.thPitch = parseFloat(cmd.substring(7)) || 0;
        this.thFmt = 0;
        this.dupr = Math.round(this.thPitch * 10000);
      } else if (cmd.startsWith('feedTpi=')) {
        this.thPitch = parseFloat(cmd.substring(8)) || 0;
        this.thFmt = 1;
        this.dupr = this.thPitch > 0 ? Math.round(254000.0 / this.thPitch) : 0;
      } else if (cmd.startsWith('feedInch=')) {
        this.thPitch = parseFloat(cmd.substring(9)) || 0;
        this.thFmt = 2;
        this.dupr = Math.round(this.thPitch * 254000.0);
      } else if (cmd === 'feedRev') {
        this.dupr = -this.dupr;
      } else if (cmd.startsWith('feedStepDu=')) {
        this.feedStepDu = parseInt(cmd.substring(11), 10) || 500;
      } else if (cmd === 'feedStepCycle') {
        if (this.feedStepDu === 100) this.feedStepDu = 500;
        else if (this.feedStepDu === 500) this.feedStepDu = 1000;
        else if (this.feedStepDu === 1000) this.feedStepDu = 2000;
        else this.feedStepDu = 100;
      } else if (cmd.startsWith('jogStepDu=')) {
        this.moveStep = parseInt(cmd.substring(10), 10) || 10000;
      } else if (cmd.startsWith('jogHold=')) {
        const parts = cmd.substring(8).split(',');
        const ax = parts[0].toUpperCase();
        const dir = (parts[1] || '').toUpperCase();
        if (ax === 'Z') {
          this.heldMoveAction = (dir === 'CHUCK' || dir === 'LEFT') ? 21 : 22;
        } else if (ax === 'X') {
          this.heldMoveAction = (dir === 'INFEED' || dir === 'IN' || dir === 'UP') ? 23 : 24;
        }
        this.heldMoveMillis = Date.now();
      } else if (cmd.startsWith('jogRelease=')) {
        this.heldMoveAction = 0;
      } else if (cmd.startsWith('jogStep=')) {
        const parts = cmd.substring(8).split(',');
        const ax = parts[0].toUpperCase();
        const dir = (parts[1] || '').toUpperCase();
        const step = this.moveStep || 10000;
        if (ax === 'Z') {
          this.z += (dir === 'CHUCK' || dir === 'LEFT') ? -step : step;
        } else if (ax === 'X') {
          this.x += (dir === 'INFEED' || dir === 'IN' || dir === 'UP') ? -step : step;
        }
      } else if (cmd.startsWith('taperRatio=')) {
        this.tpVal = parseFloat(cmd.substring(11)) || 1.0;
        this.tpMode = 0;
        this.tpPreset = -1;
        this.coneRatio = 1.0 / this.tpVal;
      } else if (cmd.startsWith('taperAngle=')) {
        this.tpVal = parseFloat(cmd.substring(11)) || 0.0;
        this.tpMode = 1;
        this.tpPreset = -1;
        this.coneRatio = 2.0 * Math.tan(this.tpVal * (Math.PI / 180.0));
      } else if (cmd.startsWith('taperPreset=')) {
        const parts = cmd.substring(12).split(',');
        this.tpPreset = parseInt(parts[0], 10) || 0;
        this.tpMode = 2;
        const deg = parseFloat(parts[1]) || 0.0;
        const ratio = parseFloat(parts[2]) || 0.0;
        this.tpVal = ratio > 0 ? ratio : deg;
        this.coneRatio = ratio > 0 ? (1.0 / ratio) : (2.0 * Math.tan(deg * (Math.PI / 180.0)));
      } else if (cmd.startsWith('axisZero=')) {
        const ax = cmd.substring(9).toUpperCase();
        if (ax === 'Z') { this.zOrigin += this.z; this.z = 0; }
        if (ax === 'X') { this.xOrigin += this.x; this.x = 0; }
      } else if (cmd.startsWith('axisPosDu=')) {
        const parts = cmd.substring(10).split(',');
        const ax = parts[0].toUpperCase();
        const du = parseInt(parts[1], 10) || 0;
        if (ax === 'Z') this.z = du;
        if (ax === 'X') this.x = du;
      } else if (cmd.startsWith('axisLimitDu=')) {
        const parts = cmd.substring(12).split(',');
        const ax = parts[0].toUpperCase();
        const isLeft = parts[1] === '1';
        const du = parseInt(parts[2], 10);
        if (ax === 'Z') { if (isLeft) this.zLeft = du; else this.zRight = du; }
        if (ax === 'X') { if (isLeft) this.xLeft = du; else this.xRight = du; }
      } else if (cmd.startsWith('axisClearLimit=')) {
        const parts = cmd.substring(15).split(',');
        const ax = parts[0].toUpperCase();
        const isLeft = parts[1] === '1';
        if (ax === 'Z') { if (isLeft) this.zLeft = null; else this.zRight = null; }
        if (ax === 'X') { if (isLeft) this.xLeft = null; else this.xRight = null; }
      } else if (cmd.startsWith('mode=')) {
        const m = parseInt(cmd.substring(5), 10);
        const modeMap = ['GEAR', 'XGEAR', 'ASYNC', 'CONE', 'TURN', 'FACE', 'CUT', 'THREAD', 'ELLIPSE', 'GCODE', 'Y', 'JOYSTICK', 'SLOT'];
        if (modeMap[m]) this.mode = modeMap[m];
      } else if (cmd.startsWith('on=')) {
        this.isOn = cmd.substring(3) === '1';
        if (this.isOn) this.spindleRunning = true;
      }
      this._lastTelemetryStr = '';
      return;
    }

    // 2. Direct Pitch Commands: @pitch:<val> or @dupr:<val>
    if (line.startsWith('@pitch:') || line.startsWith('@pitch=')) {
      const valStr = line.substring(7).trim();
      const hasSign = valStr.startsWith('-') || valStr.startsWith('+');
      const val = parseFloat(valStr);
      if (!isNaN(val) && Math.abs(val) > 0.00001) {
        let du = Math.round(Math.abs(val) * 10000);
        if (hasSign) {
          if (val < 0) du = -du;
        } else if (this.dupr < 0) {
          du = -du;
        }
        this.dupr = du;
        this._lastTelemetryStr = '';
      }
      return;
    }
    if (line.startsWith('@dupr:') || line.startsWith('@dupr=')) {
      const valStr = line.substring(6).trim();
      const hasSign = valStr.startsWith('-') || valStr.startsWith('+');
      const val = parseInt(valStr, 10);
      if (!isNaN(val) && Math.abs(val) > 0) {
        let du = Math.abs(val);
        if (hasSign) {
          if (val < 0) du = -du;
        } else if (this.dupr < 0) {
          du = -du;
        }
        this.dupr = du;
        this._lastTelemetryStr = '';
      }
      return;
    }

    // 3. WebUI Action Command: @<actionCode>:<state>
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
      case 27: // B_OFF: Stop cycle / feed disengage / zero turns and angle
        if (!this.isOn) {
          this.turns = 0;
          this.turnsFraction = 0;
          this.angle = 0;
        }
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
      this.angle = ((this.angle + revsDelta * 360) % 360 + 360) % 360;
      if (this.angle >= 359.95) this.angle = 0.0;
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
      `UI.turns=${typeof Formatters !== 'undefined' ? Formatters.formatTurns(this.turns) : this.turns.toFixed(2)}\n` +
      `UI.angle=${typeof Formatters !== 'undefined' ? Formatters.formatAngle(this.angle) : this.angle.toFixed(1)}\n` +
      `UI.x=${xMm}\n` +
      `UI.diameter=${this.isDiameterMode ? '1' : '0'}\n` +
      `UI.xLeft=${xLeftStr}\n` +
      `UI.xRight=${xRightStr}\n`;
    const modeInt = this.mode === 'GEAR' ? 0 : (this.mode === 'XGEAR' ? 1 : (this.mode === 'CONE' ? 3 : (this.mode === 'THREAD' ? 7 : 0)));
    const zLeftVal = this.zLeft !== null ? Math.round(this.zLeft) : 2147483647;
    const zRightVal = this.zRight !== null ? Math.round(this.zRight) : -2147483648;
    const xUpVal = this.xLeft !== null ? Math.round(this.xLeft) : 2147483647;
    const xDownVal = this.xRight !== null ? Math.round(this.xRight) : -2147483648;

    const domainTelemetry = this._buildDomainTelemetry(0x0F);

    const canonicalTelemetry =
      `M.mode=${modeInt}\n` +
      `M.on=${this.isOn ? 1 : 0}\n` +
      `M.estop=0\n` +
      `M.dupr=${this.dupr}\n` +
      `M.nextDupr=${this.dupr}\n` +
      `M.thFmt=${this.thFmt}\n` +
      `M.thPitch=${this.thPitch}\n` +
      `M.tpMode=${this.tpMode}\n` +
      `M.tpVal=${this.tpVal}\n` +
      `M.coneRatio=${this.coneRatio}\n` +
      `M.tpPreset=${this.tpPreset}\n` +
      `M.feedStepDu=${this.feedStepDu}\n` +
      `M.jogStepDu=${this.moveStep}\n` +
      `M.measure=${this.measure === 'MM' ? 0 : (this.measure === 'IN' ? 1 : 2)}\n` +
      `M.zDu=${Math.round(this.z)}\n` +
      `M.xDu=${Math.round(this.x)}\n` +
      `M.yDu=0\n` +
      `M.zLeftDu=${zLeftVal}\n` +
      `M.zRightDu=${zRightVal}\n` +
      `M.xUpDu=${xUpVal}\n` +
      `M.xDownDu=${xDownVal}\n` +
      `M.rpm=${Math.round(this.rpm)}\n` +
      `M.turns=${typeof Formatters !== 'undefined' ? Formatters.formatTurns(this.turns) : this.turns.toFixed(2)}\n` +
      `M.angle=${typeof Formatters !== 'undefined' ? Formatters.formatAngle(this.angle) : this.angle.toFixed(1)}\n` +
      `M.dia=${this.isDiameterMode ? 1 : 0}\n` +
      `M.abs=1\n` +
      `M.zDro=0\n` +
      `M.xDro=0\n` +
      `M.starts=${this.starts}\n` +
      `M.passes=${this.turnPasses}\n`;

    const fullTelemetry = domainTelemetry + canonicalTelemetry + telemetry;

    if (fullTelemetry !== this._lastTelemetryStr) {
      this._lastTelemetryStr = fullTelemetry;
      this._lastBroadcastTime = now;
      this._emitRaw(fullTelemetry);
    }
  }

  _buildDomainTelemetry(domainMask = 0x0F) {
    let out = '';
    const modeInt = this.mode === 'GEAR' ? 0 : (this.mode === 'XGEAR' ? 1 : (this.mode === 'CONE' ? 3 : (this.mode === 'THREAD' ? 7 : 0)));
    const zLeftVal = this.zLeft !== null ? Math.round(this.zLeft) : 2147483647;
    const zRightVal = this.zRight !== null ? Math.round(this.zRight) : -2147483648;
    const xUpVal = this.xLeft !== null ? Math.round(this.xLeft) : 2147483647;
    const xDownVal = this.xRight !== null ? Math.round(this.xRight) : -2147483648;

    if (domainMask & 0x01) { // Kinematics (K)
      out += `K.zDu=${Math.round(this.z)}\n` +
             `K.xDu=${Math.round(this.x)}\n` +
             `K.yDu=0\n` +
             `K.zLeftDu=${zLeftVal}\n` +
             `K.zRightDu=${zRightVal}\n` +
             `K.xUpDu=${xUpVal}\n` +
             `K.xDownDu=${xDownVal}\n` +
             `K.zDis=0\n` +
             `K.xDis=0\n` +
             `K.yDis=0\n` +
             `K.rpm=${Math.round(this.rpm)}\n` +
             `K.turns=${typeof Formatters !== 'undefined' ? Formatters.formatTurns(this.turns) : this.turns.toFixed(2)}\n` +
             `K.angle=${typeof Formatters !== 'undefined' ? Formatters.formatAngle(this.angle) : this.angle.toFixed(1)}\n` +
             `K.mode=${modeInt}\n` +
             `K.on=${this.isOn ? 1 : 0}\n` +
             `K.estop=0\n` +
             `K.dupr=${this.dupr}\n` +
             `K.nextDupr=${this.dupr}\n`;
    }
    if (domainMask & 0x02) { // Intent (I)
      out += `I.thFmt=${this.thFmt}\n` +
             `I.thPitch=${this.thPitch}\n` +
             `I.dupr=${this.dupr}\n` +
             `I.tpMode=${this.tpMode}\n` +
             `I.tpVal=${this.tpVal}\n` +
             `I.coneRatio=${this.coneRatio}\n` +
             `I.tpPreset=${this.tpPreset}\n` +
             `I.feedStepDu=${this.feedStepDu}\n` +
             `I.jogStepDu=${this.moveStep}\n` +
             `I.feedRev=${this.dupr < 0 ? 1 : 0}\n`;
    }
    if (domainMask & 0x04) { // Cycle (C)
      out += `C.passes=${this.turnPasses}\n` +
             `C.opIdx=${this.currentPass}\n` +
             `C.setupIdx=0\n` +
             `C.starts=${this.starts}\n`;
    }
    if (domainMask & 0x08) { // Properties (P)
      out += `P.measure=${this.measure === 'MM' ? 0 : (this.measure === 'IN' ? 1 : 2)}\n` +
             `P.dia=${this.isDiameterMode ? 1 : 0}\n` +
             `P.abs=1\n` +
             `P.zDro=0\n` +
             `P.xDro=0\n` +
             `P.screwZDu=40000\n` +
             `P.screwXDu=40000\n` +
             `P.motorStepsZ=800\n` +
             `P.motorStepsX=800\n` +
             `P.encoderPpr=2400\n`;
    }
    return out;
  }

  _emitDomainQuery(line) {
    let q = line;
    if (q.startsWith('?get=')) q = q.substring(5);
    else if (q.startsWith('get=')) q = q.substring(4);
    else if (q.startsWith('CMD.get=')) q = q.substring(8);
    else if (q === '?state') q = 'ALL';

    const tokens = q.toUpperCase().split(/[,;| ]+/);
    let mask = 0;
    for (const t of tokens) {
      if (t === 'K' || t === 'KINEMATICS') mask |= 0x01;
      else if (t === 'I' || t === 'INTENT') mask |= 0x02;
      else if (t === 'C' || t === 'CYCLE') mask |= 0x04;
      else if (t === 'P' || t === 'PROPERTIES' || t === 'PROPS') mask |= 0x08;
      else if (t === 'ALL' || t === '*') mask |= 0x0F;
    }
    if (mask === 0) mask = 0x0F;
    const resp = this._buildDomainTelemetry(mask);
    this._emitRaw(resp);
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
