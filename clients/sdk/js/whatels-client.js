/**
 * whatELS Controller Client SDK — Universal Protocol Client (Tier 2)
 * Authoritative Spec: docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md
 * Wire Reference: docs/protocol/CANONICAL_COMMAND_REFERENCE.md
 *
 * Vanilla ES module + globalThis.WhatelsClient (zero bundler).
 */

export class ControllerError extends Error {
  constructor(message, command = '', reason = '') {
    super(message);
    this.name = 'ControllerError';
    this.command = command;
    this.reason = reason;
  }
}

export class CommandTimeoutError extends Error {
  constructor(command, timeoutMs) {
    super(`Command timeout after ${timeoutMs}ms: ${command}`);
    this.name = 'CommandTimeoutError';
    this.command = command;
    this.timeoutMs = timeoutMs;
  }
}

export class TransportDisconnectedError extends Error {
  constructor(message = 'Transport disconnected') {
    super(message);
    this.name = 'TransportDisconnectedError';
  }
}

function createDefaultSnapshot() {
  return {
    kinematics: {
      zDu: 0,
      xDu: 0,
      rpm: 0,
      turns: 0,
      angle: 0,
      mode: 0,
      isOn: false,
      estop: false,
      perms: 0,
      zLeftDu: null,
      zRightDu: null,
      xUpDu: null,
      xDownDu: null,
      zDis: false,
      xDis: false,
      dupr: 0,
      nextDupr: 0
    },
    intent: {
      thPitch: 0,
      thFmt: 0,
      tpVal: 0,
      tpMode: 0,
      coneRatio: 0,
      feedStepDu: 500,
      jogStepDu: 10000,
      feedRev: false,
      direction: 1,
      targetDistDu: -1
    },
    cycle: {
      passes: 1,
      opIdx: 0,
      setupIdx: 0,
      starts: 1
    },
    properties: {
      measure: 0,
      dia: false,
      abs: true,
      zDro: false,
      xDro: false,
      screwZDu: 20000,
      screwXDu: 20000,
      motorStepsZ: 800,
      motorStepsX: 800,
      encoderPpr: 1200
    }
  };
}

function normalizeAxis(axis) {
  const ax = String(axis || '').trim().toUpperCase();
  if (ax !== 'Z' && ax !== 'X' && ax !== 'ZX') {
    throw new Error(`Invalid axis: ${axis}`);
  }
  return ax;
}

function normalizeJogDir(axis, dir) {
  const ax = normalizeAxis(axis);
  const num = Number(dir);
  const isNum = !Number.isNaN(num) && dir !== '' && typeof dir !== 'boolean';

  if (ax === 'Z') {
    if (isNum) return num < 0 ? 'CHUCK' : 'TAIL';
    const token = String(dir || '').trim().toUpperCase();
    if (token === 'CHUCK' || token === 'LEFT' || token === '-') return 'CHUCK';
    if (token === 'TAIL' || token === 'RIGHT' || token === '+') return 'TAIL';
  } else if (ax === 'X') {
    if (isNum) return num < 0 ? 'INFEED' : 'RETRACT';
    const token = String(dir || '').trim().toUpperCase();
    if (token === 'INFEED' || token === 'IN' || token === 'UP' || token === 'CENTER' || token === '-') return 'INFEED';
    if (token === 'RETRACT' || token === 'OUT' || token === 'DOWN' || token === '+') return 'RETRACT';
  }
  throw new Error(`Invalid jog direction "${dir}" for axis ${ax}`);
}

function normalizeFeedDir(axis, dir) {
  const ax = normalizeAxis(axis);
  const num = Number(dir);
  const isNum = !Number.isNaN(num) && dir !== '' && typeof dir !== 'boolean';
  if (isNum) return num < 0 ? '-1' : '+1';

  const token = String(dir || '').trim().toUpperCase();
  if (token === '-' || token === '+' || token === '-1' || token === '+1') return token;
  if (ax === 'Z' || ax === 'ZX') {
    if (token === 'CHUCK' || token === 'LEFT' || token === 'TAIL' || token === 'RIGHT') return token;
  }
  if (ax === 'X') {
    if (token === 'CENTER' || token === 'IN' || token === 'INFEED' || token === 'UP' ||
        token === 'RETRACT' || token === 'OUT' || token === 'DOWN') {
      return token;
    }
  }
  throw new Error(`Invalid feed direction "${dir}" for axis ${ax}`);
}

function isDiagnosticLine(line) {
  return (
    line.startsWith('#') ||
    line.startsWith('[I]') ||
    line.startsWith('[W]') ||
    line.startsWith('[E]') ||
    line.startsWith('ESP-ROM:')
  );
}

