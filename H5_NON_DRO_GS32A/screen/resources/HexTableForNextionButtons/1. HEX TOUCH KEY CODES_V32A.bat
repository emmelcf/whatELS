HEX TO KEY CODE - WITH HEXES FOR INPUTTING INTO NEXTION PRESS RELEASE LOGIC


  [0]  = 0,               // printh 65 00 00 01 FF FF FF      NOT IN USE
  [1]  = 0,               // printh 65 00 01 01 FF FF FF      NOT IN USE
  [2]  = 0,               // printh 65 00 02 01 FF FF FF      NOT IN USE
  [3]  = B_OFF,           // printh 65 00 03 01 FF FF FF      OFF (stop, cancel, delete)
  [4]  = B_MODE,          // printh 65 00 04 01 FF FF FF      MODE SELECTION
  [5]  = B_REVERSE,       // printh 65 00 05 01 FF FF FF      REVERSE PITCH (+/- pitch)
  [6]  = B_MEASURE,       // printh 65 00 06 01 FF FF FF      UNITS (metric, imperial, TPI)
  [7]  = B_STEP,          // printh 65 00 07 01 FF FF FF      INCREMENTS - STEPS
  [8]  = B_DISPL,         // printh 65 00 08 01 FF FF FF      TOGGLES RPM/TURNS/ANGLE DISPLAY // GECKOSUB ADDED THIS, it was missing in Stock FW V11
  [9]  = B_OFF,           // printh 65 00 09 01 FF FF FF      tTurns (OUTPUT WHEN PRESSING THE TURN BOX) ???
  [10] = B_OFF,           // printh 65 00 0A 01 FF FF FF      tAngle (OUTPUT WHEN PRESSING THE ANGLE BOX) ???
  [11] = B_X_ENA,         // printh 65 00 0B 01 FF FF FF      ENABLE/DISABLE X-AXIS 
  [12] = B_X,             // printh 65 00 0C 01 FF FF FF      ZERO X-AXIS 
  [13] = 0,               // printh 65 00 0D 01 FF FF FF      NOT IN USE
  [14] = 0,               // printh 65 00 0E 01 FF FF FF      NOT IN USE
  [15] = B_Y_ENA,         // printh 65 00 0F 01 FF FF FF      ENABLE/DISABLE Y-AXIS
  [16] = 0,               // printh 65 00 10 01 FF FF FF      NOT IN USE
  [17] = B_Y,             // printh 65 00 11 01 FF FF FF      ZERO Y-AXIS 
  [18] = 0,               // printh 65 00 12 01 FF FF FF      NOT IN USE
  [19] = B_Z_ENA,         // printh 65 00 13 01 FF FF FF      ENABLE/DISABLE Z-AXIS
  [20] = 0,               // printh 65 00 14 01 FF FF FF      NOT IN USE
  [21] = B_Z,             // printh 65 00 15 01 FF FF FF      ZERO Z-AXIS 
  [22] = 0,               // printh 65 00 16 01 FF FF FF      NOT IN USE
  [23] = B_OFF,           // printh 65 00 17 01 FF FF FF      STOP BUTTON, (cancel, delete, off) 
  [24] = B_BACKSPACE,     // printh 65 00 18 01 FF FF FF      BACKSPACE
  [25] = B_ON,            // printh 65 00 19 01 FF FF FF      PLAY, GO, RUN, ENTER, SET
  [26] = B_0,             // printh 65 00 1A 01 FF FF FF      NUMPAD 0
  [27] = B_1,             // printh 65 00 1B 01 FF FF FF      NUMPAD 1
  [28] = B_2,             // printh 65 00 1C 01 FF FF FF      NUMPAD 2
  [29] = B_3,             // printh 65 00 1D 01 FF FF FF      NUMPAD 3
  [30] = B_4,             // printh 65 00 1E 01 FF FF FF      NUMPAD 4
  [31] = B_5,             // printh 65 00 1F 01 FF FF FF      NUMPAD 5
  [32] = B_6,             // printh 65 00 20 01 FF FF FF      NUMPAD 6
  [33] = B_7,             // printh 65 00 21 01 FF FF FF      NUMPAD 7
  [34] = B_8,             // printh 65 00 22 01 FF FF FF      NUMPAD 8
  [35] = B_9,             // printh 65 00 23 01 FF FF FF      NUMPAD 9
  [36] = B_STOPU,         // printh 65 00 24 01 FF FF FF      STOP/LIMIT UP (X)
  [37] = B_STOPD,         // printh 65 00 25 01 FF FF FF      STOP/LIMIT DOWN (X)
  [38] = B_STOPF,         // printh 65 00 26 01 FF FF FF      STOP/LIMIT FORWARD (Y)
  [39] = B_STOPB,         // printh 65 00 27 01 FF FF FF      STOP/LIMIT BACK (Y)
  [40] = B_STOPL,         // printh 65 00 28 01 FF FF FF      STOP/LIMIT LEFT (Z)
  [41] = B_STOPR,         // printh 65 00 29 01 FF FF FF      STOP/LIMIT RIGHT (Z)
  [42] = B_PLUS,          // printh 65 00 2A 01 FF FF FF      + (PLUS)
  [43] = B_MINUS,         // printh 65 00 2B 01 FF FF FF      - (MINUS)
  [44] = B_UP,            // printh 65 00 2C 01 FF FF FF      JOG UP (X)
  [45] = B_DOWN,          // printh 65 00 2D 01 FF FF FF      JOG DOWN (X)
  [46] = B_FORWARD,       // printh 65 00 2E 01 FF FF FF      JOG FORWARD (Y)
  [47] = B_BACK,          // printh 65 00 2F 01 FF FF FF      JOG BACK (Y)
  [48] = B_LEFT,          // printh 65 00 30 01 FF FF FF      JOG LEFT (Z)
  [49] = B_RIGHT,         // printh 65 00 31 01 FF FF FF      JOG RIGHT (Z)
  [50] = B_MULTISTART,    // printh 65 00 32 01 FF FF FF      MULTI START THREAD

  // the ones below were added by GeckoSub for the custom FW and screenset
  [51] = 0,               // printh 65 00 33 01 FF FF FF      Reserved / Unmapped
  [52] = 0,               // printh 65 00 34 01 FF FF FF      Reserved / Unmapped
  [53] = B_DIA_RAD,       // printh 65 00 35 01 FF FF FF      DRO, dia/rad - switching between diameter and radius mode
  [54] = 0,               // printh 65 00 36 01 FF FF FF      Reserved / Unmapped
  [55] = B_VERSION,       // printh 65 00 37 01 FF FF FF      Toggle show/hide of FW version
  [56] = 0,               // printh 65 00 38 01 FF FF FF      Reserved / Unmapped
  [57] = B_PITCH,         // printh 65 00 39 01 FF FF FF      Pitch Box Button (Font/Reverse)


