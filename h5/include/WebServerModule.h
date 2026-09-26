#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "Config.h"
#include "Axis.h"

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

extern QueueHandle_t webUiEventQueue;
extern volatile int webUiEventPendingCount;
extern portMUX_TYPE webUiEventMux;
extern const int WEB_UI_EVENT_QUEUE_LENGTH;
extern const unsigned long WEB_UI_MOVE_TIMEOUT_MS;
extern volatile byte webUiHeldMoveAction;
extern volatile unsigned long webUiHeldMoveMillis;
extern volatile bool webUiMoveReleaseRequested;

extern WebServer server;
extern WebSocketsServer webSocket;
extern volatile int webSocketClientCount;
extern volatile bool machineStatusForcePublish;
extern String lastMachineStatus;
extern String wifiStatus;
extern unsigned long wifiStatusMillis;
extern bool wifiSetupApActive;
extern bool wifiStationConnected;
extern String wifiIpAddress;

extern volatile bool tftUploadActive;
extern bool tftUploadFailed;
extern int tftUploadHttpStatus;
extern String tftUploadMessage;
extern long tftUploadExpectedSize;
extern long tftUploadReceivedSize;
extern long tftUploadSentSize;
extern int tftUploadPacketLength;
extern int tftUploadProgressPercent;
extern long nextionSerialBaud;
extern byte tftUploadPacket[NEXTION_TFT_PACKET_SIZE];
extern const int NEXTION_BUFFER_LENGTH;
extern byte nextionBuffer[];
extern int nextionBufferIndex;
extern byte lastNextionPageId;

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
void publishMachineStatus(const String& statusText, const String& modeText, const String& pitchText, const String& measureText,
    const String& stepText, const String& rpmText, const String& turnsText, const String& angleText,
    const String& xText, const String& xLeftText, const String& xRightText,
    const String& yText, const String& yLeftText, const String& yRightText,
    const String& zText, const String& zLeftText, const String& zRightText,
    const String& messageText, bool isDiameterMode = true, bool isAbsoluteMode = true,
    bool xIsDroActive = false, bool zIsDroActive = false);

bool queueWebUiEvent(byte actionCode, bool isPress);
bool readWebUiEvent(WebUiEvent* event);
bool isWebUiMoveAction(byte actionCode);
void setMoveButtonPressed(byte actionCode, bool isPress);
void releaseWebUiHeldMove();
void requestWebUiMoveRelease();
void processWebUiMoveFailsafe();
void updateWebUiMoveWatchdog(byte actionCode, bool isPress);
void publishKeyboardEvent(byte physicalCode, byte actionCode, bool isPress);
bool shouldConsumeKeyboardCapture(byte physicalCode, bool isPress);

int getGcodeProgramCount();
String getCurrentGcodeProgramName();
String readGcodeProgram(const String& name);
bool saveGcode();
void removeGcodeByName(const String& name);
bool removeAllGcode();

void taskWiFi(void *param);
