#include "ui.h"
#include "config/config.h"
#include "hal/board.h"
#include "hal/keyboard.h"
#include "hal/pins_m9.h"
#include "net/wifi_manager.h"
#include "version.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

namespace {

constexpr int kStatusH = 22;
constexpr lv_color_t kGrey = LV_COLOR_MAKE(0x60, 0x66, 0x70);
constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x4c, 0xd0, 0x7d);
constexpr lv_color_t kAmber = LV_COLOR_MAKE(0xff, 0xb3, 0x47);

struct Screen {
    lv_obj_t *scr;
    lv_group_t *grp;
    void (*refresh)();
};

constexpr int kMaxDepth = 6;
Screen s_stack[kMaxDepth];
int s_depth = 0;

lv_obj_t *s_sbCall, *s_sbWifi, *s_sbBt, *s_sbGps, *s_sbBatt, *s_sbTime;

// ── Screen stack ─────────────────────────────────────────────────────────────

lv_obj_t *newScreen(const char *title, lv_group_t **grpOut) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_set_style_pad_top(scr, kStatusH + 2, 0);
    lv_obj_set_style_pad_hor(scr, 4, 0);
    lv_obj_set_style_pad_bottom(scr, 4, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(scr, 4, 0);
    if (title) {
        lv_obj_t *t = lv_label_create(scr);
        lv_label_set_text(t, title);
        lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
    }
    *grpOut = lv_group_create();
    return scr;
}

void pushScreen(lv_obj_t *scr, lv_group_t *grp, void (*refresh)() = nullptr) {
    if (s_depth == kMaxDepth) return;
    s_stack[s_depth++] = {scr, grp, refresh};
    lv_group_set_default(grp);
    lv_indev_set_group(keyboardIndev(), grp);
    lv_screen_load(scr);
    if (refresh) refresh();
}

void popScreen() {
    if (s_depth <= 1) return;
    Screen top = s_stack[--s_depth];
    Screen &prev = s_stack[s_depth - 1];
    lv_group_set_default(prev.grp);
    lv_indev_set_group(keyboardIndev(), prev.grp);
    lv_screen_load(prev.scr);
    lv_group_delete(top.grp);
    lv_obj_delete(top.scr);
    if (prev.refresh) prev.refresh();
}

void popToHome() {
    while (s_depth > 1) popScreen();
}

lv_obj_t *makeList(lv_obj_t *parent) {
    lv_obj_t *list = lv_list_create(parent);
    lv_obj_set_width(list, lv_pct(100));
    lv_obj_set_flex_grow(list, 1);
    return list;
}

lv_obj_t *addItem(lv_obj_t *list, lv_group_t *grp, const char *icon, const char *text,
                  lv_event_cb_t cb) {
    lv_obj_t *b = lv_list_add_button(list, icon, text);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, b);
    return b;
}

// ── Status bar ───────────────────────────────────────────────────────────────

lv_obj_t *sbLabel(lv_obj_t *bar, const char *txt) {
    lv_obj_t *l = lv_label_create(bar);
    lv_label_set_text(l, txt);
    return l;
}

void buildStatusBar() {
    lv_obj_t *bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bar, lv_pct(100), kStatusH);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x1b2027), 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_text_font(bar, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

    s_sbCall = sbLabel(bar, "");
    lv_obj_set_flex_grow(s_sbCall, 1);
    s_sbBt = sbLabel(bar, LV_SYMBOL_BLUETOOTH);
    s_sbGps = sbLabel(bar, LV_SYMBOL_GPS);
    s_sbWifi = sbLabel(bar, LV_SYMBOL_WIFI);
    s_sbBatt = sbLabel(bar, "");
    s_sbTime = sbLabel(bar, "--:--");
    lv_obj_set_style_text_color(s_sbBt, kGrey, 0);
    lv_obj_set_style_text_color(s_sbGps, kGrey, 0);
}

