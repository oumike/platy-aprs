#include "ui.h"
#include "config/config.h"
#include "hal/board.h"
#include "hal/keyboard.h"
#include "hal/pins_m9.h"
#include "hal/gps.h"
#include "hal/sd_card.h"
#include "net/aprs_is.h"
#include "net/wifi_manager.h"
#include "services/backup.h"
#include "services/beacon.h"
#include "services/messages.h"
#include "services/data_store.h"
#include "services/router.h"
#include "transport/ble_kiss.h"
#include "transport/lora_aprs.h"
#include "util/geo.h"
#include "ui/map_view.h"
#include "version.h"
#include <Arduino.h>
#include <lvgl.h>
#include <time.h>

namespace {

constexpr int kStatusH = 22;
constexpr lv_color_t kGrey = LV_COLOR_MAKE(0x60, 0x66, 0x70);
constexpr lv_color_t kGreen = LV_COLOR_MAKE(0x4c, 0xd0, 0x7d);
constexpr lv_color_t kAmber = LV_COLOR_MAKE(0xff, 0xb3, 0x47);
constexpr lv_color_t kRed = LV_COLOR_MAKE(0xff, 0x5c, 0x5c);

struct Screen {
    lv_obj_t *scr;
    lv_group_t *grp;
    void (*refresh)();
};

constexpr int kMaxDepth = 6;
Screen s_stack[kMaxDepth];
int s_depth = 0;

volatile bool s_pendingCloseInfo = false;
lv_obj_t *s_sbCall, *s_sbMsg, *s_sbWifi, *s_sbBt, *s_sbGps, *s_sbBatt, *s_sbTime;

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
    s_sbMsg = sbLabel(bar, "");
    lv_obj_set_style_text_color(s_sbMsg, kAmber, 0);
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

    if (g_config.rfLink == RfLink::Lora) {
        const LoraState ls = g_lora.status().state;
        lv_label_set_text(s_sbBt, ls == LoraState::Tx ? "LoRa " LV_SYMBOL_UPLOAD : "LoRa");
        lv_obj_set_style_text_color(s_sbBt, ls == LoraState::Failed ? kRed : ls == LoraState::Off ? kGrey : kGreen, 0);
    } else {
        const BleStatus b = g_ble.status();
        lv_label_set_text(s_sbBt, LV_SYMBOL_BLUETOOTH);
        lv_obj_set_style_text_color(s_sbBt,
                                    b.state == BleState::Connected                                     ? kGreen
                                    : b.state == BleState::Connecting || b.state == BleState::Waiting ? kAmber
                                                                                                       : kGrey,
                                    0);
    }

    const GpsFix g = gpsFix();
    lv_obj_set_style_text_color(s_sbGps, g.valid ? kGreen : g.alive ? kAmber : kGrey, 0);

    const uint32_t unread = messagesUnread();
    if (unread) lv_label_set_text_fmt(s_sbMsg, LV_SYMBOL_ENVELOPE " %lu", (unsigned long)unread);
    else lv_label_set_text(s_sbMsg, "");

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

// ── Toast ────────────────────────────────────────────────────────────────────

lv_obj_t *s_toast;
uint32_t s_toastUntilMs;

void toast(const String &text, uint32_t ms = 3000) {
    if (!s_toast) {
        s_toast = lv_label_create(lv_layer_top());
        lv_obj_set_width(s_toast, lv_pct(94));
        lv_label_set_long_mode(s_toast, LV_LABEL_LONG_DOT);
        lv_obj_set_style_max_height(s_toast, 60, 0);
        lv_obj_set_style_bg_color(s_toast, lv_color_hex(0x2a313b), 0);
        lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
        lv_obj_set_style_border_color(s_toast, kAmber, 0);
        lv_obj_set_style_border_width(s_toast, 1, 0);
        lv_obj_set_style_radius(s_toast, 6, 0);
        lv_obj_set_style_pad_all(s_toast, 6, 0);
        lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -6);
    }
    lv_label_set_text(s_toast, text.c_str());
    lv_obj_move_foreground(s_toast);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toastUntilMs = millis() + ms;
}

void toastLoop() {
    if (s_toast && s_toastUntilMs && (int32_t)(millis() - s_toastUntilMs) >= 0) {
        lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
        s_toastUntilMs = 0;
    }
}

