#include "ble_kiss.h"
#include <NimBLEDevice.h>
#include "config/config.h"
#include "kiss/kiss.h"

BleKissTransport g_ble;

namespace {

constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kBackoffMinS = 5;
constexpr uint32_t kBackoffMaxS = 120;

enum : uint32_t {
    CMD_SCAN = 1 << 0,
    CMD_RECONFIGURE = 1 << 1,
    CMD_CONNECT = 1 << 2,
    CMD_DISCONNECT = 1 << 3,
};

SemaphoreHandle_t s_mtx;
QueueHandle_t s_rxQ;
QueueHandle_t s_txQ;
TaskHandle_t s_task;
volatile uint32_t s_cmds;
uint32_t s_scanSeconds = 8;

// Shared state (guarded by s_mtx)
BleStatus s_status{BleState::Off, false, "", "", "", 0, 0, 0, 0};
std::vector<BleScanResult> s_results;

// Task-private state
NimBLEClient *s_client;
NimBLERemoteCharacteristic *s_writeChr;
bool s_writeNoRsp;
bool s_wantConnected;
uint32_t s_backoffS = kBackoffMinS;
uint32_t s_nextAttemptMs;
String s_addr, s_svcUuid, s_wrUuid, s_ntUuid, s_name;
uint8_t s_addrType;

template <typename F>
void withLock(F fn) {
    if (!s_mtx) {   // before begin(): nothing else is running yet
        fn();
        return;
    }
    xSemaphoreTake(s_mtx, portMAX_DELAY);
    fn();
    xSemaphoreGive(s_mtx);
}

kiss::Decoder s_decoder([](uint8_t, uint8_t cmd, const uint8_t *data, size_t len) {
    if (cmd != kiss::CMD_DATA || len < 16 || len > sizeof(RxFrame::data)) return;
    RxFrame f;
    f.len = len;
    memcpy(f.data, data, len);
    if (xQueueSend(s_rxQ, &f, 0) == pdTRUE) withLock([] { s_status.rxFrames++; });
});

void setState(BleState st, const String &err = String()) {
    withLock([&] {
        s_status.state = st;
        if (err.length() || st == BleState::Connected) s_status.lastError = err;
    });
}

class ScanCb : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice *d) override {
        // Unnamed devices are not useful for selection; KISS TNCs advertise a name.
        std::string name = d->getName();
        if (name.empty() || !d->isConnectable()) return;
        BleScanResult r;
        r.address = d->getAddress().toString().c_str();
        r.addressType = d->getAddress().getType();
        r.name = name.c_str();
        r.rssi = d->getRSSI();
        r.kissService = s_svcUuid.length() && d->isAdvertisingService(NimBLEUUID(s_svcUuid.c_str()));
        withLock([&] {
            for (auto &e : s_results) {
                if (e.address == r.address) {
                    e = r;
                    return;
                }
            }
            if (s_results.size() < 32) s_results.push_back(r);
        });
    }
    void onScanEnd(const NimBLEScanResults &, int) override {
        withLock([] { s_status.scanning = false; });
    }
} s_scanCb;

class ClientCb : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient *c) override { Serial.printf("[ble] link up (conn %u)\n", c->getConnHandle()); }
    void onConnectFail(NimBLEClient *, int reason) override { Serial.printf("[ble] connect fail %d\n", reason); }
    void onAuthenticationComplete(NimBLEConnInfo &info) override {
        Serial.printf("[ble] auth complete: encrypted=%d bonded=%d\n", info.isEncrypted(), info.isBonded());
    }
    void onDisconnect(NimBLEClient *, int reason) override {
        Serial.printf("[ble] disconnected (reason %d)\n", reason);
    }
    void onMTUChange(NimBLEClient *, uint16_t mtu) override {
        withLock([&] { s_status.mtu = mtu; });
    }
    void onPassKeyEntry(NimBLEConnInfo &info) override {
        // No keypad pairing support; most KISS TNCs use just-works.
        NimBLEDevice::injectPassKey(info, 0);
    }
} s_clientCb;

void loadConfig() {
    s_addr = g_config.tncAddress;
    s_addr.trim();
    s_addr.toLowerCase();
    s_addrType = g_config.tncAddressType;
    s_name = g_config.tncName;
    s_svcUuid = g_config.tncServiceUuid;
    s_wrUuid = g_config.tncWriteUuid;
    s_ntUuid = g_config.tncNotifyUuid;
    withLock([&] {
        s_status.peerAddress = s_addr;
        s_status.peerName = s_name;
    });
}

