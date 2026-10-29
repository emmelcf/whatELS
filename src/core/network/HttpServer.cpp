#include "HttpServer.h"
#include <esp_log.h>

static const char* TAG = "HTTP";

namespace whatels {
namespace core {
namespace network {

HttpServer& HttpServer::instance() {
  static HttpServer s_instance;
  return s_instance;
}

HttpServer::HttpServer()
  : _server(nullptr), _port(80), _running(false) {
}

HttpServer::~HttpServer() {
  stop();
}

bool HttpServer::begin(uint16_t port) {
  if (_running) {
    ESP_LOGW(TAG, "HttpServer already running, restarting on port %u", port);
    stop();
  }

  _port = port;
  ESP_LOGI(TAG, "Starting HttpServer on port %u...", _port);

  _server = std::unique_ptr<WebServer>(new WebServer(_port));
  if (!_server) {
    ESP_LOGE(TAG, "Failed to create WebServer instance");
    return false;
  }

  registerRoutes();
  _server->begin();
  _running = true;

  ESP_LOGI(TAG, "HttpServer listening on port %u", _port);
  return true;
}

void HttpServer::stop() {
  if (_server) {
    ESP_LOGI(TAG, "Stopping HttpServer...");
    _server->stop();
    _server.reset();
  }
  _running = false;
  _otaRestartPending = false;
}

void HttpServer::loop() {
  if (!_running || !_server) {
    return;
  }

  // Non-blocking client handling
  _server->handleClient();

  // Non-blocking delayed restart after successful OTA flash
  if (_otaRestartPending && (static_cast<long>(millis() - _otaRestartAt) >= 0)) {
    _otaRestartPending = false;
    ESP_LOGI(TAG, "Rebooting controller after successful OTA firmware flash...");
    ESP.restart();
  }
}

void HttpServer::registerRoutes() {
  if (!_server) return;

  // Root index handler
  _server->on("/", HTTP_GET, [this]() {
    if (!serveStaticFile("/index.html")) {
      handleNotFound();
    }
  });

  // Alias for canonical schema endpoint
  _server->on("/api/schema/profile", HTTP_GET, [this]() {
    if (!serveStaticFile("/schemas/lathe-profile.schema.json")) {
      _server->sendHeader("Cache-Control", "no-cache");
      _server->send(404, "application/json", "{\"error\":\"Schema not found\"}");
    }
  });

  // OTA firmware upload endpoint
  _server->on(
    "/firmware/upload",
    HTTP_POST,
    [this]() { handleFirmwareUploadResult(); },
    [this]() { handleFirmwareUpload(); }
  );

  // Catch-all handler for LittleFS static assets, /schemas/, /profiles/, and SPA fallback
  _server->onNotFound([this]() {
    handleNotFound();
  });
}

void HttpServer::handleNotFound() {
  if (!_server) return;

  String uri = _server->uri();

  // 1. Schemas & Profiles:
  // If requested under /schemas/ or /profiles/, serve the file directly.
  // If not found, return 404 JSON error instead of falling back to SPA HTML.
  if (uri.startsWith("/schemas/") || uri.startsWith("/profiles/")) {
    if (serveStaticFile(uri)) {
      return;
    }
    _server->sendHeader("Cache-Control", "no-cache");
    _server->send(404, "application/json", "{\"error\":\"Resource not found\"}");
    return;
  }

  // 2. Direct static asset serving for requested URI
  if (serveStaticFile(uri)) {
    return;
  }

  // 3. SPA Fallback:
  // For GET requests where the static file was not found, serve /index.html
  if (_server->method() == HTTP_GET) {
    if (serveStaticFile("/index.html")) {
      return;
    }
  }

  // 4. Default 404 response
  _server->sendHeader("Cache-Control", "no-cache");
  _server->send(404, "text/plain", "404 Not Found");
}

bool HttpServer::serveStaticFile(const String& rawPath) {
  if (!_server) return false;

  String path = rawPath;
  if (path.isEmpty() || path == "/") {
    path = "/index.html";
  }

  auto tryStreamFile = [this, &path](const String& candidatePath, bool isGzip) -> bool {
    if (!LittleFS.exists(candidatePath)) {
      return false;
    }

    File file = LittleFS.open(candidatePath, "r");
    if (!file || file.isDirectory()) {
      if (file) file.close();
      return false;
    }

    String mime = getMimeType(path);
    if (isGzip) {
      _server->sendHeader("Content-Encoding", "gzip");
    }

    // Set appropriate Cache-Control headers
    if (path.endsWith(".html") || path == "/index.html") {
      _server->sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    } else {
      _server->sendHeader("Cache-Control", "public, max-age=86400");
    }

    _server->streamFile(file, mime);
    file.close();
    return true;
  };

  // 1. Check direct path + .gz (if not already ending in .gz)
  if (!path.endsWith(".gz")) {
    if (tryStreamFile(path + ".gz", true)) {
      return true;
    }
  }

  // 2. Check direct uncompressed path
  if (tryStreamFile(path, false)) {
    return true;
  }

  // 3. Fallback checks under /www prefix (e.g. data/www assets)
  if (!path.startsWith("/www")) {
    if (!path.endsWith(".gz")) {
      if (tryStreamFile("/www" + path + ".gz", true)) {
        return true;
      }
    }
    if (tryStreamFile("/www" + path, false)) {
      return true;
    }
  }

  return false;
}

void HttpServer::handleFirmwareUpload() {
  if (!_server) return;

  HTTPUpload& upload = _server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      _otaSuccess = false;
      _otaErrorMsg = "";
      _otaReceivedSize = 0;
      _otaRestartPending = false;

      String filename = upload.filename;
      filename.toLowerCase();
      ESP_LOGI(TAG, "OTA firmware upload started: file=%s", upload.filename.c_str());

      if (!filename.endsWith(".bin")) {
        _otaSuccess = false;
        _otaErrorMsg = "Invalid file type: only .bin firmware images are permitted";
        ESP_LOGE(TAG, "%s", _otaErrorMsg.c_str());
        return;
      }

      size_t fileSize = _server->hasArg("size")
        ? static_cast<size_t>(_server->arg("size").toInt())
        : upload.totalSize;
      size_t updateSize = (fileSize > 0) ? fileSize : UPDATE_SIZE_UNKNOWN;

      if (!Update.begin(updateSize, U_FLASH)) {
        _otaSuccess = false;
        _otaErrorMsg = String("OTA begin failed: ") + Update.errorString();
        ESP_LOGE(TAG, "%s", _otaErrorMsg.c_str());
        return;
      }

      _otaTotalSize = fileSize;
      ESP_LOGI(TAG, "OTA Update.begin() initialized (target size: %u)", (unsigned int)fileSize);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (_otaErrorMsg.length() > 0) {
        // Earlier stage failed, discard further chunks
        return;
      }

      size_t written = Update.write(upload.buf, upload.currentSize);
      _otaReceivedSize += written;

      if (written != upload.currentSize) {
        _otaSuccess = false;
        _otaErrorMsg = String("OTA write failed: ") + Update.errorString();
        ESP_LOGE(TAG, "%s (wrote %u of %u)", _otaErrorMsg.c_str(), (unsigned int)written, (unsigned int)upload.currentSize);
        Update.abort();
        return;
      }
      break;
    }