void beaconNowUi() {
    String err;
    if (beaconNow(err)) toast(LV_SYMBOL_UPLOAD " Beacon sent");
    else toast("Beacon not sent: " + err);
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


// ── List helpers ─────────────────────────────────────────────────────────────

int focusedIndex(lv_obj_t *list) {
    lv_obj_t *f = lv_group_get_focused(lv_group_get_default());
    if (!f || lv_obj_get_parent(f) != list) return -1;
    return lv_obj_get_index(f);
}

lv_obj_t *addWrapItem(lv_obj_t *list, lv_group_t *grp, const char *text, lv_event_cb_t cb, void *ud,
                      const lv_font_t *font = &lv_font_montserrat_12) {
    lv_obj_t *b = lv_list_add_button(list, nullptr, text);
    lv_obj_t *l = lv_obj_get_child(b, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_style_text_font(b, font, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_group_add_obj(grp, b);
    return b;
}

lv_obj_t *makeNote(lv_obj_t *parent, const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, lv_pct(100));
    lv_obj_set_style_text_color(l, kGrey, 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_label_set_text(l, text);
    return l;
}

String ageStr(uint32_t heardS) { return geo::formatAge(uptimeS() - heardS).c_str(); }

// ── Monitor ──────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *list, *note;
    lv_group_t *grp;
    uint32_t shownSeq;
} s_mon;

void refreshMonitor() {
    if (!s_mon.list) return;
    const uint32_t last = packetLogLastSeq();
    if (last == s_mon.shownSeq) return;
    // While the user is reading further down, don't move the list under them.
    if (focusedIndex(s_mon.list) > 0) {
        lv_label_set_text_fmt(s_mon.note, "%lu new - scroll to top to update", (unsigned long)(last - s_mon.shownSeq));
        lv_obj_remove_flag(s_mon.note, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_add_flag(s_mon.note, LV_OBJ_FLAG_HIDDEN);
    s_mon.shownSeq = last;
    lv_obj_clean(s_mon.list);
    for (size_t i = 0; i < packetLogCount() && i < 40; i++) {
        const LogEntry *e = packetLogAt(i);
        char buf[300];
        snprintf(buf, sizeof(buf), "%s%s%s  %s", e->tx ? "TX " : "", e->viaIs ? "IS " : "", ageStr(e->uptimeS).c_str(), e->raw);
        lv_obj_t *b = addWrapItem(s_mon.list, s_mon.grp, buf, nullptr, nullptr);
        if (e->tx) lv_obj_set_style_text_color(b, lv_color_hex(0x3fa7ff), 0);
        else if (e->viaIs) lv_obj_set_style_text_color(b, lv_color_hex(0x9aa4b2), 0);
    }
    if (lv_obj_get_child_count(s_mon.list)) lv_group_focus_obj(lv_obj_get_child(s_mon.list, 0));
}

void openMonitor() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_EYE_OPEN " Monitor", &grp);
    s_mon = {};
    s_mon.grp = grp;
    s_mon.note = makeNote(scr, "");
    lv_obj_add_flag(s_mon.note, LV_OBJ_FLAG_HIDDEN);
    s_mon.list = makeList(scr);
    if (!packetLogCount()) makeNote(scr, "No packets yet. Set up the radio link under Settings.");
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_mon.list = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshMonitor);
}

// ── Stations ─────────────────────────────────────────────────────────────────

lv_obj_t *s_stDetail;
char s_stDetailCall[10];

void refreshStationDetail() {
    if (!s_stDetail) return;
    const Station *s = stationDbFind(s_stDetailCall);
    if (!s) {
        lv_label_set_text(s_stDetail, "Station no longer in the list.");
        return;
    }
    const bool metric = g_config.units == Units::Metric;
    String t;
    if (s->isObject) t += String("Object from ") + s->owner + "\n";
    t += "Heard " + ageStr(s->lastHeardS) + " ago, " + String(s->packets) + " packets\n";
    t += String("Last: ") + aprs::typeName(s->lastType) + "\n";
    if (s->hasPos) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Pos: %.5f, %.5f  sym %c%c\n", s->lat, s->lon, s->symTable, s->symCode);
        t += buf;
        double myLat, myLon;
        if (myPosition(myLat, myLon)) {
            const double brg = geo::bearingDeg(myLat, myLon, s->lat, s->lon);
            t += "Dist: " + String(geo::formatDistance(geo::distanceKm(myLat, myLon, s->lat, s->lon), metric).c_str()) +
                 " " + geo::compassPoint(brg) + " (" + String((int)brg) + "\xC2\xB0)\n";
        }
    }
    if (s->hasCourseSpeed)
        t += "Moving: " + String(geo::formatSpeed(s->speedKnots, metric).c_str()) + " @ " + String(s->course) + "\xC2\xB0\n";
    if (s->hasAlt) t += "Alt: " + String(geo::formatAltitude(s->altFeet, metric).c_str()) + "\n";
    if (s->hasWeather) {
        t += "WX:";
        if (s->hasTempF) t += metric ? " " + String((s->tempF - 32) * 5 / 9) + "C" : " " + String(s->tempF) + "F";
        if (s->hasWind) t += " wind " + String(s->windDir) + "\xC2\xB0/" + String(s->windMph) + "mph";
        if (s->hasHumidity) t += " " + String(s->humidity) + "%";
        if (s->hasPressure) t += " " + String(s->pressureMbar, 1) + "mb";
        t += "\n";
    }
    if (s->status[0]) t += String("Status: ") + s->status + "\n";
    if (s->comment[0]) t += String("Comment: ") + s->comment + "\n";
    if (s->path[0]) t += String("Path: ") + s->path + "\n";
    t += String("\n") + s->lastRaw;
    lv_label_set_text(s_stDetail, t.c_str());
}

