#include "WifiService.h"
#include "Config.h"
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <algorithm>

static const char* TAG = "WIFI_SVC";

namespace whatels {
namespace core {

static constexpr unsigned long WIFI_DISCONNECT_DEBOUNCE_MS = 2500;
static constexpr unsigned long WIFI_RETRY_INITIAL_INTERVAL_MS = 3000;
static constexpr unsigned long WIFI_RETRY_MAX_INTERVAL_MS = 30000;
static constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000;

WifiService& WifiService::instance() {
  static WifiService inst;
  return inst;
}

void WifiService::setStatus(const String& msg) {
  _statusText = msg;
  ESP_LOGI(TAG, "WiFi Status: %s", msg.c_str());
}

String WifiService::getModeString() const {
  switch (_state) {
    case WifiModeState::OFF: return "off";
    case WifiModeState::STA_CONNECTING: return "connecting";
    case WifiModeState::STA_CONNECTED: return "station";
    case WifiModeState::AP_ACTIVE: return "setup-ap";
    case WifiModeState::FAILED: return "failed";
    default: return "unknown";
  }
}

bool WifiService::isConnected() const {
  return (_state == WifiModeState::STA_CONNECTED && WiFi.status() == WL_CONNECTED);
}

String WifiService::getIpAddress() const {
  if (_state == WifiModeState::STA_CONNECTED) {
    return WiFi.localIP().toString();
  } else if (_state == WifiModeState::AP_ACTIVE) {
    return WiFi.softAPIP().toString();
  }
  return _ipAddress;
}

int8_t WifiService::getRssi() const {
  if (isConnected()) {
    return static_cast<int8_t>(WiFi.RSSI());
  }
  return 0;
}

void WifiService::init() {
  // Disable Wi-Fi modem sleep for ultra-low latency telemetry and HIL testing
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
}

bool WifiService::begin() {
  init();

  if (!sysConfig.wifi.enabled) {
    _state = WifiModeState::OFF;
    setStatus("WiFi disabled");
    return true;
  }

  bool ok = false;
  if (sysConfig.wifi.ssid.length() > 0) {
    ok = connectStation(sysConfig.wifi.ssid, sysConfig.wifi.password, sysConfig.wifi.hostname);
  } else {
    // No station SSID configured -> start fallback Access Point
    ok = startAccessPoint();
  }

  // Register mDNS responder for "whatels.local"
  if (!_mdnsStarted) {
    if (MDNS.begin(getHostname())) {
      _mdnsStarted = true;
      ESP_LOGI(TAG, "mDNS responder registered: http://%s.local", getHostname());
    } else {
      ESP_LOGW(TAG, "Failed to start mDNS responder for %s.local", getHostname());
    }
  }

  return ok;
}

bool WifiService::connectStation(const String& ssid, const String& password, const String& hostname) {
  if (ssid.length() == 0) {
    return startAccessPoint();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  const char* host = (hostname.length() > 0) ? hostname.c_str() : getHostname();
  WiFi.setHostname(host);

  ESP_LOGI(TAG, "Connecting to Station SSID '%s' (hostname '%s')...", ssid.c_str(), host);
  WiFi.begin(ssid.c_str(), password.c_str());

  _state = WifiModeState::STA_CONNECTING;
  _connectStartedAt = millis();
  _currentBackoffMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
  _disconnectStartedAt = 0;
  _nextRetryMs = 0;
  setStatus("Connecting");
  return true;
}

bool WifiService::startAccessPoint() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  const char* apSsid = (sysConfig.wifi.setupApSsid.length() > 0)
                       ? sysConfig.wifi.setupApSsid.c_str()
                       : "whatELS-AP";
  const char* apPass = (sysConfig.wifi.setupApPassword.length() >= 8) ? sysConfig.wifi.setupApPassword.c_str() : nullptr;

  bool ok = WiFi.softAP(apSsid, apPass);
  if (!ok) {
    ESP_LOGE(TAG, "Failed to start SoftAP '%s'!", apSsid);
    _state = WifiModeState::FAILED;
    setStatus("AP failed");
    return false;
  }

  _ipAddress = WiFi.softAPIP().toString();
  _state = WifiModeState::AP_ACTIVE;
  ESP_LOGI(TAG, "SoftAP started: SSID '%s', IP %s", apSsid, _ipAddress.c_str());
  setStatus("Setup " + _ipAddress);

  if (!_mdnsStarted) {
    if (MDNS.begin(getHostname())) {
      _mdnsStarted = true;
      ESP_LOGI(TAG, "mDNS responder registered: http://%s.local", getHostname());
    }
  }
  return true;
}

void WifiService::stop() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  _state = WifiModeState::OFF;
  _ipAddress = "";
  setStatus("WiFi off");
}

std::vector<WifiScanResult> WifiService::scanNetworks() {
  std::vector<WifiScanResult> results;
  int n = WiFi.scanNetworks(false, true); // synchronous scan
  if (n > 0) {
    results.reserve(n);
    for (int i = 0; i < n; ++i) {
      WifiScanResult r;
      r.ssid = WiFi.SSID(i);
      r.rssi = WiFi.RSSI(i);
      r.open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
      if (r.ssid.length() > 0) {
        results.push_back(r);
      }
    }
    WiFi.scanDelete();
  }
  return results;
}

void WifiService::loop() {
  if (_state == WifiModeState::OFF) return;

  unsigned long now = millis();

  if (_state == WifiModeState::STA_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      _state = WifiModeState::STA_CONNECTED;
      _ipAddress = WiFi.localIP().toString();
      _currentBackoffMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
      _disconnectStartedAt = 0;
      _nextRetryMs = 0;

      if (!_mdnsStarted) {
        if (MDNS.begin(getHostname())) {
          _mdnsStarted = true;
          ESP_LOGI(TAG, "mDNS responder registered: http://%s.local", getHostname());
        }
      }
      setStatus("See " + _ipAddress);
      ESP_LOGI(TAG, "Station connected successfully! IP: %s", _ipAddress.c_str());
    } else if (now - _connectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Station connection timed out. Falling back to AP mode (SSID: whatELS-AP)...");
      startAccessPoint();
      _nextRetryMs = now + _currentBackoffMs;
      _currentBackoffMs = std::min(_currentBackoffMs * 2, WIFI_RETRY_MAX_INTERVAL_MS);
    }
    return;
  }

