#include "ui.h"
#include "config/config.h"
#include "hal/audio.h"
#include "hal/board.h"
#include "hal/keyboard.h"
#include "hal/board_pins.h"
#include "hal/keys.h"
#include "hal/gps.h"
#include "hal/sd_card.h"
#include "hal/screen.h"
#include "net/aprs_is.h"
#include "net/wifi_manager.h"
#include "services/alerts.h"
#include "services/notify_led.h"
#include "services/backup.h"
#include "services/beacon.h"
#include "services/messages.h"
#include "services/ota.h"
#include "services/favorites.h"
#include "services/data_store.h"
#include "services/router.h"
#include "transport/ble_kiss.h"
#include "transport/lora_aprs.h"
#include "util/geo.h"
#include "ui/map_view.h"
#include "version.h"
#include <Arduino.h>
#include <WiFi.h>
#include <lvgl.h>
#include <src/misc/lv_text_private.h>
#include <src/themes/lv_theme_private.h>
#include <time.h>
#include <algorithm>

LV_FONT_DECLARE(font_star_14);

namespace {

constexpr int kStatusH = 22;
// Palette for the current theme (themeColors() sets it; dark values here).
lv_color_t kGrey = LV_COLOR_MAKE(0x60, 0x66, 0x70);
lv_color_t kGreen = LV_COLOR_MAKE(0x4c, 0xd0, 0x7d);
lv_color_t kAmber = LV_COLOR_MAKE(0xff, 0xb3, 0x47);
lv_color_t kRed = LV_COLOR_MAKE(0xff, 0x5c, 0x5c);
lv_color_t kBarBg = LV_COLOR_MAKE(0x1b, 0x20, 0x27);        // status bar
lv_color_t kItemFocusBg = LV_COLOR_MAKE(0x4a, 0x34, 0x10);  // keypad-focused list item
lv_color_t kItemFocusText = LV_COLOR_MAKE(0xff, 0xff, 0xff);
lv_color_t kDimText = LV_COLOR_MAKE(0x9a, 0xa4, 0xb2);      // secondary text
lv_color_t kOutText = LV_COLOR_MAKE(0x3f, 0xa7, 0xff);      // our own messages and packets

struct Screen {
    lv_obj_t *scr;
    lv_group_t *grp;
    void (*refresh)();
};

constexpr int kMaxDepth = 6;
Screen s_stack[kMaxDepth];
int s_depth = 0;

enum class Nav : uint8_t { None, Back, Home, Messages, Map, Monitor, Stations };
volatile Nav s_pendingNav = Nav::None;

volatile bool s_pendingCloseInfo = false;
volatile bool s_pendingFav = false;   // Alt+F
lv_obj_t *s_sbBack = nullptr;   // touch boards: Back (hold = Home) in the status bar
lv_obj_t *s_sbCall, *s_sbMsg, *s_sbWifi, *s_sbBt, *s_sbGps, *s_sbBatt, *s_sbTime;
lv_obj_t *s_sbTnc, *s_sbTncTx;         // connected-TNC radio icon + transmit arrow
uint32_t s_sbTncLastTx = 0;
lv_obj_t *s_sbIs, *s_sbIsUp;           // APRS-IS up/down arrows; the up arrow lights on send
uint32_t s_sbIsLastTx = 0, s_isRxSeen = 0, s_isRxBlinkMs = 0;
uint32_t s_rxSeenBle = 0, s_rxSeenLora = 0, s_rxBlinkMs = 0;
constexpr uint32_t kRxBlinkGapMs = 5000;   // at most one radio blink per gap, however busy the channel

// ── Theme: high-contrast keypad focus + glowing buttons ──────────────────────

lv_theme_t s_theme;
lv_style_t s_stFocus, s_stItemFocus, s_stBtnGlow, s_stBtnGlowFocus, s_stCursor;

void themeApply(lv_theme_t *, lv_obj_t *obj) {
    const bool isBtn = lv_obj_check_type(obj, &lv_button_class);
    const bool isItem = lv_obj_check_type(obj, &lv_list_button_class);
    if (isBtn) {
        lv_obj_add_style(obj, &s_stBtnGlow, 0);
        lv_obj_add_style(obj, &s_stBtnGlowFocus, LV_STATE_FOCUS_KEY);
    }
    if (isItem) lv_obj_add_style(obj, &s_stItemFocus, LV_STATE_FOCUS_KEY);
    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        lv_obj_add_style(obj, &s_stCursor, LV_PART_CURSOR | LV_STATE_FOCUSED);
    }
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_CLICKABLE)) lv_obj_add_style(obj, &s_stFocus, LV_STATE_FOCUS_KEY);
}

UiTheme s_appliedTheme = UiTheme::Dark;
bool s_stylesInited = false;

void themeColors(bool light) {
    if (light) {
        kGrey = lv_color_hex(0x9aa0a8);
        kGreen = lv_color_hex(0x1f9d55);
        kAmber = lv_color_hex(0xc77700);
        kRed = lv_color_hex(0xd32f2f);
        kBarBg = lv_color_hex(0xdfe4ea);
        kItemFocusBg = lv_color_hex(0xffe0a8);
        kItemFocusText = lv_color_hex(0x000000);
        kDimText = lv_color_hex(0x5f6b7a);
        kOutText = lv_color_hex(0x1565c0);
    } else {
        kGrey = lv_color_hex(0x606670);
        kGreen = lv_color_hex(0x4cd07d);
        kAmber = lv_color_hex(0xffb347);
        kRed = lv_color_hex(0xff5c5c);
        kBarBg = lv_color_hex(0x1b2027);
        kItemFocusBg = lv_color_hex(0x4a3410);
        kItemFocusText = lv_color_hex(0xffffff);
        kDimText = lv_color_hex(0x9aa4b2);
        kOutText = lv_color_hex(0x3fa7ff);
    }
}

// Applies g_config.theme to LVGL's default theme and our styles. Objects that
// already exist keep their local colours; rebuildForTheme() remakes them.
void themeInit() {
    const bool light = g_config.theme == UiTheme::Light;
    s_appliedTheme = g_config.theme;
    themeColors(light);
    lv_style_t *styles[] = {&s_stFocus, &s_stItemFocus, &s_stBtnGlow, &s_stBtnGlowFocus, &s_stCursor};
    for (lv_style_t *st : styles) {
        if (s_stylesInited) lv_style_reset(st);
        lv_style_init(st);
    }
    s_stylesInited = true;

    lv_style_set_outline_color(&s_stFocus, kAmber);
    lv_style_set_outline_opa(&s_stFocus, LV_OPA_COVER);
    lv_style_set_outline_width(&s_stFocus, 2);
    lv_style_set_outline_pad(&s_stFocus, 0);

    lv_style_set_bg_color(&s_stItemFocus, kItemFocusBg);
    lv_style_set_bg_opa(&s_stItemFocus, LV_OPA_COVER);
    lv_style_set_text_color(&s_stItemFocus, kItemFocusText);

    lv_style_set_shadow_color(&s_stBtnGlow, lv_palette_main(LV_PALETTE_BLUE));
    lv_style_set_shadow_width(&s_stBtnGlow, 10);
    lv_style_set_shadow_spread(&s_stBtnGlow, 0);
    lv_style_set_shadow_offset_x(&s_stBtnGlow, 0);
    lv_style_set_shadow_offset_y(&s_stBtnGlow, 0);
    lv_style_set_shadow_opa(&s_stBtnGlow, light ? LV_OPA_30 : LV_OPA_50);

    lv_style_set_shadow_color(&s_stBtnGlowFocus, kAmber);
    lv_style_set_shadow_width(&s_stBtnGlowFocus, 16);
    lv_style_set_shadow_spread(&s_stBtnGlowFocus, 2);
    lv_style_set_shadow_opa(&s_stBtnGlowFocus, LV_OPA_80);
    lv_style_set_text_color(&s_stBtnGlowFocus, lv_color_white());

    lv_style_set_border_color(&s_stCursor, kAmber);
    lv_style_set_border_width(&s_stCursor, 3);
    lv_style_set_border_side(&s_stCursor, LV_BORDER_SIDE_LEFT);
    lv_style_set_border_opa(&s_stCursor, LV_OPA_COVER);

    // Re-init LVGL's default theme in the chosen mode, then layer ours on it.
    lv_theme_t *base = lv_theme_default_get();
    const lv_color_t primary = base ? base->color_primary : lv_palette_main(LV_PALETTE_BLUE);
    const lv_color_t secondary = base ? base->color_secondary : lv_palette_main(LV_PALETTE_RED);
    base = lv_theme_default_init(nullptr, primary, secondary, !light, LV_FONT_DEFAULT);
    s_theme = *base;
    lv_theme_set_parent(&s_theme, base);
    lv_theme_set_apply_cb(&s_theme, themeApply);
    lv_display_set_theme(nullptr, &s_theme);
}

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

// ── On-screen keyboard (touch boards without a physical keyboard) ────────────

#if HAS_TOUCH && !HAS_KEYBOARD
lv_obj_t *s_osk = nullptr;

void oskHide() {
    if (!s_osk || lv_obj_has_flag(s_osk, LV_OBJ_FLAG_HIDDEN)) return;
    lv_keyboard_set_textarea(s_osk, nullptr);
    lv_obj_add_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_pad_bottom(lv_screen_active(), 4, 0);
}

