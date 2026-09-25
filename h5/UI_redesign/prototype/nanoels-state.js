/**
 * Alpine Reactive Store for NanoELS MachineModel
 * Mirrors the ESP32 MachineModel (Kinematics, Intent, Cycle, Properties)
 * Pure unidirectional data flow with zero floating-point accumulation drift.
 */
document.addEventListener('alpine:init', () => {
  Alpine.store('machine', {
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
    statusMsg: 'SYSTEM INITIALIZED',

    // -------------------------------------------------------------
    // Computed Formatters (Reactive getters for DOM bindings)
    // -------------------------------------------------------------
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

    get statusStr() {
      return this.isOn ? 'ON' : 'OFF';
    },

    // Ingests incoming canonical dictionary from nanoels-service.js
    updateFromCanonical(c) {
      if (!c) return;
      for (const k in c) {
        if (k in this && typeof this[k] !== 'function') {
          this[k] = c[k];
        }
      }
    }
  });
});