export class WhatelsClient extends EventTarget {
  constructor(transport = null, options = {}) {
    super();
    this.transport = null;
    this.clientType = options.clientType || 'webui';
    this.clientVer = options.clientVer || '1.0.0';
    this.label = options.label || 'Web UI Touch DRO';
    this.timeoutMs = options.timeoutMs || 3000;
    this.handshakeTimeoutMs = options.handshakeTimeoutMs || 2000;
    this.sessionId = null;
    this.perms = 0;
    this.controllerVer = null;
    this.connected = false;
    this.snapshot = createDefaultSnapshot();

    this._pending = [];
    this._jogTimers = new Map();
    this._transportCleanup = null;

    if (transport) {
      this.setTransport(transport);
    }
  }

  setTransport(transport) {
    if (this._transportCleanup) {
      this._transportCleanup();
      this._transportCleanup = null;
    }
    this._teardown('Transport replaced');
    this.transport = transport || null;
    if (!this.transport) return;

    const onLine = (ev) => this._onLine(ev.detail);
    const onStatus = (ev) => {
      const detail = ev.detail || {};
      if (detail.status === 'disconnected') {
        this._teardown('Transport disconnected');
      } else if (detail.status === 'connecting') {
        this.dispatchEvent(new CustomEvent('connection', {
          detail: { connected: false, sessionId: null, status: 'connecting' }
        }));
      }
    };
    const onError = (ev) => {
      const err = ev.detail instanceof Error ? ev.detail : new Error(String(ev.detail || 'Transport error'));
      this.dispatchEvent(new CustomEvent('error', { detail: err }));
    };

    this.transport.addEventListener('line', onLine);
    this.transport.addEventListener('status', onStatus);
    this.transport.addEventListener('error', onError);

    this._transportCleanup = () => {
      this.transport.removeEventListener('line', onLine);
      this.transport.removeEventListener('status', onStatus);
      this.transport.removeEventListener('error', onError);
    };
  }

  async connect(config = {}) {
    if (!this.transport) {
      throw new Error('Cannot connect: no transport configured');
    }

    if (!this.transport.isConnected()) {
      await this.transport.connect(config);
    }

    const helloLine = `CLIENT.hello=type:${this.clientType},ver:${this.clientVer},label:"${this.label}"`;
    let ackPayload = '';
    try {
      ackPayload = await this._sendCommand(helloLine, 'CLIENT.hello', this.handshakeTimeoutMs);
    } catch (err) {
      await this.transport.disconnect().catch(() => {});
      this._teardown('Handshake failed');
      this.dispatchEvent(new CustomEvent('error', { detail: err }));
      throw err;
    }

    this._parseHelloAck(ackPayload);
    this.connected = true;

    await this.transport.send('STATE.get=ALL\n');

    this.dispatchEvent(new CustomEvent('connection', {
      detail: {
        connected: true,
        sessionId: this.sessionId,
        status: 'connected',
        perms: this.perms,
        controllerVer: this.controllerVer
      }
    }));
  }

  async disconnect() {
    this._teardown('Client disconnected');
    if (this.transport && this.transport.isConnected()) {
      await this.transport.disconnect();
    }
  }

  // --- Strongly-Typed Canonical Commands ---

  async startFeed({ axis, sync = 1, feed, dir, xdir, taper, dist = -1 }) {
    const ax = normalizeAxis(axis);
    if (feed === undefined || feed === null || Number(feed) <= 0 || Number.isNaN(Number(feed))) {
      throw new Error(`Invalid feed rate: ${feed}`);
    }
    const syncFlag = (sync === false || sync === 0 || sync === '0') ? 0 : 1;
    const dirToken = normalizeFeedDir(ax, dir);

    const parts = [
      `axis:${ax}`,
      `sync:${syncFlag}`,
      `feed:${Math.round(Number(feed))}`,
      `dir:${dirToken}`
    ];

    if (ax === 'ZX') {
      if (xdir === undefined || xdir === null) {
        throw new Error('xdir is mandatory when axis is ZX');
      }
      if (taper === undefined || taper === null || Number(taper) <= 0 || Number.isNaN(Number(taper))) {
        throw new Error('taper (> 0) is mandatory when axis is ZX');
      }
      parts.push(`xdir:${normalizeFeedDir('X', xdir)}`);
      parts.push(`taper:${Math.round(Number(taper))}`);
    }

    if (dist !== undefined && dist !== null && Number(dist) !== -1) {
      parts.push(`dist:${Math.round(Number(dist))}`);
    }

    return this._sendCommand(`CMD.startFeed=${parts.join(',')}`, 'CMD.startFeed');
  }