void oskShow(lv_obj_t *ta) {
    const int h = lv_display_get_vertical_resolution(nullptr) / 2;
    if (!s_osk) {
        s_osk = lv_keyboard_create(lv_layer_top());
        lv_obj_set_size(s_osk, lv_pct(100), h);
        lv_obj_align(s_osk, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_obj_add_event_cb(s_osk, [](lv_event_t *) { oskHide(); }, LV_EVENT_READY, nullptr);
        lv_obj_add_event_cb(s_osk, [](lv_event_t *) { oskHide(); }, LV_EVENT_CANCEL, nullptr);
    }
    lv_keyboard_set_textarea(s_osk, ta);
    lv_obj_remove_flag(s_osk, LV_OBJ_FLAG_HIDDEN);
    // Shrink the screen's content area so the field stays above the keyboard.
    lv_obj_t *scr = lv_obj_get_screen(ta);
    lv_obj_set_style_pad_bottom(scr, h + 4, 0);
    lv_obj_update_layout(scr);
    lv_obj_scroll_to_view_recursive(ta, LV_ANIM_OFF);
}

void oskAttach(lv_obj_t *ta) {
    lv_obj_add_event_cb(ta, [](lv_event_t *e) { oskShow((lv_obj_t *)lv_event_get_target(e)); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        if (s_osk && lv_keyboard_get_textarea(s_osk) == lv_event_get_target(e)) oskHide();
    }, LV_EVENT_DELETE, nullptr);
}
#else
void oskHide() {}
void oskAttach(lv_obj_t *) {}
#endif

void updateBackButton() {
    if (!s_sbBack) return;
    if (s_depth > 1) lv_obj_remove_flag(s_sbBack, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_sbBack, LV_OBJ_FLAG_HIDDEN);
}

void pushScreen(lv_obj_t *scr, lv_group_t *grp, void (*refresh)() = nullptr) {
    if (s_depth == kMaxDepth) return;
    oskHide();
    s_stack[s_depth++] = {scr, grp, refresh};
    lv_group_set_default(grp);
    lv_indev_set_group(keyboardIndev(), grp);
    lv_screen_load(scr);
    updateBackButton();
    if (refresh) refresh();
}

void popScreen() {
    if (s_depth <= 1) return;
    oskHide();
    Screen top = s_stack[--s_depth];
    Screen &prev = s_stack[s_depth - 1];
    lv_group_set_default(prev.grp);
    lv_indev_set_group(keyboardIndev(), prev.grp);
    lv_screen_load(prev.scr);
    lv_group_delete(top.grp);
    lv_obj_delete(top.scr);
    updateBackButton();
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

lv_obj_t *sbRect(lv_obj_t *parent, int x, int y, int w, int h, int r, lv_color_t c) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_radius(o, r, 0);
    lv_obj_set_style_bg_color(o, c, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

// Handheld radio drawn from rectangles: antenna, body, display, speaker grille.
void buildTncIcon(lv_obj_t *bar) {
    const lv_color_t bg = kBarBg;
    s_sbTnc = lv_obj_create(bar);
    lv_obj_remove_style_all(s_sbTnc);
    lv_obj_set_size(s_sbTnc, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_sbTnc, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_sbTnc, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_sbTnc, 2, 0);
    lv_obj_remove_flag(s_sbTnc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_sbTnc, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *radio = lv_obj_create(s_sbTnc);
    lv_obj_remove_style_all(radio);
    lv_obj_set_size(radio, 10, 16);
    lv_obj_remove_flag(radio, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(radio, LV_OBJ_FLAG_SCROLLABLE);
    sbRect(radio, 1, 0, 2, 5, 1, kGreen);
    sbRect(radio, 0, 4, 10, 12, 2, kGreen);
    sbRect(radio, 2, 6, 6, 3, 0, bg);
    sbRect(radio, 2, 11, 6, 1, 0, bg);
    sbRect(radio, 2, 13, 6, 1, 0, bg);
    s_sbTncTx = lv_label_create(s_sbTnc);
    lv_label_set_text(s_sbTncTx, LV_SYMBOL_UPLOAD);
    lv_obj_set_style_text_color(s_sbTncTx, kAmber, 0);
    lv_obj_add_flag(s_sbTncTx, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_sbTnc, LV_OBJ_FLAG_HIDDEN);
}

// A 6x14 arrow from rectangles: three-step head plus a 2 px shaft.
lv_obj_t *sbArrow(lv_obj_t *parent, int x, bool up) {
    lv_obj_t *a = lv_obj_create(parent);
    lv_obj_remove_style_all(a);
    lv_obj_set_pos(a, x, 0);
    lv_obj_set_size(a, 6, 14);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(a, LV_OBJ_FLAG_SCROLLABLE);
    const int y0 = up ? 0 : 13, dy = up ? 1 : -1;
    sbRect(a, 2, y0, 2, 1, 0, kGreen);
    sbRect(a, 1, y0 + dy, 4, 1, 0, kGreen);
    sbRect(a, 0, y0 + 2 * dy, 6, 1, 0, kGreen);
    sbRect(a, 2, up ? 3 : 0, 2, 11, 0, kGreen);
    return a;
}

void sbArrowColor(lv_obj_t *arrow, lv_color_t c) {
    for (uint32_t i = 0; i < lv_obj_get_child_count(arrow); i++)
        lv_obj_set_style_bg_color(lv_obj_get_child(arrow, i), c, 0);
}

// APRS-IS link: an up arrow beside a down arrow, shown while connected.
void buildIsIcon(lv_obj_t *bar) {
    s_sbIs = lv_obj_create(bar);
    lv_obj_remove_style_all(s_sbIs);
    lv_obj_set_size(s_sbIs, 13, 14);
    lv_obj_remove_flag(s_sbIs, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_sbIs, LV_OBJ_FLAG_SCROLLABLE);
    s_sbIsUp = sbArrow(s_sbIs, 0, true);
    sbArrow(s_sbIs, 7, false);
    lv_obj_add_flag(s_sbIs, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *sbLabel(lv_obj_t *bar, const char *txt) {
    lv_obj_t *l = lv_label_create(bar);
    lv_label_set_text(l, txt);
    return l;
}

void rxBlinkTimer(lv_timer_t *);

void buildStatusBar() {
    lv_obj_t *bar = lv_obj_create(lv_layer_top());
    lv_obj_set_size(bar, lv_pct(100), kStatusH);
    lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_bg_color(bar, kBarBg, 0);
    lv_obj_set_style_pad_hor(bar, 6, 0);
    lv_obj_set_style_pad_ver(bar, 0, 0);
    lv_obj_set_style_text_font(bar, &lv_font_montserrat_12, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(bar, 8, 0);

#if HAS_TOUCH
    s_sbBack = lv_button_create(bar);
    lv_obj_set_size(s_sbBack, 40, kStatusH - 2);
    lv_obj_set_style_pad_all(s_sbBack, 0, 0);
    lv_obj_set_ext_click_area(s_sbBack, 8);
    lv_obj_t *bl = lv_label_create(s_sbBack);
    lv_label_set_text(bl, LV_SYMBOL_LEFT);
    lv_obj_center(bl);
    lv_obj_add_event_cb(s_sbBack, [](lv_event_t *) { s_pendingNav = Nav::Back; }, LV_EVENT_SHORT_CLICKED, nullptr);
    lv_obj_add_event_cb(s_sbBack, [](lv_event_t *) { s_pendingNav = Nav::Home; }, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_add_flag(s_sbBack, LV_OBJ_FLAG_HIDDEN);
#endif
    s_sbCall = sbLabel(bar, "");
    lv_obj_set_flex_grow(s_sbCall, 1);
    s_sbMsg = sbLabel(bar, "");
    lv_obj_set_style_text_color(s_sbMsg, kAmber, 0);
    buildTncIcon(bar);
    buildIsIcon(bar);
    s_sbBt = sbLabel(bar, LV_SYMBOL_BLUETOOTH);
    s_sbGps = sbLabel(bar, LV_SYMBOL_GPS);
    s_sbWifi = sbLabel(bar, LV_SYMBOL_WIFI);
    s_sbBatt = sbLabel(bar, "");
#if !HAS_BATTERY
    lv_obj_add_flag(s_sbBatt, LV_OBJ_FLAG_HIDDEN);   // mains/USB powered
#endif
    s_sbTime = sbLabel(bar, "--:--");
    lv_obj_set_style_text_color(s_sbBt, kGrey, 0);
    lv_obj_set_style_text_color(s_sbGps, kGrey, 0);
    s_rxSeenBle = g_ble.status().rxFrames;
    s_rxSeenLora = g_lora.status().rxFrames;
    s_isRxSeen = aprsIsStatus().rxLines;
    s_sbIsLastTx = aprsIsStatus().txLines;
    static lv_timer_t *blink = nullptr;
    if (!blink) blink = lv_timer_create(rxBlinkTimer, 250, nullptr);
}

void setOpa(void *obj, int32_t v) { lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0); }

// Fade out and back in; repeat = LV_ANIM_REPEAT_INFINITE keeps pulsing until stopped.
void blink(lv_obj_t *obj, uint32_t halfMs, uint32_t repeat) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, setOpa);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_10);
    lv_anim_set_duration(&a, halfMs);
    lv_anim_set_playback_duration(&a, halfMs);
    lv_anim_set_repeat_count(&a, repeat);
    lv_anim_start(&a);
}

void stopBlink(lv_obj_t *obj) {
    lv_anim_delete(obj, setOpa);
    lv_obj_set_style_opa(obj, LV_OPA_COVER, 0);
}

// Unread messages: the envelope pulses until they are read.
void updateMsgIcon() {
    const uint32_t unread = messagesUnread();
    const bool pulsing = lv_anim_get(s_sbMsg, setOpa) != nullptr;
    if (unread) {
        lv_label_set_text_fmt(s_sbMsg, LV_SYMBOL_ENVELOPE " %lu", (unsigned long)unread);
        if (!pulsing) blink(s_sbMsg, 450, LV_ANIM_REPEAT_INFINITE);
    } else {
        lv_label_set_text(s_sbMsg, "");
        if (pulsing) stopBlink(s_sbMsg);
    }
}

// Received packets: blink the radio indicator once, rate-limited.
void isRxBlink() {
    const uint32_t rx = aprsIsStatus().rxLines;
    if (rx == s_isRxSeen) return;
    s_isRxSeen = rx;
    const uint32_t now = lv_tick_get();
    if (s_isRxBlinkMs && now - s_isRxBlinkMs < kRxBlinkGapMs) return;
    if (lv_obj_has_flag(s_sbIs, LV_OBJ_FLAG_HIDDEN) || lv_anim_get(s_sbIs, setOpa)) return;
    s_isRxBlinkMs = now;
    blink(s_sbIs, 150, 0);
}

void rxBlinkTimer(lv_timer_t *) {
    isRxBlink();
    const bool lora = g_config.rfLink == RfLink::Lora;
    const uint32_t rx = lora ? g_lora.status().rxFrames : g_ble.status().rxFrames;
    uint32_t &seen = lora ? s_rxSeenLora : s_rxSeenBle;
    if (rx == seen) return;
    seen = rx;
    const uint32_t now = lv_tick_get();
    if (s_rxBlinkMs && now - s_rxBlinkMs < kRxBlinkGapMs) return;
    lv_obj_t *icon = lora ? s_sbBt : s_sbTnc;
    if (lv_obj_has_flag(icon, LV_OBJ_FLAG_HIDDEN) || lv_anim_get(icon, setOpa)) return;
    s_rxBlinkMs = now;
    blink(icon, 150, 0);
}

void updateStatusBar() {
    lv_label_set_text(s_sbCall, g_config.fullCallsign().c_str());

    const WifiStatus w = wifiStatus();
    lv_color_t wc = kGrey;
    if (w.staConnected) wc = kGreen;
    else if (w.apActive) wc = kAmber;
    lv_obj_set_style_text_color(s_sbWifi, wc, 0);
    lv_label_set_text(s_sbWifi, w.apActive ? LV_SYMBOL_WIFI " AP" : LV_SYMBOL_WIFI);

    const BleStatus tnc = g_ble.status();
    const bool tncUp = g_config.rfLink == RfLink::Ble && tnc.state == BleState::Connected;
    if (tncUp) {
        lv_obj_remove_flag(s_sbTnc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_sbBt, LV_OBJ_FLAG_HIDDEN);
        // Flash the arrow for one status tick after each frame sent to the radio.
        if (tnc.txFrames != s_sbTncLastTx) lv_obj_remove_flag(s_sbTncTx, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_sbTncTx, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_sbTnc, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_sbBt, LV_OBJ_FLAG_HIDDEN);
    }
    s_sbTncLastTx = tnc.txFrames;

    const AprsIsStatus is = aprsIsStatus();
    if (is.connected) {
        lv_obj_remove_flag(s_sbIs, LV_OBJ_FLAG_HIDDEN);
        // Up arrow goes amber for one status tick after each line sent.
        sbArrowColor(s_sbIsUp, is.txLines != s_sbIsLastTx ? kAmber : kGreen);
    } else {
        lv_obj_add_flag(s_sbIs, LV_OBJ_FLAG_HIDDEN);
    }
    s_sbIsLastTx = is.txLines;

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

    updateMsgIcon();

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
        lv_obj_set_style_text_color(s_toast, lv_color_white(), 0);
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

constexpr const char *kStar = "\xE2\x98\x85";       // U+2605, in font_star_14
constexpr const char *kStarOpen = "\xE2\x98\x86";   // U+2606

lv_obj_t *makeStar(lv_obj_t *parent, bool on) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, &font_star_14, 0);
    lv_obj_set_style_text_color(l, on ? kAmber : kGrey, 0);
    lv_label_set_text(l, on ? kStar : kStarOpen);
    return l;
}

// Puts a favorite star in front of a list item's label.
void markFavorite(lv_obj_t *item) { lv_obj_move_to_index(makeStar(item, true), 0); }

// One-line list item: [star] text ........ right (grey).
lv_obj_t *addRow(lv_obj_t *list, lv_group_t *grp, bool fav, const char *text, const char *right,
                 lv_event_cb_t cb, void *ud) {
    lv_obj_t *b = lv_list_add_button(list, nullptr, nullptr);
    lv_obj_set_style_pad_ver(b, 6, 0);
    lv_obj_set_style_pad_column(b, 6, 0);
    if (fav) makeStar(b, true);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_label_set_text(l, text);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_t *r = lv_label_create(b);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(r, kDimText, 0);
    lv_label_set_text(r, right);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
    lv_group_add_obj(grp, b);
    return b;
}

// "Favorite" toggle button for the callsign in buf (which must outlive the button).
lv_obj_t *makeFavButton(lv_obj_t *parent, lv_group_t *grp, char *call) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_set_style_pad_ver(b, 4, 0);
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_width(b, LV_SIZE_CONTENT);
    const bool on = favIs(call);
    makeStar(b, on);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, on ? "Favorite" : "Add to favorites");
    lv_obj_add_event_cb(b, [](lv_event_t *e) {
        lv_obj_t *btn = (lv_obj_t *)lv_event_get_current_target(e);
        const bool now = favToggle((const char *)lv_event_get_user_data(e));
        lv_obj_t *star = lv_obj_get_child(btn, 0);
        lv_label_set_text(star, now ? kStar : kStarOpen);
        lv_obj_set_style_text_color(star, now ? kAmber : kGrey, 0);
        lv_label_set_text(lv_obj_get_child(btn, 1), now ? "Favorite" : "Add to favorites");
        toast(now ? "Added to favorites" : "Removed from favorites");
    }, LV_EVENT_CLICKED, call);
    lv_obj_add_flag(b, LV_OBJ_FLAG_USER_1);   // found by Alt+F
    lv_group_add_obj(grp, b);
    return b;
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
        if (e->tx) lv_obj_set_style_text_color(b, kOutText, 0);
        else if (e->viaIs) lv_obj_set_style_text_color(b, kDimText, 0);
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
    makeFavButton(scr, grp, s_stDetailCall);
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
    uint32_t favGen;
    uint32_t lastBuildS;
    char calls[60][10];
} s_sta;

void refreshStations() {
    if (!s_sta.list) return;
    // Rebuild on new data, and every 30 s so the ages stay current.
    if (s_sta.gen == g_dataGen && s_sta.favGen == g_favGen && uptimeS() - s_sta.lastBuildS < 30) return;
    s_sta.gen = g_dataGen;
    s_sta.favGen = g_favGen;
    s_sta.lastBuildS = uptimeS();

    char keep[10] = "";
    const int fi = focusedIndex(s_sta.list);
    if (fi >= 0) strlcpy(keep, s_sta.calls[fi], sizeof(keep));

    lv_obj_clean(s_sta.list);
    lv_obj_t *refocus = nullptr;
    const size_t total = stationDbCount();
    size_t n = 0;
    // Favorites first, then everyone else; each most recently heard first.
    for (int pass = 0; pass < 2; pass++) {
        for (size_t i = 0; i < total && n < 60; i++) {
            const Station *s = stationDbAt(i);
            const bool fav = favIs(s->call);
            if (fav != (pass == 0)) continue;
            strlcpy(s_sta.calls[n], s->call, sizeof(s_sta.calls[n]));
            lv_obj_t *b = addRow(s_sta.list, s_sta.grp, fav, s->call, ageStr(s->lastHeardS).c_str(), [](lv_event_t *e) {
                const size_t idx = (size_t)(uintptr_t)lv_event_get_user_data(e);
                openStationDetail(s_sta.calls[idx]);
            }, (void *)(uintptr_t)n);
            if (keep[0] && !strcmp(keep, s->call)) refocus = b;
            n++;
        }
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
    oskAttach(ta);
    return ta;
}

// ── Quick messages ───────────────────────────────────────────────────────────

struct {
    lv_obj_t *bg, *target;
    lv_group_t *grp;
} s_qm;
volatile int s_pendingQuick = -2;   // -2 none, -1 cancel, else the picked index

void openQuickPicker(lv_obj_t *target) {
    if (s_qm.bg) return;
    oskHide();
    s_qm.target = target;
    s_qm.bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_qm.bg, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - kStatusH);
    lv_obj_align(s_qm.bg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(s_qm.bg, 0, 0);
    lv_obj_set_style_border_width(s_qm.bg, 0, 0);
    lv_obj_set_style_bg_color(s_qm.bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_qm.bg, LV_OPA_70, 0);
    lv_obj_clear_flag(s_qm.bg, LV_OBJ_FLAG_SCROLLABLE);
    // Tapping the backdrop cancels.
    lv_obj_add_event_cb(s_qm.bg, [](lv_event_t *e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) s_pendingQuick = -1;
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *box = lv_obj_create(s_qm.bg);
    lv_obj_set_size(box, lv_pct(94), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(box, lv_pct(96), 0);
    lv_obj_center(box);
    lv_obj_set_style_pad_all(box, 6, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 4, 0);
    lv_obj_t *title = lv_label_create(box);
    lv_label_set_text(title, LV_SYMBOL_LIST " Quick messages");

    s_qm.grp = lv_group_create();
    lv_obj_t *list = lv_list_create(box);
    lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(list, 170, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    for (size_t i = 0; i < AppConfig::kQuickMsgCount; i++) {
        if (!g_config.quickMsgs[i].length()) continue;
        addWrapItem(list, s_qm.grp, g_config.quickMsgs[i].c_str(),
                    [](lv_event_t *e) { s_pendingQuick = (int)(uintptr_t)lv_event_get_user_data(e); },
                    (void *)(uintptr_t)i, &lv_font_montserrat_14);
    }
    if (!lv_obj_get_child_count(list))
        makeNote(box, "No quick messages set. Add them under Settings > Quick messages or in the web config.");
    lv_obj_t *cancel = lv_button_create(box);
    lv_label_set_text(lv_label_create(cancel), "Cancel");
    lv_obj_add_event_cb(cancel, [](lv_event_t *) { s_pendingQuick = -1; }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(s_qm.grp, cancel);

    lv_group_set_default(s_qm.grp);
    lv_indev_set_group(keyboardIndev(), s_qm.grp);
}

// Called from uiLoop, never from an event callback (it deletes objects).
void closeQuickPicker(int pick) {
    if (!s_qm.bg) return;
    lv_obj_delete(s_qm.bg);
    lv_group_delete(s_qm.grp);
    lv_group_t *g = s_stack[s_depth - 1].grp;
    lv_group_set_default(g);
    lv_indev_set_group(keyboardIndev(), g);
    if (pick >= 0 && pick < (int)AppConfig::kQuickMsgCount) lv_textarea_set_text(s_qm.target, g_config.quickMsgs[pick].c_str());
    lv_group_focus_obj(s_qm.target);
    s_qm = {};
}

// Text input with a quick-message button: on its left on touch boards, on its
// right on keypad-only ones, where Right at the end of the text moves to it.
lv_obj_t *makeMessageInput(lv_obj_t *parent, lv_group_t *grp, const char *placeholder) {
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(row, 0, 0);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    auto makeBtn = [&]() {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_style_pad_hor(b, 8, 0);
        lv_label_set_text(lv_label_create(b), LV_SYMBOL_LIST);
        lv_group_add_obj(grp, b);
        return b;
    };
#if HAS_TOUCH
    lv_obj_t *btn = makeBtn();
#endif
    lv_obj_t *ta = makeInput(row, grp, placeholder, 67);
    lv_obj_set_width(ta, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(ta, 1);
#if !HAS_TOUCH
    lv_obj_t *btn = makeBtn();
    // Preprocess: decide before the textarea moves its cursor.
    lv_obj_add_event_cb(ta, [](lv_event_t *e) {
        if (lv_event_get_key(e) != LV_KEY_RIGHT) return;
        lv_obj_t *t = (lv_obj_t *)lv_event_get_current_target(e);
        if (lv_textarea_get_cursor_pos(t) < lv_text_get_encoded_length(lv_textarea_get_text(t))) return;
        lv_event_stop_processing(e);
        lv_group_focus_obj((lv_obj_t *)lv_event_get_user_data(e));
    }, (lv_event_code_t)(LV_EVENT_KEY | LV_EVENT_PREPROCESS), btn);
#endif
    lv_obj_add_event_cb(btn, [](lv_event_t *e) { openQuickPicker((lv_obj_t *)lv_event_get_user_data(e)); },
                        LV_EVENT_CLICKED, ta);
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

// ── Message details ──────────────────────────────────────────────────────────

// What an APRS-IS path's q construct says about who put the packet on the net.
String isOrigin(const char *path) {
    const char *q = strstr(path, "qA");
    if (!q || !q[2] || q[3] != ',') return "";
    const String by = q + 4;
    switch (q[2]) {
        case 'R': case 'O': case 'r': return "Heard on RF by igate " + by;
        case 'C': case 'X': case 'U': return "Sent straight to server " + by;
        case 'S': return "Relayed by server " + by;
        default: return "Entered APRS-IS at " + by;
    }
}

void openMessageDetail(const ChatMsg &m) {
    String t = String(m.outgoing ? "To: " : "From: ") + m.peer + "\n";
    if (m.epoch) {
        struct tm tm;
        localtime_r(&m.epoch, &tm);
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
        t += String(m.outgoing ? "Sent: " : "Received: ") + buf + "\n";
    } else if (m.uptimeS) {
        t += String(m.outgoing ? "Sent " : "Received ") + ageStr(m.uptimeS) + " ago\n";
    }
    if (m.outgoing) {
        t += String("State: ") + msgStateName(m.state) + ", " + String(m.tries) + (m.tries == 1 ? " try\n" : " tries\n");
    } else {
        t += String("Arrived via: ") + msgViaName(m.via) + "\n";
        if (m.via == MsgVia::AprsIs) {
            const String o = isOrigin(m.path);
            if (o.length()) t += o + "\n";
        } else if (m.via != MsgVia::Unknown) {
            if (m.relay[0]) t += String("Gated from APRS-IS to RF by ") + m.relay + "\n";
            t += strchr(m.path, '*') ? "Digipeated\n" : "Heard direct\n";
        }
        if (m.path[0]) t += String("Path: ") + m.path + "\n";
    }
    t += m.msgId[0] ? String("Message ID: ") + m.msgId + (m.outgoing ? "\n" : " (acked by us)\n")
                    : String("No message ID (no ack requested)\n");
    t += String("\n") + m.text;

    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_ENVELOPE " Message details", &grp);
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
    lv_obj_t *l = lv_label_create(box);
    lv_obj_set_width(l, lv_pct(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_label_set_text(l, t.c_str());
    pushScreen(scr, grp);
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
            for (size_t i = 0; i < messagesCount(); i++) {
                const ChatMsg *m = messagesAt(i);
                if (m->seq != seq) continue;
                // A failed send retries; anything else shows its details.
                if (m->state == MsgState::Failed) messagesRetry(seq, err);
                else openMessageDetail(*m);
                break;
            }
        }, (void *)(uintptr_t)n);
        if (m->outgoing) lv_obj_set_style_text_color(b, kOutText, 0);
        if (++n == 150) break;
    }
    // Keep the input row (input and quick-message button) last in the focus order.
    lv_obj_t *row = lv_obj_get_parent(s_thr.input);
    lv_obj_t *prevFocus = lv_group_get_focused(s_thr.grp);
    const bool rowFocused = prevFocus && lv_obj_get_parent(prevFocus) == row;
    for (uint32_t i = 0; i < lv_obj_get_child_count(row); i++) {
        lv_obj_t *c = lv_obj_get_child(row, i);
        lv_group_remove_obj(c);
        lv_group_add_obj(s_thr.grp, c);
    }
    if (rowFocused) lv_group_focus_obj(prevFocus);
    else if (inputFocused || !lv_group_get_focused(s_thr.grp)) lv_group_focus_obj(s_thr.input);
    if (n) lv_obj_scroll_to_view(lv_obj_get_child(s_thr.list, n - 1), LV_ANIM_OFF);
}

void openThread(const char *peer) {
    strlcpy(s_thr.peer, peer, sizeof(s_thr.peer));
    lv_group_t *grp;
    lv_obj_t *scr = newScreen((String(LV_SYMBOL_ENVELOPE " ") + peer).c_str(), &grp);
    s_thr.grp = grp;
    s_thr.gen = 0;
    makeFavButton(scr, grp, s_thr.peer);
    s_thr.list = makeList(scr);
    s_thr.input = makeMessageInput(scr, grp, "Type, Enter to send");
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
    s_new.text = makeMessageInput(scr, grp, "Message");
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

// ── Choice modal ─────────────────────────────────────────────────────────────
// A title, one button per option and Cancel, and optionally an On/Off toggle
// (s_choiceToggle, Off to start). The pick is acted on from uiLoop, after the
// modal is gone; Back or Cancel picks nothing.

typedef void (*ChoiceFn)(int pick);
struct ChoiceOpt {
    const char *label;
    bool danger;
};
struct {
    lv_obj_t *bg;
    lv_group_t *grp;
    ChoiceFn fn;
} s_choice;
volatile int s_pendingChoice = -2;   // -2 none, -1 cancel, else the option index
bool s_choiceToggle;
const char *s_choiceToggleLabel;

void setToggleText(lv_obj_t *b) {
    lv_label_set_text(lv_obj_get_child(b, 0),
                      (String(s_choiceToggleLabel) + (s_choiceToggle ? ": On" : ": Off")).c_str());
}

// focus: the option focused first; -1 focuses Cancel (for confirmations).
void openChoice(const char *title, const char *note, const ChoiceOpt *opts, int n, ChoiceFn fn, int focus = 0,
                const char *toggle = nullptr) {
    if (s_choice.bg) return;
    oskHide();
    s_choice.fn = fn;
    s_choiceToggle = false;
    s_choiceToggleLabel = toggle;
    s_choice.bg = lv_obj_create(lv_layer_top());
    lv_obj_set_size(s_choice.bg, lv_pct(100), lv_display_get_vertical_resolution(nullptr) - kStatusH);
    lv_obj_align(s_choice.bg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_radius(s_choice.bg, 0, 0);
    lv_obj_set_style_border_width(s_choice.bg, 0, 0);
    lv_obj_set_style_bg_color(s_choice.bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_choice.bg, LV_OPA_70, 0);
    lv_obj_clear_flag(s_choice.bg, LV_OBJ_FLAG_SCROLLABLE);
    // Tapping the backdrop cancels.
    lv_obj_add_event_cb(s_choice.bg, [](lv_event_t *e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) s_pendingChoice = -1;
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *box = lv_obj_create(s_choice.bg);
    lv_obj_set_size(box, lv_pct(80), LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(box, lv_pct(96), 0);
    lv_obj_center(box);
    lv_obj_set_style_pad_all(box, 8, 0);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(box, 6, 0);
    lv_obj_t *t = lv_label_create(box);
    lv_obj_set_width(t, lv_pct(100));
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_label_set_text(t, title);
    if (note) makeNote(box, note);

    s_choice.grp = lv_group_create();
    if (toggle) {
        lv_obj_t *b = lv_button_create(box);
        lv_obj_set_width(b, lv_pct(100));
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_width(l, lv_pct(100));
        lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
        setToggleText(b);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            s_choiceToggle = !s_choiceToggle;
            setToggleText((lv_obj_t *)lv_event_get_target(e));
        }, LV_EVENT_CLICKED, nullptr);
        lv_group_add_obj(s_choice.grp, b);
    }
    lv_obj_t *focusBtn = nullptr;
    for (int i = 0; i <= n; i++) {
        lv_obj_t *b = lv_button_create(box);
        lv_obj_set_width(b, lv_pct(100));
        lv_label_set_text(lv_label_create(b), i < n ? opts[i].label : "Cancel");
        if (i < n && opts[i].danger) lv_obj_set_style_bg_color(b, lv_palette_main(LV_PALETTE_RED), 0);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            s_pendingChoice = (int)(intptr_t)lv_event_get_user_data(e);
        }, LV_EVENT_CLICKED, (void *)(intptr_t)(i < n ? i : -1));
        lv_group_add_obj(s_choice.grp, b);
        if (i == (focus < 0 ? n : focus)) focusBtn = b;
    }
    lv_group_set_default(s_choice.grp);
    lv_indev_set_group(keyboardIndev(), s_choice.grp);
    if (focusBtn) lv_group_focus_obj(focusBtn);
}

// Called from uiLoop, never from an event callback (it deletes objects).
void closeChoice(int pick) {
    if (!s_choice.bg) return;
    const ChoiceFn fn = s_choice.fn;
    lv_obj_delete(s_choice.bg);
    lv_group_delete(s_choice.grp);
    s_choice = {};
    lv_group_t *g = s_stack[s_depth - 1].grp;
    lv_group_set_default(g);
    lv_indev_set_group(keyboardIndev(), g);
    if (pick >= 0 && fn) fn(pick);
}

// ── Messages ─────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *list;
    lv_group_t *grp;
    uint32_t gen;
    uint32_t favGen;
    uint32_t lastBuildS;
    char peers[40][10];
    char picked[10];   // the conversation a long press opened the menu for
    char refocus[10];  // after a star toggle moved it, focus this conversation
} s_msgs;

void openThreadMenu(size_t idx) {
    strlcpy(s_msgs.picked, s_msgs.peers[idx], sizeof(s_msgs.picked));
    static const ChoiceOpt kOpts[] = {{LV_SYMBOL_OK " Mark as read", false}, {LV_SYMBOL_TRASH " Delete", true}};
    openChoice(s_msgs.picked, nullptr, kOpts, 2, [](int pick) {
        if (pick == 0) {
            messagesMarkRead(s_msgs.picked);
        } else {
            messagesDeleteThread(s_msgs.picked);
            toast(String("Deleted conversation with ") + s_msgs.picked);
        }
        updateMsgIcon();
    });
}

void confirmDeleteAll() {
    static const ChoiceOpt kOpts[] = {{LV_SYMBOL_TRASH " Delete all", true}};
    openChoice("Delete all messages?", "Conversations are removed from the SD card too. This can't be undone.",
               kOpts, 1, [](int) {
                   if (s_choiceToggle) {
                       messagesDeleteAll();
                       toast("All messages deleted");
                   } else {
                       for (const ChatThread &t : messagesThreads())
                           if (!favIs(t.peer.c_str())) messagesDeleteThread(t.peer);
                       toast("Messages deleted; starred kept");
                   }
                   updateMsgIcon();
               }, -1, "Include starred conversations");
}

void refreshMessages() {
    if (!s_msgs.list) return;
    if (s_msgs.gen == g_msgGen && s_msgs.favGen == g_favGen && uptimeS() - s_msgs.lastBuildS < 30) return;
    s_msgs.gen = g_msgGen;
    s_msgs.favGen = g_favGen;
    s_msgs.lastBuildS = uptimeS();
    const int fi = focusedIndex(s_msgs.list);
    lv_obj_t *refocusObj = nullptr;
    lv_obj_clean(s_msgs.list);
    auto threads = messagesThreads();
    if (!threads.empty()) {
        lv_obj_t *b = addItem(s_msgs.list, s_msgs.grp, LV_SYMBOL_OK, "Mark all as read", [](lv_event_t *) {
            messagesMarkAllRead();
            updateMsgIcon();
        });
        if (!messagesUnread()) lv_obj_add_state(b, LV_STATE_DISABLED);
        addItem(s_msgs.list, s_msgs.grp, LV_SYMBOL_TRASH, "Delete all messages", [](lv_event_t *) { confirmDeleteAll(); });
    }
    addItem(s_msgs.list, s_msgs.grp, LV_SYMBOL_EDIT, "New message", [](lv_event_t *) { openNewMessage(); });
    std::stable_partition(threads.begin(), threads.end(),
                          [](const ChatThread &t) { return favIs(t.peer.c_str()); });
    size_t n = 0;
    for (const ChatThread &t : threads) {
        strlcpy(s_msgs.peers[n], t.peer.c_str(), sizeof(s_msgs.peers[n]));
        String line = t.peer;
        if (t.unread) line += "  (" + String(t.unread) + " new)";
        if (t.lastUptimeS) line += "  " + ageStr(t.lastUptimeS);
        line += "\n" + String(t.lastOutgoing ? "You: " : "") + t.lastText;
        // Short click opens it, so a long press (the menu) doesn't also open it.
        lv_obj_t *b = addWrapItem(s_msgs.list, s_msgs.grp, line.c_str(), nullptr, nullptr);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            openThread(s_msgs.peers[(size_t)(uintptr_t)lv_event_get_user_data(e)]);
        }, LV_EVENT_SHORT_CLICKED, (void *)(uintptr_t)n);
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            openThreadMenu((size_t)(uintptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_LONG_PRESSED, (void *)(uintptr_t)n);
        // Keys can't be held, so Right (or Space, for the Pager's wheel) opens it there.
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            const uint32_t k = lv_event_get_key(e);
            if (k == LV_KEY_RIGHT || k == ' ') openThreadMenu((size_t)(uintptr_t)lv_event_get_user_data(e));
        }, LV_EVENT_KEY, (void *)(uintptr_t)n);
        lv_obj_set_user_data(b, (void *)(uintptr_t)(n + 1));
        if (t.unread) lv_obj_set_style_text_color(b, kAmber, 0);
        if (favIs(t.peer.c_str())) markFavorite(b);
        if (s_msgs.refocus[0] && t.peer.equalsIgnoreCase(s_msgs.refocus)) refocusObj = b;
        if (++n == 40) break;
    }
    s_msgs.refocus[0] = 0;
    if (!n) makeNote(s_msgs.list, "No conversations yet.");
    const uint32_t cnt = lv_obj_get_child_count(s_msgs.list);
    if (refocusObj) lv_group_focus_obj(refocusObj);
    else if (fi >= 0 && (uint32_t)fi < cnt) lv_group_focus_obj(lv_obj_get_child(s_msgs.list, fi));
}

lv_obj_t *findFavButton(lv_obj_t *o, int depth) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1)) return o;
    if (depth == 0) return nullptr;
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++)
        if (lv_obj_t *f = findFavButton(lv_obj_get_child(o, i), depth - 1)) return f;
    return nullptr;
}

