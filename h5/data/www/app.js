const defaultSection = 'control';
    const sectionLinks = Array.from(document.querySelectorAll('[data-section-link]'));
    const appSections = Array.from(document.querySelectorAll('[data-section]'));
    const controlButtons = Array.from(document.querySelectorAll('[data-control-action]'));
    const controlYElements = Array.from(document.querySelectorAll('.control-y'));
    const controlStatusPanel = document.getElementById('control-status');
    const controlExtraToggle = document.querySelector('[data-control-extra-toggle]');
    const controlMobileTabs = Array.from(document.querySelectorAll('[data-control-panel-tab]'));
    const controlPanelElements = Array.from(document.querySelectorAll('[data-control-panel]'));
    const controlShortcutElements = Array.from(document.querySelectorAll('[data-control-shortcut-action], [data-control-shortcut-panel]'));
    const controlMobileMedia = window.matchMedia ? window.matchMedia('(max-width: 640px)') : { matches: false };
    const controlState = document.getElementById('control-state');
    const controlBottomState = document.getElementById('control-bottom-state');
    const controlMode = document.getElementById('control-mode');
    const controlModePitch = document.getElementById('control-mode-pitch');
    const controlPitch = document.getElementById('control-pitch');
    const controlMeasure = document.getElementById('control-measure');
    const controlStep = document.getElementById('control-step');
    const controlTurns = document.getElementById('control-turns');
    const controlAngle = document.getElementById('control-angle');
    const controlX = document.getElementById('control-x');
    const controlXInlineStops = document.getElementById('control-x-inline-stops');
    const controlXStops = controlXInlineStops;
    const controlY = document.getElementById('control-y');
    const controlYInlineStops = document.getElementById('control-y-inline-stops');
    const controlYStops = controlYInlineStops;
    const controlZ = document.getElementById('control-z');
    const controlZInlineStops = document.getElementById('control-z-inline-stops');
    const controlZStops = controlZInlineStops;
    const controlRpm = document.getElementById('control-rpm');
    const controlBottomRpm = document.getElementById('control-bottom-rpm');
    const controlFullscreenButton = document.getElementById('control-fullscreen');
    const controlMessage = document.getElementById('control-message');
    const controlActionStatusElements = Array.from(document.querySelectorAll('[data-control-action-status]'));
    const controlActionStatus = {
      set textContent(value) {
        controlActionStatusElements.forEach(element => { element.textContent = value || ''; });
      },
      get textContent() {
        return controlActionStatusElements.length > 0 ? controlActionStatusElements[0].textContent : '';
      }
    };
    const log = document.getElementById('log');
    const commandInput = document.getElementById('command');
    const sendButton = document.getElementById('send');
    const gcodeList = document.getElementById('gcode-list');
    const gcodeNameInput = document.getElementById('gcode-name');
    const gcodeContentInput = document.getElementById('gcode-content');
    const addGcodeButton = document.getElementById('add-gcode');
    const removeCommentsCheckbox = document.getElementById('remove-comments');
    const configForm = document.getElementById('config-form');
    const configFields = document.getElementById('config-fields');
    const configStatus = document.getElementById('config-status');
    const saveConfigButton = document.getElementById('save-config');
    const resetConfigButton = document.getElementById('reset-config');
    const accessibilityVoiceFeedbackInput = document.getElementById('accessibility-voice-feedback');
    const accessibilityAnnounceButtonsInput = document.getElementById('accessibility-announce-buttons');
    const accessibilityAnnounceStatusInput = document.getElementById('accessibility-announce-status');
    const accessibilityAnnounceWarningsInput = document.getElementById('accessibility-announce-warnings');
    const accessibilityAnnounceAxisInput = document.getElementById('accessibility-announce-axis');
    const accessibilityQuietHoldInput = document.getElementById('accessibility-quiet-hold');
    const accessibilitySpeechRateInput = document.getElementById('accessibility-speech-rate');
    const accessibilityTestVoiceButton = document.getElementById('accessibility-test-voice');
    const accessibilityStatus = document.getElementById('accessibility-status');
    const tftFirstUploadCheckbox = document.getElementById('tft-first-upload');
    const tftBrowseButton = document.getElementById('tft-browse');
    const tftFileInput = document.getElementById('tft-file');
    const tftProgress = document.getElementById('tft-progress');
    const tftStatus = document.getElementById('tft-status');
    const firmwareBrowseButton = document.getElementById('firmware-browse');
    const firmwareFileInput = document.getElementById('firmware-file');
    const firmwareProgress = document.getElementById('firmware-progress');
    const firmwareStatus = document.getElementById('firmware-status');
    const wifiForm = document.getElementById('wifi-form');
    const wifiEnabledInput = document.getElementById('wifi-enabled');
    const wifiSsidInput = document.getElementById('wifi-ssid');
    const wifiHostnameInput = document.getElementById('wifi-hostname');
    const wifiPasswordInput = document.getElementById('wifi-password');
    const wifiClearPasswordInput = document.getElementById('wifi-clear-password');
    const saveWifiButton = document.getElementById('save-wifi');
    const resetWifiButton = document.getElementById('reset-wifi');
    const wifiStatusElement = document.getElementById('wifi-status');
    const keyboardForm = document.getElementById('keyboard-form');
    const keyboardFields = document.getElementById('keyboard-fields');
    const keyboardShowKeysInput = document.getElementById('keyboard-show-keys');
    const keyboardLastKey = document.getElementById('keyboard-last-key');
    const keyboardStatus = document.getElementById('keyboard-status');
    const saveKeyboardButton = document.getElementById('save-keyboard');
    const resetKeyboardButton = document.getElementById('reset-keyboard');
    let tftUploadInProgress = false;
    let firmwareUploadInProgress = false;
    let firmwareReloadTimer = 0;
    
    // System Info
    const sysinfoVersion = document.getElementById('sysinfo-version');
    const sysinfoChip = document.getElementById('sysinfo-chip');
    const sysinfoFlash = document.getElementById('sysinfo-flash');
    const sysinfoHeap = document.getElementById('sysinfo-heap');
    const sysinfoRefreshBtn = document.getElementById('refresh-sysinfo');
    let firmwareStatusPollTimer = 0;
    let firmwareStatusPollBusy = false;
    let keyboardLearnTarget = '';
    let keyboardLearnTimer = 0;
    let controlHoldHeartbeatTimer = 0;
    let controlWakeLock = null;
    let controlWakeLockBusy = false;
    let controlNoSleepVideo = null;
    let controlNoSleepDrawTimer = 0;
    let accessibilityAxisTimer = 0;
    let accessibilityMessageTimer = 0;
    let accessibilityHoldAnnouncementTimer = 0;
    let accessibilityLastSpeechPhrase = '';
    let accessibilityLastSpeechMillis = 0;
    let accessibilityLastSpeechKind = '';
    let accessibilityLastStatusText = '';
    let accessibilityLastMessageText = '';
    let webUiNumpadEntryActive = false;
    let webUiNumpadDigits = '';
    const accessibilityPendingAxisValues = {};
    const accessibilityLastAxisValues = { x: '', y: '', z: '' };
    const activeControlActions = new Set();
    const tftFirstUploadStorageKey = 'nanoels-h5.tft-first-upload';
    const accessibilityStorageKey = 'nanoels-h5.accessibility';
    const accessibilityDefaults = {
      voiceFeedback: false,
      announceButtons: true,
      announceStatus: true,
      announceWarnings: true,
      announceAxis: false,
      quietHold: true,
      speechRate: '1'
    };
    let accessibilitySettings = Object.assign({}, accessibilityDefaults);
    const machineConfigSections = [
      {
        title: 'Spindle encoder',
        fields: [
          { key: 'encoderPpr', label: 'Encoder PPR', unit: 'pulses/rev', min: 1, max: 15000, step: 1, help: 'Pulses per spindle revolution from the encoder specification before quadrature counting. Common values are 600 or 1200.' },
          { key: 'encoderBacklash', label: 'Encoder backlash', unit: 'pulses', min: 0, max: 30000, step: 1, help: 'Encoder pulses ignored after spindle direction reverses. Increase only if the displayed angle flickers after reversal.' }
        ]
      },
      {
        title: 'Z axis',
        fields: [
          { key: 'zScrewDu', label: 'Lead screw pitch', unit: 'mm/rev', min: 0.0001, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Distance Z moves for one lead screw revolution. Metric screw: use the marked pitch. Inch screw: 25.4 / TPI, so 8 TPI = 3.175.' },
          { key: 'zMotorSteps', label: 'Motor steps per rev', unit: 'steps/rev', min: 1, max: 1000000, step: 1, help: 'Full motor steps * driver microsteps * motor-to-screw ratio. Example: 200-step motor at 4x microstepping = 800.' },
          { key: 'zPulsePerRevolution', label: 'Handwheel PPR', unit: 'pulses/rev', min: 1, max: 100000, step: 0.01, help: 'Physical pulses per revolution of the optional Z handwheel encoder. Use the handwheel specification.' },
          { key: 'zSpeedStart', label: 'Start speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Initial step pulse rate when motion starts. Use a value the motor can start from reliably without missing steps.' },
          { key: 'zAcceleration', label: 'Acceleration', unit: 'steps/s^2', min: 1, max: 100000000, step: 1, help: 'Ramp rate for speeding up and slowing down. Higher feels snappier but can skip steps if the motor or load cannot keep up.' },
          { key: 'zSpeedManualMove', label: 'Manual speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Top speed for jogs, rapid returns, and GCode moves on Z. Axis speed depends on both this value and steps per mm.' },
          { key: 'zMaxTravelMm', label: 'Max travel', unit: 'mm', min: 1, max: 10000, step: 1, help: 'Maximum single Z move the controller will allow. Set a little above the physically safe travel of the carriage.' },
          { key: 'zBacklashDu', label: 'Backlash', unit: 'mm', min: 0, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Lost Z motion when reversing direction. Measure with a dial indicator: move one way, zero it, reverse until the indicator starts moving.' },
          { key: 'zInvert', label: 'Invert direction', type: 'checkbox', help: 'Toggle if the Z axis moves opposite to the screen or keyboard direction.' },
          { key: 'zInvertEnable', label: 'Invert enable', type: 'checkbox', help: 'Toggle only when the stepper driver enable input works backwards.' },
          { key: 'zNeedsRest', label: 'Needs rest', type: 'checkbox', help: 'Enable if the driver should be powered only during movement. Leave off when Z must hold position while idle.' }
        ]
      },
      {
        title: 'X axis',
        fields: [
          { key: 'xScrewDu', label: 'Lead screw pitch', unit: 'mm/rev', min: 0.0001, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Distance X moves for one lead screw revolution. Metric screw: use the marked pitch. Inch screw: 25.4 / TPI, so 10 TPI = 2.54.' },
          { key: 'xMotorSteps', label: 'Motor steps per rev', unit: 'steps/rev', min: 1, max: 1000000, step: 1, help: 'Full motor steps * driver microsteps * motor-to-screw ratio. Example: 200-step motor at 4x microstepping = 800.' },
          { key: 'xPulsePerRevolution', label: 'Handwheel PPR', unit: 'pulses/rev', min: 1, max: 100000, step: 0.01, help: 'Physical pulses per revolution of the optional X handwheel encoder. Use the handwheel specification.' },
          { key: 'xSpeedStart', label: 'Start speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Initial step pulse rate when motion starts. Use a value the motor can start from reliably without missing steps.' },
          { key: 'xAcceleration', label: 'Acceleration', unit: 'steps/s^2', min: 1, max: 100000000, step: 1, help: 'Ramp rate for speeding up and slowing down. Higher feels snappier but can skip steps if the motor or slide cannot keep up.' },
          { key: 'xSpeedManualMove', label: 'Manual speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Top speed for jogs, rapid returns, and GCode moves on X. Axis speed depends on both this value and steps per mm.' },
          { key: 'xMaxTravelMm', label: 'Max travel', unit: 'mm', min: 1, max: 10000, step: 1, help: 'Maximum single X move the controller will allow. Set a little above the physically safe travel of the cross-slide.' },
          { key: 'xBacklashDu', label: 'Backlash', unit: 'mm', min: 0, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Lost X motion when reversing direction. Measure with a dial indicator: move one way, zero it, reverse until the indicator starts moving.' },
          { key: 'xInvert', label: 'Invert direction', type: 'checkbox', help: 'Toggle if the X axis moves opposite to the screen or keyboard direction.' },
          { key: 'xInvertEnable', label: 'Invert enable', type: 'checkbox', help: 'Toggle only when the stepper driver enable input works backwards.' },
          { key: 'xNeedsRest', label: 'Needs rest', type: 'checkbox', help: 'Enable if the driver should be powered only during movement. Leave off when X must hold position while idle.' }
        ]
      },
      {
        title: 'Manual movement',
        fields: [
          { key: 'stepTimeMs', label: 'Step time', unit: 'ms', min: 1, max: 10000, step: 1, help: 'Target time for one precision button step when the selected move step is not continuous.' },
          { key: 'delayBetweenStepsMs', label: 'Step delay', unit: 'ms', min: 0, max: 10000, step: 1, help: 'Pause between repeated precision steps while a manual move button is held.' },
          { key: 'enableContinuousMove', label: 'Enable continuous moves', type: 'checkbox', help: 'Let move buttons run continuously when the selected move step is 1mm or 0.1in.' },
          { key: 'axisEncoderBacklash', label: 'Handwheel backlash', unit: 'pulses', min: 0, max: 30000, step: 1, help: 'Handwheel encoder pulses ignored after direction reverses on Z/X/Y pulse inputs. Set to 0 for direct encoders without mechanical play.' }
        ]
      },
      {
        title: 'Automated modes',
        fields: [
          { key: 'safeDistanceDu', label: 'Safe retract distance', unit: 'mm', min: 0, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Distance the auxiliary axis backs away from the material between automated passes. Set to 0 to disable the extra retract.' },
          { key: 'slotLeftReductionDu', label: 'Slot left reduction', unit: 'mm/pass', min: 0, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Shortens each successive slotting left cut by this Z distance for blind slots. Set to 0 for full-length passes.' }
        ]
      },
      {
        title: 'Y axis',
        fields: [
          { key: 'activeY', label: 'Y axis connected', type: 'checkbox', help: 'Enable only when the optional Y or dividing-head axis is wired and configured.' },
          { key: 'rotaryY', label: 'Rotary axis', type: 'checkbox', help: 'On means Y values are degrees. Off means Y values are linear distance.' },
          { key: 'yMotorSteps', label: 'Motor steps per rev', unit: 'steps/rev', min: 1, max: 1000000, step: 1, help: 'Full motor steps * driver microsteps * motor-to-axis ratio.' },
          { key: 'yPulsePerRevolution', label: 'Handwheel PPR', unit: 'pulses/rev', min: 1, max: 100000, step: 0.01, help: 'Physical pulses per revolution of the optional Y handwheel encoder. Use the handwheel specification.' },
          { key: 'yScrewDu', label: 'Travel per screw rev', unit: 'mm or deg/rev', min: 0.0001, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'For rotary Y, degrees moved per worm screw revolution. For linear Y, mm moved per lead screw revolution.' },
          { key: 'ySpeedStart', label: 'Start speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Initial step pulse rate when Y motion starts. Use a value the motor can start from reliably.' },
          { key: 'yAcceleration', label: 'Acceleration', unit: 'steps/s^2', min: 1, max: 100000000, step: 1, help: 'Ramp rate for speeding up and slowing down Y motion.' },
          { key: 'ySpeedManualMove', label: 'Manual speed', unit: 'steps/s', min: 1, max: 1000000, step: 1, help: 'Top speed for manual and GCode Y moves.' },
          { key: 'yMaxTravelMm', label: 'Max travel', unit: 'mm or deg', min: 1, max: 10000, step: 1, help: 'Maximum single Y move the controller will allow. For rotary Y this is degrees.' },
          { key: 'yBacklashDu', label: 'Backlash', unit: 'mm or deg', min: 0, max: 1000, step: 0.0001, firmwareScale: 10000, help: 'Lost Y motion when reversing direction. For rotary Y enter degrees; for linear Y enter mm.' },
          { key: 'yInvert', label: 'Invert direction', type: 'checkbox', help: 'Toggle if Y moves opposite to the expected direction.' },
          { key: 'yInvertEnable', label: 'Invert enable', type: 'checkbox', help: 'Toggle only when the Y stepper driver enable input works backwards.' },
          { key: 'yNeedsRest', label: 'Needs rest', type: 'checkbox', help: 'Enable if the Y driver should be powered only during movement. Leave off when it must hold position while idle.' }
        ]
      },
      {
        title: 'Joystick',
        fields: [
          { key: 'joystickEnabled', label: 'Joystick enabled', type: 'checkbox', help: 'Enable only after the optional analog joystick is wired. Joystick axes must feed ESP32-safe 3.3V ADC inputs.' },
          { key: 'joystickZEnabled', label: 'Z input enabled', type: 'checkbox', help: 'Enable when the joystick has a wired Z potentiometer on JZ. Disable for joysticks without this axis.' },
          { key: 'joystickXEnabled', label: 'X input enabled', type: 'checkbox', help: 'Enable when the joystick has a wired X potentiometer on JY. Disable for joysticks without this axis.' },
          { key: 'joystickYEnabled', label: 'Y input enabled', type: 'checkbox', help: 'Enable when the joystick has a wired Y potentiometer on JY. Disable for joysticks without this axis.' },
          { key: 'joystickButtonTogglesOnOff', label: 'Button toggles ON/OFF', type: 'checkbox', help: 'In JOY mode, a short button click while Z and X are neutral toggles feed ON/OFF. Holding the button with Z or X deflected still performs rapid movement.' },
          { key: 'joystickCenterSamples', label: 'Center samples', unit: 'samples', min: 1, max: 1024, step: 1, help: 'Number of ADC readings averaged at startup to learn the joystick center position.' },
          { key: 'joystickOversamples', label: 'Oversamples', unit: 'samples', min: 1, max: 1024, step: 1, help: 'ADC readings averaged for each joystick update. Higher values smooth noise but add latency.' },
          { key: 'joystickSampleIntervalMs', label: 'Sample interval', unit: 'ms', min: 1, max: 1000, step: 1, help: 'Time between joystick updates. Smaller values react faster and use more CPU.' },
          { key: 'joystickAdcMax', label: 'ADC max', unit: 'counts', min: 1, max: 65535, step: 1, help: 'Maximum raw ADC count. ESP32 12-bit ADC readings normally use 4095.' },
          { key: 'joystickDeadband', label: 'Deadband', unit: 'counts', min: 0, max: 65534, step: 1, help: 'Raw ADC counts around center treated as zero. Increase if an axis drifts when released.' },
          { key: 'joystickPulseQueueLimit', label: 'Pulse queue limit', unit: 'pulses', min: 1, max: 1000000, step: 1, help: 'Maximum queued virtual handwheel pulses. Higher allows more buffered joystick motion.' },
          { key: 'joystickNormalRevolutionsPerSecond', label: 'Normal speed', unit: 'rev/s', min: 0.01, max: 1000, step: 0.01, help: 'Joystick jog speed in equivalent screw revolutions per second when rapid is not pressed.' },
          { key: 'joystickRapidRevolutionsPerSecond', label: 'Rapid speed', unit: 'rev/s', min: 0.01, max: 1000, step: 0.01, help: 'Joystick jog speed in equivalent screw revolutions per second while the button is held.' },
          { key: 'invertJoystickZ', label: 'Invert Z', type: 'checkbox', help: 'Toggle if joystick Z deflection moves the carriage in the wrong direction.' },
          { key: 'invertJoystickX', label: 'Invert X', type: 'checkbox', help: 'Toggle if joystick X deflection moves the cross-slide in the wrong direction.' },
          { key: 'invertJoystickY', label: 'Invert Y', type: 'checkbox', help: 'Toggle if joystick Y deflection changes the value in the wrong direction.' },
          { key: 'invertJoystickButton', label: 'Invert button', type: 'checkbox', help: 'Toggle if the joystick button reads pressed when released.' }
        ]
      }
    ];
    const machineConfigFields = machineConfigSections.flatMap(section => section.fields);
    const keyboardBindingSections = [
      {
        title: 'Movement',
        fields: [
          { key: 'left', label: 'Left' },
          { key: 'right', label: 'Right' },
          { key: 'up', label: 'Up' },
          { key: 'down', label: 'Down' },
          { key: 'forward', label: 'Y forward' },
          { key: 'back', label: 'Y back' }
        ]
      },
      {
        title: 'Operation',
        fields: [
          { key: 'on', label: 'On / start' },
          { key: 'off', label: 'Off / stop' },
          { key: 'plus', label: 'Plus' },
          { key: 'minus', label: 'Minus' },
          { key: 'step', label: 'Move step' },
          { key: 'reverse', label: 'Reverse pitch' },
          { key: 'measure', label: 'Measure units' },
          { key: 'display', label: 'Display info' },
          { key: 'multistart', label: 'Multi-start' }
        ]
      },
      {
        title: 'Stops and axes',
        fields: [
          { key: 'stopL', label: 'Set Z left stop' },
          { key: 'stopR', label: 'Set Z right stop' },
          { key: 'stopU', label: 'Set X forward stop' },
          { key: 'stopD', label: 'Set X rear stop' },
          { key: 'stopF', label: 'Set Y forward stop' },
          { key: 'stopB', label: 'Set Y back stop' },
          { key: 'zeroX', label: 'Zero X' },
          { key: 'zeroZ', label: 'Zero Z' },
          { key: 'zeroY', label: 'Zero Y' },
          { key: 'diameter', label: 'Set diameter' },
          { key: 'enableX', label: 'Enable X' },
          { key: 'enableZ', label: 'Enable Z' },
          { key: 'enableY', label: 'Enable Y' }
        ]
      },
      {
        title: 'Number entry',
        fields: [
          { key: 'digit0', label: 'Digit 0' },
          { key: 'digit1', label: 'Digit 1' },
          { key: 'digit2', label: 'Digit 2' },
          { key: 'digit3', label: 'Digit 3' },
          { key: 'digit4', label: 'Digit 4' },
          { key: 'digit5', label: 'Digit 5' },
          { key: 'digit6', label: 'Digit 6' },
          { key: 'digit7', label: 'Digit 7' },
          { key: 'digit8', label: 'Digit 8' },
          { key: 'digit9', label: 'Digit 9' },
          { key: 'backspace', label: 'Backspace' }
        ]
      },
      {
        title: 'Modes',
        fields: [
          { key: 'mode', label: 'Mode menu' },
          { key: 'modeGears', label: 'Mode gearbox' },
          { key: 'modeXGear', label: 'Mode X gearbox' },
          { key: 'modeJoystick', label: 'Mode joystick' },
          { key: 'modeTurn', label: 'Mode turn' },
          { key: 'modeFace', label: 'Mode face' },
          { key: 'modeCone', label: 'Mode cone' },
          { key: 'modeCut', label: 'Mode cut' },
          { key: 'modeSlot', label: 'Mode slot' },
          { key: 'modeThread', label: 'Mode thread' },
          { key: 'modeEllipse', label: 'Mode ellipse' },
          { key: 'modeGcode', label: 'Mode GCode' },
          { key: 'modeAsync', label: 'Mode async' },
          { key: 'modeY', label: 'Mode Y' }
        ]
      }
    ];
    const keyboardBindingFields = keyboardBindingSections.flatMap(section => section.fields);

    const normalizeSection = (section) => {
      return appSections.some(element => element.dataset.section === section) ? section : defaultSection;
    };

    const sectionFromHash = () => {
      const hash = window.location.hash || '';
      return normalizeSection(hash.length > 1 ? hash.substring(1).toLowerCase() : '');
    };

    const showSection = (section) => {
      const activeSection = normalizeSection(section);
      appSections.forEach(element => {
        element.classList.toggle('active', element.dataset.section === activeSection);
      });
      sectionLinks.forEach(link => {
        link.classList.toggle('active', link.dataset.sectionLink === activeSection);
      });
      document.body.classList.toggle('control-section-active', activeSection === 'control');
    };

    const handleSectionChange = (scrollToTop) => {
      showSection(sectionFromHash());
      if (scrollToTop) window.scrollTo(0, 0);
    };

    function speechSupported() {
      return 'speechSynthesis' in window && 'SpeechSynthesisUtterance' in window;
    }

    function normalizeSpeechPhrase(value) {
      return String(value || '')
        .replace(/\b([XYZ])-(?=\d)/gi, '$1 Minus ')
        .replace(/\s+/g, ' ')
        .trim();
    }

    function speechRate() {
      const rate = Number(accessibilitySettings.speechRate);
      return Number.isFinite(rate) ? Math.min(3, Math.max(0.5, rate)) : 1;
    }

    function cancelAxisAnnouncement() {
      clearTimeout(accessibilityAxisTimer);
      accessibilityAxisTimer = 0;
      Object.keys(accessibilityPendingAxisValues).forEach(key => { delete accessibilityPendingAxisValues[key]; });
      if (accessibilityLastSpeechKind === 'axis' && speechSupported()) {
        window.speechSynthesis.cancel();
        accessibilityLastSpeechKind = '';
      }
    }

    function cancelMessageAnnouncement() {
      clearTimeout(accessibilityMessageTimer);
      accessibilityMessageTimer = 0;
      if (accessibilityLastSpeechKind === 'message' && speechSupported()) {
        window.speechSynthesis.cancel();
        accessibilityLastSpeechKind = '';
      }
    }

    function speakAccessibility(phrase, options = {}) {
      const text = normalizeSpeechPhrase(phrase);
      if (!text || !speechSupported()) return false;
      if (!options.force && !accessibilitySettings.voiceFeedback) return false;
      if (!options.force) {
        const now = Date.now();
        const cooldown = options.cooldownMs === undefined ? 1500 : options.cooldownMs;
        if (text === accessibilityLastSpeechPhrase && now - accessibilityLastSpeechMillis < cooldown) return false;
      }
      if (options.kind !== 'axis' && (options.cancelPrevious || options.kind === 'button' || options.kind === 'panel' || options.kind === 'warning')) {
        cancelAxisAnnouncement();
      }
      if (options.kind !== 'message' && (options.cancelPrevious || options.kind === 'button' || options.kind === 'panel' || options.kind === 'warning' || options.kind === 'status')) {
        cancelMessageAnnouncement();
      }
      if (options.cancelPrevious) window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(text);
      utterance.rate = speechRate();
      utterance.onend = () => {
        if (accessibilityLastSpeechKind === options.kind) accessibilityLastSpeechKind = '';
      };
      utterance.onerror = utterance.onend;
      window.speechSynthesis.speak(utterance);
      accessibilityLastSpeechPhrase = text;
      accessibilityLastSpeechMillis = Date.now();
      accessibilityLastSpeechKind = options.kind || '';
      return true;
    }

    function announceButtonAction(label, state = '') {
      if (!accessibilitySettings.announceButtons) return;
      const phrase = normalizeSpeechPhrase(`${label}${state ? ' ' + state : ''}`);
      speakAccessibility(phrase, { kind: 'button' });
    }

    function axisStatusValue(axisName) {
      const elements = { x: controlX, y: controlY, z: controlZ };
      const element = elements[axisName.toLowerCase()];
      return normalizeSpeechPhrase(element ? element.textContent : '');
    }

    function axisLooksEnabled(axisName) {
      const value = axisStatusValue(axisName);
      return !!value && value !== '--';
    }

    function stopDatasetForAxis(axisName) {
      const elements = { x: controlXStops, y: controlYStops, z: controlZStops };
      return elements[axisName.toLowerCase()] || null;
    }

    function enteredNumpadValueText() {
      const message = normalizeSpeechPhrase(controlMessage ? controlMessage.textContent : '');
      const match = message.match(/^Use\s+(.+)\?$/i);
      if (match) return match[1];
      return '';
    }

    function stopActionAnnouncement(actionCode) {
      const stopActions = {
        '65': { axis: 'Z', side: 'left', label: 'left' },
        '68': { axis: 'Z', side: 'right', label: 'right' },
        '87': { axis: 'X', side: 'left', label: 'forward' },
        '83': { axis: 'X', side: 'right', label: 'rear' },
        '73': { axis: 'Y', side: 'left', label: 'forward' },
        '75': { axis: 'Y', side: 'right', label: 'back' }
      };
      const action = stopActions[String(actionCode)];
      if (!action) return '';
      if (webUiNumpadEntryActive) {
        const valueText = enteredNumpadValueText();
        return `${action.axis} ${action.label} stop set${valueText ? ' to ' + valueText : ' to entered value'}`;
      }
      const stops = stopDatasetForAxis(action.axis);
      const current = stops ? (stops.dataset[action.side] || '') : '';
      return `${action.axis} ${action.label} stop ${current ? 'removed' : 'added'}`;
    }

    function enableActionAnnouncement(actionCode) {
      const enableActions = { '81': 'Z', '67': 'X', '89': 'Y' };
      const axis = enableActions[String(actionCode)];
      if (!axis) return '';
      return `${axis} axis ${axisLooksEnabled(axis) ? 'disabled' : 'enabled'}`;
    }

    function actionAnnouncementForSpeech(actionCode, label) {
      return stopActionAnnouncement(actionCode) || enableActionAnnouncement(actionCode) || actionLabelForSpeech(actionCode, label);
    }

    function updateWebUiNumpadTracking(actionCode) {
      const code = String(actionCode);
      const codeNumber = Number(code);
      if (codeNumber >= 48 && codeNumber <= 57) {
        if (!webUiNumpadEntryActive) webUiNumpadDigits = '';
        webUiNumpadEntryActive = true;
        if (webUiNumpadDigits.length < 8) webUiNumpadDigits += String.fromCharCode(codeNumber);
        return;
      }
      if (code === '28') {
        webUiNumpadEntryActive = true;
        webUiNumpadDigits = webUiNumpadDigits.substring(0, Math.max(0, webUiNumpadDigits.length - 1));
        return;
      }
      if (webUiNumpadEntryActive && (code === '95' || code === '60')) return;
      webUiNumpadEntryActive = false;
      webUiNumpadDigits = '';
    }

    function announcePanelChange(panelName) {
      if (!accessibilitySettings.announceButtons) return;
      const labels = { jog: 'Jog', run: 'Run', numpad: 'Numpad', stops: 'Stops', modes: 'Modes' };
      speakAccessibility(labels[panelName] || panelName, { kind: 'panel' });
    }

    function announceWarning(text) {
      if (!accessibilitySettings.announceWarnings) return;
      speakAccessibility(text, { kind: 'warning', cancelPrevious: true });
    }

    function statusPhrase(value) {
      const status = normalizeSpeechPhrase(value).toUpperCase();
      if (status === 'ON') return 'Controller on';
      if (status === 'OFF') return 'Controller off';
      if (status === 'SYN') return 'Controller synchronized';
      if (status === 'CONNECTED') return 'Connected';
      if (status === 'DISCONNECTED') return 'Disconnected';
      return normalizeSpeechPhrase(value);
    }

    function announceStatusChange(value) {
      const phrase = statusPhrase(value);
      if (!phrase || phrase === accessibilityLastStatusText) return;
      accessibilityLastStatusText = phrase;
      if (!accessibilitySettings.announceStatus) return;
      speakAccessibility(phrase, { kind: 'status' });
    }

    function announceControlMessage(value) {
      const phrase = normalizeSpeechPhrase(value);
      if (!phrase || phrase === '--' || phrase === accessibilityLastMessageText) return;
      accessibilityLastMessageText = phrase;
      if (!accessibilitySettings.announceStatus) return;
      cancelMessageAnnouncement();
      accessibilityMessageTimer = setTimeout(() => {
        accessibilityMessageTimer = 0;
        speakAccessibility(phrase, { kind: 'message', cancelPrevious: true, cooldownMs: 500 });
      }, 650);
    }

    function announceAxisPosition(axisName, value) {
      if (!accessibilitySettings.announceAxis) return;
      const axisKey = axisName.toLowerCase();
      const cleanValue = normalizeSpeechPhrase(value);
      if (!cleanValue || cleanValue === '--' || accessibilityLastAxisValues[axisKey] === cleanValue) return;
      accessibilityLastAxisValues[axisKey] = cleanValue;
      clearTimeout(accessibilityAxisTimer);
      accessibilityAxisTimer = 0;
      if (accessibilityLastSpeechKind === 'axis' && speechSupported()) {
        window.speechSynthesis.cancel();
        accessibilityLastSpeechKind = '';
      }
      accessibilityPendingAxisValues[axisName] = cleanValue;
      accessibilityAxisTimer = setTimeout(() => {
        const phrase = Object.keys(accessibilityPendingAxisValues).map(axis => `${axis} ${accessibilityPendingAxisValues[axis]}`).join(', ');
        Object.keys(accessibilityPendingAxisValues).forEach(key => { delete accessibilityPendingAxisValues[key]; });
        accessibilityAxisTimer = 0;
        speakAccessibility(phrase, { kind: 'axis', cancelPrevious: true, cooldownMs: 250 });
      }, 450);
    }

    function actionLabelForSpeech(actionCode, label) {
      const text = normalizeSpeechPhrase(label);
      const actionLabels = {
        '21': 'Jog Z left',
        '22': 'Jog Z right',
        '23': 'Jog X forward',
        '24': 'Jog X back',
        '85': 'Jog Y forward',
        '74': 'Jog Y back',
        '27': 'OFF',
        '30': 'ON',
        '64': 'Step',
        '82': 'Reverse',
        '77': 'Units',
        '88': 'Zero X',
        '90': 'Zero Z',
        '72': 'Zero Y',
        '97': 'Gear mode',
        '98': 'Turn mode',
        '99': 'Face mode',
        '100': 'Cone mode',
        '101': 'Cut mode',
        '102': 'Thread mode',
        '103': 'Async mode',
        '104': 'Ellipse mode',
        '105': 'G code mode',
        '106': 'Y mode',
        '108': 'Joystick mode',
        '109': 'X gear mode',
        '110': 'Slot mode'
      };
      return actionLabels[String(actionCode)] || text;
    }

    function announceActiveHolds() {
      if (accessibilitySettings.quietHold || !accessibilitySettings.announceButtons || activeControlActions.size === 0) return;
      activeControlActions.forEach(actionCode => {
        const button = document.querySelector(`[data-control-action="${actionCode}"]`);
        const label = actionAnnouncementForSpeech(actionCode, controlButtonLabel(button));
        speakAccessibility(`${label} held`, { kind: 'button', cooldownMs: 2200 });
      });
    }

    function updateHoldAnnouncementTimer() {
      clearInterval(accessibilityHoldAnnouncementTimer);
      accessibilityHoldAnnouncementTimer = 0;
      if (!accessibilitySettings.quietHold && activeControlActions.size > 0) {
        accessibilityHoldAnnouncementTimer = setInterval(announceActiveHolds, 2500);
      }
    }

    function loadAccessibilitySettings() {
      try {
        const stored = localStorage.getItem(accessibilityStorageKey);
        if (stored) accessibilitySettings = Object.assign({}, accessibilityDefaults, JSON.parse(stored));
      } catch (error) {
        accessibilitySettings = Object.assign({}, accessibilityDefaults);
      }
      accessibilityVoiceFeedbackInput.checked = !!accessibilitySettings.voiceFeedback;
      accessibilityAnnounceButtonsInput.checked = !!accessibilitySettings.announceButtons;
      accessibilityAnnounceStatusInput.checked = !!accessibilitySettings.announceStatus;
      accessibilityAnnounceWarningsInput.checked = !!accessibilitySettings.announceWarnings;
      accessibilityAnnounceAxisInput.checked = !!accessibilitySettings.announceAxis;
      accessibilityQuietHoldInput.checked = !!accessibilitySettings.quietHold;
      if (!['1', '1.5', '2', '3'].includes(String(accessibilitySettings.speechRate))) {
        accessibilitySettings.speechRate = accessibilityDefaults.speechRate;
      }
      accessibilitySpeechRateInput.value = accessibilitySettings.speechRate || accessibilityDefaults.speechRate;
      if (!speechSupported()) {
        accessibilityStatus.textContent = 'Voice feedback is not supported by this browser';
        accessibilityVoiceFeedbackInput.disabled = true;
        accessibilityTestVoiceButton.disabled = true;
        accessibilityTestVoiceButton.classList.add('disabled');
      }
    }

    function saveAccessibilitySettings() {
      accessibilitySettings = {
        voiceFeedback: accessibilityVoiceFeedbackInput.checked,
        announceButtons: accessibilityAnnounceButtonsInput.checked,
        announceStatus: accessibilityAnnounceStatusInput.checked,
        announceWarnings: accessibilityAnnounceWarningsInput.checked,
        announceAxis: accessibilityAnnounceAxisInput.checked,
        quietHold: accessibilityQuietHoldInput.checked,
        speechRate: accessibilitySpeechRateInput.value
      };
      if (!accessibilitySettings.announceAxis) cancelAxisAnnouncement();
      if (!accessibilitySettings.announceStatus) cancelMessageAnnouncement();
      if (!accessibilitySettings.voiceFeedback && speechSupported()) {
        cancelAxisAnnouncement();
        cancelMessageAnnouncement();
        window.speechSynthesis.cancel();
      }
      updateHoldAnnouncementTimer();
      try {
        localStorage.setItem(accessibilityStorageKey, JSON.stringify(accessibilitySettings));
      } catch (error) {
      }
      accessibilityStatus.textContent = speechSupported() ? 'Accessibility settings saved on this browser' : 'Voice feedback is not supported by this browser';
    }

    function setupAccessibilitySettings() {
      loadAccessibilitySettings();
      [
        accessibilityVoiceFeedbackInput,
        accessibilityAnnounceButtonsInput,
        accessibilityAnnounceStatusInput,
        accessibilityAnnounceWarningsInput,
        accessibilityAnnounceAxisInput,
        accessibilityQuietHoldInput,
        accessibilitySpeechRateInput
      ].forEach(input => input.addEventListener('change', () => {
        const wasVoiceEnabled = accessibilitySettings.voiceFeedback;
        saveAccessibilitySettings();
        if (!wasVoiceEnabled && accessibilitySettings.voiceFeedback) {
          speakAccessibility('Voice feedback enabled', { force: true, kind: 'test', cancelPrevious: true });
        }
      }));
      accessibilityTestVoiceButton.addEventListener('click', () => {
        speakAccessibility('Voice feedback test', { force: true, kind: 'test', cancelPrevious: true });
        accessibilityStatus.textContent = speechSupported() ? 'Test voice sent' : 'Voice feedback is not supported by this browser';
      });
    }

    function setControlText(element, value) {
      if (element) element.textContent = value || '--';
    }

    function setControlStateText(value) {
      announceStatusChange(value);
      setControlText(controlState, value);
      setControlText(controlBottomState, value);
    }

    function setControlModeText(value) {
      setControlText(controlMode, value);
    }

    function setControlPitchText(value) {
      setControlText(controlPitch, value);
      if (controlModePitch) controlModePitch.textContent = value ? `Pitch ${value}` : '--';
    }

    function setControlRpmText(value) {
      setControlText(controlRpm, value);
      setControlText(controlBottomRpm, value);
    }

    function setControlMessageText(value) {
      announceControlMessage(value);
      setControlText(controlMessage, value);
    }

    function setControlAxisText(axisElement, value, axisName = '') {
      setControlText(axisElement, value);
      if (axisName) announceAxisPosition(axisName, value);
    }

    function setControlExtraVisible(visible) {
      if (!controlStatusPanel) return;
      controlStatusPanel.classList.toggle('control-extra-visible', visible);
      if (controlExtraToggle) controlExtraToggle.setAttribute('aria-expanded', visible ? 'true' : 'false');
    }

    function toggleControlExtraInfo() {
      if (!controlMobileMedia.matches || !controlStatusPanel) return;
      setControlExtraVisible(!controlStatusPanel.classList.contains('control-extra-visible'));
    }

    function setupControlExtraToggle() {
      if (!controlExtraToggle) return;
      controlExtraToggle.addEventListener('click', event => {
        event.preventDefault();
        toggleControlExtraInfo();
      });
      controlExtraToggle.addEventListener('keydown', event => {
        if (event.key !== 'Enter' && event.key !== ' ') return;
        event.preventDefault();
        toggleControlExtraInfo();
      });
      if (controlMobileMedia.addEventListener) {
        controlMobileMedia.addEventListener('change', () => setControlExtraVisible(false));
      } else if (controlMobileMedia.addListener) {
        controlMobileMedia.addListener(() => setControlExtraVisible(false));
      }
    }

    function setActiveControlPanel(panelName, scrollToPanel = false, announce = false) {
      const activePanel = controlPanelElements.some(panel => panel.dataset.controlPanel === panelName) ? panelName : 'jog';
      controlPanelElements.forEach(panel => {
        panel.classList.toggle('active', panel.dataset.controlPanel === activePanel);
      });
      controlMobileTabs.forEach(tab => {
        const active = tab.dataset.controlPanelTab === activePanel;
        tab.classList.toggle('active', active);
        tab.setAttribute('aria-selected', active ? 'true' : 'false');
      });
      if (scrollToPanel) {
        const panel = controlPanelElements.find(element => element.dataset.controlPanel === activePanel);
        if (panel && panel.scrollIntoView) panel.scrollIntoView({ block: 'start', behavior: 'smooth' });
      }
      if (announce) announcePanelChange(activePanel);
    }

    function setupControlPanelTabs() {
      controlMobileTabs.forEach(tab => {
        tab.setAttribute('role', 'tab');
        tab.addEventListener('click', event => {
          event.preventDefault();
          setActiveControlPanel(tab.dataset.controlPanelTab, false, true);
        });
      });
      setActiveControlPanel('jog');
    }

    function activateControlShortcut(element) {
      if (element.classList.contains('disabled')) return;
      const panelName = element.dataset.controlShortcutPanel;
      if (panelName) {
        setActiveControlPanel(panelName, true, true);
        controlActionStatus.textContent = `${panelName === 'stops' ? 'Stops' : 'Modes'} opened`;
        return;
      }
      const actionCode = element.dataset.controlShortcutAction;
      if (actionCode) {
        pulseControlAction(actionCode, element.dataset.controlShortcutLabel || element.textContent.trim(), element);
      }
    }

    function setupControlStatusShortcuts() {
      controlShortcutElements.forEach(element => {
        element.addEventListener('click', event => {
          event.preventDefault();
          activateControlShortcut(element);
        });
        element.addEventListener('keydown', event => {
          if (event.key !== 'Enter' && event.key !== ' ') return;
          event.preventDefault();
          activateControlShortcut(element);
        });
      });
    }

    function fullscreenElement() {
      return document.fullscreenElement || document.webkitFullscreenElement || null;
    }

    function fullscreenSupported() {
      return !!(document.documentElement.requestFullscreen || document.documentElement.webkitRequestFullscreen);
    }

    function wakeLockSupported() {
      return !!(navigator.wakeLock && navigator.wakeLock.request);
    }

    function stopControlNoSleepFallback() {
      clearInterval(controlNoSleepDrawTimer);
      controlNoSleepDrawTimer = 0;
      if (!controlNoSleepVideo) return;
      controlNoSleepVideo.pause();
      if (controlNoSleepVideo.srcObject) {
        controlNoSleepVideo.srcObject.getTracks().forEach(track => track.stop());
      }
      controlNoSleepVideo.remove();
      controlNoSleepVideo = null;
    }

    function startControlNoSleepFallback() {
      if (controlNoSleepVideo) return;
      const canvas = document.createElement('canvas');
      if (!canvas.captureStream) return;
      canvas.width = 2;
      canvas.height = 2;
      const context = canvas.getContext('2d');
      if (!context) return;
      const drawFrame = () => {
        const dark = Math.floor(Date.now() / 1000) % 2 === 0;
        context.fillStyle = dark ? '#000' : '#111';
        context.fillRect(0, 0, 2, 2);
      };
      drawFrame();
      const video = document.createElement('video');
      video.muted = true;
      video.playsInline = true;
      video.setAttribute('playsinline', '');
      video.setAttribute('aria-hidden', 'true');
      video.style.cssText = 'position:fixed;left:0;bottom:0;width:1px;height:1px;opacity:0.01;pointer-events:none;';
      video.srcObject = canvas.captureStream(1);
      document.body.appendChild(video);
      controlNoSleepVideo = video;
      controlNoSleepDrawTimer = setInterval(drawFrame, 15000);
      const playResult = video.play();
      if (playResult && playResult.catch) playResult.catch(stopControlNoSleepFallback);
    }

    function releaseControlWakeLock() {
      stopControlNoSleepFallback();
      if (!controlWakeLock) return;
      const lock = controlWakeLock;
      controlWakeLock = null;
      if (lock.release) lock.release().catch(() => {});
    }

    function requestControlWakeLock() {
      if (!fullscreenElement() || document.hidden || controlWakeLock || controlWakeLockBusy) return;
      if (!wakeLockSupported()) {
        startControlNoSleepFallback();
        return;
      }
      controlWakeLockBusy = true;
      navigator.wakeLock.request('screen')
        .then(lock => {
          controlWakeLock = lock;
          stopControlNoSleepFallback();
          lock.addEventListener('release', () => {
            if (controlWakeLock === lock) controlWakeLock = null;
          });
        })
        .catch(() => {
          if (fullscreenElement() && !document.hidden) startControlNoSleepFallback();
        })
        .then(() => {
          controlWakeLockBusy = false;
        });
    }

    function syncControlWakeLock() {
      if (fullscreenElement() && !document.hidden) requestControlWakeLock();
      else releaseControlWakeLock();
    }

    function updateFullscreenButton() {
      if (!controlFullscreenButton) return;
      const supported = fullscreenSupported();
      const active = !!fullscreenElement();
      document.body.classList.toggle('control-fullscreen-active', active);
      controlFullscreenButton.disabled = !supported;
      controlFullscreenButton.classList.toggle('disabled', !supported);
      controlFullscreenButton.textContent = active ? 'Exit' : 'Full';
      controlFullscreenButton.setAttribute('aria-label', active ? 'Exit fullscreen' : 'Fullscreen');
    }

    function afterFullscreenChange() {
      updateFullscreenButton();
      syncControlWakeLock();
    }

    function toggleFullscreen() {
      if (!fullscreenSupported()) return;
      let result;
      const enteringFullscreen = !fullscreenElement();
      if (!enteringFullscreen) {
        if (document.exitFullscreen) result = document.exitFullscreen();
        else if (document.webkitExitFullscreen) result = document.webkitExitFullscreen();
      } else if (document.documentElement.requestFullscreen) {
        result = document.documentElement.requestFullscreen();
      } else if (document.documentElement.webkitRequestFullscreen) {
        result = document.documentElement.webkitRequestFullscreen();
      }
      if (enteringFullscreen) startControlNoSleepFallback();
      updateFullscreenButton();
      if (enteringFullscreen) setTimeout(syncControlWakeLock, 1000);
      if (result && result.then) {
        result.then(afterFullscreenChange).catch(() => {
          updateFullscreenButton();
          syncControlWakeLock();
        });
      }
    }

    if (controlFullscreenButton) {
      controlFullscreenButton.addEventListener('click', event => {
        event.preventDefault();
        toggleFullscreen();
      });
      document.addEventListener('fullscreenchange', afterFullscreenChange);
      document.addEventListener('webkitfullscreenchange', afterFullscreenChange);
      document.addEventListener('visibilitychange', syncControlWakeLock);
      updateFullscreenButton();
    }

    setupAccessibilitySettings();
    setupControlExtraToggle();
    setupControlPanelTabs();
    setupControlStatusShortcuts();
    showSection(sectionFromHash());
    window.addEventListener('hashchange', () => handleSectionChange(true));

    const ws = new WebSocket(`ws://${window.location.host.split(':')[0]}:81`);

    ws.onopen = () => {
      logMessage('Connected to server');
      setControlStateText('Connected');
      setControlMessageText('Connected');
      controlActionStatus.textContent = 'Connected';
      updateButtonStates();
      ws.send('?sysinfo');
    };

    ws.onmessage = (event) => {
      const handled = handleRealtimeMessage(event.data);
      if (!handled) logMessage('Received: ' + event.data);
    };

    ws.onclose = () => {
      releaseAllControlActions(false);
      setControlStateText('Disconnected');
      setControlMessageText('Disconnected');
      controlActionStatus.textContent = 'Disconnected';
      updateButtonStates();
      logMessage('Disconnected from server');
    };

    function updateButtonStates() {
      const uploadInProgress = tftUploadInProgress || firmwareUploadInProgress;
      sendButton.disabled = commandInput.value.trim().length < 1 || uploadInProgress;
      addGcodeButton.disabled = gcodeNameInput.value.trim().length < 2 || gcodeContentInput.value.trim().length < 2 || uploadInProgress;
      saveConfigButton.disabled = uploadInProgress;
      resetConfigButton.disabled = uploadInProgress;
      saveWifiButton.disabled = uploadInProgress;
      resetWifiButton.disabled = uploadInProgress;
      saveKeyboardButton.disabled = uploadInProgress;
      resetKeyboardButton.disabled = uploadInProgress;
      tftFirstUploadCheckbox.disabled = uploadInProgress;
      tftFileInput.disabled = uploadInProgress;
      firmwareFileInput.disabled = uploadInProgress;
      const controlDisabled = uploadInProgress || ws.readyState !== WebSocket.OPEN;
      controlButtons.forEach(button => {
        button.disabled = controlDisabled;
        button.classList.toggle('disabled', controlDisabled);
      });
      controlShortcutElements.forEach(element => {
        const disabled = !!element.dataset.controlShortcutAction && controlDisabled;
        element.classList.toggle('disabled', disabled);
        element.setAttribute('aria-disabled', disabled ? 'true' : 'false');
      });
      sendButton.classList.toggle('disabled', sendButton.disabled);
      addGcodeButton.classList.toggle('disabled', addGcodeButton.disabled);
      saveConfigButton.classList.toggle('disabled', saveConfigButton.disabled);
      resetConfigButton.classList.toggle('disabled', resetConfigButton.disabled);
      saveWifiButton.classList.toggle('disabled', saveWifiButton.disabled);
      resetWifiButton.classList.toggle('disabled', resetWifiButton.disabled);
      saveKeyboardButton.classList.toggle('disabled', saveKeyboardButton.disabled);
      resetKeyboardButton.classList.toggle('disabled', resetKeyboardButton.disabled);
      document.querySelectorAll('.keyboard-learn').forEach(button => {
        button.disabled = uploadInProgress;
        button.classList.toggle('disabled', button.disabled);
      });
      tftBrowseButton.classList.toggle('disabled', uploadInProgress);
      firmwareBrowseButton.classList.toggle('disabled', uploadInProgress);
    }

    function websocketReady() {
      return ws.readyState === WebSocket.OPEN;
    }

    function setControlButtonsActive(actionCode, active) {
      document.querySelectorAll(`[data-control-action="${actionCode}"]`).forEach(button => {
        button.classList.toggle('active', active);
      });
    }

    function sendWebControlAction(actionCode, isPress) {
      if (!websocketReady()) {
        controlActionStatus.textContent = 'Disconnected';
        announceWarning('Disconnected from server');
        updateButtonStates();
        return false;
      }
      ws.send(`@${actionCode}:${isPress ? 1 : 0}\n`);
      return true;
    }

    function controlButtonLabel(button) {
      if (!button) return '';
      return button.dataset.controlLabel || button.getAttribute('aria-label') || button.textContent.trim();
    }

    function startControlHoldHeartbeat() {
      if (controlHoldHeartbeatTimer) return;
      controlHoldHeartbeatTimer = setInterval(() => {
        if (!websocketReady()) {
          releaseAllControlActions(false);
          return;
        }
        activeControlActions.forEach(actionCode => {
          sendWebControlAction(actionCode, true);
        });
      }, 200);
    }

    function stopControlHoldHeartbeatIfIdle() {
      if (activeControlActions.size > 0) return;
      clearInterval(controlHoldHeartbeatTimer);
      controlHoldHeartbeatTimer = 0;
    }

    function pressControlButton(button) {
      if (button.disabled) return;
      const actionCode = button.dataset.controlAction;
      if (!actionCode || activeControlActions.has(actionCode)) return;
      if (sendWebControlAction(actionCode, true)) {
        activeControlActions.add(actionCode);
        setControlButtonsActive(actionCode, true);
        startControlHoldHeartbeat();
        const label = controlButtonLabel(button);
        controlActionStatus.textContent = `${label} pressed`;
        announceButtonAction(actionAnnouncementForSpeech(actionCode, label), 'pressed');
        updateWebUiNumpadTracking(actionCode);
        updateHoldAnnouncementTimer();
      }
    }

    function releaseControlAction(actionCode, sendRelease) {
      if (!activeControlActions.has(actionCode)) return;
      activeControlActions.delete(actionCode);
      setControlButtonsActive(actionCode, false);
      if (sendRelease) sendWebControlAction(actionCode, false);
      stopControlHoldHeartbeatIfIdle();
      updateHoldAnnouncementTimer();
      if (sendRelease) {
        const button = document.querySelector(`[data-control-action="${actionCode}"]`);
        announceButtonAction(actionAnnouncementForSpeech(actionCode, controlButtonLabel(button)), 'released');
      }
    }

    function releaseControlButton(button, sendRelease) {
      const actionCode = button.dataset.controlAction;
      if (actionCode) releaseControlAction(actionCode, sendRelease);
    }

    function releaseAllControlActions(sendRelease = true) {
      Array.from(activeControlActions).forEach(actionCode => releaseControlAction(actionCode, sendRelease));
    }

    function pulseControlAction(actionCode, label, activeElement) {
      if (!actionCode || !sendWebControlAction(actionCode, true)) return false;
      if (activeElement) activeElement.classList.add('active');
      controlActionStatus.textContent = `${label} sent`;
      announceButtonAction(actionAnnouncementForSpeech(actionCode, label));
      updateWebUiNumpadTracking(actionCode);
      setTimeout(() => {
        sendWebControlAction(actionCode, false);
        if (activeElement) activeElement.classList.remove('active');
      }, 70);
      return true;
    }

    function pulseControlButton(button) {
      if (button.disabled) return;
      const actionCode = button.dataset.controlAction;
      pulseControlAction(actionCode, controlButtonLabel(button), button);
    }

    function setupControlButtons() {
      controlButtons.forEach(button => {
        if (button.dataset.controlHold === '1') {
          button.addEventListener('pointerdown', event => {
            if (event.button !== undefined && event.button !== 0) return;
            event.preventDefault();
            if (button.setPointerCapture) button.setPointerCapture(event.pointerId);
            pressControlButton(button);
          });
          button.addEventListener('pointerup', event => {
            event.preventDefault();
            releaseControlButton(button, true);
          });
          button.addEventListener('pointercancel', () => releaseControlButton(button, true));
          button.addEventListener('lostpointercapture', () => releaseControlButton(button, true));
          button.addEventListener('pointerleave', event => {
            if (event.pointerType === 'mouse') releaseControlButton(button, true);
          });
        } else {
          button.addEventListener('click', event => {
            event.preventDefault();
            pulseControlButton(button);
          });
        }
      });
      window.addEventListener('blur', () => releaseAllControlActions(true));
      window.addEventListener('beforeunload', () => releaseAllControlActions(true));
      document.addEventListener('visibilitychange', () => {
        if (document.hidden) releaseAllControlActions(true);
      });
    }

    function updateControlStatusFromText(line) {
      if (!line.startsWith('<') || !line.endsWith('>')) return false;
      const parts = line.substring(1, line.length - 1).split('|');
      setControlStateText(parts[0]);
      const wpos = parts.find(part => part.startsWith('WPos:'));
      if (wpos) {
        const values = wpos.substring('WPos:'.length).split(',');
        setControlAxisText(controlX, values[0], 'X');
        setControlAxisText(controlZ, values[2], 'Z');
      }
      const fs = parts.find(part => part.startsWith('FS:'));
      if (fs) {
        const values = fs.substring('FS:'.length).split(',');
        setControlRpmText(values[1]);
      }
      return true;
    }

    function updateControlStatusLine(line) {
      const index = line.indexOf('=');
      if (index <= 3 || !line.startsWith('UI.')) return false;
      const key = line.substring(3, index);
      const value = line.substring(index + 1);
      if (key === 'status') setControlStateText(value);
      else if (key === 'mode') setControlModeText(value);
      else if (key === 'pitch') setControlPitchText(value);
      else if (key === 'measure') setControlText(controlMeasure, value);
      else if (key === 'step') setControlText(controlStep, value);
      else if (key === 'turns') setControlText(controlTurns, value);
      else if (key === 'angle') setControlText(controlAngle, value);
      else if (key === 'x') setControlAxisText(controlX, value, 'X');
      else if (key === 'xLeft') controlXStops.dataset.left = value;
      else if (key === 'xRight') controlXStops.dataset.right = value;
      else if (key === 'y') setControlAxisText(controlY, value, 'Y');
      else if (key === 'yLeft') controlYStops.dataset.left = value;
      else if (key === 'yRight') controlYStops.dataset.right = value;
      else if (key === 'z') setControlAxisText(controlZ, value, 'Z');
      else if (key === 'zLeft') controlZStops.dataset.left = value;
      else if (key === 'zRight') controlZStops.dataset.right = value;
      else if (key === 'rpm') setControlRpmText(value);
      else if (key === 'message') setControlMessageText(value);
      updateAxisStopDisplays(controlXStops, controlXInlineStops);
      updateAxisStopDisplays(controlYStops, controlYInlineStops);
      updateAxisStopDisplays(controlZStops, controlZInlineStops);
      return true;
    }

    function updateStopDisplay(element) {
      if (!element) return;
      const left = element.dataset.left || '';
      const right = element.dataset.right || '';
      element.textContent = left || right ? `${left || '-'} / ${right || '-'}` : '--';
    }

    function updateInlineStopDisplay(element, left, right) {
      setControlText(element, left || right ? `${left || '-'} / ${right || '-'}` : '');
    }

    function updateAxisStopDisplays(statusElement, inlineElement) {
      if (!statusElement) return;
      const left = statusElement.dataset.left || '';
      const right = statusElement.dataset.right || '';
      updateStopDisplay(statusElement);
      updateInlineStopDisplay(inlineElement, left, right);
    }

    function applyControlConfigValues(values) {
      const showY = values.activeY === '1';
      controlYElements.forEach(element => {
        element.hidden = !showY;
      });
      if (controlStatusPanel) controlStatusPanel.classList.toggle('control-y-active', showY);
    }

    function parseStatusValue(data, key) {
      const prefix = key + '=';
      const line = data.split('\n').find(l => l.startsWith(prefix));
      return line ? line.substring(prefix.length) : '';
    }

    function parseKeyValueText(data) {
      const values = {};
      data.split('\n').forEach(line => {
        const index = line.indexOf('=');
        if (index > 0) {
          values[line.substring(0, index)] = line.substring(index + 1);
        }
      });
      return values;
    }

    function updateFirmwareProgress(percent, message) {
      if (!firmwareUploadInProgress && percent < 100) return;
      firmwareProgress.hidden = false;
      firmwareProgress.value = Math.max(Number(firmwareProgress.value) || 0, Math.min(100, Math.max(0, percent)));
      if (message) firmwareStatus.textContent = message;
    }

    function updateFirmwareStatusFromText(data) {
      const size = Number(parseStatusValue(data, 'FW.size'));
      const uploaded = Number(parseStatusValue(data, 'FW.uploaded'));
      const message = parseStatusValue(data, 'FW.message');
      if (size > 0 && uploaded >= 0) {
        updateFirmwareProgress(Math.round(uploaded * 100 / size), message || `Writing firmware ${Math.round(uploaded * 100 / size)}%`);
      } else if (message) {
        firmwareStatus.textContent = message;
      }
    }

    function pollFirmwareStatus() {
      if (firmwareStatusPollBusy) return;
      firmwareStatusPollBusy = true;
      fetch(`/status?ts=${Date.now()}`, { cache: 'no-store' })
        .then(response => response.text())
        .then(updateFirmwareStatusFromText)
        .catch(() => {})
        .then(() => {
          firmwareStatusPollBusy = false;
        });
    }

    function startFirmwareStatusPolling() {
      clearInterval(firmwareStatusPollTimer);
      firmwareStatusPollTimer = setInterval(pollFirmwareStatus, 1000);
    }

    function stopFirmwareStatusPolling() {
      clearInterval(firmwareStatusPollTimer);
      firmwareStatusPollTimer = 0;
    }

    function formatScaledConfigValue(value) {
      if (!Number.isFinite(value)) return '';
      return value.toFixed(4).replace(/\.?0+$/, '');
    }

    function configValueToDisplay(field, value) {
      if (field.firmwareScale) {
        return formatScaledConfigValue(Number(value) / field.firmwareScale);
      }
      return value;
    }

    function configValueToFirmware(field, value) {
      const trimmed = value.trim();
      if (field.firmwareScale) {
        const parsed = Number(trimmed);
        return Number.isFinite(parsed) ? String(Math.round(parsed * field.firmwareScale)) : trimmed;
      }
      return trimmed;
    }

    function addConfigHelp(row, field) {
      if (!field.help) return;
      const help = document.createElement('small');
      help.className = 'config-help';
      help.textContent = field.help;
      row.appendChild(help);
    }

    function renderConfigFields() {
      configFields.innerHTML = '';
      machineConfigSections.forEach((section, index) => {
        const sectionElement = document.createElement('details');
        sectionElement.className = 'config-section settings-group';
        const summary = document.createElement('summary');
        summary.textContent = `${section.title} (${section.fields.length})`;
        const grid = document.createElement('div');
        grid.className = 'config-grid';
        section.fields.forEach(field => {
          const id = `config-${field.key}`;
          const row = document.createElement('div');
          row.className = field.type === 'checkbox' ? 'config-field config-checkbox-field' : 'config-field';
          if (field.type === 'checkbox') {
            const label = document.createElement('label');
            label.className = 'config-checkbox';
            label.htmlFor = id;
            const input = document.createElement('input');
            input.type = 'checkbox';
            input.id = id;
            input.dataset.key = field.key;
            const text = document.createElement('span');
            text.textContent = field.label;
            label.appendChild(input);
            label.appendChild(text);
            row.appendChild(label);
            addConfigHelp(row, field);
          } else {
            const label = document.createElement('label');
            label.className = 'config-label';
            label.htmlFor = id;
            label.textContent = field.label;
            if (field.unit) {
              const unit = document.createElement('span');
              unit.className = 'config-unit';
              unit.textContent = ` (${field.unit})`;
              label.appendChild(unit);
            }
            const input = document.createElement('input');
            input.type = 'number';
            input.id = id;
            input.dataset.key = field.key;
            input.min = field.min;
            input.max = field.max;
            input.step = field.step;
            input.required = true;
            row.appendChild(label);
            row.appendChild(input);
            addConfigHelp(row, field);
          }
          grid.appendChild(row);
        });
        sectionElement.appendChild(summary);
        sectionElement.appendChild(grid);
        configFields.appendChild(sectionElement);
      });
    }

    function applyConfigValues(values) {
      machineConfigFields.forEach(field => {
        const input = document.getElementById(`config-${field.key}`);
        if (!input || !(field.key in values)) return;
        if (field.type === 'checkbox') {
          input.checked = values[field.key] === '1';
        } else {
          input.value = configValueToDisplay(field, values[field.key]);
        }
      });
    }

    function loadConfig() {
      configStatus.textContent = 'Loading machine config...';
      fetch('/config', { cache: 'no-store' })
        .then(response => response.text())
        .then(data => {
          const values = parseKeyValueText(data);
          applyConfigValues(values);
          applyControlConfigValues(values);
          configStatus.textContent = '';
        })
        .catch(() => {
          configStatus.textContent = 'Failed to load machine config';
        });
    }

    function collectConfigValues() {
      const values = new URLSearchParams();
      machineConfigFields.forEach(field => {
        const input = document.getElementById(`config-${field.key}`);
        values.append(field.key, field.type === 'checkbox' ? (input.checked ? '1' : '0') : configValueToFirmware(field, input.value));
      });
      return values;
    }

    function saveConfig(event) {
      event.preventDefault();
      configStatus.textContent = 'Saving machine config...';
      fetch('/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: collectConfigValues()
      })
      .then(response => response.text().then(text => ({ ok: response.ok, text })))
      .then(result => {
        configStatus.textContent = result.text;
        logMessage(result.text);
        if (result.ok) {
          waitForControllerReload();
        }
      })
      .catch(() => {
        configStatus.textContent = 'Machine config save failed';
      });
    }

    function resetConfig() {
      if (!confirm('Reset machine config to firmware defaults and restart the controller?')) return;
      configStatus.textContent = 'Resetting machine config...';
      fetch('/config/reset', { method: 'POST' })
        .then(response => response.text().then(text => ({ ok: response.ok, text })))
        .then(result => {
          configStatus.textContent = result.text;
          logMessage(result.text);
          if (result.ok) {
            waitForControllerReload();
          }
        })
        .catch(() => {
          configStatus.textContent = 'Machine config reset failed';
        });
    }

    function renderKeyboardFields() {
      keyboardFields.innerHTML = '';
      keyboardBindingSections.forEach((section, index) => {
        const sectionElement = document.createElement('details');
        sectionElement.className = 'config-section keyboard-group';
        sectionElement.open = index === 0;
        const summary = document.createElement('summary');
        summary.textContent = `${section.title} (${section.fields.length})`;
        const grid = document.createElement('div');
        grid.className = 'keyboard-grid';
        section.fields.forEach(field => {
          const id = `keyboard-${field.key}`;
          const row = document.createElement('div');
          row.className = 'keyboard-binding';

          const label = document.createElement('label');
          label.htmlFor = id;
          label.textContent = field.label;

          const input = document.createElement('input');
          input.type = 'number';
          input.id = id;
          input.dataset.key = field.key;
          input.min = 1;
          input.max = 255;
          input.step = 1;
          input.required = true;

          const learnButton = document.createElement('button');
          learnButton.type = 'button';
          learnButton.className = 'secondary keyboard-learn';
          learnButton.dataset.key = field.key;
          learnButton.textContent = 'Learn';
          learnButton.addEventListener('click', () => beginKeyboardLearn(field));

          row.appendChild(label);
          row.appendChild(input);
          row.appendChild(learnButton);
          grid.appendChild(row);
        });
        sectionElement.appendChild(summary);
        sectionElement.appendChild(grid);
        keyboardFields.appendChild(sectionElement);
      });
    }

    function applyKeyboardValues(values) {
      keyboardShowKeysInput.checked = values.showKeyPresses === '1';
      keyboardBindingFields.forEach(field => {
        const input = document.getElementById(`keyboard-${field.key}`);
        if (input && field.key in values) input.value = values[field.key];
      });
    }

    function loadKeyboardConfig() {
      keyboardStatus.textContent = 'Loading keyboard config...';
      fetch('/keyboard-config', { cache: 'no-store' })
        .then(response => response.text())
        .then(data => {
          applyKeyboardValues(parseKeyValueText(data));
          keyboardStatus.textContent = '';
        })
        .catch(() => {
          keyboardStatus.textContent = 'Failed to load keyboard config';
        });
    }

    function collectKeyboardConfigValues() {
      const values = new URLSearchParams();
      values.append('showKeyPresses', keyboardShowKeysInput.checked ? '1' : '0');
      keyboardBindingFields.forEach(field => {
        const input = document.getElementById(`keyboard-${field.key}`);
        values.append(field.key, input.value.trim());
      });
      return values;
    }

    function stopKeyboardCaptureRequest() {
      fetch('/keyboard-capture', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ enabled: '0' })
      }).catch(() => {});
    }

    function clearKeyboardLearn() {
      clearTimeout(keyboardLearnTimer);
      keyboardLearnTimer = 0;
      keyboardLearnTarget = '';
    }

    function saveKeyboardConfig(event) {
      event.preventDefault();
      clearKeyboardLearn();
      stopKeyboardCaptureRequest();
      keyboardStatus.textContent = 'Saving keyboard config...';
      fetch('/keyboard-config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: collectKeyboardConfigValues()
      })
      .then(response => response.text().then(text => ({ ok: response.ok, text })))
      .then(result => {
        keyboardStatus.textContent = result.text;
        logMessage(result.text);
      })
      .catch(() => {
        keyboardStatus.textContent = 'Keyboard config save failed';
      });
    }

    function resetKeyboardConfig() {
      if (!confirm('Reset keyboard mapping to firmware defaults?')) return;
      clearKeyboardLearn();
      stopKeyboardCaptureRequest();
      keyboardStatus.textContent = 'Resetting keyboard config...';
      fetch('/keyboard-config/reset', { method: 'POST' })
        .then(response => response.text().then(text => ({ ok: response.ok, text })))
        .then(result => {
          keyboardStatus.textContent = result.text;
          logMessage(result.text);
          if (result.ok) loadKeyboardConfig();
        })
        .catch(() => {
          keyboardStatus.textContent = 'Keyboard config reset failed';
        });
    }

    function beginKeyboardLearn(field) {
      clearKeyboardLearn();
      keyboardLearnTarget = field.key;
      keyboardStatus.textContent = `Learning ${field.label}: press a physical keyboard key`;
      keyboardLearnTimer = setTimeout(() => {
        keyboardLearnTarget = '';
        keyboardStatus.textContent = 'Keyboard capture timed out';
        stopKeyboardCaptureRequest();
      }, 30000);
      fetch('/keyboard-capture', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ enabled: '1' })
      })
      .then(response => response.text().then(text => ({ ok: response.ok, text })))
      .then(result => {
        if (!result.ok) {
          clearKeyboardLearn();
          keyboardStatus.textContent = result.text;
        }
      })
      .catch(() => {
        clearKeyboardLearn();
        keyboardStatus.textContent = 'Keyboard capture failed';
      });
    }

    function handleKeyboardPressCode(code) {
      keyboardLastKey.textContent = String(code);
      if (!keyboardLearnTarget) return;
      const input = document.getElementById(`keyboard-${keyboardLearnTarget}`);
      if (input) {
        input.value = String(code);
        const field = keyboardBindingFields.find(item => item.key === keyboardLearnTarget);
        keyboardStatus.textContent = `${field ? field.label : keyboardLearnTarget} set to key code ${code}`;
      }
      clearKeyboardLearn();
    }

    function applyWifiValues(values) {
      wifiEnabledInput.checked = values.wifiEnabled === '1';
      wifiSsidInput.value = values.wifiSsid || '';
      wifiHostnameInput.value = values.wifiHostname || '';
      wifiPasswordInput.value = '';
      wifiPasswordInput.placeholder = values.wifiPasswordSet === '1' ? 'Leave unchanged' : 'Password';
      wifiClearPasswordInput.checked = false;
      const mode = values.wifiMode || 'unknown';
      let statusText = '';
      if (mode === 'station') {
        statusText = `Connected (IP: ${values.wifiIp || 'unknown'})`;
      } else if (mode === 'setup-ap') {
        statusText = `Setup AP active (SSID: ${values.setupApSsid || 'unknown'})`;
      } else if (mode === 'connecting') {
        statusText = 'Connecting...';
      } else if (mode === 'off') {
        statusText = 'Disabled';
      } else {
        statusText = `Unknown state (${mode})`;
      }
      wifiStatusElement.textContent = statusText;
    }

    function loadWifi() {
      wifiStatusElement.textContent = 'Loading WiFi config...';
      fetch('/wifi', { cache: 'no-store' })
        .then(response => response.text())
        .then(data => {
          applyWifiValues(parseKeyValueText(data));
        })
        .catch(() => {
          wifiStatusElement.textContent = 'Failed to load WiFi config';
        });
    }

    function collectWifiValues() {
      return new URLSearchParams({
        wifiEnabled: wifiEnabledInput.checked ? '1' : '0',
        wifiSsid: wifiSsidInput.value.trim(),
        wifiHostname: wifiHostnameInput.value.trim(),
        wifiPassword: wifiPasswordInput.value,
        wifiClearPassword: wifiClearPasswordInput.checked ? '1' : '0'
      });
    }

    function saveWifi(event) {
      event.preventDefault();
      wifiStatusElement.textContent = 'Saving WiFi config...';
      fetch('/wifi', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: collectWifiValues()
      })
      .then(response => response.text().then(text => ({ ok: response.ok, text })))
      .then(result => {
        wifiStatusElement.textContent = result.text;
        logMessage(result.text);
        if (result.ok) {
          waitForControllerReload();
        }
      })
      .catch(() => {
        wifiStatusElement.textContent = 'WiFi config save failed';
      });
    }

    function resetWifi() {
      if (!confirm('Forget saved WiFi network and restart setup AP?')) return;
      wifiStatusElement.textContent = 'Resetting WiFi config...';
      fetch('/wifi/reset', { method: 'POST' })
        .then(response => response.text().then(text => ({ ok: response.ok, text })))
        .then(result => {
          wifiStatusElement.textContent = result.text;
          logMessage(result.text);
          if (result.ok) {
            waitForControllerReload();
          }
        })
        .catch(() => {
          wifiStatusElement.textContent = 'WiFi config reset failed';
        });
    }

    function handleRealtimeMessage(message) {
      let handled = false;
      message.split('\n').map(line => line.trim()).filter(line => !!line).forEach(line => {
        if (updateControlStatusLine(line)) {
          handled = true;
        } else if (updateControlStatusFromText(line)) {
          handled = true;
        } else if (line.startsWith('WEBUI.error=')) {
          const text = line.substring('WEBUI.error='.length);
          controlActionStatus.textContent = text;
          announceWarning(text);
          handled = true;
        } else if (line.startsWith('WEBUI.warning=')) {
          const text = line.substring('WEBUI.warning='.length);
          controlActionStatus.textContent = text;
          announceWarning(text);
          handled = true;
        } else if (line.startsWith('KEY.press=')) {
          const code = Number(line.substring('KEY.press='.length));
          if (!Number.isNaN(code)) handleKeyboardPressCode(code);
          handled = true;
        } else if (line.startsWith('KEY.release=') || line.startsWith('KEY.action=')) {
          handled = true;
        } else if (line.startsWith('FW: progress ')) {
          const percent = Number(line.substring('FW: progress '.length).replace('%', ''));
          if (!Number.isNaN(percent)) {
            updateFirmwareProgress(percent, `Writing firmware ${percent}%`);
          }
          handled = true;
        } else if (line === 'FW: upload complete, restarting controller') {
          updateFirmwareProgress(100, 'Firmware upload complete. Restarting controller...');
          handled = true;
        } else if (line.startsWith('FW: error:')) {
          firmwareStatus.textContent = line.substring('FW: '.length);
          announceWarning(firmwareStatus.textContent);
          handled = true;
        } else if (line.startsWith('SYSINFO.')) {
          const parts = line.split('=');
          if (parts.length >= 2) {
            const key = parts[0];
            const val = parts.slice(1).join('=');
            if (key === 'SYSINFO.version') {
              if (sysinfoVersion) sysinfoVersion.textContent = val;
              const footerVer = document.getElementById('footer-version');
              if (footerVer) footerVer.textContent = ' ' + val;
            }
            if (key === 'SYSINFO.chip' && sysinfoChip) sysinfoChip.textContent = val;
            if (key === 'SYSINFO.flash' && sysinfoFlash) sysinfoFlash.textContent = val;
            if (key === 'SYSINFO.heap' && sysinfoHeap) sysinfoHeap.textContent = val;
          }
          handled = true;
        }
      });
      return handled;
    }

    function waitForControllerReload() {
      clearTimeout(firmwareReloadTimer);
      firmwareReloadTimer = setTimeout(() => {
        fetch(`/status?reload=${Date.now()}`, { cache: 'no-store' })
          .then(response => {
            if (response.ok) {
              window.location.reload();
            } else {
              waitForControllerReload();
            }
          })
          .catch(waitForControllerReload);
      }, 2000);
    }

    function waitForFirmwareReload() {
      waitForControllerReload();
    }

    commandInput.addEventListener('input', updateButtonStates);
    gcodeNameInput.addEventListener('input', updateButtonStates);
    gcodeContentInput.addEventListener('input', updateButtonStates);
    configForm.addEventListener('submit', saveConfig);
    resetConfigButton.addEventListener('click', resetConfig);
    wifiForm.addEventListener('submit', saveWifi);
    resetWifiButton.addEventListener('click', resetWifi);

    if (sysinfoRefreshBtn) {
      sysinfoRefreshBtn.addEventListener('click', () => {
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send('?sysinfo\n');
        }
      });
    }
    keyboardForm.addEventListener('submit', saveKeyboardConfig);
    resetKeyboardButton.addEventListener('click', resetKeyboardConfig);
    tftFirstUploadCheckbox.addEventListener('change', saveTftFirstUploadPreference);
    tftFileInput.addEventListener('change', uploadTftFile);
    firmwareFileInput.addEventListener('change', uploadFirmwareFile);

    document.addEventListener('DOMContentLoaded', () => {
      setupControlButtons();
      loadTftFirstUploadPreference();
      renderConfigFields();
      renderKeyboardFields();
      loadConfig();
      loadWifi();
      loadKeyboardConfig();
      updateButtonStates();
    });

    function loadTftFirstUploadPreference() {
      try {
        const stored = localStorage.getItem(tftFirstUploadStorageKey);
        if (stored === 'true') {
          tftFirstUploadCheckbox.checked = true;
        } else if (stored === 'false') {
          tftFirstUploadCheckbox.checked = false;
        }
      } catch (error) {
      }
    }

    function saveTftFirstUploadPreference() {
      try {
        localStorage.setItem(tftFirstUploadStorageKey, tftFirstUploadCheckbox.checked ? 'true' : 'false');
      } catch (error) {
      }
    }

    function send() {
      const command = commandInput.value.trim();
      if (command) {
        logMessage('Sent: ' + command);
        ws.send(command + '\n');
        commandInput.value = '';
        updateButtonStates();
      }
    }

    commandInput.addEventListener('keydown', (event) => {
      if (event.key === 'Enter') send();
    });

    sendButton.addEventListener('click', () => {
      send();
    });

    function removeComments(content) {
      return content.split('\n').map(line => line.split(';')[0].trim()).filter(line => !!line).join('\n');
    }

    addGcodeButton.addEventListener('click', () => {
      const name = gcodeNameInput.value.trim();
      let content = gcodeContentInput.value.trim();
      if (removeCommentsCheckbox.checked) {
        content = removeComments(content);
      }
      if (name && content) {
        fetch('/gcode/add', {
          method: 'POST',
          headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
          body: new URLSearchParams({ name, gcode: content })
        })
        .then(response => response.text())
        .then(data => {
          logMessage(data);
          listGcodes();
          gcodeNameInput.value = '';
          gcodeContentInput.value = '';
          updateButtonStates();
        });
      }
    });

    function uploadTftFile() {
      const file = tftFileInput.files[0];
      if (!file || tftUploadInProgress) return;
      const nextionBaud = tftFirstUploadCheckbox.checked ? 9600 : 115200;

      const formData = new FormData();
      formData.append('tft', file, file.name);
      const request = new XMLHttpRequest();
      tftUploadInProgress = true;
      tftProgress.value = 0;
      tftProgress.hidden = false;
      tftStatus.textContent = `Uploading ${file.name} from ${nextionBaud} baud. Keep this page open; flashing the display can take several minutes.`;
      updateButtonStates();

      request.upload.onprogress = event => {
        if (event.lengthComputable) {
          tftProgress.value = Math.round(event.loaded * 100 / event.total);
        }
      };
      request.onload = () => {
        tftUploadInProgress = false;
        tftStatus.textContent = request.responseText;
        logMessage(request.responseText);
        tftFileInput.value = '';
        tftProgress.hidden = true;
        updateButtonStates();
      };
      request.onerror = () => {
        tftUploadInProgress = false;
        tftStatus.textContent = 'TFT upload failed';
        logMessage('TFT upload failed');
        announceWarning('TFT upload failed');
        tftFileInput.value = '';
        tftProgress.hidden = true;
        updateButtonStates();
      };
      request.open('POST', `/tft/upload?size=${file.size}&baud=${nextionBaud}`);
      request.send(formData);
    }

    function uploadFirmwareFile() {
      const file = firmwareFileInput.files[0];
      if (!file || tftUploadInProgress || firmwareUploadInProgress) return;
      if (!file.name.toLowerCase().endsWith('.bin')) {
        firmwareStatus.textContent = 'Select a compiled .bin firmware file';
        announceWarning('Select a compiled bin firmware file');
        firmwareFileInput.value = '';
        return;
      }

      const formData = new FormData();
      formData.append('firmware', file, file.name);
      const request = new XMLHttpRequest();
      firmwareUploadInProgress = true;
      firmwareProgress.value = 0;
      firmwareProgress.hidden = false;
      firmwareStatus.textContent = `Uploading ${file.name}. Keep this page open; the controller will restart after a successful upload.`;
      startFirmwareStatusPolling();
      updateButtonStates();

      request.upload.onprogress = event => {
        if (event.lengthComputable) {
          firmwareStatus.textContent = `Sending firmware to controller ${Math.round(event.loaded * 100 / event.total)}%. Waiting for flash progress from controller.`;
        }
      };
      request.onload = () => {
        logMessage(request.responseText);
        firmwareFileInput.value = '';
        stopFirmwareStatusPolling();
        if (request.status >= 200 && request.status < 300) {
          firmwareProgress.value = 100;
          firmwareStatus.textContent = `${request.responseText} Waiting for controller to come back online.`;
          waitForFirmwareReload();
        } else {
          firmwareUploadInProgress = false;
          firmwareStatus.textContent = request.responseText;
          announceWarning(request.responseText);
          firmwareProgress.hidden = true;
          updateButtonStates();
        }
      };
      request.onerror = () => {
        firmwareUploadInProgress = false;
        stopFirmwareStatusPolling();
        firmwareStatus.textContent = 'Firmware upload failed';
        logMessage('Firmware upload failed');
        announceWarning('Firmware upload failed');
        firmwareFileInput.value = '';
        firmwareProgress.hidden = true;
        updateButtonStates();
      };
      request.open('POST', `/firmware/upload?size=${file.size}`);
      request.send(formData);
    }

    function listGcodes() {
      fetch('/gcode/list')
        .then(response => response.text())
        .then(data => {
          gcodeList.innerHTML = '';
          gcodeList.classList.toggle('empty', !data);
          if (data) {
            data.split('\n').map(g => g.trim()).filter(g => !!g).forEach(gcode => {
              const row = document.createElement('div');
              row.className = 'gcode-row';
              row.dataset.name = gcode;
              row.innerHTML = `
                <span class="gcode-item" data-name="${gcode}">${gcode}</span>
                <span class="gcode-size"></span>
                <span class="remove-icon" data-name="${gcode}">&times;</span>
              `;
              row.addEventListener('click', (event) => {
                loadGcode(event.target.dataset.name);
              });
              row.title = 'Click to load G-code';
              gcodeList.appendChild(row);
              fetch(`/gcode/get?name=${encodeURIComponent(gcode)}`)
                .then(response => response.text())
                .then(text => {
                  row.querySelector('.gcode-size').textContent = `${(text.length / 1024).toFixed(1)} KB`;
                })
            });
            document.querySelectorAll('.remove-icon').forEach(icon => {
              icon.title = 'Click to remove G-code';
              icon.addEventListener('click', (event) => {
                const name = event.target.getAttribute('data-name');
                removeGcode(name);
              });
            });
          } else {
            gcodeList.innerHTML = 'No G-code stored';
          }
          fetchFreeSpace();
        });
    }

    function loadGcode(name) {
      fetch(`/gcode/get?name=${encodeURIComponent(name)}`)
        .then(response => response.text())
        .then(data => {
          gcodeNameInput.value = name;
          gcodeContentInput.value = data;
          updateButtonStates();
        });
    }

    function removeGcode(name) {
      fetch('/gcode/remove', {
        method: 'POST',
        headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
        body: new URLSearchParams({ name })
      })
      .then(response => response.text())
      .then(data => {
        logMessage(data);
        listGcodes();
      });
    }

    function fetchFreeSpace() {
      fetch('/status')
        .then(response => response.text())
        .then(data => {
          const freeSpaceBytes = Number(data.split('\n').find(l => l.startsWith('LittleFS.freeSpace=')).substr('LittleFS.freeSpace='.length));
          if (freeSpaceBytes) {
            const freeSpaceElement = document.getElementById('free-space');
            freeSpaceElement.textContent = `Free space: ${Math.floor(freeSpaceBytes / 1024)} KB`;
          }
        });
    }

    function logMessage(message) {
      const p = document.createElement('p');
      p.textContent = message;
      log.appendChild(p);
      log.scrollTop = log.scrollHeight;
    }

    listGcodes();
