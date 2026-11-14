#include "WebServerModule.h"
#include "Config.h"
#include <esp_log.h>

static const char* TAG = "WEB";
#include "core/MachineModel.h"
#include "core/WifiService.h"
#include "core/network/HttpServer.h"
#include "SettingsService.h"
#include <LittleFS.h>
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

WebSocketsServer webSocket(81);
volatile int webSocketClientCount = 0;
String wifiStatus = "No WiFi";
unsigned long wifiStatusMillis = 0;
bool wifiSetupApActive = false;
bool wifiStationConnected = false;
String wifiIpAddress = "";

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

void requestWebUiMoveRelease() {
  MotionEngine::instance().stopAll();
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
    if (cmdStr != "CMD.ping") {
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
      whatels::core_client::ClientManager::instance().onClientMessage(num, payload, length);
      return;
    }
    webSocket.sendTXT(num, "ERR:UNKNOWN_COMMAND\n");
  }
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

  whatels::core::network::HttpServer::instance().begin(80);

  if (!wsMutex) {
    wsMutex = xSemaphoreCreateRecursiveMutex();
  }

  webSocket.begin();
  webSocket.onEvent(handleWebSocketEvent);
  whatels::core_client::ClientManager::instance().init(&webSocket);

  static bool s_wasConnected = false;
  static uint32_t s_lastTelemetryPushMs = 0;

  while (emergencyStop == EstopCode::NONE) {
    whatels::core::network::HttpServer::instance().loop();
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

    auto serviceWebSocket = [&]() {
      webSocket.loop();
      const uint32_t nowMs = millis();
      if (webSocketClientCount > 0 && (nowMs - s_lastTelemetryPushMs >= 100)) {
        s_lastTelemetryPushMs = nowMs;
        whatels::core_client::ClientManager::instance().broadcastIfChanged(MachineModel::captureAll());
      }
    };

    if (wsMutex && xSemaphoreTakeRecursive(wsMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
      serviceWebSocket();
      xSemaphoreGiveRecursive(wsMutex);
    } else if (!wsMutex) {
      serviceWebSocket();
    }

    static unsigned long lastHealthLog = 0;
    if (millis() - lastHealthLog >= 10000) {
      lastHealthLog = millis();
      if (webSocketClientCount > 0) {
        ESP_LOGI(TAG, "[HEALTH] WiFi IP: %s, WS clients: %d, Free Heap: %u bytes",
                 wifiIpAddress.c_str(), webSocketClientCount, (unsigned int)ESP.getFreeHeap());
      }
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
