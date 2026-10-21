#include "map_view.h"
#include "hal/board_pins.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>

#include <algorithm>

#include "version.h"
#include "hal/sd_card.h"
#include "net/wifi_manager.h"
#include "services/data_store.h"
#include "services/router.h"
#include "util/geo.h"

// LVGL's bundled lodepng (built as C; its header pulls in C++ overloads in C++).
// LVGL patches it so `*out` is an lv_draw_buf_t (RGBA bytes, stride 4*w).
extern "C" unsigned lodepng_decode32(unsigned char **out, unsigned *w, unsigned *h,
                                     const unsigned char *in, size_t insize);

namespace {

constexpr const char *kTileBase = "http://maps.camillia.sumat.org";
constexpr int kTile = 256;
constexpr int kMinZoom = 2, kMaxZoom = 17;
constexpr int kSlots = 12;                     // decoded tiles kept in PSRAM (128 KB each)
constexpr size_t kMaxPng = 192 * 1024;
constexpr uint32_t kRetryMs = 30000;
constexpr int kPanStep = 60;
constexpr int kMaxMarkers = 40;
// Heat kernel radius, fixed on screen so clusters merge as the map zooms out.
constexpr int kHeatR = 18;
// One station's peak: lone stations sit mid-ramp, three or more overlapping saturate to red.
constexpr int kHeatPeak = 110;

// ── Worker: download and/or decode one tile off the loop task ───────────────

enum class JobState : uint8_t { Idle, Busy, Done };

struct Job {
    volatile JobState state = JobState::Idle;
    int z = 0;
    int32_t x = 0, y = 0;
    bool fetch = false;        // download first (else `png` already holds the file)
    bool ok = false;
    bool fetched = false;      // png holds freshly downloaded bytes worth caching
    size_t pngLen = 0;
    uint8_t *png = nullptr;    // kMaxPng, PSRAM
    uint16_t *rgb = nullptr;   // 256x256 RGB565, PSRAM
};
Job s_job;

bool fetchTile(Job &j) {
    char url[128];
    snprintf(url, sizeof(url), "%s/%d/%ld/%ld.png", kTileBase, j.z, (long)j.x, (long)j.y);
    WiFiClient client;
    HTTPClient http;
    http.setReuse(false);
    http.setConnectTimeout(5000);
    http.setTimeout(10000);
    if (!http.begin(client, url)) return false;
    http.addHeader("User-Agent", APP_ID "/" APP_VERSION);
    const char *hdrs[] = {"x-blocked"};
    http.collectHeaders(hdrs, 1);
    const int code = http.GET();
    if (code != HTTP_CODE_OK || http.hasHeader("x-blocked")) {
        Serial.printf("[map] tile %d/%ld/%ld HTTP %d%s\n", j.z, (long)j.x, (long)j.y, code,
                      http.hasHeader("x-blocked") ? " blocked" : "");
        http.end();
        return false;
    }
    int remaining = http.getSize();
    if (remaining > (int)kMaxPng) {
        http.end();
        return false;
    }
    WiFiClient *s = http.getStreamPtr();
    size_t n = 0;
    uint32_t deadline = millis() + 10000;
    while ((remaining != 0) && (int32_t)(millis() - deadline) < 0) {
        const int avail = s->available();
        if (avail <= 0) {
            if (!http.connected()) break;
            delay(2);
            continue;
        }
        size_t want = (size_t)avail;
        if (want > kMaxPng - n) want = kMaxPng - n;
        if (want == 0) break;
        const int r = s->readBytes(j.png + n, want);
        if (r <= 0) break;
        n += (size_t)r;
        if (remaining > 0) remaining -= r;
        deadline = millis() + 10000;
    }
    http.end();
    if (remaining > 0) return false;
    j.pngLen = n;
    return true;
}

bool decodeTile(Job &j) {
    static const uint8_t kSig[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
    if (j.pngLen < 8 || memcmp(j.png, kSig, 8) != 0) return false;
    unsigned char *out = nullptr;
    unsigned w = 0, h = 0;
    const unsigned err = lodepng_decode32(&out, &w, &h, j.png, j.pngLen);
    lv_draw_buf_t *buf = (lv_draw_buf_t *)out;
    if (err || !buf || w != kTile || h != kTile) {
        if (buf) lv_draw_buf_destroy(buf);
        Serial.printf("[map] decode failed err=%u %ux%u\n", err, w, h);
        return false;
    }
    for (int y = 0; y < kTile; y++) {
        const uint8_t *p = buf->data + (size_t)y * buf->header.stride;
        uint16_t *d = j.rgb + y * kTile;
        for (int x = 0; x < kTile; x++, p += 4)
            d[x] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
    }
    lv_draw_buf_destroy(buf);
    return true;
}

void workerTask(void *) {
    Job &j = s_job;
    j.fetched = false;
    j.ok = false;
    if (j.fetch) {
        j.fetched = fetchTile(j);
        j.ok = j.fetched && decodeTile(j);
        if (!j.ok) j.fetched = false;
    } else {
        j.ok = decodeTile(j);
    }
    j.state = JobState::Done;
    vTaskDelete(nullptr);
}

bool jobBuffersReady() {
    if (!s_job.png) s_job.png = (uint8_t *)heap_caps_malloc(kMaxPng, MALLOC_CAP_SPIRAM);
    if (!s_job.rgb) s_job.rgb = (uint16_t *)heap_caps_malloc(kTile * kTile * 2, MALLOC_CAP_SPIRAM);
    return s_job.png && s_job.rgb;
}

// ── Map state ───────────────────────────────────────────────────────────────

enum class Slot : uint8_t { Empty, Ready, Failed };

struct TileSlot {
    Slot state;
    int z;
    int32_t x, y;
    uint32_t used;      // millis() of last use (LRU) or of the failure
    uint16_t *px;       // 256x256 RGB565
};

struct Marker {
    char call[10];
    int sx, sy;
};

struct Map {
    lv_obj_t *root, *canvas, *markers, *info;
    uint16_t *buf;
    uint8_t *heat;              // w x h station density; null = no heat layer
    int w, h;
    int zoom;
    double cx, cy;              // view centre, world pixels at `zoom`
    TileSlot slots[kSlots];
    bool dirty;                 // tiles need re-blitting
    bool markersDirty;
    uint32_t dataGen;
    uint32_t lastMarkersMs;
    bool wifi;
    lv_timer_t *timer;
    void (*onStation)(const char *);
    char selected[10];
    int visible, ready, failed;
};
Map *s_map = nullptr;

// Last view, restored when the map is reopened.
bool s_haveView = false;
int s_lastZoom;
double s_lastLat, s_lastLon;

int32_t tilesAt(int z) { return (int32_t)1 << z; }

TileSlot *findSlot(int z, int32_t x, int32_t y) {
    for (auto &s : s_map->slots)
        if (s.state != Slot::Empty && s.z == z && s.x == x && s.y == y) return &s;
    return nullptr;
}

TileSlot *allocSlot() {
    TileSlot *best = nullptr;
    for (auto &s : s_map->slots) {
        if (s.state == Slot::Empty) { best = &s; break; }
        if (!best || (int32_t)(s.used - best->used) < 0) best = &s;
    }
    if (!best->px) best->px = (uint16_t *)heap_caps_malloc(kTile * kTile * 2, MALLOC_CAP_SPIRAM);
    best->state = Slot::Empty;
    return best->px ? best : nullptr;
}

void tilePath(char *out, size_t n, int z, int32_t x, int32_t y) {
    snprintf(out, n, "/platy/tiles/%d/%ld/%ld.png", z, (long)x, (long)y);
}

double viewLeft() { return s_map->cx - s_map->w / 2.0; }
double viewTop() { return s_map->cy - s_map->h / 2.0; }

void clampView() {
    const double world = (double)kTile * tilesAt(s_map->zoom);
    s_map->cx = fmod(s_map->cx, world);
    if (s_map->cx < 0) s_map->cx += world;
    if (s_map->cy < 0) s_map->cy = 0;
    if (s_map->cy > world) s_map->cy = world;
}

void setCenter(double lat, double lon) {
    s_map->cx = geo::lonToWorldX(lon, s_map->zoom);
    s_map->cy = geo::latToWorldY(lat, s_map->zoom);
    clampView();
    s_map->dirty = s_map->markersDirty = true;
}

void setZoom(int z) {
    if (z < kMinZoom) z = kMinZoom;
    if (z > kMaxZoom) z = kMaxZoom;
    if (z == s_map->zoom) return;
    const double f = ldexp(1.0, z - s_map->zoom);
    s_map->cx *= f;
    s_map->cy *= f;
    s_map->zoom = z;
    clampView();
    s_map->dirty = s_map->markersDirty = true;
}

void pan(int dx, int dy) {
    s_map->cx += dx;
    s_map->cy += dy;
    clampView();
    s_map->dirty = s_map->markersDirty = true;
}

// Visible tile range (x may exceed the world; wrap with tilesAt()).
void visibleRange(int32_t &x0, int32_t &x1, int32_t &y0, int32_t &y1) {
    const double l = viewLeft(), t = viewTop();
    x0 = (int32_t)floor(l / kTile);
    x1 = (int32_t)floor((l + s_map->w - 1) / kTile);
    y0 = (int32_t)floor(t / kTile);
    y1 = (int32_t)floor((t + s_map->h - 1) / kTile);
}

int32_t wrapX(int32_t x) {
    const int32_t n = tilesAt(s_map->zoom);
    x %= n;
    return x < 0 ? x + n : x;
}

// ── Heat layer ──────────────────────────────────────────────────────────────

uint8_t s_kernel[(2 * kHeatR + 1) * (2 * kHeatR + 1)];
bool s_kernelReady = false;

void buildKernel() {
    if (s_kernelReady) return;
    const int side = 2 * kHeatR + 1;
    const float r2 = (float)(kHeatR * kHeatR);
    for (int y = -kHeatR; y <= kHeatR; y++)
        for (int x = -kHeatR; x <= kHeatR; x++) {
            float f = 1.0f - (float)(x * x + y * y) / r2;
            f = f > 0 ? f * f : 0;   // quartic falloff: soft edge, no long tail
            s_kernel[(y + kHeatR) * side + (x + kHeatR)] = (uint8_t)lroundf(f * kHeatPeak);
        }
    s_kernelReady = true;
}

// Blue, cyan, green, yellow, red at 0, 64, 128, 192, 255.
void heatColor(uint8_t v, int &r, int &g, int &b) {
    static constexpr uint8_t kStops[5][3] = {
        {48, 96, 255}, {0, 224, 255}, {64, 236, 72}, {255, 224, 0}, {255, 48, 32}};
    const int seg = v >= 255 ? 3 : v / 64;
    const int t = v >= 255 ? 64 : v - seg * 64;
    r = kStops[seg][0] + (kStops[seg + 1][0] - kStops[seg][0]) * t / 64;
    g = kStops[seg][1] + (kStops[seg + 1][1] - kStops[seg][1]) * t / 64;
    b = kStops[seg][2] + (kStops[seg + 1][2] - kStops[seg][2]) * t / 64;
}

double worldXOnScreen(double lon) {
    const double world = (double)kTile * tilesAt(s_map->zoom);
    double x = geo::lonToWorldX(lon, s_map->zoom) - viewLeft();
    if (x < -world / 2) x += world;
    if (x > world / 2) x -= world;
    return x;
}

// Blends station density over the freshly blitted tiles.
void paintHeat() {
    Map &m = *s_map;
    if (!m.heat) return;
    buildKernel();
    memset(m.heat, 0, (size_t)m.w * m.h);
    const int side = 2 * kHeatR + 1;
    bool any = false;
    for (size_t i = 0; i < stationDbCount(); i++) {
        const Station *s = stationDbAt(i);
        if (!s || !s->hasPos) continue;
        const int cx = (int)lround(worldXOnScreen(s->lon));
        const int cy = (int)lround(geo::latToWorldY(s->lat, m.zoom) - viewTop());
        const int x0 = std::max(0, cx - kHeatR), x1 = std::min(m.w - 1, cx + kHeatR);
        const int y0 = std::max(0, cy - kHeatR), y1 = std::min(m.h - 1, cy + kHeatR);
        if (x0 > x1 || y0 > y1) continue;
        any = true;
        for (int y = y0; y <= y1; y++) {
            uint8_t *row = m.heat + (size_t)y * m.w;
            const uint8_t *k = s_kernel + (y - cy + kHeatR) * side + (kHeatR - cx);
            for (int x = x0; x <= x1; x++) {
                const int v = row[x] + k[x];
                row[x] = (uint8_t)(v > 255 ? 255 : v);
            }
        }
    }
    if (!any) return;
    for (size_t i = 0, n = (size_t)m.w * m.h; i < n; i++) {
        const uint8_t v = m.heat[i];
        if (!v) continue;
        // Never fully opaque, so streets under a hot spot still show.
        const int a = v * 3 > 190 ? 190 : v * 3;
        int hr, hg, hb;
        heatColor(v, hr, hg, hb);
        const uint16_t p = m.buf[i];
        const int r = ((p >> 11) & 0x1F) << 3, g = ((p >> 5) & 0x3F) << 2, b = (p & 0x1F) << 3;
        const int nr = r + (hr - r) * a / 255, ng = g + (hg - g) * a / 255, nb = b + (hb - b) * a / 255;
        m.buf[i] = (uint16_t)(((nr >> 3) << 11) | ((ng >> 2) << 5) | (nb >> 3));
    }
}

void blit() {
    Map &m = *s_map;
    const int lx = (int)floor(viewLeft()), ty = (int)floor(viewTop());
    int32_t x0, x1, y0, y1;
    visibleRange(x0, x1, y0, y1);
    const uint32_t now = millis();
    m.visible = m.ready = m.failed = 0;
    for (int32_t ty_ = y0; ty_ <= y1; ty_++) {
        for (int32_t tx = x0; tx <= x1; tx++) {
            const int ox = tx * kTile - lx, oy = ty_ * kTile - ty;   // tile origin on screen
            const int sx0 = ox < 0 ? 0 : ox, sy0 = oy < 0 ? 0 : oy;
            const int sx1 = ox + kTile > m.w ? m.w : ox + kTile;
            const int sy1 = oy + kTile > m.h ? m.h : oy + kTile;
            if (sx0 >= sx1 || sy0 >= sy1) continue;
            const bool inWorld = ty_ >= 0 && ty_ < tilesAt(m.zoom);
            TileSlot *s = inWorld ? findSlot(m.zoom, wrapX(tx), ty_) : nullptr;
            if (inWorld) m.visible++;
            if (s && s->state == Slot::Ready) {
                m.ready++;
                s->used = now;
                for (int y = sy0; y < sy1; y++)
                    memcpy(m.buf + y * m.w + sx0, s->px + (y - oy) * kTile + (sx0 - ox),
                           (size_t)(sx1 - sx0) * 2);
            } else {
                if (s && s->state == Slot::Failed) m.failed++;
                // Placeholder: dark background with a faint grid line every 64 px.
                for (int y = sy0; y < sy1; y++) {
                    uint16_t *row = m.buf + y * m.w;
                    const bool line = ((y - oy) & 63) == 0;
                    for (int x = sx0; x < sx1; x++)
                        row[x] = (line || ((x - ox) & 63) == 0) ? 0x31A6 : 0x18C3;
                }
            }
        }
    }
    paintHeat();
    lv_obj_invalidate(m.canvas);
}

// Picks the nearest visible tile that is neither decoded nor recently failed.
bool nextWanted(int32_t &ox, int32_t &oy) {
    Map &m = *s_map;
    int32_t x0, x1, y0, y1;
    visibleRange(x0, x1, y0, y1);
    const double ccx = m.cx / kTile, ccy = m.cy / kTile;
    double best = 1e18;
    bool found = false;
    const uint32_t now = millis();
    for (int32_t ty = y0; ty <= y1; ty++) {
        if (ty < 0 || ty >= tilesAt(m.zoom)) continue;
        for (int32_t tx = x0; tx <= x1; tx++) {
            TileSlot *s = findSlot(m.zoom, wrapX(tx), ty);
            if (s && s->state == Slot::Ready) continue;
            if (s && s->state == Slot::Failed && now - s->used < kRetryMs) continue;
            const double dx = tx + 0.5 - ccx, dy = ty + 0.5 - ccy;
            const double d = dx * dx + dy * dy;
            if (d < best) {
                best = d;
                ox = wrapX(tx);
                oy = ty;
                found = true;
            }
        }
    }
    return found;
}

void markFailed(int z, int32_t x, int32_t y) {
    TileSlot *s = findSlot(z, x, y);
    if (!s) s = allocSlot();
    if (!s) return;
    s->state = Slot::Failed;
    s->z = z;
    s->x = x;
    s->y = y;
    s->used = millis();
}

void saveToSd(const Job &j) {
    if (!sdMounted()) return;
    char dir[48], path[64], tmp[70];
    snprintf(dir, sizeof(dir), "/platy/tiles/%d/%ld", j.z, (long)j.x);
    if (!sdMakeDirs(dir)) return;
    tilePath(path, sizeof(path), j.z, j.x, j.y);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    File f = sdFs().open(tmp, FILE_WRITE);
    if (!f) return;
    const bool ok = f.write(j.png, j.pngLen) == j.pngLen;
    f.close();
    if (ok) {
        if (sdFs().exists(path)) sdFs().remove(path);
        if (sdFs().rename(tmp, path)) return;
    }
    sdFs().remove(tmp);
}

bool loadFromSd(int z, int32_t x, int32_t y) {
    if (!sdMounted()) return false;
    char path[64];
    tilePath(path, sizeof(path), z, x, y);
    if (!sdFs().exists(path)) return false;
    File f = sdFs().open(path, FILE_READ);
    if (!f) return false;
    const size_t size = f.size();
    bool ok = size > 8 && size <= kMaxPng && f.read(s_job.png, size) == size;
    f.close();
    if (!ok) return false;
    s_job.pngLen = size;
    return true;
}

void startJob(int z, int32_t x, int32_t y, bool fetch) {
    s_job.z = z;
    s_job.x = x;
    s_job.y = y;
    s_job.fetch = fetch;
    s_job.state = JobState::Busy;
    if (xTaskCreatePinnedToCore(workerTask, "maptile", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
        s_job.state = JobState::Idle;
        markFailed(z, x, y);
    }
}

void collectJob() {
    Map &m = *s_map;
    Job &j = s_job;
    if (j.ok) {
        if (j.fetched) saveToSd(j);
        TileSlot *s = findSlot(j.z, j.x, j.y);
        if (!s) s = allocSlot();
        if (s) {
            memcpy(s->px, j.rgb, kTile * kTile * 2);
            s->state = Slot::Ready;
            s->z = j.z;
            s->x = j.x;
            s->y = j.y;
            s->used = millis();
        }
        if (j.z == m.zoom) m.dirty = true;
    } else {
        if (!j.fetch && sdMounted()) {
            // Corrupt cache file: drop it so it is downloaded again.
            char path[64];
            tilePath(path, sizeof(path), j.z, j.x, j.y);
            if (sdFs().exists(path)) sdFs().remove(path);
        }
        markFailed(j.z, j.x, j.y);
        m.dirty = true;
    }
    j.state = JobState::Idle;
}

void scheduleJob() {
    Map &m = *s_map;
    int32_t x, y;
    if (!nextWanted(x, y)) return;
    if (loadFromSd(m.zoom, x, y)) {
        startJob(m.zoom, x, y, false);
    } else if (m.wifi) {
        startJob(m.zoom, x, y, true);
    } else {
        markFailed(m.zoom, x, y);
        m.dirty = true;
    }
}

// ── Markers ─────────────────────────────────────────────────────────────────

bool toScreen(double lat, double lon, int &sx, int &sy) {
    Map &m = *s_map;
    const double world = (double)kTile * tilesAt(m.zoom);
    double x = geo::lonToWorldX(lon, m.zoom) - viewLeft();
    if (x < -world / 2) x += world;
    if (x > world / 2) x -= world;
    const double y = geo::latToWorldY(lat, m.zoom) - viewTop();
    sx = (int)lround(x);
    sy = (int)lround(y);
    return sx >= -4 && sx < m.w + 4 && sy >= -4 && sy < m.h + 4;
}

// Label rectangles placed in the current marker pass, for decluttering.
lv_area_t s_labels[kMaxMarkers + 1];
int s_labelCount = 0;

bool labelFits(const lv_area_t &a) {
    for (int i = 0; i < s_labelCount; i++)
        if (a.x1 <= s_labels[i].x2 && a.x2 >= s_labels[i].x1 && a.y1 <= s_labels[i].y2 &&
            a.y2 >= s_labels[i].y1)
            return false;
    return true;
}

void addMarker(int sx, int sy, const char *text, lv_color_t color, bool highlight) {
    Map &m = *s_map;
    lv_obj_t *dot = lv_obj_create(m.markers);
    lv_obj_remove_style_all(dot);
    const int d = highlight ? 10 : 8;
    lv_obj_set_size(dot, d, d);
    lv_obj_set_pos(dot, sx - d / 2, sy - d / 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(dot, color, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dot, highlight ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_border_width(dot, highlight ? 2 : 1, 0);

    const lv_area_t a = {sx + d / 2 + 1, sy - 8,
                         sx + d / 2 + 1 + (int32_t)lv_text_get_width(text, strlen(text), &lv_font_montserrat_12, 0) + 4,
                         sy + 7};
    if (!highlight && !labelFits(a)) return;
    if (s_labelCount < kMaxMarkers + 1) s_labels[s_labelCount++] = a;

    lv_obj_t *l = lv_label_create(m.markers);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, highlight ? lv_color_white() : lv_color_black(), 0);
    lv_obj_set_style_bg_color(l, highlight ? lv_color_hex(0x1565c0) : lv_color_white(), 0);
    lv_obj_set_style_bg_opa(l, highlight ? LV_OPA_COVER : LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(l, 2, 0);
    lv_obj_set_pos(l, sx + d / 2 + 1, sy - 8);
}

void rebuildMarkers() {
    Map &m = *s_map;
    lv_obj_clean(m.markers);
    s_labelCount = 0;
    int count = 0;
    // The selected station is placed first so its label always wins.
    if (m.selected[0]) {
        const Station *s = stationDbFind(m.selected);
        int sx, sy;
        if (s && s->hasPos && toScreen(s->lat, s->lon, sx, sy)) {
            addMarker(sx, sy, s->call, lv_color_hex(0xffb300), true);
            count++;
        }
    }
    {
        double lat, lon;
        int sx, sy;
        if (myPosition(lat, lon) && toScreen(lat, lon, sx, sy))
            addMarker(sx, sy, "me", lv_color_hex(0x1e88e5), false);
    }
    const size_t n = stationDbCount();
    for (size_t i = 0; i < n && count < kMaxMarkers; i++) {
        const Station *s = stationDbAt(i);
        if (!s || !s->hasPos) continue;
        int sx, sy;
        if (!toScreen(s->lat, s->lon, sx, sy)) continue;
        if (strcmp(s->call, m.selected) == 0) continue;
        const lv_color_t c = s->hasWeather ? lv_color_hex(0x29b6f6)
                             : s->isObject ? lv_color_hex(0xab47bc)
                             : s->viaIs    ? lv_color_hex(0x9e9e9e)
                                           : lv_color_hex(0xffb300);
        addMarker(sx, sy, s->call, c, false);
        count++;
    }
    m.markersDirty = false;
    m.dataGen = g_dataGen;
    m.lastMarkersMs = millis();
}

void updateInfo() {
    Map &m = *s_map;
    char buf[96];
    const char *state;
    if (s_job.state == JobState::Busy) state = s_job.fetch ? "downloading" : "loading";
    else if (m.ready == m.visible) state = "";
    else if (!m.wifi && !sdMounted()) state = "offline, no SD";
    else if (!m.wifi) state = "offline";
    else if (m.failed) state = "some tiles failed";
    else state = "";
    if (m.selected[0])
        snprintf(buf, sizeof(buf), "z%d  %s  " LV_SYMBOL_RIGHT " %s", m.zoom, m.selected,
                 HAS_KEYBOARD ? "Enter=details" : "tap again for details");
    else
        snprintf(buf, sizeof(buf), "z%d  %s%s%s", m.zoom, state, state[0] ? "  " : "",
                 HAS_KEYBOARD ? "+/- zoom, N next, C me" : "drag to pan, tap a station");
    lv_label_set_text(m.info, buf);
}

// Cycles the selection through stations visible in the current view, nearest
// to the centre first, then recentres on it.
void selectNext() {
    Map &m = *s_map;
    struct Cand { const Station *s; };
    Cand c[kMaxMarkers];
    int n = 0;
    const size_t total = stationDbCount();
    for (size_t i = 0; i < total && n < kMaxMarkers; i++) {
        const Station *s = stationDbAt(i);
        if (!s || !s->hasPos) continue;
        int sx, sy;
        if (!toScreen(s->lat, s->lon, sx, sy)) continue;
        c[n++] = {s};
    }
    if (!n) {
        // Nothing in view: jump to the most recently heard station with a position.
        for (size_t i = 0; i < total; i++) {
            const Station *s = stationDbAt(i);
            if (s && s->hasPos) {
                strlcpy(m.selected, s->call, sizeof(m.selected));
                setCenter(s->lat, s->lon);
                return;
            }
        }
        return;
    }
    // Cycle by callsign: distance order would change after every recentre.
    std::sort(c, c + n, [](const Cand &a, const Cand &b) { return strcmp(a.s->call, b.s->call) < 0; });
    int next = 0;
    for (int i = 0; i < n; i++)
        if (strcmp(c[i].s->call, m.selected) == 0) { next = (i + 1) % n; break; }
    strlcpy(m.selected, c[next].s->call, sizeof(m.selected));
    setCenter(c[next].s->lat, c[next].s->lon);
}

bool centerOnMe() {
    double lat, lon;
    if (!myPosition(lat, lon)) return false;
    s_map->selected[0] = 0;
    setCenter(lat, lon);
    return true;
}

// ── LVGL glue ───────────────────────────────────────────────────────────────

void timerCb(lv_timer_t *) {
    Map &m = *s_map;
    const bool wifi = wifiStaConnected();
    if (wifi != m.wifi) {
        m.wifi = wifi;
        // Connectivity changed: retry failed tiles right away.
        for (auto &s : m.slots)
            if (s.state == Slot::Failed) s.state = Slot::Empty;
        m.dirty = true;
    }
    if (s_job.state == JobState::Done) collectJob();
    if (s_job.state == JobState::Idle) scheduleJob();
    const bool newData = m.dataGen != g_dataGen && millis() - m.lastMarkersMs > 3000;
    if (m.dirty || newData) {   // new stations change the heat layer too
        m.dirty = false;
        blit();
    }
    if (m.markersDirty || newData) rebuildMarkers();
    updateInfo();
}

void keyCb(lv_event_t *e) {
    const uint32_t k = lv_event_get_key(e);
    switch (k) {
        case LV_KEY_LEFT:  pan(-kPanStep, 0); break;
        case LV_KEY_RIGHT: pan(kPanStep, 0); break;
        case LV_KEY_UP:    pan(0, -kPanStep); break;
        case LV_KEY_DOWN:  pan(0, kPanStep); break;
        case '+': case '=': case 'i': case 'I': setZoom(s_map->zoom + 1); break;
        case '-': case '_': case 'o': case 'O': setZoom(s_map->zoom - 1); break;
        case 'n': case 'N': selectNext(); break;
        case 'c': case 'C': centerOnMe(); break;
        default: return;
    }
    if (k == LV_KEY_LEFT || k == LV_KEY_RIGHT || k == LV_KEY_UP || k == LV_KEY_DOWN)
        s_map->selected[0] = 0;
    lv_timer_ready(s_map->timer);
}

#if HAS_TOUCH
bool s_dragged = false;

void pressedCb(lv_event_t *) { s_dragged = false; }

void pressingCb(lv_event_t *) {
    lv_indev_t *in = lv_indev_active();
    if (!in || lv_indev_get_type(in) != LV_INDEV_TYPE_POINTER) return;
    lv_point_t v;
    lv_indev_get_vect(in, &v);
    if (!v.x && !v.y) return;
    s_dragged = true;
    s_map->selected[0] = 0;
    pan(-v.x, -v.y);
    lv_timer_ready(s_map->timer);
}

// A tap selects the nearest station; tapping the selected one opens it.
bool tapAt(int px, int py) {
    Map &m = *s_map;
    const Station *best = nullptr;
    int bestD = 28 * 28;
    for (size_t i = 0; i < stationDbCount(); i++) {
        const Station *s = stationDbAt(i);
        int sx, sy;
        if (!s || !s->hasPos || !toScreen(s->lat, s->lon, sx, sy)) continue;
        const int d = (sx - px) * (sx - px) + (sy - py) * (sy - py);
        if (d < bestD) {
            bestD = d;
            best = s;
        }
    }
    if (!best) {
        m.selected[0] = 0;
    } else if (strcmp(best->call, m.selected) == 0) {
        if (m.onStation) m.onStation(m.selected);
    } else {
        strlcpy(m.selected, best->call, sizeof(m.selected));
    }
    m.markersDirty = true;
    lv_timer_ready(m.timer);
    return true;
}

lv_obj_t *mapButton(lv_obj_t *parent, const char *sym, lv_event_cb_t cb) {
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 40, 36);
    lv_obj_set_style_bg_opa(b, LV_OPA_80, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, sym);
    lv_obj_center(l);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);
    return b;
}
#endif

void clickCb(lv_event_t *) {
    Map &m = *s_map;
#if HAS_TOUCH
    lv_indev_t *in = lv_indev_active();
    if (in && lv_indev_get_type(in) == LV_INDEV_TYPE_POINTER) {
        if (s_dragged) return;
        lv_point_t p;
        lv_indev_get_point(in, &p);
        lv_area_t a;
        lv_obj_get_coords(m.root, &a);
        tapAt(p.x - a.x1, p.y - a.y1);
        return;
    }
#endif
    if (m.selected[0] && m.onStation) m.onStation(m.selected);
    else if (!centerOnMe()) selectNext();
    lv_timer_ready(m.timer);
}

void deleteCb(lv_event_t *) {
    Map *m = s_map;
    if (!m) return;
    // Remember the view as lat/lon so it survives zoom changes.
    s_haveView = true;
    s_lastZoom = m->zoom;
    s_lastLat = geo::worldYToLat(m->cy, m->zoom);
    s_lastLon = geo::worldXToLon(m->cx, m->zoom);
    lv_timer_delete(m->timer);
    for (auto &s : m->slots) heap_caps_free(s.px);
    heap_caps_free(m->buf);
    heap_caps_free(m->heat);
    delete m;
    s_map = nullptr;
}

}  // namespace

void mapViewCreate(lv_obj_t *parent, lv_group_t *grp, void (*onStation)(const char *call)) {
    if (s_map) return;
    lv_obj_update_layout(parent);
    const int w = lv_obj_get_content_width(parent);
    const int h = lv_obj_get_content_height(parent);

    Map *m = new Map();
    memset(m->slots, 0, sizeof(m->slots));
    m->w = w;
    m->h = h;
    m->onStation = onStation;
    m->buf = (uint16_t *)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM);
    lv_obj_t *root = lv_obj_create(parent);
    m->root = root;
    if (!m->buf || !jobBuffersReady()) {
        lv_obj_t *l = lv_label_create(root);
        lv_label_set_text(l, "Not enough memory for the map.");
        heap_caps_free(m->buf);
        delete m;
        return;
    }
    s_map = m;
    // Without it the map still works, just with dots and no heat.
    m->heat = (uint8_t *)heap_caps_malloc((size_t)w * h, MALLOC_CAP_SPIRAM);

    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_USER_1);   // keyboard: deliver UP/DOWN as arrows
    lv_obj_add_event_cb(root, keyCb, LV_EVENT_KEY, nullptr);
    lv_obj_add_event_cb(root, clickCb, LV_EVENT_CLICKED, nullptr);
#if HAS_TOUCH
    lv_obj_add_event_cb(root, pressedCb, LV_EVENT_PRESSED, nullptr);
    lv_obj_add_event_cb(root, pressingCb, LV_EVENT_PRESSING, nullptr);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLL_CHAIN);
