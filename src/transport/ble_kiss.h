#pragma once
#include <Arduino.h>
#include <vector>
#include "transport.h"

enum class BleState : uint8_t {
    Off,          // no TNC configured
    Idle,         // configured but auto-connect disabled / user disconnected
    Waiting,      // backoff before the next connect attempt
    Connecting,
    Connected,
};

struct BleScanResult {
    String address;
    uint8_t addressType;
    String name;
    int8_t rssi;
    bool kissService;   // advertises the configured KISS service UUID
};

struct BleStatus {
    BleState state;
    bool scanning;
    String peerName;
    String peerAddress;
    String lastError;
    uint16_t mtu;
    uint32_t rxFrames, txFrames;
    uint32_t nextAttemptInS;   // when Waiting
};

class BleKissTransport : public PacketTransport {
public:
    void begin();
    const char *name() const override { return "BLE KISS"; }
    bool connected() const override;
    bool sendAx25(const uint8_t *data, size_t len) override;
    bool receive(RxFrame &out) override;

    // Commands; all are asynchronous and safe to call from the main loop.
    void startScan(uint32_t seconds = 8);
    std::vector<BleScanResult> scanResults();
    // Re-reads g_config (address/UUIDs) and reconnects if appropriate.
    void reconfigure();
    void connectNow();
    void disconnect();   // stays disconnected until connectNow()/reconfigure()

    BleStatus status();
};

extern BleKissTransport g_ble;

const char *bleStateName(BleState s);