void openStationDetail(const char *call) {
    strlcpy(s_stDetailCall, call, sizeof(s_stDetailCall));
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(call, &grp);
    // Focusable container so the d-pad can scroll long details.
    lv_obj_t *box = lv_obj_create(scr);
    lv_obj_set_width(box, lv_pct(100));
    lv_obj_set_flex_grow(box, 1);
    lv_obj_set_style_pad_all(box, 4, 0);
    lv_group_add_obj(grp, box);
    lv_obj_add_event_cb(box, [](lv_event_t *e) {
        lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
        const uint32_t k = lv_event_get_key(e);
        if (k == LV_KEY_DOWN || k == LV_KEY_NEXT) lv_obj_scroll_by_bounded(b, 0, -40, LV_ANIM_ON);
        if (k == LV_KEY_UP || k == LV_KEY_PREV) lv_obj_scroll_by_bounded(b, 0, 40, LV_ANIM_ON);
    }, LV_EVENT_KEY, nullptr);
    s_stDetail = lv_label_create(box);
    lv_obj_set_width(s_stDetail, lv_pct(100));
    lv_label_set_long_mode(s_stDetail, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_stDetail, &lv_font_montserrat_12, 0);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_stDetail = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshStationDetail);
}

void openMap() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(nullptr, &grp);
    lv_obj_set_style_pad_hor(scr, 0, 0);
    lv_obj_set_style_pad_bottom(scr, 0, 0);
    lv_obj_set_style_pad_top(scr, kStatusH, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    mapViewCreate(scr, grp, openStationDetail);
    pushScreen(scr, grp);
}

struct {
    lv_obj_t *list, *empty;
    lv_group_t *grp;
    uint32_t gen;
    uint32_t lastBuildS;
    char calls[60][10];
} s_sta;

void refreshStations() {
    if (!s_sta.list) return;
    // Rebuild on new data, and every 30 s so the ages stay current.
    if (s_sta.gen == g_dataGen && uptimeS() - s_sta.lastBuildS < 30) return;
    s_sta.gen = g_dataGen;
    s_sta.lastBuildS = uptimeS();

    char keep[10] = "";
    const int fi = focusedIndex(s_sta.list);
    if (fi >= 0) strlcpy(keep, s_sta.calls[fi], sizeof(keep));

    lv_obj_clean(s_sta.list);
    double myLat, myLon;
    const bool havePos = myPosition(myLat, myLon);
    const bool metric = g_config.units == Units::Metric;
    lv_obj_t *refocus = nullptr;
    const size_t n = std::min<size_t>(stationDbCount(), 60);
    for (size_t i = 0; i < n; i++) {
        const Station *s = stationDbAt(i);
        strlcpy(s_sta.calls[i], s->call, sizeof(s_sta.calls[i]));
        String line = String(s->call) + "  " + ageStr(s->lastHeardS);
        if (s->hasPos && havePos) {
            const double km = geo::distanceKm(myLat, myLon, s->lat, s->lon);
            line += "  " + String(geo::formatDistance(km, metric).c_str()) + " " +
                    geo::compassPoint(geo::bearingDeg(myLat, myLon, s->lat, s->lon));
        }
        const char *extra = s->status[0] ? s->status : s->comment;
        if (extra[0]) line += String("\n") + extra;
        lv_obj_t *b = addWrapItem(s_sta.list, s_sta.grp, line.c_str(), [](lv_event_t *e) {
            const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
            openStationDetail(s_sta.calls[idx]);
        }, (void *)(uintptr_t)i);
        if (keep[0] && !strcmp(keep, s->call)) refocus = b;
    }
    if (refocus) lv_group_focus_obj(refocus);
    if (n) lv_obj_add_flag(s_sta.empty, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(s_sta.empty, LV_OBJ_FLAG_HIDDEN);
}

void openStations() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_LIST " Stations", &grp);
    s_sta.grp = grp;
    s_sta.gen = 0;
    s_sta.empty = makeNote(scr, "No stations heard yet.");
    s_sta.list = makeList(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_sta.list = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshStations);
}

// ── Messages ─────────────────────────────────────────────────────────────────

String s_pendingThread;   // opened from uiLoop (after leaving the compose screen)

lv_obj_t *makeInput(lv_obj_t *parent, lv_group_t *grp, const char *placeholder, uint32_t maxLen) {
    lv_obj_t *ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, placeholder);
    lv_textarea_set_max_length(ta, maxLen);
    lv_obj_set_width(ta, lv_pct(100));
    lv_group_add_obj(grp, ta);
    return ta;
}

const char *kCallChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-";

struct {
    lv_obj_t *list, *input;
    lv_group_t *grp;
    char peer[10];
    uint32_t gen;
    uint32_t seqs[150];
} s_thr;

void sendFromThread() {
    const char *txt = lv_textarea_get_text(s_thr.input);
    if (!txt[0]) return;
    String err;
    if (messagesSend(s_thr.peer, txt, err)) lv_textarea_set_text(s_thr.input, "");
    else toast("Not sent: " + err);
}

