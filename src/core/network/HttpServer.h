#pragma once

/**
 * @file HttpServer.h
 * @brief Port 80 appliance HTTP server for static assets, schemas, and OTA firmware uploads.
 * @see docs/WHATELS_MASTER_ARCHITECTURE.md
 * @see docs/clients/web_ui/OVERVIEW.md
 */

#include <Arduino.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <Update.h>
#include <memory>

namespace whatels {
namespace core {
namespace network {

/**
 * @brief Port 80 appliance HTTP server for whatELS.
 *
 * Responsibilities:
 * - Static web asset delivery from LittleFS with transparent .gz support.
 * - Single Page Application (SPA) routing fallback to /index.html.
 * - Machine profile and JSON schema asset serving (/schemas/, /profiles/).
 * - Multi-part OTA firmware uploads (/firmware/upload).
 *
 * Clean Boundary Invariants:
 * - ZERO WebSocket logic (handled on Port 81 via WebSocketTransport).
 * - ZERO motion engine coupling or command dispatching.
 */
class HttpServer {
public:
  static HttpServer& instance();

  bool begin(uint16_t port = 80);
  void loop();
  void stop();

  bool isRunning() const { return _running; }
  uint16_t getPort() const { return _port; }

  // Singleton: non-copyable and non-movable
  HttpServer(const HttpServer&) = delete;
  HttpServer& operator=(const HttpServer&) = delete;
  HttpServer(HttpServer&&) = delete;
  HttpServer& operator=(HttpServer&&) = delete;

private:
  HttpServer();
  ~HttpServer();

  void registerRoutes();
  void handleNotFound();
  bool serveStaticFile(const String& path);
  void serveProgmemGzip(const char* contentType, const uint8_t* data, uint32_t len,
                        const char* cacheControl = "no-cache, no-store, must-revalidate");
  void handleWifiGet();
  void handleWifiSave();
  void handleWifiReset();
  void handleWifiScan();
  void handleStatus();
  bool readBoolConfigArg(const char* name, bool* value, String* error);
  bool readWifiConfigFromRequest(String* error);
  bool isConfigChangeBlocked() const;
  void handleFirmwareUpload();
  void handleFirmwareUploadResult();

  static String getMimeType(const String& path);

  std::unique_ptr<WebServer> _server;
  uint16_t _port{80};
  bool _running{false};

  // OTA & config restart state tracking
  bool _otaInProgress{false};
  bool _otaRestartPending{false};
  uint32_t _otaRestartAt{0};
  bool _otaSuccess{false};
  int _otaHttpStatus{200};
  String _otaErrorMsg{""};
  size_t _otaTotalSize{0};
  size_t _otaReceivedSize{0};
  bool _configRestartPending{false};
  uint32_t _configRestartAt{0};
};

} // namespace network
} // namespace core
} // namespace whatels
