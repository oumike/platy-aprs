#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>

enum class BeaconMode : uint8_t { Off = 0, Gps = 1, Fixed = 2 };
enum class Units : uint8_t { Imperial = 0, Metric = 1 };
// What the compact panel on the home screen shows.
enum class HomePanel : uint8_t { LastHeard = 0, LastMessage = 1, LastPacket = 2, None = 3 };
const char *homePanelKey(HomePanel p);    // JSON value, e.g. "lastHeard"
const char *homePanelName(HomePanel p);   // display name, e.g. "Last heard station"

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

enum class RfLink : uint8_t { Ble, Lora };

struct AppConfig {
    // Station
    String callsign = "N0CALL";
    uint8_t ssid = 9;
    char symbolTable = '/';
    char symbolCode = '>';
    String comment = "rhino-aprs";
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

    // RF link: the BLE KISS TNC or the built-in LoRa radio (one at a time)
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
    bool tncAutoConnect = true;

    // Wi-Fi
    String wifiSsid;
    String wifiPassword;
    String hostname = "rhino-aprs";

    // Device
    Units units = Units::Imperial;
    HomePanel homePanel = HomePanel::LastHeard;
    bool buzzer = true;
    bool sdLog = true;      // packet log files on the SD card
    bool sdLogIs = false;   // include packets received from APRS-IS
    uint8_t brightness = 160;
    String timezone = "EST5EDT,M3.2.0,M11.1.0";
    String adminPassword;

    String fullCallsign() const;
    int32_t effectivePasscode() const;
};

extern AppConfig g_config;

bool configBegin();   // mounts LittleFS and loads /config.json
bool configSave();
void configResetToDefaults();

// JSON round-trip used by the web API. Secrets (passwords) are never emitted;
// on input an absent or empty secret field leaves the stored value unchanged.
void configToJson(const AppConfig &c, JsonObject out);
void configFromJson(AppConfig &c, JsonObjectConst in);
// Returns an empty string when valid, else a human-readable reason.
String configValidate(const AppConfig &c);
