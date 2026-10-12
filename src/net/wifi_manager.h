#pragma once
#include <Arduino.h>

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
// User request from the device UI: bring the setup AP up for a while.
void wifiOpenSetupAp();
WifiStatus wifiStatus();
bool wifiStaConnected();
const char *apReasonStr(ApReason r);