void updateStatusBar() {
    lv_label_set_text(s_sbCall, g_config.fullCallsign().c_str());

    const WifiStatus w = wifiStatus();
    lv_color_t wc = kGrey;
    if (w.staConnected) wc = kGreen;
    else if (w.apActive) wc = kAmber;
    lv_obj_set_style_text_color(s_sbWifi, wc, 0);
    lv_label_set_text(s_sbWifi, w.apActive ? LV_SYMBOL_WIFI " AP" : LV_SYMBOL_WIFI);

    const int pct = boardBatteryPercent();
    const char *sym = pct < 0    ? LV_SYMBOL_BATTERY_EMPTY
                      : pct > 80 ? LV_SYMBOL_BATTERY_FULL
                      : pct > 55 ? LV_SYMBOL_BATTERY_3
                      : pct > 30 ? LV_SYMBOL_BATTERY_2
                      : pct > 10 ? LV_SYMBOL_BATTERY_1
                                 : LV_SYMBOL_BATTERY_EMPTY;
    if (pct < 0) lv_label_set_text(s_sbBatt, sym);
    else lv_label_set_text_fmt(s_sbBatt, "%s %d%%", sym, pct);

    const time_t now = time(nullptr);
    if (now > 1700000000) {
        struct tm tm;
        localtime_r(&now, &tm);
        lv_label_set_text_fmt(s_sbTime, "%02d:%02d", tm.tm_hour, tm.tm_min);
    }
}

// ── Placeholder screens ──────────────────────────────────────────────────────

void openPlaceholder(const char *title, const char *note) {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(title, &grp);
    lv_obj_t *l = lv_label_create(scr);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_text(l, note);
    lv_obj_set_style_text_color(l, kGrey, 0);
    pushScreen(scr, grp);
}

// ── Wi-Fi status screen ──────────────────────────────────────────────────────

lv_obj_t *s_wifiInfo = nullptr;

void refreshWifiInfo() {
    if (!s_wifiInfo) return;
    const WifiStatus w = wifiStatus();
    String t;
    t += "Network: " + (w.staConfigured ? w.staSsid : String("(not set)")) + "\n";
    t += w.staConnected ? "Connected  IP " + w.staIp + "  " + String(w.rssi) + " dBm\n"
                        : String("Not connected\n");
    t += "Web: http://" + w.hostname + ".local\n\n";
    if (w.apActive) {
        t += "Setup AP: " + w.apSsid + " (" + apReasonStr(w.apReason) + ")\n";
        t += "Open http://" + w.apIp + "  clients: " + String(w.apClients);
    } else {
        t += "Setup AP: off";
    }
    lv_label_set_text(s_wifiInfo, t.c_str());
}

void openWifiStatus() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_WIFI " Wi-Fi", &grp);
    s_wifiInfo = lv_label_create(scr);
    lv_label_set_long_mode(s_wifiInfo, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifiInfo, lv_pct(100));
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_wifiInfo = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshWifiInfo);
}

// ── Settings ─────────────────────────────────────────────────────────────────

void openAbout() {
    char buf[200];
    snprintf(buf, sizeof(buf),
             APP_NAME " v" APP_VERSION "\nElecrow ThinkNode M9\n\nHeap free: %u\nPSRAM free: %u\nBattery: %u mV",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(),
             (unsigned)boardBatteryMilliVolts());
    openPlaceholder("About", buf);
}

void openSettings() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_SETTINGS " Settings", &grp);
    lv_obj_t *list = makeList(scr);
    addItem(list, grp, LV_SYMBOL_WIFI, "Open setup access point", [](lv_event_t *) {
        wifiOpenSetupAp();
        openWifiStatus();
    });
    addItem(list, grp, LV_SYMBOL_WIFI, "Wi-Fi status", [](lv_event_t *) { openWifiStatus(); });
    addItem(list, grp, LV_SYMBOL_BLUETOOTH, "TNC (Bluetooth)", [](lv_event_t *) {
        openPlaceholder("TNC", "BLE KISS TNC pairing arrives in milestone 2.");
    });
    addItem(list, grp, LV_SYMBOL_LIST, "About", [](lv_event_t *) { openAbout(); });
    addItem(list, grp, LV_SYMBOL_REFRESH, "Reboot", [](lv_event_t *) { ESP.restart(); });
    pushScreen(scr, grp);
}

// ── Home ─────────────────────────────────────────────────────────────────────

lv_obj_t *s_homeCard, *s_homeQr, *s_homeText;
String s_homeKey;