  async stop() {
    this._clearAllJogTimers();
    return this._sendCommand('CMD.stop', 'CMD.stop');
  }

  async hold() {
    return this._sendCommand('CMD.hold', 'CMD.hold');
  }

  async resume() {
    return this._sendCommand('CMD.resume', 'CMD.resume');
  }

  async step() {
    return this._sendCommand('CMD.step', 'CMD.step');
  }

  async feedNudge(dir) {
    const token = (dir === '-' || Number(dir) < 0) ? '-1' : '+1';
    return this._sendCommand(`CMD.feedNudge=${token}`, 'CMD.feedNudge');
  }

  async jogHold(axis, dir) {
    const ax = normalizeAxis(axis);
    if (ax === 'ZX') throw new Error('jogHold supports single axis Z or X');
    const dirToken = normalizeJogDir(ax, dir);
    const line = `CMD.jogHold=${ax},${dirToken}`;

    this._clearJogTimer(ax);
    const timer = setInterval(() => {
      if (this.transport && this.transport.isConnected()) {
        this.transport.send(`${line}\n`).catch(() => {});
      } else {
        this._clearJogTimer(ax);
      }
    }, 200);
    this._jogTimers.set(ax, { timer, line });

    return this._sendCommand(line, 'CMD.jogHold');
  }

  async jogRelease(axis) {
    if (axis !== undefined && axis !== null && axis !== '') {
      const ax = normalizeAxis(axis);
      this._clearJogTimer(ax);
      return this._sendCommand(`CMD.jogRelease=${ax}`, 'CMD.jogRelease');
    }
    this._clearAllJogTimers();
    return this._sendCommand('CMD.jogRelease', 'CMD.jogRelease');
  }

  async jogStep(axis, distDu) {
    const ax = normalizeAxis(axis);
    if (ax === 'ZX') throw new Error('jogStep supports single axis Z or X');
    const du = Math.round(Number(distDu));
    if (!du || Number.isNaN(du)) {
      throw new Error(`Invalid jogStep distance: ${distDu}`);
    }
    return this._sendCommand(`CMD.jogStep=${ax},${du}`, 'CMD.jogStep');
  }

  async axisZero(axis) {
    const ax = normalizeAxis(axis);
    return this._sendCommand(`CMD.axisZero=${ax}`, 'CMD.axisZero');
  }

  async axisPosDu(axis, posDu) {
    const ax = normalizeAxis(axis);
    return this._sendCommand(`CMD.axisPosDu=${ax},${Math.round(Number(posDu))}`, 'CMD.axisPosDu');
  }

  async axisLimitDu(axis, side, du) {
    const ax = normalizeAxis(axis);
    const bound = String(side || '').trim().toUpperCase();
    return this._sendCommand(`CMD.axisLimitDu=${ax},${bound},${Math.round(Number(du))}`, 'CMD.axisLimitDu');
  }

  async axisClearLimit(axis, side) {
    const ax = normalizeAxis(axis);
    const bound = String(side || '').trim().toUpperCase();
    return this._sendCommand(`CMD.axisClearLimit=${ax},${bound}`, 'CMD.axisClearLimit');
  }

  async getState(domains = 'ALL') {
    await this.sendRaw(`STATE.get=${domains}`);
  }

  async getModes(all = false) {
    const cmd = all ? 'PROPERTIES.modes=all' : 'PROPERTIES.modes';
    return this._sendCommand(cmd, 'PROPERTIES.modes');
  }

  async getCatalog(query = '') {
    const cmd = query ? `PROPERTIES.catalog=${query}` : 'PROPERTIES.catalog';
    return this._sendCommand(cmd, 'PROPERTIES.catalog');
  }

  async getSetting(key) {
    const cmd = key ? `SETTINGS.get=${key}` : 'SETTINGS.get';
    return this._sendCommand(cmd, 'SETTINGS.get');
  }

  async setSetting(key, value) {
    return this._sendCommand(`SETTINGS.set=${key}:${value}`, 'SETTINGS.set');
  }

  async setProfile(profileId) {
    return this.setSetting('profile', profileId);
  }

  async catalogSave(entry) {
    const payload = typeof entry === 'string' ? entry : JSON.stringify(entry);
    return this._sendCommand(`SETTINGS.catalogSave=${payload}`, 'SETTINGS.catalogSave');
  }

