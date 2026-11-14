#pragma once

/**
 * @file WifiService.h
 * @brief Wi-Fi STA/AP connection and scan management service.
 * @see docs/state/ELS_PROPERTIES_AND_SETTINGS.md
 * @see docs/WHATELS_MASTER_ARCHITECTURE.md
 */

#include <Arduino.h>
#include <WiFi.h>
#include <vector>

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
  unsigned long _connectStartedAt = 0;
  unsigned long _nextRetryMs = 0;
  unsigned long _currentBackoffMs = 3000;
  bool          _mdnsStarted = false;

  WifiService() = default;

public:
  WifiService(const WifiService&) = delete;
  WifiService& operator=(const WifiService&) = delete;

  static WifiService& instance();

  // Core lifecycle & watchdog
  void init();
  bool begin();
  void loop();
  inline void update() { loop(); }
  void stop();

  // Mode triggers
  bool connectStation(const String& ssid, const String& password, const String& hostname = "");
  bool startAccessPoint();

  // Status & discovery accessors
  bool isConnected() const;
  inline bool isStationConnected() const { return isConnected(); }
  inline bool isApActive() const { return _state == WifiModeState::AP_ACTIVE; }
  String getIpAddress() const;
  int8_t getRssi() const;
  const char* getHostname() const { return "whatels"; }

  WifiModeState getState() const { return _state; }
  String getModeString() const;
  String getStatusString() const { return _statusText; }

  std::vector<WifiScanResult> scanNetworks();

private:
  void setStatus(const String& msg);
};

} // namespace core
} // namespace whatels
