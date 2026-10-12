#include <Arduino.h>
#include "config/config.h"
#include "hal/board.h"
#include "hal/display.h"
#include "hal/keyboard.h"
#include "hal/sd_card.h"
#include "services/router.h"
#include "transport/ble_kiss.h"
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
    uiBegin();

    wifiBegin();
    webBegin();
    g_ble.begin();
    if (g_config.buzzer) boardBeep(2000, 60);
}

void loop() {
    keyboardPoll();
    wifiLoop();
    webLoop();
    routerLoop();
    displayLoop();
    uiLoop();
    delay(2);
}