// Alt+F: stars or unstars the conversation focused in Messages, or whatever a
// screen with a favorite button (a conversation, a station) is showing.
void toggleFavoriteKey() {
    const Screen &top = s_stack[s_depth - 1];
    if (s_msgs.list && top.grp == s_msgs.grp) {
        lv_obj_t *f = lv_group_get_focused(s_msgs.grp);
        const uintptr_t idx = f && lv_obj_get_parent(f) == s_msgs.list ? (uintptr_t)lv_obj_get_user_data(f) : 0;
        if (!idx) return;
        const char *peer = s_msgs.peers[idx - 1];
        const bool now = favToggle(peer);
        strlcpy(s_msgs.refocus, peer, sizeof(s_msgs.refocus));
        refreshMessages();
        toast(String(now ? "Starred " : "Unstarred ") + peer);
        return;
    }
    if (lv_obj_t *b = findFavButton(top.scr, 3)) lv_obj_send_event(b, LV_EVENT_CLICKED, nullptr);
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
    updateMsgIcon();
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

// ── Wi-Fi ────────────────────────────────────────────────────────────────────

struct {
    lv_obj_t *info, *list;
    lv_group_t *grp;
    std::vector<WifiNetwork> shown;
    bool wasScanning;
} s_wifi;

struct {
    lv_obj_t *ssid, *pass;
    String fixedSsid;   // empty: the user types the name (hidden network)
} s_wifiJoin;

void wifiJoinConnect() {
    const String ssid = s_wifiJoin.ssid ? String(lv_textarea_get_text(s_wifiJoin.ssid)) : s_wifiJoin.fixedSsid;
    const String pass = s_wifiJoin.pass ? String(lv_textarea_get_text(s_wifiJoin.pass)) : String();
    String err;
    if (!wifiConnectTo(ssid, pass, err)) {
        toast(err);
        return;
    }
    toast(LV_SYMBOL_WIFI " Connecting to " + ssid + "...");
    s_pendingNav = Nav::Back;
}

// Password (and, for hidden networks, name) entry. ssid empty = hidden network.
void openWifiJoin(const String &ssid) {
    s_wifiJoin = {};
    s_wifiJoin.fixedSsid = ssid;
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(ssid.length() ? (String(LV_SYMBOL_WIFI " ") + ssid).c_str() : LV_SYMBOL_WIFI " Other network", &grp);
    if (!ssid.length()) s_wifiJoin.ssid = makeInput(scr, grp, "Network name (SSID)", 32);
    s_wifiJoin.pass = makeInput(scr, grp, "Password", 64);
    lv_textarea_set_password_mode(s_wifiJoin.pass, true);
    if (s_wifiJoin.ssid)
        lv_obj_add_event_cb(s_wifiJoin.ssid, [](lv_event_t *) { lv_group_focus_obj(s_wifiJoin.pass); }, LV_EVENT_READY, nullptr);
    lv_obj_add_event_cb(s_wifiJoin.pass, [](lv_event_t *) { wifiJoinConnect(); }, LV_EVENT_READY, nullptr);

    lv_obj_t *show = lv_button_create(scr);
    lv_label_set_text(lv_label_create(show), LV_SYMBOL_EYE_OPEN " Show password");
    lv_obj_add_event_cb(show, [](lv_event_t *e) {
        const bool hidden = !lv_textarea_get_password_mode(s_wifiJoin.pass);
        lv_textarea_set_password_mode(s_wifiJoin.pass, hidden);
        lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 0),
                          hidden ? LV_SYMBOL_EYE_OPEN " Show password" : LV_SYMBOL_EYE_CLOSE " Hide password");
    }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, show);

    lv_obj_t *b = lv_button_create(scr);
    lv_label_set_text(lv_label_create(b), LV_SYMBOL_OK " Connect");
    lv_obj_add_event_cb(b, [](lv_event_t *) { wifiJoinConnect(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, b);

    const bool saved = ssid.length() && ssid == g_config.wifiSsid && g_config.wifiPassword.length();
    makeNote(scr, saved ? "Leave the password blank to keep the saved one."
                        : "Leave the password blank for an open network.");
    pushScreen(scr, grp);
    lv_group_focus_obj(s_wifiJoin.ssid ? s_wifiJoin.ssid : s_wifiJoin.pass);
}

void wifiPick(size_t idx) {
    if (idx >= s_wifi.shown.size()) return;
    const WifiNetwork w = s_wifi.shown[idx];
    if (w.secure) {
        openWifiJoin(w.ssid);
        return;
    }
    String err;
    if (wifiConnectTo(w.ssid, "", err)) toast(LV_SYMBOL_WIFI " Connecting to " + w.ssid + "...");
    else toast(err);
}

void rebuildWifiList(bool scanning) {
    lv_obj_clean(s_wifi.list);
    if (scanning) {
        makeNote(s_wifi.list, "Scanning...");
    } else {
        for (size_t i = 0; i < s_wifi.shown.size(); i++) {
            const WifiNetwork &w = s_wifi.shown[i];
            const bool current = w.ssid == g_config.wifiSsid;
            const String name = (current ? String(LV_SYMBOL_OK " ") : String()) + w.ssid;
            const String right = String(w.rssi) + " dBm" + (w.secure ? "" : " open");
            addRow(s_wifi.list, s_wifi.grp, false, name.c_str(), right.c_str(), [](lv_event_t *e) {
                wifiPick((size_t)(uintptr_t)lv_event_get_user_data(e));
            }, (void *)(uintptr_t)i);
        }
        if (s_wifi.shown.empty()) makeNote(s_wifi.list, "No networks found.");
    }
    addItem(s_wifi.list, s_wifi.grp, LV_SYMBOL_EDIT, "Other network...", [](lv_event_t *) { openWifiJoin(""); });
}

void refreshWifi() {
    if (!s_wifi.info) return;
    const WifiStatus w = wifiStatus();
    String t;
    t += "Network: " + (w.staConfigured ? w.staSsid : String("(not set)")) + "\n";
    t += w.staConnected   ? "Connected  IP " + w.staIp + "  " + String(w.rssi) + " dBm\n"
         : w.staConfigured ? String("Connecting...\n")
                           : String("Not connected\n");
    t += "Web: http://" + w.hostname + ".local\n";
    if (w.apActive) {
        t += "Setup AP: " + w.apSsid + " (" + apReasonStr(w.apReason) + ")\n";
        t += "Open http://" + w.apIp + "  clients: " + String(w.apClients);
    } else {
        t += "Setup AP: off";
    }
    lv_label_set_text(s_wifi.info, t.c_str());

    // Show results once the scan finishes, unless the user is already moving through them.
    const bool scanning = wifiScanRunning();
    if (s_wifi.wasScanning && !scanning && focusedIndex(s_wifi.list) < 0) {
        wifiScanResults(s_wifi.shown);
        rebuildWifiList(false);
    }
    s_wifi.wasScanning = scanning;
}

void wifiScanUi() {
    s_wifi.shown.clear();
    if (!wifiScanStart()) {
        toast("Wi-Fi scan failed, try again");
        return;
    }
    s_wifi.wasScanning = true;
    rebuildWifiList(true);
}

// scan: start a network scan right away (not when opened for the setup AP,
// since scanning briefly disturbs clients on it).
void openWifi(bool scan) {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_WIFI " Wi-Fi", &grp);
    s_wifi.grp = grp;
    s_wifi.shown.clear();
    s_wifi.wasScanning = false;
    s_wifi.info = lv_label_create(scr);
    lv_label_set_long_mode(s_wifi.info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_wifi.info, lv_pct(100));
    lv_obj_set_style_text_font(s_wifi.info, &lv_font_montserrat_12, 0);
    lv_obj_t *b = lv_button_create(scr);
    lv_label_set_text(lv_label_create(b), LV_SYMBOL_REFRESH " Scan for networks");
    lv_obj_add_event_cb(b, [](lv_event_t *) { wifiScanUi(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, b);
    s_wifi.list = makeList(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_wifi.info = nullptr; }, LV_EVENT_DELETE, nullptr);
    if (scan) wifiScanUi();
    else rebuildWifiList(false);
    pushScreen(scr, grp, refreshWifi);
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
    // Padding leaves room for the focus outline and glow, which the row would otherwise clip.
    lv_obj_set_style_pad_all(row, 5, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(row, 6, 0);
    auto btn = [&](const char *txt, lv_event_cb_t cb) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_SIZE_CONTENT);
        lv_obj_set_style_pad_hor(b, 4, 0);
        lv_obj_set_style_pad_ver(b, 4, 0);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, txt);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        lv_obj_set_width(l, lv_pct(100));
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
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

// ── Paired radios (BLE bonds) ────────────────────────────────────────────────

struct {
    lv_obj_t *list;
    lv_group_t *grp;
    std::vector<BleBond> shown;
    uint8_t rebuildIn;   // ticks until the list re-reads the bond store
} s_bonds;

void rebuildBonds() {
    if (!s_bonds.list) return;
    s_bonds.shown = g_ble.bonds();
    lv_obj_clean(s_bonds.list);
    String saved = g_config.tncAddress;
    saved.toLowerCase();
    for (size_t i = 0; i < s_bonds.shown.size(); i++) {
        const auto &b = s_bonds.shown[i];
        String line = LV_SYMBOL_TRASH " ";
        if (saved.length() && b.address.equalsIgnoreCase(saved) && g_config.tncName.length()) {
            line += g_config.tncName + "  " + b.address;
        } else {
            line += b.address;
        }
        addWrapItem(s_bonds.list, s_bonds.grp, line.c_str(), [](lv_event_t *e) {
            const size_t idx = (uintptr_t)lv_event_get_user_data(e);
            if (idx >= s_bonds.shown.size()) return;
            g_ble.forgetBond(s_bonds.shown[idx].address, s_bonds.shown[idx].addressType);
            toast("Pairing deleted");
            s_bonds.rebuildIn = 1;
        }, (void *)(uintptr_t)i);
    }
    if (s_bonds.shown.empty()) addWrapItem(s_bonds.list, s_bonds.grp, "No paired radios", nullptr, nullptr);
    if (s_bonds.shown.size() > 1) {
        addWrapItem(s_bonds.list, s_bonds.grp, LV_SYMBOL_TRASH " Forget all pairings", [](lv_event_t *) {
            g_ble.forgetBond();
            toast("All pairings deleted");
            s_bonds.rebuildIn = 1;
        }, nullptr);
    }
    if (g_config.tncAddress.length()) {
        const String l = String(LV_SYMBOL_CLOSE " Remove saved radio (") +
                         (g_config.tncName.length() ? g_config.tncName : g_config.tncAddress) + ")";
        addWrapItem(s_bonds.list, s_bonds.grp, l.c_str(), [](lv_event_t *) {
            g_ble.forgetBond(g_config.tncAddress, g_config.tncAddressType);
            g_config.tncAddress = "";
            g_config.tncName = "";
            configSave();
            g_ble.reconfigure();
            toast("Saved radio removed");
            s_bonds.rebuildIn = 1;
        }, nullptr);
    }
    if (lv_obj_get_child_count(s_bonds.list)) lv_group_focus_obj(lv_obj_get_child(s_bonds.list, 0));
}

void refreshBonds() {
    // Deletion runs on the BLE task; re-read the store on the next tick.
    if (s_bonds.rebuildIn && --s_bonds.rebuildIn == 0) rebuildBonds();
}

void openBonds() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_BLUETOOTH " Paired radios", &grp);
    s_bonds.grp = grp;
    s_bonds.rebuildIn = 0;
    makeNote(scr, "Select one to delete it. Also remove this device from the radio's Bluetooth list, then Connect to pair again.");
    s_bonds.list = makeList(scr);
    lv_obj_add_event_cb(scr, [](lv_event_t *) { s_bonds.list = nullptr; }, LV_EVENT_DELETE, nullptr);
    rebuildBonds();
    pushScreen(scr, grp, refreshBonds);
}

