#include "WebServerModule.h"
#include "Config.h"
#include <esp_log.h>

static const char* TAG = "WEB";
#include "core/CommandDispatcher.h"
#include "core/TelemetrySerializer.h"
#include "core_client/ClientSerializers.h"
#include "core/WifiService.h"
#include "SettingsService.h"
#include "ControllerWebAssets.h"
#include "SystemSchemaJson.h"
#include <LittleFS.h>
#include <Update.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include "core_client/ClientManager.h"

#include "motion/kernel/MotionEngine.h"

CircleBuffer inBuffer = {};
CircleBuffer outBuffer = {};
volatile bool webBuffersReady = false;

ClientSession clientSessions[WEBSOCKETS_SERVER_CLIENT_MAX] = {};
uint8_t activeChannelMask = CHANNEL_NONE;
static SemaphoreHandle_t wsMutex = NULL;

WebServer server(80);
WebSocketsServer webSocket(81);
volatile int webSocketClientCount = 0;
static volatile bool machineStatusForcePublish = false;
String lastMachineStatus = "";
String wifiStatus = "No WiFi";
unsigned long wifiStatusMillis = 0;
bool wifiSetupApActive = false;
bool wifiStationConnected = false;
String wifiIpAddress = "";
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

static void appendUiLine(String* response, const char* name, const String& value) {
  appendConfigLine(response, name, webSafeText(value));
}

static const unsigned long MIN_PUSH_INTERVAL_MS = 20; // 50 Hz push cadence ([CONC-01], Slice 3)
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
    canonicalPayload = TelemetrySerializer::buildTelemetry(current, changedDomains);
  }
  if ((activeChannelMask & CHANNEL_GRBL) && (changedDomains & DOMAIN_KINEMATICS)) {
    grblPayload = whatels::core_client::GrblSerializer::buildStatus(current);
  }
  if ((activeChannelMask & CHANNEL_DRO) && (changedDomains & DOMAIN_KINEMATICS)) {
    droPayload = whatels::core_client::DroSerializer::buildStatus(current);
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

void publishMachineStatus() {
  if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
    dispatchTelemetry();
    xSemaphoreGiveRecursive(wsMutex);
  } else if (!wsMutex) {
    dispatchTelemetry();
  }
}

void requestWebUiMoveRelease() {
  MotionEngine::instance().stopAll();
}



static String getWifiMode() {
  return whatels::core::WifiService::instance().getModeString();
}

