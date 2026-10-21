/**
 * whatELS — Setup & Configuration Client
 * Speaks canonical line protocol over WebSocket (:81/canonical) and REST HTTP
 */

(function() {
  'use strict';

  // --- State & DOM Elements ---
  const state = {
    ws: null,
    connected: false,
    isOn: false,
    dirtySettings: new Set(),
    settingsValues: {}
  };

  const el = {
    // Header
    wsPill: document.getElementById('wsStatusPill'),
    wsText: document.getElementById('wsStatusText'),
    lathePill: document.getElementById('latheStatusPill'),
    latheText: document.getElementById('latheStatusText'),
    wifiIpBadge: document.getElementById('wifiIpText'),
    heapBadge: document.getElementById('heapText'),
    
    // Tabs
    tabBtns: document.querySelectorAll('.tab-btn'),
    tabPanels: document.querySelectorAll('.tab-panel'),

    // WiFi
    wifiBadge: document.getElementById('wifiStateBadge'),
    statusMode: document.getElementById('statusMode'),
    statusIp: document.getElementById('statusIp'),
    statusSsid: document.getElementById('statusSsid'),
    statusHostname: document.getElementById('statusHostname'),
    btnScanWifi: document.getElementById('btnScanWifi'),
    scanSpinner: document.getElementById('scanSpinner'),
    networkList: document.getElementById('networkList'),
    wifiForm: document.getElementById('wifiForm'),
    wifiEnabled: document.getElementById('inputWifiEnabled'),
    wifiSsid: document.getElementById('inputWifiSsid'),
    wifiPassword: document.getElementById('inputWifiPassword'),
    wifiHostname: document.getElementById('inputWifiHostname'),
    btnTogglePwd: document.getElementById('btnTogglePwd'),
    btnResetWifi: document.getElementById('btnResetWifi'),

    // Settings
    dirtyBadge: document.getElementById('dirtyBadge'),
    btnReloadSettings: document.getElementById('btnReloadSettings'),
    btnSaveAllSettings: document.getElementById('btnSaveAllSettings'),
    settingItems: document.querySelectorAll('.setting-item'),

    // Firmware / OTA
    infoPreset: document.getElementById('infoPreset'),
    infoHeap: document.getElementById('infoHeap'),
    otaWarning: document.getElementById('otaRunningWarning'),
    otaForm: document.getElementById('otaForm'),
    fileInput: document.getElementById('firmwareFile'),
    dropZone: document.getElementById('dropZone'),
    fileNameDisplay: document.getElementById('fileNameDisplay'),
    btnUpload: document.getElementById('btnUploadFirmware'),
    btnRestart: document.getElementById('btnRestartController'),
    progressContainer: document.getElementById('uploadProgressContainer'),
    progressBar: document.getElementById('uploadProgressBar'),
    progressLabel: document.getElementById('uploadProgressLabel'),

    // Toast
    toast: document.getElementById('toastNotification'),
    toastIcon: document.getElementById('toastIcon'),
    toastMsg: document.getElementById('toastMessage')
  };

  // =============================================================================
  // 1. TOAST NOTIFICATIONS
  // =============================================================================
  let toastTimer = null;
  function showToast(msg, type = 'info', icon = 'ℹ️') {
    if (toastTimer) clearTimeout(toastTimer);
    el.toast.className = `toast ${type}`;
    el.toastIcon.textContent = icon;
    el.toastMsg.textContent = msg;
    el.toast.hidden = false;

    toastTimer = setTimeout(() => {
      el.toast.hidden = true;
    }, 4000);
  }

  // =============================================================================
  // 2. TAB NAVIGATION
  // =============================================================================
  el.tabBtns.forEach(btn => {
    btn.addEventListener('click', () => {
      const targetTab = btn.dataset.tab;
      el.tabBtns.forEach(b => {
        const active = (b === btn);
        b.classList.toggle('active', active);
        b.setAttribute('aria-selected', active);
      });
      el.tabPanels.forEach(p => {
        p.classList.toggle('active', p.id === `panel-${targetTab}`);
      });
    });
  });

  // =============================================================================
  // 3. WEBSOCKET PROTOCOL ENGINE
  // =============================================================================
  function initWebSocket() {
    const host = window.location.hostname || '192.168.4.1';
    const wsUrl = `ws://${host}:81/canonical`;

    el.wsPill.className = 'status-pill status-ws connecting';
    el.wsText.textContent = 'Connecting...';

    state.ws = new WebSocket(wsUrl);

    state.ws.onopen = () => {
      state.connected = true;
      el.wsPill.className = 'status-pill status-ws connected';
      el.wsText.textContent = 'Connected';

      // Request live hydration and full settings dictionary
      sendWsLine('?sysinfo');
      sendWsLine('SETTINGS.get=*');
      loadWifiConfig();
    };

    state.ws.onclose = () => {
      state.connected = false;
      el.wsPill.className = 'status-pill status-ws disconnected';
      el.wsText.textContent = 'Disconnected';
      setTimeout(initWebSocket, 3000);
    };

    state.ws.onerror = () => {
      state.ws.close();
    };

    state.ws.onmessage = (evt) => {
      handleWsPayload(evt.data);
    };
  }

  function sendWsLine(line) {
    if (state.ws && state.ws.readyState === WebSocket.OPEN) {
      state.ws.send(line.endsWith('\n') ? line : line + '\n');
    }
  }

  function handleWsPayload(raw) {
    const lines = raw.split('\n');
    for (let line of lines) {
      line = line.trim();
      if (!line) continue;

      // Telemetry Kinematics
      if (line.startsWith('K.isOn=')) {
        state.isOn = (line.split('=')[1] === '1');
        updateLatheStatus(state.isOn);
      } else if (line.startsWith('P.activePreset=')) {
        const preset = line.split('=')[1];
        if (el.infoPreset) el.infoPreset.textContent = preset;
      }

      // Settings Responses
      else if (line.startsWith('SETTINGS.val ') || line.startsWith('SETTINGS.ok ')) {
        const isOk = line.startsWith('SETTINGS.ok ');
        const kvPart = line.substring(line.indexOf(' ') + 1);
        const eqIdx = kvPart.indexOf('=');
        if (eqIdx > 0) {
          const key = kvPart.substring(0, eqIdx).trim();
          const val = kvPart.substring(eqIdx + 1).trim();
          applySettingToUi(key, val, isOk);
        }
      }

      // Sysinfo
      else if (line.startsWith('SYSINFO:') || line.startsWith('heap=')) {
        parseSysinfo(line);
      }
    }
  }

  function updateLatheStatus(isOn) {
    if (isOn) {
      el.lathePill.className = 'status-pill status-lathe running';
      el.latheText.textContent = 'RUNNING';
      if (el.otaWarning) el.otaWarning.hidden = false;
      if (el.btnUpload) el.btnUpload.disabled = true;
    } else {
      el.lathePill.className = 'status-pill status-lathe stopped';
      el.latheText.textContent = 'STOPPED';
      if (el.otaWarning) el.otaWarning.hidden = true;
      if (el.btnUpload && el.fileInput.files.length > 0) el.btnUpload.disabled = false;
    }
  }

  function parseSysinfo(line) {
    const parts = line.replace('SYSINFO:', '').split(',');
    for (const part of parts) {
      const [k, v] = part.split('=');
      if (!k || !v) continue;
      if (k.trim() === 'heap' || k.trim() === 'free_heap') {
        const kb = Math.round(parseInt(v) / 1024);
        el.heapBadge.textContent = `${kb} KB`;
        if (el.infoHeap) el.infoHeap.textContent = `${kb} KB (${v.trim()} bytes)`;
      }
    }
  }

  // =============================================================================
  // 4. MACHINE SETTINGS MANAGEMENT
  // =============================================================================
  function applySettingToUi(key, val, isConfirmedSave = false) {
    state.settingsValues[key] = val;
    state.dirtySettings.delete(key);

    const item = document.querySelector(`.setting-item[data-key="${key}"]`);
    if (!item) return;

    const input = item.querySelector('input, select');
    if (!input) return;

    const scale = parseFloat(input.dataset.scale) || 1;
    if (scale !== 1) {
      input.value = (parseFloat(val) / scale).toFixed(3);
    } else {
      input.value = val;
    }

    item.classList.remove('dirty');
    if (isConfirmedSave) {
      item.classList.add('saved');
      setTimeout(() => item.classList.remove('saved'), 2000);
    }
    updateDirtyBadge();
  }

  function setupSettingsListeners() {
    el.settingItems.forEach(item => {
      const key = item.dataset.key;
      const input = item.querySelector('input, select');
      if (!input || !key) return;

      input.addEventListener('input', () => {
        state.dirtySettings.add(key);
        item.classList.add('dirty');
        updateDirtyBadge();
      });

      input.addEventListener('change', () => {
        saveSingleSetting(key, input, item);
      });
    });

    el.btnReloadSettings.addEventListener('click', () => {
      sendWsLine('SETTINGS.get=*');
      showToast('Reloading settings from controller...', 'info');
    });

    el.btnSaveAllSettings.addEventListener('click', () => {
      saveAllDirtySettings();
    });
  }

  function saveSingleSetting(key, input, item) {
    const scale = parseFloat(input.dataset.scale) || 1;
    let rawVal = input.value;
    if (scale !== 1) {
      rawVal = Math.round(parseFloat(input.value) * scale).toString();
    }
    sendWsLine(`SETTINGS.set ${key}=${rawVal}`);
    showToast(`Saved ${key}`, 'success', '💾');
  }

  function saveAllDirtySettings() {
    if (state.dirtySettings.size === 0) {
      showToast('No unsaved changes', 'info');
      return;
    }
    state.dirtySettings.forEach(key => {
      const item = document.querySelector(`.setting-item[data-key="${key}"]`);
      if (item) {
        const input = item.querySelector('input, select');
        if (input) saveSingleSetting(key, input, item);
      }
    });
  }

  function updateDirtyBadge() {
    const hasDirty = (state.dirtySettings.size > 0);
    el.dirtyBadge.hidden = !hasDirty;
  }

  // =============================================================================
  // 5. WIFI MANAGEMENT & SCANNER
  // =============================================================================
  async function loadWifiConfig() {
    try {
      const res = await fetch('/wifi', { cache: 'no-store' });
      if (!res.ok) return;
      const text = await res.text();
      const lines = text.split('\n');
      const cfg = {};
      lines.forEach(l => {
        const [k, v] = l.split('=');
        if (k && v !== undefined) cfg[k.trim()] = v.trim();
      });

      if (cfg.wifiEnabled !== undefined) el.wifiEnabled.checked = (cfg.wifiEnabled === '1' || cfg.wifiEnabled === 'true');
      if (cfg.wifiSsid !== undefined) el.wifiSsid.value = cfg.wifiSsid;
      if (cfg.wifiHostname !== undefined) el.wifiHostname.value = cfg.wifiHostname;

      el.statusMode.textContent = cfg.wifiMode || 'unknown';
      el.statusIp.textContent = cfg.wifiIp || 'Not Assigned';
      el.wifiIpBadge.textContent = cfg.wifiIp || '--';
      el.statusSsid.textContent = cfg.wifiSsid || 'None';
      el.statusHostname.textContent = `${cfg.wifiHostname || 'whatels'}.local`;

      if (cfg.wifiMode === 'station') {
        el.wifiBadge.className = 'badge-pill success';
        el.wifiBadge.textContent = 'Station Active';
      } else if (cfg.wifiMode === 'setup-ap') {
        el.wifiBadge.className = 'badge-pill info';
        el.wifiBadge.textContent = 'Setup AP Active';
      } else {
        el.wifiBadge.className = 'badge-pill';
        el.wifiBadge.textContent = cfg.wifiMode || 'Idle';
      }
    } catch (e) {
      console.warn('Could not fetch /wifi status:', e);
    }
  }

  async function scanWifiNetworks() {
    el.scanSpinner.hidden = false;
    el.btnScanWifi.disabled = true;
    el.networkList.innerHTML = '<p class="placeholder-text">Scanning 2.4GHz Wi-Fi channels...</p>';

    try {
      const res = await fetch('/api/wifi/scan', { cache: 'no-store' });
      const networks = await res.json();

      el.networkList.innerHTML = '';
      if (!networks || networks.length === 0) {
        el.networkList.innerHTML = '<p class="placeholder-text">No networks found nearby.</p>';
        return;
      }

      networks.sort((a, b) => b.rssi - a.rssi);

      networks.forEach(net => {
        const item = document.createElement('div');
        item.className = 'network-item';
        item.innerHTML = `
          <span class="network-item-name">${escapeHtml(net.ssid)}</span>
          <div class="network-item-meta">
            <span>${net.open ? '🔓 Open' : '🔒 Secure'}</span>
            <span>${net.rssi} dBm</span>
          </div>
        `;
        item.addEventListener('click', () => {
          el.wifiSsid.value = net.ssid;
          el.wifiPassword.focus();
          showToast(`Selected "${net.ssid}"`, 'info', '📶');
        });
        el.networkList.appendChild(item);
      });
    } catch (err) {
      el.networkList.innerHTML = '<p class="placeholder-text error">Scan failed. Try again.</p>';
    } finally {
      el.scanSpinner.hidden = true;
      el.btnScanWifi.disabled = false;
    }
  }

  el.btnScanWifi.addEventListener('click', scanWifiNetworks);

  el.btnTogglePwd.addEventListener('click', () => {
    const isPwd = (el.wifiPassword.type === 'password');
    el.wifiPassword.type = isPwd ? 'text' : 'password';
    el.btnTogglePwd.textContent = isPwd ? '🙈' : '👁';
  });

  el.wifiForm.addEventListener('submit', async (e) => {
    e.preventDefault();
    const params = new URLSearchParams();
    params.append('wifiEnabled', el.wifiEnabled.checked ? '1' : '0');
    params.append('wifiSsid', el.wifiSsid.value);
    params.append('wifiPassword', el.wifiPassword.value);
    params.append('wifiHostname', el.wifiHostname.value);
    params.append('wifiClearPassword', '0');

    try {
      showToast('Saving WiFi config and connecting...', 'info', '⏳');
      const res = await fetch('/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: params.toString()
      });
      if (res.ok) {
        showToast('WiFi settings applied! Controller is reconnecting...', 'success', '✅');
        setTimeout(loadWifiConfig, 3000);
      } else {
        const err = await res.text();
        showToast(`Failed: ${err}`, 'error', '❌');
      }
    } catch (err) {
      showToast('Error sending WiFi update', 'error', '❌');
    }
  });

  el.btnResetWifi.addEventListener('click', async () => {
    if (!confirm('Reset all Wi-Fi settings to factory Setup-AP defaults?')) return;
    try {
      const res = await fetch('/wifi/reset', { method: 'POST' });
      if (res.ok) {
        showToast('Wi-Fi reset. Reconnecting to Setup AP...', 'success', '🔄');
        setTimeout(loadWifiConfig, 3000);
      }
    } catch (e) {
      showToast('Reset failed', 'error');
    }
  });

  // =============================================================================
  // 6. FIRMWARE & OTA
  // =============================================================================
  el.fileInput.addEventListener('change', () => {
    if (el.fileInput.files.length > 0) {
      const f = el.fileInput.files[0];
      el.fileNameDisplay.textContent = `${f.name} (${(f.size / 1024).toFixed(1)} KB)`;
      if (!state.isOn) el.btnUpload.disabled = false;
    }
  });

  el.otaForm.addEventListener('submit', (e) => {
    e.preventDefault();
    if (state.isOn) {
      alert('Cannot flash firmware while the lathe is running!');
      return;
    }
    if (el.fileInput.files.length === 0) return;

    const file = el.fileInput.files[0];
    const formData = new FormData();
    formData.append('update', file);

    el.btnUpload.disabled = true;
    el.progressContainer.hidden = false;
    el.progressBar.style.width = '0%';
    el.progressLabel.textContent = '0%';

    const xhr = new XMLHttpRequest();
    xhr.open('POST', '/firmware/upload', true);

    xhr.upload.onprogress = (evt) => {
      if (evt.lengthComputable) {
        const pct = Math.round((evt.loaded / evt.total) * 100);
        el.progressBar.style.width = `${pct}%`;
        el.progressLabel.textContent = `${pct}%`;
      }
    };

    xhr.onload = () => {
      if (xhr.status === 200) {
        el.progressLabel.textContent = 'Complete! Rebooting...';
        showToast('Firmware flashed successfully! Rebooting in 5s...', 'success', '🚀');
        let count = 5;
        const countdown = setInterval(() => {
          count--;
          if (count <= 0) {
            clearInterval(countdown);
            window.location.reload();
          } else {
            showToast(`Rebooting controller in ${count}s...`, 'info', '🔄');
          }
        }, 1000);
      } else {
        showToast(`Flash failed: ${xhr.responseText}`, 'error', '❌');
        el.btnUpload.disabled = false;
      }
    };

    xhr.onerror = () => {
      showToast('Network error during firmware upload', 'error', '❌');
      el.btnUpload.disabled = false;
    };

    xhr.send(formData);
  });

  el.btnRestart.addEventListener('click', async () => {
    if (!confirm('Restart the whatELS controller now?')) return;
    try {
      sendWsLine('CMD.system.restart');
      showToast('Restart signal sent. Reloading page...', 'info', '🔄');
      setTimeout(() => window.location.reload(), 4000);
    } catch (e) {
      showToast('Restart failed', 'error');
    }
  });

  function escapeHtml(str) {
    return str.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
  }

  // =============================================================================
  // INITIALIZATION
  // =============================================================================
  setupSettingsListeners();
  initWebSocket();
})();
