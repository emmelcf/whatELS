/**
 * whatELS Web UI Service & Presentation Wrapper (Tier 3)
 * Thin wrapper around WhatelsClient SDK (Tier 2) preserving window.whatElsClient
 * compatibility for lathe-controller.js, whatels-state.js, and dev/lathe-viz.js.
 */
class WhatelsService extends EventTarget {
  constructor(options = {}) {
    super();
    this.mode = options.mode || 'emulator'; // 'emulator' | 'live'
    this.host = options.host || 'whatels.local';
    this.port = options.port || 81;
    this.logStatusPushes = options.logStatusPushes || false;

    this.transport = options.transport || null;
    this._customTransport = !!options.transport;
    this.client = options.client || null;
    this._clientCleanup = null;
    this._transportCleanup = null;
    this._settingsTransactions = [];

    // Normalized controller state (UI display strings)
    this.state = {
      connected: false,
      backend: this.mode,
      status: 'OFF',        // 'ON', 'OFF', 'SYN'
      mode: 'GEAR',         // 'GEAR', 'THREAD', 'TURN', etc.
      pitch: '1.500',
      measure: 'MM',        // 'MM', 'IN', 'TPI'
      step: '1.000',
      rpm: 0,
      turns: '0.0',
      angle: '0.0',
      x: '0.000',
      z: '0.000',
      diameter: '1',
      abs: '1',
      xDro: '0',
      zDro: '0',
      xLeft: '', xRight: '',
      zLeft: '', zRight: '',
      message: '',
      version: 'Unknown',
      features: []
    };

    // Authoritative Canonical State (Integers / Deci-microns / Pure Intent)
    this.canonical = {
      mode: 0,
      on: false,
      estop: 0,
      dupr: 0,
      nextDupr: 0,
      thFmt: 0,
      thPitch: 0.0,
      tpMode: 0,
      tpVal: 1.0,
      coneRatio: 1.0,
      tpPreset: 0,
      feedStepDu: 500,
      jogStepDu: 10000,
      measure: 0,
      zDu: 0,
      xDu: 0,
      zLeftDu: null,
      zRightDu: null,
      xUpDu: null,
      xDownDu: null,
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
      direction: 1,
      targetDistDu: -1,
      screwZDu: 20000,
      screwXDu: 20000,
      motorStepsZ: 800,
      motorStepsX: 800,
      encoderPpr: 1200,
      perms: 0,
      gearAxis: 'Z',
      gearSync: true,
      gearDir: 1
    };
  }

  // --- Backend Switching & Lifecycle ---