static void handleWifiScan() {
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  std::vector<whatels::core::WifiScanResult> scan = whatels::core::WifiService::instance().scanNetworks();
  String json = "[";
  for (size_t i = 0; i < scan.size(); ++i) {
    if (i > 0) json += ",";
    json += "{\"ssid\":\"" + scan[i].ssid + "\",\"rssi\":" + String(scan[i].rssi) + ",\"open\":" + (scan[i].open ? "true" : "false") + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

static String getWifiConfigResponse() {
  String response = "";
  response.reserve(360);
  appendConfigLine(&response, "wifiEnabled", sysConfig.wifi.enabled);
  appendConfigLine(&response, "wifiSsid", sysConfig.wifi.ssid);
  appendConfigLine(&response, "wifiHostname", sysConfig.wifi.hostname);
  appendConfigLine(&response, "wifiPasswordSet", sysConfig.wifi.password.length() > 0);
  appendConfigLine(&response, "wifiMode", getWifiMode());
  appendConfigLine(&response, "wifiIp", wifiIpAddress);
  appendConfigLine(&response, "wifiStatus", wifiStatus);
  appendConfigLine(&response, "setupApSsid", sysConfig.wifi.setupApSsid.c_str());
  return response;
}

static bool readWifiConfigFromRequest(String* error) {
  bool enabled = sysConfig.wifi.enabled;
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

  sysConfig.wifi.enabled = enabled;
  sysConfig.wifi.ssid = ssid;
  sysConfig.wifi.hostname = hostname.length() > 0 ? hostname : "whatels";
  if (clearPassword) {
    sysConfig.wifi.password = "";
  } else if (password.length() > 0) {
    sysConfig.wifi.password = password;
  }
  return validateWifiConfig(error);
}

static void scheduleConfigRestart() {
  configRestartPending = true;
  configRestartAt = millis() + 1500;
}


static void firmwareUploadLog(const String& message) {
  String line = String("FW: ") + message + "\n";
  ESP_LOGI(TAG, "%s", message.c_str());
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
    ESP_LOGI(TAG, "[WS] Client [%u] connected from %s (Channel: %s, Total: %d, Free Heap: %u bytes)",
             num, ip.toString().c_str(), chName, webSocketClientCount, (unsigned int)ESP.getFreeHeap());

    // Unicast initial hydration frame strictly to the connecting client 'num'
    MachineModel hydrationModel = MachineModel::captureAll();
    if (ch == CHANNEL_CANONICAL) {
      String hydration = TelemetrySerializer::buildTelemetry(hydrationModel, DOMAIN_ALL);
      webSocket.sendTXT(num, hydration);
    } else if (ch == CHANNEL_GRBL) {
      String hydration = whatels::core_client::GrblSerializer::buildStatus(hydrationModel);
      webSocket.sendTXT(num, hydration);
    } else if (ch == CHANNEL_DRO) {
      String hydration = whatels::core_client::DroSerializer::buildStatus(hydrationModel);
      webSocket.sendTXT(num, hydration);
    }
    whatels::core_client::ClientManager::instance().onClientConnected(num, (const char*)payload);
  } else if (type == WStype_DISCONNECTED) {
    whatels::core_client::ClientManager::instance().onClientDisconnected(num);
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
    ESP_LOGI(TAG, "[WS] Client [%u] disconnected (Remaining: %d, Free Heap: %u bytes)",
             num, webSocketClientCount, (unsigned int)ESP.getFreeHeap());
    requestWebUiMoveRelease();
  } else if (type == WStype_ERROR) {
    ESP_LOGE(TAG, "[WS] Client [%u] error! (Payload len: %u, Free Heap: %u bytes)",
             num, (unsigned int)length, (unsigned int)ESP.getFreeHeap());
  } else if (type == WStype_TEXT) {
    String cmdStr = "";
    for (size_t i = 0; i < length; i++) {
      if (payload[i] >= 32 && payload[i] <= 126) cmdStr += (char)payload[i];
    }
    if (cmdStr != "?" && cmdStr != "?sysinfo") {
      ESP_LOGD(TAG, "WebSocket Command [%d]: %s", num, cmdStr.c_str());
    }

    // Direct routing for GRBL CNC stream
    if (num < WEBSOCKETS_SERVER_CLIENT_MAX && clientSessions[num].channel == CHANNEL_GRBL) {
      for (size_t i = 0; i < length; i++) {
        writeBuffer(&inBuffer, payload[i]);
      }
      return;
    }

    // Native Client routing via ClientManager
    if (num < WEBSOCKETS_SERVER_CLIENT_MAX && clientSessions[num].channel == CHANNEL_CANONICAL) {
      if (whatels::core_client::ClientManager::instance().onClientMessage(num, payload, length)) {
        bool isQuery = (length >= 4 && strncmp((const char*)payload, "?get", 4) == 0) ||
                       (length >= 6 && strncmp((const char*)payload, "?state", 6) == 0) ||
                       (length >= 8 && strncmp((const char*)payload, "?sysinfo", 8) == 0) ||
                       (length >= 12 && strncasecmp((const char*)payload, "SETTINGS.get", 12) == 0) ||
                       (length >= 16 && strncasecmp((const char*)payload, "CMD.settings.get", 16) == 0);
        if (!isQuery) {
          machineStatusForcePublish = true;
        }
        return;
      }
      return;
    }
    webSocket.sendTXT(num, "CMD.error=unknown_command\n");
  }
}

static void handleClientIndex() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "text/html", (const char*)WWW_INDEX_HTML_GZ, WWW_INDEX_HTML_GZ_LEN);
}

static void handleClientStyle() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "text/css", (const char*)WWW_STYLE_CSS_GZ, WWW_STYLE_CSS_GZ_LEN);
}

static void handleClientAppJs() {
  server.sendHeader("Content-Encoding", "gzip");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send_P(200, "application/javascript", (const char*)WWW_APP_JS_GZ, WWW_APP_JS_GZ_LEN);
}