// ── Backup ───────────────────────────────────────────────────────────────────

void backupToSdNow() {
    String name, err;
    if (backupSaveToSd(false, name, err))
        openPlaceholder("Backup", ("Saved to SD card:\n/platy/backups/" + name +
                                   "\n\nPasswords are not included. Restore from the web config (Device tab).")
                                      .c_str());
    else openPlaceholder("Backup", ("Backup failed: " + err).c_str());
}

// ── Restore prompt (new flash, backups on the SD card) ──────────────────────

struct {
    std::vector<String> names;
    uint32_t rebootAtMs;
} s_restore;

void restoreFromSd(size_t idx) {
    if (idx >= s_restore.names.size() || s_restore.rebootAtMs) return;
    const String &name = s_restore.names[idx];
    JsonDocument doc;
    String err;
    if (!backupReadSd(name, doc, err) || !backupRestore(doc.as<JsonObjectConst>(), err)) {
        toast("Restore failed: " + err, 6000);
        return;
    }
    Serial.printf("[backup] restored %s; rebooting\n", name.c_str());
    toast("Restored " + name + ". Rebooting...", 5000);
    s_restore.rebootAtMs = millis() + 1500;
}

void openRestorePrompt(const std::vector<SdBackupInfo> &files) {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_SD_CARD " Restore settings?", &grp);
    makeNote(scr, "This device has no settings yet, but the SD card has backups. Pick one to restore "
                  "(the device reboots), or start fresh. Passwords are only restored if the backup includes them.");
    lv_obj_t *list = makeList(scr);
    s_restore.names.clear();
    lv_obj_t *first = nullptr;
    for (const auto &f : files) {
        String line = f.name;
        JsonDocument doc;
        String err;
        if (backupReadSd(f.name, doc, err)) {
            const char *call = doc["callsign"] | "";
            if (*call) line += String("\n") + call;
        }
        lv_obj_t *b = addWrapItem(list, grp, line.c_str(), [](lv_event_t *e) {
            restoreFromSd((size_t)(uintptr_t)lv_event_get_user_data(e));
        }, (void *)(uintptr_t)s_restore.names.size(), &lv_font_montserrat_14);
        if (!first) first = b;
        s_restore.names.push_back(f.name);
    }
    addItem(list, grp, LV_SYMBOL_CLOSE, "Start fresh (don't ask again)", [](lv_event_t *) {
        if (s_restore.rebootAtMs) return;
        configSave();   // a saved config means this prompt won't come back
        s_pendingNav = Nav::Back;
    });
    if (first) lv_group_focus_obj(first);
    pushScreen(scr, grp);
}

