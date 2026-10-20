/**
 * whatELS Communication Service & Protocol Router
 * Bridges the UI to either a Live ESP32 WebSocket (ws://<host>:81)
 * or the in-browser whatELS Firmware Emulator.
 * Handles continuous movement heartbeats, packet logging, and telemetry parsing.
 */
class WhatelsService extends EventTarget {
  constructor(options = {}) {
    super();
    this.mode = options.mode || 'emulator'; // 'emulator' | 'live'
    this.host = options.host || 'whatels.local';
    this.port = options.port || 81;
    this.logStatusPushes = options.logStatusPushes || false; // Suppressed by default for clean logs

    this.ws = null;
    this.emulator = null;
    this.heartbeatTimer = null;
    this.activeHolds = new Set();
    this.reconnectTimer = null;
    this._settingsTransactions = [];

    // Normalized controller state (UI display strings)
    this.state = {
      connected: false,
      backend: this.mode,
      status: 'OFF',        // 'ON', 'OFF', 'SYN'
      mode: 'GEAR',         // 'GEAR', 'THREAD', 'TURN', etc.
      pitch: '1.500',
      measure: 'MM',        // 'MM', 'IN', 'TPI'
      step: '0.100',
      rpm: 0,
      turns: '0.0',
      angle: '0.0',
      x: '0.000',
      z: '0.000',
      diameter: '1',        // '1' (Diameter mode), '0' (Radius mode)
      abs: '1',             // '1' (Absolute mode), '0' (Incremental mode)
      xDro: '0',            // '1' (Reading from physical scale), '0' (Stepper counts)
      zDro: '0',            // '1' (Reading from physical scale), '0' (Stepper counts)
      xLeft: '', xRight: '',
      zLeft: '', zRight: '',
      message: '',
      version: 'Unknown'
    };

    // Authoritative Canonical State (Integers / Deci-microns / Pure Intent)
    this.canonical = {
      mode: 0,
      on: false,
      estop: 0,
      dupr: 0,
      nextDupr: 0,
      thFmt: 0,           // 0: Metric, 1: TPI, 2: Inch
      thPitch: 0.0,       // Operator authoring value
      tpMode: 0,          // 0: Ratio, 1: Angle, 2: Preset
      tpVal: 1.0,         // Operator authoring value
      coneRatio: 1.0,     // Kinematic ratio
      tpPreset: 0,
      feedStepDu: 500,    // Deci-microns (0.05 mm/rev)
      jogStepDu: 10000,   // Deci-microns (1.0 mm)
      measure: 0,
      zDu: 0,
      xDu: 0,
      zLeftDu: 2147483647,
      zRightDu: -2147483648,
      xUpDu: 2147483647,
      xDownDu: -2147483648,
      zDis: false,
      xDis: false,
      abs: true,
      zDro: false,
      xDro: false,
      rpm: 0,
      turns: 0,
      angle: 0,
      starts: 1,
      passes: 1,
      opIdx: 0,
      setupIdx: 0,
      feedRev: false,
      screwZDu: 40000,
      screwXDu: 40000,
      motorStepsZ: 800,
      motorStepsX: 800,
      encoderPpr: 2400,
      perms: 0,
      gearAxis: 'Z',
      gearSync: true,
      gearDir: 1
    };

    // Action Code Mapping (from docs/h5.ino)
    this.ACTIONS = {
      B_LEFT: 21,        // Jog Z- (Carriage left / towards chuck)
      B_RIGHT: 22,       // Jog Z+ (Carriage right / towards tailstock)
      B_UP: 23,          // Jog X- (Cross-slide in / towards centerline)
      B_DOWN: 24,        // Jog X+ (Cross-slide out / retract)
      B_OFF: 27,         // Stop / Disengage
      B_BACKSPACE: 28,   // Backspace
      B_ON: 30,          // Start / Engage
      B_STEP: 64,        // Step size cycle
      B_STOPL: 65,       // Z Left stop
      B_STOPR: 68,       // Z Right stop
      B_STOPU: 87,       // X Forward stop
      B_STOPD: 83,       // X Rear stop
      B_MEASURE: 77,     // Units cycle (MM/IN/TPI)
      B_DIAMETER: 79,    // Diameter set
      B_REVERSE: 82,     // Pitch reverse
      B_MULTISTART: 84,  // Multi-start thread
      B_X: 88,           // Zero X
      B_Z: 90,           // Zero Z
      B_PLUS: 95,        // Plus adjustment
      B_MINUS: 60,       // Minus adjustment
      B_MODE_GEARS: 97,  // Gearbox mode
      B_MODE_TURN: 98,   // Turn mode
      B_MODE_FACE: 99,   // Face mode
      B_MODE_CONE: 100,  // Cone/Taper mode
      B_MODE_CUT: 101,   // Cutoff mode
      B_MODE_THREAD: 102,// Thread mode
      B_MODE_ASYNC: 103, // Async mode
      B_MODE_ELLIPSE: 104,// Ellipse mode
      B_MODE_GCODE: 105, // G-code mode
      B_MODE: 107,       // Cycle mode menu
      B_MODE_JOYSTICK: 108,// Joystick mode
      B_MODE_XGEAR: 109, // X-gearbox mode
      B_MODE_SLOT: 110,  // Slot mode
      B_DIA_RAD: 202,    // Toggle Diameter / Radius mode
      B_ABS_INC: 203,    // Toggle Absolute / Incremental mode
      B_X_TOGGLE_DRO: 206,// Toggle X Scale DRO vs Stepper
      B_Z_TOGGLE_DRO: 207 // Toggle Z Scale DRO vs Stepper
    };
  }

