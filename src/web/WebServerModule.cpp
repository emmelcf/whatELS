#include "WebServerModule.h"
#include "Config.h"
#include "Axis.h"
#include "Debug.h"
#include "Input.h"
#include "Display.h"
#include "Motion.h"
#include "CommandDispatcher.h"
#include "RichClientAdapter.h"
#include "ClientEndpoint.h"
#include "SettingsService.h"
#include "ControllerWebAssets.h"
#include "SystemSchemaJson.h"
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

ClientSession clientSessions[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
uint8_t activeChannelMask = CHANNEL_NONE;
static SemaphoreHandle_t wsMutex = NULL;

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
  if (b->mutex && xSemaphoreTake(b->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    bool ok = false;
    if ((b->head + 1) % b->size != b->tail) {
      b->buffer[b->head] = c;
      b->head = (b->head + 1) % b->size;
      ok = true;
    }
    xSemaphoreGive(b->mutex);
    return ok;
  }
  if ((b->head + 1) % b->size == b->tail) {
    return false;
  }
  b->buffer[b->head] = c;
  b->head = (b->head + 1) % b->size;
  return true;
}

bool writeBuffer(CircleBuffer* b, const char* str) {
  if (b->mutex && xSemaphoreTake(b->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    bool ok = true;
    while (*str) {
      if ((b->head + 1) % b->size == b->tail) {
        ok = false;
        break;
      }
      b->buffer[b->head] = *str++;
      b->head = (b->head + 1) % b->size;
    }
    xSemaphoreGive(b->mutex);
    return ok;
  }
  while (*str) {
    if ((b->head + 1) % b->size == b->tail) {
      return false;
    }
    b->buffer[b->head] = *str++;
    b->head = (b->head + 1) % b->size;
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
  if (b->mutex && xSemaphoreTake(b->mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    char c = 0;
    if (b->head != b->tail) {
      c = b->buffer[b->tail];
      b->tail = (b->tail + 1) % b->size;
    }
    xSemaphoreGive(b->mutex);
    return c;
  }
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
  if (!b->mutex) {
    b->mutex = xSemaphoreCreateMutex();
  }
}

void clearBuffer(CircleBuffer* b) {
  if (b->mutex && xSemaphoreTake(b->mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
    b->head = 0;
    b->tail = 0;
    xSemaphoreGive(b->mutex);
  } else {
    b->head = 0;
    b->tail = 0;
  }
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

static const unsigned long MIN_PUSH_INTERVAL_MS = 35; // ~28 Hz max rate ceiling
static unsigned long lastPushMillis = 0;
static MachineModel lastSentModel = {};
static bool hasInitialSent = false;

static void dispatchTelemetry() {
  if (!webBuffersReady || activeChannelMask == 0) return;

  unsigned long now = millis();
  if (!machineStatusForcePublish && (now - lastPushMillis < MIN_PUSH_INTERVAL_MS)) {
    return;
  }

  MachineModel current = MachineModel::captureAll();
  uint8_t changedDomains = DOMAIN_NONE;
  if (!hasInitialSent || machineStatusForcePublish) {
    changedDomains = DOMAIN_ALL;
    machineStatusForcePublish = false;
    hasInitialSent = true;
  } else {
    changedDomains = MachineModel::detectChangedDomains(lastSentModel, current);
  }

  if (changedDomains == DOMAIN_NONE) {
    return; // Lathe is idle: zero packets sent! True quiescence.
  }

  lastPushMillis = now;
  lastSentModel = current;

  // Single-pass serialization per active channel
  String canonicalPayload = "";
  String grblPayload = "";
  String droPayload = "";

  if (activeChannelMask & CHANNEL_CANONICAL) {
    canonicalPayload = RichClientAdapter::buildTelemetry(current, changedDomains);
  }
  if ((activeChannelMask & CHANNEL_GRBL) && (changedDomains & DOMAIN_KINEMATICS)) {
    grblPayload = GrblSerializer::buildStatus(current);
  }
  if ((activeChannelMask & CHANNEL_DRO) && (changedDomains & DOMAIN_KINEMATICS)) {
    droPayload = DroSerializer::buildStatus(current);
  }

  for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
    if (!clientSessions[i].active) continue;
    if (clientSessions[i].channel == CHANNEL_CANONICAL && canonicalPayload.length() > 0) {
      webSocket.sendTXT(i, canonicalPayload);
    } else if (clientSessions[i].channel == CHANNEL_GRBL && grblPayload.length() > 0) {
      webSocket.sendTXT(i, grblPayload);
    } else if (clientSessions[i].channel == CHANNEL_DRO && droPayload.length() > 0) {
      webSocket.sendTXT(i, droPayload);
    }
  }
}

void publishMachineStatus(const String& status, const String& modeText, const String& pitchText, const String& measureText,
    const String& stepText, const String& rpmText, const String& turnsText, const String& angleText,
    const String& xText, const String& xLeftText, const String& xRightText,
    const String& yText, const String& yLeftText, const String& yRightText,
    const String& zText, const String& zLeftText, const String& zRightText, const String& messageText,
    bool isDiameterMode, bool isAbsoluteMode, bool xIsDroActive, bool zIsDroActive) {
  if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    dispatchTelemetry();
    xSemaphoreGiveRecursive(wsMutex);
  } else if (!wsMutex) {
    dispatchTelemetry();
  }
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

static bool handleWebUiEventCommand(uint8_t num, uint8_t* payload, size_t length) {
  // First, delegate to CanonicalCommandTranslator for sanitized parsing and canonical dispatch
  CanonicalCommandTranslator translator(num);
  String reply = "";
  if (translator.translateAndDispatch((const char*)payload, length, &reply)) {
    if (reply.length() > 0) {
      webSocket.sendTXT(num, reply);
    }
    bool isQuery = (length >= 4 && strncmp((const char*)payload, "?get", 4) == 0) ||
                   (length >= 6 && strncmp((const char*)payload, "?state", 6) == 0) ||
                   (length >= 8 && strncmp((const char*)payload, "?sysinfo", 8) == 0) ||
                   (length >= 12 && strncasecmp((const char*)payload, "SETTINGS.get", 12) == 0) ||
                   (length >= 16 && strncasecmp((const char*)payload, "CMD.settings.get", 16) == 0);
    if (!isQuery) {
      machineStatusForcePublish = true;
    }
    return true;
  }

  // Next, try parsing as physical/virtual button event
  WebUiEvent event = {};
  String error = "";
  if (!parseWebUiEventCommand(payload, length, &event, &error)) {
    return false;
  }
  if (error.length() > 0) {
    webSocket.sendTXT(num, String("WEBUI.error=") + error + "\n");
    return true;
  }
  if (!queueWebUiEvent(event.actionCode, event.isPress)) {
    webSocket.sendTXT(num, "WEBUI.error=event queue full\n");
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
    ClientChannel ch = CHANNEL_CANONICAL;
    if (payload && length > 0) {
      if (strncmp((const char*)payload, "/grbl", 5) == 0) {
        ch = CHANNEL_GRBL;
      } else if (strncmp((const char*)payload, "/dro", 4) == 0) {
        ch = CHANNEL_DRO;
      } else {
        ch = CHANNEL_CANONICAL;
      }
    }
    if (num < WEBSOCKETS_SERVER_CLIENT_MAX) {
      clientSessions[num].clientId = num;
      clientSessions[num].active = true;
      clientSessions[num].channel = ch;
      clientSessions[num].lastPushMillis = 0;
    }
    activeChannelMask = CHANNEL_NONE;
    webSocketClientCount = 0;
    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
      if (clientSessions[i].active) {
        activeChannelMask |= clientSessions[i].channel;
        webSocketClientCount++;
      }
    }

    IPAddress ip = webSocket.remoteIP(num);
    const char* chName = (ch == CHANNEL_GRBL) ? "GRBL" : ((ch == CHANNEL_DRO) ? "DRO" : "CANONICAL");
    Serial.printf("[WS] Client [%u] connected from %s (Channel: %s, Total: %d, Free Heap: %u bytes)\n",
                  num, ip.toString().c_str(), chName, webSocketClientCount, (unsigned int)ESP.getFreeHeap());

    // Unicast initial hydration frame strictly to the connecting client 'num'
    MachineModel hydrationModel = MachineModel::captureAll();
    if (ch == CHANNEL_CANONICAL) {
      String hydration = RichClientAdapter::buildTelemetry(hydrationModel, DOMAIN_ALL);
      webSocket.sendTXT(num, hydration);
    } else if (ch == CHANNEL_GRBL) {
      String hydration = GrblSerializer::buildStatus(hydrationModel);
      webSocket.sendTXT(num, hydration);
    } else if (ch == CHANNEL_DRO) {
      String hydration = DroSerializer::buildStatus(hydrationModel);
      webSocket.sendTXT(num, hydration);
    }
  } else if (type == WStype_DISCONNECTED) {
    if (num < WEBSOCKETS_SERVER_CLIENT_MAX) {
      clientSessions[num].active = false;
      clientSessions[num].channel = CHANNEL_NONE;
    }
    activeChannelMask = CHANNEL_NONE;
    webSocketClientCount = 0;
    for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
      if (clientSessions[i].active) {
        activeChannelMask |= clientSessions[i].channel;
        webSocketClientCount++;
      }
    }
    Serial.printf("[WS] Client [%u] disconnected (Remaining: %d, Free Heap: %u bytes)\n",
                  num, webSocketClientCount, (unsigned int)ESP.getFreeHeap());
    requestWebUiMoveRelease();
  } else if (type == WStype_ERROR) {
    Serial.printf("[WS] Client [%u] error! (Payload len: %u, Free Heap: %u bytes)\n",
                  num, (unsigned int)length, (unsigned int)ESP.getFreeHeap());
  } else if (type == WStype_TEXT) {
    String cmdStr = "";
    for (size_t i = 0; i < length; i++) {
      if (payload[i] >= 32 && payload[i] <= 126) cmdStr += (char)payload[i];
    }
    if (cmdStr != "?" && cmdStr != "?sysinfo") {
      Serial.printf("WebSocket Command [%d]: %s\n", num, cmdStr.c_str());
    }

    // Direct routing for GRBL CNC stream
    if (num < WEBSOCKETS_SERVER_CLIENT_MAX && clientSessions[num].channel == CHANNEL_GRBL) {
      for (size_t i = 0; i < length; i++) {
        writeBuffer(&inBuffer, payload[i]);
      }
      return;
    }

    if (handleWebUiEventCommand(num, payload, length)) {
      return;
    }
    webSocket.sendTXT(num, "CMD.error=unknown_command\n");
  }
}

static void handleClientIndex() {
  if (LittleFS.exists("/www/index.html")) {
    File file = LittleFS.open("/www/index.html", "r");
    if (file) {
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.sendHeader("Pragma", "no-cache");
      server.sendHeader("Expires", "0");
      server.streamFile(file, "text/html");
      file.close();
      return;
    }
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "text/html", (const char*)WWW_INDEX_HTML_GZ, WWW_INDEX_HTML_GZ_LEN);
}

static void handleClientStyle() {
  if (LittleFS.exists("/www/style.css")) {
    File file = LittleFS.open("/www/style.css", "r");
    if (file) {
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.streamFile(file, "text/css");
      file.close();
      return;
    }
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "text/css", (const char*)WWW_STYLE_CSS_GZ, WWW_STYLE_CSS_GZ_LEN);
}

static void handleClientAppJs() {
  if (LittleFS.exists("/www/app.js")) {
    File file = LittleFS.open("/www/app.js", "r");
    if (file) {
      server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
      server.streamFile(file, "application/javascript");
      file.close();
      return;
    }
  }
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "application/javascript", (const char*)WWW_APP_JS_GZ, WWW_APP_JS_GZ_LEN);
}

static void handleSchemaGet() {
  server.sendHeader("Cache-Control", "public, max-age=86400");
  if (LittleFS.exists("/schemas/lathe-profile.schema.json")) {
    File file = LittleFS.open("/schemas/lathe-profile.schema.json", "r");
    if (file) {
      server.streamFile(file, "application/json");
      file.close();
      return;
    }
  }
  server.send(200, "application/json", LATHE_PROFILE_SCHEMA_JSON);
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
  server.on("/", handleClientIndex);
  server.on("/index.html", handleClientIndex);
  server.on("/style.css", handleClientStyle);
  server.on("/app.js", handleClientAppJs);
  server.on("/api/schema/profile", HTTP_GET, handleSchemaGet);
  server.on("/schemas/lathe-profile.schema.json", HTTP_GET, handleSchemaGet);
  server.on("/gcode/add", HTTP_POST, handleGcodeAdd);
  server.on("/gcode/list", HTTP_GET, handleGcodeList);
  server.on("/gcode/get", HTTP_GET, handleGcodeGet);
  server.on("/gcode/remove", HTTP_POST, handleGcodeRemove);

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
  initBuffer(&outBuffer, 4096);
  webBuffersReady = true;

  registerWebRoutes();
  server.serveStatic("/", LittleFS, "/www/");
  server.begin();

  if (!wsMutex) {
    wsMutex = xSemaphoreCreateRecursiveMutex();
  }

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);

  while (emergencyStop == ESTOP_NONE) {
    server.handleClient();
    if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      webSocket.loop();
      dispatchTelemetry();
      xSemaphoreGiveRecursive(wsMutex);
    } else if (!wsMutex) {
      webSocket.loop();
      dispatchTelemetry();
    }

    static unsigned long lastHealthLog = 0;
    if (millis() - lastHealthLog >= 10000) {
      lastHealthLog = millis();
      if (webSocketClientCount > 0) {
        Serial.printf("[HEALTH] WiFi IP: %s, WS clients: %d, Free Heap: %u bytes\n",
                      wifiIpAddress.c_str(), webSocketClientCount, (unsigned int)ESP.getFreeHeap());
      }
    }
    // WiFi connection watchdog & exponential backoff retry strategy
    const unsigned long WIFI_DISCONNECT_DEBOUNCE_MS = 2500;
    const unsigned long WIFI_RETRY_INITIAL_INTERVAL_MS = 3000;
    const unsigned long WIFI_RETRY_MAX_INTERVAL_MS = 30000;
    static unsigned long wifiDisconnectStartedAt = 0;
    static unsigned long wifiNextRetryAt = 0;
    static unsigned long wifiRetryIntervalMs = WIFI_RETRY_INITIAL_INTERVAL_MS;

    if (wifiStationConnected) {
      if (WiFi.status() != WL_CONNECTED) {
        if (wifiDisconnectStartedAt == 0) {
          wifiDisconnectStartedAt = millis();
        } else if (millis() - wifiDisconnectStartedAt >= WIFI_DISCONNECT_DEBOUNCE_MS) {
          DEBUG_PRINTLN("WiFi connection lost! Confirmed disconnected after debounce.");
          requestWebUiMoveRelease();
          webSocketClientCount = 0;
          activeChannelMask = CHANNEL_NONE;
          for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
            clientSessions[i].active = false;
            clientSessions[i].channel = CHANNEL_NONE;
          }
          wifiStationConnected = false;
          wifiIpAddress = "";
          wifiDisconnectStartedAt = 0;
          wifiRetryIntervalMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
          wifiNextRetryAt = millis() + wifiRetryIntervalMs;
          setWiFiStatus("WiFi lost");
        }
      } else {
        wifiDisconnectStartedAt = 0;
      }
    } else if (!wifiSetupApActive && WIFI_SSID.length() > 0) {
      if (WiFi.status() == WL_CONNECTED) {
        wifiStationConnected = true;
        wifiIpAddress = WiFi.localIP().toString();
        wifiDisconnectStartedAt = 0;
        wifiRetryIntervalMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
        setWiFiStatus("See " + wifiIpAddress);
        DEBUG_PRINTLN("WiFi reconnected successfully!");
      } else if ((long)(millis() - wifiNextRetryAt) >= 0) {
        DEBUG_PRINTF("WiFi retrying connection (interval %lu ms)...\n", wifiRetryIntervalMs);
        setWiFiStatus("WiFi retry (" + String(wifiRetryIntervalMs / 1000) + "s)");
        WiFi.reconnect();
        wifiRetryIntervalMs = min(wifiRetryIntervalMs * 2, WIFI_RETRY_MAX_INTERVAL_MS);
        wifiNextRetryAt = millis() + wifiRetryIntervalMs;
      }
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
      if (outBuffer.mutex && xSemaphoreTake(outBuffer.mutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (outBuffer.head != outBuffer.tail) {
          outData.reserve(1024);
          while (outBuffer.head != outBuffer.tail) {
            outData += outBuffer.buffer[outBuffer.tail];
            outBuffer.tail = (outBuffer.tail + 1) % outBuffer.size;
          }
        }
        xSemaphoreGive(outBuffer.mutex);
      } else if (!outBuffer.mutex) {
        while (bufferAvailable(&outBuffer)) {
          outData += shiftBuffer(&outBuffer);
        }
      }
      if (outData.length() > 0) {
        if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
          webSocket.broadcastTXT(outData);
          xSemaphoreGiveRecursive(wsMutex);
        } else if (!wsMutex) {
          webSocket.broadcastTXT(outData);
        }
      }
    }
    taskYIELD();
  }
  vTaskDelete(NULL);
}