void refreshThread() {
    if (!s_thr.list) return;
    if (s_thr.gen == g_msgGen) return;
    messagesMarkRead(s_thr.peer);
    s_thr.gen = g_msgGen;
    const bool inputFocused = lv_group_get_focused(s_thr.grp) == s_thr.input;
    lv_obj_clean(s_thr.list);
    size_t n = 0;
    for (size_t i = 0; i < messagesCount(); i++) {
        const ChatMsg *m = messagesAt(i);
        if (strcasecmp(m->peer, s_thr.peer)) continue;
        String line = m->text;
        if (m->outgoing) {
            const char *st = m->state == MsgState::Acked      ? LV_SYMBOL_OK
                             : m->state == MsgState::Pending  ? "..."
                             : m->state == MsgState::Rejected ? LV_SYMBOL_CLOSE " rejected"
                                                              : LV_SYMBOL_CLOSE " not delivered, press to retry";
            line = String("You: ") + line + "  " + st;
        }
        s_thr.seqs[n] = m->seq;
        lv_obj_t *b = addWrapItem(s_thr.list, s_thr.grp, line.c_str(), [](lv_event_t *e) {
            const uint32_t seq = s_thr.seqs[(size_t)(uintptr_t)lv_event_get_user_data(e)];
            String err;
            for (size_t i = 0; i < messagesCount(); i++)
                if (messagesAt(i)->seq == seq && messagesAt(i)->state == MsgState::Failed) messagesRetry(seq, err);
        }, (void *)(uintptr_t)n);
        if (m->outgoing) lv_obj_set_style_text_color(b, lv_color_hex(0x3fa7ff), 0);
        if (++n == 150) break;
    }
    // Keep the input last in the focus order.
    lv_group_remove_obj(s_thr.input);
    lv_group_add_obj(s_thr.grp, s_thr.input);
    if (inputFocused || !lv_group_get_focused(s_thr.grp)) lv_group_focus_obj(s_thr.input);
    if (n) lv_obj_scroll_to_view(lv_obj_get_child(s_thr.list, n - 1), LV_ANIM_OFF);
}

void openThread(const char *peer) {
    strlcpy(s_thr.peer, peer, sizeof(s_thr.peer));
    lv_group_t *grp;
    lv_obj_t *scr = newScreen((String(LV_SYMBOL_ENVELOPE " ") + peer).c_str(), &grp);
    s_thr.grp = grp;
    s_thr.gen = 0;
    s_thr.list = makeList(scr);
    s_thr.input = makeInput(scr, grp, "Type, Enter to send", 67);
    lv_obj_add_event_cb(s_thr.input, [](lv_event_t *) { sendFromThread(); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_thr.list = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshThread);
    lv_group_focus_obj(s_thr.input);
}

struct {
    lv_obj_t *to, *text;
} s_new;

void sendNew() {
    String to = lv_textarea_get_text(s_new.to);
    to.trim();
    to.toUpperCase();
    String err;
    if (!messagesSend(to, lv_textarea_get_text(s_new.text), err)) {
        toast("Not sent: " + err);
        return;
    }
    s_pendingThread = to;   // swap this screen for the conversation
}

void openNewMessage(const char *to = "") {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_EDIT " New message", &grp);
    s_new.to = makeInput(scr, grp, "To (callsign)", 9);
    lv_textarea_set_accepted_chars(s_new.to, kCallChars);
    lv_textarea_set_text(s_new.to, to);
    s_new.text = makeInput(scr, grp, "Message", 67);
    lv_obj_add_event_cb(s_new.to, [](lv_event_t *) { lv_group_focus_obj(s_new.text); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_new.text, [](lv_event_t *) { sendNew(); }, LV_EVENT_READY, nullptr);
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, LV_SYMBOL_UPLOAD " Send");
    lv_obj_add_event_cb(b, [](lv_event_t *) { sendNew(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, b);
    makeNote(scr, "Up/down moves between fields. Max 67 characters.");
    pushScreen(scr, grp);
    lv_group_focus_obj(to[0] ? s_new.text : s_new.to);
}

struct {
    lv_obj_t *list;
    lv_group_t *grp;
    uint32_t gen;
    uint32_t lastBuildS;
    char peers[40][10];
} s_msgs;

void refreshMessages() {
    if (!s_msgs.list) return;
    if (s_msgs.gen == g_msgGen && uptimeS() - s_msgs.lastBuildS < 30) return;
    s_msgs.gen = g_msgGen;
    s_msgs.lastBuildS = uptimeS();
    const int fi = focusedIndex(s_msgs.list);
    lv_obj_clean(s_msgs.list);
    addItem(s_msgs.list, s_msgs.grp, LV_SYMBOL_EDIT, "New message", [](lv_event_t *) { openNewMessage(); });
    const auto threads = messagesThreads();
    size_t n = 0;
    for (const ChatThread &t : threads) {
        strlcpy(s_msgs.peers[n], t.peer.c_str(), sizeof(s_msgs.peers[n]));
        String line = t.peer;
        if (t.unread) line += "  (" + String(t.unread) + " new)";
        if (t.lastUptimeS) line += "  " + ageStr(t.lastUptimeS);
        line += "\n" + String(t.lastOutgoing ? "You: " : "") + t.lastText;
        lv_obj_t *b = addWrapItem(s_msgs.list, s_msgs.grp, line.c_str(), [](lv_event_t *e) {
            openThread(s_msgs.peers[(size_t)(uintptr_t)lv_event_get_user_data(e)]);
        }, (void *)(uintptr_t)n);
        if (t.unread) lv_obj_set_style_text_color(b, kAmber, 0);
        if (++n == 40) break;
    }
    if (!n) makeNote(s_msgs.list, "No conversations yet.");
    const uint32_t cnt = lv_obj_get_child_count(s_msgs.list);
    if (fi >= 0 && (uint32_t)fi < cnt) lv_group_focus_obj(lv_obj_get_child(s_msgs.list, fi));
}

void openMessages() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_ENVELOPE " Messages", &grp);
    s_msgs.grp = grp;
    s_msgs.gen = 0;
    s_msgs.list = makeList(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_msgs.list = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshMessages);
}

