import test from 'node:test';
import assert from 'node:assert/strict';

import { ClientTransport } from '../../clients/sdk/js/transports/client-transport.js';
import { WebSocketTransport } from '../../clients/sdk/js/transports/websocket-transport.js';
import { EmulatorTransport } from '../../clients/sdk/js/transports/emulator-transport.js';
import {
  WhatelsClient,
  ControllerError,
  CommandTimeoutError,
  TransportDisconnectedError
} from '../../clients/sdk/js/whatels-client.js';

test('ClientTransport base class invariants', async (t) => {
  const transport = new ClientTransport();
  assert.equal(transport.status, 'disconnected');
  assert.equal(transport.isConnected(), false);

  await assert.rejects(async () => transport.connect(), /must be implemented by subclass/);
  await assert.rejects(async () => transport.disconnect(), /must be implemented by subclass/);
  await assert.rejects(async () => transport.send('TEST\n'), /must be implemented by subclass/);

  let receivedLine = null;
  transport.addEventListener('line', (e) => {
    receivedLine = e.detail;
  });
  transport._emitLine('K.zDu=12345');
  assert.equal(receivedLine, 'K.zDu=12345');

  let receivedStatus = null;
  transport.addEventListener('status', (e) => {
    receivedStatus = e.detail;
  });
  transport._emitStatus('connecting', { host: 'whatels.local' });
  assert.equal(transport.status, 'connecting');
  assert.equal(receivedStatus.status, 'connecting');
  assert.equal(receivedStatus.host, 'whatels.local');

  let receivedError = null;
  transport.addEventListener('error', (e) => {
    receivedError = e.detail;
  });
  const testErr = new Error('Socket fault');
  transport._emitError(testErr);
  assert.equal(receivedError, testErr);
});

test('EmulatorTransport wraps emulator and routes lines', async (t) => {
  class MockEmulator {
    constructor() {
      this.sent = [];
      this.stopped = false;
      this.onmessage = null;
    }
    send(data) {
      this.sent.push(data);
    }
    stop() {
      this.stopped = true;
    }
  }

  const mockEmu = new MockEmulator();
  const transport = new EmulatorTransport({ emulator: mockEmu });

  const statusEvents = [];
  transport.addEventListener('status', (e) => statusEvents.push(e.detail.status));

  await transport.connect();
  assert.equal(transport.status, 'connected');
  assert.equal(transport.isConnected(), true);
  assert.deepEqual(statusEvents, ['connected']);

  const lines = [];
  transport.addEventListener('line', (e) => lines.push(e.detail));

  // Emulator produces message
  mockEmu.onmessage({ data: 'K.rpm=1200\n' });
  assert.deepEqual(lines, ['K.rpm=1200']);

  // Send line through transport
  await transport.send('STATE.get=ALL');
  assert.deepEqual(mockEmu.sent, ['STATE.get=ALL\n']);

  await transport.disconnect();
  assert.equal(transport.status, 'disconnected');
  assert.equal(transport.isConnected(), false);
  assert.equal(mockEmu.stopped, true);
});

