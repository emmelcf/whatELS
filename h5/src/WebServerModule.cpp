#include "WebServerModule.h"
#include "Config.h"
#include "Axis.h"
#include "Debug.h"
#include "Input.h"
#include "Display.h"
#include "Motion.h"
#include <LittleFS.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>

CircleBuffer inBuffer = {};
CircleBuffer outBuffer = {};
volatile bool webBuffersReady = false;

QueueHandle_t webUiEventQueue = NULL;
volatile int webUiEventPendingCount = 0;
portMUX_TYPE webUiEventMux = portMUX_INITIALIZER_UNLOCKED;
const int WEB_UI_EVENT_QUEUE_LENGTH = 16;
const unsigned long WEB_UI_MOVE_TIMEOUT_MS = 600;
volatile byte webUiHeldMoveAction = 0;
volatile unsigned long webUiHeldMoveMillis = 0;
volatile bool webUiMoveReleaseRequested = false;

WebServer server(80);
WebSocketsServer webSocket(81);
volatile int webSocketClientCount = 0;
volatile bool machineStatusForcePublish = false;
String lastMachineStatus = "";
String wifiStatus = "No WiFi";
unsigned long wifiStatusMillis = 0;
bool wifiSetupApActive = false;
bool wifiStationConnected = false;
String wifiIpAddress = "";
volatile bool tftUploadActive = false;
bool tftUploadFailed = false;
int tftUploadHttpStatus = 200;
String tftUploadMessage = "";
long tftUploadExpectedSize = 0;
long tftUploadReceivedSize = 0;
long tftUploadSentSize = 0;
int tftUploadPacketLength = 0;
int tftUploadProgressPercent = 0;
long nextionSerialBaud = NEXTION_NORMAL_BAUD;
byte tftUploadPacket[NEXTION_TFT_PACKET_SIZE];
const int NEXTION_BUFFER_LENGTH = 256;
byte nextionBuffer[NEXTION_BUFFER_LENGTH];
int nextionBufferIndex = 0;
byte lastNextionPageId = 255;
volatile bool firmwareUploadActive = false;
bool firmwareUploadFailed = false;
int firmwareUploadHttpStatus = 200;
String firmwareUploadMessage = "";
size_t firmwareUploadExpectedSize = 0;
size_t firmwareUploadReceivedSize = 0;
int firmwareUploadProgressPercent = 0;
bool firmwareUploadRestartPending = false;
unsigned long firmwareUploadRestartAt = 0;
bool configRestartPending = false;
unsigned long configRestartAt = 0;
bool keyboardCaptureActive = false;
byte keyboardCaptureReleaseCode = 0;
unsigned long keyboardCaptureUntil = 0;

bool bufferAvailable(CircleBuffer* b) {
  return b->head != b->tail;
}

bool writeBuffer(CircleBuffer* b, char c) {
  if ((b->head + 1) % b->size == b->tail) {
    return false;
  }
  b->buffer[b->head] = c;
  b->head = (b->head + 1) % b->size;
  return true;
}

bool writeBuffer(CircleBuffer* b, const char* str) {
  while (*str) {
    if (!writeBuffer(b, *str++)) {
      return false;
    }
  }
  return true;
}

bool writeBuffer(CircleBuffer* b, const String& str) {
  return writeBuffer(b, str.c_str());
}

bool writeBuffer(CircleBuffer* b, float f, int precision) {
  char buffer[16];
  dtostrf(f, 0, precision, buffer);
  return writeBuffer(b, buffer);
}

char shiftBuffer(CircleBuffer* b) {
  if (b->head == b->tail) {
    return 0;
  }
  char c = b->buffer[b->tail];
  b->tail = (b->tail + 1) % b->size;
  return c;
}

void initBuffer(CircleBuffer* b, size_t size) {
  b->size = size;
  b->buffer = (char*)malloc(size);
  b->head = 0;
  b->tail = 0;
}

void clearBuffer(CircleBuffer* b) {
  b->head = 0;
  b->tail = 0;
}

bool webUiIsConnected() {
  return webSocketClientCount > 0;
}

void queueWebSocketText(const String& text) {
  if (!webBuffersReady) return;
  writeBuffer(&outBuffer, text);
}

static String webSafeText(const String& value) {
  String result = "";
  result.reserve(value.length());
  for (int i = 0; i < value.length(); i++) {
    byte c = byte(value.charAt(i));
    if (c == 223) {
      // skip
    } else if (c >= 32 && c <= 126) {
      result += char(c);
    } else {
      result += "?";
    }
  }
  return result;
}

template <typename T>
inline void appendConfigLine(String* response, const char* name, T value) {
  *response += String(name) + "=" + String(value) + "\n";
}

static bool parseLongValue(const String& text, long* value) {
  String trimmed = text;
  trimmed.trim();
  if (trimmed.length() == 0) return false;
  char* end = nullptr;
  long parsed = strtol(trimmed.c_str(), &end, 10);
  if (end == trimmed.c_str() || *end != '\0') return false;
  *value = parsed;
  return true;
}

static bool parseFloatValue(const String& text, float* value) {
  String trimmed = text;
  trimmed.trim();
  if (trimmed.length() == 0) return false;
  char* end = nullptr;
  float parsed = strtof(trimmed.c_str(), &end);
  if (end == trimmed.c_str() || *end != '\0' || !isfinite(parsed)) return false;
  *value = parsed;
  return true;
}

static bool parseBoolValue(const String& text, bool* value) {
  String normalized = text;
  normalized.trim();
  normalized.toLowerCase();
  if (normalized == "1" || normalized == "true" || normalized == "on") {
    *value = true;
    return true;
  }
  if (normalized == "0" || normalized == "false" || normalized == "off") {
    *value = false;
    return true;
  }
  return false;
}

static bool readBoolConfigArg(const char* name, bool* value, String* error) {
  if (server.hasArg(name)) {
    String arg = server.arg(name);
    if (arg == "true" || arg == "1") {
      *value = true;
    } else if (arg == "false" || arg == "0") {
      *value = false;
    } else {
      if (error) *error = String("Invalid boolean value for ") + name;
      return false;
    }
  }
  return true;
}

static bool readLongConfigArg(const char* name, long* target, long minValue, long maxValue, String* error) {
  if (!server.hasArg(name)) {
    if(error) *error = String("Missing config value: ") + name;
    return false;
  }
  long value;
  if (!parseLongValue(server.arg(name), &value) || value < minValue || value > maxValue) {
    if(error) *error = String(name) + " must be between " + String(minValue) + " and " + String(maxValue);
    return false;
  }
  *target = value;
  return true;
}

static bool readIntConfigArg(const char* name, int* target, int minValue, int maxValue, String* error) {
  long value;
  if (!readLongConfigArg(name, &value, minValue, maxValue, error)) return false;
  *target = int(value);
  return true;
}

static bool readFloatConfigArg(const char* name, float* target, float minValue, float maxValue, String* error) {
  if (!server.hasArg(name)) {
    if(error) *error = String("Missing config value: ") + name;
    return false;
  }
  float value;
  if (!parseFloatValue(server.arg(name), &value) || value < minValue || value > maxValue) {
    if(error) *error = String(name) + " must be between " + String(minValue, 2) + " and " + String(maxValue, 2);
    return false;
  }
  *target = value;
  return true;
}

static bool readPulsePerRevolutionConfigArgs(String* error) {
  if (server.hasArg("zPulsePerRevolution") || server.hasArg("xPulsePerRevolution") || server.hasArg("yPulsePerRevolution")) {
    return
      readFloatConfigArg("zPulsePerRevolution", &PULSE_PER_REVOLUTION_Z, 1.0, 100000.0, error) &&
      readFloatConfigArg("xPulsePerRevolution", &PULSE_PER_REVOLUTION_X, 1.0, 100000.0, error) &&
      readFloatConfigArg("yPulsePerRevolution", &PULSE_PER_REVOLUTION_Y, 1.0, 100000.0, error);
  }
  float pulsePerRevolution;
  if (!readFloatConfigArg("pulsePerRevolution", &pulsePerRevolution, 1.0, 100000.0, error)) return false;
  PULSE_PER_REVOLUTION_Z = pulsePerRevolution;
  PULSE_PER_REVOLUTION_X = pulsePerRevolution;
  PULSE_PER_REVOLUTION_Y = pulsePerRevolution;
  return true;
}



