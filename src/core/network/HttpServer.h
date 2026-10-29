#pragma once

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
  void handleFirmwareUpload();
  void handleFirmwareUploadResult();

  static String getMimeType(const String& path);

  std::unique_ptr<WebServer> _server;
  uint16_t _port{80};
  bool _running{false};

  // OTA state tracking
  bool _otaRestartPending{false};
  uint32_t _otaRestartAt{0};
  bool _otaSuccess{false};
  String _otaErrorMsg{""};
  size_t _otaTotalSize{0};
  size_t _otaReceivedSize{0};
};

} // namespace network
} // namespace core
} // namespace whatels
