/**
 * DEV ONLY - Lathe visualizer profiles.
 * Pure data: describes the physical lathe + which optional hardware is fitted.
 * Add a new simulated setup by appending an entry to LatheVizProfiles.
 *
 * Units: mm. Axis conventions are configurable per profile:
 *   zPositive: 'left' | 'right'   screen direction of increasing Z (headstock is on the left)
 *   xPositive: 'out'  | 'in'      increasing X moves tool away from / toward the spindle centreline
 *   zZeroFromChuckMm              distance from chuck face to tool tip when Z DRO = 0
 *   xZeroRadiusMm                 tool-tip distance from centreline when X DRO = 0
 */
(function () {
  const base = {
    bedLengthMm: 600,
    swingMm: 180,
    zTravelMm: 450,
    xTravelMm: 80,
    headstockLenMm: 110,
    chuckLenMm: 25,
    chuckDiaMm: 100,
    stockDiaMm: 30,
    zPositive: 'right',
    xPositive: 'out',
    zZeroFromChuckMm: 60,
    xZeroRadiusMm: 15,
    tailstock: true,
    z: { motor: true, mpg: false, dro: false },
    x: { motor: true, mpg: false, dro: false },
    joystick: false
  };

  function make(name, over) {
    const p = JSON.parse(JSON.stringify(base));
    p.name = name;
    for (const k in over) {
      if (over[k] && typeof over[k] === 'object' && !Array.isArray(over[k])) {
        Object.assign(p[k], over[k]);
      } else {
        p[k] = over[k];
      }
    }
    return p;
  }

  window.LatheVizProfiles = [
    make('Z + X motors', {}),
    make('Z motor only (manual X)', { x: { motor: false, dro: true } }),
    make('Z + X motors, MPG on both', { z: { mpg: true }, x: { mpg: true } }),
    make('Z motor, MPG on Z only', { x: { motor: false, dro: true }, z: { mpg: true } }),
    make('Joystick', { joystick: true }),
    make('Large lathe', {
      bedLengthMm: 1200, swingMm: 350, zTravelMm: 900, xTravelMm: 160,
      chuckDiaMm: 200, stockDiaMm: 60, xZeroRadiusMm: 30,
      z: { mpg: true }, x: { mpg: true }
    })
  ];
})();
