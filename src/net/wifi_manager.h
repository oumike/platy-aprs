#pragma once
#include <Arduino.h>
#include <vector>

enum class ApReason : uint8_t { None, Setup, Fallback, Manual };

struct WifiStatus {
    bool apActive;
    ApReason apReason;
    String apSsid;
    String apIp;
    uint8_t apClients;
    bool staConfigured;
    bool staConnected;
    String staSsid;
    String staIp;
    int8_t rssi;
    String hostname;
};

void wifiBegin();
void wifiLoop();
// Re-read Wi-Fi credentials from g_config and (re)connect. The AP stays up
// until the new connection succeeds so a browser on the AP sees the result.
void wifiApplyConfig();
// User request from the device UI: bring the setup AP up. Refused (false) while
// connected to a network; the AP also shuts down once a connection is made.
bool wifiOpenSetupAp();
WifiStatus wifiStatus();
bool wifiStaConnected();
const char *apReasonStr(ApReason r);

struct WifiNetwork {
    String ssid;
    int8_t rssi;
    bool secure;
};

// Starts an asynchronous scan (shared by the device UI and the web UI). A
// pending connection attempt is cancelled so the radio is free to scan.
bool wifiScanStart();
bool wifiScanRunning();
// Results of the last completed scan: named networks only, one entry per SSID
// (strongest), strongest first. False when no completed scan is available.
bool wifiScanResults(std::vector<WifiNetwork> &out);
// Saves new credentials and connects to them. An empty password with the
// currently saved SSID keeps the saved password. Returns false (with a
// reason) when the credentials are invalid or can't be saved.
bool wifiConnectTo(const String &ssid, const String &password, String &err);