static bool validateMachineConfig(String* error) {
  if (SPEED_START_Z > SPEED_MANUAL_MOVE_Z) {
    if(error) *error = "zSpeedStart must be less than or equal to zSpeedManualMove";
    return false;
  }
  if (SPEED_START_X > SPEED_MANUAL_MOVE_X) {
    if(error) *error = "xSpeedStart must be less than or equal to xSpeedManualMove";
    return false;
  }
  if (SPEED_START_Y > SPEED_MANUAL_MOVE_Y) {
    if(error) *error = "ySpeedStart must be less than or equal to ySpeedManualMove";
    return false;
  }
  if (JOYSTICK_DEADBAND >= JOYSTICK_ADC_MAX) {
    if(error) *error = "joystickDeadband must be less than joystickAdcMax";
    return false;
  }
  if (JOYSTICK_ENABLED && !anyJoystickAxisEnabled()) {
    if(error) *error = "at least one joystick axis must be enabled when joystickEnabled is set";
    return false;
  }
  return true;
}

static bool readMachineConfigFromRequest(String* error) {
  return
    readIntConfigArg("encoderPpr", &ENCODER_PPR, 1, 15000, error) &&
    readIntConfigArg("encoderBacklash", &ENCODER_BACKLASH, 0, 30000, error) &&
    readIntConfigArg("axisEncoderBacklash", &AXIS_ENCODER_BACKLASH, 0, 30000, error) &&
    readLongConfigArg("zScrewDu", &SCREW_Z_DU, 1, 10000000, error) &&
    readLongConfigArg("zMotorSteps", &MOTOR_STEPS_Z, 1, 1000000, error) &&
    readLongConfigArg("zSpeedStart", &SPEED_START_Z, 1, 1000000, error) &&
    readLongConfigArg("zAcceleration", &ACCELERATION_Z, 1, 100000000, error) &&
    readLongConfigArg("zSpeedManualMove", &SPEED_MANUAL_MOVE_Z, 1, 1000000, error) &&
    readBoolConfigArg("zInvert", &INVERT_Z, error) &&
    readBoolConfigArg("zInvertEnable", &INVERT_Z_ENABLE, error) &&
    readBoolConfigArg("zNeedsRest", &NEEDS_REST_Z, error) &&
    readLongConfigArg("zMaxTravelMm", &MAX_TRAVEL_MM_Z, 1, 10000, error) &&
    readLongConfigArg("zBacklashDu", &BACKLASH_DU_Z, 0, 10000000, error) &&
    readLongConfigArg("xScrewDu", &SCREW_X_DU, 1, 10000000, error) &&
    readLongConfigArg("xMotorSteps", &MOTOR_STEPS_X, 1, 1000000, error) &&
    readLongConfigArg("xSpeedStart", &SPEED_START_X, 1, 1000000, error) &&
    readLongConfigArg("xAcceleration", &ACCELERATION_X, 1, 100000000, error) &&
    readLongConfigArg("xSpeedManualMove", &SPEED_MANUAL_MOVE_X, 1, 1000000, error) &&
    readBoolConfigArg("xInvert", &INVERT_X, error) &&
    readBoolConfigArg("xInvertEnable", &INVERT_X_ENABLE, error) &&
    readBoolConfigArg("xNeedsRest", &NEEDS_REST_X, error) &&
    readLongConfigArg("xMaxTravelMm", &MAX_TRAVEL_MM_X, 1, 10000, error) &&
    readLongConfigArg("xBacklashDu", &BACKLASH_DU_X, 0, 10000000, error) &&
    readLongConfigArg("stepTimeMs", &STEP_TIME_MS, 1, 10000, error) &&
    readLongConfigArg("delayBetweenStepsMs", &DELAY_BETWEEN_STEPS_MS, 0, 10000, error) &&
    readBoolConfigArg("enableContinuousMove", &ENABLE_CONTINUOUS_MOVE, error) &&
    readLongConfigArg("safeDistanceDu", &SAFE_DISTANCE_DU, 0, 10000000, error) &&
    readLongConfigArg("slotLeftReductionDu", &SLOT_LEFT_REDUCTION_DU, 0, 10000000, error) &&
    readBoolConfigArg("activeY", &ACTIVE_Y, error) &&
    readBoolConfigArg("rotaryY", &ROTARY_Y, error) &&
    readLongConfigArg("yMotorSteps", &MOTOR_STEPS_Y, 1, 1000000, error) &&
    readLongConfigArg("yScrewDu", &SCREW_Y_DU, 1, 10000000, error) &&
    readLongConfigArg("ySpeedStart", &SPEED_START_Y, 1, 1000000, error) &&
    readLongConfigArg("yAcceleration", &ACCELERATION_Y, 1, 100000000, error) &&
    readLongConfigArg("ySpeedManualMove", &SPEED_MANUAL_MOVE_Y, 1, 1000000, error) &&
    readBoolConfigArg("yInvert", &INVERT_Y, error) &&
    readBoolConfigArg("yInvertEnable", &INVERT_Y_ENABLE, error) &&
    readBoolConfigArg("yNeedsRest", &NEEDS_REST_Y, error) &&
    readLongConfigArg("yMaxTravelMm", &MAX_TRAVEL_MM_Y, 1, 10000, error) &&
    readLongConfigArg("yBacklashDu", &BACKLASH_DU_Y, 0, 10000000, error) &&
    readPulsePerRevolutionConfigArgs(error) &&
    readBoolConfigArg("joystickEnabled", &JOYSTICK_ENABLED, error) &&
    readBoolConfigArg("joystickZEnabled", &JOYSTICK_Z_ENABLED, error) &&
    readBoolConfigArg("joystickXEnabled", &JOYSTICK_X_ENABLED, error) &&
    readBoolConfigArg("joystickYEnabled", &JOYSTICK_Y_ENABLED, error) &&
    readBoolConfigArg("joystickButtonTogglesOnOff", &JOYSTICK_BUTTON_TOGGLES_ON_OFF, error) &&
    readIntConfigArg("joystickCenterSamples", &JOYSTICK_CENTER_SAMPLES, 1, 1024, error) &&
    readIntConfigArg("joystickOversamples", &JOYSTICK_OVERSAMPLES, 1, 1024, error) &&
    readIntConfigArg("joystickSampleIntervalMs", &JOYSTICK_SAMPLE_INTERVAL_MS, 1, 1000, error) &&
    readIntConfigArg("joystickAdcMax", &JOYSTICK_ADC_MAX, 1, 65535, error) &&
    readIntConfigArg("joystickDeadband", &JOYSTICK_DEADBAND, 0, 65534, error) &&
    readIntConfigArg("joystickPulseQueueLimit", &JOYSTICK_PULSE_QUEUE_LIMIT, 1, 1000000, error) &&
    readFloatConfigArg("joystickNormalRevolutionsPerSecond", &JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND, 0.01, 1000.0, error) &&
    readFloatConfigArg("joystickRapidRevolutionsPerSecond", &JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND, 0.01, 1000.0, error) &&
    readBoolConfigArg("invertJoystickZ", &INVERT_JOYSTICK_Z, error) &&
    readBoolConfigArg("invertJoystickX", &INVERT_JOYSTICK_X, error) &&
    readBoolConfigArg("invertJoystickY", &INVERT_JOYSTICK_Y, error) &&
    readBoolConfigArg("invertJoystickButton", &INVERT_JOYSTICK_BUTTON, error) &&
    validateMachineConfig(error);
}

