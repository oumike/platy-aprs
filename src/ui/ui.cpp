#include "ui.h"
#include "config/config.h"
#include "hal/board.h"
#include "hal/keyboard.h"
#include "hal/pins_m9.h"
#include "hal/sd_card.h"
#include "net/wifi_manager.h"
#include "services/backup.h"
#include "services/data_store.h"
#include "services/router.h"
#include "transport/ble_kiss.h"
#include "util/geo.h"
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

    const BleStatus b = g_ble.status();
    lv_obj_set_style_text_color(s_sbBt,
                                b.state == BleState::Connected                                     ? kGreen
                                : b.state == BleState::Connecting || b.state == BleState::Waiting ? kAmber
                                                                                                   : kGrey,
                                0);

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
        snprintf(buf, sizeof(buf), "%s%s  %s", e->tx ? "TX " : "", ageStr(e->uptimeS).c_str(), e->raw);
        lv_obj_t *b = addWrapItem(s_mon.list, s_mon.grp, buf, nullptr, nullptr);
        if (e->tx) lv_obj_set_style_text_color(b, lv_color_hex(0x3fa7ff), 0);
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
    if (!packetLogCount()) makeNote(scr, "No packets yet. Connect a TNC under Settings > TNC.");
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

// ── TNC ──────────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *info, *list;
    lv_group_t *grp;
    std::vector<BleScanResult> shown;
    bool wasScanning;
} s_tnc;

void refreshTnc() {
    if (!s_tnc.info) return;
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
    lv_obj_t *scr = newScreen(LV_SYMBOL_BLUETOOTH " TNC", &grp);
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
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 4, 0);
    auto btn = [&](const char *txt, lv_event_cb_t cb) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
        lv_group_add_obj(grp, b);
    };
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
    addItem(list, grp, LV_SYMBOL_BLUETOOTH, "TNC (Bluetooth)", [](lv_event_t *) { openTnc(); });
    addItem(list, grp, LV_SYMBOL_SD_CARD, "Backup settings to SD card", [](lv_event_t *) { backupToSdNow(); });
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
    const BleStatus b = g_ble.status();
    const String tnc = String("TNC: ") + bleStateName(b.state) +
                       (b.state == BleState::Connected ? "\nHeard: " + String(stationDbCount()) + " stations" : String());
    const String key = String(showQr) + w.apSsid + w.staIp + String(w.staConnected) + g_config.fullCallsign() + tnc;
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
            lv_label_set_text_fmt(s_homeText, "%s\nWi-Fi: %s\nConfig: http://%s\n%s", g_config.fullCallsign().c_str(),
                                  w.staSsid.c_str(), w.staIp.c_str(), tnc.c_str());
        else
            lv_label_set_text_fmt(s_homeText, "%s\nWi-Fi: connecting to %s...\n%s", g_config.fullCallsign().c_str(),
                                  w.staSsid.c_str(), tnc.c_str());
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
    addItem(list, grp, LV_SYMBOL_LIST, "Stations", [](lv_event_t *) { openStations(); });
    addItem(list, grp, LV_SYMBOL_ENVELOPE, "Messages", [](lv_event_t *) {
        openPlaceholder("Messages", "APRS messaging arrives in milestone 3.");
    });
    addItem(list, grp, LV_SYMBOL_EYE_OPEN, "Monitor", [](lv_event_t *) { openMonitor(); });
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
