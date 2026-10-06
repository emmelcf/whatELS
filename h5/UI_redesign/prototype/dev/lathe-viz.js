/**
 * DEV ONLY - Lathe visualizer (top-down).
 *
 * Self-contained: injects its own CSS + DOM and reads only the shared Alpine
 * 'machine' store (fed by emulator OR real controller telemetry). To remove it
 * from the page, delete the <script> block marked "DEV: lathe visualizer".
 *
 * Extending: push a module onto window.LatheVizModules:
 *   { id, when(profile) -> bool, build(ctx) -> { update(s, dyn) } }
 * ctx = { g, p, geo, el, zToX(mm), xToY(mm, isDia) }  (all SVG units are mm)
 */
(function () {
  const NS = 'http://www.w3.org/2000/svg';
  const SENT = 2147483640;
  const LS_KEY = 'latheVizProfile.v2';

  function el(tag, attrs, parent) {
    const e = document.createElementNS(NS, tag);
    for (const k in (attrs || {})) e.setAttribute(k, attrs[k]);
    if (parent) parent.appendChild(e);
    return e;
  }
  const isSet = (v) => v !== undefined && v !== null && isFinite(v) && Math.abs(v) < SENT;

  // ---------------------------------------------------------------- modules
  const Modules = (window.LatheVizModules = window.LatheVizModules || []);

  Modules.push({
    id: 'bed',
    when: () => true,
    build({ g, p, geo, el }) {
      el('rect', { x: 0, y: -geo.bedHalfW, width: p.bedLengthMm, height: geo.bedHalfW * 2, class: 'lv-bed' }, g);
      el('line', { x1: 0, x2: p.bedLengthMm, y1: -geo.bedHalfW + 10, y2: -geo.bedHalfW + 10, class: 'lv-way' }, g);
      el('line', { x1: 0, x2: p.bedLengthMm, y1: geo.bedHalfW - 10, y2: geo.bedHalfW - 10, class: 'lv-way' }, g);
      // swing extents + centreline
      el('line', { x1: geo.chuckFaceX, x2: p.bedLengthMm, y1: -p.swingMm / 2, y2: -p.swingMm / 2, class: 'lv-swing' }, g);
      el('line', { x1: 0, x2: p.bedLengthMm, y1: 0, y2: 0, class: 'lv-center' }, g);
      return { update() {} };
    }
  });

  Modules.push({
    id: 'headstock',
    when: () => true,
    build({ g, p, geo, el }) {
      const hh = p.swingMm * 0.4;
      el('rect', { x: 0, y: -hh, width: p.headstockLenMm, height: hh * 2, class: 'lv-head' }, g);
      el('rect', { x: p.headstockLenMm, y: -p.chuckDiaMm / 2, width: p.chuckLenMm, height: p.chuckDiaMm, class: 'lv-chuck' }, g);
      // stock (reference only)
      const stockLen = p.zZeroFromChuckMm + 40;
      el('rect', { x: geo.chuckFaceX, y: -p.stockDiaMm / 2, width: stockLen, height: p.stockDiaMm, class: 'lv-stock' }, g);
      // spindle end-on gauge
      const r = Math.min(hh * 0.55, 32), cx = p.headstockLenMm / 2;
      el('circle', { cx, cy: 0, r, class: 'lv-gauge' }, g);
      const needle = el('line', { x1: cx, y1: 0, x2: cx, y2: -r, class: 'lv-needle' }, g);
      el('circle', { cx, cy: 0, r: r + 4, class: 'lv-gauge-hit' }, g); // drag target for hand-turning
      return {
        update(s, dyn) {
          needle.setAttribute('transform', `rotate(${dyn.spinDeg.toFixed(1)} ${cx} 0)`);
        }
      };
    }
  });

  Modules.push({
    id: 'tailstock',
    when: (p) => p.tailstock,
    build({ g, p, geo, el }) {
      const x0 = p.bedLengthMm * 0.86;
      el('rect', { x: x0, y: -p.swingMm * 0.18, width: 60, height: p.swingMm * 0.36, class: 'lv-head' }, g);
      el('rect', { x: x0 - 40, y: -7, width: 40, height: 14, class: 'lv-chuck' }, g);
      return { update() {} };
    }
  });

  Modules.push({
    id: 'limits',
    when: () => true,
    build({ g, p, geo, el, zToX, xToY }) {
      const mk = (cls, txt, vertical) => {
        const grp = el('g', { class: 'lv-limit ' + cls, visibility: 'hidden' }, g);
        const ln = el('line', {}, grp);
        const t = el('text', {}, grp);
        t.textContent = txt;
        return { grp, ln, t, vertical };
      };
      const zl = mk('lv-lz', 'Z L', true), zr = mk('lv-lz', 'Z R', true);
      const xu = mk('lv-lx', 'X ↑', false), xd = mk('lv-lx', 'X ↓', false);
      const setZ = (o, du) => {
        if (!isSet(du)) { o.grp.setAttribute('visibility', 'hidden'); return; }
        const x = zToX(du / 10000);
        o.grp.setAttribute('visibility', 'visible');
        o.ln.setAttribute('x1', x); o.ln.setAttribute('x2', x);
        o.ln.setAttribute('y1', -geo.bedHalfW - 6); o.ln.setAttribute('y2', geo.bedHalfW + 6);
        o.t.setAttribute('x', x + 3); o.t.setAttribute('y', -geo.bedHalfW - 9);
      };
      const setX = (o, du, dia) => {
        if (!isSet(du)) { o.grp.setAttribute('visibility', 'hidden'); return; }
        const y = xToY(du / 10000, dia);
        o.grp.setAttribute('visibility', 'visible');
        o.ln.setAttribute('x1', geo.chuckFaceX); o.ln.setAttribute('x2', p.bedLengthMm);
        o.ln.setAttribute('y1', y); o.ln.setAttribute('y2', y);
        o.t.setAttribute('x', p.bedLengthMm - 22); o.t.setAttribute('y', y - 3);
      };
      return {
        update(s) {
          setZ(zl, s.zLeftDu); setZ(zr, s.zRightDu);
          setX(xu, s.xUpDu, s.dia); setX(xd, s.xDownDu, s.dia);
        }
      };
    }
  });

  // Carriage + cross slide + tool (+ optional X motor / handwheels mounted on the apron)
  Modules.push({
    id: 'carriage',
    when: () => true,
    build({ g, p, geo, el, zToX, xToY }) {
      const car = el('g', {}, g);
      const sy0 = p.swingMm * 0.28, sy1 = p.swingMm * 0.55;
      el('rect', { x: -40, y: sy0, width: 80, height: sy1 - sy0, class: 'lv-saddle' }, car);
      const slide = el('g', {}, car);
      el('rect', { x: -22, y: 8, width: 44, height: 60, class: 'lv-slide' }, slide);
      el('polygon', { points: '0,0 -6,12 6,12', class: 'lv-tool' }, slide);
      const parts = [];

      const mpg = (cx, cy, label, axis) => {
        const w = el('g', { class: 'lv-mpg' }, car);
        el('circle', { cx, cy, r: 20, class: 'lv-wheel' }, w);
        const spokes = el('g', {}, w);
        for (let i = 0; i < 4; i++) el('line', { x1: cx, y1: cy, x2: cx + 18 * Math.cos(i * Math.PI / 2), y2: cy + 18 * Math.sin(i * Math.PI / 2), class: 'lv-spoke' }, spokes);
        el('circle', { cx: cx + 15, cy, r: 3, class: 'lv-knob' }, spokes);
        const t = el('text', { x: cx, y: cy + 33, class: 'lv-label' }, w);
        t.textContent = label;
        return { w, spokes, cx, cy, axis };
      };

      if (p.x.motor) {
        el('rect', { x: -15, y: sy1, width: 30, height: 26, class: 'lv-motor' }, car);
        const t = el('text', { x: 0, y: sy1 + 18, class: 'lv-label lv-dark' }, car);
        t.textContent = 'X';
      }
      if (p.z.mpg) parts.push(mpg(-52, sy1 + 36, 'Z MPG', 'z'));
      if (p.x.mpg) parts.push(mpg(52, sy1 + 36, 'X MPG', 'x'));

      return {
        update(s, dyn) {
          const tx = zToX(s.zDu / 10000);
          const ty = xToY(s.xDu / 10000, s.dia);
          car.setAttribute('transform', `translate(${tx.toFixed(2)} 0)`);
          slide.setAttribute('transform', `translate(0 ${ty.toFixed(2)})`);
          for (const m of parts) {
            const v = m.axis === 'z' ? dyn.zAcc : dyn.xAcc;
            m.spokes.setAttribute('transform', `rotate(${(v * 360 / 2).toFixed(1)} ${m.cx} ${m.cy})`);
            m.w.classList.toggle('lv-active', Math.abs(m.axis === 'z' ? dyn.vz : dyn.vx) > 0.5);
          }
        }
      };
    }
  });

  // Z lead screw + motor at the tail end of the bed
  Modules.push({
    id: 'zMotor',
    when: (p) => p.z.motor,
    build({ g, p, geo, el }) {
      const y = geo.bedHalfW - 10;
      el('line', { x1: p.headstockLenMm + 20, x2: p.bedLengthMm + 8, y1: y, y2: y, class: 'lv-screw' }, g);
      el('rect', { x: p.bedLengthMm + 8, y: y - 14, width: 40, height: 28, class: 'lv-motor' }, g);
      const t = el('text', { x: p.bedLengthMm + 28, y: y + 5, class: 'lv-label lv-dark' }, g);
      t.textContent = 'Z';
      return { update() {} };
    }
  });

  // Joystick (HUD, bottom-left): knob deflects with tool velocity
  Modules.push({
    id: 'joystick',
    when: (p) => p.joystick,
    build({ g, p, geo, el }) {
      const cx = geo.vb.x + 55, cy = geo.vb.y + geo.vb.h - 55;
      el('circle', { cx, cy, r: 32, class: 'lv-gauge' }, g);
      el('line', { x1: cx - 32, x2: cx + 32, y1: cy, y2: cy, class: 'lv-center' }, g);
      el('line', { x1: cx, x2: cx, y1: cy - 32, y2: cy + 32, class: 'lv-center' }, g);
      const knob = el('circle', { cx, cy, r: 9, class: 'lv-knob' }, g);
      const t = el('text', { x: cx, y: cy + 46, class: 'lv-label' }, g);
      t.textContent = 'JOYSTICK';
      return {
        update(s, dyn) {
          const k = (v) => Math.max(-1, Math.min(1, v / 40)) * 22;
          knob.setAttribute('cx', cx + k(dyn.sz)); knob.setAttribute('cy', cy + k(dyn.sx));
        }
      };
    }
  });

  // ---------------------------------------------------------------- panel
  const CSS = `
  .lv-panel{position:fixed;right:12px;bottom:12px;width:640px;min-width:320px;max-width:96vw;z-index:99999;
    background:rgba(14,18,24,.96);border:1px solid #34404f;border-radius:10px;color:#cfd8e3;
    font:12px/1.3 system-ui,Segoe UI,sans-serif;box-shadow:0 8px 30px rgba(0,0,0,.5);resize:horizontal;overflow:hidden}
  .lv-panel.lv-docked{position:static;right:auto;bottom:auto;width:800px;max-width:95vw;flex:0 0 auto;
    margin-top:22px;resize:none;box-shadow:0 12px 32px rgba(0,0,0,.5);box-sizing:border-box}
  .lv-hd{display:flex;gap:8px;align-items:center;padding:6px 10px;background:#1b222c;cursor:default}
  .lv-hd b{letter-spacing:.08em;font-size:11px;color:#8fb4ff}
  .lv-hd select,.lv-hd button,.lv-set input[type=number]{background:#0e1218;color:#cfd8e3;border:1px solid #3a4658;border-radius:4px;padding:2px 6px;font:inherit}
  .lv-hd button{cursor:pointer}.lv-sp{flex:1}
  .lv-body.lv-hidden{display:none}
  .lv-svg{width:100%;display:block;background:#0b0f14}
  .lv-ro{display:flex;flex-wrap:wrap;gap:4px 14px;padding:6px 10px;font-variant-numeric:tabular-nums;border-top:1px solid #222b37}
  .lv-ro span b{color:#fff;font-weight:600}.lv-ro .lv-warn b{color:#ffb347}
  .lv-set{padding:6px 10px 10px;border-top:1px solid #222b37;display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:4px 10px}
  .lv-set label{display:flex;align-items:center;gap:6px;justify-content:space-between}
  .lv-set input[type=number]{width:62px}
  .lv-set h4{grid-column:1/-1;margin:6px 0 0;font-size:11px;color:#8fb4ff;letter-spacing:.06em}
  .lv-bed{fill:#222a35;stroke:#3a4658}.lv-way{stroke:#3a4658;stroke-width:1.5}
  .lv-swing{stroke:#46546a;stroke-dasharray:6 5;stroke-width:1}
  .lv-center{stroke:#5a6b84;stroke-dasharray:10 4 2 4;stroke-width:1}
  .lv-head{fill:#2c3644;stroke:#4a586d}.lv-chuck{fill:#566478;stroke:#7d8da3}
  .lv-stock{fill:rgba(200,210,225,.10);stroke:#8c9bb0;stroke-dasharray:4 3}
  .lv-gauge{fill:#10151c;stroke:#6b7c94}.lv-needle{stroke:#ff6b6b;stroke-width:2.5;stroke-linecap:round}
  .lv-gauge-hit{fill:transparent;cursor:grab;pointer-events:all}.lv-gauge-hit:active{cursor:grabbing}
  .lv-spin{display:flex;flex-wrap:wrap;align-items:center;gap:6px 10px;padding:6px 10px;border-top:1px solid #222b37;background:#10151c}
  .lv-spin button,.lv-spin input[type=range]{font:inherit}
  .lv-spin button{background:#0e1218;color:#cfd8e3;border:1px solid #3a4658;border-radius:4px;padding:2px 8px;cursor:pointer}
  .lv-spin button.lv-on{background:#1f6f3f;border-color:#2ecc71;color:#fff}
  .lv-spin .lv-ovr{display:flex;align-items:center;gap:6px;font-weight:700;color:#ffb347}
  .lv-spin .lv-ctl{display:flex;align-items:center;gap:6px}
  .lv-spin.lv-off .lv-ctl{opacity:.35;pointer-events:none}
  .lv-badge{font-weight:700;font-size:11px;padding:1px 8px;border-radius:10px;background:#3b1d1d;color:#ff7b7b;display:none}
  .lv-spin:not(.lv-off) .lv-badge{display:inline-block}
  .lv-badge.lv-live{background:#5a1010;color:#fff;animation:lvpulse 1.2s infinite}
  @keyframes lvpulse{50%{opacity:.55}}
  .lv-saddle{fill:#3a4a63;stroke:#6483ad}.lv-slide{fill:#4a6285;stroke:#87a8d6}
  .lv-tool{fill:#ffd24a;stroke:#fff3b0}
  .lv-motor{fill:#7a3fbf;stroke:#b98cf0}.lv-screw{stroke:#8d7aa8;stroke-width:3;stroke-dasharray:3 2}
  .lv-wheel{fill:#1d2530;stroke:#7d8da3;stroke-width:2}.lv-spoke{stroke:#7d8da3;stroke-width:2}
  .lv-knob{fill:#4ad1ff}.lv-active .lv-wheel{stroke:#4ad1ff}
  .lv-label{fill:#9fb0c6;font-size:11px;text-anchor:middle}.lv-label.lv-dark{fill:#fff;font-weight:700}
  .lv-limit line{stroke-width:1.8}.lv-limit text{font-size:11px;font-weight:700}
  .lv-lz line{stroke:#ff9f43}.lv-lz text{fill:#ff9f43}
  .lv-lx line{stroke:#2ecc9a}.lv-lx text{fill:#2ecc9a}
  `;

  function injectCss() {
    const s = document.createElement('style');
    s.textContent = CSS;
    document.head.appendChild(s);
  }

  function clone(o) { return JSON.parse(JSON.stringify(o)); }

  function loadProfile() {
    try {
      const raw = JSON.parse(localStorage.getItem(LS_KEY));
      if (raw && raw.profile) return raw;
    } catch (e) { /* ignore */ }
    return { idx: 0, profile: clone(window.LatheVizProfiles[0]) };
  }

  function init() {
    injectCss();
    let saved = loadProfile();
    const root = document.createElement('div');
    root.className = 'lv-panel';
    root.id = 'latheVizPanel';
    root.innerHTML = `
      <div class="lv-hd"><b>LATHE VIZ</b>
        <select id="lvProfile">${window.LatheVizProfiles.map((p, i) => `<option value="${i}">${p.name}</option>`).join('')}</select>
        <span class="lv-sp"></span>
        <button id="lvCfg" title="Edit lathe properties">⚙</button>
        <button id="lvToggle" title="Collapse">–</button></div>
      <div class="lv-body" id="lvBody">
        <svg class="lv-svg" id="lvSvg" xmlns="${NS}"></svg>
        <div class="lv-spin lv-off" id="lvSpin">
          <label class="lv-ovr" title="Replace the real spindle encoder input with this simulation"><input type="checkbox" id="lvOvr"> Override spindle encoder</label>
          <span class="lv-badge" id="lvBadge"></span>
          <span id="lvNote" style="color:#ffb347;display:none">Connected firmware doesn't support spindle sim (update firmware)</span>
          <span class="lv-ctl">
            <button id="lvMotor" title="Spindle motor on/off">MOTOR OFF</button>
            <button id="lvDir" title="Spindle direction">CW</button>
            <input type="range" id="lvRpm" min="0" max="2000" step="10" value="300"><b id="lvRpmVal" style="min-width:62px">300 rpm</b>
          </span>
          <span class="lv-ctl"><span>Turn by hand:</span>
            <button data-turn="-90">−90°</button><button data-turn="-10">−10°</button><button data-turn="-1">−1°</button>
            <button data-turn="1">+1°</button><button data-turn="10">+10°</button><button data-turn="90">+90°</button><button data-turn="360">+1 rev</button>
            <span style="color:#7d8da3">or drag the chuck gauge</span>
          </span>
        </div>
        <div class="lv-ro" id="lvRo"></div>
        <div class="lv-set lv-hidden" id="lvSet"></div>
      </div>`;
    // Default: docked in page flow between the control UI and the dev console.
    const devConsole = document.getElementById('devConsole');
    if (devConsole && devConsole.parentNode) {
      root.classList.add('lv-docked');
      devConsole.parentNode.insertBefore(root, devConsole);
    } else {
      document.body.appendChild(root);
    }
    const $ = (id) => root.querySelector('#' + id);
    $('lvProfile').value = saved.idx;

    let layers = [];
    let geo, profile;

    function rebuild() {
      profile = saved.profile;
      const p = profile;
      geo = {
        bedHalfW: p.swingMm * 0.45,
        chuckFaceX: p.headstockLenMm + p.chuckLenMm
      };
      geo.originX = geo.chuckFaceX + p.zZeroFromChuckMm;
      geo.vb = { x: -20, y: -p.swingMm / 2 - 24, w: p.bedLengthMm + 90, h: p.swingMm * 1.05 + 150 };
      const svg = $('lvSvg');
      svg.innerHTML = '';
      svg.setAttribute('viewBox', `${geo.vb.x} ${geo.vb.y} ${geo.vb.w} ${geo.vb.h}`);
      const zToX = (mm) => geo.originX + (p.zPositive === 'left' ? -mm : mm);
      const xToY = (mm, dia) => {
        const r = dia ? mm / 2 : mm;
        return p.xZeroRadiusMm + (p.xPositive === 'out' ? r : -r);
      };
      layers = [];
      for (const m of Modules) {
        if (!m.when(p)) continue;
        const g = el('g', { 'data-mod': m.id }, svg);
        layers.push(m.build({ g, p, geo, el, zToX, xToY }));
      }
      renderSettings();
    }

    function save() { localStorage.setItem(LS_KEY, JSON.stringify(saved)); }

    function renderSettings() {
      const p = profile;
      const num = (path, label) => `<label>${label}<input type="number" data-path="${path}" value="${path.split('.').reduce((o, k) => o[k], p)}"></label>`;
      const chk = (path, label) => `<label>${label}<input type="checkbox" data-path="${path}" ${path.split('.').reduce((o, k) => o[k], p) ? 'checked' : ''}></label>`;
      const sel = (path, label, opts) => `<label>${label}<select data-path="${path}">${opts.map((o) => `<option ${path.split('.').reduce((a, k) => a[k], p) === o ? 'selected' : ''}>${o}</option>`).join('')}</select></label>`;
      $('lvSet').innerHTML = `
        <h4>DIMENSIONS (mm)</h4>
        ${num('bedLengthMm', 'Bed')}${num('swingMm', 'Swing')}${num('zTravelMm', 'Z travel')}${num('xTravelMm', 'X travel')}
        ${num('chuckDiaMm', 'Chuck ø')}${num('stockDiaMm', 'Stock ø')}${num('zZeroFromChuckMm', 'Z0 from chuck')}${num('xZeroRadiusMm', 'X0 radius')}
        <h4>HARDWARE</h4>
        ${chk('z.motor', 'Z motor')}${chk('x.motor', 'X motor')}${chk('z.mpg', 'Z MPG')}${chk('x.mpg', 'X MPG')}
        ${chk('joystick', 'Joystick')}${chk('tailstock', 'Tailstock')}
        <h4>AXIS DIRECTION</h4>
        ${sel('zPositive', 'Z+ is', ['left', 'right'])}${sel('xPositive', 'X+ is', ['out', 'in'])}`;
    }

    $('lvSet').addEventListener('change', (e) => {
      const t = e.target, path = t.dataset.path;
      if (!path) return;
      const keys = path.split('.');
      let o = profile;
      while (keys.length > 1) o = o[keys.shift()];
      o[keys[0]] = t.type === 'checkbox' ? t.checked : t.type === 'number' ? parseFloat(t.value) || 0 : t.value;
      save(); rebuild();
    });
    $('lvProfile').addEventListener('change', (e) => {
      saved = { idx: +e.target.value, profile: clone(window.LatheVizProfiles[+e.target.value]) };
      save(); rebuild();
    });
    $('lvCfg').addEventListener('click', () => $('lvSet').classList.toggle('lv-hidden'));
    let collapsed = false;
    $('lvToggle').addEventListener('click', () => {
      collapsed = !collapsed;
      $('lvBody').classList.toggle('lv-hidden', collapsed);
      $('lvToggle').textContent = collapsed ? '+' : '–';
    });

    rebuild();

    // -------------------------------------------------- spindle encoder simulation
    // Sends CMD.spindleSim / CMD.spindleSimTurn through the shared client, so the
    // emulator and the real controller are driven identically.
    const spin = { active: false, motorOn: false, cw: true, rpm: 300 };
    const signedRpm = () => (spin.active && spin.motorOn ? (spin.cw ? 1 : -1) * spin.rpm : 0);
    const qsend = (cmd) => {
      const c = window.nanoelsClient;
      if (c && typeof c.sendRaw === 'function') c.sendRaw(cmd + '\n', '', true); // quiet: keep it out of the TX log
    };
    const supported = () => {
      const c = window.nanoelsClient;
      return !!(c && c.state && Array.isArray(c.state.features) && c.state.features.includes('spindleSim'));
    };
    const pushSim = () => { if (supported() || !spin.active) { if (supported()) qsend(`CMD.spindleSim=${spin.active ? 1 : 0},${signedRpm()}`); } };
    function refreshSpinUi() {
      const ok = supported();
      if (!ok && spin.active) { spin.active = false; spin.motorOn = false; turnQueue = 0; $('lvOvr').checked = false; }
      $('lvOvr').disabled = !ok;
      $('lvNote').style.display = ok ? 'none' : 'inline';
      $('lvSpin').classList.toggle('lv-off', !spin.active);
      $('lvMotor').textContent = spin.motorOn ? 'MOTOR ON' : 'MOTOR OFF';
      $('lvMotor').classList.toggle('lv-on', spin.motorOn);
      $('lvDir').textContent = spin.cw ? 'CW' : 'CCW';
      $('lvRpmVal').textContent = spin.rpm + ' rpm';
      const s = window.Alpine && Alpine.store && Alpine.store('machine');
      const live = s && s.backend === 'live';
      const b = $('lvBadge');
      b.textContent = live ? 'OVERRIDE ACTIVE - REAL CONTROLLER' : 'OVERRIDE ACTIVE';
      b.classList.toggle('lv-live', !!live);
    }
    $('lvOvr').addEventListener('change', (e) => {
      spin.active = e.target.checked;
      if (!spin.active) { spin.motorOn = false; turnQueue = 0; }
      pushSim(); refreshSpinUi();
    });
    $('lvMotor').addEventListener('click', () => { spin.motorOn = !spin.motorOn; pushSim(); refreshSpinUi(); });
    $('lvDir').addEventListener('click', () => { spin.cw = !spin.cw; pushSim(); refreshSpinUi(); });
    $('lvRpm').addEventListener('input', (e) => { spin.rpm = +e.target.value; pushSim(); refreshSpinUi(); });
    // Heartbeat: firmware/emulator fall back to the real encoder if this stops (2 s watchdog).
    setInterval(() => { if (spin.active) pushSim(); refreshSpinUi(); }, 500);
    window.addEventListener('beforeunload', () => { if (spin.active) { spin.active = false; pushSim(); } });

    // Hand rotation: buttons feed a queue at <=180 deg/s so the steppers can keep up.
    let turnQueue = 0;
    root.querySelectorAll('[data-turn]').forEach((b) => b.addEventListener('click', () => { if (spin.active) turnQueue += +b.dataset.turn; }));
    setInterval(() => {
      if (!spin.active || !turnQueue) return;
      const step = Math.max(-4.5, Math.min(4.5, turnQueue));
      turnQueue -= step;
      qsend(`CMD.spindleSimTurn=${step.toFixed(2)}`);
    }, 25);

    // Hand rotation: drag the chuck gauge. Needle follows the pointer immediately.
    let dragging = false, dragDeg = 0, lastPtr = 0, dragAcc = 0, lastSend = 0;
    const ptrDeg = (e) => {
      const svg = $('lvSvg'), r = svg.getBoundingClientRect();
      const k = r.width / geo.vb.w;
      const cx = r.left + (profile.headstockLenMm / 2 - geo.vb.x) * k;
      const cy = r.top + (0 - geo.vb.y) * k;
      return Math.atan2(e.clientY - cy, e.clientX - cx) * 180 / Math.PI;
    };
    $('lvSvg').addEventListener('pointerdown', (e) => {
      if (!spin.active || !e.target.classList || !e.target.classList.contains('lv-gauge-hit')) return;
      dragging = true; lastPtr = ptrDeg(e); dragAcc = 0;
      const s = window.Alpine && Alpine.store('machine');
      dragDeg = dyn.spinDeg; lastSend = performance.now();
      e.target.setPointerCapture(e.pointerId);
      e.preventDefault();
    });
    $('lvSvg').addEventListener('pointermove', (e) => {
      if (!dragging) return;
      const a = ptrDeg(e);
      let d = a - lastPtr;
      while (d > 180) d -= 360;
      while (d <= -180) d += 360;
      lastPtr = a;
      dragDeg = ((dragDeg + d) % 360 + 360) % 360;
      dragAcc += d;
      const now = performance.now();
      if (now - lastSend >= 25 && Math.abs(dragAcc) >= 0.05) {
        // clamp so a wild flick can't command an absurd instantaneous speed
        const send = Math.max(-60, Math.min(60, dragAcc));
        qsend(`CMD.spindleSimTurn=${send.toFixed(2)}`);
        dragAcc -= send; lastSend = now;
      }
    });
    const endDrag = () => { if (dragging && dragAcc) qsend(`CMD.spindleSimTurn=${Math.max(-60, Math.min(60, dragAcc)).toFixed(2)}`); dragging = false; dragAcc = 0; };
    $('lvSvg').addEventListener('pointerup', endDrag);
    $('lvSvg').addEventListener('pointercancel', endDrag);
    refreshSpinUi();

    // -------------------------------------------------- render loop
    const dyn = { vz: 0, vx: 0, sz: 0, sx: 0, zAcc: 0, xAcc: 0, spinDeg: 0 };
    let last = performance.now(), pz = null, px = null;
    const ro = $('lvRo');

    function frame(now) {
      requestAnimationFrame(frame);
      const s = window.Alpine && Alpine.store && Alpine.store('machine');
      if (!s || collapsed) { last = now; return; }
      const dt = Math.max(0.001, (now - last) / 1000);
      last = now;
      const zMm = s.zDu / 10000, xMm = s.xDu / 10000;
      if (pz === null) { pz = zMm; px = xMm; }
      const dz = zMm - pz, dx = xMm - px;
      pz = zMm; px = xMm;
      dyn.zAcc += dz; dyn.xAcc += dx;
      // smoothed velocities (mm/s) in axis units and in screen direction
      const a = Math.min(1, dt * 12);
      dyn.vz += (dz / dt - dyn.vz) * a;
      dyn.vx += (dx / dt - dyn.vx) * a;
      dyn.sz = dyn.vz * (profile.zPositive === 'left' ? -1 : 1);
      dyn.sx = dyn.vx * (profile.xPositive === 'out' ? 1 : -1) * (s.dia ? 0.5 : 1);
      // Spindle needle: integrate visually while the spindle is turning, otherwise show the
      // controller-reported position (so hand-turn / drag is a closed loop).
      const simMotor = spin.active && spin.motorOn && spin.rpm > 0;
      const realMotor = !spin.active && s.isOn && s.rpm > 0;
      if (dragging) {
        dyn.spinDeg = dragDeg;
      } else if (simMotor || realMotor) {
        const vr = simMotor ? signedRpm() : s.rpm;
        dyn.spinDeg = (((dyn.spinDeg + vr * 0.6 * dt) % 360) + 360) % 360;
      } else if (isFinite(s.turns) && s.turns !== undefined) {
        dyn.spinDeg = (((s.turns % 1) + 1) % 1) * 360;
      } else {
        dyn.spinDeg = s.angle || dyn.spinDeg;
      }

      for (const l of layers) l.update(s, dyn);

      const lim = (label, v) => `<span><i>${label}</i> <b>${v}</b></span>`;
      ro.innerHTML =
        `<span><i>Z</i> <b>${s.zDisplay}</b></span><span><i>X</i> <b>${s.xDisplay}${s.dia ? ' ø' : ' r'}</b></span>` +
        `<span><i>Mode</i> <b>${s.modeName}</b></span><span><i>Spindle</i> <b>${s.spindleDisplay} ${s.spindleUnit}</b></span>` +
        lim('Z-L', s.zLeftDisplay) + lim('Z-R', s.zRightDisplay) + lim('X↑', s.xUpDisplay) + lim('X↓', s.xDownDisplay) +
        `<span class="${s.connected ? '' : 'lv-warn'}"><i>Link</i> <b>${s.backend}${s.connected ? '' : ' (offline)'}</b></span>`;
    }
    requestAnimationFrame(frame);
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
})();
