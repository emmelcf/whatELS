#pragma once

#include <Arduino.h>
#include <WiFi.h>

namespace whatels {
namespace core {

enum class WifiModeState : uint8_t {
  OFF = 0,
  STA_CONNECTING,
  STA_CONNECTED,
  AP_ACTIVE,
  FAILED
};

struct WifiScanResult {
  String ssid;
  int32_t rssi;
  bool open;
};

class WifiService {
private:
  WifiModeState _state = WifiModeState::OFF;
  String        _ipAddress = "";
  String        _statusText = "No WiFi";
  unsigned long _disconnectStartedAt = 0;
  unsigned long _nextRetryAt = 0;
  unsigned long _retryIntervalMs = 3000;
  unsigned long _connectStartedAt = 0;
  bool          _mdnsStarted = false;

  WifiService() = default;

public:
  static WifiService& instance();

  void init();
  bool begin();
  void update();

  // Mode & configuration triggers
  bool connectStation(const String& ssid, const String& password, const String& hostname = "");
  bool startAccessPoint();
  void stop();

  // Network discovery
  std::vector<WifiScanResult> scanNetworks();

  // Status accessors
  WifiModeState getState() const { return _state; }
  String getModeString() const;
  String getStatusString() const { return _statusText; }
  String getIpAddress() const { return _ipAddress; }
  int32_t getRssi() const { return (_state == WifiModeState::STA_CONNECTED) ? WiFi.RSSI() : 0; }
  bool isStationConnected() const { return _state == WifiModeState::STA_CONNECTED; }
  bool isApActive() const { return _state == WifiModeState::AP_ACTIVE; }

private:
  void setStatus(const String& msg);
  void handleStationConnecting();
  void handleStationConnected();
  void handleStationDisconnected();
};

} // namespace core
} // namespace whatels
