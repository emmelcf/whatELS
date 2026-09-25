/**
 * Alpine Reactive Store for NanoELS MachineModel
 * Mirrors the ESP32 MachineModel (Kinematics, Intent, Cycle, Properties)
 * Pure unidirectional data flow with zero floating-point accumulation drift.
 */
document.addEventListener('alpine:init', () => {
  const store = Alpine.store('machine', {
    // -------------------------------------------------------------
    // Kinematics Domain ('K.*' in deci-microns: 10,000 du = 1.0 mm)
    // -------------------------------------------------------------
    zDu: 0,
    xDu: 0,
    yDu: 0,
    zLeftDu: 2147483647,
    zRightDu: -2147483648,
    xUpDu: 2147483647,
    xDownDu: -2147483648,
    zDis: false,
    xDis: false,
    yDis: false,
    rpm: 0,
    turns: 0,
    angle: 0,
    mode: 0,             // 0: GEAR, 1: XGEAR, 2: ASYNC, 3: CONE, 4: TURN, 5: FACE, 6: CUT, 7: THREAD...
    isOn: false,
    estop: false,
    dupr: 0,
    nextDupr: 0,

    // -------------------------------------------------------------
    // Intent Domain ('I.*' - Exact operator typed setpoints)
    // -------------------------------------------------------------
    thFmt: 0,           // 0: Metric mm, 1: Imperial TPI, 2: Imperial Inch
    thPitch: 1.5,       // Exact entered pitch (e.g. 20.0 for 20 TPI)
    tpMode: 0,          // 0: Ratio 1:K, 1: Angle °, 2: Standard Preset
    tpVal: 20.0,        // Exact entered taper value (e.g. 3.5 for 1:3.5)
    coneRatio: 0.05,    // Kinematic X/Z ratio
    tpPreset: -1,
    feedStepDu: 500,    // 0.05 mm/rev = 500 du
    jogStepDu: 10000,   // 1.0 mm = 10000 du
    feedRev: false,

    // -------------------------------------------------------------
    // Cycle Domain ('C.*')
    // -------------------------------------------------------------
    passes: 1,
    opIdx: 0,
    setupIdx: 0,
    starts: 1,

    // -------------------------------------------------------------
    // Properties Domain ('P.*')
    // -------------------------------------------------------------
    measure: 0,         // 0: Metric (mm), 1: Imperial (in)
    dia: true,          // Lathe X cross-slide diameter mode
    abs: true,          // Absolute vs Incremental
    zDro: false,        // Physical optical/magnetic DRO active
    xDro: false,
    screwZDu: 40000,
    screwXDu: 40000,
    motorStepsZ: 800,
    motorStepsX: 800,
    encoderPpr: 2400,

    // Connection & UI diagnostics
    connected: false,
    backend: 'emulator',
    statusMsg: 'READY: GEARBOX SYNCHRONIZED TO SPINDLE',
    spindleMode: 0,      // 0: RPM, 1: TURNS, 2: ANGLE

    // -------------------------------------------------------------
    // Computed Formatters (Reactive getters for DOM bindings)
    // -------------------------------------------------------------
    get on() {
      return this.isOn;
    },
    set on(v) {
      this.isOn = !!v;
    },

    get unitStr() {
      return this.measure === 1 ? 'in' : 'mm';
    },

    get zDisplay() {
      return Formatters.formatAxisDu(this.zDu, this.unitStr, false);
    },

    get xDisplay() {
      return Formatters.formatAxisDu(this.xDu, this.unitStr, this.dia);
    },

    get zLeftDisplay() {
      return Formatters.formatLimitDistance(this.zDu, this.zLeftDu, this.unitStr, false);
    },

    get zRightDisplay() {
      return Formatters.formatLimitDistance(this.zDu, this.zRightDu, this.unitStr, false);
    },

    get xUpDisplay() {
      return Formatters.formatLimitDistance(this.xDu, this.xUpDu, this.unitStr, this.dia);
    },

    get xDownDisplay() {
      return Formatters.formatLimitDistance(this.xDu, this.xDownDu, this.unitStr, this.dia);
    },

    get pitchDisplay() {
      return Formatters.formatPitch(this.thPitch, this.thFmt, this.unitStr);
    },

    get taperDisplay() {
      return Formatters.formatTaper(this.tpVal, this.tpMode, this.coneRatio);
    },

    get stepDisplay() {
      return Formatters.formatStepDu(this.jogStepDu, this.unitStr);
    },

    get feedStepDisplay() {
      return Formatters.formatStepDu(this.feedStepDu, this.unitStr);
    },

    get rpmDisplay() {
      return Formatters.formatRpm(this.rpm);
    },

    get angleDisplay() {
      return Formatters.formatAngle(this.angle);
    },

    get turnsDisplay() {
      return Formatters.formatTurns(this.turns);
    },

    get spindleDisplay() {
      if (this.spindleMode === 1) return this.turnsDisplay;
      if (this.spindleMode === 2) return this.angleDisplay;
      return this.rpmDisplay;
    },

    get spindleUnit() {
      if (this.spindleMode === 1) return 'TURNS';
      if (this.spindleMode === 2) return '°';
      return 'RPM';
    },

    get modeName() {
      const names = [
        'GEARBOX',   // 0: GEAR
        'X-GEAR',    // 1: XGEAR
        'ASYNC',     // 2: ASYNC
        'TAPER',     // 3: CONE
        'TURNING',   // 4: TURN
        'FACING',    // 5: FACE
        'CUTOFF',    // 6: CUT
        'THREADING', // 7: THREAD
        'SPHERE',    // 8: ELLIP
        'JOYSTICK',  // 9: JOY
        'SLOTTING'   // 10: SLOT
      ];
      return names[this.mode] || 'GEARBOX';
    },

    get statusStr() {
      return this.isOn ? 'ON' : 'OFF';
    },

    // -------------------------------------------------------------
    // Store Actions
    // -------------------------------------------------------------
    cycleMeasure() {
      this.measure = this.measure === 0 ? 1 : 0;
    },

    toggleDiaRad() {
      this.dia = !this.dia;
    },

    cycleJogStep() {
      if (window.nanoelsClient) {
        window.nanoelsClient.cycleStep();
      }
    },

    cycleSpindleMode() {
      this.spindleMode = (this.spindleMode + 1) % 3;
    },

    start() {
      this.isOn = true;
      if (window.nanoelsClient) {
        window.nanoelsClient.startCycle();
      }
    },

    stop() {
      this.isOn = false;
      if (window.nanoelsClient) {
        window.nanoelsClient.stopCycle();
      }
    },

    setStatusMsg(msg) {
      this.statusMsg = msg;
    },

    // Ingests incoming canonical dictionary from nanoels-service.js
    updateFromCanonical(c) {
      if (!c) return;
      if ('on' in c) this.isOn = !!c.on;
      for (const k in c) {
        if (k in this && typeof this[k] !== 'function') {
          this[k] = c[k];
        }
      }
    }
  });

  // Attach listener to NanoELS client service if present
  if (window.nanoelsClient) {
    window.nanoelsClient.addEventListener('telemetry', (e) => {
      if (e.detail && e.detail.canonical) {
        store.updateFromCanonical(e.detail.canonical);
      }
      if (e.detail && e.detail.message) {
        store.statusMsg = e.detail.message;
      }
    });

    window.nanoelsClient.addEventListener('connection', (e) => {
      store.connected = !!e.detail.connected;
      if (e.detail.mode) store.backend = e.detail.mode;
    });
  }
});