test('WebSocketTransport connection, line framing, and exponential backoff schedule', async (t) => {
  // Mock WebSocket
  class MockWebSocket extends EventTarget {
    static OPEN = 1;
    static CLOSED = 3;
    constructor(url) {
      super();
      this.url = url;
      this.readyState = 0; // CONNECTING
      this.sent = [];
      MockWebSocket.instances.push(this);
    }
    send(data) {
      this.sent.push(data);
    }
    close() {
      this.readyState = MockWebSocket.CLOSED;
      if (typeof this.onclose === 'function') this.onclose({ code: 1000 });
      this.dispatchEvent(new Event('close'));
    }
  }
  MockWebSocket.instances = [];

  const originalWS = globalThis.WebSocket;
  globalThis.WebSocket = MockWebSocket;

  try {
    const transport = new WebSocketTransport({
      host: 'test.local',
      port: 81,
      path: '/canonical',
      autoReconnect: true
    });

    assert.equal(transport.status, 'disconnected');

    const connectPromise = transport.connect();
    assert.equal(transport.status, 'connecting');
    assert.equal(MockWebSocket.instances.length, 1);
    const ws1 = MockWebSocket.instances[0];
    assert.equal(ws1.url, 'ws://test.local:81/canonical');

    // Simulate open
    ws1.readyState = MockWebSocket.OPEN;
    if (typeof ws1.onopen === 'function') ws1.onopen({});
    ws1.dispatchEvent(new Event('open'));

    await connectPromise;
    assert.equal(transport.status, 'connected');
    assert.equal(transport.isConnected(), true);

    // Test line reception & newline splitting
    const rxLines = [];
    transport.addEventListener('line', (e) => rxLines.push(e.detail));

    ws1.onmessage({ data: 'K.zDu=1000\r\nK.xDu=2000\n' });
    assert.deepEqual(rxLines, ['K.zDu=1000', 'K.xDu=2000']);

    // Test send ensures single \n
    await transport.send('CMD.stop');
    assert.deepEqual(ws1.sent, ['CMD.stop\n']);

    await transport.send('CMD.startFeed=axis:Z\n');
    assert.deepEqual(ws1.sent, ['CMD.stop\n', 'CMD.startFeed=axis:Z\n']);

    // Test exponential backoff calculation
    assert.equal(transport._getReconnectDelay(0), 1000);
    assert.equal(transport._getReconnectDelay(1), 2000);
    assert.equal(transport._getReconnectDelay(2), 4000);
    assert.equal(transport._getReconnectDelay(3), 8000);
    assert.equal(transport._getReconnectDelay(4), 15000);
    assert.equal(transport._getReconnectDelay(5), 15000);

    // Disconnect
    await transport.disconnect();
    assert.equal(transport.status, 'disconnected');
    assert.equal(transport.isConnected(), false);
  } finally {
    globalThis.WebSocket = originalWS;
  }
});

test('WhatelsService delegates I/O through WhatelsClient & ClientTransport', async (t) => {
  await import('../../clients/web_ui/whatels-service.js');
  const ServiceClass = globalThis.WhatelsService;
  assert.ok(ServiceClass, 'WhatelsService should be defined');

  class MockTestTransport extends ClientTransport {
    constructor() {
      super();
      this.sent = [];
    }
    async connect() {
      this._emitStatus('connected');
    }
    async disconnect() {
      this._emitStatus('disconnected');
    }
    async send(line) {
      this.sent.push(line);
      if (line.startsWith('CLIENT.hello=')) {
        queueMicrotask(() => {
          this._emitLine('OK:CLIENT.hello:session_id=7,type=webui,controllerVer=H5V38,perms=0x000F');
        });
      } else if (line.startsWith('CMD.stop')) {
        queueMicrotask(() => {
          this._emitLine('OK:CMD.stop');
        });
      }
    }
  }

  const mockTransport = new MockTestTransport();
  const service = new ServiceClass({ transport: mockTransport });

  await service.connect();
  assert.equal(service.state.connected, true);
  assert.equal(service.client.sessionId, 7);

  // Send raw command
  service.sendRaw('CMD.stop\n');
  assert.ok(mockTransport.sent.some((l) => l.startsWith('CLIENT.hello=')));
  assert.ok(mockTransport.sent.includes('STATE.get=ALL\n'));
  assert.ok(mockTransport.sent.includes('SYS.get=info\n'));
  assert.ok(mockTransport.sent.includes('CMD.stop\n'));

  // startCycle must NOT emit CMD.on=1
  const beforeCount = mockTransport.sent.length;
  service.startCycle(['CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK']);
  const afterLines = mockTransport.sent.slice(beforeCount);
  assert.deepEqual(afterLines, ['CMD.startFeed=axis:Z,sync:1,feed:15000,dir:CHUCK\n']);
  assert.equal(afterLines.some((l) => l.includes('CMD.on=')), false);

  // cycleMeasure and cycleStep are purely client-local
  const txCountBeforeLocal = mockTransport.sent.length;
  service.cycleMeasure();
  assert.equal(service.state.measure, 'IN');
  service.cycleStep();
  assert.equal(service.canonical.jogStepDu, 1000);
  assert.equal(mockTransport.sent.length, txCountBeforeLocal);

  // Handle incoming line
  let telemetryFired = false;
  service.addEventListener('telemetry', () => {
    telemetryFired = true;
  });
  mockTransport._emitLine('K.zDu=-250000');
  assert.equal(telemetryFired, true);
  assert.equal(service.state.z, '-25.000');

  service.disconnect();
  assert.equal(service.state.connected, false);
});

