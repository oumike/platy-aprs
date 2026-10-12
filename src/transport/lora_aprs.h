#pragma once
#include <Arduino.h>
#include "transport.h"

enum class LoraState : uint8_t { Off, Failed, Rx, Tx };

struct LoraStatus {
    LoraState state;
    String lastError;
    float freqMhz;
    uint32_t rxFrames, txFrames, rxBad;
    float lastRssi, lastSnr;    // of the last good packet
    uint32_t lastRxS;           // uptime seconds, 0 = never
};

// LoRa APRS on the M9's LR1110. Active only when g_config.rfLink is LoRa.
// Everything (including SPI, which is shared with the LCD and SD card) runs in
// the loop task via loop(); the radio IRQ only sets a flag. Transmit is
// non-blocking: an SF12 packet is on air for several seconds.
class LoraAprsTransport : public PacketTransport {
public:
    void begin();
    void loop();
    // Re-reads g_config: starts, retunes or stops the radio.
    void reconfigure();
    const char *name() const override { return "LoRa"; }
    bool connected() const override;
    bool sendAx25(const uint8_t *data, size_t len) override;
    bool receive(RxFrame &out) override;
    LoraStatus status() const;
};

extern LoraAprsTransport g_lora;

const char *loraStateName(LoraState s);

// The transport selected by g_config.rfLink.
PacketTransport &rfLink();
