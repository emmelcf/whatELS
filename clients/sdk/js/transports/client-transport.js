/**
 * whatELS Client SDK — Pluggable Transport Contract
 * Authoritative Spec: docs/clients/sdk/CLIENT_SDK_SPECIFICATION.md#31-contract-1-pluggable-transport-clienttransport
 */

/**
 * Abstract Base Client Transport
 * Manages raw bidirectional line streaming over any physical or virtual medium.
 */
export class ClientTransport extends EventTarget {
  constructor() {
    super();
    this.status = 'disconnected'; // 'disconnected' | 'connecting' | 'connected'
  }

  /**
   * Establishes the transport connection.
   * @param {Object} config Transport-specific connection parameters.
   * @returns {Promise<void>} Resolves when connection is open, rejects on error.
   */
  async connect(config = {}) {
    throw new Error('connect() must be implemented by subclass');
  }

  /**
   * Gracefully closes the transport connection.
   * @returns {Promise<void>}
   */
  async disconnect() {
    throw new Error('disconnect() must be implemented by subclass');
  }

  /**
   * @returns {boolean} True if transport is open and ready to transmit.
   */
  isConnected() {
    return this.status === 'connected';
  }

  /**
   * Transmits a line of text to the controller.
   * Implementations MUST ensure line terminates with a single '\n'.
   * @param {string} line
   * @returns {Promise<void>}
   */
  async send(line) {
    throw new Error('send() must be implemented by subclass');
  }

  // --- Protected Event Helper Methods ---
  _emitLine(line) {
    this.dispatchEvent(new CustomEvent('line', { detail: line }));
  }

  _emitStatus(status, details = {}) {
    this.status = status;
    this.dispatchEvent(new CustomEvent('status', { detail: { status, ...details } }));
  }

  _emitError(error) {
    this.dispatchEvent(new CustomEvent('error', { detail: error }));
  }
}

if (typeof globalThis !== 'undefined') {
  globalThis.ClientTransport = ClientTransport;
}

