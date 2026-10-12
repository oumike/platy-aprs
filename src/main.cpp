#include <Arduino.h>
#include "config/config.h"
#include "hal/board.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "hal/gps.h"
#include "hal/sd_card.h"
#include "services/alerts.h"
#include "services/beacon.h"
#include "services/messages.h"
#include "services/router.h"
#include "services/sd_log.h"
#include "transport/ble_kiss.h"
#include "net/aprs_is.h"
#include "net/web_server.h"
#include "net/wifi_manager.h"
#include "ui/ui.h"
#include "version.h"

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\n[boot] %s v%s\n", APP_NAME, APP_VERSION);

    boardBegin();
    configBegin();
    setenv("TZ", g_config.timezone.c_str(), 1);
    tzset();
    displayBegin();
    displaySetBrightness(g_config.brightness);
    keyboardBegin();
    sdBegin();
    routerBegin();
    messagesBegin();
    gpsBegin();
    gpsSetEnabled(g_config.gpsEnabled);
    uiBegin();

    wifiBegin();
    webBegin();
    aprsIsBegin();
    g_ble.begin();
    if (g_config.buzzer) boardBeep(2000, 60);
}

void loop() {
    keyboardPoll();
    wifiLoop();
    webLoop();
    routerLoop();
    aprsIsLoop();
    gpsSetEnabled(g_config.gpsEnabled);   // follows config changes
    gpsLoop();
    beaconLoop();
    messagesLoop();
    sdLogLoop();
    alertsLoop();
    displayLoop();
    uiLoop();
    delay(2);
}