static bool readKeyboardConfigFromRequest(String* error) {
  if (!readBoolConfigArg("showKeyPresses", &SHOW_KEY_PRESSES, error)) return false;

  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (!server.hasArg(keyboardBindings[i].id)) {
      if(error) *error = String("Missing keyboard value: ") + keyboardBindings[i].id;
      return false;
    }
    long value;
    if (!parseLongValue(server.arg(keyboardBindings[i].id), &value) || value < 1 || value > 255) {
      if(error) *error = String(keyboardBindings[i].label) + " must use key code 1..255";
      return false;
    }
    keyboardBindings[i].code = byte(value);
  }

  return validateKeyboardConfig(error);
}

static void appendUiLine(String* response, const char* name, const String& value) {
  appendConfigLine(response, name, webSafeText(value));
}

void publishMachineStatus(const String& status, const String& modeText, const String& pitchText, const String& measureText,
    const String& stepText, const String& rpmText, const String& turnsText, const String& angleText,
    const String& xText, const String& xLeftText, const String& xRightText,
    const String& yText, const String& yLeftText, const String& yRightText,
    const String& zText, const String& zLeftText, const String& zRightText, const String& messageText,
    bool isDiameterMode, bool isAbsoluteMode, bool xIsDroActive, bool zIsDroActive) {
  if (!webBuffersReady || !webUiIsConnected()) return;

  String response = "";
  response.reserve(600);
  appendUiLine(&response, "UI.status", status);
  appendUiLine(&response, "UI.mode", modeText);
  appendUiLine(&response, "UI.pitch", pitchText);
  appendUiLine(&response, "UI.measure", measureText);
  appendUiLine(&response, "UI.step", stepText);
  appendUiLine(&response, "UI.rpm", rpmText);
  appendUiLine(&response, "UI.turns", turnsText);
  appendUiLine(&response, "UI.angle", angleText);
  appendUiLine(&response, "UI.x", xText);
  appendUiLine(&response, "UI.diameter", isDiameterMode ? "1" : "0");
  appendUiLine(&response, "UI.abs", isAbsoluteMode ? "1" : "0");
  appendUiLine(&response, "UI.xDro", xIsDroActive ? "1" : "0");
  appendUiLine(&response, "UI.zDro", zIsDroActive ? "1" : "0");
  appendUiLine(&response, "UI.xLeft", xLeftText);
  appendUiLine(&response, "UI.xRight", xRightText);
  appendUiLine(&response, "UI.y", yText);
  appendUiLine(&response, "UI.yLeft", yLeftText);
  appendUiLine(&response, "UI.yRight", yRightText);
  appendUiLine(&response, "UI.z", zText);
  appendUiLine(&response, "UI.zLeft", zLeftText);
  appendUiLine(&response, "UI.zRight", zRightText);
  appendUiLine(&response, "UI.message", messageText);

  if (!machineStatusForcePublish && response == lastMachineStatus) return;
  machineStatusForcePublish = false;
  lastMachineStatus = response;
  queueWebSocketText(response);
}

static bool isWebUiActionCode(byte actionCode) {
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    if (keyboardBindings[i].actionCode == actionCode) {
      return true;
    }
  }
  return false;
}

static bool parseWebUiStateToken(const String& token, bool* isPress) {
  if (token == "1" || token == "p" || token == "press" || token == "down") {
    *isPress = true;
    return true;
  }
  if (token == "0" || token == "r" || token == "release" || token == "up") {
    *isPress = false;
    return true;
  }
  return false;
}

static bool parseWebUiEventCommand(uint8_t* payload, size_t length, WebUiEvent* event, String* error) {
  size_t start = 0;
  while (start < length && payload[start] <= 32) start++;
  size_t end = length;
  while (end > start && payload[end - 1] <= 32) end--;
  if (start >= end || payload[start] != '@') return false;

  size_t i = start + 1;
  long actionCode = 0;
  bool hasActionCode = false;
  while (i < end && payload[i] >= '0' && payload[i] <= '9') {
    hasActionCode = true;
    actionCode = actionCode * 10 + (payload[i] - '0');
    if (actionCode > 255) {
      *error = "web action code must be 1..255";
      return true;
    }
    i++;
  }

  if (!hasActionCode) {
    *error = "missing web action code";
    return true;
  }
  if (actionCode < 1 || !isWebUiActionCode(byte(actionCode))) {
    *error = String("unknown web action code ") + String(actionCode);
    return true;
  }

  if (i >= end) {
    *error = "missing web action state";
    return true;
  }

  char separator = char(payload[i]);
  if (separator != ':' && separator != '=' && separator != ',') {
    *error = "expected @action:state";
    return true;
  }
  i++;
  if (i >= end) {
    *error = "missing web action state";
    return true;
  }

  bool isPress = true;
  String state = "";
  while (i < end) {
    state += char(payload[i]);
    i++;
  }
  state.trim();
  state.toLowerCase();
  if (!parseWebUiStateToken(state, &isPress)) {
    *error = String("unknown web action state ") + state;
    return true;
  }

  event->actionCode = byte(actionCode);
  event->isPress = isPress;
  return true;
}

bool queueWebUiEvent(byte actionCode, bool isPress) {
  if (webUiEventQueue == NULL) return false;
  WebUiEvent event = { actionCode, isPress };
  bool queued = xQueueSend(webUiEventQueue, &event, 0) == pdTRUE;
  if (queued) {
    portENTER_CRITICAL(&webUiEventMux);
    webUiEventPendingCount = webUiEventPendingCount + 1;
    portEXIT_CRITICAL(&webUiEventMux);
  }
  return queued;
}

bool readWebUiEvent(WebUiEvent* event) {
  if (webUiEventQueue == NULL || webUiEventPendingCount <= 0) return false;
  bool received = xQueueReceive(webUiEventQueue, event, 0) == pdTRUE;
  portENTER_CRITICAL(&webUiEventMux);
  if (received && webUiEventPendingCount > 0) webUiEventPendingCount = webUiEventPendingCount - 1;
  else if (!received) webUiEventPendingCount = 0;
  portEXIT_CRITICAL(&webUiEventMux);
  return received;
}

bool isWebUiMoveAction(byte actionCode) {
  return actionCode == B_LEFT || actionCode == B_RIGHT || actionCode == B_UP || actionCode == B_DOWN || actionCode == B_FORWARD || actionCode == B_BACK;
}

void setMoveButtonPressed(byte actionCode, bool isPress) {
  if (actionCode == B_LEFT) {
    buttonLeftPressed = isPress;
  } else if (actionCode == B_RIGHT) {
    buttonRightPressed = isPress;
  } else if (actionCode == B_UP) {
    buttonUpPressed = isPress;
  } else if (actionCode == B_DOWN) {
    buttonDownPressed = isPress;
  } else if (actionCode == B_FORWARD) {
    buttonForwardPressed = isPress;
  } else if (actionCode == B_BACK) {
    buttonBackPressed = isPress;
  }
}

void releaseWebUiHeldMove() {
  byte actionCode = webUiHeldMoveAction;
  if (actionCode != 0) {
    setMoveButtonPressed(actionCode, false);
  }
  webUiHeldMoveAction = 0;
  webUiHeldMoveMillis = 0;
}

void requestWebUiMoveRelease() {
  if (webUiHeldMoveAction == 0 && webUiEventPendingCount <= 0) return;
  webUiMoveReleaseRequested = true;
  queueWebUiEvent(B_LEFT, false);
  queueWebUiEvent(B_RIGHT, false);
  queueWebUiEvent(B_UP, false);
  queueWebUiEvent(B_DOWN, false);
  queueWebUiEvent(B_FORWARD, false);
  queueWebUiEvent(B_BACK, false);
}

