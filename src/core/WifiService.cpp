#include "WifiService.h"
#include "Config.h"
#include "SettingsService.h"
#include <ESPmDNS.h>
#include <esp_wifi.h>
#include <esp_log.h>

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

  if (sysConfig.wifi.ssid.length() > 0) {
    return connectStation(sysConfig.wifi.ssid, sysConfig.wifi.password, sysConfig.wifi.hostname);
  }

  // No station SSID configured -> start setup Access Point immediately
  return startAccessPoint();
}

bool WifiService::connectStation(const String& ssid, const String& password, const String& hostname) {
  if (ssid.length() == 0) {
    return startAccessPoint();
  }

  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);

  String host = (hostname.length() > 0) ? hostname : sysConfig.wifi.hostname;
  if (host.length() == 0) host = "whatels";
  WiFi.setHostname(host.c_str());

  ESP_LOGI(TAG, "Connecting to Station SSID '%s' (hostname '%s')...", ssid.c_str(), host.c_str());
  WiFi.begin(ssid.c_str(), password.c_str());

  _state = WifiModeState::STA_CONNECTING;
  _connectStartedAt = millis();
  _retryIntervalMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
  _disconnectStartedAt = 0;
  setStatus("Connecting");
  return true;
}

bool WifiService::startAccessPoint() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);

  const char* apSsid = sysConfig.wifi.setupApSsid.length() > 0 ? sysConfig.wifi.setupApSsid.c_str() : "whatELS-Setup";
  const char* apPass = sysConfig.wifi.setupApPassword.length() > 0 ? sysConfig.wifi.setupApPassword.c_str() : "whatels-setup";

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

void WifiService::update() {
  if (_state == WifiModeState::OFF) return;

  if (_state == WifiModeState::STA_CONNECTING) {
    if (WiFi.status() == WL_CONNECTED) {
      _state = WifiModeState::STA_CONNECTED;
      _ipAddress = WiFi.localIP().toString();
      _retryIntervalMs = WIFI_RETRY_INITIAL_INTERVAL_MS;
      _disconnectStartedAt = 0;

      String host = sysConfig.wifi.hostname.length() > 0 ? sysConfig.wifi.hostname : "whatels";
      if (!_mdnsStarted && MDNS.begin(host.c_str())) {
        _mdnsStarted = true;
        ESP_LOGI(TAG, "mDNS responder started: http://%s.local", host.c_str());
      }
      setStatus("See " + _ipAddress);
      ESP_LOGI(TAG, "Station connected successfully! IP: %s", _ipAddress.c_str());
    } else if (millis() - _connectStartedAt >= WIFI_CONNECT_TIMEOUT_MS) {
      ESP_LOGW(TAG, "Station connection timed out. Falling back to Setup AP...");
      startAccessPoint();
    }
    return;
  }

  if (_state == WifiModeState::STA_CONNECTED) {
    if (WiFi.status() != WL_CONNECTED) {
      if (_disconnectStartedAt == 0) {
        _disconnectStartedAt = millis();
      } else if (millis() - _disconnectStartedAt >= WIFI_DISCONNECT_DEBOUNCE_MS) {
        ESP_LOGW(TAG, "Station connection lost after debounce!");
        _state = WifiModeState::STA_CONNECTING;
        _disconnectStartedAt = 0;
        _nextRetryAt = millis() + _retryIntervalMs;
        setStatus("WiFi lost, retrying...");
      }
    } else {
      _disconnectStartedAt = 0;
    }
    return;
  }

  // Reconnection watchdog for temporary dropouts
  if (_state == WifiModeState::AP_ACTIVE && sysConfig.wifi.ssid.length() > 0) {
    // If running in AP mode because station timed out, periodically retry station if desired
    if (_nextRetryAt != 0 && (long)(millis() - _nextRetryAt) >= 0) {
      ESP_LOGD(TAG, "Periodic retry to station SSID '%s'...", sysConfig.wifi.ssid.c_str());
      WiFi.reconnect();
      _retryIntervalMs = std::min(_retryIntervalMs * 2, WIFI_RETRY_MAX_INTERVAL_MS);
      _nextRetryAt = millis() + _retryIntervalMs;
    }
  }
}

} // namespace core
} // namespace whatels
