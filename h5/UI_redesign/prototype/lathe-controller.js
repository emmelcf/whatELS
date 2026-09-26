(function(){
  var $ = function(id){ return document.getElementById(id); };

  // Standard Taper Presets & Categories for Gearbox 2-Axis Mode
  var TAPER_CATEGORIES = [
    { id: 'SPINDLE', label: 'COLLETS & SPINDLES' },
    { id: 'RATIOS', label: 'RATIOS & PINS' },
    { id: 'CENTERS', label: 'CHAMFERS & CENTERS' }
  ];

  var gearTaperCategory = 'SPINDLE';

  var TAPER_PRESETS = [
    // --- COLLETS & SPINDLES (9 items: MT1-MT5, R8, 5C, ER, 7:24) ---
    { name: 'MT1', category: 'SPINDLE', angle: 1.4286, ratio: 0.049882, spec: '0.5986 in/ft' },
    { name: 'MT2', category: 'SPINDLE', angle: 1.4306, ratio: 0.049951, spec: '0.5994 in/ft' },
    { name: 'MT3', category: 'SPINDLE', angle: 1.4376, ratio: 0.050196, spec: '0.6024 in/ft' },
    { name: 'MT4', category: 'SPINDLE', angle: 1.4875, ratio: 0.051938, spec: '0.6233 in/ft' },
    { name: 'MT5', category: 'SPINDLE', angle: 1.5072, ratio: 0.052626, spec: '0.6315 in/ft' },
    { name: 'R8 COLLET', category: 'SPINDLE', angle: 8.4250, ratio: 0.296270, spec: '16° 51\' included' },
    { name: '5C COLLET', category: 'SPINDLE', angle: 10.0000, ratio: 0.352654, spec: '20° 00\' included' },
    { name: 'ER COLLET', category: 'SPINDLE', angle: 8.0000, ratio: 0.281096, spec: '16° 00\' included' },
    { name: '7:24 STEEP', category: 'SPINDLE', angle: 8.2971, ratio: 0.291667, spec: '3.500 in/ft (ISO)' },

    // --- RATIOS & PINS (6 items: 1:50, 1:48, 1:10, 1:20, 1:5, NPT/BSPT) ---
    { name: '1:50 METRIC PIN', category: 'RATIOS', angle: 0.5729, ratio: 0.020000, spec: 'ISO 2339 / DIN 1' },
    { name: '1:48 US PIN', category: 'RATIOS', angle: 0.5968, ratio: 0.020833, spec: '1/4" / ft (ASME)' },
    { name: '1:10 SHAFT', category: 'RATIOS', angle: 2.8624, ratio: 0.100000, spec: 'ISO 2268 (Motors)' },
    { name: '1:20 METRIC', category: 'RATIOS', angle: 1.4321, ratio: 0.050000, spec: 'DIN 228 / ISO' },
    { name: '1:5 METRIC', category: 'RATIOS', angle: 5.7106, ratio: 0.200000, spec: 'DIN 254 (Hubs)' },
    { name: 'NPT/BSPT PIPE', category: 'RATIOS', angle: 1.7899, ratio: 0.062500, spec: '1:16 (0.750 in/ft)' },

    // --- CHAMFERS & CENTERS (3 items: 60° Center, 45° Chamfer, 30° Bevel) ---
    { name: '60° LATHE CENTER', category: 'CENTERS', angle: 30.0000, ratio: 1.154701, spec: '60° 00\' included' },
    { name: '45° CHAMFER', category: 'CENTERS', angle: 45.0000, ratio: 2.000000, spec: '1:1 (45°)' },
    { name: '30° BEVEL', category: 'CENTERS', angle: 30.0000, ratio: 1.154701, spec: '30° lead angle' }
  ];


  // Core State Engine
  var state = {
    unit: 'mm',
    xRep: 'diameter',
    z: -18.426,
    zLeft: null, zRight: null,
    xDia: 26.438,
    xLeft: null, xRight: null,
    get zMin(){ return this.zLeft !== null ? +(this.z - this.zLeft).toFixed(3) : null; },
    set zMin(v){ this.zLeft = (v !== null) ? Math.max(0, +(this.z - v).toFixed(3)) : null; },
    get zMax(){ return this.zRight !== null ? +(this.z + this.zRight).toFixed(3) : null; },
    set zMax(v){ this.zRight = (v !== null) ? Math.max(0, +(v - this.z).toFixed(3)) : null; },
    get xMinDia(){ return this.xLeft !== null ? +(this.xDia - this.xLeft).toFixed(3) : null; },
    set xMinDia(v){ this.xLeft = (v !== null) ? Math.max(0, +(this.xDia - v).toFixed(3)) : null; },
    get xMaxDia(){ return this.xRight !== null ? +(this.xDia + this.xRight).toFixed(3) : null; },
    set xMaxDia(v){ this.xRight = (v !== null) ? Math.max(0, +(v - this.xDia).toFixed(3)) : null; },
    steps: [1.000, 0.100, 0.010], stepIdx: 2,
    spindleSimRunning: false,
    rpm: 0, turns: 0, angle: 0, spindleCycle: 0,
    
    // Operating Mode & State
    mode: 'GEARBOX', // Default mode
    runState: 'stopped', // strictly 'stopped' or 'running' (NanoELS hardware binary state)
    
    // Gearbox Mode
    gearboxMode: 'SYNC', // 'SYNC' (spindle synchronized) or 'ASYNC' (time-based)
    activeFeedAxis: 'Z', // 'Z', 'X', or 'Z+X' (2-Axis Taper)
    gearTaperPresetIdx: 0,
    gearTaperRatio: 0.049882,
    gearTaperAngle: 1.4286,
    gearTaperSide: 'EXTERNAL', // 'EXTERNAL' or 'INTERNAL'
    gearTaperInvert: false, // false: Normal (larger at chuck), true: Inverted (smaller at chuck)
    gearFeedModeZ: 'FEED', // 'FEED' (mm/rev or in/rev), 'PITCH' (leadscrew thread sync)
    gearThreadHandedness: 'RH', // 'RH' or 'LH'
    gearThreadUnit: 'metric', // 'metric' or 'tpi'
    gearPitchZ: 0.100, gearDirZ: 'FWD',
    gearPitchX: 0.050, gearDirX: 'IN',
    asyncSpeedZ: 150, asyncSpeedX: 75,
    // Step Progression State (4-step standard cycles)
    feedStepIdxZ: 1,       // 0: 0.010, 1: 0.050, 2: 0.100, 3: 0.200 (or in: 0.001, 0.002, 0.005, 0.010)
    feedStepIdxX: 1,
    feedStepTpiIdx: 0,     // 0: 1, 1: 2, 2: 4, 3: 8
    asyncStepIdxZ: 1,      // 0: 10, 1: 25, 2: 50, 3: 100 (or in: 0.5, 1.0, 2.0, 5.0)
    asyncStepIdxX: 0,
    
    // Turning (Dimension-First Pattern)
    turnPitch: 0.080,
    turnLength: 25.000,
    turnDir: 'CHUCK',        // 'CHUCK' (toward headstock / Z-) or 'TAIL' (toward tailstock / Z+)
    turnTotalDepth: 2.400,   // on diameter
    turnMaxDoc: 0.400,       // max depth of cut on diameter
    turnPasses: 6,           // discrete pass count (two-way coupled with Max DOC)
    turnRetract: 0.500,
    turnType: 'EXTERNAL',
    turnRestoreLimits: false,
    turnSavedZMin: null,
    turnSavedZMax: null,
    turnCurrentPass: 1,

    // Facing (Dimension-First Pattern)
    facePitch: 0.050,
    faceStroke: 14.000,      // stroke across face
    faceDir: 'CENTER',       // 'CENTER' (inward / X-) or 'RETRACT' (outward / X+)
    faceTotalDepth: 1.000,   // depth into face (Z)
    faceMaxDoc: 0.250,       // step depth per pass (Z)
    facePasses: 4,           // discrete pass count (two-way coupled with Max DOC)
    faceRetract: 0.500,
    faceRestoreLimits: false,
    faceSavedXMin: null,
    faceSavedXMax: null,
    faceCurrentPass: 1,

    // Threading (Conversational + Safety Features)
    pitch: 1.750,
    threadStandard: 'METRIC', // 'METRIC', 'UN', 'NPT', 'BSPT', 'FREEFORM'
    threadStandardName: 'M12 × 1.75',
    threadSizeIdx: 6,
    threadPitchSubIdx: 0,
    threadSide: 'EXTERNAL', // 'EXTERNAL' or 'INTERNAL'
    threadHandedness: 'RIGHT', // 'RIGHT' (RH) or 'LEFT' (LH)
    threadDir: 'RIGHT', // mirrors threadHandedness for backwards compat
    threadCutDir: 'CHUCK', // 'CHUCK' (outside-in / Z-) or 'TAIL' (inside-out / Z+ blind hole safe)
    threadProfile: 'STRAIGHT', // 'STRAIGHT' or 'NPT 1:16'
    threadConeRatio: 0, // 0 for straight, 0.0625 for NPT/BSPT
    threadStarts: 1, // 1 to 124
    threadTotalDepth: 1.073, // mm (calculated from standard)
    threadTargetStockDia: 11.90, // target prep OD
    threadPassCount: 11,
    threadCurrentPass: 1,
    threadSkimVerify: true, // Human-in-the-loop skim verification pass
    threadSkimStage: 'IDLE', // 'IDLE', 'SKIMMING', 'VERIFY_PROMPT', 'THREADING'
    pass: '01 / 11',
    depth: 1.073,
    pitchTab: 'metric',
    threadUnit: 'metric',
    
    // Other H5 Modes
    taperAngle: 5.71, taperLen: 25.0, taperDir: 'INT', taperCycleActive: false,
    cutoffFeed: 0.030, cutoffPeck: 1.200, cutoffRetract: 0.500,
    slotStrokes: 12, slotFeedPerStroke: 0.020, slotLen: 15.0,
    sphereRadius: 12.0, sphereType: 'CONVEX', sphereStep: 0.050,
    joyDeadzone: 5, joyMaxRate: 800,

    // Message History Log
    msgLog: [
      'SYSTEM BOOT: NANOELS H5 INITIALIZED',
      'READY: GEARBOX SYNCHRONIZED TO SPINDLE'
    ]
  };

  // Standard Fasteners Dataset with Ranges & 3rd-Choice Secondary Sizes
  var THREAD_FAMILIES = {
    METRIC: {
      name: 'METRIC',
      ranges: [
        { label: '≤ M6', minDia: 1, maxDia: 6 },
        { label: 'M7 – M12', minDia: 7, maxDia: 12 },
        { label: 'M14 – M24', minDia: 14, maxDia: 24 },
        { label: 'M27 – M42', minDia: 27, maxDia: 42 }
      ],
      sizes: [
        // ≤ M6 (M1 to M6 in ascending diameter order)
        { size: 'M1', dia: 1.0, rangeIdx: 0, choice: 3, pitches: [0.25], stockMin: 0.89, stockMax: 0.98 },
        { size: 'M1.2', dia: 1.2, rangeIdx: 0, choice: 3, pitches: [0.25], stockMin: 1.09, stockMax: 1.18 },
        { size: 'M1.4', dia: 1.4, rangeIdx: 0, choice: 3, pitches: [0.30], stockMin: 1.29, stockMax: 1.38 },
        { size: 'M1.6', dia: 1.6, rangeIdx: 0, choice: 3, pitches: [0.35, 0.20], stockMin: 1.49, stockMax: 1.58 },
        { size: 'M1.8', dia: 1.8, rangeIdx: 0, choice: 3, pitches: [0.35, 0.20], stockMin: 1.69, stockMax: 1.78 },
        { size: 'M2', dia: 2.0, rangeIdx: 0, choice: 3, pitches: [0.40, 0.25], stockMin: 1.88, stockMax: 1.98 },
        { size: 'M2.2', dia: 2.2, rangeIdx: 0, choice: 3, pitches: [0.45, 0.25], stockMin: 2.08, stockMax: 2.18 },
        { size: 'M2.5', dia: 2.5, rangeIdx: 0, choice: 3, pitches: [0.45, 0.35], stockMin: 2.38, stockMax: 2.48 },
        { size: 'M3', dia: 3.0, rangeIdx: 0, choice: 1, pitches: [0.50, 0.35], stockMin: 2.87, stockMax: 2.98 },
        { size: 'M3.2', dia: 3.2, rangeIdx: 0, choice: 3, pitches: [0.50, 0.35], stockMin: 3.07, stockMax: 3.18 },
        { size: 'M3.5', dia: 3.5, rangeIdx: 0, choice: 2, pitches: [0.60, 0.35], stockMin: 3.35, stockMax: 3.48 },
        { size: 'M4', dia: 4.0, rangeIdx: 0, choice: 1, pitches: [0.70, 0.50], stockMin: 3.84, stockMax: 3.98 },
        { size: 'M5', dia: 5.0, rangeIdx: 0, choice: 1, pitches: [0.80, 0.50], stockMin: 4.83, stockMax: 4.98 },
        { size: 'M6', dia: 6.0, rangeIdx: 0, choice: 1, pitches: [1.00, 0.75], stockMin: 5.79, stockMax: 5.97 },
        // M7 - M12
        { size: 'M6.5', dia: 6.5, rangeIdx: 1, choice: 3, pitches: [1.00, 0.75], stockMin: 6.29, stockMax: 6.47 },
        { size: 'M7', dia: 7.0, rangeIdx: 1, choice: 2, pitches: [1.00, 0.75], stockMin: 6.79, stockMax: 6.97 },
        { size: 'M7.5', dia: 7.5, rangeIdx: 1, choice: 3, pitches: [1.00, 0.75], stockMin: 7.29, stockMax: 7.47 },
        { size: 'M8', dia: 8.0, rangeIdx: 1, choice: 1, pitches: [1.25, 1.00, 0.75], stockMin: 7.76, stockMax: 7.97 },
        { size: 'M8.5', dia: 8.5, rangeIdx: 1, choice: 3, pitches: [1.25, 1.00], stockMin: 8.26, stockMax: 8.47 },
        { size: 'M9', dia: 9.0, rangeIdx: 1, choice: 2, pitches: [1.25, 1.00, 0.75], stockMin: 8.76, stockMax: 8.97 },
        { size: 'M10', dia: 10.0, rangeIdx: 1, choice: 1, pitches: [1.50, 1.25, 1.00, 0.75], stockMin: 9.73, stockMax: 9.97 },
        { size: 'M11', dia: 11.0, rangeIdx: 1, choice: 2, pitches: [1.50, 1.25, 1.00], stockMin: 10.73, stockMax: 10.97 },
        { size: 'M12', dia: 12.0, rangeIdx: 1, choice: 1, pitches: [1.75, 1.50, 1.25, 1.00], stockMin: 11.70, stockMax: 11.97 },
        // M14 - M24
        { size: 'M13', dia: 13.0, rangeIdx: 2, choice: 3, pitches: [1.50, 1.25], stockMin: 12.73, stockMax: 12.97 },
        { size: 'M14', dia: 14.0, rangeIdx: 2, choice: 1, pitches: [2.00, 1.50, 1.25, 1.00], stockMin: 13.68, stockMax: 13.96 },
        { size: 'M15', dia: 15.0, rangeIdx: 2, choice: 3, pitches: [1.50, 1.00], stockMin: 14.73, stockMax: 14.97 },
        { size: 'M16', dia: 16.0, rangeIdx: 2, choice: 1, pitches: [2.00, 1.50, 1.00], stockMin: 15.68, stockMax: 15.96 },
        { size: 'M17', dia: 17.0, rangeIdx: 2, choice: 3, pitches: [1.50, 1.00], stockMin: 16.73, stockMax: 16.97 },
        { size: 'M18', dia: 18.0, rangeIdx: 2, choice: 2, pitches: [2.50, 2.00, 1.50, 1.00], stockMin: 17.62, stockMax: 17.96 },
        { size: 'M19', dia: 19.0, rangeIdx: 2, choice: 3, pitches: [1.50, 1.00], stockMin: 18.73, stockMax: 18.97 },
        { size: 'M20', dia: 20.0, rangeIdx: 2, choice: 1, pitches: [2.50, 2.00, 1.50, 1.00], stockMin: 19.62, stockMax: 19.96 },
        { size: 'M22', dia: 22.0, rangeIdx: 2, choice: 2, pitches: [2.50, 2.00, 1.50, 1.00], stockMin: 21.62, stockMax: 21.96 },
        { size: 'M24', dia: 24.0, rangeIdx: 2, choice: 1, pitches: [3.00, 2.00, 1.50, 1.00], stockMin: 23.58, stockMax: 23.95 },
        { size: 'M25', dia: 25.0, rangeIdx: 2, choice: 3, pitches: [2.00, 1.50], stockMin: 24.68, stockMax: 24.96 },
        // M27 - M42
        { size: 'M26', dia: 26.0, rangeIdx: 3, choice: 3, pitches: [1.50], stockMin: 25.73, stockMax: 25.97 },
        { size: 'M27', dia: 27.0, rangeIdx: 3, choice: 2, pitches: [3.00, 2.00, 1.50], stockMin: 26.58, stockMax: 26.95 },
        { size: 'M28', dia: 28.0, rangeIdx: 3, choice: 3, pitches: [2.00, 1.50], stockMin: 27.68, stockMax: 27.96 },
        { size: 'M30', dia: 30.0, rangeIdx: 3, choice: 1, pitches: [3.50, 3.00, 2.00, 1.50], stockMin: 29.53, stockMax: 29.95 },
        { size: 'M32', dia: 32.0, rangeIdx: 3, choice: 3, pitches: [2.00, 1.50], stockMin: 31.68, stockMax: 31.96 },
        { size: 'M33', dia: 33.0, rangeIdx: 3, choice: 2, pitches: [3.50, 3.00, 2.00, 1.50], stockMin: 32.53, stockMax: 32.95 },
        { size: 'M35', dia: 35.0, rangeIdx: 3, choice: 3, pitches: [1.50], stockMin: 34.73, stockMax: 34.97 },
        { size: 'M36', dia: 36.0, rangeIdx: 3, choice: 1, pitches: [4.00, 3.00, 2.00, 1.50], stockMin: 35.47, stockMax: 35.94 },
        { size: 'M38', dia: 38.0, rangeIdx: 3, choice: 3, pitches: [1.50], stockMin: 37.73, stockMax: 37.97 },
        { size: 'M39', dia: 39.0, rangeIdx: 3, choice: 2, pitches: [4.00, 3.00, 2.00, 1.50], stockMin: 38.47, stockMax: 38.94 },
        { size: 'M40', dia: 40.0, rangeIdx: 3, choice: 3, pitches: [3.00, 2.00, 1.50], stockMin: 39.58, stockMax: 39.95 },
        { size: 'M42', dia: 42.0, rangeIdx: 3, choice: 1, pitches: [4.50, 4.00, 3.00, 2.00, 1.50], stockMin: 41.41, stockMax: 41.94 }
      ]
    },
    UN: {
      name: 'INCH',
      ranges: [
        { label: '#4 – 3/8″', minDia: 0.1, maxDia: 0.38 },
        { label: '7/16″ – 3/4″', minDia: 0.4, maxDia: 0.76 },
        { label: '7/8″ – 1-1/2″', minDia: 0.8, maxDia: 1.6 }
      ],
      sizes: [
        { size: '#4', dia: 2.84, rangeIdx: 0, choice: 1, pitches: [{ tpi: 40, type: 'UNC' }, { tpi: 48, type: 'UNF' }], stockMin: 2.68, stockMax: 2.81 },
        { size: '#6', dia: 3.51, rangeIdx: 0, choice: 1, pitches: [{ tpi: 32, type: 'UNC' }, { tpi: 40, type: 'UNF' }], stockMin: 3.33, stockMax: 3.48 },
        { size: '#8', dia: 4.17, rangeIdx: 0, choice: 1, pitches: [{ tpi: 32, type: 'UNC' }, { tpi: 36, type: 'UNF' }], stockMin: 3.99, stockMax: 4.14 },
        { size: '#10', dia: 4.83, rangeIdx: 0, choice: 1, pitches: [{ tpi: 24, type: 'UNC' }, { tpi: 32, type: 'UNF' }], stockMin: 4.62, stockMax: 4.79 },
        { size: '1/4″', dia: 6.35, rangeIdx: 0, choice: 1, pitches: [{ tpi: 20, type: 'UNC' }, { tpi: 28, type: 'UNF' }], stockMin: 6.12, stockMax: 6.32 },
        { size: '5/16″', dia: 7.94, rangeIdx: 0, choice: 1, pitches: [{ tpi: 18, type: 'UNC' }, { tpi: 24, type: 'UNF' }], stockMin: 7.69, stockMax: 7.90 },
        { size: '3/8″', dia: 9.53, rangeIdx: 0, choice: 1, pitches: [{ tpi: 16, type: 'UNC' }, { tpi: 24, type: 'UNF' }], stockMin: 9.25, stockMax: 9.49 },
        // Range 1
        { size: '7/16″', dia: 11.11, rangeIdx: 1, choice: 1, pitches: [{ tpi: 14, type: 'UNC' }, { tpi: 20, type: 'UNF' }], stockMin: 10.82, stockMax: 11.07 },
        { size: '1/2″', dia: 12.70, rangeIdx: 1, choice: 1, pitches: [{ tpi: 13, type: 'UNC' }, { tpi: 20, type: 'UNF' }], stockMin: 12.38, stockMax: 12.66 },
        { size: '9/16″', dia: 14.29, rangeIdx: 1, choice: 2, pitches: [{ tpi: 12, type: 'UNC' }, { tpi: 18, type: 'UNF' }], stockMin: 13.94, stockMax: 14.24 },
        { size: '5/8″', dia: 15.88, rangeIdx: 1, choice: 1, pitches: [{ tpi: 11, type: 'UNC' }, { tpi: 18, type: 'UNF' }], stockMin: 15.52, stockMax: 15.83 },
        { size: '3/4″', dia: 19.05, rangeIdx: 1, choice: 1, pitches: [{ tpi: 10, type: 'UNC' }, { tpi: 16, type: 'UNF' }], stockMin: 18.66, stockMax: 19.00 },
        // Range 2
        { size: '7/8″', dia: 22.23, rangeIdx: 2, choice: 1, pitches: [{ tpi: 9, type: 'UNC' }, { tpi: 14, type: 'UNF' }], stockMin: 21.80, stockMax: 22.17 },
        { size: '1″', dia: 25.40, rangeIdx: 2, choice: 1, pitches: [{ tpi: 8, type: 'UNC' }, { tpi: 12, type: 'UNF' }], stockMin: 24.96, stockMax: 25.34 },
        { size: '1-1/8″', dia: 28.58, rangeIdx: 2, choice: 2, pitches: [{ tpi: 7, type: 'UNC' }, { tpi: 12, type: 'UNF' }], stockMin: 28.08, stockMax: 28.51 },
        { size: '1-1/4″', dia: 31.75, rangeIdx: 2, choice: 1, pitches: [{ tpi: 7, type: 'UNC' }, { tpi: 12, type: 'UNF' }], stockMin: 31.25, stockMax: 31.68 },
        { size: '1-1/2″', dia: 38.10, rangeIdx: 2, choice: 1, pitches: [{ tpi: 6, type: 'UNC' }, { tpi: 12, type: 'UNF' }], stockMin: 37.52, stockMax: 38.02 }
      ]
    },
    NPT: {
      name: 'NPT',
      ranges: [
        { label: '1/8″ – 1/2″', minDia: 10, maxDia: 22 },
        { label: '3/4″ – 2″', minDia: 23, maxDia: 61 }
      ],
      sizes: [
        { size: '1/8″', tpi: 27, rangeIdx: 0, choice: 1, pipeOd: 10.29, smallOd: 9.96, l2: 6.7, passes: 10 },
        { size: '1/4″', tpi: 18, rangeIdx: 0, choice: 1, pipeOd: 13.72, smallOd: 13.26, l2: 10.2, passes: 12 },
        { size: '3/8″', tpi: 18, rangeIdx: 0, choice: 1, pipeOd: 17.15, smallOd: 16.66, l2: 10.4, passes: 12 },
        { size: '1/2″', tpi: 14, rangeIdx: 0, choice: 1, pipeOd: 21.34, smallOd: 20.73, l2: 13.6, passes: 14 },
        { size: '3/4″', tpi: 14, rangeIdx: 1, choice: 1, pipeOd: 26.67, smallOd: 26.04, l2: 14.2, passes: 14 },
        { size: '1″', tpi: 11.5, rangeIdx: 1, choice: 1, pipeOd: 33.40, smallOd: 32.59, l2: 17.3, passes: 16 },
        { size: '1-1/4″', tpi: 11.5, rangeIdx: 1, choice: 2, pipeOd: 42.16, smallOd: 41.31, l2: 18.0, passes: 16 },
        { size: '1-1/2″', tpi: 11.5, rangeIdx: 1, choice: 2, pipeOd: 48.26, smallOd: 47.38, l2: 18.4, passes: 16 },
        { size: '2″', tpi: 11.5, rangeIdx: 1, choice: 2, pipeOd: 60.33, smallOd: 59.41, l2: 19.2, passes: 18 }
      ]
    },
    BSPT: {
      name: 'BSPT',
      ranges: [
        { label: '1/8″ – 1/2″', minDia: 9, maxDia: 22 },
        { label: '3/4″ – 2″', minDia: 23, maxDia: 61 }
      ],
      sizes: [
        { size: '1/8″', tpi: 28, rangeIdx: 0, choice: 1, pipeOd: 9.73, smallOd: 9.40, l2: 6.5, passes: 10 },
        { size: '1/4″', tpi: 19, rangeIdx: 0, choice: 1, pipeOd: 13.16, smallOd: 12.70, l2: 9.7, passes: 12 },
        { size: '3/8″', tpi: 19, rangeIdx: 0, choice: 1, pipeOd: 16.66, smallOd: 16.20, l2: 10.1, passes: 12 },
        { size: '1/2″', tpi: 14, rangeIdx: 0, choice: 1, pipeOd: 20.96, smallOd: 20.35, l2: 13.2, passes: 14 },
        { size: '3/4″', tpi: 14, rangeIdx: 1, choice: 1, pipeOd: 26.44, smallOd: 25.80, l2: 14.0, passes: 14 },
        { size: '1″', tpi: 11, rangeIdx: 1, choice: 1, pipeOd: 33.25, smallOd: 32.40, l2: 16.8, passes: 16 },
        { size: '1-1/4″', tpi: 11, rangeIdx: 1, choice: 2, pipeOd: 41.91, smallOd: 41.05, l2: 17.5, passes: 16 },
        { size: '1-1/2″', tpi: 11, rangeIdx: 1, choice: 2, pipeOd: 47.80, smallOd: 46.90, l2: 18.0, passes: 16 },
        { size: '2″', tpi: 11, rangeIdx: 1, choice: 2, pipeOd: 59.61, smallOd: 58.70, l2: 19.0, passes: 18 }
      ]
    }
  };

  // Backwards compatibility alias for STD_THREADS
  var STD_THREADS = {
    METRIC: THREAD_FAMILIES.METRIC.sizes,
    UN: THREAD_FAMILIES.UN.sizes,
    NPT: THREAD_FAMILIES.NPT.sizes,
    BSPT: THREAD_FAMILIES.BSPT.sizes
  };

  var JOG_STEPS_MM = [1.000, 0.100, 0.010];
  var JOG_STEPS_IN = [0.1000, 0.0100, 0.0010];

  // 4 Standard Step Progressions (Lathe Ergonomic Standard)
  var FEED_STEPS_MM = [0.010, 0.050, 0.100, 0.200];
  var FEED_STEPS_IN = [0.0010, 0.0020, 0.0050, 0.0100];
  var TPI_STEPS = [1, 2, 4, 8];
  var ASYNC_STEPS_MM = [10, 25, 50, 100];
  var ASYNC_STEPS_IN = [0.5, 1.0, 2.0, 5.0];

  var METRIC_PITCHES = [0.5, 0.6, 0.7, 0.75, 0.8, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0];
  var TPI_VALUES = [8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24, 28];
  var pitchSelectTarget = 'modePitch'; // 'modePitch' or 'gearZ'

  function fmt(v, dp){ return v.toFixed(dp); }
  function toDisplayLen(mm){ return state.unit === 'mm' ? mm : mm / 25.4; }
  function lenStr(mm, dp){ dp = dp || (state.unit==='mm' ? 3 : 4); return fmt(toDisplayLen(mm), dp); }
  function unitLabel(){ return state.unit; }
  function formatTaperRatio(ratio){
    if(state && state.gearTaperAuthMode === 'RATIO' && state.gearTaperAuthVal){
      var v = state.gearTaperAuthVal;
      var str = (v % 1 === 0) ? v.toFixed(0) : (v * 10 % 1 === 0 ? v.toFixed(1) : (v >= 10 ? v.toFixed(2) : v.toFixed(3)));
      return '1 : ' + str;
    }
    if(!ratio || isNaN(ratio) || ratio <= 0) return '1 : —';
    var k = 1 / ratio;
    var kStr = (k % 1 === 0) ? k.toFixed(0) : (k * 10 % 1 === 0 ? k.toFixed(1) : (k >= 10 ? k.toFixed(2) : k.toFixed(3)));
    return '1 : ' + kStr;
  }
  function getActiveJogStepMm(){
    if(state.unit === 'mm'){
      return JOG_STEPS_MM[state.stepIdx % JOG_STEPS_MM.length];
    } else {
      return JOG_STEPS_IN[state.stepIdx % JOG_STEPS_IN.length] * 25.4;
    }
  }
  function getActiveJogStepDisplay(){
    if(state.unit === 'mm'){
      return fmt(JOG_STEPS_MM[state.stepIdx % JOG_STEPS_MM.length], 3);
    } else {
      return fmt(JOG_STEPS_IN[state.stepIdx % JOG_STEPS_IN.length], 4);
    }
  }
  function getActiveGearStepInfo(){
    if(state.mode === 'TURNING'){
      var arr = state.unit === 'mm' ? FEED_STEPS_MM : FEED_STEPS_IN;
      var idx = state.feedStepIdxZ % arr.length;
      var unitStr = state.unit === 'mm' ? ' mm' : ' in';
      return { val: arr[idx], unitStr: unitStr, type: 'feed', axis: 'Z', arr: arr, idx: idx };
    }
    if(state.mode === 'FACING'){
      var arr = state.unit === 'mm' ? FEED_STEPS_MM : FEED_STEPS_IN;
      var idx = state.feedStepIdxX % arr.length;
      var unitStr = state.unit === 'mm' ? ' mm' : ' in';
      return { val: arr[idx], unitStr: unitStr, type: 'feed', axis: 'X', arr: arr, idx: idx };
    }
    var isSync = state.gearboxMode === 'SYNC';
    var isZ = state.activeFeedAxis === 'Z' || state.activeFeedAxis === 'Z+X';
    var isTpi = isZ && state.gearFeedUnitZ === 'TPI' && isSync;
    
    if(!isSync){
      var arr = state.unit === 'mm' ? ASYNC_STEPS_MM : ASYNC_STEPS_IN;
      var idx = (isZ ? state.asyncStepIdxZ : state.asyncStepIdxX) % arr.length;
      var unitStr = state.unit === 'mm' ? ' mm/m' : ' in/m';
      return { val: arr[idx], unitStr: unitStr, type: 'async', axis: isZ ? 'Z' : 'X', arr: arr, idx: idx };
    } else if(isTpi){
      var arr = TPI_STEPS;
      var idx = state.feedStepTpiIdx % arr.length;
      return { val: arr[idx], unitStr: ' TPI', type: 'tpi', axis: 'Z', arr: arr, idx: idx };
    } else {
      var arr = state.unit === 'mm' ? FEED_STEPS_MM : FEED_STEPS_IN;
      var idx = (isZ ? state.feedStepIdxZ : state.feedStepIdxX) % arr.length;
      var unitStr = state.unit === 'mm' ? ' mm' : ' in';
      return { val: arr[idx], unitStr: unitStr, type: 'feed', axis: isZ ? 'Z' : 'X', arr: arr, idx: idx };
    }
  }

  function renderDeckStepRowHTML(stepInfo){
    if(!stepInfo || !stepInfo.arr) return '';
    var chips = stepInfo.arr.map(function(val, idx){
      var isAct = (idx === stepInfo.idx);
      var lbl = '';
      if(stepInfo.type === 'feed'){
        lbl = '±' + fmt(val, state.unit === 'mm' ? 3 : 4);
      } else if(stepInfo.type === 'tpi'){
        lbl = '±' + val + ' TPI';
      } else {
        lbl = '±' + val;
      }
      return '<div class="deck-step-chip tap' + (isAct ? ' active' : '') + '" data-step-idx="' + idx + '">' + lbl + '</div>';
    }).join('');
    return '<div class="deck-step-row">' + chips + '</div>';
  }

  function clamp(v, a, b){
    var lo = a===null ? -Infinity : Math.min(a,b);
    var hi = b===null ? Infinity : Math.max(a,b);
    return Math.min(Math.max(v, lo), hi);
  }

  function setStatusMsg(text){
    if(window.Alpine && Alpine.store('machine')){
      Alpine.store('machine').statusMsg = text;
    } else {
      $('statusMessage').textContent = text;
    }
    if(!state.msgLog) state.msgLog = [];
    if(state.msgLog.length === 0 || state.msgLog[state.msgLog.length - 1] !== text){
      state.msgLog.push(text);
      if(state.msgLog.length > 40) state.msgLog.shift();
    }
  }

  function getLimitIconSVG(type){
    var stroke = 'currentColor';
    if(type === 'zLeft'){
      // Wall on left, arrow pointing left into wall: |←
      return '<svg width="12" height="12" viewBox="0 0 12 12" class="lim-icon" style="stroke:' + stroke + ';fill:none;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round;"><line x1="2" y1="1.5" x2="2" y2="10.5"/><line x1="10.5" y1="6" x2="3.5" y2="6"/><polyline points="6.5 3 3.5 6 6.5 9"/></svg>';
    }
    if(type === 'zRight'){
      // Wall on right, arrow pointing right into wall: →|
      return '<svg width="12" height="12" viewBox="0 0 12 12" class="lim-icon" style="stroke:' + stroke + ';fill:none;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round;"><line x1="10" y1="1.5" x2="10" y2="10.5"/><line x1="1.5" y1="6" x2="8.5" y2="6"/><polyline points="5.5 3 8.5 6 5.5 9"/></svg>';
    }
    if(type === 'xLeft'){
      // Bar at top (centerline), arrow pointing UP into bar: ⤒
      return '<svg width="12" height="12" viewBox="0 0 12 12" class="lim-icon" style="stroke:' + stroke + ';fill:none;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round;"><line x1="1.5" y1="2" x2="10.5" y2="2"/><line x1="6" y1="10.5" x2="6" y2="3.5"/><polyline points="3 6.5 6 3.5 9 6.5"/></svg>';
    }
    if(type === 'xRight'){
      // Bar at bottom (retract), arrow pointing DOWN into bar: ⭳
      return '<svg width="12" height="12" viewBox="0 0 12 12" class="lim-icon" style="stroke:' + stroke + ';fill:none;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round;"><line x1="1.5" y1="10" x2="10.5" y2="10"/><line x1="6" y1="1.5" x2="6" y2="8.5"/><polyline points="3 5.5 6 8.5 9 5.5"/></svg>';
    }
    return '';
  }

  function limitText(bound){
    var v = state[bound];
    if(v === null) return 'SET';
    var prefix = (bound === 'xLeft' || bound === 'xRight') && state.xRep === 'diameter' ? 'Ø' : '';
    return prefix + lenStr(Math.abs(v));
  }

  function renderLimitChip(elemId, bound){
    var el = $(elemId);
    if(!el) return;
    var v = state[bound];
    var isUnset = (v === null);
    var isAtLimit = (!isUnset && Math.abs(v) <= 0.001);
    el.classList.toggle('empty-limit', isUnset);
    el.classList.toggle('at-limit', isAtLimit);

    var icon = getLimitIconSVG(bound);
    if(isUnset){
      if(bound === 'zLeft' || bound === 'xLeft'){
        el.innerHTML = icon + '<span>SET</span>';
      } else {
        el.innerHTML = '<span>SET</span>' + icon;
      }
      return;
    }

    var numStr = lenStr(Math.abs(v));
    var prefix = ((bound === 'xLeft' || bound === 'xRight') && state.xRep === 'diameter') ? 'Ø' : '';
    if(bound === 'zLeft' || bound === 'xLeft'){
      el.innerHTML = icon + '<span>' + prefix + numStr + '</span>';
    } else {
      el.innerHTML = '<span>' + prefix + numStr + '</span>' + icon;
    }
  }

  function isFeeding(){
    if(state.runState !== 'running') return false;
    if(state.mode === 'GEARBOX' && state.gearboxMode === 'ASYNC') return true;
    return state.spindleSimRunning;
  }

  // Gearbox Display Helpers (Fully Decoupled Units)
  function getGearZDisplay(){
    if(state.gearboxMode === 'ASYNC'){
      var spd = state.unit === 'in' ? (state.asyncSpeedZ / 25.4).toFixed(1) : state.asyncSpeedZ.toString();
      return { val: spd, unit: unitLabel() + '/min' };
    }
    if(state.gearFeedModeZ === 'FEED'){
      var fVal = state.unit === 'mm' ? state.gearPitchZ : state.gearPitchZ / 25.4;
      return { val: fmt(fVal, state.unit === 'mm' ? 3 : 4), unit: unitLabel() + '/rev' };
    }
    // PITCH Mode (Screw cutting synchronization with handedness)
    var hand = ' (' + (state.gearThreadHandedness || 'RH') + ')';
    if(state.gearThreadUnit === 'tpi'){
      var tpi = state.gearPitchZ > 0 ? (25.4 / state.gearPitchZ) : 0;
      var tpiStr = Math.abs(tpi - Math.round(tpi)) < 0.05 ? Math.round(tpi).toString() : tpi.toFixed(1);
      var mmLead = state.gearPitchZ > 0 ? state.gearPitchZ.toFixed(3) : '0.000';
      return { val: tpiStr, unit: 'TPI' + hand, companion: '(' + mmLead + ' mm)' };
    } else {
      var tpiComp = state.gearPitchZ > 0 ? (25.4 / state.gearPitchZ).toFixed(1) : '0';
      return { val: lenStr(state.gearPitchZ), unit: unitLabel() + hand, companion: '(' + tpiComp + ' TPI)' };
    }
  }

  function getGearXDisplay(){
    if(state.gearboxMode === 'ASYNC'){
      var spd = state.unit === 'in' ? (state.asyncSpeedX / 25.4).toFixed(1) : state.asyncSpeedX.toString();
      return { val: spd, unit: unitLabel() + '/min' };
    }
    var fVal = state.unit === 'mm' ? state.gearPitchX : state.gearPitchX / 25.4;
    return { val: fmt(fVal, state.unit === 'mm' ? 3 : 4), unit: unitLabel() + '/rev' };
  }

  function getThreadPitchDisplay(){
    var hand = state.threadHandedness === 'RIGHT' ? 'RH' : 'LH';
    if(state.threadUnit === 'tpi'){
      var tpi = state.threadTpiVal || (state.pitch > 0 ? (25.4 / state.pitch) : 0);
      var tpiStr = (tpi % 1 === 0) ? tpi.toFixed(0) : (tpi * 10 % 1 === 0 ? tpi.toFixed(1) : tpi.toFixed(2));
      var mmLead = state.pitch > 0 ? (25.4 / tpi).toFixed(3) : '0.000';
      return { val: tpiStr, unit: 'TPI (' + hand + ')', companion: '(' + mmLead + ' mm)', num: tpi };
    } else {
      var tpiComp = state.pitch > 0 ? (25.4 / state.pitch).toFixed(1) : '0';
      return { val: lenStr(state.pitch), unit: unitLabel() + ' (' + hand + ')', companion: '(' + tpiComp + ' TPI)', num: state.pitch };
    }
  }

  // Two-way Pass Calculation for Turning & Facing
  function getTurnPasses(){
    var total = Math.abs(state.turnTotalDepth);
    var count = Math.max(1, state.turnPasses || 1);
    var actual = count > 0 ? (total / count) : 0;
    return { count: count, doc: actual };
  }

  function getFacePasses(){
    var total = Math.abs(state.faceTotalDepth);
    var count = Math.max(1, state.facePasses || 1);
    var actual = count > 0 ? (total / count) : 0;
    return { count: count, doc: actual };
  }

  function recalcTurnLimits(){
    if(state.turnDir === 'CHUCK'){
      state.zLeft = state.turnLength;
      state.zRight = 0.000;
      setStatusMsg('ENVELOPE APPLIED: CHUCK SHOULDER SET TO ' + lenStr(state.zLeft) + ' ' + unitLabel());
    } else {
      state.zRight = state.turnLength;
      state.zLeft = 0.000;
      setStatusMsg('ENVELOPE APPLIED: TAIL SHOULDER SET TO ' + lenStr(state.zRight) + ' ' + unitLabel());
    }
  }

  function recalcFaceLimits(){
    if(state.faceDir === 'CENTER'){
      state.xLeft = state.faceStroke;
      state.xRight = 0.000;
      setStatusMsg('ENVELOPE APPLIED: CENTER STOP SET TO ' + lenStr(state.xLeft) + ' ' + unitLabel());
    } else {
      state.xRight = state.faceStroke;
      state.xLeft = 0.000;
      setStatusMsg('ENVELOPE APPLIED: RETRACT STOP SET TO ' + lenStr(state.xRight) + ' ' + unitLabel());
    }
  }

  function renderDynamicDeck(){
    var deck = $('dynamicDeck');
    var isRun = state.runState === 'running';

    if(state.mode === 'GEARBOX'){
      var isSync = state.gearboxMode === 'SYNC';
      var zDisp = getGearZDisplay();
      var xDisp = getGearXDisplay();
      var isTaper = state.activeFeedAxis === 'Z+X';
      var activeDisp = (state.activeFeedAxis === 'Z' || isTaper) ? zDisp : xDisp;
      var dirLabel = isTaper ? 
        ('DIR: ' + (state.gearDirZ === 'FWD' ? '← TO CHUCK (Z−)' : '→ TO TAIL (Z+)') + ' | ' + (state.gearTaperSide === 'EXTERNAL' ? 'EXT' : 'INT')) :
        (state.activeFeedAxis === 'Z' ? 
          (state.gearDirZ === 'FWD' ? 'DIR: ← TO CHUCK (Z−)' : 'DIR: → TO TAIL (Z+)') :
          (state.gearDirX === 'IN' ? 'DIR: ↑ TO CENTER (X−)' : 'DIR: ↓ RETRACT (X+)'));

      var stepInfo = getActiveGearStepInfo();
      var stepChipsHTML = renderDeckStepRowHTML(stepInfo);

      var deckHeaderLeft = (!isSync) ? 'ASYNC SPEED' : (state.gearFeedModeZ === 'PITCH' && state.activeFeedAxis === 'Z' ? 'THREAD LEAD' : 'FEED RATE');
      var deckHeaderRight = isTaper ?
        '<span style="font-size:10.5px; color:var(--amber); font-weight:700;">Z+X TAPER</span>' :
        '<span style="font-size:10.5px; color:' + (state.activeFeedAxis==='Z'?'var(--green)':'var(--blue)') + '; font-weight:700;">' + state.activeFeedAxis + '-AXIS</span>';

      deck.innerHTML = '' +
        '<div style="display:flex; justify-content:space-between; align-items:center;">' +
          '<span style="font-size:11px; font-weight:700; color:var(--dim);">' + deckHeaderLeft + '</span>' +
          deckHeaderRight +
        '</div>' +
        '<div class="deck-controls">' +
          '<div class="deck-nudge tap" id="deckNudgeMinus">−</div>' +
          '<div class="deck-readout ' + (!isRun ? 'tap' : '') + '" id="deckReadout"' + (isRun ? ' style="cursor:default;"' : '') + '>' +
            '<span class="deck-val mono">' + activeDisp.val + '</span>' +
            '<span class="deck-unit">' + activeDisp.unit + '</span>' +
            (activeDisp.companion ? '<span class="deck-companion mono" style="font-size:11px; color:var(--dim); margin-left:6px; font-weight:600;">' + activeDisp.companion + '</span>' : '') +
          '</div>' +
          '<div class="deck-nudge tap" id="deckNudgePlus">+</div>' +
        '</div>' +
        stepChipsHTML;

    } else if(state.mode === 'TURNING'){
      var pInfo = getTurnPasses();
      var stepInfo = getActiveGearStepInfo();

      deck.innerHTML = '' +
        '<div style="display:flex;justify-content:space-between;align-items:center;">' +
          '<span style="font-size:11px;font-weight:700;color:var(--dim);">TURNING FEED TRIM</span>' +
          (isRun ? '<span class="deck-pass-badge mono">PASS ' + state.turnCurrentPass + ' / ' + pInfo.count + '</span>' : '<span style="font-size:10px;color:var(--green);font-weight:700;">Z-AXIS</span>') +
        '</div>' +
        '<div class="deck-controls">' +
          '<div class="deck-nudge tap" id="deckNudgeMinus">−</div>' +
          '<div class="deck-readout ' + (!isRun ? 'tap' : '') + '" id="deckReadout"' + (isRun ? ' style="cursor:default;"' : '') + '>' +
            '<span class="deck-val mono">' + lenStr(state.turnPitch, state.unit==='mm'?3:4) + '</span>' +
            '<span class="deck-unit">' + unitLabel() + '/rev</span>' +
          '</div>' +
          '<div class="deck-nudge tap" id="deckNudgePlus">+</div>' +
        '</div>' +
        renderDeckStepRowHTML(stepInfo);

    } else if(state.mode === 'FACING'){
      var fpInfo = getFacePasses();
      var stepInfo = getActiveGearStepInfo();

      deck.innerHTML = '' +
        '<div style="display:flex;justify-content:space-between;align-items:center;">' +
          '<span style="font-size:11px;font-weight:700;color:var(--dim);">FACING FEED TRIM</span>' +
          (isRun ? '<span class="deck-pass-badge mono">PASS ' + state.faceCurrentPass + ' / ' + fpInfo.count + '</span>' : '<span style="font-size:10px;color:var(--blue);font-weight:700;">X-AXIS</span>') +
        '</div>' +
        '<div class="deck-controls">' +
          '<div class="deck-nudge tap" id="deckNudgeMinus">−</div>' +
          '<div class="deck-readout ' + (!isRun ? 'tap' : '') + '" id="deckReadout"' + (isRun ? ' style="cursor:default;"' : '') + '>' +
            '<span class="deck-val mono">' + lenStr(state.facePitch, state.unit==='mm'?3:4) + '</span>' +
            '<span class="deck-unit">' + unitLabel() + '/rev</span>' +
          '</div>' +
          '<div class="deck-nudge tap" id="deckNudgePlus">+</div>' +
        '</div>' +
        renderDeckStepRowHTML(stepInfo);

    } else if(state.mode === 'THREADING'){
      var totPass = state.threadPassCount || 11;
      if(isRun){
        var curPass = state.threadCurrentPass || 1;
        var isSkim = (state.threadSkimStage === 'SKIMMING');
        var passLabel = isSkim ? 'SKIM PASS' : ('PASS ' + (curPass < 10 ? '0' : '') + curPass + ' / ' + (totPass < 10 ? '0' : '') + totPass);
        
        // Quadratic infeed depth calculation matching NanoELS h5.ino line 9236
        var fraction = (totPass - curPass) / totPass;
        fraction = fraction * fraction;
        var curInfeed = isSkim ? 0 : state.threadTotalDepth * (1 - fraction);
        var pct = isSkim ? 12 : Math.max(8, Math.round((curPass / totPass) * 100));

        deck.innerHTML = '' +
          '<div class="deck-thread-mon">' +
            '<div class="mon-head">' +
              '<span class="mon-title">THREADING RUNNING</span>' +
              '<span class="mon-status-pill active mono">' + passLabel + '</span>' +
            '</div>' +
            '<div class="thread-phase-strip">' +
              '<span class="phase-chip ' + (curPass > 0 ? 'done' : 'active') + '">1. INFEED</span>' +
              '<span class="phase-chip active">2. CUT (' + (state.threadCutDir==='CHUCK'?'Z−':'Z+') + ')</span>' +
              '<span class="phase-chip">3. RETRACT</span>' +
              '<span class="phase-chip">4. RETURN</span>' +
            '</div>' +
            '<div class="mon-row" style="margin-top:2px;">' +
              '<span>INFEED DEPTH:</span>' +
              '<b class="mono" style="color:var(--amber);">' + lenStr(curInfeed, 3) + ' / ' + lenStr(state.threadTotalDepth, 3) + ' ' + unitLabel() + '</b>' +
            '</div>' +
            '<div class="thread-mini-bar">' +
              '<div class="fill" style="width:' + pct + '%;"></div>' +
            '</div>' +
            '<div class="mon-row" style="font-size:10.5px; margin-top:2px;">' +
              '<span>SYNC STATUS:</span>' +
              '<b class="mono" style="color:var(--green);">LEADSCRW LOCKED · ' + (state.spindleRpm || 240) + ' RPM</b>' +
            '</div>' +
          '</div>';
      } else {
        var nextPassLabel = (state.threadSkimVerify && state.threadSkimStage !== 'THREADING') ? 
          'STAGE 1 (SKIM VERIFY)' : 
          ('PASS 01 OF ' + (totPass < 10 ? '0' : '') + totPass);
        var isReverseSpindle = (state.threadCutDir === 'TAIL' && state.threadHandedness === 'RIGHT') || (state.threadCutDir === 'CHUCK' && state.threadHandedness === 'LEFT');
        var spindleText = isReverseSpindle ? 'SPINDLE: CW (REV)' : 'SPINDLE: CCW (FWD)';
        var threadLenMm = state.threadLength || 20.0;
        var zTarget = state.threadCutDir === 'CHUCK' ? -threadLenMm : threadLenMm;

        deck.innerHTML = '' +
          '<div class="deck-thread-mon">' +
            '<div class="mon-head">' +
              '<span class="mon-title">THREADING PROGRAM</span>' +
              '<span class="mon-status-pill ready">● READY</span>' +
            '</div>' +
            '<div class="mon-row" style="margin-top:3px;">' +
              '<span>NEXT PASS:</span>' +
              '<b class="mono" style="color:var(--blue); font-size:12.5px;">' + nextPassLabel + '</b>' +
            '</div>' +
            '<div class="mon-row">' +
              '<span>ENVELOPE:</span>' +
              '<b class="mono">Z 0.0 → ' + lenStr(zTarget, 1) + ' ' + unitLabel() + ' (' + (state.threadCutDir==='CHUCK'?'Z−':'Z+') + ')</b>' +
            '</div>' +
            '<div class="mon-row">' +
              '<span>SPINDLE:</span>' +
              '<b class="mono" style="color:' + (isReverseSpindle ? 'var(--amber)' : 'var(--green)') + ';">' + spindleText + '</b>' +
            '</div>' +
            '<div class="mon-row" style="font-size:10.5px; color:var(--dimmer); margin-top:2px;">' +
              '<span>MOTION:</span>' +
              '<span>AUTO CYCLE · LEADSCRW SYNC</span>' +
            '</div>' +
          '</div>';
      }

    } else {
      deck.innerHTML = '' +
        '<div style="font-size:11px;font-weight:700;color:var(--dim);margin-bottom:2px;">' + state.mode + ' ACTIVE</div>' +
        '<div class="deck-controls">' +
          '<div class="deck-nudge tap" id="deckNudgeMinus">−</div>' +
          '<div class="deck-readout tap" id="deckReadout">' +
            '<span class="deck-val mono">' + lenStr(state.pitch, 3) + '</span>' +
            '<span class="deck-unit">' + unitLabel() + ' pitch</span>' +
          '</div>' +
          '<div class="deck-nudge tap" id="deckNudgePlus">+</div>' +
        '</div>';
    }

    // Attach 4-Chip Deck Step Handlers
    deck.querySelectorAll('.deck-step-chip[data-step-idx]').forEach(function(chip){
      chip.addEventListener('click', function(){
        var idx = parseInt(chip.dataset.stepIdx, 10);
        var info = getActiveGearStepInfo();
        if(info.type === 'async'){
          if(info.axis === 'Z') state.asyncStepIdxZ = idx; else state.asyncStepIdxX = idx;
        } else if(info.type === 'tpi'){
          state.feedStepTpiIdx = idx;
        } else {
          if(info.axis === 'Z') state.feedStepIdxZ = idx; else state.feedStepIdxX = idx;
        }
        render();
      });
    });

    // Attach Deck Nudge Handlers
    var btnMinus = $('deckNudgeMinus');
    var btnPlus = $('deckNudgePlus');
    var readout = $('deckReadout');

    if(btnMinus && btnPlus){
      btnMinus.addEventListener('click', function(){
        if(state.mode === 'GEARBOX'){
          var info = getActiveGearStepInfo();
          if(info.type === 'async'){
            var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
            if(info.axis === 'Z') state.asyncSpeedZ = Math.max(1, Math.round(state.asyncSpeedZ - delta));
            else state.asyncSpeedX = Math.max(1, Math.round(state.asyncSpeedX - delta));
          } else if(info.type === 'tpi'){
            var currTpi = 25.4 / state.gearPitchZ;
            var nextTpi = Math.max(1, +(currTpi - info.val).toFixed(1));
            state.gearPitchZ = +(25.4 / nextTpi).toFixed(4);
          } else { // SYNC FEED
            var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
            if(info.axis === 'Z') state.gearPitchZ = Math.max(0.001, +(state.gearPitchZ - delta).toFixed(4));
            else state.gearPitchX = Math.max(0.001, +(state.gearPitchX - delta).toFixed(4));
          }
        } else if(state.mode === 'TURNING'){
          var info = getActiveGearStepInfo();
          var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
          state.turnPitch = Math.max(0.005, +(state.turnPitch - delta).toFixed(4));
        } else if(state.mode === 'FACING'){
          var info = getActiveGearStepInfo();
          var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
          state.facePitch = Math.max(0.005, +(state.facePitch - delta).toFixed(4));
        } else if(state.mode === 'THREADING' && !isRun){
          if(state.threadUnit === 'tpi'){
            var currTpi = 25.4 / state.pitch;
            var nextTpi = Math.max(1, Math.round(currTpi) - 1);
            state.pitch = +(25.4 / nextTpi).toFixed(4);
          } else {
            state.pitch = Math.max(0.2, +(state.pitch - 0.05).toFixed(3));
          }
        }
        syncPitchToController();
        render();
      });

      btnPlus.addEventListener('click', function(){
        if(state.mode === 'GEARBOX'){
          var info = getActiveGearStepInfo();
          if(info.type === 'async'){
            var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
            if(info.axis === 'Z') state.asyncSpeedZ = Math.round(state.asyncSpeedZ + delta);
            else state.asyncSpeedX = Math.round(state.asyncSpeedX + delta);
          } else if(info.type === 'tpi'){
            var currTpi = 25.4 / state.gearPitchZ;
            var nextTpi = Math.min(80, +(currTpi + info.val).toFixed(1));
            state.gearPitchZ = +(25.4 / nextTpi).toFixed(4);
          } else { // SYNC FEED
            var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
            if(info.axis === 'Z') state.gearPitchZ = +(state.gearPitchZ + delta).toFixed(4);
            else state.gearPitchX = +(state.gearPitchX + delta).toFixed(4);
          }
        } else if(state.mode === 'TURNING'){
          var info = getActiveGearStepInfo();
          var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
          state.turnPitch = +(state.turnPitch + delta).toFixed(4);
        } else if(state.mode === 'FACING'){
          var info = getActiveGearStepInfo();
          var delta = state.unit === 'mm' ? info.val : (info.val * 25.4);
          state.facePitch = +(state.facePitch + delta).toFixed(4);
        } else if(state.mode === 'THREADING' && !isRun){
          if(state.threadUnit === 'tpi'){
            var currTpi = 25.4 / state.pitch;
            var nextTpi = Math.min(80, Math.round(currTpi) + 1);
            state.pitch = +(25.4 / nextTpi).toFixed(4);
          } else {
            state.pitch = +(state.pitch + 0.05).toFixed(3);
          }
        }
        syncPitchToController();
        render();
      });

      if(readout){
        readout.addEventListener('click', function(){
          if(isRun) return;
          if(state.mode === 'GEARBOX'){
            if(state.activeFeedAxis === 'Z' || state.activeFeedAxis === 'Z+X'){
              if(state.gearboxMode === 'ASYNC'){
                var curSpd = state.unit === 'mm' ? state.asyncSpeedZ : +(state.asyncSpeedZ / 25.4).toFixed(1);
                openKeypad('asyncSpeedZ', curSpd);
              } else if(state.gearFeedModeZ === 'FEED'){
                openKeypad('gearPitchZ', toDisplayLen(state.gearPitchZ));
              } else {
                openGearboxPitchModal();
              }
            } else {
              if(state.gearboxMode === 'ASYNC'){
                var curSpd = state.unit === 'mm' ? state.asyncSpeedX : +(state.asyncSpeedX / 25.4).toFixed(1);
                openKeypad('asyncSpeedX', curSpd);
              } else {
                openKeypad('gearPitchX', state.unit==='mm'?state.gearPitchX:state.gearPitchX/25.4);
              }
            }
          } else if(state.mode === 'TURNING'){
            openKeypad('turnPitch', state.unit==='mm'?state.turnPitch:state.turnPitch/25.4);
          } else if(state.mode === 'FACING'){
            openKeypad('facePitch', state.unit==='mm'?state.facePitch:state.facePitch/25.4);
          } else if(state.mode === 'THREADING'){
            if(isRun){
              setStatusMsg('THREADING LOCKED: CANNOT CHANGE PITCH DURING ACTIVE RUN');
            } else {
              openPitchSelect('modePitch', state.threadUnit);
            }
          }
        });
      }
    }
  }

  function modeBodyHTML(){
    var isRun = state.runState === 'running';
    var ed = !isRun ? 'editable tap' : '';

    if(state.mode === 'GEARBOX'){
      var isSync = state.gearboxMode === 'SYNC';
      var zDisp = getGearZDisplay();
      var zActive = state.activeFeedAxis === 'Z';
      var xActive = state.activeFeedAxis === 'X';
      var isTaper = state.activeFeedAxis === 'Z+X';
      var zUnitDisabled = !isSync || isRun;
      var axisBtnLabel = isTaper ? 'AXIS: Z+X (TAPER)' : ('AXIS: ' + state.activeFeedAxis);

      var dirLabel = '';
      if(zActive){
        dirLabel = state.gearDirZ === 'FWD' ? 'DIR: ← CHUCK (Z−)' : 'DIR: → TAIL (Z+)';
      } else if(xActive){
        dirLabel = state.gearDirX === 'IN' ? 'DIR: ↑ CENTER (X−)' : 'DIR: ↓ RETRACT (X+)';
      } else {
        dirLabel = state.gearDirZ === 'FWD' ? 'DIR: ← CHUCK (Z−)' : 'DIR: → TAIL (Z+)';
      }

      var presetName = (state.gearTaperPresetIdx >= 0 && state.gearTaperPresetIdx < TAPER_PRESETS.length) 
        ? TAPER_PRESETS[state.gearTaperPresetIdx].name 
        : (state.gearTaperAuthMode ? 'CUSTOM (' + (state.gearTaperAuthMode === 'ANGLE' ? state.gearTaperAuthVal + '°' : (state.gearTaperAuthMode === 'RATIO' ? '1:' + state.gearTaperAuthVal : state.gearTaperAuthVal)) + ')' : 'CUSTOM');

      var normalConeSvg = '<svg width="26" height="16" viewBox="0 0 28 18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display:inline-block; vertical-align:middle;"><polygon points="3,2 25,6 25,12 3,16" fill="currentColor" fill-opacity="0.25"/></svg>';
      var invertConeSvg = '<svg width="26" height="16" viewBox="0 0 28 18" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round" style="display:inline-block; vertical-align:middle;"><polygon points="3,6 25,2 25,16 3,12" fill="currentColor" fill-opacity="0.25"/></svg>';

      var axisClass = isTaper ? 'axis-zx-on' : (zActive ? 'axis-z-on' : 'axis-x-on');
      var dirChipClass = isTaper ? 'dir-zx' : (zActive ? 'dir-z' : 'dir-x');

      return '' +
        '<div style="display:flex; flex-direction:column; gap:6px;">' +
          // Top Row: Primary Axis + Feed Direction side-by-side (saves full row of vertical space)
          '<div style="display:flex; gap:4px;">' +
            '<div class="deck-toggle-btn ' + (!isRun?'tap ':'') + axisClass + '" id="fld-gearAxis" style="flex:1; min-height:38px;' + (isRun?' opacity:0.45; cursor:not-allowed;':'') + '">' + axisBtnLabel + '</div>' +
            '<div class="deck-toggle-btn ' + (!isRun?'tap ':'') + dirChipClass + '" id="fld-gearDir" style="flex:1; min-height:38px; font-weight:700;' + (isRun?' opacity:0.45; cursor:not-allowed;':' cursor:pointer;') + '">' + dirLabel + '</div>' +
          '</div>' +

          (zActive ? 
            '<div class="feed-block active-axis-row" style="background:rgba(61, 220, 132, 0.03); border:1px solid rgba(61, 220, 132, 0.35); border-radius:4px; padding:6px; display:flex; flex-direction:column; gap:6px;">' +
              // SYNC vs ASYNC Toggle (Exclusive to Z axis where controller supports both)
              '<div class="deck-toggle-btn ' + (!isRun?'tap ':'') + (isSync?'on':'') + '" id="fld-gearSync" style="min-height:34px; font-size:12px; font-weight:700;' + (isRun?' opacity:0.45; cursor:not-allowed;':'') + '">' +
                (isSync ? '⚡ SYNC (SPINDLE)' : '⏱ ASYNC (TIME FEED)') +
              '</div>' +
              (isSync ?
                '<div style="display:flex; gap:4px; ' + (zUnitDisabled ? 'opacity:0.35; pointer-events:none;' : '') + '">' +
                  '<div class="toggle-chip ' + (!isRun?'tap ':'') + (state.gearFeedModeZ==='FEED'?'active':'') + '" id="fld-unitFeed" style="flex:1; text-align:center; min-height:36px; padding:6px 0; font-size:12px; font-weight:700;">FEED (' + unitLabel() + '/rev)</div>' +
                  '<div class="toggle-chip ' + (!isRun?'tap ':'') + (state.gearFeedModeZ==='PITCH'?'active':'') + '" id="fld-unitPitch" style="flex:1; text-align:center; min-height:36px; padding:6px 0; font-size:12px; font-weight:700;">PITCH (' + (state.gearThreadHandedness||'RH') + ')</div>' +
                '</div>' +
                (state.gearFeedModeZ === 'PITCH' ?
                  '<div style="display:flex; justify-content:space-between; align-items:center; background:#0e141a; border:1px solid #1b2632; border-radius:3px; padding:6px 10px; margin-top:2px;">' +
                    '<div style="display:flex; flex-direction:column;">' +
                      '<span style="font-size:9.5px; color:var(--dim); font-weight:700;">THREAD LEAD</span>' +
                      '<b class="mono" style="font-size:15px; color:var(--fg); margin-top:2px;">' + zDisp.val + ' ' + zDisp.unit + ' (' + (state.gearThreadHandedness||'RH') + ')</b>' +
                    '</div>' +
                    '<div class="tap" id="btnEditPitch" style="background:#112519; color:var(--green); border:1px solid #1f4a2d; font-size:11.5px; font-weight:700; padding:5px 10px; border-radius:3px; cursor:pointer; display:inline-flex; align-items:center; justify-content:center;' + (isRun?' opacity:0.45; pointer-events:none;':'') + '">✎ PITCH</div>' +
                  '</div>' : '')
              : 
                '<div style="background:#0e141a; border:1px solid #1b2632; border-radius:3px; padding:6px 10px; font-size:11.5px; color:var(--dim); line-height:1.4;">' +
                  'Constant timed carriage feed (' + unitLabel() + '/min) independent of spindle rotation.' +
                '</div>'
              ) +
            '</div>' 
          : xActive ?
            '<div class="feed-block active-axis-row" style="background:rgba(90, 169, 255, 0.03); border:1px solid rgba(90, 169, 255, 0.35); border-radius:4px; padding:8px; display:flex; flex-direction:column; gap:4px;">' +
              '<div style="display:flex; justify-content:space-between; align-items:center;">' +
                '<span style="font-size:10.5px; font-weight:700; color:var(--dim); letter-spacing:0.4px;">CROSS-SLIDE MOTION</span>' +
                '<span class="mono" style="font-size:10.5px; font-weight:700; color:var(--blue); background:#0c1c2a; border:1px solid #1e364c; padding:2px 6px; border-radius:3px;">X POWER FACING</span>' +
              '</div>' +
              '<div style="font-size:11.5px; color:var(--dim); line-height:1.4; margin-top:2px;">' +
                'Transverse facing feed synchronized to spindle rotation (' + unitLabel() + '/rev).' +
              '</div>' +
            '</div>'
          : 
            '<div class="feed-block active-axis-row" style="border: 1px solid rgba(255, 180, 84, 0.4); background: rgba(255, 180, 84, 0.03); border-radius:4px; padding:6px; display:flex; flex-direction:column; gap:6px;">' +
              '<div style="display:flex; gap:6px;">' +
                '<div class="inline-dir-chip dir-zx ' + (!isRun?'tap ':'') + '" id="fld-gearTaperSide" style="flex:1; min-height:36px; display:flex; align-items:center; justify-content:center; font-size:11.5px; font-weight:700;' + (isRun?' opacity:0.45; cursor:not-allowed;':' cursor:pointer;') + '">' +
                  (state.gearTaperSide==='EXTERNAL'?'EXT (OD)':'INT (ID)') +
                '</div>' +
                '<div class="inline-dir-chip dir-zx ' + (!isRun?'tap ':'') + '" id="fld-gearTaperInvert" style="flex:1; min-height:36px; display:flex; align-items:center; justify-content:center; gap:8px; font-size:11.5px; font-weight:700;' + (isRun?' opacity:0.45; cursor:not-allowed;':' cursor:pointer;') + '" title="Toggle Cone Slope Direction">' +
                  (state.gearTaperInvert ? invertConeSvg : normalConeSvg) +
                  '<span>' + (state.gearTaperInvert ? 'INVERT' : 'NORMAL') + '</span>' +
                '</div>' +
              '</div>' +

              '<div id="boxTaperValues" class="' + (!isRun?'tap':'') + '" style="background:#141b21; border:1px solid #2e3c49; border-radius:4px; padding:8px 10px; cursor:pointer;" title="Tap to edit taper parameters">' +
                '<div style="display:flex; align-items:center; gap:6px;">' +
                  '<span style="font-size:10px; font-weight:700; color:var(--dim); letter-spacing:0.4px;">TAPER:</span>' +
                  '<b style="font-size:12.5px; color:var(--amber); letter-spacing:0.3px;">' + presetName + '</b>' +
                '</div>' +
                '<div style="display:flex; align-items:baseline; gap:8px; margin-top:2px;">' +
                  '<span class="mono" style="font-size:16px; font-weight:700; color:var(--fg);">' + fmt(state.gearTaperAngle, 2) + '°</span>' +
                  '<span style="color:var(--dim); font-size:11px;">·</span>' +
                  '<span class="mono" style="font-size:15px; font-weight:700; color:var(--amber);">' + formatTaperRatio(state.gearTaperRatio) + '</span>' +
                  '<span class="mono" style="font-size:10.5px; color:var(--dimmer);">(' + state.gearTaperRatio.toFixed(4) + ')</span>' +
                '</div>' +
              '</div>' +

              '<div style="display:flex; justify-content:flex-end;">' +
                '<div class="tap" id="btnOpenTaperModal" style="background:var(--amber); color:#120e02; font-family:inherit; font-weight:700; font-size:13px; letter-spacing:0.5px; padding:6px 14px; border-radius:4px; min-height:36px; cursor:pointer; display:inline-flex; align-items:center; justify-content:center; gap:6px;' + (isRun?' opacity:0.45; pointer-events:none;':'') + '" title="Edit Taper Parameters">' +
                  '<span style="font-size:13px;">✎</span><span>EDIT</span>' +
                '</div>' +
              '</div>' +
            '</div>'
          ) +
        '</div>';
    }

    if(state.mode === 'TURNING'){
      var pInfo = getTurnPasses();
      var dp = state.unit === 'mm' ? 3 : 4;
      return '' +
        '<div class="kv"><span>OPERATION</span><span class="toggle-chip active tap" id="fld-turnType" style="min-height:38px; min-width:140px;">' + (state.turnType==='EXTERNAL'?'EXTERNAL (OD)':'INTERNAL (ID / BORE)') + '</span></div>' +
        '<div class="kv"><span>LENGTH</span><span class="inline-dir-chip dir-z tap" id="fld-turnDir">' + (state.turnDir==='CHUCK'?'← CHUCK':'→ TAIL') + '</span><b class="mono ' + ed + '" id="fld-turnLen">' + lenStr(state.turnLength, state.unit==='mm'?2:3) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>TOTAL DEPTH</span><b class="mono ' + ed + '" id="fld-turnDepth">Ø ' + lenStr(state.turnTotalDepth, dp) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv-bracket-group">' +
          '<div class="kv"><span>MAX DOC / PASS</span><b class="mono ' + ed + '" id="fld-turnMaxDoc">Ø ' + lenStr(state.turnMaxDoc, dp) + ' ' + unitLabel() + '</b></div>' +
          '<div class="kv"><span>PASSES</span><b class="mono ' + ed + '" id="fld-turnPasses">' + pInfo.count + ' @ Ø' + lenStr(pInfo.doc, dp) + '</b></div>' +
        '</div>' +
        '<div class="kv"><span>RESTORE LIMITS</span><span class="toggle-chip ' + (state.turnRestoreLimits?'active':'') + ' tap" id="fld-turnRestore">' + (state.turnRestoreLimits?'ON FINISH: YES':'ON FINISH: NO') + '</span></div>';
    }

    if(state.mode === 'FACING'){
      var fpInfo = getFacePasses();
      var dp = state.unit === 'mm' ? 3 : 4;
      return '' +
        '<div class="kv"><span>STROKE</span><span class="inline-dir-chip dir-x tap" id="fld-faceDir">' + (state.faceDir==='CENTER'?'↑ TO CENTER':'↓ RETRACT') + '</span><b class="mono ' + ed + '" id="fld-faceStroke">Ø ' + lenStr(state.faceStroke, state.unit==='mm'?2:3) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>TOTAL DEPTH</span><b class="mono ' + ed + '" id="fld-faceDepth">' + lenStr(state.faceTotalDepth, dp) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv-bracket-group">' +
          '<div class="kv"><span>MAX DOC / PASS</span><b class="mono ' + ed + '" id="fld-faceMaxDoc">' + lenStr(state.faceMaxDoc, dp) + ' ' + unitLabel() + '</b></div>' +
          '<div class="kv"><span>PASSES</span><b class="mono ' + ed + '" id="fld-facePasses">' + fpInfo.count + ' @ ' + lenStr(fpInfo.doc, dp) + '</b></div>' +
        '</div>' +
        '<div class="kv"><span>RESTORE LIMITS</span><span class="toggle-chip ' + (state.faceRestoreLimits?'active':'') + ' tap" id="fld-faceRestore">' + (state.faceRestoreLimits?'ON FINISH: YES':'ON FINISH: NO') + '</span></div>';
    }

    if(state.mode === 'THREADING'){
      var isRun = state.runState === 'running';
      var disCls = isRun ? 'disabled' : '';
      var tp = getThreadPitchDisplay();
      var specLabel = state.threadStandardName ? state.threadStandardName : (tp.val + ' ' + tp.unit);
      var leadMm = state.pitch * (state.threadStarts || 1);
      var sideLabel = state.threadSide === 'INTERNAL' ? 'INTERNAL (NUT)' : 'EXTERNAL (BOLT)';
      var handLabel = (state.threadHandedness === 'RIGHT' ? 'RH' : 'LH');
      var cutLabel = state.threadCutDir === 'CHUCK' ? '← TO CHUCK (Z−)' : '→ TO TAIL (Z+)';
      var passCount = state.threadPassCount || 11;
      var infeedDepth = state.threadTotalDepth || 1.073;
      var stockDia = state.threadTargetStockDia || (state.xDia > 0 ? state.xDia : 12.0);
      var rootDia = state.threadTargetRootDia || (state.threadSide === 'INTERNAL' ? (stockDia + 2*infeedDepth) : (stockDia - 2*infeedDepth));

      return '' +
        '<div class="thread-action-bar">' +
          '<div class="thread-act-btn new tap ' + disCls + '" id="btnThreadNew">+ NEW THREAD</div>' +
          '<div class="thread-act-btn edit tap ' + disCls + '" id="btnThreadEdit">✎ EDIT SETUP</div>' +
        '</div>' +
        '<div style="background:#0a0e12; border:1px solid #1a2530; border-radius:4px; padding:6px 8px; display:flex; flex-direction:column; gap:3px;">' +
          '<div style="display:flex; justify-content:space-between; align-items:center; border-bottom:1px solid #141d24; padding-bottom:4px;">' +
            '<b style="font-size:14.5px; color:var(--blue); letter-spacing:0.3px;">' + specLabel + '</b>' +
            '<span style="font-size:10px; font-weight:700; color:var(--dim); background:#121a22; border:1px solid #1f2c38; padding:1px 6px; border-radius:3px;">' + sideLabel + ' · ' + handLabel + '</span>' +
          '</div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>PITCH / LEAD</span><b class="mono" style="font-size:12.5px;">' + (state.threadUnit === 'tpi' ? (tp.val + ' TPI ' + tp.companion) : (fmt(state.pitch, 3) + ' mm ' + tp.companion)) + (state.threadStarts > 1 ? ' (' + state.threadStarts + 'S / ' + fmt(leadMm, 2) + 'mm)' : '') + '</b></div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>FEED DIR / LEN</span><b class="mono" style="font-size:12.5px;">' + cutLabel + ' · ' + lenStr(state.threadLength || 20.0, 1) + ' ' + unitLabel() + '</b></div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>STOCK (PREP Ø)</span><b class="mono" style="font-size:12.5px;">Ø ' + fmt(stockDia, 2) + ' mm</b></div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>TARGET ROOT Ø</span><b class="mono" style="font-size:12.5px;">Ø ' + fmt(rootDia, 2) + ' mm</b></div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>INFEED / PASSES</span><b class="mono" style="font-size:12.5px; color:var(--amber);">' + lenStr(infeedDepth, 3) + ' ' + unitLabel() + ' (' + passCount + 'P)</b></div>' +
          '<div class="kv" style="min-height:22px; padding:1px 0; font-size:12px;"><span>SKIM VERIFY</span><span class="mono" style="font-size:11.5px; font-weight:700; color:' + (state.threadSkimVerify ? 'var(--green)' : 'var(--dimmer)') + ';">' + (state.threadSkimVerify ? 'STAGE 1: ENABLED' : 'DISABLED') + '</span></div>' +
        '</div>';
    }

    if(state.mode === 'TAPER'){
      return '' +
        '<div class="kv"><span>TAPER ANGLE</span><b class="mono ' + ed + '" id="fld-taperAngle">' + fmt(state.taperAngle,2) + '°</b></div>' +
        '<div class="kv"><span>LENGTH</span><b class="mono ' + ed + '" id="fld-taperLen">' + lenStr(state.taperLen, state.unit==='mm'?1:2) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>TAPER SIDE</span><span class="dir-chip-fixed ' + ed + '" id="fld-taperDir">' + state.taperDir + '</span></div>';
    }

    if(state.mode === 'CUTOFF'){
      var dp = state.unit === 'mm' ? 3 : 4;
      return '' +
        '<div class="kv"><span>PLUNGE FEED</span><b class="mono ' + ed + '" id="fld-cutFeed">' + lenStr(state.cutoffFeed, dp) + ' ' + unitLabel() + '/rev</b></div>' +
        '<div class="kv"><span>PECK DEPTH</span><b class="mono ' + ed + '" id="fld-cutPeck">' + lenStr(state.cutoffPeck, state.unit==='mm'?2:3) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>RETRACT</span><b class="mono ' + ed + '" id="fld-cutRetract">' + lenStr(state.cutoffRetract, state.unit==='mm'?2:3) + ' ' + unitLabel() + '</b></div>';
    }

    if(state.mode === 'SLOTTING'){
      var dp = state.unit === 'mm' ? 3 : 4;
      return '' +
        '<div class="kv"><span>TOTAL STROKES</span><b class="mono ' + ed + '" id="fld-slotStrokes">' + state.slotStrokes + '</b></div>' +
        '<div class="kv"><span>FEED / STROKE</span><b class="mono ' + ed + '" id="fld-slotFeed">' + lenStr(state.slotFeedPerStroke, dp) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>STROKE LENGTH</span><b class="mono ' + ed + '" id="fld-slotLen">' + lenStr(state.slotLen, state.unit==='mm'?1:2) + ' ' + unitLabel() + '</b></div>';
    }

    if(state.mode === 'SPHERE'){
      return '' +
        '<div class="kv"><span>SPHERE RADIUS</span><b class="mono ' + ed + '" id="fld-sphereRad">' + lenStr(state.sphereRadius, state.unit==='mm'?2:3) + ' ' + unitLabel() + '</b></div>' +
        '<div class="kv"><span>PROFILE</span><span class="dir-chip-fixed ' + ed + '" id="fld-sphereType">' + state.sphereType + '</span></div>' +
        '<div class="kv"><span>ARC STEP</span><b class="mono ' + ed + '" id="fld-sphereStep">' + lenStr(state.sphereStep, state.unit==='mm'?3:4) + ' ' + unitLabel() + '</b></div>';
    }

    if(state.mode === 'JOYSTICK'){
      return '' +
        '<div class="kv"><span>MAX RAPID</span><b class="mono ' + ed + '" id="fld-joyRate">' + lenStr(state.joyMaxRate, 0) + ' ' + unitLabel() + '/min</b></div>' +
        '<div class="kv"><span>DEADZONE</span><b class="mono ' + ed + '" id="fld-joyDead">' + state.joyDeadzone + ' %</b></div>' +
        '<div class="kv"><span>STATUS</span><b class="mono" style="color:var(--blue)">ANALOG READY</b></div>';
    }
  }

  function bindModeEvents(){
    if(state.runState === 'running') return; // Field editing locked during cycle execution

    if(state.mode === 'GEARBOX'){
      var btnSync = $('fld-gearSync');
      if(btnSync){
        btnSync.addEventListener('click', function(){
          state.gearboxMode = state.gearboxMode === 'SYNC' ? 'ASYNC' : 'SYNC';
          syncModeToController('GEARBOX');
          updateStatusMsg(); render();
        });
      }

      var btnAxis = $('fld-gearAxis');
      if(btnAxis){
        btnAxis.addEventListener('click', function(){
          if(state.activeFeedAxis === 'Z'){
            state.activeFeedAxis = 'X';
            state.gearboxMode = 'SYNC';
          } else if(state.activeFeedAxis === 'X'){
            state.activeFeedAxis = 'Z+X';
            state.gearboxMode = 'SYNC';
          } else {
            state.activeFeedAxis = 'Z';
          }
          syncModeToController('GEARBOX');
          syncPitchToController();
          updateStatusMsg(); render();
        });
      }

      var btnGearDir = $('fld-gearDir');
      if(btnGearDir){
        btnGearDir.addEventListener('click', function(){
          if(state.runState === 'running') return;
          if(state.activeFeedAxis === 'Z' || state.activeFeedAxis === 'Z+X'){
            state.gearDirZ = (state.gearDirZ === 'FWD' ? 'REV' : 'FWD');
            if(window.nanoelsClient) window.nanoelsClient.reversePitch();
            setStatusMsg('Z FEED DIRECTION: ' + (state.gearDirZ==='FWD'?'← TO CHUCK (Z−)':'→ TO TAIL (Z+)'));
          } else {
            state.gearDirX = (state.gearDirX === 'IN' ? 'OUT' : 'IN');
            if(window.nanoelsClient) window.nanoelsClient.reversePitch();
            setStatusMsg('X FEED DIRECTION: ' + (state.gearDirX==='IN'?'↑ TO CENTER (X−)':'↓ RETRACT (X+)'));
          }
          render();
        });
      }

      var btnFeed = $('fld-unitFeed');
      var btnPitch = $('fld-unitPitch');
      if(btnFeed && btnPitch){
        btnFeed.addEventListener('click', function(){ 
          state.gearFeedModeZ = 'FEED'; 
          updateStatusMsg(); 
          render(); 
          openKeypad('gearPitchZ', toDisplayLen(state.gearPitchZ));
        });
        btnPitch.addEventListener('click', function(){ 
          state.gearFeedModeZ = 'PITCH'; 
          updateStatusMsg(); 
          render(); 
          openGearboxPitchModal();
        });
      }

      var btnGearZ = $('fld-gearZ');
      if(btnGearZ){
        btnGearZ.addEventListener('click', function(){
          if(state.gearboxMode === 'ASYNC'){
            openKeypad('asyncSpeedZ', state.asyncSpeedZ);
          } else if(state.gearFeedModeZ === 'FEED'){
            openKeypad('gearPitchZ', toDisplayLen(state.gearPitchZ));
          } else {
            openGearboxPitchModal();
          }
        });
      }

      var btnGearX = $('fld-gearX');
      if(btnGearX){
        btnGearX.addEventListener('click', function(){
          if(state.gearboxMode === 'ASYNC') openKeypad('asyncSpeedX', state.asyncSpeedX);
          else openKeypad('gearPitchX', state.unit==='mm'?state.gearPitchX:state.gearPitchX/25.4);
        });
      }

      var btnOpenTaper = $('btnOpenTaperModal');
      if(btnOpenTaper){
        btnOpenTaper.addEventListener('click', function(e){
          e.stopPropagation();
          openGearboxTaperModal();
        });
      }

      var boxTaper = $('boxTaperValues');
      if(boxTaper){
        boxTaper.addEventListener('click', function(){
          openGearboxTaperModal();
        });
      }

      var btnEditPitch = $('btnEditPitch');
      if(btnEditPitch){
        btnEditPitch.addEventListener('click', function(){
          openGearboxPitchModal();
        });
      }

      var btnTaperSide = $('fld-gearTaperSide');
      if(btnTaperSide){
        btnTaperSide.addEventListener('click', function(){
          state.gearTaperSide = state.gearTaperSide === 'EXTERNAL' ? 'INTERNAL' : 'EXTERNAL';
          setStatusMsg('TAPER PROFILE: ' + (state.gearTaperSide==='EXTERNAL'?'EXTERNAL (OD TAPER)':'INTERNAL (ID BORE TAPER)'));
          render();
        });
      }

      var btnTaperInvert = $('fld-gearTaperInvert');
      if(btnTaperInvert){
        btnTaperInvert.addEventListener('click', function(){
          state.gearTaperInvert = !state.gearTaperInvert;
          setStatusMsg('TAPER CONE SLOPE: ' + (state.gearTaperInvert ? 'INVERTED (TALLER AT TAILSTOCK)' : 'NORMAL (TALLER AT CHUCK)'));
          render();
        });
      }
      return;
    }

    if(state.mode === 'TURNING'){
      $('fld-turnType').addEventListener('click', function(){
        state.turnType = state.turnType === 'EXTERNAL' ? 'INTERNAL' : 'EXTERNAL';
        updateStatusMsg();
        render();
      });
      $('fld-turnDir').addEventListener('click', function(){
        state.turnDir = state.turnDir === 'CHUCK' ? 'TAIL' : 'CHUCK';
        recalcTurnLimits();
        updateStatusMsg();
        render();
      });
      $('fld-turnLen').addEventListener('click', function(){ openKeypad('turnLength', state.turnLength); });
      $('fld-turnDepth').addEventListener('click', function(){ openKeypad('turnTotalDepth', state.turnTotalDepth); });
      $('fld-turnMaxDoc').addEventListener('click', function(){ openKeypad('turnMaxDoc', state.turnMaxDoc); });
      $('fld-turnPasses').addEventListener('click', function(){ openKeypad('turnPasses', state.turnPasses); });
      $('fld-turnRestore').addEventListener('click', function(){
        state.turnRestoreLimits = !state.turnRestoreLimits;
        updateStatusMsg(); render();
      });
    } else if(state.mode === 'FACING'){
      $('fld-faceDir').addEventListener('click', function(){
        state.faceDir = state.faceDir === 'CENTER' ? 'RETRACT' : 'CENTER';
        recalcFaceLimits();
        updateStatusMsg();
        render();
      });
      $('fld-faceStroke').addEventListener('click', function(){ openKeypad('faceStroke', state.faceStroke); });
      $('fld-faceDepth').addEventListener('click', function(){ openKeypad('faceTotalDepth', state.faceTotalDepth); });
      $('fld-faceMaxDoc').addEventListener('click', function(){ openKeypad('faceMaxDoc', state.faceMaxDoc); });
      $('fld-facePasses').addEventListener('click', function(){ openKeypad('facePasses', state.facePasses); });
      $('fld-faceRestore').addEventListener('click', function(){
        state.faceRestoreLimits = !state.faceRestoreLimits;
        updateStatusMsg(); render();
      });
    } else if(state.mode === 'THREADING'){
      var btnNew = $('btnThreadNew');
      var btnEdit = $('btnThreadEdit');
      if(btnNew){
        btnNew.addEventListener('click', function(e){
          e.stopPropagation();
          if(state.runState === 'running') return;
          openThreadWizard('NEW');
        });
      }
      if(btnEdit){
        btnEdit.addEventListener('click', function(e){
          e.stopPropagation();
          if(state.runState === 'running') return;
          openThreadWizard('EDIT');
        });
      }
    } else if(state.mode === 'TAPER'){
      $('fld-taperAngle').addEventListener('click', function(){ openKeypad('taperAngle', state.taperAngle); });
      $('fld-taperLen').addEventListener('click', function(){ openKeypad('taperLen', state.taperLen); });
      $('fld-taperDir').addEventListener('click', function(){ state.taperDir = state.taperDir==='INT'?'EXT':'INT'; render(); });
    } else if(state.mode === 'CUTOFF'){
      $('fld-cutFeed').addEventListener('click', function(){ openKeypad('cutoffFeed', state.cutoffFeed); });
      $('fld-cutPeck').addEventListener('click', function(){ openKeypad('cutoffPeck', state.cutoffPeck); });
      $('fld-cutRetract').addEventListener('click', function(){ openKeypad('cutoffRetract', state.cutoffRetract); });
    } else if(state.mode === 'SLOTTING'){
      $('fld-slotStrokes').addEventListener('click', function(){ openKeypad('slotStrokes', state.slotStrokes); });
      $('fld-slotFeed').addEventListener('click', function(){ openKeypad('slotFeedPerStroke', state.slotFeedPerStroke); });
      $('fld-slotLen').addEventListener('click', function(){ openKeypad('slotLen', state.slotLen); });
    } else if(state.mode === 'SPHERE'){
      $('fld-sphereRad').addEventListener('click', function(){ openKeypad('sphereRadius', state.sphereRadius); });
      $('fld-sphereType').addEventListener('click', function(){ state.sphereType = state.sphereType==='CONVEX'?'CONCAVE':'CONVEX'; render(); });
      $('fld-sphereStep').addEventListener('click', function(){ openKeypad('sphereStep', state.sphereStep); });
    } else if(state.mode === 'JOYSTICK'){
      $('fld-joyRate').addEventListener('click', function(){ openKeypad('joyMaxRate', state.joyMaxRate); });
      $('fld-joyDead').addEventListener('click', function(){ openKeypad('joyDeadzone', state.joyDeadzone); });
    }
  }

  function updateStatusMsg(){
    if(state.mode === 'GEARBOX'){
      if(state.gearboxMode === 'ASYNC'){
        setStatusMsg('ASYNC ACTIVE: ' + state.activeFeedAxis + '-AXIS MOTORIZED AT ' + (state.activeFeedAxis==='Z'?state.asyncSpeedZ:state.asyncSpeedX) + ' ' + unitLabel() + '/MIN');
      } else {
        var zDisp = getGearZDisplay();
        var xDisp = getGearXDisplay();
        if(state.activeFeedAxis === 'Z+X'){
          var taperPresetName = (state.gearTaperPresetIdx >= 0 && state.gearTaperPresetIdx < TAPER_PRESETS.length) 
            ? TAPER_PRESETS[state.gearTaperPresetIdx].name 
            : 'CUSTOM TAPER';
          setStatusMsg('SYNC ARMED: 2-AXIS TAPER (Z: ' + zDisp.val + ' ' + zDisp.unit + ' | SLAVE X: ' + taperPresetName + ' ' + fmt(state.gearTaperAngle,2) + '°)');
        } else {
          var curDisp = state.activeFeedAxis === 'Z' ? zDisp : xDisp;
          setStatusMsg('SYNC ARMED: ' + state.activeFeedAxis + '-AXIS GEARED AT ' + curDisp.val + ' ' + curDisp.unit);
        }
      }
    } else if(state.mode === 'TURNING'){
      var p = getTurnPasses();
      var dirStr = state.turnDir === 'CHUCK' ? '← TO CHUCK' : '→ TO TAIL';
      var typeStr = state.turnType === 'EXTERNAL' ? 'EXT (OD)' : 'INT (BORING)';
      setStatusMsg('TURNING [' + typeStr + ']: ' + fmt(state.turnLength,1) + 'mm ' + dirStr + ' | ' + p.count + ' PASSES (MAX DOC: Ø' + fmt(state.turnMaxDoc,3) + ') | RESTORE LIMITS: ' + (state.turnRestoreLimits?'ON':'OFF'));
    } else if(state.mode === 'FACING'){
      var fp = getFacePasses();
      var fdirStr = state.faceDir === 'CENTER' ? '↑ TO CENTER' : '↓ RETRACT';
      setStatusMsg('FACING: Ø' + fmt(state.faceStroke,1) + 'mm ' + fdirStr + ' | ' + fp.count + ' PASSES (MAX DOC: ' + fmt(state.faceMaxDoc,3) + ') | RESTORE LIMITS: ' + (state.faceRestoreLimits?'ON':'OFF'));
    } else if(state.mode === 'THREADING'){
      setStatusMsg('AUTO THREADING: PITCH ' + fmt(state.pitch,3) + 'mm ' + state.threadDir + ' | 90° RADIAL PLUNGE INFEED');
    } else {
      setStatusMsg('CYCLE MODE: ' + state.mode + ' READY');
    }
  }

  // Universal Runstack: Binary START / STOP (matches NanoELS hardware state machine)
  var runstackBound = false;
  function renderRunstack(){
    var hasAlpine = window.Alpine && Alpine.store('machine');
    var isRun = state.runState === 'running';

    if(!hasAlpine){
      var runText = isRun ? (state.mode === 'GEARBOX' ? 'FEEDING' : 'RUNNING') : (state.mode === 'GEARBOX' ? 'START FEED' : 'START CYCLE');
      var runClass = isRun ? 'running' : 'start';
      var stopClass = isRun ? 'active' : 'idle';

      $('runstackArea').innerHTML = '' +
        '<div class="rbtn ' + runClass + ' tap" id="btnRun">' + (isRun ? '<span style="display:inline-block; width:8px; height:8px; border-radius:50%; background:var(--green); margin-right:8px; box-shadow:0 0 6px var(--green);"></span>' : '') + runText + '</div>' +
        '<div class="rbtn stop ' + stopClass + ' ' + (isRun ? 'tap' : '') + '" id="btnStop">STOP</div>';
    }

    if(!runstackBound){
      var bRun = $('btnRun');
      var bStop = $('btnStop');
      if(bRun && bStop){
        if(hasAlpine) runstackBound = true;

        bRun.addEventListener('click', function(){
          if(window.nanoelsClient){
            window.nanoelsClient.startCycle();
          }
          if(window.Alpine && Alpine.store('machine')){
            Alpine.store('machine').isOn = true;
          }
          if(state.runState === 'stopped'){
            // Cache limits if opt-in restoration is active
            if(state.mode === 'TURNING' && state.turnRestoreLimits && state.turnSavedZMin === null && state.turnSavedZMax === null){
              state.turnSavedZMin = state.zMin;
              state.turnSavedZMax = state.zMax;
            } else if(state.mode === 'FACING' && state.faceRestoreLimits && state.faceSavedXMin === null && state.faceSavedXMax === null){
              state.faceSavedXMin = state.xMinDia;
              state.faceSavedXMax = state.xMaxDia;
            } else if(state.mode === 'THREADING'){
              if(state.threadSkimVerify){
                state.threadSkimStage = 'SKIMMING';
                state.threadCurrentPass = 0;
                state.pass = 'SKIM';
                setStatusMsg('THREADING STAGE 1/2: EXECUTING SKIM PASS AT PRE-TURN OD');
              } else {
                state.threadSkimStage = 'THREADING';
                state.threadCurrentPass = 1;
                state.pass = '01 / ' + (state.threadPassCount < 10 ? '0' : '') + state.threadPassCount;
                setStatusMsg('THREADING: PASS 01 OF ' + state.threadPassCount);
              }
            }
            state.runState = 'running';
            if(state.mode !== 'THREADING') setStatusMsg('CYCLE STARTED: ' + state.mode + ' ACTIVE');
          } else {
            stopCycle(true);
            return;
          }
          syncSpindleSimulation();
          render();
        });

        bStop.addEventListener('click', function(){
          if(window.nanoelsClient){
            window.nanoelsClient.stopCycle();
          }
          if(window.Alpine && Alpine.store('machine')){
            Alpine.store('machine').isOn = false;
          }
          if(state.runState !== 'stopped'){
            stopCycle(true);
          }
        });
      }
    }
  }

  function stopCycle(manualStop){
    state.runState = 'stopped';
    if(window.Alpine && Alpine.store('machine')){
      Alpine.store('machine').isOn = false;
    }
    
    // Auto-restore limit if opt-in was active
    if(state.mode === 'TURNING' && state.turnRestoreLimits && (state.turnSavedZMin !== null || state.turnSavedZMax !== null)){
      state.zMin = state.turnSavedZMin;
      state.zMax = state.turnSavedZMax;
      state.turnSavedZMin = null;
      state.turnSavedZMax = null;
      setStatusMsg('TURNING ENDED: RESTORED Z LIMITS');
    } else if(state.mode === 'FACING' && state.faceRestoreLimits && (state.faceSavedXMin !== null || state.faceSavedXMax !== null)){
      state.xMinDia = state.faceSavedXMin;
      state.xMaxDia = state.faceSavedXMax;
      state.faceSavedXMin = null;
      state.faceSavedXMax = null;
      setStatusMsg('FACING ENDED: RESTORED X LIMITS');
    }

    state.turnCurrentPass = 1;
    state.faceCurrentPass = 1;
    state.threadSkimStage = 'IDLE';
    state.threadCurrentPass = 1;
    syncSpindleSimulation();
    render();
  }

  // Spindle Encoder Simulation & Multi-Pass Cycle Stepper
  var simTimer = null;
  function syncSpindleSimulation(){
    if(window.nanoelsClient){
      if(simTimer){ clearInterval(simTimer); simTimer = null; }
      return;
    }
    if(state.spindleSimRunning && !simTimer){
      state.rpm = 640;
      simTimer = setInterval(function(){
        state.rpm = 620 + Math.round(Math.random()*35);
        state.angle = (state.angle + state.rpm*6/60) % 360;
        state.turns += (state.rpm/60/10);

        if(isFeeding()){
          var revDelta = (state.rpm / 60) * 0.1;
          
          if(state.mode === 'GEARBOX' && state.gearboxMode === 'SYNC'){
            if(state.activeFeedAxis === 'Z'){
              var dz = (state.gearDirZ==='FWD' ? -1 : 1) * state.gearPitchZ * revDelta;
              state.z = clamp(state.z + dz, state.zMin, state.zMax);
              if(state.z === state.zMin || state.z === state.zMax) state.runState = 'stopped';
            } else if(state.activeFeedAxis === 'X'){
              var dx = (state.gearDirX==='IN' ? -1 : 1) * state.gearPitchX * revDelta;
              state.xDia = clamp(state.xDia + dx, state.xMinDia, state.xMaxDia);
              if(state.xDia === state.xMinDia || state.xDia === state.xMaxDia) state.runState = 'stopped';
            } else if(state.activeFeedAxis === 'Z+X'){
              var dz = (state.gearDirZ==='FWD' ? -1 : 1) * state.gearPitchZ * revDelta;
              state.z = clamp(state.z + dz, state.zMin, state.zMax);
              var coneRatio = state.gearTaperRatio || 0.0625;
              var taperSideSign = (state.gearTaperSide === 'INTERNAL' ? 1 : -1);
              if(state.gearTaperInvert) taperSideSign = -taperSideSign;
              var dx = Math.abs(dz) * coneRatio * taperSideSign;
              state.xDia = clamp(state.xDia + dx, state.xMinDia, state.xMaxDia);
              if(state.z === state.zMin || state.z === state.zMax || (state.xMinDia !== null && state.xDia === state.xMinDia) || (state.xMaxDia !== null && state.xDia === state.xMaxDia)){
                state.runState = 'stopped';
              }
            }
          } else if(state.mode === 'TURNING'){
            var zDirSign = (state.turnDir === 'CHUCK' ? -1 : 1);
            var stepZ = zDirSign * state.turnPitch * revDelta;
            state.z = clamp(state.z + stepZ, state.zMin, state.zMax);
            var hitShoulder = state.turnDir === 'CHUCK' ? (state.z === state.zMin) : (state.z === state.zMax);
            if(hitShoulder){
              var pInfo = getTurnPasses();
              if(state.turnCurrentPass < pInfo.count){
                state.turnCurrentPass++;
                state.z = state.turnDir === 'CHUCK' ? (state.zMax !== null ? state.zMax : (state.zMin + state.turnLength))
                                                    : (state.zMin !== null ? state.zMin : (state.zMax - state.turnLength));
                if(state.turnType === 'INTERNAL'){
                  state.xDia += pInfo.doc;
                } else {
                  state.xDia -= pInfo.doc;
                }
              } else {
                stopCycle(false);
              }
            }
          } else if(state.mode === 'FACING'){
            var xDirSign = (state.faceDir === 'CENTER' ? -1 : 1);
            var stepX = xDirSign * state.facePitch * revDelta;
            state.xDia = clamp(state.xDia + stepX, state.xMinDia, state.xMaxDia);
            var hitFaceStop = state.faceDir === 'CENTER' ? (state.xDia === state.xMinDia) : (state.xDia === state.xMaxDia);
            if(hitFaceStop){
              var fpInfo = getFacePasses();
              if(state.faceCurrentPass < fpInfo.count){
                state.faceCurrentPass++;
                state.xDia = state.faceDir === 'CENTER' ? (state.xMaxDia !== null ? state.xMaxDia : (state.xMinDia + state.faceStroke))
                                                        : (state.xMinDia !== null ? state.xMinDia : (state.xMaxDia - state.faceStroke));
                state.z -= fpInfo.doc;
              } else {
                stopCycle(false);
              }
            }
          } else if(state.mode === 'THREADING'){
            var zDirSign = (state.threadCutDir === 'CHUCK' ? -1 : 1);
            var lead = state.pitch * (state.threadStarts || 1);
            var stepZ = zDirSign * lead * revDelta;
            state.z = clamp(state.z + stepZ, state.zMin, state.zMax);

            // Coordinated 2-axis taper if coneRatio != 0
            if(state.threadConeRatio && state.threadConeRatio !== 0){
              var dx = (state.threadCutDir === 'CHUCK' ? -1 : 1) * (state.threadConeRatio * Math.abs(stepZ));
              state.xDia = clamp(state.xDia + dx, state.xMinDia, state.xMaxDia);
            }

            var hitEnd = state.threadCutDir === 'CHUCK' ? (state.z === state.zMin) : (state.z === state.zMax);
            if(hitEnd){
              if(state.threadSkimStage === 'SKIMMING'){
                state.runState = 'stopped';
                state.threadSkimStage = 'VERIFY_PROMPT';
                state.z = state.threadCutDir === 'CHUCK' ? (state.zMax !== null ? state.zMax : -4.000)
                                                        : (state.zMin !== null ? state.zMin : -42.000);
                var stockLabel = state.threadStandardName ? 
                  (STD_THREADS[state.threadStandard] && STD_THREADS[state.threadStandard][state.threadSizeIdx] ? 
                    'Ø ' + STD_THREADS[state.threadStandard][state.threadSizeIdx].stockMin + ' – ' + STD_THREADS[state.threadStandard][state.threadSizeIdx].stockMax + ' mm' :
                    'Ø ' + lenStr(state.xDia) + ' ' + unitLabel()) :
                  ('Ø ' + lenStr(state.xDia) + ' ' + unitLabel());
                $('skimTargetDia').textContent = stockLabel;
                $('skimVerifyOverlay').classList.add('show');
                setStatusMsg('SKIM PASS COMPLETE: AWAITING OPERATOR VERIFICATION');
              } else if(state.threadSkimStage === 'THREADING'){
                if(state.threadCurrentPass < state.threadPassCount){
                  state.threadCurrentPass++;
                  state.pass = (state.threadCurrentPass < 10 ? '0' : '') + state.threadCurrentPass + ' / ' + (state.threadPassCount < 10 ? '0' : '') + state.threadPassCount;
                  state.z = state.threadCutDir === 'CHUCK' ? (state.zMax !== null ? state.zMax : -4.000)
                                                          : (state.zMin !== null ? state.zMin : -42.000);
                  var N = state.threadPassCount;
                  var fracPrev = (N - (state.threadCurrentPass - 1)) / N;
                  var fracCur = (N - state.threadCurrentPass) / N;
                  var deltaInfeed = state.threadTotalDepth * ((fracPrev * fracPrev) - (fracCur * fracCur));
                  if(state.threadSide === 'INTERNAL') state.xDia += (deltaInfeed * 2);
                  else state.xDia -= (deltaInfeed * 2);
                  setStatusMsg('THREADING: PASS ' + state.threadCurrentPass + ' OF ' + state.threadPassCount);
                } else {
                  stopCycle(false);
                  state.threadSkimStage = 'IDLE';
                  setStatusMsg('THREADING COMPLETE: FINISHED ' + state.threadPassCount + ' PASSES');
                }
              }
            }
          }
        }
        render();
      }, 100);
    } else if(!state.spindleSimRunning && simTimer){
      clearInterval(simTimer); simTimer = null;
      state.rpm = 0;
      render();
    }
  }

  function updateJogAntiTrap(){
    var eps = 0.001;
    var atZLeft  = (state.zLeft  !== null && state.zLeft  <= eps);
    var atZRight = (state.zRight !== null && state.zRight <= eps);
    var atXLeft  = (state.xLeft  !== null && state.xLeft  <= eps);
    var atXRight = (state.xRight !== null && state.xRight <= eps);

    var btnLeft = document.querySelector('.dpad .btn.left-a');   // Z- (Chuck)
    var btnRight = document.querySelector('.dpad .btn.right-a'); // Z+ (Tail)
    var btnUp = document.querySelector('.dpad .btn.up');         // X- (To Center)
    var btnDown = document.querySelector('.dpad .btn.down');     // X+ (Retract)

    if(btnLeft) btnLeft.classList.toggle('limit-blocked', atZLeft);
    if(btnRight) btnRight.classList.toggle('limit-blocked', atZRight);
    if(btnUp) btnUp.classList.toggle('limit-blocked', atXLeft);
    if(btnDown) btnDown.classList.toggle('limit-blocked', atXRight);

    // NW: Z- and X- (Into cut / To Center). Blocked if at Z-Left OR X-Left.
    // SE: Z+ and X+ (Retract). Blocked if at Z-Right OR X-Right.
    var btnNw = document.querySelector('.chamfer-btn[data-diag="nw"]');
    var btnSe = document.querySelector('.chamfer-btn[data-diag="se"]');
    if(btnNw) btnNw.classList.toggle('limit-blocked', atZLeft || atXLeft);
    if(btnSe) btnSe.classList.toggle('limit-blocked', atZRight || atXRight);
  }

  var lastRenderedModeKey = '';
  function render(forceModeRebuild){
    var hasAlpine = window.Alpine && Alpine.store('machine');
    if(!hasAlpine){
      $('zValue').textContent = lenStr(state.z);
      renderLimitChip('lim-zMin', 'zLeft');
      renderLimitChip('lim-zMax', 'zRight');

      var isDia = state.xRep === 'diameter';
      var xVal = isDia ? state.xDia : state.xDia / 2;
      var glyphClass = isDia ? 'dia' : 'rad';
      var glyphChar = isDia ? 'Ø' : 'R';
      $('xValue').innerHTML = '<span class="dia-glyph ' + glyphClass + '">' + glyphChar + '</span>' + lenStr(xVal);
      renderLimitChip('lim-xMaxDia', 'xRight');
      renderLimitChip('lim-xMinDia', 'xLeft');
      $('repBadge').textContent = isDia ? 'Ø DIA' : 'R RAD';
      $('repBadge').className = 'chip-sm rep-toggle tap ' + (isDia ? 'is-dia' : 'is-rad');

      $('stepValue').textContent = getActiveJogStepDisplay();

      if(state.spindleCycle === 0){ $('spindleValue').textContent = fmt(state.rpm,0); $('spindleUnit').textContent = 'RPM'; }
      else if(state.spindleCycle === 1){ $('spindleValue').textContent = (typeof Formatters !== 'undefined' ? Formatters.formatTurns(state.turns) : Math.floor(state.turns).toString()); $('spindleUnit').textContent = 'TURNS'; }
      else if(state.spindleCycle === 2){ $('spindleValue').textContent = (typeof Formatters !== 'undefined' ? Formatters.formatAngle(state.angle) : fmt(state.angle,1)); $('spindleUnit').textContent = '°'; }

      $('modeName').textContent = state.mode + (state.runState === 'running' ? ' 🔒' : '');
    }

    $('modeHead').classList.toggle('locked', state.runState === 'running');

    // Only rebuild modeBody DOM when mode or state parameters change, avoiding 10Hz DOM thrash
    var currentModeKey = state.mode + '|' + state.runState + '|' + state.activeFeedAxis + '|' + state.gearFeedModeZ + '|' + state.gearboxMode + '|' + (state.gearTaperPresetIdx) + '|' + (state.gearTaperAuthVal) + '|' + (state.turnCurrentPass) + '|' + (state.faceCurrentPass) + '|' + (state.pitch) + '|' + (state.threadTpiVal);
    if(forceModeRebuild || currentModeKey !== lastRenderedModeKey){
      lastRenderedModeKey = currentModeKey;
      $('modeBody').innerHTML = modeBodyHTML();
      bindModeEvents();
    }

    renderRunstack();
    renderDynamicDeck();
    updateJogAntiTrap();

    if(!hasAlpine){
      var sp = $('statusPill');
      if(isFeeding()){
        sp.className = 'pill running'; sp.innerHTML = '<span class="dot"></span>FEEDING';
      } else if(state.runState === 'running' && !state.spindleSimRunning && (state.mode!=='GEARBOX'||state.gearboxMode==='SYNC')){
        sp.className = 'pill paused'; sp.innerHTML = '<span class="dot"></span>ARMED (WAIT)';
      } else {
        sp.className = 'pill stopped'; sp.innerHTML = '<span class="dot"></span>STOPPED';
      }

      $('unitToggle').textContent = state.unit;
      $('device').classList.toggle('is-running', state.runState === 'running');
    }
  }

  // Header Handlers
  $('unitToggle').addEventListener('click', function(){ 
    if(state.runState === 'running') return;
    state.unit = state.unit === 'mm' ? 'in' : 'mm';
    if(window.Alpine && Alpine.store('machine')){
      Alpine.store('machine').measure = state.unit === 'in' ? 1 : 0;
      var curStep = state.unit === 'mm' ? JOG_STEPS_MM[state.stepIdx % JOG_STEPS_MM.length] : JOG_STEPS_IN[state.stepIdx % JOG_STEPS_IN.length];
      var stepDu = Math.round(state.unit === 'in' ? curStep * 254000 : curStep * 10000);
      Alpine.store('machine').jogStepDu = stepDu;
    }
    render(); 
  });
  $('settingsBtn').addEventListener('click', function(){ 
    if(state.runState === 'running') return;
    $('settingsOverlay').classList.add('show'); 
  });
  $('settingsClose').addEventListener('click', function(){ $('settingsOverlay').classList.remove('show'); });
  $('openDocBtn').addEventListener('click', function(){
    if(state.runState === 'running') return;
    $('settingsOverlay').classList.remove('show');
    showDocTab('decisions');
    $('docOverlay').classList.add('show');
  });

  // Message Log Overlay Handlers
  $('btnOpenLog').addEventListener('click', function(){
    if(state.runState === 'running') return;
    var list = $('logHistoryList');
    list.innerHTML = '';
    if(!state.msgLog || state.msgLog.length === 0){
      list.innerHTML = '<div style="color:var(--dimmer); padding:4px;">No messages logged yet.</div>';
    } else {
      for(var i = state.msgLog.length - 1; i >= 0; i--){
        var row = document.createElement('div');
        row.style.borderBottom = '1px solid #1a2228';
        row.style.padding = '3px 0';
        row.textContent = '• ' + state.msgLog[i];
        list.appendChild(row);
      }
    }
    $('logOverlay').classList.add('show');
  });
  $('logCloseBtn').addEventListener('click', function(){ $('logOverlay').classList.remove('show'); });

  // Mode Selector with Running Lockout (Silent disable, zero banners/popups to obstruct operator)
  $('modeHead').addEventListener('click', function(){
    if(state.runState === 'running') return;
    $('modeOverlay').classList.add('show');
  });
  function syncModeToController(targetMode){
    if(!window.nanoelsClient) return;
    var modeMap = {
      'GEARBOX': 'GEARS',
      'JOYSTICK': 'JOYSTICK',
      'TURNING': 'TURN',
      'FACING': 'FACE',
      'THREADING': 'THREAD',
      'TAPER': 'CONE',
      'CUTOFF': 'CUT',
      'SLOTTING': 'SLOT',
      'SPHERE': 'ELLIPSE'
    };
    var m = targetMode || state.mode;
    var actionKey = modeMap[m];
    if(m === 'GEARBOX'){
      if(state.activeFeedAxis === 'Z'){
        actionKey = (state.gearboxMode === 'ASYNC') ? 'ASYNC' : 'GEARS';
      } else if(state.activeFeedAxis === 'X'){
        actionKey = 'XGEAR';
      } else if(state.activeFeedAxis === 'Z+X'){
        actionKey = 'CONE';
      } else {
        actionKey = 'GEARS';
      }
    }
    if(actionKey){
      window.nanoelsClient.setMode(actionKey);
    }
  }

  function syncPitchToController(targetPitch){
    if(!window.nanoelsClient) return;

    // 1. Threading Mode
    if(state.mode === 'THREADING'){
      if(state.threadUnit === 'tpi'){
        var tpi = state.threadTpiVal || (state.pitch > 0 ? +(25.4 / state.pitch).toFixed(2) : 13.0);
        window.nanoelsClient.setFeedTpi(tpi);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').thFmt = 1;
          Alpine.store('machine').thPitch = tpi;
        }
      } else {
        var pMm = targetPitch || state.pitch || 1.5;
        window.nanoelsClient.setFeedMm(pMm);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').thFmt = 0;
          Alpine.store('machine').thPitch = pMm;
        }
      }
      return;
    }

    // 2. Taper Mode (Dedicated Mode or Gearbox 2-Axis Taper)
    if(state.mode === 'TAPER' || (state.mode === 'GEARBOX' && state.activeFeedAxis === 'Z+X')){
      if(state.gearTaperPresetIdx >= 0 && TAPER_PRESETS[state.gearTaperPresetIdx]){
        var p = TAPER_PRESETS[state.gearTaperPresetIdx];
        window.nanoelsClient.setTaperPreset(state.gearTaperPresetIdx, p.angle, 1.0 / p.ratio);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').tpPreset = state.gearTaperPresetIdx;
          Alpine.store('machine').tpMode = 2;
          Alpine.store('machine').tpVal = 1.0 / p.ratio;
          Alpine.store('machine').coneRatio = p.ratio;
        }
      } else if(state.gearTaperAuthMode === 'ANGLE'){
        window.nanoelsClient.setTaperAngle(state.gearTaperAuthVal || state.gearTaperAngle || 1.432);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').tpPreset = -1;
          Alpine.store('machine').tpMode = 1;
          Alpine.store('machine').tpVal = state.gearTaperAuthVal || state.gearTaperAngle;
          Alpine.store('machine').coneRatio = state.gearTaperRatio;
        }
      } else {
        var rVal = state.gearTaperAuthVal || (state.gearTaperRatio > 0 ? (1.0 / state.gearTaperRatio) : 20.0);
        window.nanoelsClient.setTaperRatio(rVal);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').tpPreset = -1;
          Alpine.store('machine').tpMode = 0;
          Alpine.store('machine').tpVal = rVal;
          Alpine.store('machine').coneRatio = state.gearTaperRatio;
        }
      }
      var taperPitch = targetPitch || state.gearPitchZ || 0.15;
      window.nanoelsClient.setFeedMm(taperPitch);
      return;
    }

    // 3. Gearbox / Turning / Facing Feed
    if(state.mode === 'GEARBOX'){
      if(state.gearboxMode === 'ASYNC') return;
      if(state.gearThreadUnit === 'tpi'){
        var tpiVal = state.gearPitchZ_tpi || +(25.4 / (targetPitch || state.gearPitchZ || 2.0)).toFixed(2);
        window.nanoelsClient.setFeedTpi(tpiVal);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').thFmt = 1;
          Alpine.store('machine').thPitch = tpiVal;
        }
        return;
      }
      var p = (targetPitch !== undefined && targetPitch !== null) ? targetPitch : ((state.activeFeedAxis === 'X') ? state.gearPitchX : state.gearPitchZ);
      if(p !== undefined && p !== null && !isNaN(p) && p > 0){
        window.nanoelsClient.setFeedMm(p);
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').thFmt = 0;
          Alpine.store('machine').thPitch = p;
        }
      }
      return;
    }

    var p = targetPitch;
    if(p === undefined || p === null){
      if(state.mode === 'TURNING'){
        p = state.turnPitch;
      } else if(state.mode === 'FACING'){
        p = state.facePitch;
      }
    }
    if(p !== undefined && p !== null && !isNaN(p) && p > 0){
      window.nanoelsClient.setFeedMm(p);
      if(window.Alpine && Alpine.store('machine')){
        Alpine.store('machine').thFmt = 0;
        Alpine.store('machine').thPitch = p;
      }
    }
  }

  $('modeOverlay').querySelectorAll('button[data-mode]').forEach(function(b){
    b.addEventListener('click', function(){
      state.mode = b.dataset.mode;
      state.runState = 'stopped';
      $('modeOverlay').classList.remove('show');
      syncModeToController(state.mode);
      syncPitchToController();
      updateStatusMsg();
      render();
    });
  });
  $('modeCancel').addEventListener('click', function(){ $('modeOverlay').classList.remove('show'); });

  $('repBadge').addEventListener('click', function(e){
    e.stopPropagation();
    state.xRep = state.xRep === 'diameter' ? 'radius' : 'diameter';
    if(window.Alpine && Alpine.store('machine')){
      Alpine.store('machine').dia = state.xRep === 'diameter';
    }
    render();
  });

  $('spindleBox').addEventListener('click', function(){
    state.spindleCycle = (state.spindleCycle + 1) % 3;
    if(window.Alpine && Alpine.store('machine')){
      Alpine.store('machine').spindleMode = state.spindleCycle;
    }
    render(true);
  });

  // Step selector: Tap to cycle, Long-press for Numpad
  var stepHoldTimer = null, stepDidHold = false;
  $('stepMain').addEventListener('pointerdown', function(){
    if(state.runState === 'running') return;
    stepDidHold = false;
    stepHoldTimer = setTimeout(function(){
      stepDidHold = true;
      var curVal = state.unit === 'mm' ? JOG_STEPS_MM[state.stepIdx % JOG_STEPS_MM.length] : JOG_STEPS_IN[state.stepIdx % JOG_STEPS_IN.length];
      openKeypad('step', curVal);
    }, 600);
  });
  $('stepMain').addEventListener('pointerup', function(){
    clearTimeout(stepHoldTimer);
    if(state.runState === 'running') return;
    if(!stepDidHold){
      if(window.nanoelsClient){
        window.nanoelsClient.cycleStep();
      }
      var maxLen = state.unit === 'mm' ? JOG_STEPS_MM.length : JOG_STEPS_IN.length;
      state.stepIdx = (state.stepIdx + 1) % maxLen;
      var curVal = state.unit === 'mm' ? JOG_STEPS_MM[state.stepIdx] : JOG_STEPS_IN[state.stepIdx];
      var stepDu = Math.round((state.unit === 'in' ? curVal * 254000 : curVal * 10000));
      if(window.Alpine && Alpine.store('machine')){
        Alpine.store('machine').jogStepDu = stepDu;
      }
      render();
    }
  });
  $('stepMain').addEventListener('pointercancel', function(){ clearTimeout(stepHoldTimer); });

  // Axis Coordinate Set Overlay
  var axisTarget = null;
  function openAxisOverlay(axis){
    if(state.runState === 'running') return;
    axisTarget = axis;
    if(axis==='z'){
      $('axisOverlayTitle').textContent = 'Z AXIS COORDINATE';
      $('axisOverlayValue').textContent = lenStr(state.z) + ' ' + unitLabel();
    } else {
      $('axisOverlayTitle').textContent = 'X AXIS COORDINATE';
      var xVal = state.xRep==='diameter' ? state.xDia : state.xDia/2;
      var prefix = state.xRep==='diameter' ? 'Ø' : 'R';
      $('axisOverlayValue').textContent = prefix + lenStr(xVal) + ' ' + unitLabel();
    }
    $('axisOverlay').classList.add('show');
  }
  $('zValue').addEventListener('click', function(){ openAxisOverlay('z'); });
  $('xValue').addEventListener('click', function(){ openAxisOverlay('x'); });
  $('axisCancelBtn').addEventListener('click', function(){ $('axisOverlay').classList.remove('show'); });
  $('axisZeroBtn').addEventListener('click', function(){
    if(window.nanoelsClient){
      window.nanoelsClient.zeroAxis(axisTarget);
    }
    if(axisTarget==='z') state.z = 0; else state.xDia = 0;
    $('axisOverlay').classList.remove('show'); render();
  });
  $('axisSetBtn').addEventListener('click', function(){
    $('axisOverlay').classList.remove('show');
    var curr = axisTarget==='z' ? toDisplayLen(state.z) : toDisplayLen(state.xRep==='diameter' ? state.xDia : state.xDia/2);
    openKeypad(axisTarget === 'z' ? 'coordZ' : 'coordX', curr);
  });

  // Soft Limit Overlay
  var limitLabels = { zLeft:'Z CHUCK LIMIT', zRight:'Z TAIL LIMIT', xLeft:'X CENTER LIMIT', xRight:'X RETRACT LIMIT' };
  var limitTarget = null;
  function openLimitOverlay(bound){
    if(state.runState === 'running') return;
    limitTarget = bound;
    $('limitOverlayTitle').textContent = limitLabels[bound] || 'ENVELOPE LIMIT';
    var curVal = state[bound];
    var icon = getLimitIconSVG(bound);
    if(curVal !== null){
      var prefix = (bound === 'xLeft' || bound === 'xRight') && state.xRep === 'diameter' ? 'Ø' : '';
      $('limitOverlayValue').innerHTML = icon + ' ' + prefix + lenStr(Math.abs(curVal)) + ' ' + unitLabel();
    } else {
      $('limitOverlayValue').innerHTML = '—';
    }
    $('limitOverlay').classList.add('show');
  }
  document.querySelectorAll('.chip-sm[data-bound]').forEach(function(el){
    el.addEventListener('click', function(){ openLimitOverlay(el.dataset.bound); });
  });
  $('limitCancelBtn').addEventListener('click', function(){ $('limitOverlay').classList.remove('show'); });
  $('limitSetBtn').addEventListener('click', function(){
    if(window.nanoelsClient){
      var isZ = (limitTarget === 'zLeft' || limitTarget === 'zRight');
      var isMin = (limitTarget === 'zLeft' || limitTarget === 'xLeft');
      window.nanoelsClient.toggleStop(isZ ? 'z' : 'x', isMin ? 'min' : 'max');
    }
    state[limitTarget] = 0.000;
    $('limitOverlay').classList.remove('show'); render();
  });
  $('limitManualBtn').addEventListener('click', function(){
    $('limitOverlay').classList.remove('show');
    var curr = state[limitTarget] !== null ? toDisplayLen(state[limitTarget]) : 0;
    openKeypad('limitTarget', curr);
  });
  $('limitClearBtn').addEventListener('click', function(){ 
    if(window.nanoelsClient){
      var isZ = (limitTarget === 'zLeft' || limitTarget === 'zRight');
      var isMin = (limitTarget === 'zLeft' || limitTarget === 'xLeft');
      window.nanoelsClient.toggleStop(isZ ? 'z' : 'x', isMin ? 'min' : 'max');
    }
    state[limitTarget] = null; $('limitOverlay').classList.remove('show'); render(); 
  });

  // Asynchronous Jogging (UP = into cut / to center, DOWN = retract)
  document.querySelectorAll('.dpad .btn[data-axis]').forEach(function(btn){
    var holdTimer=null, repeatTimer=null;
    var axis = btn.dataset.axis, dir = parseInt(btn.dataset.dir,10);
    var isPressed = false;
    function isBlocked(){
      var eps = 0.001;
      if(axis === 'z'){
        if(dir === -1 && state.zLeft !== null && state.zLeft <= eps) return 'Z CHUCK LIMIT';
        if(dir === 1 && state.zRight !== null && state.zRight <= eps) return 'Z TAIL LIMIT';
      } else {
        if(dir === 1 && state.xLeft !== null && state.xLeft <= eps) return 'X CENTER LIMIT';
        if(dir === -1 && state.xRight !== null && state.xRight <= eps) return 'X RETRACT LIMIT';
      }
      return null;
    }
    function doStep(){
      var blk = isBlocked();
      if(blk){
        setStatusMsg('LIMIT BLOCKED: RESTING ON ' + blk + ' — JOG OPPOSITE TO RETREAT');
        return;
      }
      var stepMm = getActiveJogStepMm();
      if(axis==='z'){
        state.z += dir * stepMm;
        if(dir === -1){
          if(state.zLeft !== null) state.zLeft = Math.max(0, +(state.zLeft - stepMm).toFixed(4));
          if(state.zRight !== null) state.zRight = +(state.zRight + stepMm).toFixed(4);
        } else {
          if(state.zRight !== null) state.zRight = Math.max(0, +(state.zRight - stepMm).toFixed(4));
          if(state.zLeft !== null) state.zLeft = +(state.zLeft + stepMm).toFixed(4);
        }
      } else { 
        // dir=1 is UP (To Center, diameter decreases), dir=-1 is DOWN (Retract, diameter increases)
        var xDelta = (dir === 1 ? -1 : 1) * stepMm;
        state.xDia += xDelta;
        if(dir === 1){
          if(state.xLeft !== null) state.xLeft = Math.max(0, +(state.xLeft - stepMm).toFixed(4));
          if(state.xRight !== null) state.xRight = +(state.xRight + stepMm).toFixed(4);
        } else {
          if(state.xRight !== null) state.xRight = Math.max(0, +(state.xRight - stepMm).toFixed(4));
          if(state.xLeft !== null) state.xLeft = +(state.xLeft + stepMm).toFixed(4);
        }
      }
      render();
    }
    function start(e){
      e.preventDefault();
      var blk = isBlocked();
      if(blk){
        setStatusMsg('LIMIT BLOCKED: RESTING ON ' + blk + ' — JOG OPPOSITE TO RETREAT');
        return;
      }
      isPressed = true;
      btn.classList.add('pressed');
      if (window.nanoelsClient) {
        window.nanoelsClient.jog(axis, dir, true);
      }
      doStep();
      holdTimer = setTimeout(function(){ repeatTimer = setInterval(doStep, 110); }, 350);
    }
    function stop(){
      if (!isPressed) return;
      isPressed = false;
      btn.classList.remove('pressed');
      clearTimeout(holdTimer);
      clearInterval(repeatTimer);
      if (window.nanoelsClient) {
        window.nanoelsClient.jog(axis, dir, false);
      }
    }
    btn.addEventListener('pointerdown', start);
    btn.addEventListener('pointerup', stop);
    btn.addEventListener('pointercancel', stop);
  });

  // Chamfer Jogging (NW = Z- X- into cut; SE = Z+ X+ retract)
  document.querySelectorAll('.chamfer-btn[data-diag]').forEach(function(btn){
    var holdTimer=null, repeatTimer=null;
    var diag = btn.dataset.diag;
    var zDir = diag==='nw' ? -1 : 1;
    var xDir = diag==='nw' ? -1 : 1;
    var isPressed = false;
    function isBlocked(){
      var eps = 0.001;
      if(zDir === -1 && state.zLeft !== null && state.zLeft <= eps) return 'Z CHUCK LIMIT';
      if(zDir === 1 && state.zRight !== null && state.zRight <= eps) return 'Z TAIL LIMIT';
      if(xDir === -1 && state.xLeft !== null && state.xLeft <= eps) return 'X CENTER LIMIT';
      if(xDir === 1 && state.xRight !== null && state.xRight <= eps) return 'X RETRACT LIMIT';
      return null;
    }
    function doStep(){
      var blk = isBlocked();
      if(blk){
        setStatusMsg('LIMIT BLOCKED: RESTING ON ' + blk + ' — JOG OPPOSITE TO RETREAT');
        return;
      }
      var stepMm = getActiveJogStepMm();
      state.z += zDir * stepMm;
      if(zDir === -1){
        if(state.zLeft !== null) state.zLeft = Math.max(0, +(state.zLeft - stepMm).toFixed(4));
        if(state.zRight !== null) state.zRight = +(state.zRight + stepMm).toFixed(4);
      } else {
        if(state.zRight !== null) state.zRight = Math.max(0, +(state.zRight - stepMm).toFixed(4));
        if(state.zLeft !== null) state.zLeft = +(state.zLeft + stepMm).toFixed(4);
      }
      state.xDia += xDir * stepMm;
      if(xDir === -1){
        if(state.xLeft !== null) state.xLeft = Math.max(0, +(state.xLeft - stepMm).toFixed(4));
        if(state.xRight !== null) state.xRight = +(state.xRight + stepMm).toFixed(4);
      } else {
        if(state.xRight !== null) state.xRight = Math.max(0, +(state.xRight - stepMm).toFixed(4));
        if(state.xLeft !== null) state.xLeft = +(state.xLeft + stepMm).toFixed(4);
      }
      render();
    }
    function start(e){
      e.preventDefault();
      var blk = isBlocked();
      if(blk){
        setStatusMsg('LIMIT BLOCKED: RESTING ON ' + blk + ' — JOG OPPOSITE TO RETREAT');
        return;
      }
      isPressed = true;
      btn.classList.add('pressed');
      if (window.nanoelsClient) {
        window.nanoelsClient.jog('z', zDir, true);
        window.nanoelsClient.jog('x', xDir, true);
      }
      doStep();
      holdTimer = setTimeout(function(){ repeatTimer = setInterval(doStep, 110); }, 350);
    }
    function stop(){
      if (!isPressed) return;
      isPressed = false;
      btn.classList.remove('pressed');
      clearTimeout(holdTimer);
      clearInterval(repeatTimer);
      if (window.nanoelsClient) {
        window.nanoelsClient.jog('z', zDir, false);
        window.nanoelsClient.jog('x', xDir, false);
      }
    }
    btn.addEventListener('pointerdown', start);
    btn.addEventListener('pointerup', stop);
    btn.addEventListener('pointercancel', stop);
  });

  // Progressive Thread Configuration Wizard Engine
  var wizStage = 1; // 1: Family, 2: Size, 3: Pitch, 4: Strategy
  var wizFamily = 'METRIC'; // 'METRIC', 'UN', 'NPT', 'BSPT', 'FREEFORM'
  var wizRangeIdx = 1; // Default range (M7 - M12)
  var wizShowMore = false; // Toggle 3rd-choice sizes
  var wizSizeItem = null;
  var wizPitchIdx = 0;
  var wizCustomDia = null;
  var wizCustomPitch = null;
  var wizSide = 'EXTERNAL';
  var wizHandedness = 'RIGHT';
  var wizCutDir = 'CHUCK';
  var wizSkimVerify = true;
  var wizThreadLength = 20.0;
  var wizStarts = 1;
  var wizFreeTab = 'metric';
  var wizFreePitchMm = 1.75;
  var wizPasses = null;
  var wizPassesCustom = false;
  var wizEditSession = false;

  function calcAutoPasses(infeedDepth, side){
    var base = Math.round(infeedDepth / 0.10);
    if(side === 'INTERNAL') base += 2;
    return Math.max(6, Math.min(18, base));
  }

  function getWizBlueprint(){
    var name = '';
    var pitchMm = 1.0;
    var stockMin = 0, stockMax = 0, targetRoot = 0, infeedDepth = 0, passes = 11;
    var coneRatio = 0;
    var profile = '60° ISO';
    var l2 = 0;

    if(wizFamily === 'FREEFORM'){
      pitchMm = wizFreePitchMm;
      var pStr = wizFreeTab === 'metric' ? pitchMm.toFixed(2) + ' mm' : +(25.4 / pitchMm).toFixed(1) + ' TPI';
      var leadMm = pitchMm * (wizStarts || 1);
      name = 'CUSTOM ' + pStr + (wizStarts > 1 ? ' (' + wizStarts + ' STARTS, LEAD ' + leadMm.toFixed(2) + ' mm)' : '');
      coneRatio = 0;
      profile = '60° FORM';
      infeedDepth = +(0.6134 * pitchMm).toFixed(3);
      passes = (wizPassesCustom && wizPasses) ? wizPasses : calcAutoPasses(infeedDepth, wizSide);
      var curDia = state.xDia > 0 ? state.xDia : 20.0;
      if(wizSide === 'EXTERNAL'){
        stockMin = +(curDia - 0.15).toFixed(2);
        stockMax = +curDia.toFixed(2);
        targetRoot = +(curDia - 2 * infeedDepth).toFixed(2);
      } else {
        stockMin = +curDia.toFixed(2);
        stockMax = +curDia.toFixed(2);
        targetRoot = +(curDia + 2 * infeedDepth).toFixed(2);
      }
    } else if(wizSizeItem){
      var item = wizSizeItem;
      if(wizFamily === 'METRIC'){
        var p = item.pitches[wizPitchIdx] || item.pitches[0];
        pitchMm = typeof p === 'number' ? p : (wizCustomPitch || 1.75);
        name = item.size + ' × ' + pitchMm.toFixed(2);
        coneRatio = 0;
        profile = '60° ISO';
        if(wizSide === 'EXTERNAL'){
          stockMin = item.stockMin;
          stockMax = item.stockMax;
          infeedDepth = +(0.6134 * pitchMm).toFixed(3);
          targetRoot = +(item.dia - 2 * infeedDepth).toFixed(2);
          passes = (wizPassesCustom && wizPasses) ? wizPasses : calcAutoPasses(infeedDepth, wizSide);
        } else {
          var tapDrill = +(item.dia - pitchMm).toFixed(2);
          stockMin = tapDrill;
          stockMax = tapDrill;
          infeedDepth = +(0.5413 * pitchMm).toFixed(3);
          targetRoot = item.dia;
          passes = (wizPassesCustom && wizPasses) ? wizPasses : calcAutoPasses(infeedDepth, wizSide);
        }
      } else if(wizFamily === 'UN'){
        var pObj = item.pitches[wizPitchIdx] || item.pitches[0];
        var tpi = pObj.tpi;
        pitchMm = +(25.4 / tpi).toFixed(4);
        name = item.size + '-' + tpi + ' ' + (pObj.type || 'UN');
        coneRatio = 0;
        profile = '60° UN';
        if(wizSide === 'EXTERNAL'){
          stockMin = item.stockMin;
          stockMax = item.stockMax;
          infeedDepth = +(0.6134 * pitchMm).toFixed(3);
          targetRoot = +(item.dia - 2 * infeedDepth).toFixed(2);
          passes = (wizPassesCustom && wizPasses) ? wizPasses : calcAutoPasses(infeedDepth, wizSide);
        } else {
          var tapDrill = +(item.dia - pitchMm).toFixed(2);
          stockMin = tapDrill;
          stockMax = tapDrill;
          infeedDepth = +(0.5413 * pitchMm).toFixed(3);
          targetRoot = item.dia;
          passes = (wizPassesCustom && wizPasses) ? wizPasses : calcAutoPasses(infeedDepth, wizSide);
        }
      } else if(wizFamily === 'NPT'){
        var tpi = item.tpi;
        pitchMm = +(25.4 / tpi).toFixed(4);
        name = item.size + '-' + tpi + ' NPT';
        coneRatio = 0.0625;
        profile = 'NPT 1:16';
        infeedDepth = +(0.8000 * pitchMm).toFixed(3);
        passes = (wizPassesCustom && wizPasses) ? wizPasses : (item.passes || calcAutoPasses(infeedDepth, wizSide));
        stockMin = item.smallOd;
        stockMax = item.pipeOd;
        targetRoot = +(item.pipeOd - 2 * infeedDepth).toFixed(2);
        l2 = item.l2;
      } else if(wizFamily === 'BSPT'){
        var tpi = item.tpi;
        pitchMm = +(25.4 / tpi).toFixed(4);
        name = item.size + '-' + tpi + ' BSPT (R/Rc)';
        coneRatio = 0.0625;
        profile = 'BSPT 1:16';
        infeedDepth = +(0.6403 * pitchMm).toFixed(3);
        passes = (wizPassesCustom && wizPasses) ? wizPasses : (item.passes || calcAutoPasses(infeedDepth, wizSide));
        stockMin = item.smallOd;
        stockMax = item.pipeOd;
        targetRoot = +(item.pipeOd - 2 * infeedDepth).toFixed(2);
        l2 = item.l2;
      }
    }

    return {
      family: wizFamily,
      name: name || (wizFamily + ' THREAD'),
      pitchMm: pitchMm,
      infeedDepth: infeedDepth,
      passes: passes,
      stockMin: stockMin,
      stockMax: stockMax,
      targetRoot: targetRoot,
      coneRatio: coneRatio,
      profile: profile,
      side: wizSide,
      handedness: wizHandedness,
      cutDir: wizCutDir,
      skimVerify: wizSkimVerify,
      length: wizThreadLength || 20.0,
      l2: l2,
      starts: (wizFamily === 'FREEFORM' ? (wizStarts || 1) : 1)
    };
  }

  function updateWizBlueprintCard(){
    if(wizStage === 1){
      // Step 1: Nothing selected yet
      $('wbpName').textContent = '—';
      $('wbpStock').textContent = '—';
      $('wbpRoot').textContent = '—';
      $('wbpDepth').textContent = '—';
      $('wbpKinematics').textContent = '—';
      $('wbpKinematics').style.color = 'var(--dimmer)';
      return;
    }

    if(wizStage === 2){
      // Step 2: Family selected, waiting for Diameter/Size
      var famName = THREAD_FAMILIES[wizFamily] ? THREAD_FAMILIES[wizFamily].name : wizFamily;
      $('wbpName').textContent = famName;
      $('wbpStock').textContent = '—';
      $('wbpRoot').textContent = '—';
      $('wbpDepth').textContent = '—';
      $('wbpKinematics').textContent = '—';
      $('wbpKinematics').style.color = 'var(--dimmer)';
      return;
    }

    if(wizStage === 3){
      // Step 3: Diameter selected, waiting for Pitch
      var sLabel = wizSizeItem ? wizSizeItem.size : (wizFamily === 'FREEFORM' ? 'FREEFORM' : 'CUSTOM');
      $('wbpName').textContent = sLabel;
      $('wbpStock').textContent = '—';
      $('wbpRoot').textContent = '—';
      $('wbpDepth').textContent = '—';
      $('wbpKinematics').textContent = '—';
      $('wbpKinematics').style.color = 'var(--dimmer)';
      return;
    }

    // Step 4: Full Blueprint available
    var bp = getWizBlueprint();
    if(!bp) return;

    $('wbpName').textContent = bp.name + ' (' + bp.side + ', ' + (bp.handedness==='RIGHT'?'RH':'LH') + ')';
    var stockTarget = bp.side === 'EXTERNAL' ? (bp.stockMax || bp.dia || 0) : bp.stockMin;
    $('wbpStock').textContent = 'Ø ' + fmt(stockTarget, 2) + ' mm';
    $('wbpRoot').textContent = 'Ø ' + fmt(bp.targetRoot, 2) + ' mm';

    $('wbpDepth').textContent = lenStr(bp.infeedDepth) + ' ' + unitLabel() + ' (' + bp.passes + ' PASSES)';
    
    var feedText = bp.cutDir === 'CHUCK' ? '← TO CHUCK (Z−)' : '→ TO TAIL (Z+)';
    $('wbpKinematics').textContent = feedText;
    $('wbpKinematics').style.color = 'var(--fg)';
  }

  function setWizardStage(stage){
    wizStage = stage;
    $('threadStageFamily').style.display = wizStage === 1 ? 'flex' : 'none';
    $('threadStageSize').style.display = wizStage === 2 ? 'flex' : 'none';
    $('threadStagePitch').style.display = wizStage === 3 ? 'flex' : 'none';
    $('threadStageSetup').style.display = wizStage === 4 ? 'flex' : 'none';

    var backBtn = $('btnThreadNavBack');
    var title = $('wizStageTitle');

    if(wizStage === 1){
      if(backBtn) backBtn.style.display = 'none';
      title.textContent = 'STEP 1: THREAD FAMILY';

      document.querySelectorAll('.wiz-family-btn').forEach(function(btn){
        btn.style.opacity = '1';
        btn.style.pointerEvents = 'auto';
        btn.title = '';
      });
    } else if(wizStage === 2){
      if(backBtn){
        backBtn.style.display = 'inline-flex';
        backBtn.textContent = '← FAMILY';
      }
      var famName = THREAD_FAMILIES[wizFamily] ? THREAD_FAMILIES[wizFamily].name : wizFamily;
      title.textContent = 'STEP 2: ' + famName + ' DIAMETER';
      renderStage2Sizes();
    } else if(wizStage === 3){
      if(backBtn){
        backBtn.style.display = 'inline-flex';
        backBtn.textContent = '← SIZES';
      }
      var sLabel = wizSizeItem ? wizSizeItem.size : 'CUSTOM';
      title.textContent = 'STEP 3: ' + sLabel + ' PITCH';
      renderStage3Pitch();
    } else if(wizStage === 4){
      if(backBtn){
        backBtn.style.display = 'inline-flex';
        backBtn.textContent = wizEditSession ? '← NEW SPEC' : '← PITCH';
      }
      title.textContent = 'STEP 4: MACHINING STRATEGY';
      renderStage4Setup();
    }

    // Bottom action buttons: only show and enable on final step (Step 4) once all required parameters are chosen
    var prepBtn = $('btnWizPrepareStock');
    var applyBtn = $('btnWizApply');
    var isReady = (wizStage === 4);
    if(prepBtn){
      prepBtn.style.setProperty('display', isReady ? 'flex' : 'none', 'important');
      prepBtn.disabled = !isReady;
    }
    if(applyBtn){
      applyBtn.style.setProperty('display', isReady ? 'flex' : 'none', 'important');
      applyBtn.disabled = !isReady;
      applyBtn.textContent = 'SAVE & THREAD';
    }

    updateWizBlueprintCard();
  }

  function renderStage2Sizes(){
    var fam = THREAD_FAMILIES[wizFamily];
    if(!fam) return;

    // 1. Render Range Pills
    var rangeRow = $('wizRangeRow');
    rangeRow.innerHTML = '';
    (fam.ranges || []).forEach(function(r, idx){
      var btn = document.createElement('button');
      btn.className = 'wiz-range-btn' + (idx === wizRangeIdx ? ' active' : '');
      btn.textContent = r.label;
      btn.addEventListener('click', function(){
        wizRangeIdx = idx;
        renderStage2Sizes();
      });
      rangeRow.appendChild(btn);
    });

    // 2. Render Size Grid
    var sizeGrid = $('wizSizeGrid');
    var moreBtn = $('btnSizeMore');
    if(moreBtn && moreBtn.parentElement === sizeGrid){
      sizeGrid.removeChild(moreBtn);
    }
    sizeGrid.innerHTML = '';
    var filtered = fam.sizes.filter(function(s){
      if(s.rangeIdx !== wizRangeIdx) return false;
      if(!wizShowMore && s.choice === 3) return false;
      return true;
    });

    // Sort strictly by ascending diameter/OD
    filtered.sort(function(a, b){
      var da = a.dia !== undefined ? a.dia : (a.pipeOd !== undefined ? a.pipeOd : 0);
      var db = b.dia !== undefined ? b.dia : (b.pipeOd !== undefined ? b.pipeOd : 0);
      return da - db;
    });

    // If current selected item is not in this range, pick first in range
    if(filtered.length > 0 && (!wizSizeItem || wizSizeItem.rangeIdx !== wizRangeIdx)){
      wizSizeItem = filtered[0];
      wizPitchIdx = 0;
    }

    filtered.forEach(function(item){
      var btn = document.createElement('button');
      btn.textContent = item.size;
      if(wizSizeItem && wizSizeItem.size === item.size) btn.classList.add('active');
      if(item.choice === 3) btn.classList.add('choice3');
      btn.addEventListener('click', function(){
        wizSizeItem = item;
        wizPitchIdx = 0;
        if((wizFamily === 'NPT' || wizFamily === 'BSPT') && item.l2){
          wizThreadLength = item.l2;
        }
        updateWizBlueprintCard();
        setWizardStage(3);
      });
      sizeGrid.appendChild(btn);
    });

    // 3. More toggle button status, placement & label
    if(moreBtn){
      var hasMore = fam.sizes.some(function(s){ return s.rangeIdx === wizRangeIdx && s.choice === 3; });
      if(hasMore){
        moreBtn.classList.add('show');
        moreBtn.textContent = wizShowMore ? '− LESS SIZES' : '+ MORE SIZES';
        moreBtn.style.color = wizShowMore ? 'var(--blue)' : 'var(--dim)';
        moreBtn.style.borderColor = wizShowMore ? '#2a618d' : 'var(--line)';
        moreBtn.style.background = wizShowMore ? '#0e273a' : '#141d24';

        // Right align: In 6-col grid, col 5-6 fits on current row if empty, or drops to next row
        moreBtn.style.gridColumn = '5 / 7';
        moreBtn.style.justifySelf = 'end';
        sizeGrid.appendChild(moreBtn);
      } else {
        moreBtn.classList.remove('show');
      }
    }
  }

  function renderStage3Pitch(){
    var pitchList = $('wizPitchGrid');
    pitchList.innerHTML = '';
    var customPitchBtn = $('btnCustomPitchEntry');

    if(wizFamily === 'FREEFORM'){
      if(customPitchBtn) customPitchBtn.style.display = 'inline-flex';
      var presets = [0.5, 0.75, 1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0];
      var grid = document.createElement('div');
      grid.className = 'wiz-grid';
      presets.forEach(function(p){
        var b = document.createElement('button');
        b.textContent = p.toFixed(2);
        if(Math.abs(wizFreePitchMm - p) < 0.01) b.classList.add('active');
        b.addEventListener('click', function(){
          wizFreePitchMm = p;
          wizFreeTab = 'metric';
          updateWizBlueprintCard();
          setWizardStage(4);
        });
        grid.appendChild(b);
      });
      pitchList.appendChild(grid);
      return;
    }

    // Hide custom pitch entry for all standard families
    if(customPitchBtn) customPitchBtn.style.display = 'none';
    if(!wizSizeItem) return;

    if(wizFamily === 'METRIC'){
      (wizSizeItem.pitches || []).forEach(function(p, idx){
        var item = document.createElement('div');
        item.className = 'wiz-pitch-item' + (idx === wizPitchIdx ? ' active' : '');
        item.innerHTML = '<span class="p-title">' + p.toFixed(2) + '</span>' +
                         '<span class="p-type" style="color:' + (idx === 0 ? 'var(--blue)' : 'var(--dim)') + ';">' +
                         (idx === 0 ? 'COARSE' : 'FINE') + '</span>';
        item.addEventListener('click', function(){
          wizPitchIdx = idx;
          updateWizBlueprintCard();
          setWizardStage(4);
        });
        pitchList.appendChild(item);
      });
    } else if(wizFamily === 'UN'){
      (wizSizeItem.pitches || []).forEach(function(pObj, idx){
        var tpi = pObj.tpi;
        var item = document.createElement('div');
        item.className = 'wiz-pitch-item' + (idx === wizPitchIdx ? ' active' : '');
        item.innerHTML = '<span class="p-title">' + tpi + ' TPI</span>' +
                         '<span class="p-type" style="color:' + (idx === 0 ? 'var(--blue)' : 'var(--dim)') + ';">' + pObj.type + '</span>';
        item.addEventListener('click', function(){
          wizPitchIdx = idx;
          updateWizBlueprintCard();
          setWizardStage(4);
        });
        pitchList.appendChild(item);
      });
    } else if(wizFamily === 'NPT' || wizFamily === 'BSPT'){
      var item = document.createElement('div');
      item.className = 'wiz-pitch-item active';
      item.innerHTML = '<span class="p-title">' + wizSizeItem.tpi + ' TPI</span>' +
                       '<span class="p-type" style="color:var(--amber);">1:16 TAPER</span>';
      item.addEventListener('click', function(){
        updateWizBlueprintCard();
        setWizardStage(4);
      });
      pitchList.appendChild(item);
    }
  }

  function renderStage4Setup(){
    $('btnWizSide').textContent = wizSide === 'EXTERNAL' ? 'EXTERNAL' : 'INTERNAL';
    $('btnWizSide').classList.toggle('active', wizSide === 'EXTERNAL');
    $('btnWizHandedness').textContent = wizHandedness === 'RIGHT' ? 'RIGHT (RH)' : 'LEFT (LH)';
    $('btnWizHandedness').classList.toggle('active', wizHandedness === 'RIGHT');
    $('btnWizCutDir').textContent = wizCutDir === 'CHUCK' ? '← TO CHUCK' : '→ TO TAIL';
    $('btnWizSkimVerify').textContent = 'SKIM PASS';
    $('btnWizSkimVerify').classList.toggle('active', wizSkimVerify);

    var lenDisplay = toDisplayLen(wizThreadLength);
    if($('wizLengthVal')) $('wizLengthVal').textContent = fmt(lenDisplay, state.unit === 'mm' ? 2 : 3);
    if($('wizLengthUnit')) $('wizLengthUnit').textContent = unitLabel();

    var bp = getWizBlueprint();
    var pCount = (wizPassesCustom && wizPasses) ? wizPasses : (bp ? bp.passes : 11);
    if($('wizPassesVal')){
      $('wizPassesVal').textContent = pCount + (wizPassesCustom ? ' (MANUAL)' : ' (AUTO)');
    }

    var isCustom = (wizFamily === 'FREEFORM');
    if($('wizStartsRow')){
      $('wizStartsRow').style.display = isCustom ? 'flex' : 'none';
    }
    if($('wizStartsVal')){
      $('wizStartsVal').textContent = wizStarts + (wizStarts > 1 ? ' STARTS' : ' START');
    }

    var note = '';
    if(wizFamily === 'NPT' || wizFamily === 'BSPT'){
      var l2Val = (wizSizeItem && wizSizeItem.l2) ? wizSizeItem.l2 : 10.2;
      note = 'Standard 1:16 pipe engagement length (L2 = ' + l2Val + ' mm)';
    } else if(isCustom && wizStarts > 1){
      var effLead = wizFreePitchMm * wizStarts;
      note = 'Multi-Start: ' + wizStarts + ' starts × ' + wizFreePitchMm.toFixed(2) + ' mm = Lead ' + effLead.toFixed(2) + ' mm/rev';
    }
    if($('wizSetupAdvisory')) $('wizSetupAdvisory').textContent = note;
  }

  // Dedicated Gearbox Leadscrew Quick-Change Pitch Engine (No Blueprint, 1-Tap)
  var GEARBOX_METRIC_RANGES = [
    { label: '0.25 – 0.80', pitches: [0.25, 0.30, 0.35, 0.40, 0.45, 0.50, 0.60, 0.70, 0.75, 0.80] },
    { label: '1.00 – 2.50', pitches: [1.00, 1.25, 1.50, 1.75, 2.00, 2.25, 2.50] },
    { label: '3.00 – 6.00', pitches: [3.00, 3.50, 4.00, 4.50, 5.00, 5.50, 6.00] }
  ];

  var GEARBOX_INCH_RANGES = [
    { label: '64 – 28', pitches: [64, 56, 48, 44, 40, 36, 32, 28] },
    { label: '27 – 12', pitches: [27, 24, 20, 18, 16, 14, 13, 12] },
    { label: '11.5 – 4', pitches: [11.5, 11, 10, 9, 8, 7, 6, 5, 4] }
  ];

  var gearPitchUnit = 'metric';
  var gearPitchRangeIdx = 1;

  function openGearboxPitchModal(){
    if(state.runState === 'running') return;
    gearPitchUnit = (state.gearThreadUnit === 'tpi') ? 'tpi' : 'metric';
    var curPitch = state.gearPitchZ;
    if(gearPitchUnit === 'metric'){
      gearPitchRangeIdx = 1;
      if(curPitch < 1.0) gearPitchRangeIdx = 0;
      else if(curPitch > 2.5) gearPitchRangeIdx = 2;
    } else {
      var curTpi = curPitch > 0 ? (25.4 / curPitch) : 16;
      gearPitchRangeIdx = 1;
      if(curTpi > 27) gearPitchRangeIdx = 0;
      else if(curTpi < 12) gearPitchRangeIdx = 2;
    }
    renderGearboxPitchModal();
    $('gearPitchOverlay').classList.add('show');
  }

  function renderGearboxPitchModal(){
    $('btnGearUnitMm').classList.toggle('active', gearPitchUnit === 'metric');
    $('btnGearUnitIn').classList.toggle('active', gearPitchUnit === 'tpi');

    var ranges = (gearPitchUnit === 'metric') ? GEARBOX_METRIC_RANGES : GEARBOX_INCH_RANGES;
    if(gearPitchRangeIdx >= ranges.length) gearPitchRangeIdx = 0;

    var rangeRow = $('gearRangeRow');
    rangeRow.innerHTML = '';
    ranges.forEach(function(r, idx){
      var b = document.createElement('button');
      b.className = 'gear-range-btn' + (idx === gearPitchRangeIdx ? ' active' : '');
      b.textContent = r.label;
      b.addEventListener('click', function(){
        gearPitchRangeIdx = idx;
        renderGearboxPitchModal();
      });
      rangeRow.appendChild(b);
    });

    var grid = $('gearPitchGrid');
    grid.innerHTML = '';
    var curRange = ranges[gearPitchRangeIdx];
    curRange.pitches.forEach(function(pVal){
      var btn = document.createElement('button');
      btn.className = 'gear-pitch-btn';

      var isCur = false;
      if(gearPitchUnit === 'metric'){
        btn.textContent = pVal.toFixed(2);
        isCur = (state.gearThreadUnit !== 'tpi' && Math.abs(state.gearPitchZ - pVal) < 0.005);
      } else {
        btn.textContent = pVal + ' TPI';
        var tpi = state.gearPitchZ > 0 ? +(25.4 / state.gearPitchZ).toFixed(1) : 0;
        isCur = (state.gearThreadUnit === 'tpi' && Math.abs(tpi - pVal) < 0.1);
      }

      if(isCur) btn.classList.add('active');

      btn.addEventListener('click', function(){
        if(gearPitchUnit === 'metric'){
          state.gearPitchZ = pVal;
          state.gearThreadUnit = 'metric';
        } else {
          state.gearPitchZ = +(25.4 / pVal).toFixed(4);
          state.gearThreadUnit = 'tpi';
        }
        state.gearFeedModeZ = 'PITCH';
        syncPitchToController(state.gearPitchZ);
        setStatusMsg('GEARBOX PITCH SET: ' + (gearPitchUnit==='metric' ? pVal.toFixed(2)+' mm' : pVal+' TPI') + ' (' + (state.gearThreadHandedness||'RH') + ')');
        $('gearPitchOverlay').classList.remove('show');
        render();
      });

      grid.appendChild(btn);
    });
  }

  $('btnGearUnitMm').addEventListener('click', function(){
    gearPitchUnit = 'metric';
    gearPitchRangeIdx = 1;
    renderGearboxPitchModal();
  });
  $('btnGearUnitIn').addEventListener('click', function(){
    gearPitchUnit = 'tpi';
    gearPitchRangeIdx = 1;
    renderGearboxPitchModal();
  });
  $('btnGearPitchClose').addEventListener('click', function(){
    $('gearPitchOverlay').classList.remove('show');
  });
  $('btnGearCustomKeypad').addEventListener('click', function(){
    $('gearPitchOverlay').classList.remove('show');
    var curVal = (gearPitchUnit === 'tpi') ?
      (state.gearPitchZ > 0 ? +(25.4 / state.gearPitchZ).toFixed(1) : 16) :
      (state.gearPitchZ > 0 ? +state.gearPitchZ.toFixed(2) : 1.50);
    openKeypad('gearPitchZ_custom', curVal);
  });

  // Dedicated Gearbox Taper Selection Modal (Presets + Linked Calculator)
  function openGearboxTaperModal(){
    if(state.runState === 'running') return;
    $('btnTaperTabPresets').classList.add('active');
    $('btnTaperTabCustom').classList.remove('active');
    $('taperPresetsView').style.display = 'flex';
    $('taperCustomView').style.display = 'none';

    // Auto-select category corresponding to current active preset
    if(state.gearTaperPresetIdx >= 0 && state.gearTaperPresetIdx < TAPER_PRESETS.length){
      gearTaperCategory = TAPER_PRESETS[state.gearTaperPresetIdx].category || 'SPINDLE';
    } else {
      gearTaperCategory = 'SPINDLE';
    }

    renderGearboxTaperPresets();
    $('gearTaperOverlay').classList.add('show');
  }

  function renderGearboxTaperPresets(){
    // Render Category Tabs
    var catRow = $('taperCatRow');
    if(catRow){
      catRow.innerHTML = '';
      TAPER_CATEGORIES.forEach(function(cat){
        var btn = document.createElement('div');
        btn.className = 'gear-range-btn tap' + (cat.id === gearTaperCategory ? ' active' : '');
        btn.textContent = cat.label;
        btn.addEventListener('click', function(){
          gearTaperCategory = cat.id;
          renderGearboxTaperPresets();
        });
        catRow.appendChild(btn);
      });
    }

    var grid = $('gearTaperPresetsGrid');
    if(!grid) return;
    grid.innerHTML = '';

    TAPER_PRESETS.forEach(function(p, idx){
      if(p.category !== gearTaperCategory) return;

      var b = document.createElement('div');
      b.className = 'taper-preset-btn tap';
      
      var isCur = (state.gearTaperPresetIdx === idx) || (Math.abs(state.gearTaperRatio - p.ratio) < 0.00005);
      if(isCur) b.classList.add('active');

      b.innerHTML = '<div class="tpb-name">' + p.name + '</div>' +
        '<div class="tpb-spec">' + p.spec + '</div>';

      b.addEventListener('click', function(){
        state.gearTaperPresetIdx = idx;
        state.gearTaperRatio = p.ratio;
        state.gearTaperAngle = p.angle;
        state.gearTaperAuthMode = null;
        state.gearTaperAuthVal = null;
        syncPitchToController();
        setStatusMsg('TAPER SET: ' + p.name + ' (' + p.spec + ')');
        $('gearTaperOverlay').classList.remove('show');
        render();
      });
      grid.appendChild(b);
    });
  }

  $('btnTaperTabPresets').addEventListener('click', function(){
    this.classList.add('active');
    $('btnTaperTabCustom').classList.remove('active');
    $('taperPresetsView').style.display = 'flex';
    $('taperCustomView').style.display = 'none';
  });

  // Custom Taper State Machine
  // Formats: 'ANGLE' (degrees), 'RATIO' (1:K, entered as K), 'DECIMAL' (C = diametral taper ratio)
  var customTaperState = {
    activeMode: 'ANGLE',  // currently displayed in the input field
    authMode: 'ANGLE',    // authoritative format entered by user
    authVal: 1.79         // exact numeric value entered in authMode
  };

  function convertTaperValue(val, fromMode, toMode){
    if(val <= 0 || isNaN(val)) return 0;
    // Step 1: Normalize fromMode to diametral taper ratio C
    var c = 0;
    if(fromMode === 'ANGLE'){
      c = 2 * Math.tan(val * Math.PI / 180);
    } else if(fromMode === 'RATIO'){
      c = 1 / val;
    } else if(fromMode === 'DECIMAL'){
      c = val;
    }

    // Step 2: Convert C to toMode
    if(toMode === 'ANGLE'){
      return Math.atan(c / 2) * (180 / Math.PI);
    } else if(toMode === 'RATIO'){
      return c > 0 ? (1 / c) : 0;
    } else if(toMode === 'DECIMAL'){
      return c;
    }
    return 0;
  }

  function updateCompanionRow(rowNum, mode, authMode, authVal){
    var lblEl = $('customTaperComp' + rowNum + 'Lbl');
    var valEl = $('customTaperComp' + rowNum + 'Val');
    if(!lblEl || !valEl) return;

    var isSource = (mode === authMode);
    var val = isSource ? authVal : convertTaperValue(authVal, authMode, mode);

    if(mode === 'ANGLE'){
      lblEl.textContent = 'ANGLE';
      valEl.textContent = (isSource ? val : val.toFixed(4)) + '°';
    } else if(mode === 'RATIO'){
      lblEl.textContent = 'RATIO';
      valEl.textContent = '1 : ' + (isSource ? val : (val >= 10 ? val.toFixed(2) : val.toFixed(3)));
    } else if(mode === 'DECIMAL'){
      lblEl.textContent = 'DECIMAL';
      valEl.textContent = isSource ? val.toString() : val.toFixed(5);
    }
  }

  function renderCustomTaperUI(){
    var active = customTaperState.activeMode;
    var auth = customTaperState.authMode;
    var authVal = customTaperState.authVal;

    // Determine value to display in main input
    var displayVal;
    if(active === auth){
      displayVal = authVal;
    } else {
      var raw = convertTaperValue(authVal, auth, active);
      if(active === 'ANGLE') displayVal = +raw.toFixed(4);
      else if(active === 'RATIO') displayVal = +(raw >= 10 ? raw.toFixed(2) : raw.toFixed(3));
      else if(active === 'DECIMAL') displayVal = +raw.toFixed(5);
    }

    var inp = $('customTaperMainInp');
    var cycleLbl = $('customTaperCycleLbl');
    var pfx = $('customTaperPrefix');
    var unit = $('customTaperUnit');

    if(active === 'ANGLE'){
      cycleLbl.textContent = 'ANGLE';
      pfx.style.display = 'none';
      unit.style.display = 'inline-flex';
      unit.textContent = '°';
      inp.step = '0.01';
      inp.value = displayVal;
    } else if(active === 'RATIO'){
      cycleLbl.textContent = 'RATIO';
      pfx.style.display = 'inline-flex';
      pfx.textContent = '1 :';
      unit.style.display = 'none';
      inp.step = '0.1';
      inp.value = displayVal;
    } else if(active === 'DECIMAL'){
      cycleLbl.textContent = 'DECIMAL';
      pfx.style.display = 'none';
      unit.style.display = 'none';
      inp.step = '0.0001';
      inp.value = displayVal;
    }

    // Render Companion Computed Rows (the other two formats, side-by-side neutral)
    var otherModes = ['ANGLE', 'RATIO', 'DECIMAL'].filter(function(m){ return m !== active; });
    updateCompanionRow(1, otherModes[0], auth, authVal);
    updateCompanionRow(2, otherModes[1], auth, authVal);
  }

  $('btnTaperTabCustom').addEventListener('click', function(){
    this.classList.add('active');
    $('btnTaperTabPresets').classList.remove('active');
    $('taperPresetsView').style.display = 'none';
    $('taperCustomView').style.display = 'flex';

    if(state.gearTaperAuthMode && state.gearTaperAuthVal){
      customTaperState.authMode = state.gearTaperAuthMode;
      customTaperState.authVal = state.gearTaperAuthVal;
      customTaperState.activeMode = state.gearTaperAuthMode;
    } else {
      customTaperState.authMode = 'ANGLE';
      customTaperState.authVal = +(state.gearTaperAngle.toFixed(4));
      customTaperState.activeMode = 'ANGLE';
    }
    renderCustomTaperUI();
  });

  $('btnCustomTaperCycle').addEventListener('click', function(){
    if(customTaperState.activeMode === 'ANGLE'){
      customTaperState.activeMode = 'RATIO';
    } else if(customTaperState.activeMode === 'RATIO'){
      customTaperState.activeMode = 'DECIMAL';
    } else {
      customTaperState.activeMode = 'ANGLE';
    }
    renderCustomTaperUI();
  });

  $('customTaperInpBox').addEventListener('click', function(){
    openKeypad('customTaperVal', $('customTaperMainInp').value);
  });

  $('customTaperMainInp').addEventListener('input', function(){
    var raw = parseFloat(this.value);
    if(isNaN(raw) || raw <= 0) return;

    customTaperState.authMode = customTaperState.activeMode;
    customTaperState.authVal = raw;

    var auth = customTaperState.authMode;
    var authVal = customTaperState.authVal;

    var otherModes = ['ANGLE', 'RATIO', 'DECIMAL'].filter(function(m){ return m !== customTaperState.activeMode; });
    updateCompanionRow(1, otherModes[0], auth, authVal);
    updateCompanionRow(2, otherModes[1], auth, authVal);
  });

  $('btnApplyCustomTaper').addEventListener('click', function(){
    var auth = customTaperState.authMode;
    var authVal = customTaperState.authVal;

    var currentInpVal = parseFloat($('customTaperMainInp').value);
    if(!isNaN(currentInpVal) && currentInpVal > 0 && Math.abs(currentInpVal - authVal) > 0.000001 && customTaperState.activeMode === auth){
      authVal = currentInpVal;
      customTaperState.authVal = authVal;
    }

    if(isNaN(authVal) || authVal <= 0){
      setStatusMsg('INVALID TAPER VALUE');
      return;
    }

    // Convert as late as possible for NanoELS controller
    var finalAngle = convertTaperValue(authVal, auth, 'ANGLE');
    var finalRatio = convertTaperValue(authVal, auth, 'DECIMAL');

    if(finalAngle <= 0 || finalAngle >= 89.9){
      setStatusMsg('TAPER ANGLE OUT OF BOUNDS (0.01° - 88°)');
      return;
    }

    state.gearTaperAuthMode = auth;
    state.gearTaperAuthVal = authVal;
    state.gearTaperAngle = +finalAngle.toFixed(4);
    state.gearTaperRatio = +finalRatio.toFixed(6);
    state.gearTaperPresetIdx = -1; // custom
    syncPitchToController();

    var desc = '';
    if(auth === 'ANGLE') desc = authVal + '°';
    else if(auth === 'RATIO') desc = '1:' + authVal;
    else if(auth === 'DECIMAL') desc = 'C=' + authVal;

    setStatusMsg('CUSTOM TAPER SET: [' + auth + ' ' + desc + '] (' + state.gearTaperAngle.toFixed(2) + '° / ' + formatTaperRatio(state.gearTaperRatio) + ')');
    $('gearTaperOverlay').classList.remove('show');
    render();
  });

  function openThreadWizard(mode){
    if(state.runState === 'running') return;
    pitchSelectTarget = 'modePitch';
    wizShowMore = false;

    if(mode === 'EDIT'){
      wizEditSession = true;
      wizSide = state.threadSide || 'EXTERNAL';
      wizHandedness = state.threadHandedness || 'RIGHT';
      wizCutDir = state.threadCutDir || 'CHUCK';
      wizSkimVerify = state.threadSkimVerify !== undefined ? state.threadSkimVerify : true;
      wizStarts = state.threadStarts || 1;
      wizThreadLength = state.threadLength || 20.0;
      wizPasses = state.threadPassCount || 11;
      wizPassesCustom = true;
      wizFamily = (state.threadStandard && THREAD_FAMILIES[state.threadStandard]) ? state.threadStandard : 'METRIC';
      var fam = THREAD_FAMILIES[wizFamily];
      var sIdx = state.threadSizeIdx || 0;
      wizSizeItem = (fam && fam.sizes) ? (fam.sizes[sIdx] || fam.sizes[0]) : null;
      wizRangeIdx = wizSizeItem ? wizSizeItem.rangeIdx : 1;
      wizPitchIdx = state.threadPitchSubIdx || 0;
      if(wizFamily === 'FREEFORM'){
        wizFreePitchMm = state.pitch || 1.75;
      }
      setWizardStage(4);
      updateWizBlueprintCard();
    } else {
      // 'NEW' mode - start fresh from Step 1
      wizEditSession = false;
      wizSide = 'EXTERNAL';
      wizHandedness = 'RIGHT';
      wizCutDir = 'CHUCK';
      wizSkimVerify = true;
      wizStarts = 1;
      wizThreadLength = 20.0;
      wizPasses = null;
      wizPassesCustom = false;
      wizFamily = 'METRIC';
      wizRangeIdx = 1;
      wizPitchIdx = 0;
      var fam = THREAD_FAMILIES['METRIC'];
      wizSizeItem = (fam && fam.sizes) ? fam.sizes[0] : null;
      setWizardStage(1);
    }
    $('pitchOverlay').classList.add('show');
  }

  function openPitchSelect(target, tab){
    openThreadWizard('NEW');
    pitchSelectTarget = target || 'modePitch';
  }

  // Wizard Navigation Listeners
  $('btnThreadNavBack').addEventListener('click', function(){
    if(wizStage === 2){
      setWizardStage(1);
    } else if(wizStage === 3){
      if(wizFamily === 'FREEFORM') setWizardStage(1);
      else setWizardStage(2);
    } else if(wizStage === 4){
      if(wizEditSession){
        // Start thread selection over while preserving machining strategy settings!
        setWizardStage(1);
      } else {
        if(wizFamily === 'FREEFORM') setWizardStage(1);
        else setWizardStage(3);
      }
    }
  });

  $('btnThreadNavClose').addEventListener('click', function(){
    $('pitchOverlay').classList.remove('show');
  });

  $('btnWizCancel').addEventListener('click', function(){
    $('pitchOverlay').classList.remove('show');
  });

  // Family selection buttons in Stage 1
  document.querySelectorAll('.wiz-family-btn').forEach(function(btn){
    btn.addEventListener('click', function(){
      var famKey = btn.dataset.family;
      wizFamily = famKey;
      wizRangeIdx = 0;
      wizShowMore = false;
      if(wizFamily === 'FREEFORM'){
        setWizardStage(3);
      } else {
        var fam = THREAD_FAMILIES[wizFamily];
        if(fam && fam.sizes.length > 0){
          wizRangeIdx = fam.ranges.length > 1 ? 1 : 0;
          wizSizeItem = fam.sizes.find(function(s){ return s.rangeIdx === wizRangeIdx; }) || fam.sizes[0];
          wizPitchIdx = 0;
        }
        setWizardStage(2);
      }
    });
  });

  // Stage 2: More sizes toggle
  $('btnSizeMore').addEventListener('click', function(){
    wizShowMore = !wizShowMore;
    renderStage2Sizes();
  });

  // Stage 2: Custom Diameter Entry (if element present)
  if($('btnSizeCustom')){
    $('btnSizeCustom').addEventListener('click', function(){
      $('pitchOverlay').classList.remove('show');
      openKeypad('threadCustomDia', '');
    });
  }

  // Stage 3: Custom Pitch Entry
  $('btnCustomPitchEntry').addEventListener('click', function(){
    $('pitchOverlay').classList.remove('show');
    var curVal = state.threadUnit === 'tpi' ? (state.pitch > 0 ? +(25.4 / state.pitch).toFixed(1) : 16) : toDisplayLen(state.pitch);
    openKeypad('pitch', curVal);
  });

  // Stage 4: Strategy Toggles
  $('btnWizSide').addEventListener('click', function(){
    wizSide = wizSide === 'EXTERNAL' ? 'INTERNAL' : 'EXTERNAL';
    if(!wizPassesCustom){
      wizPasses = null; // Re-calculate with +2 offset for internal
    }
    renderStage4Setup();
    updateWizBlueprintCard();
  });
  $('btnWizHandedness').addEventListener('click', function(){
    wizHandedness = wizHandedness === 'RIGHT' ? 'LEFT' : 'RIGHT';
    renderStage4Setup();
    updateWizBlueprintCard();
  });
  $('btnWizCutDir').addEventListener('click', function(){
    wizCutDir = wizCutDir === 'CHUCK' ? 'TAIL' : 'CHUCK';
    renderStage4Setup();
    updateWizBlueprintCard();
  });
  $('btnWizSkimVerify').addEventListener('click', function(){
    wizSkimVerify = !wizSkimVerify;
    renderStage4Setup();
    updateWizBlueprintCard();
  });

  if($('btnWizLengthBox')){
    $('btnWizLengthBox').addEventListener('click', function(){
      $('pitchOverlay').classList.remove('show');
      openKeypad('wizThreadLength', toDisplayLen(wizThreadLength));
    });
  }

  if($('btnWizPassesMinus')){
    $('btnWizPassesMinus').addEventListener('click', function(){
      var bp = getWizBlueprint();
      var cur = (wizPassesCustom && wizPasses) ? wizPasses : (bp ? bp.passes : 11);
      wizPasses = Math.max(1, cur - 1);
      wizPassesCustom = true;
      renderStage4Setup();
      updateWizBlueprintCard();
    });
  }
  if($('btnWizPassesPlus')){
    $('btnWizPassesPlus').addEventListener('click', function(){
      var bp = getWizBlueprint();
      var cur = (wizPassesCustom && wizPasses) ? wizPasses : (bp ? bp.passes : 11);
      wizPasses = Math.min(30, cur + 1);
      wizPassesCustom = true;
      renderStage4Setup();
      updateWizBlueprintCard();
    });
  }
  if($('wizPassesVal')){
    $('wizPassesVal').addEventListener('click', function(){
      var bp = getWizBlueprint();
      var cur = (wizPassesCustom && wizPasses) ? wizPasses : (bp ? bp.passes : 11);
      $('pitchOverlay').classList.remove('show');
      openKeypad('wizPasses', cur);
    });
  }

  if($('btnWizStartsMinus')){
    $('btnWizStartsMinus').addEventListener('click', function(){
      wizStarts = Math.max(1, wizStarts - 1);
      renderStage4Setup();
      updateWizBlueprintCard();
    });
  }
  if($('btnWizStartsPlus')){
    $('btnWizStartsPlus').addEventListener('click', function(){
      wizStarts = Math.min(9, wizStarts + 1);
      renderStage4Setup();
      updateWizBlueprintCard();
    });
  }
  if($('wizStartsVal')){
    $('wizStartsVal').addEventListener('click', function(){
      $('pitchOverlay').classList.remove('show');
      openKeypad('wizThreadStarts', wizStarts);
    });
  }

  function commitThreadSettingsFromWiz(bp){
    if(pitchSelectTarget === 'gearZ'){
      state.gearPitchZ = bp.pitchMm;
      state.gearPitchZ_tpi = bp.tpi || null;
      state.gearThreadHandedness = bp.handedness === 'RIGHT' ? 'RH' : 'LH';
      state.gearFeedModeZ = 'PITCH';
      state.gearThreadUnit = (bp.family === 'UN' || bp.tpi) ? 'tpi' : 'metric';
      syncPitchToController(state.gearPitchZ);
      setStatusMsg('GEARBOX PITCH SET: ' + bp.name + ' (' + state.gearThreadHandedness + ')');
    } else {
      state.threadStandard = bp.family;
      state.threadStandardName = bp.name;
      state.pitch = bp.pitchMm;
      state.threadTpiVal = bp.tpi || null;
      state.threadUnit = (bp.family === 'UN' || bp.family === 'NPT' || bp.tpi) ? 'tpi' : 'metric';
      syncPitchToController(state.pitch);
      state.threadStarts = bp.starts || wizStarts || 1;
      if(wizSizeItem && THREAD_FAMILIES[bp.family]){
        state.threadSizeIdx = THREAD_FAMILIES[bp.family].sizes.indexOf(wizSizeItem);
      }
      state.threadPitchSubIdx = wizPitchIdx;
      state.threadSide = bp.side;
      state.threadHandedness = bp.handedness;
      state.threadDir = bp.handedness;
      state.threadCutDir = bp.cutDir;
      state.threadSkimVerify = bp.skimVerify;
      state.threadProfile = bp.profile;
      state.threadConeRatio = bp.coneRatio;
      state.threadTotalDepth = bp.infeedDepth;
      state.threadPassCount = bp.passes;
      state.threadTargetStockDia = bp.stockMax;
      state.threadTargetRootDia = bp.targetRoot;
      state.threadLength = bp.length || wizThreadLength || 20.0;
      state.pass = '01 / ' + (bp.passes < 10 ? '0' : '') + bp.passes;
      setStatusMsg('CONVERSATIONAL THREAD SET: ' + bp.name + ' (' + bp.handedness + ', L=' + fmt(state.threadLength, 1) + 'mm)');
    }
  }

  // Action: SAVE & THREAD
  $('btnWizApply').addEventListener('click', function(){
    if(wizStage !== 4) return;
    var bp = getWizBlueprint();
    if(!bp) return;
    commitThreadSettingsFromWiz(bp);
    if(pitchSelectTarget !== 'gearZ'){
      state.mode = 'THREADING';
      syncModeToController('THREADING');
    }
    $('pitchOverlay').classList.remove('show');
    updateStatusMsg();
    render();
  });

  // Action: PREPARE STOCK Bridge
  $('btnWizPrepareStock').addEventListener('click', function(){
    if(wizStage !== 4) return;
    var bp = getWizBlueprint();
    if(!bp) return;
    commitThreadSettingsFromWiz(bp);
    
    if(bp.coneRatio > 0){
      // Pipe Thread -> TAPER Mode
      state.mode = 'TAPER';
      syncModeToController('TAPER');
      state.taperAngle = 1.79; // 1:16 Cone Ratio (1 deg 47 min)
      state.taperLen = bp.l2 || 15.0;
      state.taperDir = bp.side === 'EXTERNAL' ? 'INT' : 'EXT';
      setStatusMsg('PREPARE STOCK: SWITCHED TO TAPER (1.79° / 1:16 CONE, L=' + state.taperLen + 'mm FOR ' + bp.name + ')');
    } else {
      // Straight Thread -> TURNING Mode
      state.mode = 'TURNING';
      syncModeToController('TURNING');
      var targetOd = bp.side === 'EXTERNAL' ? bp.stockMax : bp.stockMin;
      var deltaDia = +(state.xDia - targetOd).toFixed(3);
      if(deltaDia > 0.05){
        state.turnTotalDepth = deltaDia;
      } else {
        state.turnTotalDepth = 0.400; // Finish skim allowance
      }
      state.turnPasses = Math.max(2, Math.round(state.turnTotalDepth / state.turnMaxDoc));
      state.turnType = bp.side;
      setStatusMsg('PREPARE STOCK: SWITCHED TO TURNING (TARGET ' + (bp.side==='EXTERNAL'?'OD':'ID') + ': Ø' + fmt(targetOd,2) + ' mm FOR ' + bp.name + ')');
    }

    $('pitchOverlay').classList.remove('show');
    updateStatusMsg();
    render();
  });

  // Skim Pass Verification Modal Handlers
  $('skimConfirmBtn').addEventListener('click', function(){
    $('skimVerifyOverlay').classList.remove('show');
    state.threadSkimStage = 'THREADING';
    state.threadCurrentPass = 1;
    state.pass = '01 / ' + (state.threadPassCount < 10 ? '0' : '') + state.threadPassCount;
    state.runState = 'running';
    setStatusMsg('SKIM VERIFIED: STARTING MULTI-PASS THREADING (STAGE 2/2)');
    syncSpindleSimulation();
    render();
  });

  $('skimAbortBtn').addEventListener('click', function(){
    $('skimVerifyOverlay').classList.remove('show');
    state.threadSkimStage = 'IDLE';
    state.runState = 'stopped';
    setStatusMsg('THREADING ABORTED BY OPERATOR: INSPECT STOCK / ADJUST TOOLING');
    syncSpindleSimulation();
    render();
  });

  // Calculator Keypad Engine
  var kpTarget = null, kpInitialVal = 0;
  function buildKeypad(){
    var grid = $('kpGrid');
    grid.innerHTML='';
    var keys = ['7','8','9', '4','5','6', '1','2','3', '±','0','.'];
    keys.forEach(function(k){
      var b = document.createElement('button');
      b.textContent = k;
      b.addEventListener('click', function(e){
        e.preventDefault();
        var inp = $('kpInput');
        var val = inp.value;
        var start = inp.selectionStart;
        var end = inp.selectionEnd;

        if(k==='±'){
          if(val.startsWith('-')) inp.value = val.slice(1);
          else if(val !== '') inp.value = '-' + val;
        } else if(k==='.'){
          if(val.indexOf('.') === -1){
            inp.value = val.slice(0, start) + '.' + val.slice(end);
            inp.setSelectionRange(start + 1, start + 1);
          }
        } else {
          inp.value = val.slice(0, start) + k + val.slice(end);
          inp.setSelectionRange(start + 1, start + 1);
        }
        inp.focus();
      });
      grid.appendChild(b);
    });
  }
  buildKeypad();

  // Bottom row handlers: Half, Backspace, and Clear
  $('kpHalfBtn').addEventListener('click', function(e){
    e.preventDefault();
    var inp = $('kpInput');
    var v = parseFloat(inp.value !== '' ? inp.value : '0');
    if(!isNaN(v)){
      inp.value = +(v / 2).toFixed(4);
      inp.focus();
      inp.select();
    }
  });

  $('kpBkspBtn').addEventListener('click', function(e){
    e.preventDefault();
    var inp = $('kpInput');
    var val = inp.value;
    var start = inp.selectionStart;
    var end = inp.selectionEnd;
    if(start === end && start > 0){
      inp.value = val.slice(0, start - 1) + val.slice(end);
      inp.setSelectionRange(start - 1, start - 1);
    } else if(start !== end){
      inp.value = val.slice(0, start) + val.slice(end);
      inp.setSelectionRange(start, start);
    }
    inp.focus();
  });

  $('kpClearBtn').addEventListener('click', function(e){
    e.preventDefault();
    $('kpInput').value = '';
    $('kpInput').focus();
  });

  var kpTitles = {
    step:'CUSTOM STEP', gearPitchZ:'Z FEED RATE', gearPitchX:'X FEED RATE',
    asyncSpeedZ:'Z ASYNC SPEED', asyncSpeedX:'X ASYNC SPEED',
    turnPitch:'TURNING FEED', turnLength:'TURNING LENGTH', turnTotalDepth:'TOTAL DEPTH (Ø)',
    turnMaxDoc:'MAX DOC / PASS (Ø)', turnPasses:'TURNING PASSES', facePitch:'FACING FEED', faceStroke:'FACING STROKE (Ø)',
    faceTotalDepth:'TOTAL FACING DEPTH', faceMaxDoc:'MAX DOC / PASS', facePasses:'FACING PASSES', pitch:'THREAD PITCH',
    threadStarts:'THREAD STARTS (1-9)', threadTotalDepth:'THREAD TOTAL DEPTH', threadCustomDia:'CUSTOM THREAD DIAMETER',
    wizThreadLength:'THREAD LENGTH',
    taperAngle:'TAPER ANGLE', taperLen:'TAPER LENGTH', cutoffFeed:'PLUNGE FEED',
    gearTaperAngle:'TAPER CONE ANGLE',
    cutoffPeck:'PECK DEPTH', cutoffRetract:'RETRACT DIST', slotStrokes:'TOTAL STROKES',
    slotFeedPerStroke:'STROKE FEED', slotLen:'STROKE LENGTH', sphereRadius:'SPHERE RADIUS',
    sphereStep:'ARC STEP', joyMaxRate:'MAX JOYSTICK SPEED', joyDeadzone:'JOYSTICK DEADZONE',
    coordZ:'SET Z COORDINATE', coordX:'SET X COORDINATE', limitTarget:'SET LIMIT',
    gearPitchZ_custom:'CUSTOM GEAR PITCH',
    customTaperVal:'CUSTOM TAPER',
    wizThreadStarts:'THREAD STARTS (1-9)'
  };

  function openKeypad(target, defaultVal){
    if(state.runState === 'running') return;
    kpTarget = target;
    kpInitialVal = (defaultVal !== undefined && defaultVal !== '') ? +parseFloat(defaultVal).toFixed(4) : 0;
    
    // Field opens pre-filled and fully selected so typing immediately overwrites it
    $('kpInput').value = defaultVal !== undefined ? defaultVal.toString() : '';
    var isIntegerCount = (target==='turnPasses' || target==='facePasses' || target==='slotStrokes' || target==='threadStarts' || target==='wizThreadStarts');
    var unitStr = ' (' + unitLabel() + ')';
    if(isIntegerCount) unitStr = ' (COUNT)';
    else if(target === 'pitch' && state.pitchTab === 'tpi') unitStr = ' (TPI)';
    else if(target === 'gearPitchZ'){
      if(state.gearFeedModeZ === 'PITCH' && state.gearThreadUnit === 'tpi') unitStr = ' (TPI)';
      else unitStr = ' (' + unitLabel() + '/rev)';
    }
    else if(target === 'asyncSpeedZ' || target === 'asyncSpeedX') unitStr = ' (' + unitLabel() + '/min)';
    else if(target === 'gearPitchZ_custom') unitStr = ' (' + (gearPitchUnit === 'tpi' ? 'TPI' : unitLabel()) + ')';
    else if(target === 'customTaperVal'){
      if(customTaperState.activeMode === 'ANGLE') unitStr = ' (DEGREES)';
      else if(customTaperState.activeMode === 'RATIO') unitStr = ' (1:K RATIO)';
      else if(customTaperState.activeMode === 'DECIMAL') unitStr = ' (DECIMAL)';
    }

    $('kpTitle').textContent = (kpTitles[target] || 'INPUT') + unitStr;
    
    // Strictly gate INSERT CURR: only show for coordinates and software limits
    var isCoordinate = (target==='coordZ' || target==='coordX' || target==='limitTarget');
    $('kpCurrBtn').classList.toggle('hide', !isCoordinate);

    // Hide ÷ 2 button for custom diameter
    var hideHalf = (target === 'threadCustomDia');
    $('kpHalfBtn').classList.toggle('hide', hideHalf);

    $('kpOverlay').classList.add('show');
    setTimeout(function(){
      $('kpInput').focus();
      $('kpInput').select();
    }, 60);
  }

  $('kpCurrBtn').addEventListener('click', function(){
    $('kpInput').value = kpInitialVal.toString();
    $('kpInput').focus();
    $('kpInput').setSelectionRange($('kpInput').value.length, $('kpInput').value.length);
  });

  $('kpCancel').addEventListener('click', function(){
    $('kpOverlay').classList.remove('show');
    if(kpTarget === 'gearPitchZ_custom'){
      $('gearPitchOverlay').classList.add('show');
    } else if(kpTarget === 'wizThreadLength' || kpTarget === 'wizThreadStarts' || kpTarget === 'pitch' || kpTarget === 'threadCustomDia'){
      $('pitchOverlay').classList.add('show');
    }
  });
  $('kpConfirm').addEventListener('click', function(){
    var raw = $('kpInput').value;
    var v = parseFloat(raw !== '' ? raw : '0');
    if(!isNaN(v)){
      var mm = state.unit==='mm' ? v : v*25.4;
      if(kpTarget==='coordZ'){ state.z = mm; }
      else if(kpTarget==='coordX'){ state.xDia = state.xRep==='diameter' ? mm : mm*2; }
      else if(kpTarget==='limitTarget'){ state[limitTarget] = mm; }
      else if(kpTarget==='step'){
        if(state.unit === 'mm'){
          JOG_STEPS_MM[state.stepIdx % JOG_STEPS_MM.length] = Math.abs(v);
        } else {
          JOG_STEPS_IN[state.stepIdx % JOG_STEPS_IN.length] = Math.abs(v);
        }
      }
      else if(kpTarget==='gearPitchZ'){
        if(state.gearFeedModeZ === 'PITCH' && state.gearThreadUnit === 'tpi'){
          state.gearPitchZ_tpi = Math.max(0.1, v);
          state.gearPitchZ = +(25.4 / state.gearPitchZ_tpi).toFixed(4);
        } else {
          state.gearPitchZ_tpi = null;
          state.gearPitchZ = Math.max(0.001, Math.abs(mm));
        }
        syncPitchToController(state.gearPitchZ);
        render();
      }
      else if(kpTarget==='gearPitchZ_custom'){
        if(gearPitchUnit === 'tpi'){
          state.gearPitchZ_tpi = Math.max(0.1, v);
          state.gearPitchZ = +(25.4 / state.gearPitchZ_tpi).toFixed(4);
          state.gearThreadUnit = 'tpi';
        } else {
          state.gearPitchZ_tpi = null;
          state.gearPitchZ = Math.max(0.01, Math.abs(mm));
          state.gearThreadUnit = 'metric';
        }
        state.gearFeedModeZ = 'PITCH';
        syncPitchToController(state.gearPitchZ);
        setStatusMsg('GEARBOX PITCH SET: ' + (gearPitchUnit==='metric' ? state.gearPitchZ.toFixed(2)+' mm' : state.gearPitchZ_tpi+' TPI') + ' (' + (state.gearThreadHandedness||'RH') + ')');
        render();
      }
      else if(kpTarget==='gearPitchX'){ 
        state.gearPitchX = mm; 
        syncPitchToController(state.gearPitchX);
        render();
      }
      else if(kpTarget==='asyncSpeedZ'){ state.asyncSpeedZ = Math.round(state.unit === 'mm' ? v : v * 25.4); render(); }
      else if(kpTarget==='asyncSpeedX'){ state.asyncSpeedX = Math.round(state.unit === 'mm' ? v : v * 25.4); render(); }
      else if(kpTarget==='turnPitch'){ 
        state.turnPitch = mm; 
        syncPitchToController(state.turnPitch);
        render();
      }
      else if(kpTarget==='turnLength'){ 
        state.turnLength = Math.abs(mm);
        recalcTurnLimits();
      }
      else if(kpTarget==='turnTotalDepth'){ 
        state.turnTotalDepth = Math.abs(mm);
        state.turnPasses = Math.max(1, Math.ceil(state.turnTotalDepth / state.turnMaxDoc));
      }
      else if(kpTarget==='turnMaxDoc'){ 
        state.turnMaxDoc = Math.max(0.01, Math.abs(mm));
        state.turnPasses = Math.max(1, Math.ceil(state.turnTotalDepth / state.turnMaxDoc));
      }
      else if(kpTarget==='turnPasses'){
        state.turnPasses = Math.max(1, Math.round(v));
        state.turnMaxDoc = +(state.turnTotalDepth / state.turnPasses).toFixed(3);
      }
      else if(kpTarget==='facePitch'){ 
        state.facePitch = mm; 
        syncPitchToController(state.facePitch);
        render();
      }
      else if(kpTarget==='faceStroke'){ 
        state.faceStroke = Math.abs(mm);
        recalcFaceLimits();
      }
      else if(kpTarget==='faceTotalDepth'){ 
        state.faceTotalDepth = Math.abs(mm);
        state.facePasses = Math.max(1, Math.ceil(state.faceTotalDepth / state.faceMaxDoc));
      }
      else if(kpTarget==='faceMaxDoc'){ 
        state.faceMaxDoc = Math.max(0.01, Math.abs(mm));
        state.facePasses = Math.max(1, Math.ceil(state.faceTotalDepth / state.faceMaxDoc));
      }
      else if(kpTarget==='facePasses'){
        state.facePasses = Math.max(1, Math.round(v));
        state.faceMaxDoc = +(state.faceTotalDepth / state.facePasses).toFixed(3);
      }
      else if(kpTarget==='pitch'){
        if(state.pitchTab === 'tpi'){
          state.threadTpiVal = Math.max(0.1, v);
          state.pitch = +(25.4 / state.threadTpiVal).toFixed(4);
          state.threadUnit = 'tpi';
        } else {
          state.threadTpiVal = null;
          state.pitch = mm;
          state.threadUnit = 'metric';
        }
        syncPitchToController(state.pitch);
        render();
      }
      else if(kpTarget==='wizCustomPitch'){
        wizFreePitchMm = state.pitch;
        wizCustomPitch = state.pitch;
        updateWizBlueprintCard();
        setWizardStage(4);
        $('pitchOverlay').classList.add('show');
      }
      else if(kpTarget==='threadCustomDia'){
        var custDia = Math.abs(mm);
        if(custDia > 0){
          var coarseP = +(0.1 * custDia).toFixed(2);
          if(coarseP < 0.25) coarseP = 0.25;
          var fineP = +(0.075 * custDia).toFixed(2);
          wizSizeItem = {
            size: 'Ø' + fmt(custDia, 2),
            dia: custDia,
            rangeIdx: 0,
            choice: 1,
            pitches: [coarseP, fineP],
            stockMin: +(custDia - 0.15).toFixed(2),
            stockMax: +custDia.toFixed(2)
          };
          wizPitchIdx = 0;
          setWizardStage(3);
          $('pitchOverlay').classList.add('show');
        }
      }
      else if(kpTarget==='wizThreadLength'){
        wizThreadLength = Math.max(0.1, Math.abs(mm));
        renderStage4Setup();
        updateWizBlueprintCard();
        $('pitchOverlay').classList.add('show');
      }
      else if(kpTarget==='wizPasses'){
        wizPasses = Math.max(1, Math.min(30, Math.round(v)));
        wizPassesCustom = true;
        renderStage4Setup();
        updateWizBlueprintCard();
        $('pitchOverlay').classList.add('show');
      }
      else if(kpTarget==='wizThreadStarts'){
        wizStarts = Math.max(1, Math.min(9, Math.round(v)));
        renderStage4Setup();
        updateWizBlueprintCard();
        $('pitchOverlay').classList.add('show');
      }
      else if(kpTarget==='customTaperVal'){
        if(!isNaN(v) && v > 0){
          customTaperState.authMode = customTaperState.activeMode;
          customTaperState.authVal = v;
          renderCustomTaperUI();
        }
      }
      else if(kpTarget==='threadStarts'){
        state.threadStarts = Math.max(1, Math.min(9, Math.round(v)));
      }
      else if(kpTarget==='threadTotalDepth'){
        state.threadTotalDepth = Math.abs(mm);
        state.threadPassCount = Math.max(6, Math.min(18, Math.round(state.threadTotalDepth / 0.10)));
      }
      else if(kpTarget==='taperAngle'){ state.taperAngle = v; }
      else if(kpTarget==='gearTaperAngle'){
        state.gearTaperAngle = Math.abs(v);
        state.gearTaperRatio = +(2 * Math.tan((state.gearTaperAngle * Math.PI) / 180)).toFixed(5);
        var matchIdx = -1;
        for(var i = 0; i < TAPER_PRESETS.length; i++){
          if(Math.abs(TAPER_PRESETS[i].angle - state.gearTaperAngle) < 0.05){
            matchIdx = i;
            break;
          }
        }
        if(matchIdx !== -1){
          state.gearTaperPresetIdx = matchIdx;
        }
      }
      else if(kpTarget==='taperLen'){ state.taperLen = mm; }
      else if(kpTarget==='cutoffFeed'){ state.cutoffFeed = mm; }
      else if(kpTarget==='cutoffPeck'){ state.cutoffPeck = mm; }
      else if(kpTarget==='cutoffRetract'){ state.cutoffRetract = mm; }
      else if(kpTarget==='slotStrokes'){ state.slotStrokes = Math.max(1, Math.round(v)); }
      else if(kpTarget==='slotFeedPerStroke'){ state.slotFeedPerStroke = mm; }
      else if(kpTarget==='slotLen'){ state.slotLen = mm; }
      else if(kpTarget==='sphereRadius'){ state.sphereRadius = mm; }
      else if(kpTarget==='sphereStep'){ state.sphereStep = mm; }
      else if(kpTarget==='joyMaxRate'){ state.joyMaxRate = Math.round(v); }
      else if(kpTarget==='joyDeadzone'){ state.joyDeadzone = Math.round(v); }
    }
    $('kpOverlay').classList.remove('show');
    updateStatusMsg();
    render();
  });

  var ledgerContent = '' +
    '<h4>1. Single Hardware Register Truth & Radical Shop Safety</h4>' +
    '<p>NanoELS controller firmware only supports a single pair of global limit registers per axis (<code>z_min</code>, <code>z_max</code>, <code>x_min</code>, <code>x_max</code>). To avoid false operator confidence, the UI refuses to run "ghost limits." What is shown on screen matches the physical hardware register. In Turning/Facing, setting a dimension updates the active limit register directly.</p>' +
    '<h4>2. Opt-In Limit Restoration Quality-of-Life (QoL)</h4>' +
    '<p>By default (<code>RESTORE LIMITS: NO</code>), the system mirrors native NanoELS behavior: running a cycle permanently establishes new limit boundaries. For operators who previously set a chuck safety stop, toggling <code>RESTORE LIMITS: YES</code> directs the ESP32 to cache prior limits and restore them automatically upon cycle completion or abort.</p>' +
    '<h4>3. Dimension-First Turning & Facing Workflow</h4>' +
    '<p>Machinists think in part dimensions (e.g. 25mm long tenon) rather than machine stop coordinates. Typing <code>LENGTH = 25.0 mm</code> calculates <code>Z_MIN = Z_CURRENT - 25.0 mm</code> instantly, eliminating air-cutting jog math while updating the limit chip visibly.</p>' +
    '<h4>4. Derived Passes vs. Independent Locks</h4>' +
    '<p>NanoELS calculates passes automatically based on <code>TOTAL DEPTH</code> and <code>MAX DOC</code>. Because pass count is mathematically constrained by machine rigidity ceilings, pass count is displayed as a derived calculation rather than an unconstrained input.</p>' +
    '<h4>5. Operator-Aligned Coordinate Directions</h4>' +
    '<p>From the operator apron perspective: <code>↑ TO CENTER (X−)</code> drives the cross-slide into the cut (reducing diameter), while <code>↓ RETRACT (X+)</code> backs the cross-slide toward the operator (increasing diameter). Northwest chamfer is <code>↖ Z− X−</code> (chuck and into cut).</p>' +
    '<h4>6. Tabular Monospace Readout Stability</h4>' +
    '<p>Axis fonts are locked at 50px JetBrains Mono with tabular figures. Digits never snap, shrink, or twitch as coordinates transition through negative signs or extra integer places.</p>';

  var manualContent = '' +
    '<h4>Multi-Pass Turning Workflow</h4>' +
    '<ul>' +
      '<li><b>1. Touch Off:</b> Jog tool to stock face, tap Z value, and select <code>ZERO (0.000)</code>.</li>' +
      '<li><b>2. Set Part Length:</b> Tap <code>LENGTH</code> in Turning card, type drawing length (e.g., 25.0). The <code>Z MIN</code> limit chip automatically sets to -25.000mm.</li>' +
      '<li><b>3. Infeed Setup:</b> Set <code>TOTAL DEPTH</code> (amount to remove on diameter) and <code>MAX DOC / PASS</code>. The passes count updates automatically.</li>' +
      '<li><b>4. Safety Choice:</b> If you had a chuck stop set prior, toggle <code>RESTORE LIMITS: ON FINISH: YES</code>.</li>' +
      '<li><b>5. Execute:</b> Press green <code>START FEED</code>. The Dynamic Center Deck displays <code>PASS 01 / N</code> with live feed trim buttons active.</li>' +
    '</ul>' +
    '<h4>Gearbox Mode</h4>' +
    '<ul>' +
      '<li>Toggle between <code>SYNC (SPINDLE)</code> (feed locked to spindle) and <code>ASYNC (TIME)</code> (constant mm/min) in the right card.</li>' +
      '<li>In Sync mode, toggle Z units between <code>FEED</code> (mm/rev), <code>PITCH</code> (mm lead), and <code>TPI</code> (threads/inch).</li>' +
      '<li>Use center deck nudge buttons <code>[ − ]</code> and <code>[ + ]</code> for live feed override and <code>DIRECTION</code> toggle.</li>' +
    '</ul>';

  function showDocTab(which){
    $('tabDocDecisions').classList.toggle('active', which==='decisions');
    $('tabDocManual').classList.toggle('active', which==='manual');
    $('docContent').innerHTML = which==='decisions' ? ledgerContent : manualContent;
  }

  $('tabDocDecisions').addEventListener('click', function(){ showDocTab('decisions'); });
  $('tabDocManual').addEventListener('click', function(){ showDocTab('manual'); });
  $('docClose').addEventListener('click', function(){ $('docOverlay').classList.remove('show'); });

  // --- NanoELS Integration & Dev Console Controller ---
  function initDevConsoleAndService(){
    var savedBackend = localStorage.getItem('nanoels_backend') || 'emulator';
    var savedHost = localStorage.getItem('nanoels_host') || 'nanoels.local';
    var savedLogStatus = localStorage.getItem('nanoels_log_status_pushes') === 'true';
    var savedBufferSize = localStorage.getItem('nanoels_log_buffer') || '1000';
    var savedHeight = localStorage.getItem('nanoels_log_height') || '200';

    var btnEmulator = $('btnDevModeEmulator');
    var btnLive = $('btnDevModeLive');
    var hostInput = $('devHostInput');
    var hostWrap = $('devHostWrap');
    var btnConnect = $('btnDevConnect');
    var statusBadge = $('devStatusBadge');
    var logWindow = $('devLogWindow');
    var chkLogStatus = $('chkLogStatusPushes');
    var selBufferSize = $('devBufferSize');
    var chkAutoScroll = $('chkAutoScroll');
    var btnScrollBottom = $('btnDevScrollBottom');
    var btnClear = $('btnDevClearLog');
    var resizeBar = $('devResizeBar');

    var consecutiveTelemetryCount = 0;
    var activeStreamRow = null;
    var activeStreamCountBadge = null;
    var activeStreamSummary = null;
    var activeStreamDrawerContent = null;
    var activeStreamTime = null;

    function renderTelemetryDrawerHTML(data, raw){
      if(!data) return '';
      return '' +
        '<div class="dev-telemetry-grid">' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Axis Z</span><span class="dev-telemetry-val">' + (data.z || '--') + ' ' + (data.measure||'MM') + '</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Axis X</span><span class="dev-telemetry-val">' + (data.x || '--') + ' ' + (data.measure||'MM') + '</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Spindle</span><span class="dev-telemetry-val">' + (data.rpm || '0') + ' RPM</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Status</span><span class="dev-telemetry-val" style="color:' + (data.status==='ON'?'var(--green)':'var(--red)') + '">' + (data.status || 'OFF') + '</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Mode</span><span class="dev-telemetry-val">' + (data.mode || '--') + '</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Lead/Pitch</span><span class="dev-telemetry-val">' + (data.pitch || '--') + '</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">Z Limits</span><span class="dev-telemetry-val">[' + (data.zLeft || '--') + ' / ' + (data.zRight || '--') + ']</span></div>' +
          '<div class="dev-telemetry-field"><span class="dev-telemetry-key">X Limits</span><span class="dev-telemetry-val">[' + (data.xLeft || '--') + ' / ' + (data.xRight || '--') + ']</span></div>' +
        '</div>' +
        (raw ? '<div class="dev-telemetry-raw-box">' + escapeHtml(raw) + '</div>' : '');
    }

    if(hostInput) hostInput.value = savedHost;
    if(chkLogStatus) chkLogStatus.checked = savedLogStatus;
    if(selBufferSize) selBufferSize.value = savedBufferSize;
    if(logWindow) logWindow.style.height = savedHeight + 'px';

    var heightBtns = document.querySelectorAll('[data-log-height]');
    heightBtns.forEach(function(b){
      b.classList.toggle('active', b.dataset.logHeight === savedHeight);
      b.addEventListener('click', function(){
        var h = b.dataset.logHeight;
        logWindow.style.height = h + 'px';
        heightBtns.forEach(function(btn){ btn.classList.toggle('active', btn === b); });
        localStorage.setItem('nanoels_log_height', h);
      });
    });

    if(resizeBar){
      var startY, startH;
      function onPointerMove(e){
        var newH = Math.max(80, Math.min(650, startH + (e.clientY - startY)));
        logWindow.style.height = newH + 'px';
      }
      function onPointerUp(){
        window.removeEventListener('pointermove', onPointerMove);
        window.removeEventListener('pointerup', onPointerUp);
        var finalH = parseInt(logWindow.style.height, 10);
        localStorage.setItem('nanoels_log_height', finalH);
        heightBtns.forEach(function(btn){
          btn.classList.toggle('active', btn.dataset.logHeight === finalH.toString());
        });
      }
      resizeBar.addEventListener('pointerdown', function(e){
        startY = e.clientY;
        startH = logWindow.getBoundingClientRect().height;
        window.addEventListener('pointermove', onPointerMove);
        window.addEventListener('pointerup', onPointerUp);
      });
    }

    var isUserScrolledUp = false;
    logWindow.addEventListener('scroll', function(){
      var distFromBottom = logWindow.scrollHeight - logWindow.clientHeight - logWindow.scrollTop;
      isUserScrolledUp = distFromBottom > 35;
      if(btnScrollBottom){
        btnScrollBottom.style.display = isUserScrolledUp ? 'inline-flex' : 'none';
      }
    });

    if(btnScrollBottom){
      btnScrollBottom.addEventListener('click', function(){
        logWindow.scrollTop = logWindow.scrollHeight;
        isUserScrolledUp = false;
        if(chkAutoScroll) chkAutoScroll.checked = true;
        btnScrollBottom.style.display = 'none';
      });
    }

    if(chkAutoScroll){
      chkAutoScroll.addEventListener('change', function(){
        if(chkAutoScroll.checked){
          logWindow.scrollTop = logWindow.scrollHeight;
          isUserScrolledUp = false;
          if(btnScrollBottom) btnScrollBottom.style.display = 'none';
        }
      });
    }

    if(selBufferSize){
      selBufferSize.addEventListener('change', function(){
        localStorage.setItem('nanoels_log_buffer', selBufferSize.value);
        var maxLimit = parseInt(selBufferSize.value, 10);
        while(logWindow.children.length > maxLimit){
          if(logWindow.firstChild === activeStreamRow){
            activeStreamRow = null;
          }
          logWindow.removeChild(logWindow.firstChild);
        }
      });
    }

    function updatePills(backend){
      if(btnEmulator) btnEmulator.classList.toggle('active', backend === 'emulator');
      if(btnLive) btnLive.classList.toggle('active', backend === 'live');
      if(hostWrap){
        hostWrap.style.opacity = (backend === 'live') ? '1' : '0.45';
      }
    }
    updatePills(savedBackend);

    // Initialize Service
    if(window.NanoelsService){
      window.nanoelsClient = new window.NanoelsService({
        mode: savedBackend,
        host: savedHost,
        port: 81,
        logStatusPushes: savedLogStatus
      });

      // Handle Connection Status Updates
      window.nanoelsClient.addEventListener('connection', function(e){
        var detail = e.detail;
        if(detail.mode === 'emulator'){
          statusBadge.className = 'dev-status-badge emu';
          statusBadge.innerHTML = '<span class="dev-dot"></span>EMULATOR ACTIVE';
        } else if(detail.connecting){
          statusBadge.className = 'dev-status-badge live-connecting';
          statusBadge.innerHTML = '<span class="dev-dot"></span>CONNECTING (' + detail.host + ':81)...';
        } else if(detail.connected){
          statusBadge.className = 'dev-status-badge live-connected';
          statusBadge.innerHTML = '<span class="dev-dot"></span>CONNECTED (' + detail.host + ':81)';
        } else {
          statusBadge.className = 'dev-status-badge live-offline';
          statusBadge.innerHTML = '<span class="dev-dot"></span>OFFLINE (ws://' + detail.host + ':81)';
        }
      });

      // Handle Traffic Logging
      window.nanoelsClient.addEventListener('traffic', function(e){
        var item = e.detail;

        if(item.type === 'telemetry_packet'){
          if(!chkLogStatus || !chkLogStatus.checked){
            return;
          }

          consecutiveTelemetryCount++;

          var t = item.time;
          var timeStr = ('0' + t.getHours()).slice(-2) + ':' +
                        ('0' + t.getMinutes()).slice(-2) + ':' +
                        ('0' + t.getSeconds()).slice(-2) + '.' +
                        ('00' + t.getMilliseconds()).slice(-3);

          if(consecutiveTelemetryCount <= 3){
            var row = document.createElement('div');
            row.className = 'dev-telemetry-row';
            row.innerHTML = '' +
              '<div class="dev-telemetry-header">' +
                '<div class="dev-telemetry-header-left">' +
                  '<span class="dev-log-time">' + timeStr + '</span>' +
                  '<span class="dev-log-badge rx">RX ◀</span>' +
                  '<span class="dev-telemetry-summary">' + escapeHtml(item.summary) + '</span>' +
                '</div>' +
                '<button type="button" class="dev-telemetry-expand-btn">[+] DATA</button>' +
              '</div>' +
              '<div class="dev-telemetry-drawer">' + renderTelemetryDrawerHTML(item.data, item.raw) + '</div>';

            var btnExpand = row.querySelector('.dev-telemetry-expand-btn');
            var drawer = row.querySelector('.dev-telemetry-drawer');
            btnExpand.addEventListener('click', function(ev){
              ev.stopPropagation();
              var open = drawer.classList.toggle('open');
              btnExpand.textContent = open ? '[−] HIDE' : '[+] DATA';
            });
            row.querySelector('.dev-telemetry-header').addEventListener('click', function(){
              var open = drawer.classList.toggle('open');
              btnExpand.textContent = open ? '[−] HIDE' : '[+] DATA';
            });

            logWindow.appendChild(row);
            activeStreamRow = null;
          } else {
            var childrenCount = consecutiveTelemetryCount - 3;
            if(!activeStreamRow){
              var row = document.createElement('div');
              row.className = 'dev-telemetry-row stream-group';
              row.innerHTML = '' +
                '<div class="dev-telemetry-header">' +
                  '<div class="dev-telemetry-header-left">' +
                    '<span class="dev-log-time stream-time">' + timeStr + '</span>' +
                    '<span class="dev-log-badge rx">RX ◀</span>' +
                    '<span class="dev-log-badge sys" style="font-size:9px;">STREAM</span>' +
                    '<span class="dev-telemetry-summary stream-summary">' + escapeHtml(item.summary) + '</span>' +
                    '<span class="dev-telemetry-count-badge stream-count">+' + childrenCount + ' updates</span>' +
                  '</div>' +
                  '<button type="button" class="dev-telemetry-expand-btn">[+] DATA</button>' +
                '</div>' +
                '<div class="dev-telemetry-drawer stream-drawer">' +
                  '<div class="stream-drawer-content">' + renderTelemetryDrawerHTML(item.data, item.raw) + '</div>' +
                '</div>';

              var btnExpand = row.querySelector('.dev-telemetry-expand-btn');
              var drawer = row.querySelector('.stream-drawer');
              btnExpand.addEventListener('click', function(ev){
                ev.stopPropagation();
                var open = drawer.classList.toggle('open');
                btnExpand.textContent = open ? '[−] HIDE' : '[+] DATA';
              });
              row.querySelector('.dev-telemetry-header').addEventListener('click', function(){
                var open = drawer.classList.toggle('open');
                btnExpand.textContent = open ? '[−] HIDE' : '[+] DATA';
              });

              logWindow.appendChild(row);

              activeStreamRow = row;
              activeStreamCountBadge = row.querySelector('.stream-count');
              activeStreamSummary = row.querySelector('.stream-summary');
              activeStreamDrawerContent = row.querySelector('.stream-drawer-content');
              activeStreamTime = row.querySelector('.stream-time');
            } else {
              // In-place mutation: ZERO new DOM elements added!
              if(activeStreamCountBadge) activeStreamCountBadge.textContent = '+' + childrenCount + ' updates';
              if(activeStreamSummary) activeStreamSummary.textContent = item.summary;
              if(activeStreamTime) activeStreamTime.textContent = timeStr;
              if(activeStreamDrawerContent) activeStreamDrawerContent.innerHTML = renderTelemetryDrawerHTML(item.data, item.raw);
            }
          }
        } else {
          // Regular log line (TX commands, KEY presses, SYSINFO, warnings, etc.)
          consecutiveTelemetryCount = 0;
          activeStreamRow = null;

          var row = document.createElement('div');
          row.className = 'dev-log-row';

          var t = item.time;
          var timeStr = ('0' + t.getHours()).slice(-2) + ':' +
                        ('0' + t.getMinutes()).slice(-2) + ':' +
                        ('0' + t.getSeconds()).slice(-2) + '.' +
                        ('00' + t.getMilliseconds()).slice(-3);

          var badgeClass = item.direction === 'TX' ? 'tx' : (item.direction === 'RX' ? 'rx' : (item.direction === 'ERR' ? 'err' : 'sys'));
          var dirGlyph = item.direction === 'TX' ? 'TX ▶' : (item.direction === 'RX' ? 'RX ◀' : item.direction);

          row.innerHTML = '<span class="dev-log-time">' + timeStr + '</span>' +
                          '<span class="dev-log-badge ' + badgeClass + '">' + dirGlyph + '</span>' +
                          '<span class="dev-log-raw">' + escapeHtml(item.raw) + '</span>' +
                          (item.annotation ? '<span class="dev-log-note">(' + escapeHtml(item.annotation) + ')</span>' : '');

          logWindow.appendChild(row);
        }

        // Keep DOM prune limit per selected buffer size
        var maxLimit = parseInt(selBufferSize ? selBufferSize.value : 1000, 10);
        while(logWindow.children.length > maxLimit){
          if(logWindow.firstChild === activeStreamRow){
            activeStreamRow = null;
          }
          logWindow.removeChild(logWindow.firstChild);
        }

        // Only auto-scroll if enabled AND user has not scrolled up
        if(chkAutoScroll && chkAutoScroll.checked && !isUserScrolledUp){
          logWindow.scrollTop = logWindow.scrollHeight;
        }
      });

      // Handle Telemetry Updates into Prototype State (Layer 1 / Layer 2 Decoupled)
      window.nanoelsClient.addEventListener('telemetry', function(e){
        var data = e.detail;
        if(!data) return;
        var c = data.canonical;

        // 1. Synchronize Alpine Reactive Store
        if(window.Alpine && Alpine.store('machine')){
          Alpine.store('machine').updateFromTelemetry(data);
        }

        // 2. Units & Measure (MM / IN)
        if(c && c.measure !== undefined){
          state.unit = c.measure === 1 ? 'in' : 'mm';
        } else if(data.measure !== undefined && data.measure !== ''){
          var m = data.measure.toUpperCase();
          state.unit = (m === 'IN') ? 'in' : 'mm';
        }
        var scale = (state.unit === 'in') ? 25.4 : 1.0;

        // 3. Coordinate Positions (Z & X)
        if(c && c.zDu !== undefined){
          state.z = c.zDu / 10000.0;
        } else if(data.z !== undefined && data.z !== ''){
          state.z = parseFloat(data.z) * scale;
        }

        var controllerIsInDia = (c && c.dia !== undefined) ? c.dia : ((data.diameter !== undefined) ? (data.diameter === '1' || data.diameter === true) : true);
        state.xRep = controllerIsInDia ? 'diameter' : 'radius';

        if(c && c.xDu !== undefined){
          state.xDia = (c.xDu / 10000.0) * (controllerIsInDia ? 2.0 : 1.0);
        } else if(data.x !== undefined && data.x !== ''){
          var rawX = parseFloat(data.x) * scale;
          state.xDia = controllerIsInDia ? rawX : (rawX * 2);
        }

        // 4. Spindle Readouts (RPM, TURNS, ANGLE)
        if(c && c.rpm !== undefined){
          state.rpm = c.rpm;
        } else if(data.rpm !== undefined && data.rpm !== ''){
          state.rpm = parseInt(data.rpm, 10) || 0;
        }

        if(c && c.turns !== undefined){
          state.turns = c.turns;
        } else if(data.turns !== undefined && data.turns !== ''){
          state.turns = parseFloat(data.turns) || 0;
        }

        if(c && c.angle !== undefined){
          state.angle = c.angle;
        } else if(data.angle !== undefined && data.angle !== ''){
          state.angle = parseFloat(String(data.angle).replace('°', '')) || 0;
        }

        // 5. Run State & Mode (ON / OFF / SYN)
        if(c && (c.on !== undefined || c.isOn !== undefined)){
          state.runState = (c.on || c.isOn) ? 'running' : 'stopped';
        } else if(data.status !== undefined && data.status !== ''){
          state.runState = (data.status === 'ON' || data.status === 'SYN') ? 'running' : 'stopped';
        }

        // 6. Steps (Jog & Feed)
        if(c && c.jogStepDu !== undefined){
          state.step = c.jogStepDu / 10000.0;
        } else if(data.step !== undefined && data.step !== ''){
          state.step = parseFloat(data.step) * scale;
        }
        if(c && c.feedStepDu !== undefined){
          state.feedStep = c.feedStepDu / 10000.0;
        }

        // 7. Soft Limits (Z Left/Right, X Up/Down)
        if(c && c.zLeftDu !== undefined){
          state.zLeft = (c.zLeftDu === 2147483647) ? null : Math.abs(c.zDu - c.zLeftDu) / 10000.0;
        } else if(data.zLeft !== undefined){
          state.zLeft = (data.zLeft === '') ? null : Math.abs(parseFloat(data.zLeft)) * scale;
        }

        if(c && c.zRightDu !== undefined){
          state.zRight = (c.zRightDu === -2147483648) ? null : Math.abs(c.zRightDu - c.zDu) / 10000.0;
        } else if(data.zRight !== undefined){
          state.zRight = (data.zRight === '') ? null : Math.abs(parseFloat(data.zRight)) * scale;
        }

        if(c && c.xUpDu !== undefined){
          state.xLeft = (c.xUpDu === 2147483647) ? null : Math.abs(c.xDu - c.xUpDu) / 10000.0;
        } else if(data.xLeft !== undefined){
          state.xLeft = (data.xLeft === '') ? null : Math.abs(parseFloat(data.xLeft)) * scale;
        }

        if(c && c.xDownDu !== undefined){
          state.xRight = (c.xDownDu === -2147483648) ? null : Math.abs(c.xDownDu - c.xDu) / 10000.0;
        } else if(data.xRight !== undefined){
          state.xRight = (data.xRight === '') ? null : Math.abs(parseFloat(data.xRight)) * scale;
        }

        // 8. Authoritative Taper Intent (Domain I)
        if(c && c.tpPreset !== undefined && c.tpPreset >= 0){
          state.gearTaperPresetIdx = c.tpPreset;
          if(window.TAPER_PRESETS && window.TAPER_PRESETS[c.tpPreset]){
            state.gearTaperRatio = window.TAPER_PRESETS[c.tpPreset].ratio;
            state.gearTaperAngle = window.TAPER_PRESETS[c.tpPreset].angle;
          }
        } else if(c && c.tpMode === 0 && c.tpVal > 0){
          state.gearTaperRatio = c.tpVal; // 1:K exact author value
          state.gearTaperAuthMode = 'RATIO';
          state.gearTaperAuthVal = c.tpVal;
          state.gearTaperPresetIdx = -1;
        } else if(c && c.tpMode === 1 && c.tpVal > 0){
          state.gearTaperAngle = c.tpVal; // degrees exact author value
          state.gearTaperAuthMode = 'ANGLE';
          state.gearTaperAuthVal = c.tpVal;
          state.gearTaperPresetIdx = -1;
        }

        // 9. Authoritative Thread Intent (Domain I)
        if(c && c.thFmt === 1 && c.thPitch > 0){
          state.threadTpiVal = c.thPitch;
          state.gearPitchZ_tpi = c.thPitch;
          state.pitch = +(25.4 / c.thPitch).toFixed(4);
          state.threadUnit = 'tpi';
          state.gearThreadUnit = 'tpi';
        } else if(c && c.thFmt === 0 && c.thPitch > 0){
          state.pitch = c.thPitch;
          state.threadTpiVal = null;
          state.gearPitchZ_tpi = null;
          state.threadUnit = 'mm';
          state.gearThreadUnit = 'metric';
        }

        // 10. Indicators & Status Message
        if(data.abs !== undefined) state.absMode = (data.abs === '1');
        if(data.xDro !== undefined) state.xIsDro = (data.xDro === '1');
        if(data.zDro !== undefined) state.zIsDro = (data.zDro === '1');
        if(data.message !== undefined && data.message !== '') state.statusMsg = data.message;

        // 5. Controller Operating Mode Mapping
        if(data.mode !== undefined && data.mode !== ''){
          var modeKey = data.mode.toUpperCase();
          if(modeKey === 'GEAR' || modeKey === 'XGEAR' || modeKey === 'ASYNC'){
            state.mode = 'GEARBOX';
            state.gearboxMode = (modeKey === 'ASYNC') ? 'ASYNC' : 'SYNC';
            if(modeKey === 'XGEAR') state.activeFeedAxis = 'X';
            else state.activeFeedAxis = 'Z';
          } else if(modeKey === 'CONE'){
            if(state.mode === 'TAPER'){
              // Trigger A: Motion started externally on lathe hardware while UI was idle in TAPER setup
              var isControllerRunning = (data.status === 'ON' || data.status === 'SYN');
              if(isControllerRunning && !state.taperCycleActive){
                state.mode = 'GEARBOX';
                state.gearboxMode = 'SYNC';
                state.activeFeedAxis = 'Z+X';
              }
            } else {
              // Switch was initiated on controller or in GEARBOX: map to Gearbox Z+X
              state.mode = 'GEARBOX';
              state.gearboxMode = 'SYNC';
              state.activeFeedAxis = 'Z+X';
            }
          } else if(modeKey === 'TURN'){
            state.mode = 'TURNING';
          } else if(modeKey === 'FACE'){
            state.mode = 'FACING';
          } else if(modeKey === 'THREAD'){
            state.mode = 'THREADING';
          } else if(modeKey === 'CUT'){
            state.mode = 'CUTOFF';
          } else if(modeKey === 'SLOT'){
            state.mode = 'SLOTTING';
          } else if(modeKey === 'ELLIP'){
            state.mode = 'SPHERE';
          } else if(modeKey === 'JOY'){
            state.mode = 'JOYSTICK';
          }
        }

        // 6. Pitch / Feed Synchronization
        if(data.pitch !== undefined && data.pitch !== ''){
          var pVal = parseFloat(data.pitch);
          if(!isNaN(pVal) && pVal > 0){
            var isTpi = (data.measure && data.measure.toUpperCase() === 'TPI');
            if(state.mode === 'THREADING'){
              if(isTpi){
                state.pitch = +(25.4 / pVal).toFixed(4);
                state.threadUnit = 'tpi';
              } else {
                state.pitch = (state.unit === 'in') ? pVal * 25.4 : pVal;
              }
            } else if(state.mode === 'TAPER' && !state.taperCycleActive){
              // Trigger B: External feed adjustment on CONE while idle in TAPER setup -> yield to GEARBOX Z+X
              var currentPitch = (state.unit === 'in') ? state.gearPitchZ / 25.4 : state.gearPitchZ;
              if(Math.abs(pVal - currentPitch) > 0.0001){
                state.mode = 'GEARBOX';
                state.gearboxMode = 'SYNC';
                state.activeFeedAxis = 'Z+X';
                state.gearPitchZ = (state.unit === 'in') ? pVal * 25.4 : pVal;
              }
            } else if(state.mode === 'GEARBOX'){
              if(state.activeFeedAxis === 'X'){
                state.gearPitchX = (state.unit === 'in') ? pVal * 25.4 : pVal;
              } else {
                if(isTpi){
                  state.gearPitchZ = +(25.4 / pVal).toFixed(4);
                  state.gearThreadUnit = 'tpi';
                  state.gearFeedModeZ = 'PITCH';
                } else {
                  state.gearPitchZ = (state.unit === 'in') ? pVal * 25.4 : pVal;
                }
              }
            } else if(state.mode === 'TURNING'){
              state.turnPitch = (state.unit === 'in') ? pVal * 25.4 : pVal;
            } else if(state.mode === 'FACING'){
              state.facePitch = (state.unit === 'in') ? pVal * 25.4 : pVal;
            }
          }
        }

        // 7. Status Message
        if(data.message !== undefined && data.message !== ''){
          setStatusMsg(data.message);
        }

        render();
      });

      // Connect initial
      window.nanoelsClient.connect();
    }

    // Dev Console Button Actions
    if(btnEmulator){
      btnEmulator.addEventListener('click', function(){
        savedBackend = 'emulator';
        localStorage.setItem('nanoels_backend', savedBackend);
        updatePills(savedBackend);
        if(window.nanoelsClient){
          window.nanoelsClient.setBackend('emulator');
        }
      });
    }

    if(btnLive){
      btnLive.addEventListener('click', function(){
        savedBackend = 'live';
        localStorage.setItem('nanoels_backend', savedBackend);
        updatePills(savedBackend);
        var host = hostInput ? hostInput.value.trim() : 'nanoels.local';
        if(window.nanoelsClient){
          window.nanoelsClient.setBackend('live', host, 81);
        }
      });
    }

    if(btnConnect){
      btnConnect.addEventListener('click', function(){
        var host = hostInput ? hostInput.value.trim() : 'nanoels.local';
        localStorage.setItem('nanoels_host', host);
        savedBackend = 'live';
        localStorage.setItem('nanoels_backend', savedBackend);
        updatePills(savedBackend);
        if(window.nanoelsClient){
          window.nanoelsClient.setBackend('live', host, 81);
        }
      });
    }

    if(btnClear){
      btnClear.addEventListener('click', function(){
        if(logWindow) logWindow.innerHTML = '';
        consecutiveTelemetryCount = 0;
        activeStreamRow = null;
        activeStreamCountBadge = null;
        activeStreamSummary = null;
        activeStreamDrawerContent = null;
        activeStreamTime = null;
      });
    }

    if(chkLogStatus){
      chkLogStatus.addEventListener('change', function(){
        var active = chkLogStatus.checked;
        localStorage.setItem('nanoels_log_status_pushes', active);
        if(window.nanoelsClient){
          window.nanoelsClient.setLogStatusPushes(active);
        }
      });
    }

    function escapeHtml(str){
      if(!str) return '';
      return String(str).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
    }
  }

  // Initial Boot Render
  initDevConsoleAndService();
  updateStatusMsg();
  render();
})();