void processWebUiMoveFailsafe() {
  if (webUiMoveReleaseRequested) {
    webUiMoveReleaseRequested = false;
    releaseWebUiHeldMove();
  } else if (webUiHeldMoveAction != 0 && millis() - webUiHeldMoveMillis > WEB_UI_MOVE_TIMEOUT_MS) {
    releaseWebUiHeldMove();
    queueWebSocketText("WEBUI.warning=move released after lost heartbeat\n");
  }
}

void updateWebUiMoveWatchdog(byte actionCode, bool isPress) {
  if (!isWebUiMoveAction(actionCode)) return;
  if (isPress) {
    webUiHeldMoveAction = actionCode;
    webUiHeldMoveMillis = millis();
  } else if (webUiHeldMoveAction == actionCode) {
    releaseWebUiHeldMove();
  }
}

static bool handleWebUiEventCommand(uint8_t* payload, size_t length) {
  if (length >= 8 && strncmp((const char*)payload, "?sysinfo", 8) == 0) {
    String response = "SYSINFO.version=" CONTROLLER_VERSION_TEXT "\n";
    response += "SYSINFO.chip=" + String(ESP.getChipModel()) + " rev " + String(ESP.getChipRevision()) + "\n";
    response += "SYSINFO.flash=" + String(ESP.getFlashChipSize() / 1024 / 1024) + " MB\n";
    response += "SYSINFO.heap=" + String(ESP.getFreeHeap() / 1024) + " KB\n";
    queueWebSocketText(response);
    return true;
  }

  WebUiEvent event = {};
  String error = "";
  if (!parseWebUiEventCommand(payload, length, &event, &error)) {
    return false;
  }
  if (error.length() > 0) {
    queueWebSocketText(String("WEBUI.error=") + error + "\n");
    return true;
  }
  if (!queueWebUiEvent(event.actionCode, event.isPress)) {
    queueWebSocketText("WEBUI.error=event queue full\n");
  }
  return true;
}

void publishKeyboardEvent(byte physicalCode, byte actionCode, bool isPress) {
  String line = String("KEY.") + (isPress ? "press=" : "release=") + String(physicalCode) + "\n";
  line += "KEY.action=";
  line += keyboardActionLabel(actionCode);
  line += "\n";
  queueWebSocketText(line);
  if (SHOW_KEY_PRESSES) {
    setText("t3", (isPress ? "Press " : "Release ") + String(physicalCode));
  }
}

static void startKeyboardCapture() {
  keyboardCaptureActive = true;
  keyboardCaptureReleaseCode = 0;
  keyboardCaptureUntil = millis() + 30000;
}

static void stopKeyboardCapture() {
  keyboardCaptureActive = false;
  keyboardCaptureReleaseCode = 0;
}

bool shouldConsumeKeyboardCapture(byte physicalCode, bool isPress) {
  if (keyboardCaptureReleaseCode != 0 && physicalCode == keyboardCaptureReleaseCode && !isPress) {
    keyboardCaptureReleaseCode = 0;
    return true;
  }
  if (!keyboardCaptureActive) return false;
  if (long(millis() - keyboardCaptureUntil) > 0) {
    stopKeyboardCapture();
    return false;
  }
  if (isPress) {
    keyboardCaptureActive = false;
    keyboardCaptureReleaseCode = physicalCode;
  }
  return true;
}

static String getKeyboardConfigResponse() {
  String response = "";
  response.reserve(1200);
  appendConfigLine(&response, "showKeyPresses", SHOW_KEY_PRESSES);
  for (int i = 0; i < KEYBOARD_BINDING_COUNT; i++) {
    appendConfigLine(&response, keyboardBindings[i].id, int(keyboardBindings[i].code));
  }
  return response;
}

static String getMachineConfigResponse() {
  String response = "";
  response.reserve(2600);
  appendConfigLine(&response, "encoderPpr", ENCODER_PPR);
  appendConfigLine(&response, "encoderBacklash", ENCODER_BACKLASH);
  appendConfigLine(&response, "axisEncoderBacklash", AXIS_ENCODER_BACKLASH);
  appendConfigLine(&response, "zScrewDu", SCREW_Z_DU);
  appendConfigLine(&response, "zMotorSteps", MOTOR_STEPS_Z);
  appendConfigLine(&response, "zPulsePerRevolution", PULSE_PER_REVOLUTION_Z);
  appendConfigLine(&response, "zSpeedStart", SPEED_START_Z);
  appendConfigLine(&response, "zAcceleration", ACCELERATION_Z);
  appendConfigLine(&response, "zSpeedManualMove", SPEED_MANUAL_MOVE_Z);
  appendConfigLine(&response, "zInvert", INVERT_Z);
  appendConfigLine(&response, "zInvertEnable", INVERT_Z_ENABLE);
  appendConfigLine(&response, "zNeedsRest", NEEDS_REST_Z);
  appendConfigLine(&response, "zMaxTravelMm", MAX_TRAVEL_MM_Z);
  appendConfigLine(&response, "zBacklashDu", BACKLASH_DU_Z);
  appendConfigLine(&response, "xScrewDu", SCREW_X_DU);
  appendConfigLine(&response, "xMotorSteps", MOTOR_STEPS_X);
  appendConfigLine(&response, "xPulsePerRevolution", PULSE_PER_REVOLUTION_X);
  appendConfigLine(&response, "xSpeedStart", SPEED_START_X);
  appendConfigLine(&response, "xAcceleration", ACCELERATION_X);
  appendConfigLine(&response, "xSpeedManualMove", SPEED_MANUAL_MOVE_X);
  appendConfigLine(&response, "xInvert", INVERT_X);
  appendConfigLine(&response, "xInvertEnable", INVERT_X_ENABLE);
  appendConfigLine(&response, "xNeedsRest", NEEDS_REST_X);
  appendConfigLine(&response, "xMaxTravelMm", MAX_TRAVEL_MM_X);
  appendConfigLine(&response, "xBacklashDu", BACKLASH_DU_X);
  appendConfigLine(&response, "stepTimeMs", STEP_TIME_MS);
  appendConfigLine(&response, "delayBetweenStepsMs", DELAY_BETWEEN_STEPS_MS);
  appendConfigLine(&response, "enableContinuousMove", ENABLE_CONTINUOUS_MOVE);
  appendConfigLine(&response, "safeDistanceDu", SAFE_DISTANCE_DU);
  appendConfigLine(&response, "slotLeftReductionDu", SLOT_LEFT_REDUCTION_DU);
  appendConfigLine(&response, "activeY", ACTIVE_Y);
  appendConfigLine(&response, "rotaryY", ROTARY_Y);
  appendConfigLine(&response, "yMotorSteps", MOTOR_STEPS_Y);
  appendConfigLine(&response, "yPulsePerRevolution", PULSE_PER_REVOLUTION_Y);
  appendConfigLine(&response, "yScrewDu", SCREW_Y_DU);
  appendConfigLine(&response, "ySpeedStart", SPEED_START_Y);
  appendConfigLine(&response, "yAcceleration", ACCELERATION_Y);
  appendConfigLine(&response, "ySpeedManualMove", SPEED_MANUAL_MOVE_Y);
  appendConfigLine(&response, "yInvert", INVERT_Y);
  appendConfigLine(&response, "yInvertEnable", INVERT_Y_ENABLE);
  appendConfigLine(&response, "yNeedsRest", NEEDS_REST_Y);
  appendConfigLine(&response, "yMaxTravelMm", MAX_TRAVEL_MM_Y);
  appendConfigLine(&response, "yBacklashDu", BACKLASH_DU_Y);
  appendConfigLine(&response, "pulsePerRevolution", PULSE_PER_REVOLUTION_Z);
  appendConfigLine(&response, "joystickEnabled", JOYSTICK_ENABLED);
  appendConfigLine(&response, "joystickZEnabled", JOYSTICK_Z_ENABLED);
  appendConfigLine(&response, "joystickXEnabled", JOYSTICK_X_ENABLED);
  appendConfigLine(&response, "joystickYEnabled", JOYSTICK_Y_ENABLED);
  appendConfigLine(&response, "joystickButtonTogglesOnOff", JOYSTICK_BUTTON_TOGGLES_ON_OFF);
  appendConfigLine(&response, "joystickCenterSamples", JOYSTICK_CENTER_SAMPLES);
  appendConfigLine(&response, "joystickOversamples", JOYSTICK_OVERSAMPLES);
  appendConfigLine(&response, "joystickSampleIntervalMs", JOYSTICK_SAMPLE_INTERVAL_MS);
  appendConfigLine(&response, "joystickAdcMax", JOYSTICK_ADC_MAX);
  appendConfigLine(&response, "joystickDeadband", JOYSTICK_DEADBAND);
  appendConfigLine(&response, "joystickPulseQueueLimit", JOYSTICK_PULSE_QUEUE_LIMIT);
  appendConfigLine(&response, "joystickNormalRevolutionsPerSecond", JOYSTICK_NORMAL_REVOLUTIONS_PER_SECOND);
  appendConfigLine(&response, "joystickRapidRevolutionsPerSecond", JOYSTICK_RAPID_REVOLUTIONS_PER_SECOND);
  appendConfigLine(&response, "invertJoystickZ", INVERT_JOYSTICK_Z);
  appendConfigLine(&response, "invertJoystickX", INVERT_JOYSTICK_X);
  appendConfigLine(&response, "invertJoystickY", INVERT_JOYSTICK_Y);
  appendConfigLine(&response, "invertJoystickButton", INVERT_JOYSTICK_BUTTON);
  return response;
}