  setBackend(mode, host = null, port = null) {
    if (host) {
      this.host = host.replace(/^https?:\/\//i, '').replace(/^wss?:\/\//i, '').split('/')[0].split(':')[0];
    }
    if (port) this.port = port;

    if (this.mode !== mode || mode === 'live') {
      this.disconnect();
      this.mode = mode;
      this.state.backend = mode;
      this.connect();
    }
  }

  _resolveTransport() {
    const targetType = this.mode === 'emulator' ? 'emulator' : 'websocket';
    if (!this.transport || (!this._customTransport && this.transport.type !== targetType)) {
      if (this.mode === 'emulator') {
        const EmuTransport = globalThis.EmulatorTransport || (typeof window !== 'undefined' && window.EmulatorTransport);
        if (!EmuTransport) {
          this._log('ERR', 'Transport error', 'EmulatorTransport class not found');
          return null;
        }
        this.transport = new EmuTransport();
      } else {
        const WsTransport = globalThis.WebSocketTransport || (typeof window !== 'undefined' && window.WebSocketTransport);
        if (!WsTransport) {
          this._log('ERR', 'Transport error', 'WebSocketTransport class not found');
          return null;
        }
        this.transport = new WsTransport({
          host: this.host,
          port: this.port,
          path: '/canonical',
          autoReconnect: true
        });
      }
    }
    return this.transport;
  }

  async _ensureClient() {
    const transport = this._resolveTransport();
    if (!transport) return null;

    if (!this.client) {
      let ClientClass = globalThis.WhatelsClient || (typeof window !== 'undefined' && window.WhatelsClient);
      if (!ClientClass) {
        const mod = await import('../sdk/js/whatels-client.js');
        ClientClass = mod.WhatelsClient;
      }
      this.client = new ClientClass(transport, {
        clientType: 'webui',
        clientVer: '1.0.0',
        label: 'Web UI Touch DRO'
      });
    } else if (this.client.transport !== transport) {
      this.client.setTransport(transport);
    }

    this._bindTransport(transport);
    this._bindClient(this.client);
    return this.client;
  }

  _bindTransport(transport) {
    if (this._transportCleanup) {
      this._transportCleanup();
      this._transportCleanup = null;
    }

    // Wrap send once per transport instance for TX traffic logging
    if (!transport._whatelsLoggedSend) {
      const origSend = transport.send.bind(transport);
      transport.send = (line) => {
        const trimmed = String(line || '').trim();
        const isPing = trimmed === 'CMD.ping' || trimmed === 'SYS.get=info';
        if (!isPing || this.logStatusPushes) {
          this._log('TX', trimmed, '', isPing);
        }
        return origSend(line);
      };
      transport._whatelsLoggedSend = true;
    }

    const onLine = (event) => this._handleAuxiliaryLine(event.detail);
    const onStatus = (event) => {
      const detail = event.detail || {};
      if (detail.status === 'connecting') {
        this.state.connected = false;
        this._emit('connection', { connected: false, connecting: true, mode: this.mode, host: this.host });
      }
    };

    transport.addEventListener('line', onLine);
    transport.addEventListener('status', onStatus);

    this._transportCleanup = () => {
      transport.removeEventListener('line', onLine);
      transport.removeEventListener('status', onStatus);
    };
  }

  _bindClient(client) {
    if (this._clientCleanup) {
      this._clientCleanup();
      this._clientCleanup = null;
    }

    const onSnapshot = (event) => {
      const { snapshot } = event.detail || {};
      if (!snapshot) return;
      this._syncFromSnapshot(snapshot);
    };

    const onConnection = (event) => {
      const detail = event.detail || {};
      this.state.connected = !!detail.connected;
      if (detail.connected) {
        this.state.features = [];
        const hostLabel = this.mode === 'emulator' ? 'Local Emulator' : this.host;
        this._log('SYS', `${this.mode === 'emulator' ? 'Emulator' : 'WebSocket'} connected`, `Session ${detail.sessionId ?? '-'}`);
        this._emit('connection', {
          connected: true,
          mode: this.mode,
          host: hostLabel,
          sessionId: detail.sessionId,
          perms: detail.perms
        });
        this.sendRaw('SYS.get=info\n', 'Query controller sysinfo');
      } else if (detail.status === 'disconnected') {
        this._log('SYS', `${this.mode === 'emulator' ? 'Emulator' : 'WebSocket'} disconnected`, 'Connection closed');
        this._emit('connection', { connected: false, mode: this.mode, host: this.host });
      }
    };

    const onLog = (event) => {
      this._log('RX', event.detail, 'Controller Log');
    };

    const onError = (event) => {
      const err = event.detail;
      this._log('ERR', `${this.mode === 'emulator' ? 'Emulator' : 'WebSocket'} error`, (err && err.message) || 'Connection error');
    };

    client.addEventListener('snapshot', onSnapshot);
    client.addEventListener('connection', onConnection);
    client.addEventListener('log', onLog);
    client.addEventListener('error', onError);

    this._clientCleanup = () => {
      client.removeEventListener('snapshot', onSnapshot);
      client.removeEventListener('connection', onConnection);
      client.removeEventListener('log', onLog);
      client.removeEventListener('error', onError);
    };
  }

  async connect() {
    const client = await this._ensureClient();
    if (!client) return;
    try {
      await client.connect({ host: this.host, port: this.port });
    } catch (err) {
      this._log('ERR', 'Connection failed', (err && err.message) || String(err));
      this.state.connected = false;
      this._emit('connection', { connected: false, mode: this.mode, host: this.host });
    }
  }

  disconnect() {
    if (this.client) {
      this.client.disconnect().catch((err) => {
        this._log('ERR', 'Disconnect error', (err && err.message) || String(err));
      });
    } else if (this.transport) {
      this.transport.disconnect().catch((err) => {
        this._log('ERR', 'Disconnect error', (err && err.message) || String(err));
      });
    }
    this.state.connected = false;
    this._emit('connection', { connected: false, mode: this.mode, host: this.host });
  }

  // --- Outbound Action API (Delegates to WhatelsClient) ---

  sendRaw(data, annotation = '', quiet = false) {
    if (this.transport && this.transport.isConnected()) {
      this.transport.send(data).catch((err) => {
        if (!quiet) this._log('ERR', 'Transport send failure', err.message);
      });
    }
  }

  _fire(promise) {
    if (promise && typeof promise.catch === 'function') {
      promise.catch((err) => {
        this._log('ERR', 'Command failed', (err && err.message) || String(err));
      });
    }
    return promise;
  }

  jog(axis, dirOrSemantic, isPress) {
    if (!this.client) return;
    if (isPress) {
      return this._fire(this.client.jogHold(axis, dirOrSemantic));
    }
    return this._fire(this.client.jogRelease(axis));
  }

  jogStep(axis, dirOrSemantic) {
    if (!this.client) return;
    const upper = String(axis || '').toUpperCase();
    let sign = 0;
    const num = Number(dirOrSemantic);
    const isNum = !Number.isNaN(num) && dirOrSemantic !== '' && typeof dirOrSemantic !== 'boolean';

    if (upper === 'Z') {
      if (isNum) sign = num < 0 ? -1 : 1;
      else if (typeof dirOrSemantic === 'string') {
        const s = dirOrSemantic.toUpperCase();
        if (s === 'CHUCK' || s === 'LEFT' || s === '-') sign = -1;
        else if (s === 'TAIL' || s === 'RIGHT' || s === '+') sign = 1;
      }
    } else if (upper === 'X') {
      if (isNum) sign = num > 0 ? -1 : 1;
      else if (typeof dirOrSemantic === 'string') {
        const s = dirOrSemantic.toUpperCase();
        if (s === 'INFEED' || s === 'IN' || s === 'UP' || s === 'CENTER' || s === '-') sign = -1;
        else if (s === 'RETRACT' || s === 'OUT' || s === 'DOWN' || s === '+') sign = 1;
      }
    }
    if (!sign) return;
    const stepMag = Math.abs(Math.round(this.canonical.jogStepDu || 10000));
    return this._fire(this.client.jogStep(upper, sign * stepMag));
  }

  startCycle(bundle) {
    if (bundle) {
      if (Array.isArray(bundle)) {
        for (const cmd of bundle) {
          if (!cmd) continue;
          this.sendRaw(cmd.endsWith('\n') ? cmd : `${cmd}\n`, '', true);
        }
      } else if (typeof bundle === 'string') {
        for (const line of bundle.split('\n')) {
          const trimmed = line.trim();
          if (trimmed) this.sendRaw(`${trimmed}\n`, '', true);
        }
      }
    }
  }

  stopCycle() {
    if (this.client) return this._fire(this.client.stop());
    this.sendRaw('CMD.stop\n', 'STOP FEED / CYCLE');
  }

  zeroAxis(axis) {
    return this.setAxisZero(axis);
  }

  toggleStop(axis, bound) {
    const upper = String(axis || '').toUpperCase();
    const isLeftOrUp = (bound === 'min' || bound === 'left' || bound === 'up');
    const boundToken = upper === 'X' ? (isLeftOrUp ? 'UP' : 'DOWN') : (isLeftOrUp ? 'LEFT' : 'RIGHT');
    const curDu = upper === 'Z' ? this.canonical.zDu : this.canonical.xDu;
    if (this.client) return this._fire(this.client.axisLimitDu(upper, boundToken, Math.round(curDu)));
  }

  // Client-local Step and Measure cycles (no CMD.jogStepDu / CMD.measure on wire)
  cycleStep() {
    return this.cycleJogStep();
  }

  cycleJogStep() {
    const cur = this.canonical.jogStepDu || 10000;
    let next = 10000;
    if (cur === 10000) next = 1000;
    else if (cur === 1000) next = 100;
    else next = 10000;
    this.canonical.jogStepDu = next;
    this._updateDerivedState();
    this._emitTelemetry();
    return next;
  }

  cycleMeasure() {
    const cur = this.canonical.measure !== undefined ? this.canonical.measure : 0;
    const nextM = (cur + 1) % 3;
    this.canonical.measure = nextM;
    this._updateDerivedState();
    this._emitTelemetry();
    return nextM;
  }

  toggleDiaRad() {
    this.canonical.dia = !this.canonical.dia;
    this._emitTelemetry();
  }

  // --- Canonical Command Pass-Throughs ---

  startFeed(params) {
    if (this.client) return this._fire(this.client.startFeed(params));
  }

  setJogStepDu(stepDu) {
    if (!stepDu || Number.isNaN(Number(stepDu))) return;
    this.canonical.jogStepDu = Math.round(stepDu);
    this._updateDerivedState();
    this._emitTelemetry();
  }

  setAxisZero(axis) {
    if (this.client) return this._fire(this.client.axisZero(axis));
  }

  setAxisPosDu(axis, posDu) {
    if (this.client) return this._fire(this.client.axisPosDu(axis, posDu));
  }

  setAxisLimitDu(axis, isLeftOrUp, limitDu) {
    const upper = String(axis || '').toUpperCase();
    const boundToken = upper === 'X' ? (isLeftOrUp ? 'UP' : 'DOWN') : (isLeftOrUp ? 'LEFT' : 'RIGHT');
    if (this.client) return this._fire(this.client.axisLimitDu(upper, boundToken, limitDu));
  }

  clearAxisLimit(axis, isLeftOrUp) {
    const upper = String(axis || '').toUpperCase();
    const boundToken = upper === 'X' ? (isLeftOrUp ? 'UP' : 'DOWN') : (isLeftOrUp ? 'LEFT' : 'RIGHT');
    if (this.client) return this._fire(this.client.axisClearLimit(upper, boundToken));
  }

  setControllerOn(isOn) {
    if (!isOn) {
      return this.stopCycle();
    }
  }

  queryState(domains = 'ALL') {
    if (this.client) return this._fire(this.client.getState(domains));
  }

  getSettings(key = 'ALL') {
    return new Promise((resolve, reject) => {
      const txnId = Math.random().toString(36).substring(2, 9);
      const timer = setTimeout(() => {
        const idx = this._settingsTransactions.findIndex((t) => t.id === txnId);
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

      this.sendRaw(`SETTINGS.get=${key}\n`);
    });
  }

  setSetting(key, val) {
    if (this.client) return this.client.setSetting(key, val);
    return Promise.reject(new Error('Client not initialized'));
  }

  nudgeFeed(dir) {
    if (this.client) return this._fire(this.client.feedNudge(dir));
  }

  discoverModes() {
    if (this.client) return this._fire(this.client.getModes());
  }

  simEncoder(active) {
    if (this.client) return this._fire(this.client.simEncoder(active));
  }

  simRpm(rpm) {
    if (this.client) return this._fire(this.client.simRpm(rpm));
  }

  simPulses(pulses) {
    if (this.client) return this._fire(this.client.simPulses(pulses));
  }

  setLogStatusPushes(enabled) {
    this.logStatusPushes = !!enabled;
  }

  // --- Snapshot & Auxiliary Line Handling ---

  _syncFromSnapshot(snapshot) {
    const { kinematics, intent, cycle, properties } = snapshot;
    Object.assign(this.canonical, kinematics, intent, cycle);
    // Preserve client-local measure & jogStepDu unless explicitly updated by properties
    const localMeasure = this.canonical.measure;
    const localJogStep = this.canonical.jogStepDu;
    Object.assign(this.canonical, {
      abs: properties.abs,
      zDro: properties.zDro,
      xDro: properties.xDro,
      screwZDu: properties.screwZDu,
      screwXDu: properties.screwXDu,
      motorStepsZ: properties.motorStepsZ,
      motorStepsX: properties.motorStepsX,
      encoderPpr: properties.encoderPpr
    });
    if (localMeasure !== undefined) this.canonical.measure = localMeasure;
    if (localJogStep !== undefined) this.canonical.jogStepDu = localJogStep;
    this.canonical.on = kinematics.isOn;

    this._updateDerivedState();
    this._emitTelemetry();
  }

  _updateDerivedState() {
    this.state.z = ((this.canonical.zDu || 0) / 10000.0).toFixed(3);
    this.state.x = ((this.canonical.xDu || 0) / 10000.0).toFixed(3);
    this.state.rpm = Math.round(this.canonical.rpm || 0);
    this.state.turns = String(this.canonical.turns || 0);
    this.state.angle = String(this.canonical.angle || 0);
    this.state.status = this.canonical.on ? 'ON' : 'OFF';
    this.state.measure = this.canonical.measure === 1 ? 'IN' : (this.canonical.measure === 2 ? 'TPI' : 'MM');
    this.state.step = ((this.canonical.jogStepDu || 10000) / 10000.0).toFixed(3);
  }

  _emitTelemetry() {
    this._emit('telemetry', { ...this.state, canonical: { ...this.canonical } });
    if (this.logStatusPushes) {
      const summary = `Z: ${this.state.z} mm | X: ${this.state.x} mm | ${this.state.rpm} RPM | ${this.state.status}`;
      this._logPacket('RX', summary, { ...this.state }, '');
    }
  }

  _handleAuxiliaryLine(rawLine) {
    if (typeof rawLine !== 'string') return;
    const line = rawLine.trim();
    if (!line) return;

    if (line.startsWith('UI.')) {
      const idx = line.indexOf('=');
      if (idx !== -1) {
        const key = line.substring(3, idx);
        if (key !== 'dia' && key !== 'diameter' && key !== 'measure' && key !== 'step' && key in this.state) {
          this.state[key] = line.substring(idx + 1);
          this._emit('telemetry', { ...this.state, canonical: { ...this.canonical } });
        }
      }
      return;
    }

    if (line.startsWith('SYS.') || line.startsWith('SYSINFO.')) {
      const prefixLen = line.startsWith('SYSINFO.') ? 8 : 4;
      const sub = line.substring(prefixLen);
      if (sub.startsWith('version=')) {
        this.state.version = sub.substring('version='.length);
      } else if (sub.startsWith('features=')) {
        this.state.features = sub.substring('features='.length).split(',').map((f) => f.trim()).filter(Boolean);
        this._emit('features', this.state.features);
      }
      this._log('RX', line, 'Hardware Info');
      return;
    }

    if (line.startsWith('WEBUI.warning=')) {
      this._emit('warning', line.substring(14));
      this._log('RX', line, 'Firmware Warning');
      return;
    }

    if (line.startsWith('KEY.press=')) {
      this._emit('key', Number(line.substring(10)));
      this._log('RX', line, 'Key Press Remote Event');
      return;
    }

    if (line.startsWith('SETTING.')) {
      this._log('RX', line, 'Setting Value');
      const eq = line.indexOf('=');
      if (eq !== -1) {
        const k = line.substring(8, eq);
        const v = line.substring(eq + 1);
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
        // Resolve 'get all' when batch arrives
        for (let i = this._settingsTransactions.length - 1; i >= 0; i--) {
          const txn = this._settingsTransactions[i];
          if (txn.type === 'get' && txn.key === 'all' && Object.keys(txn.results).length > 0) {
            clearTimeout(txn.timer);
            this._settingsTransactions.splice(i, 1);
            txn.resolve(txn.results);
          }
        }
      }
      return;
    }

    if (!line.startsWith('K.') && !line.startsWith('I.') && !line.startsWith('C.') && !line.startsWith('P.') && !line.startsWith('M.')) {
      this._log('RX', line, 'Controller Message');
    }
  }

  // --- Logger & Dispatcher ---

  _logPacket(direction, summary, data, raw) {
    this._emit('traffic', {
      time: new Date(),
      direction,
      type: 'telemetry_packet',
      summary,
      data,
      raw,
      isTelemetry: true
    });
  }

  _log(direction, raw, annotation = '', isTelemetry = false) {
    this._emit('traffic', {
      time: new Date(),
      direction,
      type: 'standard',
      raw,
      annotation,
      isTelemetry
    });
  }

  _emit(eventName, detail) {
    this.dispatchEvent(new CustomEvent(eventName, { detail }));
  }
}

if (typeof globalThis !== 'undefined') {
  globalThis.WhatelsService = WhatelsService;
}
if (typeof window !== 'undefined') {
  window.WhatelsService = WhatelsService;
}
