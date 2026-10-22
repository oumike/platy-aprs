#include "lora_aprs.h"

#include <RadioLib.h>
#include <SPI.h>

#include "aprs/lora_frame.h"
#include "ax25/ax25.h"
#include "ble_kiss.h"
#include "config/config.h"
#include "hal/board_pins.h"
#include "services/data_store.h"

LoraAprsTransport g_lora;

namespace {

#if LORA_CHIP_LR1110
// RadioLib keeps the raw RX-buffer accessors and packet params protected; two
// LR11x0 quirks need them (see readPacket() and armRx()). Same workaround as
// camillia-mt's MeshLR1110.
class Radio : public LR1110 {
public:
    explicit Radio(Module *m) : LR1110(m) {}
    using LR11x0::clearRxBuffer;
    using LR11x0::readBuffer8;
    int16_t restoreRxMaxPayload() {
        return setPacketParamsLoRa(preambleLengthLoRa, headerType, RADIOLIB_LR11X0_MAX_PACKET_LENGTH,
                                   crcTypeLoRa, (uint8_t)invertIQEnabled);
    }
};
#else
using Radio = SX1262;
#endif

constexpr uint8_t kSyncWord = 0x12;      // LoRa "private" sync word, used by LoRa APRS
constexpr uint16_t kPreamble = 8;
constexpr uint32_t kTxTimeoutMs = 20000;
constexpr uint32_t kBusyHoldMs = 4000;   // max wait for an incoming packet before we transmit anyway

Radio *s_radio = nullptr;
volatile bool s_irq = false;
bool s_wantReconfig = false;
bool s_active = false;

LoraStatus s_status{LoraState::Off, "", 0, 0, 0, 0, 0, 0, 0};
uint32_t s_txStartMs = 0;
uint32_t s_busySinceMs = 0;

// Received frames (AX.25) waiting for the router.
constexpr int kRxQ = 4;
RxFrame s_rxq[kRxQ];
int s_rxHead = 0, s_rxTail = 0;

// TNC2 lines waiting to go out.
constexpr int kTxQ = 4;
struct TxLine { uint16_t len; uint8_t data[256]; };
TxLine s_txq[kTxQ];
int s_txHead = 0, s_txTail = 0;

void IRAM_ATTR onIrq() { s_irq = true; }

void fail(const char *what, int code) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s (%d)", what, code);
    s_status.lastError = buf;
    s_status.state = LoraState::Failed;
    Serial.printf("[lora] %s\n", buf);
}

int armRx() {
    // The LR11x0 uses the payload-length packet param as the RX maximum too, and
    // RadioLib leaves it at the last TX length, so restore it before every RX.
    s_irq = false;
#if LORA_CHIP_LR1110
    s_radio->restoreRxMaxPayload();
    const int st = s_radio->startReceive();
#else
    // Latch preamble detection too (DIO1 still fires only on RX done) so
    // startTx() can see a packet arriving.
    const int st = s_radio->startReceive(RADIOLIB_SX126X_RX_TIMEOUT_INF,
                                         RADIOLIB_IRQ_RX_DEFAULT_FLAGS | (1UL << RADIOLIB_IRQ_PREAMBLE_DETECTED),
                                         RADIOLIB_IRQ_RX_DEFAULT_MASK, 0);
#endif
    if (st == RADIOLIB_ERR_NONE) s_status.state = LoraState::Rx;
    else fail("startReceive failed", st);
    s_busySinceMs = 0;
    return st;
}

void stopRadio() {
    if (s_radio && s_active) {
        s_radio->standby();
        s_radio->sleep();
    }
    s_active = false;
    s_status.state = LoraState::Off;
    s_rxHead = s_rxTail = s_txHead = s_txTail = 0;
}

bool startRadio() {
    const AppConfig &c = g_config;
    if (!s_radio) {
        SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI);
        // Casts: on the CrowPanel CS is GPIO0, and a literal 0 also matches the
        // RadioLibHal* overload.
        s_radio = new Radio(new Module((uint32_t)PIN_LORA_CS, (uint32_t)PIN_LORA_DIO1,
                                       (uint32_t)PIN_LORA_RST, (uint32_t)PIN_LORA_BUSY));
    }
    s_radio->reset();
    int st = s_radio->begin(c.loraFreqMhz, c.loraBwKhz, c.loraSf, c.loraCr, kSyncWord, c.loraPowerDbm,
                            kPreamble, LORA_TCXO_V);
    if (st != RADIOLIB_ERR_NONE) {
        fail("radio init failed", st);
        return false;
    }
