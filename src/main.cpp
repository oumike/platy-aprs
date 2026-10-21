#include <Arduino.h>
#include "config/config.h"
#include "hal/audio.h"
#include "hal/board.h"
#include "hal/display.h"
#include "hal/screen.h"
#include "hal/keyboard.h"
#include "hal/gps.h"
#include "hal/sd_card.h"
#include "services/alerts.h"
#include "services/beacon.h"
#include "services/messages.h"
#include "services/ota.h"
#include "services/router.h"
#include "services/sd_log.h"
#include "transport/ble_kiss.h"
#include "transport/lora_aprs.h"
#include "net/aprs_is.h"
#include "net/web_server.h"
#include "net/wifi_manager.h"
#include "ui/ui.h"
#include "version.h"

void setup() {
    Serial.begin(115200);
    delay(50);
    Serial.printf("\n[boot] %s %s\n", APP_NAME, APP_VERSION);

    boardBegin();
    audioBegin();
    configBegin();
    setenv("TZ", g_config.timezone.c_str(), 1);
    tzset();
    displayBegin();
    displaySetBrightness(g_config.brightness);
    screenBegin();
    uiShowSplash();
    keyboardBegin();
    sdBegin();
    routerBegin();
    messagesBegin();
    gpsBegin();
    gpsSetEnabled(g_config.gpsEnabled);
    uiBegin();

    wifiBegin();
    webBegin();
    otaBegin();
    aprsIsBegin();
    g_ble.begin();
    g_lora.begin();
    alertPlay(Alert::Boot);
}

void loop() {
    keyboardPoll();
    wifiLoop();
    webLoop();
    otaLoop();
    g_lora.loop();
    routerLoop();
    aprsIsLoop();
    gpsSetEnabled(g_config.gpsEnabled);   // follows config changes
    gpsLoop();
    beaconLoop();
    messagesLoop();
    sdLoop();
    sdLogLoop();
    alertsLoop();
    screenLoop();
    displayLoop();
    uiLoop();
    delay(2);
}