// ── Settings ─────────────────────────────────────────────────────────────────

// ── Splash ───────────────────────────────────────────────────────────────────

extern "C" const lv_image_dsc_t splash_img;   // tools/gen_splash.py

// The mascot over a deep-teal gradient, the name, a tagline and, unless null, a
// footer line.
// Laid out for 320x240 (every board here). Not part of any group: nothing on it
// takes focus.
lv_obj_t *buildSplash(const char *footer) {
    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x15506A), 0);
    lv_obj_set_style_bg_grad_color(scr, lv_color_hex(0x0A2230), 0);
    lv_obj_set_style_bg_grad_dir(scr, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // The About screen's footer (version, heap, battery) sits at the bottom,
    // and the status bar covers the top while it is open.
    const int vres = lv_display_get_vertical_resolution(nullptr);
    int areaTop = 0, areaBottom = vres;
    if (footer) {
        lv_obj_t *foot = lv_label_create(scr);
        lv_label_set_text(foot, footer);
        lv_obj_set_style_text_font(foot, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(foot, lv_color_hex(0x9FC7D6), 0);
        lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_update_layout(foot);
        areaTop = kStatusH;
        areaBottom = lv_obj_get_y(foot) - 4;
    }

    // Image (132 tall) and title, plus the tagline when there's room (not on
    // About), make one block centred in the free area. The image is 240x132;
    // its speech bubble spans (0..88, 2..34) within it, top left, coming out
    // of the bill.
    const int blockH = footer ? 162 : 180;
    const int top = areaTop + (areaBottom - areaTop - blockH) / 2;
    lv_obj_t *img = lv_image_create(scr);
    lv_image_set_src(img, &splash_img);
    lv_obj_set_pos(img, 40, top);

    lv_obj_t *cq = lv_label_create(scr);
    lv_label_set_text(cq, "CQ CQ");
    lv_obj_set_style_text_font(cq, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(cq, lv_color_hex(0x15506A), 0);
    lv_obj_set_style_text_align(cq, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(cq, 88);
    lv_obj_set_pos(cq, 40, top + 2 + 8);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, APP_NAME);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, top + 138);

    if (footer) return scr;
    lv_obj_t *tag = lv_label_create(scr);
    lv_label_set_text(tag, "APRS in your pocket");
    lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(tag, lv_color_hex(0xFFC440), 0);
    lv_obj_align(tag, LV_ALIGN_TOP_MID, 0, top + 164);
    return scr;
}

lv_obj_t *s_splash = nullptr;
uint32_t s_splashAtMs = 0;

// ── Screensaver: the platy drifting over black, bouncing off the edges ───────

constexpr int kSaverW = 120, kSaverH = 66;   // splash image at half size
struct {
    lv_obj_t *bg, *img;
    lv_timer_t *timer;
    int x, y, dx, dy;
} s_saver;

void saverStep(lv_timer_t *) {
    const int maxX = lv_obj_get_width(s_saver.bg) - kSaverW;
    const int maxY = lv_obj_get_height(s_saver.bg) - kSaverH;
    s_saver.x += s_saver.dx;
    s_saver.y += s_saver.dy;
    if (s_saver.x <= 0 || s_saver.x >= maxX) s_saver.dx = -s_saver.dx;
    if (s_saver.y <= 0 || s_saver.y >= maxY) s_saver.dy = -s_saver.dy;
    s_saver.x = LV_CLAMP(0, s_saver.x, maxX);
    s_saver.y = LV_CLAMP(0, s_saver.y, maxY);
    lv_obj_set_pos(s_saver.img, s_saver.x, s_saver.y);
}

void saverShow() {
    if (s_saver.bg) return;
    s_saver.bg = lv_obj_create(lv_layer_sys());
    lv_obj_remove_style_all(s_saver.bg);
    lv_obj_set_size(s_saver.bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(s_saver.bg, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_saver.bg, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_saver.bg, LV_OBJ_FLAG_SCROLLABLE);
    s_saver.img = lv_image_create(s_saver.bg);
    lv_image_set_src(s_saver.img, &splash_img);
    lv_obj_set_size(s_saver.img, kSaverW, kSaverH);
    lv_image_set_inner_align(s_saver.img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_update_layout(s_saver.bg);
    s_saver.x = (int)(esp_random() % (uint32_t)(lv_obj_get_width(s_saver.bg) - kSaverW));
    s_saver.y = (int)(esp_random() % (uint32_t)(lv_obj_get_height(s_saver.bg) - kSaverH));
    s_saver.dx = (esp_random() & 1) ? 1 : -1;
    s_saver.dy = (esp_random() & 1) ? 1 : -1;
    lv_obj_set_pos(s_saver.img, s_saver.x, s_saver.y);
    s_saver.timer = lv_timer_create(saverStep, 50, nullptr);
}

void saverHide() {
    if (!s_saver.bg) return;
    lv_timer_delete(s_saver.timer);
    lv_obj_delete(s_saver.bg);
    s_saver = {};
}

void openAbout() {
    char buf[160];
    snprintf(buf, sizeof(buf), APP_VERSION " - %s\nHeap %u - PSRAM %u - %u mV", boardName(),
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram(), (unsigned)boardBatteryMilliVolts());
    lv_group_t *grp = lv_group_create();
    pushScreen(buildSplash(buf), grp);
}

// ── Firmware update ──────────────────────────────────────────────────────────

struct {
    lv_obj_t *info, *install;
} s_ota;

void refreshOta() {
    const OtaStatus o = otaStatus();
    String t = String("Running ") + APP_VERSION + "\n";
    switch (o.state) {
        case OtaState::Idle: t += "Not checked yet."; break;
        case OtaState::Checking: t += "Checking..."; break;
        case OtaState::UpToDate: t += "Up to date (latest " + o.latest + ")."; break;
        case OtaState::Available: t += "Update available: " + o.latest; break;
        case OtaState::Installing:
            t += "Installing " + o.latest + "...\n" +
                 (o.total ? String(o.written * 100 / o.total) + "%" : String(o.written / 1024) + " KB");
            break;
        case OtaState::Done: t += "Installed " + o.latest + ". Rebooting..."; break;
        case OtaState::Failed: t += "Failed: " + o.error; break;
    }
    if (!o.layoutOk) t = String("Running ") + APP_VERSION + "\nUpdates need the factory image: reflash over USB.";
    else if (WiFi.status() != WL_CONNECTED && o.state != OtaState::Installing) t += "\n\nNot on Wi-Fi.";
    lv_label_set_text(s_ota.info, t.c_str());
    if (o.state == OtaState::Available) lv_obj_remove_flag(s_ota.install, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_ota.install, LV_OBJ_FLAG_HIDDEN);
}

void openOta() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_DOWNLOAD " Firmware update", &grp);
    s_ota.info = lv_label_create(scr);
    lv_obj_set_width(s_ota.info, lv_pct(100));
    lv_label_set_long_mode(s_ota.info, LV_LABEL_LONG_WRAP);
    lv_obj_t *list = makeList(scr);
    addItem(list, grp, LV_SYMBOL_REFRESH, "Check for updates", [](lv_event_t *) {
        if (!otaStartCheck()) toast("Can't check now (busy, or not on Wi-Fi)");
        refreshOta();
    });
    s_ota.install = addItem(list, grp, LV_SYMBOL_DOWNLOAD, "Install update", [](lv_event_t *) {
        if (!otaStartInstall()) toast("Can't install now");
        refreshOta();
    });
    lv_obj_t *autoBtn = addItem(list, grp, LV_SYMBOL_OK, "", [](lv_event_t *e) {
        g_config.otaAutoInstall = !g_config.otaAutoInstall;
        configSave();
        lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 1),
                          g_config.otaAutoInstall ? "Install automatically: on" : "Install automatically: off");
    });
    lv_label_set_text(lv_obj_get_child(autoBtn, 1),
                      g_config.otaAutoInstall ? "Install automatically: on" : "Install automatically: off");
    pushScreen(scr, grp, refreshOta);   // refreshed every second while on top
}

