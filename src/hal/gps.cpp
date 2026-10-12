#include "gps.h"
#include "pins_m9.h"
#include <TinyGPSPlus.h>
#include <sys/time.h>
#include <time.h>

static TinyGPSPlus s_gps;
static bool s_on = false;
static uint32_t s_lastCharsMs = 0;
static uint32_t s_lastChars = 0;
static bool s_clockSet = false;

void gpsSetEnabled(bool on) {
    if (on == s_on) return;
    s_on = on;
    if (on) {
        digitalWrite(PIN_GPS_RST, LOW);   // released
        digitalWrite(PIN_GPS_EN, LOW);    // powered
        Serial1.setRxBufferSize(2048);
        Serial1.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
        Serial.println("[gps] on");
    } else {
        Serial1.end();
        digitalWrite(PIN_GPS_EN, HIGH);
        Serial.println("[gps] off");
    }
}

void gpsBegin() {
    pinMode(PIN_GPS_EN, OUTPUT);
    pinMode(PIN_GPS_RST, OUTPUT);
    digitalWrite(PIN_GPS_RST, LOW);
    digitalWrite(PIN_GPS_EN, HIGH);
}

// Set the system clock from GPS when NTP hasn't (no Wi-Fi in the field).
static void maybeSetClock() {
    if (s_clockSet || !s_gps.date.isValid() || !s_gps.time.isValid() || s_gps.date.year() < 2024) return;
    if (time(nullptr) > 1700000000) {
        s_clockSet = true;   // already set (NTP)
        return;
    }
    struct tm tm = {};
    tm.tm_year = s_gps.date.year() - 1900;
    tm.tm_mon = s_gps.date.month() - 1;
    tm.tm_mday = s_gps.date.day();
    tm.tm_hour = s_gps.time.hour();
    tm.tm_min = s_gps.time.minute();
    tm.tm_sec = s_gps.time.second();
    // timegm via a UTC-interpreting mktime: compute against the TZ offset.
    const char *tz = getenv("TZ");
    String saved = tz ? tz : "";
    setenv("TZ", "UTC0", 1);
    tzset();
    const time_t t = mktime(&tm);
    if (saved.length()) setenv("TZ", saved.c_str(), 1);
    else unsetenv("TZ");
    tzset();
    struct timeval tv = {t, 0};
    settimeofday(&tv, nullptr);
    s_clockSet = true;
    Serial.println("[gps] clock set from GPS");
}

void gpsLoop() {
    if (!s_on) return;
    int n = 0;
    while (Serial1.available() && n++ < 1024) s_gps.encode(Serial1.read());
    if (s_gps.charsProcessed() != s_lastChars) {
        s_lastChars = s_gps.charsProcessed();
        s_lastCharsMs = millis();
    }
    maybeSetClock();
}

GpsFix gpsFix() {
    GpsFix f = {};
    f.enabled = s_on;
    if (!s_on) return f;
    f.alive = s_lastCharsMs && millis() - s_lastCharsMs < 5000;
    f.sats = s_gps.satellites.isValid() ? s_gps.satellites.value() : 0;
    f.hdop = s_gps.hdop.isValid() ? s_gps.hdop.hdop() : 0;
    f.fixAgeMs = s_gps.location.age();
    f.valid = f.alive && s_gps.location.isValid() && f.fixAgeMs < 5000;
    if (!f.valid) return f;
    f.lat = s_gps.location.lat();
    f.lon = s_gps.location.lng();
    f.hasAlt = s_gps.altitude.isValid();
    f.altM = s_gps.altitude.meters();
    f.speedKmh = s_gps.speed.isValid() ? s_gps.speed.kmph() : 0;
    f.course = s_gps.course.isValid() ? (int)s_gps.course.deg() : 0;
    return f;
}
