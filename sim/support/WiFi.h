#pragma once
// Lume Nano simulator: no radio. Scans return two fake networks so the Wi-Fi screens can be
// exercised; connecting always fails ("indisponível no simulador").
#include <Arduino.h>
typedef enum { WIFI_OFF = 0, WIFI_STA = 1, WIFI_AP = 2, WIFI_AP_STA = 3 } wifi_mode_t;
typedef enum { WIFI_AUTH_OPEN = 0, WIFI_AUTH_WEP, WIFI_AUTH_WPA_PSK, WIFI_AUTH_WPA2_PSK, WIFI_AUTH_WPA_WPA2_PSK } wifi_auth_mode_t;
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_SCAN_COMPLETED = 2, WL_CONNECTED = 3,
               WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6 } wl_status_t;
constexpr int16_t WIFI_SCAN_RUNNING = -1;
constexpr int16_t WIFI_SCAN_FAILED = -2;
class IPAddress {
public:
    String toString() const { return String("0.0.0.0"); }
};
class SimWiFi {
public:
    bool mode(wifi_mode_t) { return true; }
    wifi_mode_t getMode() { return WIFI_OFF; }
    int16_t scanNetworks(bool = false, bool = false) { scanned_ = true; return WIFI_SCAN_RUNNING; }
    int16_t scanComplete() { return scanned_ ? 2 : WIFI_SCAN_FAILED; }
    void scanDelete() { scanned_ = false; }
    String SSID(uint8_t i) { return String(i == 0 ? "Rede simulada" : "Rede simulada 2"); }
    String SSID() { return String(); }
    int32_t RSSI(uint8_t i) { return i == 0 ? -48 : -71; }
    int32_t RSSI() { return 0; }
    wifi_auth_mode_t encryptionType(uint8_t i) { return i == 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN; }
    wl_status_t status() { return WL_DISCONNECTED; }
    bool isConnected() { return false; }
    wl_status_t begin(const char*, const char* = nullptr) { return WL_CONNECT_FAILED; }
    bool disconnect(bool = false, bool = false) { return true; }
    IPAddress localIP() { return {}; }
    IPAddress softAPIP() { return {}; }
    String macAddress() { return String("02:00:00:00:5E:01"); }
    bool setHostname(const char*) { return true; }
    bool setSleep(bool) { return true; }
private:
    bool scanned_ = false;
};
inline SimWiFi WiFi;