void onIncomingMessage(const ChatMsg &m) {
    // No banner when that conversation is already on screen.
    if (s_thr.list && !strcasecmp(s_thr.peer, m.peer)) return;
    toast(String(LV_SYMBOL_ENVELOPE " ") + m.peer + ": " + m.text, 6000);
}

// ── GPS ──────────────────────────────────────────────────────────────────────

lv_obj_t *s_gpsInfo;

void refreshGps() {
    if (!s_gpsInfo) return;
    const GpsFix f = gpsFix();
    const bool metric = g_config.units == Units::Metric;
    String t;
    if (!f.enabled) t = "GPS is off (Settings or the web Beacon tab).";
    else if (!f.alive) t = "No data from the GPS receiver yet.";
    else if (!f.valid) t = "Searching...  satellites: " + String(f.sats);
    else {
        char buf[64];
        snprintf(buf, sizeof(buf), "%.5f, %.5f\n", f.lat, f.lon);
        t = String("Fix, ") + f.sats + " satellites, HDOP " + String(f.hdop, 1) + "\n" + buf;
        if (f.hasAlt) t += "Alt: " + String(geo::formatAltitude(f.altM * 3.28084, metric).c_str()) + "\n";
        t += "Speed: " + String(geo::formatSpeed(f.speedKmh / 1.852, metric).c_str()) + "  course " + String(f.course) + "\xC2\xB0";
    }
    const BeaconStatus b = beaconStatus();
    t += String("\n\nBeacon: ") + b.mode;
    if (b.sentAny) t += ", last " + String(geo::formatAge(b.lastAgoS).c_str()) + " ago";
    if (b.nextInS >= 0) t += ", next in " + String(geo::formatAge(b.nextInS).c_str());
    if (b.waitingFor.length()) t += "\nWaiting for " + b.waitingFor;
    if (b.lastError.length()) t += "\nLast error: " + b.lastError;
    lv_label_set_text(s_gpsInfo, t.c_str());
}

void openGps() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_GPS " GPS & beacon", &grp);
    s_gpsInfo = lv_label_create(scr);
    lv_obj_set_width(s_gpsInfo, lv_pct(100));
    lv_label_set_long_mode(s_gpsInfo, LV_LABEL_LONG_WRAP);
    lv_obj_t *list = makeList(scr);
    addItem(list, grp, LV_SYMBOL_UPLOAD, "Send beacon now", [](lv_event_t *) { beaconNowUi(); });
    lv_obj_t *gb = addItem(list, grp, LV_SYMBOL_POWER, "", [](lv_event_t *e) {
        g_config.gpsEnabled = !g_config.gpsEnabled;
        configSave();
        lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), -1),
                          g_config.gpsEnabled ? "Turn GPS off" : "Turn GPS on");
    });
    lv_label_set_text(lv_obj_get_child(gb, -1), g_config.gpsEnabled ? "Turn GPS off" : "Turn GPS on");
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_gpsInfo = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshGps);
}

// ── TNC ──────────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *info, *list;
    lv_group_t *grp;
    std::vector<BleScanResult> shown;
    bool wasScanning;
} s_tnc;

bool s_reopenTnc;  // rebuild the radio screen after an RF-link switch

String loraSummary() {
    const LoraStatus l = g_lora.status();
    String t = String("LoRa: ") + loraStateName(l.state) + "  " + String(l.freqMhz, 3) + " MHz";
    if (l.state == LoraState::Failed && l.lastError.length()) t += "\n" + l.lastError;
    return t;
}

void refreshTnc() {
    if (!s_tnc.info) return;
    if (g_config.rfLink == RfLink::Lora) {
        const LoraStatus l = g_lora.status();
        String t = loraSummary() + "\nSF" + String(g_config.loraSf) + "  BW" + String(g_config.loraBwKhz, 1) +
                   "  CR4/" + String(g_config.loraCr) + "  " + String(g_config.loraPowerDbm) + " dBm" +
                   "\nRX " + String(l.rxFrames) + "  TX " + String(l.txFrames) + "  bad " + String(l.rxBad);
        if (l.lastRxS)
            t += "\nLast heard " + String(uptimeS() - l.lastRxS) + "s ago, " + String(l.lastRssi, 0) + " dBm, SNR " +
                 String(l.lastSnr, 1);
        t += "\nRadio settings are in the web config.";
        lv_label_set_text(s_tnc.info, t.c_str());
        return;
    }
    const BleStatus b = g_ble.status();
    String t = String("State: ") + bleStateName(b.state);
    if (b.peerAddress.length()) t += "\n" + (b.peerName.length() ? b.peerName + "  " : String()) + b.peerAddress;
    if (b.state == BleState::Connected)
        t += "\nMTU " + String(b.mtu) + "  RX " + String(b.rxFrames) + "  TX " + String(b.txFrames);
    else if (b.lastError.length())
        t += "\n" + b.lastError + (b.nextAttemptInS ? ", retry in " + String(b.nextAttemptInS) + "s" : String());
    if (b.scanning) t += "\nScanning...";
    lv_label_set_text(s_tnc.info, t.c_str());

    // Refresh results while scanning and once when it finishes, unless the
    // user is already moving through them.
    if ((b.scanning || s_tnc.wasScanning) && focusedIndex(s_tnc.list) < 0) {
        s_tnc.shown = g_ble.scanResults();
        lv_obj_clean(s_tnc.list);
        for (size_t i = 0; i < s_tnc.shown.size(); i++) {
            const auto &r = s_tnc.shown[i];
            const String line = (r.kissService ? "[KISS] " : "") + r.name + "  " + String(r.rssi) + " dBm\n" + r.address;
            addWrapItem(s_tnc.list, s_tnc.grp, line.c_str(), [](lv_event_t *e) {
                const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
                if (idx >= s_tnc.shown.size()) return;
                const auto &r = s_tnc.shown[idx];
                g_config.tncAddress = r.address;
                g_config.tncAddressType = r.addressType;
                g_config.tncName = r.name;
                configSave();
                g_ble.connectNow();
            }, (void *)(uintptr_t)i);
        }
        if (!b.scanning && s_tnc.shown.empty()) addWrapItem(s_tnc.list, s_tnc.grp, "No devices found", nullptr, nullptr);
    }
    s_tnc.wasScanning = b.scanning;
}

