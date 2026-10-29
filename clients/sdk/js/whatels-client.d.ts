/**
 * whatELS Controller Client SDK — Type Definitions
 * Universal TypeScript IDL for whatELS Operator Clients
 */

export type Axis = 'Z' | 'X' | 'ZX';
export type SemanticDirZ = 'CHUCK' | 'TAIL' | 'LEFT' | 'RIGHT';
export type SemanticDirX = 'INFEED' | 'RETRACT' | 'CENTER' | 'IN' | 'UP' | 'OUT' | 'DOWN';
export type SemanticDirection = SemanticDirZ | SemanticDirX | -1 | 1;
export type LimitSide = 'LEFT' | 'RIGHT' | 'UP' | 'DOWN' | 'MIN' | 'MAX';
export type TransportStatus = 'disconnected' | 'connecting' | 'connected';

export interface TransportConfig {
  host?: string;
  port?: number;
  path?: string;
  baudRate?: number;
  promptUser?: boolean;
  autoReconnect?: boolean;
}

export interface StartFeedParams {
  axis: Axis;
  sync?: boolean | 0 | 1;          // optional, default 1 (spindle synchronized)
  feed: number;                    // mandatory positive integer: dupr (sync:1) or du/min (sync:0)
  dir: -1 | 1 | SemanticDirection; // mandatory primary axis direction
  xdir?: -1 | 1 | SemanticDirX;    // mandatory when axis === 'ZX': secondary X direction
  taper?: number;                  // mandatory when axis === 'ZX': radial X du per 10,000 du of Z (> 0)
  dist?: number;                   // optional integer deci-microns, default -1 (continuous feed to limit)
}

export interface ClientHelloParams {
  clientType?: string;             // e.g. 'webui', 'smarttouch', 'hil_harness'
  clientVer?: string;              // semver, e.g. '1.0.0'
  label?: string;                  // human-readable device name, e.g. 'Headstock Tablet'
}

export interface KinematicsDomain {
  zDu: number;                     // deci-microns (10,000 du = 1.0 mm)
  xDu: number;                     // deci-microns (radius)
  rpm: number;                     // spindle RPM
  turns: number;                   // spindle turns counter
  angle: number;                   // spindle angle 0..359.9°
  mode: number;                    // active mode ID (0: GEAR, 50: GEARBOX, etc.)
  isOn: boolean;                   // true if motion is physically active
  estop: boolean;                  // true if emergency stop is triggered
  perms: number;                   // live in-motion permission bitmask
  zLeftDu: number | null;          // Z chuck travel limit (du)
  zRightDu: number | null;         // Z tailstock travel limit (du)
  xUpDu: number | null;            // X infeed travel limit (du)
  xDownDu: number | null;          // X retract travel limit (du)
  zDis: boolean;                   // true if Z limit contacted
  xDis: boolean;                   // true if X limit contacted
  dupr: number;                    // live feed rate in deci-microns per rev
  nextDupr: number;                // queued feed rate
}

export interface IntentDomain {
  thPitch: number;                 // operator entered pitch (mm or TPI)
  thFmt: 0 | 1 | 2;                // 0: Metric mm, 1: Imperial TPI, 2: Imperial Inch
  tpVal: number;                   // operator entered taper value
  tpMode: 0 | 1 | 2;               // 0: Ratio 1:K, 1: Angle °, 2: Preset
  coneRatio: number;               // kinematic X/Z ratio
  feedStepDu: number;              // discrete feed step size (du)
  jogStepDu: number;               // discrete jog step size (du)
  feedRev: boolean;                // pitch direction reversed
}

export interface CycleDomain {
  passes: number;                  // total cycle pass count
  opIdx: number;                   // current pass or cycle step index
  setupIdx: number;                // wizard step index
  starts: number;                  // multi-start thread count
}

export interface PropertiesDomain {
  measure: 0 | 1;                  // 0: Metric (mm), 1: Imperial (in)
  dia: boolean;                    // true: Diameter mode, false: Radius mode
  abs: boolean;                    // true: Absolute mode, false: Incremental
  zDro: boolean;                   // true: Physical scale DRO active
  xDro: boolean;                   // true: Physical scale DRO active
  screwZDu: number;                // leadscrew pitch in deci-microns
  screwXDu: number;                // cross-slide pitch in deci-microns
  motorStepsZ: number;             // motor steps per rev
  motorStepsX: number;             // motor steps per rev
  encoderPpr: number;              // spindle encoder pulses per rev
}

export interface MachineSnapshot {
  kinematics: KinematicsDomain;
  intent: IntentDomain;
  cycle: CycleDomain;
  properties: PropertiesDomain;
}

export interface SnapshotEventDetail {
  domainMask: number;              // 0x01 (K), 0x02 (I), 0x04 (C), 0x08 (P)
  snapshot: MachineSnapshot;
}

export interface ConnectionEventDetail {
  connected: boolean;
  sessionId: number | null;
  status: TransportStatus;
}

export declare class ClientTransport extends EventTarget {
  status: TransportStatus;
  connect(config?: TransportConfig): Promise<void>;
  disconnect(): Promise<void>;
  isConnected(): boolean;
  send(line: string): Promise<void>;
  addEventListener(type: 'line', listener: (ev: CustomEvent<string>) => void): void;
  addEventListener(type: 'status', listener: (ev: CustomEvent<{ status: TransportStatus }>) => void): void;
  addEventListener(type: 'error', listener: (ev: CustomEvent<Error>) => void): void;
}

export declare class WhatelsClient extends EventTarget {
  constructor(transport: ClientTransport, options?: ClientHelloParams & { timeoutMs?: number });
  transport: ClientTransport;
  clientType: string;
  clientVer: string;
  label: string;
  timeoutMs: number;
  sessionId: number | null;
  perms: number;

  connect(): Promise<void>;
  disconnect(): Promise<void>;
  setTransport(transport: ClientTransport): void;

  startFeed(params: StartFeedParams): Promise<string>;
  stop(): Promise<string>;
  jogHold(axis: Axis, dir: SemanticDirection): Promise<string>;
  jogRelease(axis: Axis): Promise<string>;
  jogStep(axis: Axis, distDu: number): Promise<string>;

  axisZero(axis: Axis): Promise<string>;
  axisPosDu(axis: Axis, posDu: number): Promise<string>;
  axisLimitDu(axis: Axis, side: LimitSide, du: number): Promise<string>;
  axisClearLimit(axis: Axis, side: LimitSide): Promise<string>;

  getSetting(key: string): Promise<string>;
  setSetting(key: string, value: string | number): Promise<string>;

  addEventListener(type: 'snapshot', listener: (ev: CustomEvent<SnapshotEventDetail>) => void): void;
  addEventListener(type: 'connection', listener: (ev: CustomEvent<ConnectionEventDetail>) => void): void;
  addEventListener(type: 'log', listener: (ev: CustomEvent<string>) => void): void;
  addEventListener(type: 'error', listener: (ev: CustomEvent<Error>) => void): void;
}