bool linkUp() { return s_client && s_client->isConnected() && s_writeChr; }

void dropLink() {
    s_writeChr = nullptr;
    if (s_client && s_client->isConnected()) s_client->disconnect();
}

void scheduleRetry(const String &err) {
    s_nextAttemptMs = millis() + s_backoffS * 1000;
    s_backoffS = min(s_backoffS * 2, kBackoffMaxS);
    setState(BleState::Waiting, err);
    Serial.printf("[ble] %s; retry in %lus\n", err.c_str(), (unsigned long)(s_nextAttemptMs - millis()) / 1000);
}

void tryConnect() {
    setState(BleState::Connecting);
    Serial.printf("[ble] connecting to %s (%s type %u)\n", s_name.c_str(), s_addr.c_str(), s_addrType);
    if (!s_client) {
        s_client = NimBLEDevice::createClient();
        s_client->setClientCallbacks(&s_clientCb, false);
        s_client->setConnectTimeout(kConnectTimeoutMs);
    }
    // exchangeMTU=false: some radios (VR-N7600) drop the link if MTU is requested during connect.
    if (!s_client->connect(NimBLEAddress(std::string(s_addr.c_str()), s_addrType), true, false, false)) {
        scheduleRetry("connect failed (" + String(NimBLEUtils::returnCodeToString(s_client->getLastError())) + ")");
        return;
    }
    NimBLERemoteService *svc = s_client->getService(s_svcUuid.c_str());
    NimBLERemoteCharacteristic *wr = svc ? svc->getCharacteristic(s_wrUuid.c_str()) : nullptr;
    NimBLERemoteCharacteristic *nt = svc ? svc->getCharacteristic(s_ntUuid.c_str()) : nullptr;
    if (!svc || !wr || !nt) {
        s_client->disconnect();
        scheduleRetry(!svc ? "KISS service not found" : "KISS characteristics not found");
        return;
    }
    s_decoder.reset();
    const bool useNotify = nt->canNotify();
    if (!nt->subscribe(useNotify, [](NimBLERemoteCharacteristic *, uint8_t *d, size_t n, bool) {
            s_decoder.feed(d, n);
        })) {
        s_client->disconnect();
        scheduleRetry("subscribe failed");
        return;
    }
    // Prefer acknowledged writes so the radio confirms each KISS frame.
    s_writeNoRsp = !wr->canWrite() && wr->canWriteNoResponse();
    Serial.printf("[ble] write char props: write=%d noRsp=%d -> %s\n", wr->canWrite(), wr->canWriteNoResponse(),
                  s_writeNoRsp ? "no-response" : "with-response");
    s_writeChr = wr;
    s_backoffS = kBackoffMinS;
    withLock([&] { s_status.mtu = s_client->getMTU(); });
    setState(BleState::Connected);
    Serial.printf("[ble] connected, MTU %u\n", s_client->getMTU());
}

void sendPending() {
    RxFrame f;
    while (xQueueReceive(s_txQ, &f, 0) == pdTRUE) {
        if (!linkUp()) continue;
        auto enc = kiss::encodeData(f.data, f.len, 0);
        const size_t chunk = max<int>(20, (int)s_client->getMTU() - 3);
        bool ok = true;
        for (size_t off = 0; ok && off < enc.size(); off += chunk) {
            ok = s_writeChr->writeValue(enc.data() + off, min(chunk, enc.size() - off), !s_writeNoRsp);
        }
        if (ok) withLock([] { s_status.txFrames++; });
        Serial.printf("[ble] tx %u bytes %s\n", (unsigned)enc.size(), ok ? "ok" : "FAILED");
    }
}