void openTnc() {
    lv_group_t *grp;
    const bool lora = g_config.rfLink == RfLink::Lora;
    lv_obj_t *scr = newScreen(lora ? "Radio (LoRa)" : LV_SYMBOL_BLUETOOTH " Radio (TNC)", &grp);
    s_tnc.grp = grp;
    s_tnc.shown.clear();
    s_tnc.wasScanning = false;
    s_tnc.info = lv_label_create(scr);
    lv_obj_set_width(s_tnc.info, lv_pct(100));
    lv_label_set_long_mode(s_tnc.info, LV_LABEL_LONG_WRAP);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(row, 4, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    auto btn = [&](const char *txt, lv_event_cb_t cb) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_group_add_obj(grp, b);
    };
    btn(lora ? "Use BLE TNC" : "Use LoRa", [](lv_event_t *) {
        g_config.rfLink = g_config.rfLink == RfLink::Lora ? RfLink::Ble : RfLink::Lora;
        configSave();
        g_ble.reconfigure();
        g_lora.reconfigure();
        s_reopenTnc = true;
    });
    if (lora) {
        s_tnc.list = nullptr;
        lv_obj_add_event_cb(scr, [](lv_event_t *) { s_tnc.info = nullptr; }, LV_EVENT_DELETE, nullptr);
        pushScreen(scr, grp, refreshTnc);
        return;
    }
    btn(LV_SYMBOL_REFRESH " Scan", [](lv_event_t *) {
        lv_obj_clean(s_tnc.list);
        g_ble.startScan(8);
    });
    btn("Connect", [](lv_event_t *) { g_ble.connectNow(); });
    btn("Disconnect", [](lv_event_t *) { g_ble.disconnect(); });

    s_tnc.list = makeList(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_tnc.info = nullptr; }, LV_EVENT_DELETE, nullptr);
    pushScreen(scr, grp, refreshTnc);
}

// ── Backup ───────────────────────────────────────────────────────────────────

void backupToSdNow() {
    String name, err;
    if (backupSaveToSd(false, name, err))
        openPlaceholder("Backup", ("Saved to SD card:\n/rhino/backups/" + name +
                                   "\n\nPasswords are not included. Restore from the web config (Device tab).")
                                      .c_str());
    else openPlaceholder("Backup", ("Backup failed: " + err).c_str());
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
    lv_obj_t *hp = addItem(list, grp, LV_SYMBOL_HOME, "", [](lv_event_t *e) {
        g_config.homePanel = (HomePanel)(((uint8_t)g_config.homePanel + 1) % ((uint8_t)HomePanel::None + 1));
        configSave();
        lv_label_set_text_fmt(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), -1), "Home panel: %s",
                              homePanelName(g_config.homePanel));
    });
    lv_label_set_text_fmt(lv_obj_get_child(hp, -1), "Home panel: %s", homePanelName(g_config.homePanel));
    addItem(list, grp, LV_SYMBOL_GPS, "GPS & beacon", [](lv_event_t *) { openGps(); });
    addItem(list, grp, LV_SYMBOL_BLUETOOTH, "Radio link (TNC / LoRa)", [](lv_event_t *) { openTnc(); });
    addItem(list, grp, LV_SYMBOL_SD_CARD, "Backup settings to SD card", [](lv_event_t *) { backupToSdNow(); });
    addItem(list, grp, LV_SYMBOL_LIST, "About", [](lv_event_t *) { openAbout(); });
    addItem(list, grp, LV_SYMBOL_REFRESH, "Reboot", [](lv_event_t *) { ESP.restart(); });
    pushScreen(scr, grp);
}

// ── Info modal ───────────────────────────────────────────────────────────────

struct {
    lv_obj_t *bg, *qr, *text;
    lv_group_t *grp;
    String key;
} s_info;