#if LORA_CHIP_LR1110
    // The antenna switch is driven by the LR1110's DIO5/DIO6 and only after it
    // has been given a table (schematic-confirmed in camillia-mt's hw_m9.h).
    static const uint32_t kPins[Module::RFSWITCH_MAX_PINS] = {
        RADIOLIB_LR11X0_DIO5, RADIOLIB_LR11X0_DIO6, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
    static const Module::RfSwitchMode_t kTable[] = {
        {LR11x0::MODE_STBY, {LOW, LOW}},   {LR11x0::MODE_RX, {HIGH, LOW}},
        {LR11x0::MODE_TX, {HIGH, HIGH}},   {LR11x0::MODE_TX_HP, {LOW, HIGH}},
        {LR11x0::MODE_TX_HF, {LOW, LOW}},  {LR11x0::MODE_GNSS, {LOW, LOW}},
        {LR11x0::MODE_WIFI, {LOW, LOW}},   END_OF_MODE_TABLE,
    };
    s_radio->setRfSwitchTable(kPins, kTable);
    s_radio->setIrqAction(onIrq);
#else
    s_radio->setDio2AsRfSwitch(true);
    s_radio->setCurrentLimit(140);
    s_radio->setDio1Action(onIrq);
#endif
    s_radio->setOutputPower(c.loraPowerDbm);
    s_radio->setCRC(2);
    s_radio->setRxBoostedGainMode(true);
    s_active = true;
    s_status.lastError = "";
    s_status.freqMhz = c.loraFreqMhz;
    if (armRx() != RADIOLIB_ERR_NONE) return false;
    Serial.printf("[lora] ready %.3f MHz SF%u BW%.1f CR4/%u %d dBm\n", c.loraFreqMhz, c.loraSf,
                  c.loraBwKhz, c.loraCr, c.loraPowerDbm);
    return true;
}

#if LORA_CHIP_LR1110
// Reads one packet. The LR11x0 RX buffer is a 256-byte ring whose start offset
// creeps forward with every packet; RadioLib's readData() does not handle the
// wrap, so read the two halves ourselves.
bool readPacket(uint8_t *buf, size_t cap, size_t &len) {
    uint8_t offset = 0;
    len = s_radio->getPacketLength(true, &offset);
    const uint32_t irq = s_radio->getIrqFlags();
    const bool crcBad = (irq & RADIOLIB_LR11X0_IRQ_CRC_ERR) ||
                        ((irq & RADIOLIB_LR11X0_IRQ_HEADER_ERR) &&
                         !(irq & RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID));
    bool ok = len > 0 && len <= cap && !crcBad;
    if (ok) {
        const size_t first = (offset + len > 256) ? (256 - offset) : len;
        ok = s_radio->readBuffer8(buf, first, offset) == RADIOLIB_ERR_NONE;
        if (ok && first < len) ok = s_radio->readBuffer8(buf + first, len - first, 0) == RADIOLIB_ERR_NONE;
    }
    s_radio->clearRxBuffer();
    s_radio->clearIrqFlags(RADIOLIB_LR11X0_IRQ_ALL);
    return ok;
}

bool rxInProgress() {
    const uint32_t irq = s_radio->getIrqFlags();
    return irq & (RADIOLIB_LR11X0_IRQ_PREAMBLE_DETECTED | RADIOLIB_LR11X0_IRQ_SYNC_WORD_HEADER_VALID);
}

void clearIrqs() { s_radio->clearIrqFlags(RADIOLIB_LR11X0_IRQ_ALL); }
#else
bool readPacket(uint8_t *buf, size_t cap, size_t &len) {
    len = s_radio->getPacketLength(true);
    if (len == 0 || len > cap) {
        s_radio->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL);
        return false;
    }
    return s_radio->readData(buf, len) == RADIOLIB_ERR_NONE;   // checks CRC, clears IRQs
}

bool rxInProgress() {
    const uint32_t irq = s_radio->getIrqFlags();
    return irq & (RADIOLIB_SX126X_IRQ_PREAMBLE_DETECTED | RADIOLIB_SX126X_IRQ_HEADER_VALID);
}

void clearIrqs() { s_radio->clearIrqFlags(RADIOLIB_SX126X_IRQ_ALL); }
#endif