void refreshHome() {
    const WifiStatus w = wifiStatus();
    const bool showQr = w.apActive;
    const String key = String(showQr) + w.apSsid + w.staIp + String(w.staConnected) + g_config.fullCallsign();
    if (key == s_homeKey) return;
    s_homeKey = key;

    if (showQr) {
        const String qr = "WIFI:T:nopass;S:" + w.apSsid + ";;";
        lv_qrcode_update(s_homeQr, qr.c_str(), qr.length());
        lv_obj_remove_flag(s_homeQr, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text_fmt(s_homeText, "Setup Wi-Fi:\n%s\n\nThen open\nhttp://%s", w.apSsid.c_str(),
                              w.apIp.c_str());
    } else {
        lv_obj_add_flag(s_homeQr, LV_OBJ_FLAG_HIDDEN);
        if (w.staConnected)
            lv_label_set_text_fmt(s_homeText, "%s\nWi-Fi: %s\nConfig: http://%s", g_config.fullCallsign().c_str(),
                                  w.staSsid.c_str(), w.staIp.c_str());
        else
            lv_label_set_text_fmt(s_homeText, "%s\nWi-Fi: connecting to %s...", g_config.fullCallsign().c_str(),
                                  w.staSsid.c_str());
    }
}

void buildHome() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(nullptr, &grp);

    s_homeCard = lv_obj_create(scr);
    lv_obj_set_size(s_homeCard, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_homeCard, 6, 0);
    lv_obj_set_flex_flow(s_homeCard, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_homeCard, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_homeCard, 8, 0);
    lv_obj_clear_flag(s_homeCard, LV_OBJ_FLAG_SCROLLABLE);

    s_homeQr = lv_qrcode_create(s_homeCard);
    lv_qrcode_set_size(s_homeQr, 88);
    lv_qrcode_set_dark_color(s_homeQr, lv_color_black());
    lv_qrcode_set_light_color(s_homeQr, lv_color_white());
    lv_obj_set_style_border_color(s_homeQr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_homeQr, 3, 0);

    s_homeText = lv_label_create(s_homeCard);
    lv_obj_set_flex_grow(s_homeText, 1);
    lv_label_set_long_mode(s_homeText, LV_LABEL_LONG_WRAP);

    lv_obj_t *list = makeList(scr);
    addItem(list, grp, LV_SYMBOL_LIST, "Stations", [](lv_event_t *) {
        openPlaceholder("Stations", "Heard stations arrive in milestone 2.");
    });
    addItem(list, grp, LV_SYMBOL_ENVELOPE, "Messages", [](lv_event_t *) {
        openPlaceholder("Messages", "APRS messaging arrives in milestone 3.");
    });
    addItem(list, grp, LV_SYMBOL_EYE_OPEN, "Monitor", [](lv_event_t *) {
        openPlaceholder("Monitor", "Raw packet monitor arrives in milestone 2.");
    });
    addItem(list, grp, LV_SYMBOL_GPS, "Map", [](lv_event_t *) {
        openPlaceholder("Map", "Map arrives in milestone 4.");
    });
    addItem(list, grp, LV_SYMBOL_SETTINGS, "Settings", [](lv_event_t *) { openSettings(); });

    pushScreen(scr, grp, refreshHome);
}

enum class Nav : uint8_t { None, Back, Home };
volatile Nav s_pendingNav = Nav::None;

// Runs inside LVGL's indev read; navigation is deferred to uiLoop() so no
// screen is deleted while LVGL is still processing input for it.
bool hotkey(uint8_t key) {
    switch (key) {
        case M9_KEY_BACK: s_pendingNav = Nav::Back; return true;
        case M9_KEY_HOME: s_pendingNav = Nav::Home; return true;
        default: return false;
    }
}

void tick(lv_timer_t *) {
    updateStatusBar();
    if (s_depth > 0 && s_stack[s_depth - 1].refresh) s_stack[s_depth - 1].refresh();
}

}  // namespace

void uiBegin() {
    keyboardSetHotkeyHandler(hotkey);
    buildStatusBar();
    buildHome();
    updateStatusBar();
    lv_timer_create(tick, 1000, nullptr);
}

void uiLoop() {
    const Nav nav = s_pendingNav;
    s_pendingNav = Nav::None;
    if (nav == Nav::Back) popScreen();
    else if (nav == Nav::Home) popToHome();
}