void refreshInfo() {
    if (!s_info.bg) return;
    const WifiStatus w = wifiStatus();
    const BleStatus b = g_ble.status();
    String t = g_config.fullCallsign() + "\n";
    if (w.staConnected) t += "Wi-Fi: " + w.staSsid + "\nhttp://" + w.staIp + "\n";
    else if (w.staConfigured) t += "Wi-Fi: connecting to " + w.staSsid + "...\n";
    else t += "Wi-Fi: not set up\n";
    if (w.apActive) t += "Setup AP: " + w.apSsid + "\nhttp://" + w.apIp + "\n";
    if (g_config.rfLink == RfLink::Lora) {
        t += loraSummary();
    } else {
        t += String("TNC: ") + bleStateName(b.state);
        if (b.peerName.length()) t += " (" + b.peerName + ")";
    }
    if (g_config.aprsIsEnabled) {
        const AprsIsStatus is = aprsIsStatus();
        t += String("\nAPRS-IS: ") + is.state + (is.connected && !is.verified ? " (unverified)" : "");
    }
    t += "\nStations heard: " + String(stationDbCount());
    const GpsFix g = gpsFix();
    t += String("\nGPS: ") + (!g.enabled ? "off" : g.valid ? "fix, " + String(g.sats) + " sats" : g.alive ? "searching" : "no data");
    const BeaconStatus bs = beaconStatus();
    t += String("\nBeacon: ") + bs.mode + (bs.waitingFor.length() ? " (waiting for " + bs.waitingFor + ")" : String());
    const String key = t + String(w.apActive);
    if (key == s_info.key) return;
    s_info.key = key;
    lv_label_set_text(s_info.text, t.c_str());
    if (w.apActive) {
        const String qr = "WIFI:T:nopass;S:" + w.apSsid + ";;";
        lv_qrcode_update(s_info.qr, qr.c_str(), qr.length());
        lv_obj_remove_flag(s_info.qr, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_info.qr, LV_OBJ_FLAG_HIDDEN);
    }
}

void closeInfo() {
    if (!s_info.bg) return;
    lv_obj_delete(s_info.bg);
    lv_group_delete(s_info.grp);
    s_info = {};
    lv_group_t *g = s_stack[s_depth - 1].grp;
    lv_group_set_default(g);
    lv_indev_set_group(keyboardIndev(), g);
}