static String getWifiMode() {
  if (!WIFI_ENABLED) return "off";
  if (wifiSetupApActive) return "setup-ap";
  if (wifiStationConnected) return "station";
  return "starting";
}

static String getWifiConfigResponse() {
  String response = "";
  response.reserve(360);
  appendConfigLine(&response, "wifiEnabled", WIFI_ENABLED);
  appendConfigLine(&response, "wifiSsid", WIFI_SSID);
  appendConfigLine(&response, "wifiHostname", WIFI_HOSTNAME);
  appendConfigLine(&response, "wifiPasswordSet", WIFI_PASSWORD.length() > 0);
  appendConfigLine(&response, "wifiMode", getWifiMode());
  appendConfigLine(&response, "wifiIp", wifiIpAddress);
  appendConfigLine(&response, "wifiStatus", wifiStatus);
  appendConfigLine(&response, "setupApSsid", WIFI_SETUP_AP_SSID);
  return response;
}

static bool readWifiConfigFromRequest(String* error) {
  bool enabled = WIFI_ENABLED;
  if (!readBoolConfigArg("wifiEnabled", &enabled, error)) {
    return false;
  }
  if (!server.hasArg("wifiSsid")) {
    *error = "Missing WiFi value: wifiSsid";
    return false;
  }
  if (!server.hasArg("wifiPassword")) {
    *error = "Missing WiFi value: wifiPassword";
    return false;
  }
  if (!server.hasArg("wifiHostname")) {
    *error = "Missing WiFi value: wifiHostname";
    return false;
  }

  bool clearPassword = false;
  if (!readBoolConfigArg("wifiClearPassword", &clearPassword, error)) {
    return false;
  }

  String ssid = server.arg("wifiSsid");
  ssid.trim();
  String password = server.arg("wifiPassword");
  String hostname = server.arg("wifiHostname");
  hostname.trim();

  WIFI_ENABLED = enabled;
  WIFI_SSID = ssid;
  WIFI_HOSTNAME = hostname.length() > 0 ? hostname : DEFAULT_WIFI_HOSTNAME;
  if (clearPassword) {
    WIFI_PASSWORD = "";
  } else if (password.length() > 0) {
    WIFI_PASSWORD = password;
  }
  return validateWifiConfig(error);
}

static void scheduleConfigRestart() {
  configRestartPending = true;
  configRestartAt = millis() + 1500;
}

static void tftUploadLog(const String& message) {
  String line = String("TFT: ") + message + "\n";
  DEBUG_PRINT(line);
  webSocket.broadcastTXT(line);
  webSocket.loop();
}

static void firmwareUploadLog(const String& message) {
  String line = String("FW: ") + message + "\n";
  DEBUG_PRINT(line);
  webSocket.broadcastTXT(line);
  webSocket.loop();
}

static void handleWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    webSocketClientCount = webSocketClientCount + 1;
    machineStatusForcePublish = true;
  } else if (type == WStype_DISCONNECTED) {
    requestWebUiMoveRelease();
    if (webSocketClientCount > 0) webSocketClientCount = webSocketClientCount - 1;
  } else if (type == WStype_TEXT) {
    String cmdStr = "";
    for (size_t i = 0; i < length; i++) {
      if (payload[i] >= 32 && payload[i] <= 126) cmdStr += (char)payload[i];
    }
    if (cmdStr != "?" && cmdStr != "?sysinfo") {
      Serial.printf("WebSocket Command: %s\n", cmdStr.c_str());
    }

    if (handleWebUiEventCommand(payload, length)) {
      return;
    }
    for (size_t i = 0; i < length; i++) {
      writeBuffer(&inBuffer, payload[i]);
    }
  }
}

static void handleClientRequests() {
  File file = LittleFS.open("/www/index.html", "r");
  if (!file) {
    server.send(404, "text/plain", "Web UI not found on LittleFS");
    return;
  }
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.sendHeader("Pragma", "no-cache");
  server.sendHeader("Expires", "0");
  server.streamFile(file, "text/html");
  file.close();
}

int getGcodeProgramCount() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return 0;
  }

  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();
    if (filename.endsWith(".gcode")) {
      count++;
    }
    file = root.openNextFile();
  }

  return count;
}

String getCurrentGcodeProgramName() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return "";
  }

  int count = 0;
  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();
    if (filename.endsWith(".gcode")) {
      if (count == gcodeProgramIndex) {
        return filename.substring(0, filename.length() - 6);
      }
      count++;
    }
    file = root.openNextFile();
  }

  return "";
}

bool saveGcode() {
  if (gcodeSaveName.length() < 2) {
    writeBuffer(&outBuffer, "error: name must be at least 2 chars\n");
    return false;
  }
  if (gcodeSaveValue.length() < 2) {
    writeBuffer(&outBuffer, "error: program too short\n");
    return false;
  }

  String filename = "/" + gcodeSaveName + ".gcode";
  File file = LittleFS.open(filename, "w");
  if (!file) {
    writeBuffer(&outBuffer, "error: failed to open file\n");
    return false;
  }

  file.print(gcodeSaveValue);
  file.close();

  writeBuffer(&outBuffer, "success: G-code saved\n");
  gcodeProgramCount = getGcodeProgramCount();
  return true;
}

void removeGcodeByName(const String& name) {
  if (name.length() == 0) return;

  String filename = "/" + name + ".gcode";
  if (!LittleFS.exists(filename)) {
    writeBuffer(&outBuffer, "error: file not found\n");
    return;
  }

  if (!LittleFS.remove(filename)) {
    writeBuffer(&outBuffer, "error: failed to delete " + filename + "\n");
    return;
  }

  writeBuffer(&outBuffer, "success: " + name + " deleted\n");
  gcodeProgramCount = getGcodeProgramCount();
}

bool removeAllGcode() {
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) {
    writeBuffer(&outBuffer, "error: failed to open directory\n");
    return false;
  }

  File file = root.openNextFile();
  while (file) {
    String filename = file.name();
    file.close();
    if (filename.endsWith(".gcode")) {
      LittleFS.remove("/" + filename);
    }
    file = root.openNextFile();
  }

  writeBuffer(&outBuffer, "success: all G-code deleted\n");
  gcodeProgramCount = 0;
  return true;
}