lv_obj_t *s_quickTa[AppConfig::kQuickMsgCount];

void saveQuickMsgs() {
    for (size_t i = 0; i < AppConfig::kQuickMsgCount; i++) {
        String t = lv_textarea_get_text(s_quickTa[i]);
        t.trim();
        g_config.quickMsgs[i] = t;
    }
    toast(configSave() ? "Quick messages saved" : "Save failed");
}

void openQuickMsgs() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_EDIT " Quick messages", &grp);
    for (size_t i = 0; i < AppConfig::kQuickMsgCount; i++) {
        s_quickTa[i] = makeInput(scr, grp, (String("Quick message ") + (i + 1)).c_str(), 67);
        lv_textarea_set_text(s_quickTa[i], g_config.quickMsgs[i].c_str());
        lv_obj_add_event_cb(s_quickTa[i], [](lv_event_t *e) {
            const size_t i = (size_t)(uintptr_t)lv_event_get_user_data(e);
            if (i + 1 < AppConfig::kQuickMsgCount) lv_group_focus_obj(s_quickTa[i + 1]);
            else saveQuickMsgs();
        }, LV_EVENT_READY, (void *)(uintptr_t)i);
    }
    lv_obj_t *b = lv_button_create(scr);
    lv_label_set_text(lv_label_create(b), LV_SYMBOL_SAVE " Save");
    lv_obj_add_event_cb(b, [](lv_event_t *) { saveQuickMsgs(); }, LV_EVENT_CLICKED, nullptr);
    lv_group_add_obj(grp, b);
    makeNote(scr, "Shown as shortcuts when writing a message. Leave blank to hide one.");
    pushScreen(scr, grp);
}