  async catalogDelete(entry) {
    const payload = typeof entry === 'string' ? entry : JSON.stringify(entry);
    return this._sendCommand(`SETTINGS.catalogDelete=${payload}`, 'SETTINGS.catalogDelete');
  }

  async simEncoder(active) {
    const flag = (active === true || active === 1 || active === '1') ? 1 : 0;
    return this._sendCommand(`CMD.simEncoder=${flag}`, 'CMD.simEncoder');
  }

  async simRpm(rpm) {
    return this._sendCommand(`CMD.simRpm=${Number(rpm)}`, 'CMD.simRpm');
  }

  async simPulses(pulses) {
    return this._sendCommand(`CMD.simPulses=${Math.round(Number(pulses))}`, 'CMD.simPulses');
  }

  async sendRaw(line) {
    if (!this.transport || !this.transport.isConnected()) {
      throw new TransportDisconnectedError('Cannot send: transport is not connected');
    }
    const wire = line.endsWith('\n') ? line : `${line}\n`;
    await this.transport.send(wire);
  }

  // --- Internal Transaction & Telemetry Handling ---

  _sendCommand(line, commandName, timeoutMs = this.timeoutMs) {
    if (!this.transport || !this.transport.isConnected()) {
      return Promise.reject(new TransportDisconnectedError(`Cannot send ${commandName}: transport not connected`));
    }

    return new Promise((resolve, reject) => {
      const txn = {
        commandName,
        resolve,
        reject,
        timer: null,
        settled: false
      };

      txn.timer = setTimeout(() => {
        if (txn.settled) return;
        txn.settled = true;
        this._removePending(txn);
        reject(new CommandTimeoutError(commandName, timeoutMs));
      }, timeoutMs);

      this._pending.push(txn);

      const wire = line.endsWith('\n') ? line : `${line}\n`;
      this.transport.send(wire).catch((err) => {
        if (txn.settled) return;
        txn.settled = true;
        clearTimeout(txn.timer);
        this._removePending(txn);
        reject(err instanceof Error ? err : new Error(String(err)));
      });
    });
  }

  _removePending(txn) {
    const idx = this._pending.indexOf(txn);
    if (idx !== -1) this._pending.splice(idx, 1);
  }

  _clearJogTimer(axis) {
    const entry = this._jogTimers.get(axis);
    if (entry) {
      clearInterval(entry.timer);
      this._jogTimers.delete(axis);
    }
  }

  _clearAllJogTimers() {
    for (const entry of this._jogTimers.values()) {
      clearInterval(entry.timer);
    }
    this._jogTimers.clear();
  }

  _teardown(reason) {
    this._clearAllJogTimers();
    const pending = this._pending.splice(0);
    for (const txn of pending) {
      if (txn.settled) continue;
      txn.settled = true;
      clearTimeout(txn.timer);
      txn.reject(new TransportDisconnectedError(reason));
    }

    const wasConnected = this.connected || this.sessionId !== null;
    this.connected = false;
    this.sessionId = null;
    this.perms = 0;
    this.controllerVer = null;

    if (wasConnected) {
      this.dispatchEvent(new CustomEvent('connection', {
        detail: { connected: false, sessionId: null, status: 'disconnected' }
      }));
    }
  }

  _parseHelloAck(payload) {
    if (!payload) return;
    const pairs = payload.split(',');
    for (const pair of pairs) {
      const eq = pair.indexOf('=');
      if (eq === -1) continue;
      const k = pair.substring(0, eq).trim();
      const v = pair.substring(eq + 1).trim();
      if (k === 'session_id') {
        const id = parseInt(v, 10);
        if (!Number.isNaN(id)) this.sessionId = id;
      } else if (k === 'perms') {
        const p = v.startsWith('0x') || v.startsWith('0X') ? parseInt(v, 16) : parseInt(v, 10);
        if (!Number.isNaN(p)) this.perms = p;
      } else if (k === 'controllerVer') {
        this.controllerVer = v;
      }
    }
  }

  _onLine(rawLine) {
    if (typeof rawLine !== 'string') return;
    const line = rawLine.trim();
    if (!line) return;

    if (isDiagnosticLine(line)) {
      this.dispatchEvent(new CustomEvent('log', { detail: line }));
      return;
    }

    if (this._tryResolveAck(line)) {
      return;
    }

    // Telemetry lines (supports both single-line and pipe-delimited frames)
    const segments = line.includes('|') && !line.startsWith('<') ? line.split('|') : [line];
    let domainMask = 0;
    for (const seg of segments) {
      const s = seg.trim();
      if (!s) continue;
      domainMask |= this._ingestTelemetryField(s);
    }

    if (domainMask !== 0) {
      this.dispatchEvent(new CustomEvent('snapshot', {
        detail: {
          domainMask,
          snapshot: {
            kinematics: { ...this.snapshot.kinematics },
            intent: { ...this.snapshot.intent },
            cycle: { ...this.snapshot.cycle },
            properties: { ...this.snapshot.properties }
          }
        }
      }));
    }
  }

