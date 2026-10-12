#pragma once
#include <Arduino.h>
#include <string>

// APRS-IS client. The TCP connection lives in its own FreeRTOS task (connects
// block); lines are exchanged with the loop task through queues.

struct AprsIsStatus {
    const char *state;      // "off", "waiting for Wi-Fi", "connecting", "connected"
    bool connected;
    bool verified;          // server accepted our passcode (needed to send)
    String server;          // server name from the login banner
    String lastError;
    uint32_t rxLines;
    uint32_t txLines;
    uint32_t gatedRfToIs;
    uint32_t gatedIsToRf;
    uint32_t connectedForS;
};

void aprsIsBegin();
// Call from the loop task: tracks config/Wi-Fi changes, hands received lines
// to the router.
void aprsIsLoop();
// True when connected with a verified login.
bool aprsIsReady();
// Queue a line ("SRC>DST,PATH:info", no CR/LF) for the server.
bool aprsIsSend(const std::string &line);
AprsIsStatus aprsIsStatus();
void aprsIsCountGated(bool rfToIs);