  // --- Backend Switching & Lifecycle ---
  setBackend(mode, host = null, port = null) {
    if (host) this.host = host.replace(/^https?:\/\//i, '').replace(/^wss?:\/\//i, '').split('/')[0].split(':')[0];
    if (port) this.port = port;

    if (this.mode !== mode || mode === 'live') {
      this.disconnect();
      this.mode = mode;
      this.state.backend = mode;
      this.connect();
    }
  }

  connect() {
    this.disconnect();
    if (this.mode === 'emulator') {
      this._connectEmulator();
    } else {
      this._connectWebSocket();
    }
  }

  disconnect() {
    clearTimeout(this.reconnectTimer);
    this._stopHeartbeat();

    if (this.ws) {
      this.ws.onopen = null;
      this.ws.onmessage = null;
      this.ws.onerror = null;
      this.ws.onclose = null;
      try { this.ws.close(); } catch(e) {}
      this.ws = null;
    }

    if (this.emulator) {
      this.emulator.stop();
      this.emulator.onmessage = null;
      this.emulator = null;
    }

    this.state.connected = false;
    this._emit('connection', { connected: false, mode: this.mode, host: this.host });
  }

  _connectEmulator() {
    const EmuClass = window.WhatelsEmulator;
    if (!EmuClass) {
      console.error('WhatelsEmulator class not found!');
      return;
    }
    this.emulator = new EmuClass();
    this.emulator.onmessage = (event) => {
      this._handleIncoming(event.data);
    };
    this.state.connected = true;
    this._log('SYS', 'Emulator connected', 'Internal virtual ESP32 initialized');
    this._emit('connection', { connected: true, mode: 'emulator', host: 'Local Emulator' });
    this.sendRaw('?get=ALL\n', 'Hydrate all state domains');
    this.sendRaw('?sysinfo\n', 'Query emulator sysinfo');
  }

  _connectWebSocket() {
    const wsUrl = `ws://${this.host}:${this.port}/canonical`;
    this._log('SYS', `Connecting to ${wsUrl}...`, 'Opening WebSocket connection');
    this._emit('connection', { connected: false, connecting: true, mode: 'live', host: this.host });

    try {
      this.ws = new WebSocket(wsUrl);
      this.ws.onopen = () => {
        this.state.connected = true;
        this.state.features = [];
        this._log('SYS', `Connected to ${wsUrl}`, 'WebSocket connection established (Channel: Canonical)');
        this._emit('connection', { connected: true, mode: 'live', host: this.host });
        this.sendRaw('?sysinfo\n', 'Query controller sysinfo');
      };

      this.ws.onmessage = (event) => {
        this._handleIncoming(event.data);
      };

      this.ws.onerror = (err) => {
        this._log('ERR', 'WebSocket error', err.message || 'Connection error');
      };

      this.ws.onclose = () => {
        this.state.connected = false;
        this._stopHeartbeat();
        this._log('SYS', 'WebSocket disconnected', 'Connection closed');
        this._emit('connection', { connected: false, mode: 'live', host: this.host });
        // Auto-reconnect after 3 seconds if still in live mode
        if (this.mode === 'live') {
          this.reconnectTimer = setTimeout(() => this.connect(), 3000);
        }
      };
    } catch (err) {
      this._log('ERR', 'Failed to initialize WebSocket', err.message);
    }
  }

  // --- Outbound Action API ---

  // Raw transmitter with logging
  sendRaw(data, annotation = '', quiet = false) {
    const isTelemetryPing = data === '?' || data === '?sysinfo' || data === '?sysinfo\n';
    if (!quiet && (!isTelemetryPing || this.logStatusPushes)) {
      this._log('TX', data.trim(), annotation, isTelemetryPing);
    }

    if (this.mode === 'emulator' && this.emulator) {
      this.emulator.send(data);
    } else if (this.mode === 'live' && this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(data);
    }
  }

  // Pulse single action code (press -> 70ms -> release)
  pulseAction(actionCode, label = '') {
    this.sendRaw(`@${actionCode}:1\n`, `${label} [Code ${actionCode} Press]`);
    setTimeout(() => {
      this.sendRaw(`@${actionCode}:0\n`, `${label} [Code ${actionCode} Release]`);
    }, 70);
  }

  // Start continuous hold with 200ms firmware safety heartbeat
  startHold(actionCode, label = '') {
    if (this.activeHolds.has(actionCode)) return;
    this.activeHolds.add(actionCode);
    this.sendRaw(`@${actionCode}:1\n`, `${label} [Code ${actionCode} Held]`);

    if (!this.heartbeatTimer) {
      this.heartbeatTimer = setInterval(() => {
        for (const code of this.activeHolds) {
          this.sendRaw(`@${code}:1\n`, `Heartbeat [Code ${code}]`);
        }
      }, 200);
    }
  }

  stopHold(actionCode, label = '') {
    if (!this.activeHolds.has(actionCode)) return;
    this.activeHolds.delete(actionCode);
    this.sendRaw(`@${actionCode}:0\n`, `${label} [Code ${actionCode} Released]`);
    if (this.activeHolds.size === 0) {
      this._stopHeartbeat();
    }
  }

  _stopHeartbeat() {
    if (this.heartbeatTimer) {
      clearInterval(this.heartbeatTimer);
      this.heartbeatTimer = null;
    }
    this.activeHolds.clear();
  }

  // High-level Jog Controller supporting both Semantic Directions and legacy numbers
  jog(axis, dirOrSemantic, isPress) {
    const upper = axis.toUpperCase();
    let semanticDir = '';
    const num = Number(dirOrSemantic);
    const isNum = !isNaN(num) && dirOrSemantic !== '' && typeof dirOrSemantic !== 'boolean';

    if (upper === 'Z') {
      if (isNum) {
        semanticDir = num < 0 ? 'CHUCK' : 'TAIL';
      } else if (typeof dirOrSemantic === 'string') {
        semanticDir = dirOrSemantic.toUpperCase();
      }
      if (semanticDir === 'LEFT') semanticDir = 'CHUCK';
      if (semanticDir === 'RIGHT') semanticDir = 'TAIL';
    } else if (upper === 'X') {
      if (isNum) {
        semanticDir = num > 0 ? 'INFEED' : 'RETRACT';
      } else if (typeof dirOrSemantic === 'string') {
        semanticDir = dirOrSemantic.toUpperCase();
      }
      if (semanticDir === 'IN' || semanticDir === 'UP') semanticDir = 'INFEED';
      if (semanticDir === 'OUT' || semanticDir === 'DOWN') semanticDir = 'RETRACT';
    }

    if (!semanticDir) return;
    const label = `Jog ${upper} ${semanticDir}`;
    if (isPress) {
      this.sendRaw(`CMD.jogHold=${upper},${semanticDir}\n`, `${label} [Held]`);
      if (!this.heartbeatTimer) {
        this.heartbeatTimer = setInterval(() => {
          this.sendRaw(`CMD.jogHold=${upper},${semanticDir}\n`, `Heartbeat [${label}]`);
        }, 200);
      }
    } else {
      this._stopHeartbeat();
      this.sendRaw(`CMD.jogRelease=${upper}\n`, `${label} [Released]`);
    }
  }

  // Discrete 1-step jog in semantic direction
  jogStep(axis, dirOrSemantic) {
    const upper = axis.toUpperCase();
    let semanticDir = '';
    const num = Number(dirOrSemantic);
    const isNum = !isNaN(num) && dirOrSemantic !== '' && typeof dirOrSemantic !== 'boolean';

    if (upper === 'Z') {
      if (isNum) {
        semanticDir = num < 0 ? 'CHUCK' : 'TAIL';
      } else if (typeof dirOrSemantic === 'string') {
        semanticDir = dirOrSemantic.toUpperCase();
      }
      if (semanticDir === 'LEFT') semanticDir = 'CHUCK';
      if (semanticDir === 'RIGHT') semanticDir = 'TAIL';
    } else if (upper === 'X') {
      if (isNum) {
        semanticDir = num > 0 ? 'INFEED' : 'RETRACT';
      } else if (typeof dirOrSemantic === 'string') {
        semanticDir = dirOrSemantic.toUpperCase();
      }
      if (semanticDir === 'IN' || semanticDir === 'UP') semanticDir = 'INFEED';
      if (semanticDir === 'OUT' || semanticDir === 'DOWN') semanticDir = 'RETRACT';
    }
    if (!semanticDir) return;
    this.sendRaw(`CMD.jogStep=${upper},${semanticDir}\n`, `Jog Step ${upper} ${semanticDir}`);
  }

  // Cycle Start / Stop
  startCycle(bundle) {
    if (bundle) {
      if (Array.isArray(bundle)) {
        for (const cmd of bundle) {
          if (!cmd) continue;
          this.sendRaw(cmd.endsWith('\n') ? cmd : cmd + '\n', '', true);
        }
      } else if (typeof bundle === 'string') {
        const lines = bundle.split('\n');
        for (const line of lines) {
          const trimmed = line.trim();
          if (trimmed) this.sendRaw(trimmed + '\n', '', true);
        }
      }
    }
    this.sendRaw('CMD.on=1\n', 'START FEED / CYCLE');
  }
  stopCycle()  { this.sendRaw('CMD.stop\n', 'STOP FEED / CYCLE'); }

  // Zero axes
  zeroAxis(axis) {
    const upper = axis.toUpperCase();
    this.sendRaw(`CMD.axisZero=${upper}\n`, `Zero Axis ${upper}`);
  }

  // Set / Clear Stops
  toggleStop(axis, bound) {
    const upper = axis.toUpperCase();
    const isLeftOrUp = (bound === 'min' || bound === 'left' || bound === 'up');
    const curDu = upper === 'Z' ? this.canonical.zDu : this.canonical.xDu;
    this.sendRaw(`CMD.axisLimitDu=${upper},${isLeftOrUp ? 1 : 0},${Math.round(curDu)}\n`, `Set Stop ${upper} ${bound}`);
  }

  // Step and Measure cycles
  cycleStep() {
    const cur = this.canonical.jogStepDu || 10000;
    let next = 10000;
    if (cur === 10000) next = 1000;
    else if (cur === 1000) next = 100;
    else next = 10000;
    this.canonical.jogStepDu = next;
    this.sendRaw(`CMD.jogStepDu=${next}\n`, `Set Jog Step ${next} du`);
  }
  cycleMeasure() {
    const cur = this.canonical.measure !== undefined ? this.canonical.measure : 0;
    const nextM = (cur + 1) % 3;
    this.canonical.measure = nextM;
    this.sendRaw(`CMD.measure=${nextM}\n`, `Cycle Measure Units (${nextM})`);
  }
  toggleDiaRad() {
    this.canonical.dia = !this.canonical.dia;
  }
  reversePitch() { this.reverseFeed(); }
  nudgePitch(plus) {
    const deltaDu = plus ? 500 : -500;
    const curDu = this.canonical.dupr || 15000;
    const nextDu = Math.max(100, curDu + deltaDu);
    this.setFeedDupr(nextDu);
  }

  // --- Canonical 4-Layer Command APIs ---
  setFeedDupr(du) {
    if (du === undefined || isNaN(du)) return;
    this.sendRaw(`CMD.feedDupr=${Math.round(du)}\n`, `Set Feedrate ${du} du`);
  }
  setFeedMm(pitchMm) {
    if (!pitchMm || isNaN(pitchMm)) return;
    this.sendRaw(`CMD.feedMm=${pitchMm}\n`, `Set Metric Pitch ${pitchMm} mm`);
  }
  setFeedTpi(tpi) {
    if (!tpi || isNaN(tpi)) return;
    this.sendRaw(`CMD.feedTpi=${tpi}\n`, `Set Thread ${tpi} TPI`);
  }
  setFeedInch(pitchInch) {
    if (!pitchInch || isNaN(pitchInch)) return;
    this.sendRaw(`CMD.feedInch=${pitchInch}\n`, `Set Imperial Pitch ${pitchInch} in`);
  }
  reverseFeed() {
    this.sendRaw(`CMD.feedRev\n`, `Reverse Feed Direction`);
  }
  setFeedStepDu(stepDu) {
    if (!stepDu || isNaN(stepDu)) return;
    this.sendRaw(`CMD.feedStepDu=${Math.round(stepDu)}\n`, `Set Feed Step ${stepDu} du`);
  }
  cycleFeedStep() {
    this.sendRaw(`CMD.feedStepCycle\n`, `Cycle Feed Step`);
  }
  setJogStepDu(stepDu) {
    if (!stepDu || isNaN(stepDu)) return;
    this.sendRaw(`CMD.jogStepDu=${Math.round(stepDu)}\n`, `Set Jog Step ${stepDu} du`);
  }
  setTaperRatio(ratioK) {
    if (!ratioK || isNaN(ratioK)) return;
    this.sendRaw(`CMD.taperRatio=${ratioK}\n`, `Set Taper Ratio 1:${ratioK}`);
  }
  setTaperAngle(deg) {
    if (deg === undefined || isNaN(deg)) return;
    this.sendRaw(`CMD.taperAngle=${deg}\n`, `Set Taper Angle ${deg}°`);
  }
  setTaperPreset(presetId, deg, ratioVal) {
    this.sendRaw(`CMD.taperPreset=${presetId || 0},${deg || 0},${ratioVal || 0}\n`, `Set Taper Preset #${presetId}`);
  }
  setAxisZero(axis) {
    this.sendRaw(`CMD.axisZero=${axis.toUpperCase()}\n`, `Zero Axis ${axis}`);
  }
  setAxisPosDu(axis, posDu) {
    this.sendRaw(`CMD.axisPosDu=${axis.toUpperCase()},${Math.round(posDu)}\n`, `Preset Axis ${axis} to ${posDu} du`);
  }
  setAxisLimitDu(axis, isLeftOrUp, limitDu) {
    this.sendRaw(`CMD.axisLimitDu=${axis.toUpperCase()},${isLeftOrUp ? 1 : 0},${Math.round(limitDu)}\n`, `Set Limit ${axis}`);
  }
  clearAxisLimit(axis, isLeftOrUp) {
    this.sendRaw(`CMD.axisClearLimit=${axis.toUpperCase()},${isLeftOrUp ? 1 : 0}\n`, `Clear Limit ${axis}`);
  }
  setOperatingMode(modeId) {
    this.sendRaw(`CMD.mode=${modeId}\n`, `Set Operating Mode ${modeId}`);
  }
  setControllerOn(isOn) {
    this.sendRaw(`CMD.on=${isOn ? 1 : 0}\n`, `Controller ${isOn ? 'ON' : 'OFF'}`);
  }

  queryState(domains = 'ALL') {
    this.sendRaw(`?get=${domains}\n`, `Query state [${domains}]`);
  }

  // Settings Transaction API (Unicast Request-Response)
  getSettings(key = 'ALL') {
    return new Promise((resolve, reject) => {
      const txnId = Math.random().toString(36).substring(2, 9);
      const timer = setTimeout(() => {
        const idx = this._settingsTransactions.findIndex(t => t.id === txnId);
        if (idx !== -1) {
          const txn = this._settingsTransactions[idx];
          this._settingsTransactions.splice(idx, 1);
          if (txn.type === 'get' && txn.key === 'all' && Object.keys(txn.results).length > 0) {
            resolve(txn.results);
          } else {
            reject(new Error(`SETTINGS.get timeout for ${key}`));
          }
        }
      }, 1500);

      this._settingsTransactions.push({
        id: txnId,
        type: 'get',
        key: key.toLowerCase(),
        targetKey: key,
        resolve,
        reject,
        timer,
        results: {}
      });

      this.sendRaw(`SETTINGS.get=${key}\n`, `Get Setting [${key}]`);
    });
  }

  setSetting(key, val) {
    return new Promise((resolve, reject) => {
      const txnId = Math.random().toString(36).substring(2, 9);
      const timer = setTimeout(() => {
        const idx = this._settingsTransactions.findIndex(t => t.id === txnId);
        if (idx !== -1) {
          this._settingsTransactions.splice(idx, 1);
          reject(new Error(`SETTINGS.set timeout for ${key}`));
        }
      }, 2000);

      this._settingsTransactions.push({
        id: txnId,
        type: 'set',
        key: key.toLowerCase(),
        targetKey: key,
        resolve,
        reject,
        timer
      });

      this.sendRaw(`SETTINGS.set=${key},${val}\n`, `Set Setting [${key}=${val}]`);
    });
  }

  // Backward-compatible Pitch / Feedrate setters
  setPitch(pitchMm) {
    this.setFeedMm(pitchMm);
  }

  // --- Gearbox Composite APIs ---
  setGearAxis(axis) {
    if (!axis) return;
    const ax = String(axis).toUpperCase();
    this.sendRaw(`CMD.gearAxis=${ax}\n`, `Set Gear Axis ${ax}`);
  }

  setGearSync(isSync) {
    const val = isSync ? 1 : 0;
    this.sendRaw(`CMD.gearSync=${val}\n`, `Set Gear Sync [${val}]`);
  }

  setGearDir(dir) {
    if (dir === undefined) return;
    this.sendRaw(`CMD.gearDir=${dir}\n`, `Set Gear Direction [${dir}]`);
  }

  nudgeFeed(dir) {
    const d = (dir === '-' || Number(dir) < 0) ? -1 : 1;
    this.sendRaw(`CMD.feedNudge=${d}\n`, `Feed Nudge [${d > 0 ? '+1' : '-1'}]`);
  }

  discoverModes(query = '') {
    const q = query ? (query.startsWith('?') ? query : '?' + query) : '';
    this.sendRaw(`MODE.discover${q}\n`, `Discover Modes [${query}]`);
  }

  // Mode Selection
  setMode(modeKeyOrId) {
    if (typeof modeKeyOrId === 'number') {
      this.sendRaw(`CMD.mode=${modeKeyOrId}\n`, `Set Mode [ID ${modeKeyOrId}]`);
    } else if (typeof modeKeyOrId === 'string') {
      const slug = modeKeyOrId.trim().toLowerCase();
      // If it's a known canonical slug, send the slug directly; otherwise translate or send as is
      this.sendRaw(`CMD.mode=${slug}\n`, `Set Mode [${slug}]`);
    }
  }

  setLogStatusPushes(enabled) {
    this.logStatusPushes = !!enabled;
  }

  // --- Incoming Message Parser ---
  _handleIncoming(text) {
    let telemetryChanged = false;
    let telemetryLines = [];
    const lines = text.split('\n');

    for (const raw of lines) {
      const line = raw.trim();
      if (!line) continue;

      const isDomainK = line.startsWith('K.');
      const isDomainI = line.startsWith('I.');
      const isDomainC = line.startsWith('C.');
      const isDomainP = line.startsWith('P.');
      const isM = line.startsWith('M.');
      const isUI = line.startsWith('UI.');
      const isGrbl = line.startsWith('<') && line.endsWith('>');

      if (isDomainK || isDomainI || isDomainC || isDomainP || isM) {
        telemetryLines.push(line);
        const idx = line.indexOf('=');
        if (idx !== -1) {
          const key = line.substring(2, idx);
          if (key === 'dia' || key === 'diameter') continue; // Decoupled: pure client-side presentation
          const val = line.substring(idx + 1);
          if (key in this.canonical) {
            if (typeof this.canonical[key] === 'boolean') {
              this.canonical[key] = val === '1' || val === 'true';
            } else if (typeof this.canonical[key] === 'number') {
              if (key === 'perms' && typeof val === 'string' && (val.startsWith('0x') || val.startsWith('0X'))) {
                this.canonical[key] = parseInt(val, 16);
              } else if (key === 'gearDir') {
                this.canonical[key] = (val === 'CHUCK' || val === 'CENTER' || val === 'INFEED' || val === '-1' || val === -1) ? -1 : 1;
              } else {
                const num = parseFloat(val);
                if (!isNaN(num)) {
                  this.canonical[key] = num;
                } else if (key === 'zRightDu' || key === 'xDownDu') {
                  this.canonical[key] = -2147483648;
                } else if (key === 'zLeftDu' || key === 'xUpDu') {
                  this.canonical[key] = 2147483647;
                }
              }
            } else {
              this.canonical[key] = val;
            }
            telemetryChanged = true;
          }
        }
      } else if (isUI) {
        telemetryLines.push(line);
        const idx = line.indexOf('=');
        if (idx !== -1) {
          const key = line.substring(3, idx);
          if (key === 'dia' || key === 'diameter') continue; // Decoupled: pure client-side presentation
          const val = line.substring(idx + 1);

          if (key in this.state) {
            this.state[key] = val;
            telemetryChanged = true;
          }
        }
      } else if (isGrbl) {
        telemetryLines.push(line);
        // Parse GRBL-style <Idle|WPos:X,Y,Z|FS:F,RPM|Id:...>
        const parts = line.substring(1, line.length - 1).split('|');
        if (parts[0]) {
          this.state.status = parts[0].toUpperCase() === 'RUN' ? 'ON' : 'OFF';
          telemetryChanged = true;
        }
        const wpos = parts.find(p => p.startsWith('WPos:'));
        if (wpos) {
          const coords = wpos.substring(5).split(',');
          if (coords.length >= 3) {
            this.state.x = coords[0];
            this.state.z = coords[2];
            telemetryChanged = true;
          }
        }
        const fs = parts.find(p => p.startsWith('FS:'));
        if (fs) {
          const fsParts = fs.substring(3).split(',');
          if (fsParts.length >= 2) {
            this.state.rpm = parseInt(fsParts[1], 10) || 0;
            telemetryChanged = true;
          }
        }
      } else if (line.startsWith('SETTING.')) {
        this._log('RX', line, 'Setting Value');
        const eq = line.indexOf('=');
        if (eq !== -1) {
          const k = line.substring(8, eq);
          if (k.toLowerCase() === 'dia' || k.toLowerCase() === 'diameter') continue; // Decoupled
          const v = line.substring(eq + 1);
          if (k in this.canonical) {
            this.canonical[k] = (typeof this.canonical[k] === 'number') ? parseFloat(v) : v;
            telemetryChanged = true;
          }
          for (let i = this._settingsTransactions.length - 1; i >= 0; i--) {
            const txn = this._settingsTransactions[i];
            if (txn.type === 'get') {
              if (txn.key === k.toLowerCase()) {
                clearTimeout(txn.timer);
                this._settingsTransactions.splice(i, 1);
                txn.resolve({ [k]: v });
              } else if (txn.key === 'all') {
                txn.results[k] = v;
              }
            }
          }
        }
      } else if (line.startsWith('OK:SETTINGS.')) {
        this._log('RX', line, 'Setting Updated');
        const eq = line.indexOf('=');
        const k = eq !== -1 ? line.substring(12, eq) : line.substring(12);
        const v = eq !== -1 ? line.substring(eq + 1) : '';
        for (let i = this._settingsTransactions.length - 1; i >= 0; i--) {
          const txn = this._settingsTransactions[i];
          if (txn.type === 'set' && txn.key === k.toLowerCase()) {
            clearTimeout(txn.timer);
            this._settingsTransactions.splice(i, 1);
            txn.resolve({ key: k, val: v, status: 'ok' });
          }
        }
      } else if (line.startsWith('ERR:SETTINGS.')) {
        this._log('ERR', line, 'Setting Error');
        const eq = line.indexOf('=');
        const k = eq !== -1 ? line.substring(13, eq) : line.substring(13);
        const reason = eq !== -1 ? line.substring(eq + 1) : 'Unknown error';
        for (let i = this._settingsTransactions.length - 1; i >= 0; i--) {
          const txn = this._settingsTransactions[i];
          if (txn.key === k.toLowerCase()) {
            clearTimeout(txn.timer);
            this._settingsTransactions.splice(i, 1);
            txn.reject(new Error(reason));
          }
        }
      } else if (line.startsWith('SYSINFO.')) {
        if (line.startsWith('SYSINFO.version=')) {
          this.state.version = line.substring('SYSINFO.version='.length);
        }
        if (line.startsWith('SYSINFO.features=')) {
          this.state.features = line.substring('SYSINFO.features='.length).split(',').map((f) => f.trim()).filter(Boolean);
          this._emit('features', this.state.features);
        }
        this._log('RX', line, 'Hardware Info');
      } else if (line.startsWith('WEBUI.warning=')) {
        this._emit('warning', line.substring(14));
        this._log('RX', line, 'Firmware Warning');
      } else if (line.startsWith('KEY.press=')) {
        this._emit('key', Number(line.substring(10)));
        this._log('RX', line, 'Key Press Remote Event');
      } else {
        this._log('RX', line, 'Controller Message');
      }
    }

    // Resolve any 'get all' settings transactions that accumulated values
    for (let i = this._settingsTransactions.length - 1; i >= 0; i--) {
      const txn = this._settingsTransactions[i];
      if (txn.type === 'get' && txn.key === 'all' && Object.keys(txn.results).length > 0) {
        clearTimeout(txn.timer);
        this._settingsTransactions.splice(i, 1);
        txn.resolve(txn.results);
      }
    }

    if (telemetryChanged) {
      if (this.canonical.zDu !== undefined) this.state.z = (this.canonical.zDu / 10000.0).toFixed(3);
      if (this.canonical.xDu !== undefined) this.state.x = (this.canonical.xDu / 10000.0).toFixed(3);
      if (this.canonical.rpm !== undefined) this.state.rpm = Math.round(this.canonical.rpm);
      if (this.canonical.turns !== undefined) this.state.turns = String(this.canonical.turns);
      if (this.canonical.angle !== undefined) this.state.angle = String(this.canonical.angle);
      if (this.canonical.on !== undefined) this.state.status = this.canonical.on ? 'ON' : 'OFF';
      if (this.canonical.measure !== undefined) this.state.measure = this.canonical.measure === 1 ? 'IN' : 'MM';

      this._emit('telemetry', { ...this.state, canonical: { ...this.canonical } });
      if (this.logStatusPushes) {
        const summary = `Z: ${this.state.z} mm | X: ${this.state.x} mm | ${this.state.rpm} RPM | ${this.state.status}`;
        this._logPacket('RX', summary, { ...this.state }, telemetryLines.join('\n'));
      }
    }
  }

  // --- Logger & Dispatcher ---
  _logPacket(direction, summary, data, raw) {
    const entry = {
      time: new Date(),
      direction,
      type: 'telemetry_packet',
      summary,
      data,
      raw,
      isTelemetry: true
    };
    this._emit('traffic', entry);
  }

  _log(direction, raw, annotation = '', isTelemetry = false) {
    const entry = {
      time: new Date(),
      direction,
      type: 'standard',
      raw,
      annotation,
      isTelemetry
    };
    this._emit('traffic', entry);
  }

  _emit(eventName, detail) {
    this.dispatchEvent(new CustomEvent(eventName, { detail }));
  }
}

window.WhatelsService = WhatelsService;
