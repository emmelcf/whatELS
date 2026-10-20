/**
 * Standard Machining Presets for whatELS Lathe Controller
 * Collets, spindles, standard taper pins, and threading standards.
 */
(function(window) {
  'use strict';

  const TAPER_CATEGORIES = [
    { id: 'SPINDLE', label: 'COLLETS & SPINDLES' },
    { id: 'RATIOS', label: 'RATIOS & PINS' },
    { id: 'CENTERS', label: 'CHAMFERS & CENTERS' }
  ];

  const TAPER_PRESETS = [
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

  window.TAPER_CATEGORIES = TAPER_CATEGORIES;
  window.TAPER_PRESETS = TAPER_PRESETS;
})(window);