test('WhatelsClient handshake, OK/ERR correlation, timeout, jog watchdog, and teardown', async (t) => {
  class LoopbackMockTransport extends ClientTransport {
    constructor() {
      super();
      this.sent = [];
      this.autoAckHello = true;
    }
    async connect() {
      this._emitStatus('connected');
    }
    async disconnect() {
      this._emitStatus('disconnected');
    }
    async send(line) {
      this.sent.push(line);
      if (this.autoAckHello && line.startsWith('CLIENT.hello=')) {
        queueMicrotask(() => {
          this._emitLine('OK:CLIENT.hello:session_id=42,type=webui,controllerVer=H5V38,perms=0x001F');
        });
      }
    }
  }

  const transport = new LoopbackMockTransport();
  const client = new WhatelsClient(transport, { timeoutMs: 60, handshakeTimeoutMs: 60 });

  const connEvents = [];
  client.addEventListener('connection', (e) => connEvents.push(e.detail));

  await client.connect();
  assert.equal(client.connected, true);
  assert.equal(client.sessionId, 42);
  assert.equal(client.perms, 0x001F);
  assert.equal(connEvents.at(-1).connected, true);
  assert.equal(transport.sent[0].startsWith('CLIENT.hello=type:webui,ver:1.0.0,label:'), true);
  assert.equal(transport.sent[1], 'STATE.get=ALL\n');

  // OK correlation + canonical command formatting
  const feedPromise = client.startFeed({ axis: 'ZX', sync: 1, feed: 10000, dir: 'CHUCK', xdir: 'RETRACT', taper: 1000, dist: 50000 });
  assert.equal(transport.sent.at(-1), 'CMD.startFeed=axis:ZX,sync:1,feed:10000,dir:CHUCK,xdir:RETRACT,taper:1000,dist:50000\n');
  transport._emitLine('OK:CMD.startFeed');
  await feedPromise;

  // ERR correlation -> ControllerError
  const nudgePromise = client.feedNudge(-1);
  assert.equal(transport.sent.at(-1), 'CMD.feedNudge=-1\n');
  transport._emitLine('ERR:CMD.feedNudge:MOTION_LOCKED');
  await assert.rejects(nudgePromise, (err) => err instanceof ControllerError && err.reason === 'MOTION_LOCKED');

  // Timeout -> CommandTimeoutError
  const timeoutPromise = client.getCatalog('path=threads');
  assert.equal(transport.sent.at(-1), 'PROPERTIES.catalog=path=threads\n');
  await assert.rejects(timeoutPromise, (err) => err instanceof CommandTimeoutError);

  // Catalog & profile commands
  const saveP = client.catalogSave({ domain: 'threads', id: 'custom_m12' });
  assert.ok(transport.sent.at(-1).startsWith('SETTINGS.catalogSave='));
  transport._emitLine('OK:SETTINGS.catalogSave');
  await saveP;

  const delP = client.catalogDelete({ domain: 'threads', id: 'custom_m12' });
  assert.ok(transport.sent.at(-1).startsWith('SETTINGS.catalogDelete='));
  transport._emitLine('OK:SETTINGS.catalogDelete');
  await delP;

  const profP = client.setProfile('wm210v');
  assert.equal(transport.sent.at(-1), 'SETTINGS.set=profile:wm210v\n');
  transport._emitLine('OK:SETTINGS.set');
  await profP;

  // Log multiplexing & telemetry
  const logs = [];
  client.addEventListener('log', (e) => logs.push(e.detail));
  transport._emitLine('# Diagnostic comment');
  assert.deepEqual(logs, ['# Diagnostic comment']);

  const snapshots = [];
  client.addEventListener('snapshot', (e) => snapshots.push(e.detail));
  transport._emitLine('K.zDu=12345|I.direction=-1|I.targetDistDu=50000');
  assert.equal(snapshots.length, 1);
  assert.equal(snapshots[0].snapshot.kinematics.zDu, 12345);
  assert.equal(snapshots[0].snapshot.intent.direction, -1);
  assert.equal(snapshots[0].snapshot.intent.targetDistDu, 50000);

  // 200ms jog heartbeat watchdog
  const jogP = client.jogHold('Z', 'CHUCK');
  transport._emitLine('OK:CMD.jogHold');
  await jogP;
  await new Promise((r) => setTimeout(r, 460));
  const jogHolds = transport.sent.filter((l) => l === 'CMD.jogHold=Z,CHUCK\n');
  assert.ok(jogHolds.length >= 2, `Expected >= 2 jogHold heartbeats, got ${jogHolds.length}`);

  // Unconditional disconnect teardown cancels timers & rejects pending promises
  const pendingStop = client.stop();
  await transport.disconnect();
  await assert.rejects(pendingStop, (err) => err instanceof TransportDisconnectedError);
  assert.equal(client.connected, false);
  assert.equal(client.sessionId, null);

  // Handshake timeout drops transport
  transport.autoAckHello = false;
  await assert.rejects(client.connect(), (err) => err instanceof CommandTimeoutError);
  assert.equal(transport.isConnected(), false);
});