void openInfo() {
    if (s_info.bg) return;
    // Dimmed backdrop below the status bar; the box takes all key input.
    s_info.bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_info.bg, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - kStatusH);
    lv_obj_align(s_info.bg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(s_info.bg, 0, 0);
    lv_obj_set_style_border_width(s_info.bg, 0, 0);
    lv_obj_set_style_bg_color(s_info.bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_info.bg, LV_OPA_70, 0);
    lv_obj_clear_flag(s_info.bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *box = lv_obj_create(s_info.bg);
    lv_obj_set_size(box, lv_pct(94), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(box, lv_pct(100), 0);
    lv_obj_center(box);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(box, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(box, 6, 0);

    s_info.qr = lv_qrcode_create(box);
    lv_qrcode_set_size(s_info.qr, 96);
    lv_qrcode_set_dark_color(s_info.qr, lv_color_black());
    lv_qrcode_set_light_color(s_info.qr, lv_color_white());
    lv_obj_set_style_border_color(s_info.qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(s_info.qr, 3, 0);

    s_info.text = lv_label_create(box);
    lv_obj_set_width(s_info.text, lv_pct(100));
    lv_label_set_long_mode(s_info.text, LV_LABEL_LONG_WRAP);

    lv_obj_t *close = lv_button_create(box);
    lv_obj_t *cl = lv_label_create(close);
    lv_label_set_text(cl, "Close");
    lv_obj_add_event_cb(close, [](lv_event_t *) { s_pendingCloseInfo = true; }, LV_EVENT_CLICKED, nullptr);

    s_info.grp = lv_group_create();
    lv_group_add_obj(s_info.grp, close);
    lv_group_set_default(s_info.grp);
    lv_indev_set_group(keyboardIndev(), s_info.grp);
    refreshInfo();
}

// ── Home ─────────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *panel, *title, *body;
    String key;
    char call[10];   // station the panel links to
    bool apWasActive;
} s_home;

void refreshHomePanel() {
    const HomePanel mode = g_config.homePanel;
    if (mode == HomePanel::None) {
        lv_obj_add_flag(s_home.panel, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_remove_flag(s_home.panel, LV_OBJ_FLAG_HIDDEN);

    String title, body;
    s_home.call[0] = 0;
    if (mode == HomePanel::LastHeard) {
        const Station *st = stationDbCount() ? stationDbAt(0) : nullptr;
        if (st) {
            title = "Last heard, " + ageStr(st->lastHeardS) + " ago";
            body = st->call;
            double myLat, myLon;
            if (st->hasPos && myPosition(myLat, myLon))
                body += "  " + String(geo::formatDistance(geo::distanceKm(myLat, myLon, st->lat, st->lon),
                                                          g_config.units == Units::Metric).c_str()) +
                        " " + geo::compassPoint(geo::bearingDeg(myLat, myLon, st->lat, st->lon));
            const char *extra = st->status[0] ? st->status : st->comment;
            if (extra[0]) body += String("\n") + extra;
            strlcpy(s_home.call, st->call, sizeof(s_home.call));
        } else {
            title = "Last heard";
            body = "No stations heard yet.";
        }
    } else if (mode == HomePanel::LastMessage) {
        const HeardMessage &m = lastMessage();
        if (m.valid) {
            title = "Last message, " + ageStr(m.uptimeS) + " ago" + (m.forMe ? "  (to you)" : "");
            body = String(m.from) + " > " + m.to + "\n" + m.text;
            strlcpy(s_home.call, m.from, sizeof(s_home.call));
        } else {
            title = "Last message";
            body = "No messages received yet.";
        }
    } else {
        const LogEntry *e = packetLogCount() ? packetLogAt(0) : nullptr;
        title = e ? "Last packet, " + ageStr(e->uptimeS) + " ago" : String("Last packet");
        body = e ? e->raw : "No packets yet.";
    }
    const String key = title + body;
    if (key == s_home.key) return;
    s_home.key = key;
    lv_label_set_text(s_home.title, title.c_str());
    lv_label_set_text(s_home.body, body.c_str());
}

void refreshHome() {
    // Pop the info up by itself whenever the setup AP comes on, so the QR code
    // is there for first-time setup.
    const bool ap = wifiStatus().apActive;
    if (ap && !s_home.apWasActive && s_depth == 1) openInfo();
    s_home.apWasActive = ap;
    refreshHomePanel();
}

void buildHome() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(nullptr, &grp);

    s_home.panel = lv_obj_create(scr);
    lv_obj_set_size(s_home.panel, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(s_home.panel, 5, 0);
    lv_obj_set_flex_flow(s_home.panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_home.panel, 1, 0);
    lv_obj_clear_flag(s_home.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_home.panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_home.panel, [](lv_event_t *) {
        if (g_config.homePanel == HomePanel::LastPacket) openMonitor();
        else if (s_home.call[0]) openStationDetail(s_home.call);
    }, LV_EVENT_CLICKED, nullptr);

    s_home.title = lv_label_create(s_home.panel);
    lv_obj_set_style_text_font(s_home.title, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(s_home.title, kGrey, 0);
    s_home.body = lv_label_create(s_home.panel);
    lv_obj_set_width(s_home.body, lv_pct(100));
    // Fixed two lines so the panel never grows over the menu.
    lv_label_set_long_mode(s_home.body, LV_LABEL_LONG_DOT);
    lv_obj_set_height(s_home.body, 2 * lv_font_get_line_height(&lv_font_montserrat_14));
    lv_group_add_obj(grp, s_home.panel);

    lv_obj_t *list = makeList(scr);
    lv_obj_t *first = addItem(list, grp, LV_SYMBOL_LIST, "Stations", [](lv_event_t *) { openStations(); });
    addItem(list, grp, LV_SYMBOL_ENVELOPE, "Messages", [](lv_event_t *) { openMessages(); });
    addItem(list, grp, LV_SYMBOL_EYE_OPEN, "Monitor", [](lv_event_t *) { openMonitor(); });
    addItem(list, grp, LV_SYMBOL_UPLOAD, "Send beacon now", [](lv_event_t *) { beaconNowUi(); });
    addItem(list, grp, LV_SYMBOL_GPS, "Map", [](lv_event_t *) {
        openMap();
    });
    addItem(list, grp, LV_SYMBOL_WIFI, "Status & connection info", [](lv_event_t *) { openInfo(); });
    addItem(list, grp, LV_SYMBOL_SETTINGS, "Settings", [](lv_event_t *) { openSettings(); });
    lv_group_focus_obj(first);

    pushScreen(scr, grp, refreshHome);
}

enum class Nav : uint8_t { None, Back, Home, Messages, Map, Monitor };
volatile Nav s_pendingNav = Nav::None;

// Runs inside LVGL's indev read; navigation is deferred to uiLoop() so no
// screen is deleted while LVGL is still processing input for it.
bool hotkey(uint8_t key) {
    switch (key) {
        case M9_KEY_BACK: s_pendingNav = Nav::Back; return true;
        case M9_KEY_HOME: s_pendingNav = Nav::Home; return true;
        case M9_KEY_MESSAGE: s_pendingNav = Nav::Messages; return true;
        case M9_KEY_MAP: s_pendingNav = Nav::Map; return true;
        case M9_KEY_SUB_HOME: s_pendingNav = Nav::Monitor; return true;
        default: return false;
    }
}

void tick(lv_timer_t *) {
    updateStatusBar();
    if (s_depth > 0 && s_stack[s_depth - 1].refresh) s_stack[s_depth - 1].refresh();
    refreshInfo();
}

}  // namespace

void uiBegin() {
    keyboardSetHotkeyHandler(hotkey);
    buildStatusBar();
    buildHome();
    updateStatusBar();
    messagesSetIncomingHook(onIncomingMessage);
    lv_timer_create(tick, 1000, nullptr);
}

void uiLoop() {
    const Nav nav = s_pendingNav;
    s_pendingNav = Nav::None;
    if (s_pendingCloseInfo || (nav != Nav::None && s_info.bg)) {
        s_pendingCloseInfo = false;
        closeInfo();
        if (nav == Nav::Home) popToHome();
        return;
    }
    if (nav == Nav::Back) popScreen();
    else if (nav == Nav::Home) popToHome();
    else if (nav == Nav::Messages) {
        popToHome();
        openMessages();
    } else if (nav == Nav::Map) {
        popToHome();
        openMap();
    } else if (nav == Nav::Monitor) {
        popToHome();
        openMonitor();
    }
    if (s_pendingThread.length()) {
        const String peer = s_pendingThread;
        s_pendingThread = "";
        popScreen();
        openThread(peer.c_str());
    }
    if (s_reopenTnc) {
        s_reopenTnc = false;
        popScreen();
        openTnc();
    }
    toastLoop();
}
