/**
 * Pure Render-Time Formatters for whatELS
 * Converts canonical integer deci-microns (du) and authoring setpoints into display strings.
 * Zero DOM dependencies, zero side effects, zero floating-point accumulation drift.
 */
(function(window) {
  'use strict';

  const Formatters = {
    // Coordinate display (fixed decimals, handles mm vs inch)
    formatAxisDu(du, unit = 'mm', isDiameter = false) {
      if (du === undefined || du === null || isNaN(du)) return '0.000';
      const factor = isDiameter ? 2.0 : 1.0;
      if (unit === 'in') {
        const inches = (du * factor) / 254000.0;
        return inches.toFixed(4);
      }
      const mm = (du * factor) / 10000.0;
      return mm.toFixed(3);
    },

    // Limit distance-to-boundary countdown (always positive distance remaining)
    formatLimitDistance(carriageDu, limitDu, unit = 'mm', isDiameter = false) {
      if (limitDu === undefined || limitDu === null || limitDu === '' || isNaN(limitDu)) {
        return null;
      }
      const numLimit = Number(limitDu);
      if (isNaN(numLimit) || numLimit >= 2147483640 || numLimit <= -2147483640) {
        return null;
      }
      const cDu = Number(carriageDu);
      if (isNaN(cDu)) return null;
      const distDu = Math.abs(cDu - numLimit);
      const factor = isDiameter ? 2.0 : 1.0;
      const divisor = unit === 'in' ? 254000.0 : 10000.0;
      const dist = (distDu * factor) / divisor;
      const decimals = unit === 'in' ? 4 : 3;
      return dist.toFixed(decimals);
    },

    // Authoritative Pitch/Lead format - Lossless Intent
    formatPitch(thPitch, thFmt, unit = 'mm') {
      if (thPitch === undefined || thPitch === null || isNaN(thPitch) || thPitch <= 0) {
        return unit === 'in' ? '20 TPI' : '1.500 mm';
      }
      if (thFmt === 1) { // Authoritative TPI (e.g. 13 TPI, 11.5 TPI)
        const v = Number(thPitch);
        const tpiStr = (v % 1 === 0) ? v.toFixed(0) : (v * 10 % 1 === 0 ? v.toFixed(1) : v.toFixed(2));
        return `${tpiStr} TPI`;
      }
      if (thFmt === 2) { // Imperial Inch Pitch (e.g. 0.0500 in)
        return `${Number(thPitch).toFixed(4)} in`;
      }
      // Metric Pitch (mm)
      const v = Number(thPitch);
      const mmStr = (v % 0.01 === 0) ? v.toFixed(2) : v.toFixed(3);
      return `${mmStr} mm`;
    },

    // Authoritative Taper format (Preset, 1:K Ratio, or Angle in degrees) - Lossless Intent
    formatTaper(tpVal, tpMode, coneRatio, tpPreset = -1) {
      // 1. If it's a known preset, display preset name losslessly
      if (tpPreset !== undefined && tpPreset !== null && tpPreset >= 0) {
        if (typeof window !== 'undefined' && window.TAPER_PRESETS && window.TAPER_PRESETS[tpPreset]) {
          return window.TAPER_PRESETS[tpPreset].name;
        }
      }
      // 2. Authoritative Angle (tpMode === 1)
      if (tpMode === 1 && tpVal > 0) {
        const v = Number(tpVal);
        const aStr = (v % 1 === 0) ? v.toFixed(0) : (v * 10 % 1 === 0 ? v.toFixed(1) : v.toFixed(2));
        return `${aStr}°`;
      }
      // 3. Authoritative Ratio 1:K (tpMode === 0)
      if (tpVal > 0) {
        const v = Number(tpVal);
        const kStr = (v % 1 === 0) ? v.toFixed(0) : (v * 10 % 1 === 0 ? v.toFixed(1) : (v * 100 % 1 === 0 ? v.toFixed(2) : v.toFixed(3)));
        return `1 : ${kStr}`;
      }
      // 4. Fallback from Kinematic Cone Ratio
      if (coneRatio > 0) {
        const k = 1.0 / coneRatio;
        const kStr = (k % 1 === 0) ? k.toFixed(0) : (k >= 10 ? k.toFixed(2) : k.toFixed(3));
        return `1 : ${kStr}`;
      }
      return '1 : --';
    },

    // Feed / Rapid formatting (synced vs async, metric vs imperial)
    formatFeed(dupr, unit = 'mm', isAsync = false, asyncSpeed = 150) {
      if (isAsync) {
        if (unit === 'in') {
          const inMin = Number(asyncSpeed || 0) / 25.4;
          return { val: inMin.toFixed(1), unit: 'in/min', num: inMin };
        }
        const mmMin = Math.round(Number(asyncSpeed || 0));
        return { val: mmMin.toString(), unit: 'mm/min', num: mmMin };
      }
      // Synchronized feed (dupr = deci-microns per rev: 10,000 du = 1.0 mm)
      if (unit === 'in') {
        const inches = Number(dupr || 0) / 254000.0;
        return { val: inches.toFixed(4), unit: 'in/rev', num: inches };
      }
      const mm = Number(dupr || 0) / 10000.0;
      return { val: mm.toFixed(3), unit: 'mm/rev', num: mm };
    },

    // Real-time Cutting Surface Speed (SFM in inch, SMM in mm)
    // Derived from Spindle RPM and Workpiece Diameter (X DRO position)
    formatSurfaceSpeed(xDu, rpm, unit = 'mm', isDiameter = false) {
      const r = Math.abs(Number(rpm) || 0);
      const factor = isDiameter ? 1.0 : 2.0; // if not diameter, xDu is radius -> multiply by 2 for dia
      const diaMm = (Math.abs(Number(xDu) || 0) * factor) / 10000.0;

      if (r <= 0 || diaMm <= 0.001) {
        return { val: '0', unit: unit === 'in' ? 'SFM' : 'SMM', num: 0 };
      }

      if (unit === 'in') {
        // SFM = (pi * Dia_in * RPM) / 12 = (Dia_in * RPM) / 3.8197
        const diaIn = diaMm / 25.4;
        const sfm = (Math.PI * diaIn * r) / 12.0;
        return { val: Math.round(sfm).toString(), unit: 'SFM', num: sfm };
      }

      // SMM = (pi * Dia_mm * RPM) / 1000
      const smm = (Math.PI * diaMm * r) / 1000.0;
      return { val: Math.round(smm).toString(), unit: 'SMM', num: smm };
    },

    // Thread Pitch with Companion Readout (e.g. "20 TPI (1.270 mm)" or "1.500 mm (16.93 TPI)")
    formatPitchWithCompanion(thPitch, thFmt, unit = 'mm') {
      const p = Number(thPitch) || (unit === 'in' ? 20.0 : 1.5);
      if (thFmt === 1) { // TPI
        const tpiStr = `${(p % 1 === 0) ? p.toFixed(0) : (p * 10 % 1 === 0 ? p.toFixed(1) : p.toFixed(2))} TPI`;
        const mmLead = p > 0 ? (25.4 / p).toFixed(3) : '0.000';
        return {
          primary: tpiStr,
          companion: `(${mmLead} mm)`,
          full: `${tpiStr} (${mmLead} mm)`
        };
      }
      if (thFmt === 2) { // Imperial Inch Pitch
        const inStr = `${p.toFixed(4)} in`;
        const tpi = p > 0 ? (1.0 / p).toFixed(1) : '0';
        return {
          primary: inStr,
          companion: `(${tpi} TPI)`,
          full: `${inStr} (${tpi} TPI)`
        };
      }
      // Metric Pitch (mm)
      const mmStr = `${(p % 0.01 === 0) ? p.toFixed(2) : p.toFixed(3)} mm`;
      const tpi = p > 0 ? (25.4 / p).toFixed(2) : '0';
      return {
        primary: mmStr,
        companion: `(${tpi} TPI)`,
        full: `${mmStr} (${tpi} TPI)`
      };
    },

    // Multi-format Taper Companions (Ratio 1:K, Half-Angle, Included Angle, Taper Per Foot, mm/m)
    formatTaperCompanions(tpVal, tpMode, coneRatio, tpPreset = -1) {
      let slope = 0.05; // diametral slope Delta Dia / Delta Z
      let presetName = null;

      if (tpPreset !== undefined && tpPreset !== null && tpPreset >= 0 && typeof window !== 'undefined' && window.TAPER_PRESETS && window.TAPER_PRESETS[tpPreset]) {
        presetName = window.TAPER_PRESETS[tpPreset].name;
        slope = window.TAPER_PRESETS[tpPreset].ratio;
      } else if (tpMode === 1 && tpVal > 0) { // Angle (degrees)
        slope = 2.0 * Math.tan((Number(tpVal) * Math.PI) / 180.0);
      } else if (tpVal > 0) {
        slope = 1.0 / Number(tpVal);
      } else if (coneRatio > 0) {
        slope = Number(coneRatio);
      }

      const k = (tpMode === 0 && tpVal > 0) ? Number(tpVal) : (slope > 0 ? (1.0 / slope) : 20.0);
      const halfAngleDeg = (tpMode === 1 && tpVal > 0) ? Number(tpVal) : Math.atan(slope / 2.0) * (180.0 / Math.PI);
      const incAngleDeg = halfAngleDeg * 2.0;
      const tpf = slope * 12.0; // Taper Per Foot in inches
      const mmPerM = slope * 1000.0;

      const kStr = (k % 1 === 0) ? k.toFixed(0) : (k * 10 % 1 === 0 ? k.toFixed(1) : (k >= 10 ? k.toFixed(2) : k.toFixed(3)));
      const angleStr = (halfAngleDeg % 1 === 0) ? halfAngleDeg.toFixed(0) : halfAngleDeg.toFixed(2);

      return {
        preset: presetName,
        ratio: `1 : ${kStr}`,
        angle: `${angleStr}°`,
        incAngle: `${incAngleDeg.toFixed(2)}°`,
        inPerFt: `${tpf.toFixed(4)} in/ft`,
        mmPerM: `${mmPerM.toFixed(1)} mm/m`,
        slope
      };
    },

    // Feed / Step increment formatting
    formatStepDu(stepDu, unit = 'mm') {
      if (!stepDu || isNaN(stepDu)) return unit === 'in' ? '0.0010' : '0.100';
      const divisor = unit === 'in' ? 254000.0 : 10000.0;
      const val = stepDu / divisor;
      const decimals = unit === 'in' ? 4 : 3;
      return val.toFixed(decimals);
    },

    // Spindle RPM
    formatRpm(rpm) {
      return Math.round(Number(rpm) || 0);
    },

    // Spindle Angle: Strictly [0.00°, 359.99°], resets cleanly after 360
    formatAngle(angle) {
      let a = Number(angle) || 0;
      // Strict modulo 360 wrap
      a = ((a % 360) + 360) % 360;
      // Precision rounding near 360.00 boundary (never display 360.00)
      if (a >= 359.995 || a < 0.005) a = 0.0;
      return a.toFixed(2);
    },

    // Spindle Cumulative Turns: Matches Nextion firmware formatting
    // (<100: 2 decimals, <1000: 1 decimal, >=1000: 0 decimals)
    formatTurns(turns) {
      const t = Math.abs(Number(turns) || 0);
      if (t < 100) return t.toFixed(2);
      if (t < 1000) return t.toFixed(1);
      return Math.round(t).toString();
    }
  };

  window.Formatters = Formatters;
})(window);