void task(void *) {
    loadConfig();
    s_wantConnected = g_config.tncAutoConnect && s_addr.length();
    s_nextAttemptMs = millis() + 1000;
    setState(s_addr.length() ? (s_wantConnected ? BleState::Waiting : BleState::Idle) : BleState::Off);

    NimBLEScan *scan = NimBLEDevice::getScan();
    scan->setScanCallbacks(&s_scanCb, false);
    scan->setActiveScan(true);
    scan->setInterval(100);
    scan->setWindow(80);

    for (;;) {
        const uint32_t cmds = __atomic_exchange_n(&s_cmds, 0, __ATOMIC_SEQ_CST);

        if (cmds & (CMD_RECONFIGURE | CMD_DISCONNECT | CMD_CONNECT)) {
            const String prevAddr = s_addr;
            const String prevSvc = s_svcUuid + s_wrUuid + s_ntUuid;
            loadConfig();
            if (cmds & CMD_DISCONNECT) s_wantConnected = false;
            else if (cmds & CMD_CONNECT) s_wantConnected = s_addr.length() > 0;
            else s_wantConnected = s_addr.length() && (g_config.tncAutoConnect || linkUp());

            const bool targetChanged = prevAddr != s_addr || prevSvc != s_svcUuid + s_wrUuid + s_ntUuid;
            if (linkUp() && (!s_wantConnected || targetChanged)) dropLink();
            if (!linkUp()) {
                s_backoffS = kBackoffMinS;
                s_nextAttemptMs = millis();
                setState(s_addr.length() ? (s_wantConnected ? BleState::Waiting : BleState::Idle) : BleState::Off);
            }
        }

        if (cmds & CMD_SCAN) {
            withLock([] {
                s_results.clear();
                s_status.scanning = true;
            });
            if (scan->isScanning()) scan->stop();
            scan->clearResults();
            if (!scan->start(s_scanSeconds * 1000, false, true)) {
                withLock([] { s_status.scanning = false; });
            }
        }

        // Detect link loss.
        if (s_writeChr && !(s_client && s_client->isConnected())) {
            s_writeChr = nullptr;
            if (s_wantConnected) scheduleRetry("link lost");
            else setState(BleState::Idle);
        }

        bool scanning;
        withLock([&] { scanning = s_status.scanning; });
        if (!linkUp() && s_wantConnected && !scanning && (int32_t)(millis() - s_nextAttemptMs) >= 0) {
            tryConnect();
        }

        sendPending();
        withLock([] {
            s_status.nextAttemptInS =
                s_status.state == BleState::Waiting && (int32_t)(s_nextAttemptMs - millis()) > 0
                    ? (s_nextAttemptMs - millis()) / 1000
                    : 0;
        });
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void post(uint32_t cmd) { __atomic_or_fetch(&s_cmds, cmd, __ATOMIC_SEQ_CST); }

}  // namespace

const char *bleStateName(BleState s) {
    switch (s) {
        case BleState::Off: return "not configured";
        case BleState::Idle: return "disconnected";
        case BleState::Waiting: return "waiting";
        case BleState::Connecting: return "connecting";
        case BleState::Connected: return "connected";
    }
    return "?";
}

void BleKissTransport::begin() {
    s_mtx = xSemaphoreCreateMutex();
    s_rxQ = xQueueCreate(12, sizeof(RxFrame));
    s_txQ = xQueueCreate(6, sizeof(RxFrame));
    NimBLEDevice::init(g_config.hostname.c_str());
    NimBLEDevice::setSecurityAuth(true, false, true);   // just-works bonding when the peer asks
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setMTU(247);
    xTaskCreatePinnedToCore(task, "ble_kiss", 6144, nullptr, 2, &s_task, 0);
}

bool BleKissTransport::connected() const {
    bool c;
    withLock([&] { c = s_status.state == BleState::Connected; });
    return c;
}

bool BleKissTransport::sendAx25(const uint8_t *data, size_t len) {
    if (!s_txQ || !connected() || len > sizeof(RxFrame::data)) return false;
    RxFrame f;
    f.len = len;
    memcpy(f.data, data, len);
    return xQueueSend(s_txQ, &f, 0) == pdTRUE;
}

bool BleKissTransport::receive(RxFrame &out) { return s_rxQ && xQueueReceive(s_rxQ, &out, 0) == pdTRUE; }

void BleKissTransport::startScan(uint32_t seconds) {
    s_scanSeconds = constrain(seconds, 2u, 30u);
    withLock([] { s_status.scanning = true; });   // reflect immediately in the UI
    post(CMD_SCAN);
}

std::vector<BleScanResult> BleKissTransport::scanResults() {
    std::vector<BleScanResult> r;
    withLock([&] { r = s_results; });
    std::sort(r.begin(), r.end(), [](const BleScanResult &a, const BleScanResult &b) {
        if (a.kissService != b.kissService) return a.kissService;
        return a.rssi > b.rssi;
    });
    return r;
}

void BleKissTransport::reconfigure() { post(CMD_RECONFIGURE); }
void BleKissTransport::connectNow() { post(CMD_CONNECT); }
void BleKissTransport::disconnect() { post(CMD_DISCONNECT); }

BleStatus BleKissTransport::status() {
    BleStatus s;
    withLock([&] { s = s_status; });
    return s;
}
