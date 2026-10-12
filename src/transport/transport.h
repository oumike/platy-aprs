#pragma once
#include <stddef.h>
#include <stdint.h>

// Radio-agnostic packet link. BLE KISS is the first implementation; a LoRa
// APRS transport can later provide the same interface.
struct RxFrame {
    uint16_t len;
    uint8_t data[400];   // raw AX.25 (no FCS)
};

class PacketTransport {
public:
    virtual ~PacketTransport() = default;
    virtual const char *name() const = 0;
    virtual bool connected() const = 0;
    // Queue an AX.25 frame for transmission. Returns false if the link is
    // down or the queue is full.
    virtual bool sendAx25(const uint8_t *data, size_t len) = 0;
    // Non-blocking; returns true and fills `out` when a frame is waiting.
    virtual bool receive(RxFrame &out) = 0;
};
