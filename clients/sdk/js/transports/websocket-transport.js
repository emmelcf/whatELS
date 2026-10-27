/**
 * whatELS Client SDK — WebSocket Transport Adapter
 * Manages standard browser/Node WebSocket connections to whatELS controller.
 * Authoritative Spec: docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md#41-websockettransport
 */

import { ClientTransport } from './client-transport.js';

export class WebSocketTransport extends ClientTransport {
  /**
   * @param {Object} options
   * @param {string} [options.host='whatels.local']
   * @param {number} [options.port=81]
   * @param {string} [options.path='/canonical']
   * @param {boolean} [options.autoReconnect=true]
   */
  constructor(options = {}) {
    super();
    this.host = options.host || 'whatels.local';
    this.port = options.port || 81;
    this.path = options.path || '/canonical';
    this.autoReconnect = options.autoReconnect !== undefined ? !!options.autoReconnect : true;

    this.ws = null;
    this._reconnectTimer = null;
    this._reconnectAttempts = 0;
    this._manualDisconnect = false;
    this._rxBuffer = '';
    this._connectPromise = null;
  }

  /**
   * Calculates exponential reconnect delay in milliseconds (1s -> 2s -> 4s -> 8s -> 15s).
   * @param {number} attempt
   * @returns {number} Delay in milliseconds
   */
  _getReconnectDelay(attempt) {
    const delays = [1000, 2000, 4000, 8000, 15000];
    return delays[Math.min(attempt, delays.length - 1)];
  }

  /**
   * Connects to the controller WebSocket.
   * @param {Object} [config={}]
   * @returns {Promise<void>}
   */
  async connect(config = {}) {
    if (config.host) this.host = config.host;
    if (config.port) this.port = config.port;
    if (config.path) this.path = config.path;
    if (config.autoReconnect !== undefined) this.autoReconnect = !!config.autoReconnect;

    this._manualDisconnect = false;
    clearTimeout(this._reconnectTimer);

    if (this.ws) {
      try {
        this.ws.onopen = null;
        this.ws.onmessage = null;
        this.ws.onerror = null;
        this.ws.onclose = null;
        this.ws.close();
      } catch (e) {
        // Ignore close errors
      }
      this.ws = null;
    }

    const WSClass = typeof WebSocket !== 'undefined' ? WebSocket : (typeof globalThis !== 'undefined' ? globalThis.WebSocket : null);
    if (!WSClass) {
      const err = new Error('WebSocket implementation not found');
      this._emitError(err);
      throw err;
    }

    const cleanHost = this.host.replace(/^https?:\/\//i, '').replace(/^wss?:\/\//i, '').split('/')[0].split(':')[0];
    const cleanPath = this.path.startsWith('/') ? this.path : `/${this.path}`;
    const wsUrl = `ws://${cleanHost}:${this.port}${cleanPath}`;

    this._emitStatus('connecting', { host: cleanHost, port: this.port, url: wsUrl });

    return new Promise((resolve, reject) => {
      let isSettled = false;

      try {
        this.ws = new WSClass(wsUrl);

        this.ws.onopen = () => {
          this._reconnectAttempts = 0;
          this._rxBuffer = '';
          this._emitStatus('connected', { host: cleanHost, port: this.port, url: wsUrl });
          if (!isSettled) {
            isSettled = true;
            resolve();
          }
        };

        this.ws.onmessage = (event) => {
          const data = (event && typeof event.data !== 'undefined') ? event.data : event;
          if (typeof data === 'string') {
            this._rxBuffer += data;
            const lines = this._rxBuffer.split('\n');
            // If data ended with newline, last item is empty string.
            // If data was chunked mid-line, last item is partial and kept in buffer.
            this._rxBuffer = lines.pop();

            for (const line of lines) {
              const cleanLine = line.replace(/\r$/, '');
              if (cleanLine.length > 0) {
                this._emitLine(cleanLine);
              }
            }
          }
        };

        this.ws.onerror = (err) => {
          const errorObj = err instanceof Error ? err : new Error((err && err.message) || 'WebSocket error');
          this._emitError(errorObj);
          if (!isSettled) {
            isSettled = true;
            reject(errorObj);
          }
        };

        this.ws.onclose = (event) => {
          const wasConnected = this.isConnected();
          this.ws = null;
          this._emitStatus('disconnected', {
            host: cleanHost,
            port: this.port,
            code: event ? event.code : undefined,
            reason: event ? event.reason : undefined
          });

          if (!isSettled) {
            isSettled = true;
            reject(new Error(`WebSocket closed before connecting (code: ${event ? event.code : 'unknown'})`));
          }

          if (this.autoReconnect && !this._manualDisconnect) {
            const delay = this._getReconnectDelay(this._reconnectAttempts++);
            this._reconnectTimer = setTimeout(() => {
              if (this.autoReconnect && !this._manualDisconnect && !this.isConnected()) {
                this.connect().catch(() => {});
              }
            }, delay);
          }
        };
      } catch (err) {
        this._emitError(err);
        if (!isSettled) {
          isSettled = true;
          reject(err);
        }
      }
    });
  }

  /**
   * Gracefully disconnects and cancels any pending reconnects.
   * @returns {Promise<void>}
   */
  async disconnect() {
    this._manualDisconnect = true;
    clearTimeout(this._reconnectTimer);

    if (this.ws) {
      this.ws.onopen = null;
      this.ws.onmessage = null;
      this.ws.onerror = null;
      this.ws.onclose = null;
      try {
        this.ws.close();
      } catch (e) {
        // Ignore close errors
      }
      this.ws = null;
    }

    this._emitStatus('disconnected', { host: this.host, port: this.port });
  }

  /**
   * Transmits a line of text over WebSocket.
   * Ensures line terminates with a single '\n'.
   * @param {string} line
   * @returns {Promise<void>}
   */
  async send(line) {
    if (!this.ws || !this.isConnected()) {
      throw new Error('Cannot send: WebSocket is not connected');
    }

    const payload = line.endsWith('\n') ? line : `${line}\n`;
    this.ws.send(payload);
  }
}

if (typeof globalThis !== 'undefined') {
  globalThis.WebSocketTransport = WebSocketTransport;
}