void setSoundLabel(lv_obj_t *item) {
    lv_label_set_text(lv_obj_get_child(item, 1), (String("Sound: ") + alertSoundLabel(g_config.alertSound)).c_str());
}

void setVolumeLabel(lv_obj_t *item) {
    lv_label_set_text(lv_obj_get_child(item, 1), (String("Volume: ") + g_config.volumePct + "%").c_str());
}

// Brightness row: a list button (child 1 the label, child 2 a slider). Keys
// keep the row focused and Left/Right step it, so up/down still move through
// the list; on touch screens the slider drags.
constexpr int kBrightnessMin = 10, kBrightnessStep = 25;

void setBrightnessRow(lv_obj_t *row, int level, bool save) {
    level = level < kBrightnessMin ? kBrightnessMin : level > 255 ? 255 : level;
    g_config.brightness = (uint8_t)level;
    screenApplyBrightness();
    lv_label_set_text(lv_obj_get_child(row, 1),
                      (String("Brightness: ") + (level * 100 + 127) / 255 + "%").c_str());
    lv_obj_t *slider = lv_obj_get_child(row, 2);
    if (lv_slider_get_value(slider) != level) lv_slider_set_value(slider, level, LV_ANIM_OFF);
    if (save) configSave();
}

void addBrightnessRow(lv_obj_t *list, lv_group_t *grp) {
    // Select steps up and wraps to the dimmest level after full.
    lv_obj_t *row = addItem(list, grp, LV_SYMBOL_EYE_OPEN, "", [](lv_event_t *e) {
        const int b = g_config.brightness;
        setBrightnessRow((lv_obj_t *)lv_event_get_target(e), b >= 255 ? kBrightnessMin : b + kBrightnessStep, true);
    });
    lv_obj_add_event_cb(row, [](lv_event_t *e) {
        const uint32_t k = lv_event_get_key(e);
        if (k != LV_KEY_LEFT && k != LV_KEY_RIGHT) return;
        const int step = k == LV_KEY_RIGHT ? kBrightnessStep : -kBrightnessStep;
        setBrightnessRow((lv_obj_t *)lv_event_get_target(e), g_config.brightness + step, true);
    }, LV_EVENT_KEY, nullptr);
    lv_obj_t *slider = lv_slider_create(row);
    lv_group_remove_obj(slider);
    lv_slider_set_range(slider, kBrightnessMin, 255);
    lv_obj_set_width(slider, lv_pct(40));
    lv_obj_set_height(slider, 8);
    lv_obj_set_style_margin_right(slider, 10, 0);
    lv_obj_set_ext_click_area(slider, 12);
    lv_obj_add_event_cb(slider, [](lv_event_t *e) {
        lv_obj_t *s = (lv_obj_t *)lv_event_get_target(e);
        setBrightnessRow(lv_obj_get_parent(s), lv_slider_get_value(s), false);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(slider, [](lv_event_t *) { configSave(); }, LV_EVENT_RELEASED, nullptr);
    setBrightnessRow(row, g_config.brightness, false);
}

bool s_themeFromSettings = false;
lv_obj_t *s_themeItem = nullptr;  // in the newest Settings screen

void openSettings() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(LV_SYMBOL_SETTINGS " Settings", &grp);
    lv_obj_t *list = makeList(scr);
    addItem(list, grp, LV_SYMBOL_WIFI, "Open setup access point", [](lv_event_t *) {
        if (!wifiOpenSetupAp()) toast("Connected to Wi-Fi; setup AP stays off");
        openWifi(false);
    });
    addItem(list, grp, LV_SYMBOL_WIFI, "Wi-Fi networks", [](lv_event_t *) { openWifi(true); });
    addItem(list, grp, LV_SYMBOL_EDIT, "Quick messages", [](lv_event_t *) { openQuickMsgs(); });
    addItem(list, grp, LV_SYMBOL_GPS, "GPS & beacon", [](lv_event_t *) { openGps(); });
    addItem(list, grp, LV_SYMBOL_BLUETOOTH, "Radio link (TNC / LoRa)", [](lv_event_t *) { openTnc(); });
    addItem(list, grp, LV_SYMBOL_BLUETOOTH, "Paired radios", [](lv_event_t *) { openBonds(); });
    lv_obj_t *is = addItem(list, grp, LV_SYMBOL_UPLOAD, "", [](lv_event_t *e) {
        g_config.aprsIsEnabled = !g_config.aprsIsEnabled;
        configSave();
        lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
        lv_label_set_text(lv_obj_get_child(b, 1), g_config.aprsIsEnabled ? "APRS-IS: On" : "APRS-IS: Off");
    });
    lv_label_set_text(lv_obj_get_child(is, 1), g_config.aprsIsEnabled ? "APRS-IS: On" : "APRS-IS: Off");
    // Cycles through the timeout choices; the label shows the current one.
    lv_obj_t *to = addItem(list, grp, LV_SYMBOL_EYE_CLOSE, "", [](lv_event_t *e) {
        int i = 0;
        while (i < kScreenTimeoutCount && kScreenTimeoutSecs[i] != g_config.screenOnSecs) i++;
        g_config.screenOnSecs = kScreenTimeoutSecs[(i + 1) % kScreenTimeoutCount];
        configSave();
        lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
        lv_label_set_text(lv_obj_get_child(b, 1),
                          (String("Screen timeout: ") + screenTimeoutLabel(g_config.screenOnSecs)).c_str());
    });
    lv_label_set_text(lv_obj_get_child(to, 1),
                      (String("Screen timeout: ") + screenTimeoutLabel(g_config.screenOnSecs)).c_str());
    lv_obj_t *ss = addItem(list, grp, LV_SYMBOL_IMAGE, "", [](lv_event_t *e) {
        g_config.screenSaver = !g_config.screenSaver;
        configSave();
        lv_obj_t *b = (lv_obj_t *)lv_event_get_target(e);
        lv_label_set_text(lv_obj_get_child(b, 1), g_config.screenSaver ? "At timeout: Screensaver" : "At timeout: Screen off");
    });
    lv_label_set_text(lv_obj_get_child(ss, 1), g_config.screenSaver ? "At timeout: Screensaver" : "At timeout: Screen off");
    addBrightnessRow(list, grp);
    if (audioPresent()) {
        // Each press moves to the next style or volume and plays the new-message tone with it.
        lv_obj_t *snd = addItem(list, grp, LV_SYMBOL_BELL, "", [](lv_event_t *e) {
            g_config.alertSound = (AlertSound)(((uint8_t)g_config.alertSound + 1) % kAlertSoundCount);
            configSave();
            alertPreview(g_config.alertSound);
            setSoundLabel((lv_obj_t *)lv_event_get_target(e));
        });
        setSoundLabel(snd);
        if (audioHasVolume()) {
            lv_obj_t *vol = addItem(list, grp, LV_SYMBOL_VOLUME_MAX, "", [](lv_event_t *e) {
                g_config.volumePct = g_config.volumePct >= 100 ? 0 : g_config.volumePct + 10;
                configSave();
                alertPreview(g_config.alertSound == AlertSound::Off ? AlertSound::Default : g_config.alertSound);
                setVolumeLabel((lv_obj_t *)lv_event_get_target(e));
            });
            setVolumeLabel(vol);
        }
    }
    if (notifyLedPresent()) {
        // Each press moves to the next colour and blinks it once.
        lv_obj_t *led = addItem(list, grp, LV_SYMBOL_CHARGE, "", [](lv_event_t *e) {
            g_config.ledColor = (LedColor)(((uint8_t)g_config.ledColor + 1) % kLedColorCount);
            configSave();
            notifyLedPreview(g_config.ledColor);
            lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 1),
                              (String("Message light: ") + ledColorLabel(g_config.ledColor)).c_str());
        });
        lv_label_set_text(lv_obj_get_child(led, 1),
                          (String("Message light: ") + ledColorLabel(g_config.ledColor)).c_str());
    }
    if (notifyKbBlinkPresent()) {
        // Turning it on blinks the keyboard once to show it works.
        lv_obj_t *kb = addItem(list, grp, LV_SYMBOL_KEYBOARD, "", [](lv_event_t *e) {
            g_config.kbBlink = !g_config.kbBlink;
            configSave();
            notifyLedApplySetting();
            if (g_config.kbBlink) notifyLedPreview(LedColor::White);
            lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 1),
                              g_config.kbBlink ? "Keyboard blink: On" : "Keyboard blink: Off");
        });
        lv_label_set_text(lv_obj_get_child(kb, 1), g_config.kbBlink ? "Keyboard blink: On" : "Keyboard blink: Off");
    }
    if (notifyLightPresent()) {
        lv_obj_t *lto = addItem(list, grp, LV_SYMBOL_CHARGE, "", [](lv_event_t *e) {
            int i = 0;
            while (i < kLedTimeoutCount && kLedTimeoutSecs[i] != g_config.ledTimeoutSecs) i++;
            g_config.ledTimeoutSecs = kLedTimeoutSecs[(i + 1) % kLedTimeoutCount];
            configSave();
            lv_label_set_text(lv_obj_get_child((lv_obj_t *)lv_event_get_target(e), 1),
                              (String("Light timeout: ") + ledTimeoutLabel(g_config.ledTimeoutSecs)).c_str());
        });
        lv_label_set_text(lv_obj_get_child(lto, 1),
                          (String("Light timeout: ") + ledTimeoutLabel(g_config.ledTimeoutSecs)).c_str());
    }
    if (sdHasSlot())
        addItem(list, grp, LV_SYMBOL_SD_CARD, "Backup settings to SD card", [](lv_event_t *) { backupToSdNow(); });
    lv_obj_t *th = addItem(list, grp, LV_SYMBOL_EYE_OPEN, "", [](lv_event_t *) {
        // uiLoop() sees the change and rebuilds the UI in the new theme.
        g_config.theme = g_config.theme == UiTheme::Light ? UiTheme::Dark : UiTheme::Light;
        configSave();
        s_themeFromSettings = true;
    });
    s_themeItem = th;
    lv_label_set_text(lv_obj_get_child(th, 1), g_config.theme == UiTheme::Light ? "Theme: Light" : "Theme: Dark");
    addItem(list, grp, LV_SYMBOL_DOWNLOAD, "Firmware update", [](lv_event_t *) { openOta(); });
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
    if (sdHasSlot()) t += "\nSD: " + sdStatusText();
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
    bool apWasActive;
} s_home;