void handleRx() {
    uint8_t buf[256];
    size_t len = 0;
    if (!readPacket(buf, sizeof(buf), len)) {
        s_status.rxBad++;
        // A corrupted packet can leave the receiver shifted until it has been
        // through standby (seen in MeshCore and camillia-mt).
        s_radio->standby();
        armRx();
        return;
    }
    const float rssi = s_radio->getRSSI(), snr = s_radio->getSNR();
    armRx();

    std::string tnc2;
    ax25::Frame f;
    if (!lora_frame::decode(buf, len, tnc2) || !ax25::fromTnc2(tnc2, f)) {
        s_status.rxBad++;
        Serial.printf("[lora] rx %u bytes not APRS/AX.25-representable (rssi %.0f)\n", (unsigned)len, rssi);
        return;
    }
    const std::vector<uint8_t> bin = ax25::encode(f);
    const int next = (s_rxHead + 1) % kRxQ;
    if (bin.empty() || bin.size() > sizeof(RxFrame::data) || next == s_rxTail) return;
    s_rxq[s_rxHead].len = (uint16_t)bin.size();
    memcpy(s_rxq[s_rxHead].data, bin.data(), bin.size());
    s_rxHead = next;
    s_status.rxFrames++;
    s_status.lastRssi = rssi;
    s_status.lastSnr = snr;
    s_status.lastRxS = uptimeS();
    Serial.printf("[lora] rx rssi %.0f snr %.1f: %s\n", rssi, snr, tnc2.c_str());
}

void startTx() {
    // Hold off while a packet is being received; the IRQ status latches preamble
    // and header detection even though only RX/TX-done drive DIO.
    if (rxInProgress()) {
        if (!s_busySinceMs) s_busySinceMs = millis() | 1;
        if (millis() - s_busySinceMs < kBusyHoldMs) return;
        clearIrqs();
    }
    const TxLine &t = s_txq[s_txTail];
    s_irq = false;
    const int st = s_radio->startTransmit(const_cast<uint8_t *>(t.data), t.len);
    s_txTail = (s_txTail + 1) % kTxQ;
    if (st != RADIOLIB_ERR_NONE) {
        Serial.printf("[lora] startTransmit failed %d\n", st);
        armRx();
        return;
    }
    s_status.state = LoraState::Tx;
    s_txStartMs = millis();
}

}  // namespace

const char *loraStateName(LoraState s) {
    switch (s) {
        case LoraState::Off: return "off";
        case LoraState::Failed: return "failed";
        case LoraState::Rx: return "listening";
        case LoraState::Tx: return "transmitting";
    }
    return "?";
}

PacketTransport &rfLink() {
    if (g_config.rfLink == RfLink::Lora) return g_lora;
    return g_ble;
}

void LoraAprsTransport::begin() { reconfigure(); }

void LoraAprsTransport::reconfigure() { s_wantReconfig = true; }

void LoraAprsTransport::loop() {
    if (s_wantReconfig) {
        s_wantReconfig = false;
        stopRadio();
        if (g_config.rfLink == RfLink::Lora) startRadio();
    }
    if (!s_active || s_status.state == LoraState::Failed) return;

    if (s_status.state == LoraState::Tx) {
        if (s_irq) {
            s_irq = false;
            s_radio->finishTransmit();
            s_status.txFrames++;
            Serial.printf("[lora] tx done in %lu ms\n", (unsigned long)(millis() - s_txStartMs));
            armRx();
        } else if (millis() - s_txStartMs > kTxTimeoutMs) {
            Serial.println("[lora] tx timeout");
            s_radio->standby();
            armRx();
        }
        return;
    }
    if (s_irq) {
        s_irq = false;
        handleRx();
    }
    if (s_status.state == LoraState::Rx && s_txTail != s_txHead) startTx();
}

bool LoraAprsTransport::connected() const {
    return s_active && (s_status.state == LoraState::Rx || s_status.state == LoraState::Tx);
}

bool LoraAprsTransport::sendAx25(const uint8_t *data, size_t len) {
    if (!connected()) return false;
    ax25::Frame f;
    if (!ax25::decode(data, len, f)) return false;
    const std::string pkt = lora_frame::encode(ax25::toTnc2(f));
    const int next = (s_txHead + 1) % kTxQ;
    if (pkt.size() > sizeof(TxLine::data) || next == s_txTail) return false;
    s_txq[s_txHead].len = (uint16_t)pkt.size();
    memcpy(s_txq[s_txHead].data, pkt.data(), pkt.size());
    s_txHead = next;
    return true;
}

bool LoraAprsTransport::receive(RxFrame &out) {
    if (s_rxTail == s_rxHead) return false;
    out = s_rxq[s_rxTail];
    s_rxTail = (s_rxTail + 1) % kRxQ;
    return true;
}

LoraStatus LoraAprsTransport::status() const { return s_status; }
