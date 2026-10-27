/**
 * whatELS Client SDK — Emulator Transport Adapter
 * Adapts window.WhatelsEmulator in-memory firmware emulator to ClientTransport.
 * Authoritative Spec: docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md#43-emulatortransport
 */

import { ClientTransport } from './client-transport.js';

export class EmulatorTransport extends ClientTransport {
  /**
   * @param {Object} options
   * @param {Object|Function} [options.emulator] Pre-instantiated emulator or Emulator constructor class.
   */
  constructor(options = {}) {
    super();
    this.options = options;
    this.emulator = null;
  }

  /**
   * Establishes link with the virtual emulator.
   * @param {Object} [config={}]
   * @returns {Promise<void>}
   */
  async connect(config = {}) {
    const EmuSource = config.emulator || this.options.emulator ||
      (typeof window !== 'undefined' ? window.WhatelsEmulator : undefined) ||
      (typeof globalThis !== 'undefined' ? globalThis.WhatelsEmulator : undefined);

    if (!EmuSource) {
      const err = new Error('WhatelsEmulator class or instance not found');
      this._emitError(err);
      throw err;
    }

    if (typeof EmuSource === 'function') {
      this.emulator = new EmuSource();
    } else {
      this.emulator = EmuSource;
    }

    this.emulator.onmessage = (event) => {
      const data = (event && typeof event.data !== 'undefined') ? event.data : event;
      if (typeof data === 'string') {
        const lines = data.split('\n');
        for (const line of lines) {
          const cleanLine = line.replace(/\r$/, '');
          if (cleanLine.length > 0) {
            this._emitLine(cleanLine);
          }
        }
      }
    };

    if (typeof this.emulator.start === 'function') {
      try {
        this.emulator.start();
      } catch (e) {
        // Ignore if start fails or not required
      }
    }

    this._emitStatus('connected', { mode: 'emulator' });
  }

  /**
   * Disconnects from the emulator and stops execution.
   * @returns {Promise<void>}
   */
  async disconnect() {
    if (this.emulator) {
      if (typeof this.emulator.stop === 'function') {
        try {
          this.emulator.stop();
        } catch (e) {
          // Ignore teardown errors
        }
      }
      this.emulator.onmessage = null;
      this.emulator = null;
    }

    this._emitStatus('disconnected', { mode: 'emulator' });
  }

  /**
   * Transmits a line of text to the virtual emulator.
   * @param {string} line
   * @returns {Promise<void>}
   */
  async send(line) {
    if (!this.emulator || !this.isConnected()) {
      throw new Error('Cannot send: emulator transport is not connected');
    }

    const payload = line.endsWith('\n') ? line : `${line}\n`;
    this.emulator.send(payload);
  }
}

if (typeof globalThis !== 'undefined') {
  globalThis.EmulatorTransport = EmulatorTransport;
}