void refreshHome() {
    // Pop the info up by itself whenever the setup AP comes on, so the QR code
    // is there for first-time setup.
    const bool ap = wifiStatus().apActive;
    if (ap && !s_home.apWasActive && s_depth == 1) openInfo();
    s_home.apWasActive = ap;
}

void buildHome() {
    lv_group_t *grp;
    lv_obj_t *scr = newScreen(nullptr, &grp);

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

// Runs inside LVGL's indev read; navigation is deferred to uiLoop() so no
// screen is deleted while LVGL is still processing input for it.
bool hotkey(uint8_t key) {
    switch (key) {
        case RK_BACK: s_pendingNav = Nav::Back; return true;
        case RK_HOME: s_pendingNav = Nav::Home; return true;
        case RK_MESSAGE: s_pendingNav = Nav::Messages; return true;
        case RK_MAP: s_pendingNav = Nav::Map; return true;
        case RK_SUB_BACK: s_pendingNav = Nav::Stations; return true;
        case RK_MONITOR: s_pendingNav = Nav::Monitor; return true;
        case RK_FAVORITE: s_pendingFav = true; return true;
        default: return false;
    }
}

// While someone is configuring the device from a phone through the setup AP,
// the screen must not time out under them; once the AP closes (setup done),
// bring it back so they see the device come up.
void holdScreenForSetupAp() {
    static bool held = false;
    const WifiStatus w = wifiStatus();
    const bool hold = w.apActive && (w.apReason == ApReason::Setup || w.apReason == ApReason::Manual);
    if (hold) screenNoteActivity();
    else if (held && screenAsleep() && !screenTryWake("setup done")) return;  // retry after guard
    held = hold;
}

// Switches to g_config.theme (changed in Settings or the web config). Objects
// carry colours from when they were built, so rebuild the status bar and home;
// if this came from Settings, reopen it on the theme item.
void rebuildForTheme() {
    const bool inSettings = s_themeFromSettings;
    s_themeFromSettings = false;
    closeQuickPicker(-1);
    closeInfo();
    closeChoice(-1);
    popToHome();
#if HAS_TOUCH && !HAS_KEYBOARD
    if (s_osk) {
        lv_obj_delete(s_osk);
        s_osk = nullptr;
    }
#endif
    if (s_toast) {
        lv_obj_delete(s_toast);
        s_toast = nullptr;
    }
    themeInit();
    lv_obj_delete(lv_obj_get_parent(s_sbCall));
    s_sbBack = nullptr;
    buildStatusBar();
    const Screen old = s_stack[0];
    s_depth = 0;
    buildHome();
    lv_group_delete(old.grp);
    lv_obj_delete(old.scr);
    updateStatusBar();
    if (inSettings) {
        openSettings();
        lv_group_t *g = s_stack[s_depth - 1].grp;
        if (s_themeItem) {
            lv_group_focus_obj(s_themeItem);
            lv_obj_scroll_to_view(s_themeItem, LV_ANIM_OFF);
        }
    }
}

void tick(lv_timer_t *) {
    holdScreenForSetupAp();
    updateStatusBar();
    if (s_depth > 0 && s_stack[s_depth - 1].refresh) s_stack[s_depth - 1].refresh();
    refreshInfo();
}

}  // namespace

lv_obj_t *uiBuildSplash(const char *footer) { return buildSplash(footer); }

void uiShowSplash() {
    s_splash = buildSplash(nullptr);
    lv_screen_load(s_splash);
    lv_refr_now(nullptr);
    s_splashAtMs = millis();
}

void uiBegin() {
    // Keep the splash up for at least 3.5 s, however quick the rest of setup was.
    while (s_splash && millis() - s_splashAtMs < 3500) delay(10);
    keyboardSetHotkeyHandler(hotkey);
    screenSetSaverHooks(saverShow, saverHide);
    themeInit();
    buildStatusBar();
    buildHome();
    if (configIsFresh()) {
        const auto files = backupListSd();
        if (!files.empty()) openRestorePrompt(files);
    }
    updateStatusBar();
    messagesSetIncomingHook(onIncomingMessage);
    lv_timer_create(tick, 1000, nullptr);
    if (s_splash && lv_screen_active() != s_splash) {
        lv_obj_delete(s_splash);
        s_splash = nullptr;
    }
}

void uiLoop() {
    const Nav nav = s_pendingNav;
    s_pendingNav = Nav::None;
    if (s_qm.bg && (s_pendingQuick != -2 || nav != Nav::None)) {
        const int pick = s_pendingQuick;
        s_pendingQuick = -2;
        closeQuickPicker(pick);
        if (nav == Nav::Back || nav == Nav::None) return;
    }
    if (s_choice.bg && (s_pendingChoice != -2 || nav != Nav::None)) {
        const int pick = s_pendingChoice;
        s_pendingChoice = -2;
        closeChoice(pick);
        if (nav == Nav::Back || nav == Nav::None) return;
    }
    if (s_pendingCloseInfo || (nav != Nav::None && s_info.bg)) {
        s_pendingCloseInfo = false;
        closeInfo();
        if (nav == Nav::Home) popToHome();
        return;
    }
    if (s_pendingFav) {
        s_pendingFav = false;
        if (!s_qm.bg && !s_info.bg && !s_choice.bg) toggleFavoriteKey();
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
    } else if (nav == Nav::Stations) {
        popToHome();
        openStations();
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
    if (s_restore.rebootAtMs && (int32_t)(millis() - s_restore.rebootAtMs) >= 0) ESP.restart();
    if (g_config.theme != s_appliedTheme) rebuildForTheme();
    toastLoop();
}