  _tryResolveAck(line) {
    if (line.startsWith('OK:')) {
      const rest = line.substring(3);
      for (let i = 0; i < this._pending.length; i++) {
        const txn = this._pending[i];
        const prefix = txn.commandName;
        if (rest === prefix || rest.startsWith(`${prefix}:`) || rest.startsWith(`${prefix}=`)) {
          this._pending.splice(i, 1);
          txn.settled = true;
          clearTimeout(txn.timer);
          const payload = rest.length > prefix.length ? rest.substring(prefix.length + 1) : line;
          txn.resolve(payload);
          return true;
        }
      }
      return false;
    }

    if (line.startsWith('ERR:')) {
      const rest = line.substring(4);
      if (rest === 'UNKNOWN_COMMAND' && this._pending.length > 0) {
        const txn = this._pending.shift();
        txn.settled = true;
        clearTimeout(txn.timer);
        txn.reject(new ControllerError(line, txn.commandName, 'UNKNOWN_COMMAND'));
        return true;
      }

      for (let i = 0; i < this._pending.length; i++) {
        const txn = this._pending[i];
        const prefix = txn.commandName;
        if (rest === prefix || rest.startsWith(`${prefix}:`) || rest.startsWith(`${prefix}=`)) {
          this._pending.splice(i, 1);
          txn.settled = true;
          clearTimeout(txn.timer);
          const reason = rest.length > prefix.length ? rest.substring(prefix.length + 1) : 'ERROR';
          txn.reject(new ControllerError(line, prefix, reason));
          return true;
        }
      }
      return false;
    }

    // Legacy single-setting reply while SETTINGS.get is pending
    if (line.startsWith('SETTING.')) {
      const idx = this._pending.findIndex((t) => t.commandName === 'SETTINGS.get');
      if (idx !== -1) {
        const txn = this._pending[idx];
        const eq = line.indexOf('=');
        if (eq !== -1) {
          this._pending.splice(idx, 1);
          txn.settled = true;
          clearTimeout(txn.timer);
          txn.resolve(line.substring(eq + 1));
          return true;
        }
      }
    }

    return false;
  }

  _ingestTelemetryField(line) {
    const eq = line.indexOf('=');
    if (eq === -1) return 0;

    const domain = line.substring(0, 2);
    const key = line.substring(2, eq);
    const val = line.substring(eq + 1);

    if (domain === 'K.') {
      const k = this.snapshot.kinematics;
      if (key === 'on') {
        k.isOn = val === '1' || val === 'true';
        return 0x01;
      }
      if (key in k) {
        k[key] = this._coerceValue(k[key], key, val);
        return 0x01;
      }
      return 0;
    }

    if (domain === 'I.') {
      const intent = this.snapshot.intent;
      if (key in intent) {
        intent[key] = this._coerceValue(intent[key], key, val);
        return 0x02;
      }
      return 0;
    }

    if (domain === 'C.') {
      const c = this.snapshot.cycle;
      if (key in c) {
        c[key] = this._coerceValue(c[key], key, val);
        return 0x04;
      }
      return 0;
    }

    if (domain === 'P.') {
      if (key === 'dia' || key === 'diameter') return 0; // Purely client-local
      const p = this.snapshot.properties;
      if (key in p) {
        p[key] = this._coerceValue(p[key], key, val);
        return 0x08;
      }
      return 0;
    }

    return 0;
  }

  _coerceValue(existing, key, val) {
    if (typeof existing === 'boolean') {
      return val === '1' || val === 'true';
    }
    if (key === 'perms' && (val.startsWith('0x') || val.startsWith('0X'))) {
      return parseInt(val, 16);
    }
    if (key === 'zLeftDu' || key === 'zRightDu' || key === 'xUpDu' || key === 'xDownDu') {
      if (val === '' || val === 'null' || val === 'undefined') return null;
      const n = Number(val);
      return Number.isNaN(n) ? null : n;
    }
    const num = Number(val);
    return Number.isNaN(num) ? val : num;
  }
}

if (typeof globalThis !== 'undefined') {
  globalThis.WhatelsClient = WhatelsClient;
}