String readGcodeProgram(const String& name) {
  String filename = "/" + name + ".gcode";
  File file = LittleFS.open(filename, "r");
  if (!file) {
    return "";
  }

  String result;
  result.reserve(file.size());
  char buf[64];
  while (file.available()) {
    size_t bytesRead = file.readBytes(buf, sizeof(buf));
    result.concat(buf, bytesRead);
  }
  file.close();
  return result;
}

static void handleGcodeAdd() {
  if (server.hasArg("name") && server.hasArg("gcode")) {
    gcodeSaveName = server.arg("name");
    gcodeSaveValue = server.arg("gcode");
    if (saveGcode()) {
      server.send(200, "text/plain", "G-code saved successfully");
    } else {
      server.send(500, "text/plain", "Failed to save G-code");
    }
  } else {
    server.send(400, "text/plain", "Missing parameters");
  }
}

static void handleGcodeList() {
  String response = "";
  File root = LittleFS.open("/");
  if (root && root.isDirectory()) {
    File file = root.openNextFile();
    while (file) {
      String filename = file.name();
      if (filename.endsWith(".gcode")) {
        response += filename.substring(0, filename.length() - 6) + "\n";
      }
      file.close();
      file = root.openNextFile();
    }
  }
  server.send(200, "text/plain", response);
}

static void handleGcodeGet() {
  if (server.hasArg("name")) {
    String gcode = readGcodeProgram(server.arg("name"));
    if (gcode != "") {
      server.send(200, "text/plain", gcode);
    } else {
      server.send(404, "text/plain", "G-code file not found");
    }
  } else {
    server.send(400, "text/plain", "Missing parameter: name");
  }
}

static void handleGcodeRemove() {
  if (server.hasArg("name")) {
    removeGcodeByName(server.arg("name"));
    server.send(200, "text/plain", "G-code removed");
  } else {
    server.send(400, "text/plain", "Missing parameter: name");
  }
}

static bool machineConfigChangeBlocked() {
  return isOn || tftUploadActive || firmwareUploadActive || firmwareUploadRestartPending || configRestartPending;
}

static void handleConfigGet() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/plain", getMachineConfigResponse());
}

static void handleConfigSave() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before changing machine config");
    return;
  }

  String error = "";
  if (!readMachineConfigFromRequest(&error)) {
    loadMachineConfig();
    server.send(400, "text/plain", error);
    return;
  }

  saveMachineConfig();
  scheduleConfigRestart();
  server.send(200, "text/plain", "Machine config saved. Restarting controller...");
}

static void handleConfigReset() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before resetting machine config");
    return;
  }

  setMachineConfigDefaults();
  saveMachineConfig();
  scheduleConfigRestart();
  server.send(200, "text/plain", "Machine config reset. Restarting controller...");
}

static void handleKeyboardConfigGet() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/plain", getKeyboardConfigResponse());
}

static void handleKeyboardConfigSave() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before changing keyboard config");
    return;
  }

  String error = "";
  if (!readKeyboardConfigFromRequest(&error)) {
    loadKeyboardConfig();
    server.send(400, "text/plain", error);
    return;
  }

  saveKeyboardConfig();
  server.send(200, "text/plain", "Keyboard config saved");
}

static void handleKeyboardConfigReset() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before resetting keyboard config");
    return;
  }

  setKeyboardConfigDefaults();
  saveKeyboardConfig();
  server.send(200, "text/plain", "Keyboard config reset");
}

static void handleKeyboardCapture() {
  bool enabled = false;
  String error = "";
  if (!readBoolConfigArg("enabled", &enabled, &error)) {
    server.send(400, "text/plain", error);
    return;
  }
  if (enabled && machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before learning keyboard keys");
    return;
  }

  if (enabled) {
    startKeyboardCapture();
    server.send(200, "text/plain", "Keyboard capture started");
  } else {
    stopKeyboardCapture();
    server.send(200, "text/plain", "Keyboard capture stopped");
  }
}

static void handleWifiGet() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/plain", getWifiConfigResponse());
}

static void handleWifiSave() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before changing WiFi config");
    return;
  }

  String error = "";
  if (!readWifiConfigFromRequest(&error)) {
    loadWifiConfig();
    server.send(400, "text/plain", error);
    return;
  }

  saveWifiConfig();
  scheduleConfigRestart();
  server.send(200, "text/plain", "WiFi config saved. Restarting controller...");
}

static void handleWifiReset() {
  if (machineConfigChangeBlocked()) {
    server.send(409, "text/plain", "Stop the controller and wait for uploads to finish before resetting WiFi config");
    return;
  }

  setWifiConfigDefaults();
  saveWifiConfig();
  scheduleConfigRestart();
  server.send(200, "text/plain", "WiFi config reset. Restarting controller...");
}

static void beginNextionSerial(long baud) {
  if (nextionSerialBaud == baud) {
    return;
  }

  Serial1.updateBaudRate(baud);
  nextionSerialBaud = baud;
  DELAY(5);
}

static void clearNextionInput() {
  while (Serial1.available() > 0) {
    Serial1.read();
  }
}

static bool waitForNextionByte(byte expected, unsigned long timeoutMs, const String& context) {
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial1.available() > 0) {
      if (Serial1.read() == expected) {
        return true;
      }
    }
    DELAY(1);
    taskYIELD();
  }
  tftUploadLog(context + ": timeout waiting for display ACK");
  return false;
}

static bool waitForNextionConnect(unsigned long timeoutMs, const String& context) {
  String response = "";
  unsigned long start = millis();
  while (millis() - start < timeoutMs) {
    while (Serial1.available() > 0) {
      byte b = Serial1.read();
      if (b != 0xFF && response.length() < 120) {
        response += char(b);
      }
      if (response.indexOf("comok") >= 0) {
        return true;
      }
    }
    DELAY(1);
    taskYIELD();
  }
  tftUploadLog(context + ": connect timeout");
  return false;
}

static unsigned long nextionBaudDelayMs(long baud) {
  return 1000000 / baud + 30;
}

static void setTftUploadError(const String& message, int status) {
  tftUploadFailed = true;
  tftUploadHttpStatus = status;
  tftUploadMessage = message;
  tftUploadLog(message);
}

static bool startNextionTftUpload(long fileSize, long currentBaud) {
  tftUploadLog(String("upload started, size=") + String(fileSize) + " bytes, display baud=" + String(currentBaud));
  if (fileSize <= 0) {
    setTftUploadError("error: TFT file is empty", 400);
    return false;
  }
  if (currentBaud != NEXTION_FIRST_UPLOAD_BAUD && currentBaud != NEXTION_NORMAL_BAUD) {
    setTftUploadError(String("error: unsupported Nextion baud ") + String(currentBaud), 400);
    return false;
  }
  if (isOn) {
    setTftUploadError("error: stop the controller before TFT upload", 409);
    return false;
  }

  tftUploadActive = true;
  tftUploadFailed = false;
  tftUploadHttpStatus = 200;
  tftUploadMessage = "TFT upload started";
  tftUploadExpectedSize = fileSize;
  tftUploadReceivedSize = 0;
  tftUploadSentSize = 0;
  tftUploadPacketLength = 0;
  tftUploadProgressPercent = 0;
  nextionBufferIndex = 0;
  beginNextionSerial(currentBaud);
  clearNextionInput();

  writeNextionCommandRaw("");
  DELAY(nextionBaudDelayMs(currentBaud));
  clearNextionInput();
  writeNextionCommandRaw("connect");
  if (!waitForNextionConnect(NEXTION_CONNECT_TIMEOUT_MS, String("connect at selected baud ") + String(currentBaud))) {
    setTftUploadError(String("error: Nextion display did not answer at selected baud ") + String(currentBaud) + ". Power H5 via POWER port, not USB, and try again", 500);
    beginNextionSerial(NEXTION_NORMAL_BAUD);
    tftUploadActive = false;
    return false;
  }
  tftUploadLog(String("display connected at ") + String(currentBaud) + " baud");
  long uploadBaud = NEXTION_UPLOAD_BAUD;
  clearNextionInput();
  writeNextionCommandRaw("");
  writeNextionCommandRaw(String("whmi-wri ") + String(fileSize) + "," + String(uploadBaud) + ",0");
  if (currentBaud != uploadBaud) {
    DELAY(nextionBaudDelayMs(currentBaud) + 50);
    beginNextionSerial(uploadBaud);
  }
  if (!waitForNextionByte(0x05, NEXTION_ACK_TIMEOUT_MS, "whmi-wri")) {
    setTftUploadError("error: Nextion display did not accept TFT upload", 500);
    beginNextionSerial(NEXTION_NORMAL_BAUD);
    tftUploadActive = false;
    return false;
  }
  tftUploadLog(String("display accepted upload, streaming at ") + String(uploadBaud) + " baud");
  return true;
}

