#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class BeaconMode : uint8_t { Off = 0, Gps = 1, Fixed = 2 };
enum class Units : uint8_t { Imperial = 0, Metric = 1 };

// Alert tone style. Off silences every alert, not just new messages.
enum class AlertSound : uint8_t { Default, Chirpy, Bass, Off };
constexpr uint8_t kAlertSoundCount = 4;
const char *alertSoundKey(AlertSound s);   // JSON value: "default", "chirpy", ...
enum class UiTheme : uint8_t { Dark, Light };
// Screen orientation, on boards that can rotate (CrowPanel, Wio Tracker L2).
enum class Orientation : uint8_t { Landscape, Landscape180, Portrait, Portrait180 };
const char *orientationKey(Orientation o);     // "landscape", "landscape180", ...
const char *orientationLabel(Orientation o);   // "Landscape", "Landscape 180", ...
// UI text size; Medium is the original size.
enum class TextSize : uint8_t { Small, Medium, Large, XLarge };
const char *textSizeKey(TextSize t);     // "small", "medium", "large", "xlarge"
const char *textSizeLabel(TextSize t);   // "Small", "Medium", "Large", "X-Large"
// New-message light colour, on boards with an RGB status LED (Mesh Deck).
enum class LedColor : uint8_t { Off, Red, Green, Blue, Yellow, Cyan, Magenta, White };
constexpr uint8_t kLedColorCount = 8;
const char *ledColorKey(LedColor c);   // JSON value: "off", "red", ...
// Button (accent) colour presets, offered on the device and in the web config;
// the web config also takes any hex colour. Presets pick the text colour that
// reads best on them; a custom colour comes with the user's choice.
struct AccentPreset {
    const char *name;
    uint32_t rgb;
};
extern const AccentPreset kAccentPresets[];
extern const int kAccentPresetCount;
constexpr uint32_t kAccentDefault = 0xD96C00;   // orange
bool accentPrefersDarkText(uint32_t rgb);   // dark text has more contrast than light
const char *accentLabel(uint32_t rgb);      // preset name, or "Custom"
// Blink timeout choices in seconds; 0 = never stop while messages are unread.
extern const uint16_t kLedTimeoutSecs[];
extern const int kLedTimeoutCount;

struct SmartBeaconConfig {
    bool enabled = true;
    uint16_t slowRateS = 1800;
    uint16_t fastRateS = 90;
    uint16_t slowSpeedKmh = 5;
    uint16_t fastSpeedKmh = 90;
    uint16_t minTurnDeg = 28;
    uint16_t turnSlope = 255;
    uint16_t minTurnTimeS = 15;
};

enum class RfLink : uint8_t { Ble, Lora, Off };

struct AppConfig {
    // Station
    String callsign = "N0CALL";
    uint8_t ssid = 9;
    char symbolTable = '/';
    char symbolCode = '>';
    String comment = "Platy APRS";
    String path = "WIDE1-1,WIDE2-1";

    // Beaconing
    BeaconMode beaconMode = BeaconMode::Gps;
    double fixedLat = 0.0;
    double fixedLon = 0.0;
    int32_t fixedAltM = 0;
    uint16_t beaconIntervalS = 600;
    SmartBeaconConfig smart;
    bool gpsEnabled = true;    // GPS receiver powered (needed for beacon mode GPS)

    // APRS-IS / iGate / digi
    bool aprsIsEnabled = false;
    String aprsIsServer = "rotate.aprs2.net";
    uint16_t aprsIsPort = 14580;
    int32_t aprsIsPasscode = -1;   // -1 = compute from callsign
    String aprsIsFilter = "m/50";
    bool igateRfToIs = true;
    bool igateIsToRfMsgs = false;
    bool digiEnabled = false;

    // RF link: the BLE KISS TNC or the built-in LoRa radio (one at a time), or neither
    RfLink rfLink = RfLink::Ble;
    float loraFreqMhz = 433.775f;   // LoRa APRS (OE5BPA): 433.775 MHz SF12 BW125 CR4/5
    uint8_t loraSf = 12;
    float loraBwKhz = 125.0f;
    uint8_t loraCr = 5;             // 4/5 .. 4/8
    int8_t loraPowerDbm = 20;

    // TNC (BLE KISS)
    String tncName;
    String tncAddress;
    uint8_t tncAddressType = 0;   // BLE_ADDR_PUBLIC=0, RANDOM=1
    String tncServiceUuid = "00000001-ba2a-46c9-ae49-01b0961f68bb";
    String tncWriteUuid = "00000002-ba2a-46c9-ae49-01b0961f68bb";
    String tncNotifyUuid = "00000003-ba2a-46c9-ae49-01b0961f68bb";
    bool tncAutoConnect = false;   // connect at boot and reconnect when the link drops

    // Wi-Fi
    bool wifiEnabled = true;   // off: no network, no setup AP (the web config is unreachable)
    String wifiSsid;
    String wifiPassword;
    String hostname = "platy-aprs";

    // Device
    Units units = Units::Imperial;
    AlertSound alertSound = AlertSound::Default;
    uint8_t volumePct = 50;   // 0-100 in steps of 10; speaker boards only
    bool sdLog = true;      // packet log files on the SD card
    bool sdLogIs = false;   // include packets received from APRS-IS
    uint8_t brightness = 160;
    uint32_t screenOnSecs = 30;   // screen timeout; 0 = never
    UiTheme theme = UiTheme::Dark;
    Orientation orientation = Orientation::Landscape;
    TextSize textSize = TextSize::Medium;
    uint32_t accentColor = kAccentDefault;   // buttons (0xRRGGBB)
    bool accentLightText = false;            // white text on them, else black
    LedColor ledColor = LedColor::Blue;   // new-message blink; LED boards only
    uint16_t ledTimeoutSecs = 15;         // stop blinking after this; 0 = never (LED and keyboard)
    bool kbBlink = true;                  // T-Deck keyboard backlight blink, screen off
    bool screenSaver = true;      // at the timeout: screensaver (true) or screen off
    bool otaAutoInstall = false;  // install a newer release without asking
    String timezone = "EST5EDT,M3.2.0,M11.1.0";
    String adminPassword;

    // Favorite callsigns (stations and conversations), comma-separated.
    String favorites;

    // Canned texts offered when composing a message.
    static constexpr size_t kQuickMsgCount = 5;
    String quickMsgs[kQuickMsgCount] = {"On my way", "Running late", "Copy, thanks", "Call me when you can", "73!"};

    String fullCallsign() const;
    int32_t effectivePasscode() const;
};

extern AppConfig g_config;

bool configBegin();   // mounts LittleFS and loads /config.json
// True when there was no saved config at boot (new or erased flash).
bool configIsFresh();
bool configSave();
void configResetToDefaults();

// JSON round-trip used by the web API. Secrets (passwords) are never emitted;
// on input an absent or empty secret field leaves the stored value unchanged.
void configToJson(const AppConfig &c, JsonObject out);
void configFromJson(AppConfig &c, JsonObjectConst in);
// Returns an empty string when valid, else a human-readable reason.
String configValidate(const AppConfig &c);
