/**
 * Pure Render-Time Formatters for NanoELS
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
      if (limitDu === undefined || limitDu === null || limitDu === 2147483647 || limitDu === -2147483648) {
        return null;
      }
      const distDu = Math.abs(carriageDu - limitDu);
      const factor = isDiameter ? 2.0 : 1.0;
      const divisor = unit === 'in' ? 254000.0 : 10000.0;
      const dist = (distDu * factor) / divisor;
      const decimals = unit === 'in' ? 4 : 3;
      return dist.toFixed(decimals);
    },

    // Authoritative Pitch/Lead format
    formatPitch(thPitch, thFmt, unit = 'mm') {
      if (thPitch === undefined || thPitch === null || isNaN(thPitch) || thPitch <= 0) {
        return unit === 'in' ? '0.0500' : '1.500';
      }
      if (thFmt === 1) { // TPI
        return `${Number(thPitch).toFixed(thPitch % 1 === 0 ? 0 : 2)} TPI`;
      }
      if (thFmt === 2) { // Imperial Inch Pitch
        return `${Number(thPitch).toFixed(4)} in`;
      }
      // Metric Pitch (mm)
      return `${Number(thPitch).toFixed(3)} mm`;
    },

    // Authoritative Taper format (1:K or Angle in degrees)
    formatTaper(tpVal, tpMode, coneRatio) {
      if (tpMode === 1) { // Angle
        return `${Number(tpVal).toFixed(2)}°`;
      }
      // Ratio 1:K
      if (tpVal > 0) {
        return `1 : ${Number(tpVal).toFixed(3)}`;
      }
      if (coneRatio > 0) {
        return `1 : ${(1.0 / coneRatio).toFixed(3)}`;
      }
      return '1 : --';
    },

    // Feed / Step increment formatting
    formatStepDu(stepDu, unit = 'mm') {
      if (!stepDu || isNaN(stepDu)) return '0.100';
      const divisor = unit === 'in' ? 254000.0 : 10000.0;
      const val = stepDu / divisor;
      const decimals = unit === 'in' ? 4 : 3;
      return val.toFixed(decimals);
    },

    // Spindle RPM
    formatRpm(rpm) {
      return Math.round(Number(rpm) || 0);
    },

    // Spindle Angle (0..359.9)
    formatAngle(angleTenths) {
      return (Number(angleTenths || 0) / 10.0).toFixed(1);
    },

    // Spindle Cumulative Turns
    formatTurns(turns) {
      return Number(turns || 0).toFixed(1);
    }
  };

  window.Formatters = Formatters;
})(window);