static bool flushTftUploadPacket() {
  if (tftUploadPacketLength == 0) {
    return true;
  }

  Serial1.write(tftUploadPacket, tftUploadPacketLength);
  Serial1.flush();
  tftUploadSentSize += tftUploadPacketLength;
  tftUploadPacketLength = 0;
  int progress = tftUploadExpectedSize > 0 ? int(tftUploadSentSize * 100 / tftUploadExpectedSize) : 0;
  if (progress >= tftUploadProgressPercent + 10 || progress == 100) {
    tftUploadProgressPercent = progress;
    tftUploadLog(String("progress ") + String(progress) + "%");
  }
  if (!waitForNextionByte(0x05, NEXTION_ACK_TIMEOUT_MS, "packet")) {
    setTftUploadError("error: Nextion display did not acknowledge TFT data", 500);
    return false;
  }
  return true;
}

static bool writeTftUploadData(const byte* data, size_t length) {
  size_t offset = 0;
  while (offset < length) {
    int packetSpace = NEXTION_TFT_PACKET_SIZE - tftUploadPacketLength;
    int copyLength = min(int(length - offset), packetSpace);
    memcpy(tftUploadPacket + tftUploadPacketLength, data + offset, copyLength);
    tftUploadPacketLength += copyLength;
    tftUploadReceivedSize += copyLength;
    offset += copyLength;
    if (tftUploadPacketLength == NEXTION_TFT_PACKET_SIZE && !flushTftUploadPacket()) {
      return false;
    }
  }
  return true;
}

static void finishTftUpload() {
  if (!tftUploadFailed && tftUploadReceivedSize != tftUploadExpectedSize) {
    setTftUploadError("error: TFT upload size mismatch", 400);
  }
  if (!tftUploadFailed && !flushTftUploadPacket()) {
  }
  if (!tftUploadFailed) {
    tftUploadMessage = "TFT upload complete";
    tftUploadLog("upload complete, waiting for display reset");
    waitForNextionByte(0x88, 5000, "display reset");
  }
  beginNextionSerial(NEXTION_NORMAL_BAUD);
  lcdHashLine0 = LCD_HASH_INITIAL;
  lcdHashLine1 = LCD_HASH_INITIAL;
  lcdHashLine2 = LCD_HASH_INITIAL;
  lcdHashLine3 = LCD_HASH_INITIAL;
  tftUploadActive = false;
}

static void handleTftUpload() {
  const HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    filename.toLowerCase();
    long fileSize = server.hasArg("size") ? server.arg("size").toInt() : upload.totalSize;
    long currentBaud = server.hasArg("baud") ? server.arg("baud").toInt() : NEXTION_FIRST_UPLOAD_BAUD;
    tftUploadLog(String("selected ") + upload.filename + ", baud=" + String(currentBaud));
    if (!filename.endsWith(".tft")) {
      setTftUploadError("error: only .tft files can be uploaded", 400);
      return;
    }
    startNextionTftUpload(fileSize, currentBaud);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!tftUploadFailed) {
      writeTftUploadData(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    finishTftUpload();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    setTftUploadError("error: TFT upload aborted", 400);
    beginNextionSerial(NEXTION_NORMAL_BAUD);
    tftUploadActive = false;
  }
}

static void handleTftUploadResult() {
  server.send(tftUploadHttpStatus, "text/plain", tftUploadMessage);
}

static void setFirmwareUploadError(const String& message, int status) {
  firmwareUploadFailed = true;
  firmwareUploadHttpStatus = status;
  firmwareUploadMessage = message;
  firmwareUploadLog(message);
}

static bool startFirmwareUpload(size_t fileSize) {
  firmwareUploadLog(String("upload started, size=") + String((unsigned long)fileSize) + " bytes");
  firmwareUploadRestartPending = false;
  if (fileSize == 0) {
    setFirmwareUploadError("error: firmware file is empty", 400);
    return false;
  }
  if (tftUploadActive) {
    setFirmwareUploadError("error: wait for TFT upload to finish before firmware upload", 409);
    return false;
  }
  if (isOn) {
    setFirmwareUploadError("error: stop the controller before firmware upload", 409);
    return false;
  }

  firmwareUploadActive = true;
  firmwareUploadFailed = false;
  firmwareUploadHttpStatus = 200;
  firmwareUploadMessage = "Firmware upload started";
  firmwareUploadExpectedSize = fileSize;
  firmwareUploadReceivedSize = 0;
  firmwareUploadProgressPercent = 0;

  if (!Update.begin(fileSize, U_FLASH)) {
    setFirmwareUploadError(String("error: firmware update could not start: ") + Update.errorString(), 500);
    firmwareUploadActive = false;
    return false;
  }
  return true;
}

static bool writeFirmwareUploadData(const byte* data, size_t length) {
  size_t written = Update.write((uint8_t*)data, length);
  firmwareUploadReceivedSize += written;
  if (written != length) {
    setFirmwareUploadError(String("error: firmware write failed: ") + Update.errorString(), 500);
    return false;
  }

  int progress = firmwareUploadExpectedSize > 0 ? int(firmwareUploadReceivedSize * 100 / firmwareUploadExpectedSize) : 0;
  if (progress >= firmwareUploadProgressPercent + 10 || progress == 100) {
    firmwareUploadProgressPercent = progress;
    firmwareUploadLog(String("progress ") + String(progress) + "%");
  }
  return true;
}

static void finishFirmwareUpload() {
  if (!firmwareUploadFailed && firmwareUploadReceivedSize != firmwareUploadExpectedSize) {
    setFirmwareUploadError("error: firmware upload size mismatch", 400);
  }
  if (!firmwareUploadFailed && !Update.end(true)) {
    setFirmwareUploadError(String("error: firmware update failed: ") + Update.errorString(), 500);
  }
  if (!firmwareUploadFailed) {
    firmwareUploadMessage = "Firmware upload complete. Restarting controller...";
    firmwareUploadLog("upload complete, restarting controller");
    firmwareUploadRestartPending = true;
    firmwareUploadRestartAt = millis() + 1500;
  } else if (firmwareUploadActive) {
    Update.abort();
  }
  firmwareUploadActive = false;
}

static void handleFirmwareUpload() {
  const HTTPUpload& upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    String filename = upload.filename;
    filename.toLowerCase();
    size_t fileSize = server.hasArg("size") ? server.arg("size").toInt() : upload.totalSize;
    firmwareUploadLog(String("selected ") + upload.filename);
    if (!filename.endsWith(".bin")) {
      setFirmwareUploadError("error: only .bin firmware files can be uploaded", 400);
      return;
    }
    startFirmwareUpload(fileSize);
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (!firmwareUploadFailed) {
      writeFirmwareUploadData(upload.buf, upload.currentSize);
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    finishFirmwareUpload();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (firmwareUploadActive) {
      Update.abort();
    }
    setFirmwareUploadError("error: firmware upload aborted", 400);
    firmwareUploadActive = false;
  }
}

