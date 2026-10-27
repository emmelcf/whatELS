import test from 'node:test';
import assert from 'node:assert/strict';

import { ClientTransport } from '../../clients/sdk/js/transports/client-transport.js';
import { WebSocketTransport } from '../../clients/sdk/js/transports/websocket-transport.js';
import { EmulatorTransport } from '../../clients/sdk/js/transports/emulator-transport.js';

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
  await transport.send('?get=ALL');
  assert.deepEqual(mockEmu.sent, ['?get=ALL\n']);

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

test('WhatelsService delegates I/O through ClientTransport', async (t) => {
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
    }
  }

  const mockTransport = new MockTestTransport();
  const service = new ServiceClass({ transport: mockTransport });

  service.connect();
  assert.equal(service.state.connected, true);

  // Send raw command
  service.sendRaw('CMD.stop\n');
  assert.deepEqual(mockTransport.sent, ['?get=ALL\n', '?sysinfo\n', 'CMD.stop\n']);

  // Handle incoming line
  let telemetryFired = false;
  service.addEventListener('telemetry', (e) => {
    telemetryFired = true;
  });
  mockTransport._emitLine('K.zDu=-250000');
  assert.equal(telemetryFired, true);
  assert.equal(service.state.z, '-25.000');

  service.disconnect();
  assert.equal(service.state.connected, false);
});