  if (_state == WifiModeState::STA_CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      if (_disconnectStartedAt == 0) {
        _disconnectStartedAt = now;
      } else if (now - _disconnectStartedAt >= WIFI_DISCONNECT_DEBOUNCE_MS) {
        ESP_LOGW(TAG, "Station connection lost. Reconnecting with exponential backoff...");
        _state = WifiModeState::STA_CONNECTING;
        _disconnectStartedAt = 0;
        WiFi.reconnect();
        _connectStartedAt = now;
        _nextRetryMs = now + _currentBackoffMs;
        _currentBackoffMs = std::min(_currentBackoffMs * 2, WIFI_RETRY_MAX_INTERVAL_MS);
        setStatus("WiFi lost, retrying...");
      }
    } else {
      _disconnectStartedAt = 0;
    }
    return;
  }

  // Periodic reconnection retry when running in AP fallback mode and station credentials exist
  if (_state == WifiModeState::AP_ACTIVE && sysConfig.wifi.ssid.length() > 0) {
    if (_nextRetryMs != 0 && (long)(now - _nextRetryMs) >= 0) {
      ESP_LOGD(TAG, "Periodic retry to station SSID '%s' (backoff: %lu ms)...",
               sysConfig.wifi.ssid.c_str(), _currentBackoffMs);
      WiFi.reconnect();
      _nextRetryMs = now + _currentBackoffMs;
      _currentBackoffMs = std::min(_currentBackoffMs * 2, WIFI_RETRY_MAX_INTERVAL_MS);
    }
  }
}

} // namespace core
} // namespace whatels