static void handleSchemaGet() {
  server.sendHeader("Cache-Control", "public, max-age=86400");
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

static int s_gcodeProgramIndex = 0;
static int s_gcodeProgramCount = 0;

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
      if (count == s_gcodeProgramIndex) {
        return filename.substring(0, filename.length() - 6);
      }
      count++;
    }
    file = root.openNextFile();
  }

  return "";
}

bool saveGcode(const String& name, const String& content) {
  if (name.length() < 2) {
    writeBuffer(&outBuffer, "error: name must be at least 2 chars\n");
    return false;
  }
  if (content.length() < 2) {
    writeBuffer(&outBuffer, "error: program too short\n");
    return false;
  }

  String filename = "/" + name + ".gcode";
  File file = LittleFS.open(filename, "w");
  if (!file) {
    writeBuffer(&outBuffer, "error: failed to open file\n");
    return false;
  }

  file.print(content);
  file.close();

  writeBuffer(&outBuffer, "success: G-code saved\n");
  s_gcodeProgramCount = getGcodeProgramCount();
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
  s_gcodeProgramCount = getGcodeProgramCount();
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
  s_gcodeProgramCount = 0;
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
    if (saveGcode(server.arg("name"), server.arg("gcode"))) {
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
  return SettingsService::isOn() || firmwareUploadActive || firmwareUploadRestartPending || configRestartPending;
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
  if (SettingsService::isOn()) {
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

  server.on("/wifi", HTTP_GET, handleWifiGet);
  server.on("/wifi", HTTP_POST, handleWifiSave);
  server.on("/wifi/reset", HTTP_POST, handleWifiReset);
  server.on("/api/wifi/scan", HTTP_GET, handleWifiScan);
  server.on("/firmware/upload", HTTP_POST, handleFirmwareUploadResult, handleFirmwareUpload);
  server.on("/status", HTTP_GET, handleStatus);
}

void taskWiFi(void *param) {
  ESP_LOGI(TAG, "Starting WiFi task...");
  whatels::core::WifiService& wifiSvc = whatels::core::WifiService::instance();
  if (!wifiSvc.begin()) {
    ESP_LOGE(TAG, "WiFi service failed to start. Stopping WiFi task.");
    vTaskDelete(NULL);
    return;
  }

  initBuffer(&inBuffer, 1024);
  initBuffer(&outBuffer, 4096);
  webBuffersReady = true;

  registerWebRoutes();
  server.begin();

  if (!wsMutex) {
    wsMutex = xSemaphoreCreateRecursiveMutex();
  }

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  whatels::core_client::ClientManager::instance().init(&webSocket);

  static bool s_wasConnected = false;

  while (emergencyStop == EstopCode::NONE) {
    server.handleClient();
    wifiSvc.update();

    wifiStationConnected = wifiSvc.isStationConnected();
    wifiSetupApActive = wifiSvc.isApActive();
    wifiIpAddress = wifiSvc.getIpAddress();
    wifiStatus = wifiSvc.getStatusString();

    if (s_wasConnected && !wifiStationConnected && !wifiSetupApActive) {
      requestWebUiMoveRelease();
      webSocketClientCount = 0;
      activeChannelMask = CHANNEL_NONE;
      for (uint8_t i = 0; i < WEBSOCKETS_SERVER_CLIENT_MAX; i++) {
        clientSessions[i].active = false;
        clientSessions[i].channel = CHANNEL_NONE;
      }
    }
    s_wasConnected = wifiStationConnected;

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
        ESP_LOGI(TAG, "[HEALTH] WiFi IP: %s, WS clients: %d, Free Heap: %u bytes",
                 wifiIpAddress.c_str(), webSocketClientCount, (unsigned int)ESP.getFreeHeap());
      }
    }

    if (firmwareUploadRestartPending && ((long)(millis() - firmwareUploadRestartAt) >= 0)) {
      firmwareUploadLog("restarting now");
      vTaskDelay(pdMS_TO_TICKS(50));
      ESP.restart();
    }
    if (configRestartPending && ((long)(millis() - configRestartAt) >= 0)) {
      ESP_LOGI(TAG, "Machine config saved, restarting now");
      vTaskDelay(pdMS_TO_TICKS(50));
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
