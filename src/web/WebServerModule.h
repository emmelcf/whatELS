#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "Config.h"

struct CircleBuffer {
  char* buffer;
  size_t head;
  size_t tail;
  size_t size;
  SemaphoreHandle_t mutex;
};

extern CircleBuffer inBuffer;
extern CircleBuffer outBuffer;
extern volatile bool webBuffersReady;


enum ClientChannel : uint8_t {
  CHANNEL_NONE      = 0,
  CHANNEL_CANONICAL = 1 << 0,
  CHANNEL_GRBL      = 1 << 1,
  CHANNEL_DRO       = 1 << 2
};

struct ClientSession {
  uint8_t clientId;
  bool active;
  ClientChannel channel;
  uint32_t lastPushMillis;
};

extern ClientSession clientSessions[WEBSOCKETS_SERVER_CLIENT_MAX];
extern uint8_t activeChannelMask;

extern WebServer server;
extern WebSocketsServer webSocket;
extern volatile int webSocketClientCount;
extern String lastMachineStatus;
extern String wifiStatus;
extern unsigned long wifiStatusMillis;
extern bool wifiSetupApActive;
extern bool wifiStationConnected;
extern String wifiIpAddress;


extern volatile bool firmwareUploadActive;
extern bool firmwareUploadFailed;
extern int firmwareUploadHttpStatus;
extern String firmwareUploadMessage;
extern size_t firmwareUploadExpectedSize;
extern size_t firmwareUploadReceivedSize;
extern int firmwareUploadProgressPercent;
extern bool firmwareUploadRestartPending;
extern unsigned long firmwareUploadRestartAt;
extern bool configRestartPending;
extern unsigned long configRestartAt;

extern bool keyboardCaptureActive;
extern byte keyboardCaptureReleaseCode;
extern unsigned long keyboardCaptureUntil;

bool bufferAvailable(CircleBuffer* b);
bool writeBuffer(CircleBuffer* b, char c);
bool writeBuffer(CircleBuffer* b, const char* str);
bool writeBuffer(CircleBuffer* b, const String& str);
bool writeBuffer(CircleBuffer* b, float f, int precision);
char shiftBuffer(CircleBuffer* b);
void initBuffer(CircleBuffer* b, size_t size);
void clearBuffer(CircleBuffer* b);

bool webUiIsConnected();
void queueWebSocketText(const String& text);
void publishMachineStatus();

void requestWebUiMoveRelease();

int getGcodeProgramCount();
String getCurrentGcodeProgramName();
String readGcodeProgram(const String& name);
bool saveGcode(const String& name, const String& content);
void removeGcodeByName(const String& name);
bool removeAllGcode();

void taskWiFi(void *param);