static void handleFirmwareUploadResult() {
  server.sendHeader("Connection", "close");
  server.send(firmwareUploadHttpStatus, "text/plain", firmwareUploadMessage);
}

static void handleStatus() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/plain",
    String("LittleFS.freeSpace=") + String(LittleFS.totalBytes() - LittleFS.usedBytes()) + "\n" +
    "TFT.uploadActive=" + String(tftUploadActive ? 1 : 0) + "\n" +
    "TFT.uploaded=" + String(tftUploadSentSize) + "\n" +
    "TFT.size=" + String(tftUploadExpectedSize) + "\n" +
    "TFT.message=" + tftUploadMessage + "\n" +
    "FW.uploadActive=" + String(firmwareUploadActive ? 1 : 0) + "\n" +
    "FW.uploaded=" + String((unsigned long)firmwareUploadReceivedSize) + "\n" +
    "FW.size=" + String((unsigned long)firmwareUploadExpectedSize) + "\n" +
    "FW.message=" + firmwareUploadMessage + "\n" +
    "WiFi.mode=" + getWifiMode() + "\n" +
    "WiFi.ip=" + wifiIpAddress + "\n" +
    "WiFi.status=" + wifiStatus + "\n" +
    "Config.restartPending=" + String(configRestartPending ? 1 : 0) + "\n");
}

static void setWiFiStatus(const String& status) {
  wifiStatus = status;
  wifiStatusMillis = millis();
}

static void registerWebRoutes() {
  server.on("/", handleClientRequests);
  server.on("/gcode/add", HTTP_POST, handleGcodeAdd);
  server.on("/gcode/list", HTTP_GET, handleGcodeList);
  server.on("/gcode/get", HTTP_GET, handleGcodeGet);
  server.on("/gcode/remove", HTTP_POST, handleGcodeRemove);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigSave);
  server.on("/config/reset", HTTP_POST, handleConfigReset);
  server.on("/keyboard-config", HTTP_GET, handleKeyboardConfigGet);
  server.on("/keyboard-config", HTTP_POST, handleKeyboardConfigSave);
  server.on("/keyboard-config/reset", HTTP_POST, handleKeyboardConfigReset);
  server.on("/keyboard-capture", HTTP_POST, handleKeyboardCapture);
  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/tft/upload", HTTP_POST, handleTftUploadResult, handleTftUpload);
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUploadResult, handleFirmwareUpload);
  server.on("/status", HTTP_GET, handleStatus);
}

static String getStationWifiError() {
  if (WiFi.status() == WL_NO_SSID_AVAIL) return "No SSID";
  if (WiFi.status() == WL_CONNECT_FAILED) return "WiFi failed";
  if (WiFi.status() == WL_CONNECTION_LOST) return "WiFi lost";
  if (WiFi.status() == WL_DISCONNECTED) return "WiFi disconnected";
  return "WiFi error";
}

static bool connectStationWifi() {
  wifiSetupApActive = false;
  wifiStationConnected = false;
  wifiIpAddress = "";

  if (WIFI_SSID.length() == 0) {
    setWiFiStatus("No SSID");
    return false;
  }

  if (WIFI_HOSTNAME.length() > 0) {
    DEBUG_PRINTF("Setting WiFi hostname to: %s\n", WIFI_HOSTNAME.c_str());
    WiFi.setHostname(WIFI_HOSTNAME.c_str());
  } else {
    DEBUG_PRINTLN("WIFI_HOSTNAME is empty! Falling back to default.");
    WiFi.setHostname("nanoels");
  }

  WiFi.mode(WIFI_STA);
  
  if (WIFI_HOSTNAME.length() > 0) {
    WiFi.setHostname(WIFI_HOSTNAME.c_str());
  }
  
  DEBUG_PRINTF("Connecting to WiFi SSID: %s\n", WIFI_SSID.c_str());
  WiFi.begin(WIFI_SSID.c_str(), WIFI_PASSWORD.c_str());
  setWiFiStatus("Connecting");
  for (int i = 0; i < 40; i++) {
    if (WiFi.status() == WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi connected successfully!");
      break;
    }
    DEBUG_PRINT(".");
    DELAY(500);
    taskYIELD();
  }
  if (WiFi.status() != WL_CONNECTED) {
    DEBUG_PRINTLN("");
    String errorMsg = getStationWifiError();
    DEBUG_PRINTF("WiFi connection failed. Status: %d, Error: %s\n", WiFi.status(), errorMsg.c_str());
    setWiFiStatus(errorMsg);
    return false;
  }
  DEBUG_PRINTLN("");

  wifiStationConnected = true;
  wifiIpAddress = WiFi.localIP().toString();
  
  if (WIFI_HOSTNAME.length() > 0) {
    if (MDNS.begin(WIFI_HOSTNAME.c_str())) {
      DEBUG_PRINTF("mDNS responder started: http://%s.local\n", WIFI_HOSTNAME.c_str());
    } else {
      DEBUG_PRINTLN("Error setting up mDNS responder!");
    }
  }

  setWiFiStatus("See " + wifiIpAddress);
  DEBUG_PRINT("Web UI: http://");
  DEBUG_PRINTLN(wifiIpAddress);
  return true;
}

static bool startSetupAccessPoint() {
  WiFi.disconnect(true);
  DEBUG_PRINTLN("Starting Setup Access Point...");
  WiFi.mode(WIFI_AP);
  wifiSetupApActive = WiFi.softAP(WIFI_SETUP_AP_SSID, WIFI_SETUP_AP_PASSWORD);
  wifiStationConnected = false;
  if (!wifiSetupApActive) {
    DEBUG_PRINTLN("WiFi softAP creation failed!");
    wifiIpAddress = "";
    setWiFiStatus("WiFi AP failed");
    return false;
  }
  DEBUG_PRINTLN("WiFi softAP created successfully.");

  wifiIpAddress = WiFi.softAPIP().toString();
  setWiFiStatus("Setup " + wifiIpAddress);
  DEBUG_PRINT("Setup AP: ");
  DEBUG_PRINT(WIFI_SETUP_AP_SSID);
  DEBUG_PRINT(" / ");
  DEBUG_PRINTLN(WIFI_SETUP_AP_PASSWORD);
  DEBUG_PRINT("Web UI: http://");
  DEBUG_PRINTLN(wifiIpAddress);
  return true;
}

void taskWiFi(void *param) {
  DEBUG_PRINTLN("Starting WiFi task...");
  if (!connectStationWifi() && !startSetupAccessPoint()) {
    DEBUG_PRINTLN("Both Station and AP modes failed to start. Stopping WiFi task.");
    vTaskDelete(NULL);
    return;
  }

  initBuffer(&inBuffer, 1024);
  initBuffer(&outBuffer, 1024);
  webBuffersReady = true;

  registerWebRoutes();
  server.serveStatic("/", LittleFS, "/www/");
  server.begin();

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);

  while (emergencyStop == ESTOP_NONE) {
    server.handleClient();
    webSocket.loop();
    if (wifiStationConnected && WiFi.status() != WL_CONNECTED) {
      DEBUG_PRINTLN("WiFi connection lost! Disconnected.");
      requestWebUiMoveRelease();
      webSocketClientCount = 0;
      wifiStationConnected = false;
      wifiIpAddress = "";
      setWiFiStatus("WiFi lost");
    }
    if (firmwareUploadRestartPending && ((long)(millis() - firmwareUploadRestartAt) >= 0)) {
      firmwareUploadLog("restarting now");
      DELAY(50);
      ESP.restart();
    }
    if (configRestartPending && ((long)(millis() - configRestartAt) >= 0)) {
      DEBUG_PRINTLN("Machine config saved, restarting now");
      DELAY(50);
      ESP.restart();
    }

    if (bufferAvailable(&outBuffer)) {
      String outData = "";
      while (bufferAvailable(&outBuffer)) {
        outData += shiftBuffer(&outBuffer);
      }
      webSocket.broadcastTXT(outData);
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}