#endif
    lv_obj_add_event_cb(root, deleteCb, LV_EVENT_DELETE, nullptr);
    lv_group_add_obj(grp, root);

    m->canvas = lv_canvas_create(root);
    lv_canvas_set_buffer(m->canvas, m->buf, w, h, LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(m->canvas, 0, 0);

    m->markers = lv_obj_create(root);
    lv_obj_remove_style_all(m->markers);
    lv_obj_set_size(m->markers, w, h);
    lv_obj_clear_flag(m->markers, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    m->info = lv_label_create(root);
    lv_obj_set_width(m->info, w);
    lv_obj_align(m->info, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_text_font(m->info, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(m->info, lv_color_white(), 0);
    lv_obj_set_style_bg_color(m->info, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(m->info, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(m->info, 4, 0);
    lv_label_set_long_mode(m->info, LV_LABEL_LONG_CLIP);

#if HAS_TOUCH
    lv_obj_t *col = lv_obj_create(root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 6, 0);
    lv_obj_align(col, LV_ALIGN_TOP_RIGHT, -4, 4);
    mapButton(col, LV_SYMBOL_PLUS, [](lv_event_t *) {
        setZoom(s_map->zoom + 1);
        lv_timer_ready(s_map->timer);
    });
    mapButton(col, LV_SYMBOL_MINUS, [](lv_event_t *) {
        setZoom(s_map->zoom - 1);
        lv_timer_ready(s_map->timer);
    });
    mapButton(col, LV_SYMBOL_GPS, [](lv_event_t *) {
        centerOnMe();
        lv_timer_ready(s_map->timer);
    });
#endif

    // A job left over from a previous visit is discarded (its tile may be off-screen).
    if (s_job.state == JobState::Done) s_job.state = JobState::Idle;

    double lat, lon;
    if (s_haveView) {
        m->zoom = s_lastZoom;
        setCenter(s_lastLat, s_lastLon);
    } else if (myPosition(lat, lon)) {
        m->zoom = 12;
        setCenter(lat, lon);
    } else {
        m->zoom = 4;
        setCenter(39.8, -98.6);
        for (size_t i = 0; i < stationDbCount(); i++) {
            const Station *s = stationDbAt(i);
            if (s && s->hasPos) {
                m->zoom = 11;
                setCenter(s->lat, s->lon);
                break;
            }
        }
    }
    m->wifi = wifiStaConnected();
    m->dataGen = g_dataGen;
    m->timer = lv_timer_create(timerCb, 40, nullptr);
    lv_timer_ready(m->timer);
}