    case UPLOAD_FILE_END: {
      if (_otaErrorMsg.length() > 0) {
        Update.abort();
        return;
      }

      if (_otaTotalSize > 0 && _otaReceivedSize != _otaTotalSize) {
        _otaSuccess = false;
        _otaErrorMsg = "OTA size mismatch: expected " + String((unsigned long)_otaTotalSize) +
                       " bytes, received " + String((unsigned long)_otaReceivedSize) + " bytes";
        ESP_LOGE(TAG, "%s", _otaErrorMsg.c_str());
        Update.abort();
        return;
      }

      if (Update.end(true)) {
        _otaSuccess = true;
        ESP_LOGI(TAG, "OTA firmware upload successful (%u bytes). Ready to restart.", (unsigned int)_otaReceivedSize);
      } else {
        _otaSuccess = false;
        _otaErrorMsg = String("OTA finalize failed: ") + Update.errorString();
        ESP_LOGE(TAG, "%s", _otaErrorMsg.c_str());
        Update.abort();
      }
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      Update.abort();
      _otaSuccess = false;
      _otaErrorMsg = "OTA upload aborted by client";
      ESP_LOGW(TAG, "%s", _otaErrorMsg.c_str());
      break;
    }
  }
}

void HttpServer::handleFirmwareUploadResult() {
  if (!_server) return;

  _server->sendHeader("Connection", "close");

  if (_otaSuccess) {
    String json = "{\"status\":\"ok\",\"message\":\"Firmware upload successful. Controller restarting...\",\"bytes\":";
    json += String((unsigned long)_otaReceivedSize);
    json += "}";
    _server->send(200, "application/json", json);

    // Schedule reboot with a grace period so HTTP 200 response flushes to client
    _otaRestartPending = true;
    _otaRestartAt = millis() + 1500;
  } else {
    String msg = _otaErrorMsg.isEmpty() ? "Firmware upload failed" : _otaErrorMsg;
    String json = "{\"status\":\"error\",\"message\":\"" + msg + "\"}";
    _server->send(500, "application/json", json);
  }
}

String HttpServer::getMimeType(const String& path) {
  String p = path;
  if (p.endsWith(".gz")) {
    p = p.substring(0, p.length() - 3);
  }

  if (p.endsWith(".html") || p.endsWith(".htm")) {
    return "text/html";
  } else if (p.endsWith(".js")) {
    return "application/javascript";
  } else if (p.endsWith(".css")) {
    return "text/css";
  } else if (p.endsWith(".json")) {
    return "application/json";
  } else if (p.endsWith(".png")) {
    return "image/png";
  } else if (p.endsWith(".svg") || p.endsWith(".svgz")) {
    return "image/svg+xml";
  } else if (p.endsWith(".ico")) {
    return "image/x-icon";
  } else if (p.endsWith(".jpg") || p.endsWith(".jpeg")) {
    return "image/jpeg";
  } else if (p.endsWith(".gif")) {
    return "image/gif";
  } else if (p.endsWith(".xml")) {
    return "text/xml";
  } else if (p.endsWith(".pdf")) {
    return "application/x-pdf";
  } else if (p.endsWith(".zip")) {
    return "application/x-zip";
  } else if (p.endsWith(".txt")) {
    return "text/plain";
  }

  return "application/octet-stream";
}

} // namespace network
} // namespace core
} // namespace whatels