test('WhatelsEmulator handles CLIENT.hello, STATE.get=ALL, PROPERTIES.modes arrays, and sim* commands', async (t) => {
  await import('../../clients/web_ui/whatels-emulator.js');
  const EmuClass = globalThis.WhatelsEmulator;
  assert.ok(EmuClass, 'WhatelsEmulator should be defined');

  const emu = new EmuClass();
  const transport = new EmulatorTransport({ emulator: emu });
  const client = new WhatelsClient(transport);

  try {
    await client.connect();
    assert.equal(client.connected, true);
    assert.equal(client.sessionId, 1);

    const modesJson = await client.getModes();
    const modesObj = JSON.parse(modesJson);
    assert.ok(Array.isArray(modesObj.modes[0].required_hardware));
    assert.ok(Array.isArray(modesObj.modes[0].live_permissions));
    assert.equal(typeof modesObj.modes[0].required_hardware[0], 'string');

    await client.simEncoder(1);
    await client.simRpm(600);
    await client.simPulses(120);
    await client.startFeed({ axis: 'Z', sync: 1, feed: 15000, dir: 'CHUCK', dist: 20000 });
    await client.stop();
  } finally {
    await client.disconnect();
  }
});

test('Transports expose standard type property and handle streaming chunks and errors', async (t) => {
  const base = new ClientTransport();
  assert.equal(base.type, 'base');

  const ws = new WebSocketTransport();
  assert.equal(ws.type, 'websocket');

  // Emulator chunking & type
  class ChunkingEmulator {
    constructor() {
      this.onmessage = null;
    }
    start() {}
    stop() {}
  }
  const emu = new ChunkingEmulator();
  const emuTransport = new EmulatorTransport({ emulator: emu });
  assert.equal(emuTransport.type, 'emulator');

  await emuTransport.connect();

  const lines = [];
  emuTransport.addEventListener('line', (e) => lines.push(e.detail));

  // Deliver partial chunks
  emu.onmessage({ data: 'K.zDu=123' });
  assert.deepEqual(lines, []); // Not complete yet

  emu.onmessage({ data: '45\nK.xDu=678' });
  assert.deepEqual(lines, ['K.zDu=12345']);

  emu.onmessage({ data: '90\n' });
  assert.deepEqual(lines, ['K.zDu=12345', 'K.xDu=67890']);

  await emuTransport.disconnect();

  // Test error propagation on emulator start failure
  class FaultyEmulator {
    start() {
      throw new Error('Hardware init failure');
    }
  }
  const faultTransport = new EmulatorTransport({ emulator: new FaultyEmulator() });
  let caughtError = null;
  faultTransport.addEventListener('error', (e) => {
    caughtError = e.detail;
  });

  await assert.rejects(async () => {
    await faultTransport.connect();
  }, /Hardware init failure/);

  assert.ok(caughtError);
  assert.match(caughtError.message, /Hardware init failure/);
});

