#include "messages.h"
#include "alerts.h"
#include "aprs/aprs_encoder.h"
#include "ax25/ax25.h"
#include <algorithm>
#include "config/config.h"
#include "data_store.h"
#include "hal/sd_card.h"
#include "router.h"
#include "transport/ble_kiss.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <time.h>

uint32_t g_msgGen = 1;

namespace {

constexpr size_t kCap = 150;
constexpr const char *kFile = "/platy/messages.json";
constexpr uint8_t kMaxTries = 5;
constexpr uint32_t kRetryS[kMaxTries] = {30, 60, 120, 240, 240};   // wait after try n

ChatMsg *s_msgs;       // ring
size_t s_head, s_count;
uint32_t s_nextSeq = 1;
uint32_t s_nextId;
bool s_dirty;
bool s_loaded = false;   // the stored conversations have been read (or there were none)
uint32_t s_dirtyMs;
void (*s_hook)(const ChatMsg &) = nullptr;

ChatMsg &at(size_t i) { return s_msgs[(s_head + kCap - s_count + i) % kCap]; }

time_t nowEpoch() {
    const time_t t = time(nullptr);
    return t > 1700000000 ? t : 0;
}

void changed() {
    g_msgGen++;
    if (!s_dirty) s_dirtyMs = millis();
    s_dirty = true;
}

ChatMsg &append() {
    ChatMsg &m = s_msgs[s_head];
    s_head = (s_head + 1) % kCap;
    if (s_count < kCap) s_count++;
    memset(&m, 0, sizeof(m));
    m.seq = s_nextSeq++;
    m.epoch = nowEpoch();
    m.uptimeS = uptimeS();
    return m;
}

void upper(char *s) {
    for (; *s; s++) *s = toupper(*s);
}

String myCall() {
    String c = g_config.fullCallsign();
    c.toUpperCase();
    return c;
}

bool transmit(ChatMsg &m) {
    String err;
    const bool ok = routerSendInfo(aprs::encodeMessage(m.peer, m.text, m.msgId), err);
    if (!ok) Serial.printf("[msg] send to %s failed: %s\n", m.peer, err.c_str());
    return ok;
}

void sendAck(const char *to, const std::string &id) {
    String err;
    if (!routerSendInfo(aprs::encodeAck(to, id), err)) Serial.printf("[msg] ack to %s failed: %s\n", to, err.c_str());
}

// ── SD persistence ───────────────────────────────────────────────────────────

void save() {
    // Not before the stored conversations have been read: a card that mounts
    // after boot would otherwise be overwritten with only what is in memory.
    if (!s_loaded) return;
    s_dirty = false;
    if (!sdMounted()) return;
    sdMakeDirs("/platy");
    JsonDocument doc;
    doc["nextId"] = s_nextId;
    JsonArray a = doc["messages"].to<JsonArray>();
    for (size_t i = 0; i < s_count; i++) {
        const ChatMsg &m = at(i);
        JsonObject o = a.add<JsonObject>();
        o["peer"] = m.peer;
        o["out"] = m.outgoing;
        // A pending message can't be resumed after a reboot.
        o["state"] = (uint8_t)(m.state == MsgState::Pending ? MsgState::Failed : m.state);
        o["unread"] = m.unread;
        o["id"] = m.msgId;
        o["text"] = m.text;
        o["epoch"] = (uint32_t)m.epoch;
        if (!m.outgoing && m.via != MsgVia::Unknown) {
            o["via"] = (uint8_t)m.via;
            o["path"] = m.path;
            if (m.relay[0]) o["relay"] = m.relay;
        }
    }
    File f = sdFs().open(kFile, FILE_WRITE);
    if (!f) return;
    serializeJson(doc, f);
    f.close();
}

void load() {
    if (!sdMounted()) return;
    s_loaded = true;
    if (!sdFs().exists(kFile)) return;
    File f = sdFs().open(kFile, FILE_READ);
    if (!f) return;
    JsonDocument doc;
    const DeserializationError e = deserializeJson(doc, f);
    f.close();
    if (e) {
        Serial.printf("[msg] %s unreadable: %s\n", kFile, e.c_str());
        return;
    }
    if (doc["nextId"].is<uint32_t>()) s_nextId = doc["nextId"];
    for (JsonObjectConst o : doc["messages"].as<JsonArrayConst>()) {
        ChatMsg &m = append();
        strlcpy(m.peer, o["peer"] | "", sizeof(m.peer));
        m.outgoing = o["out"] | false;
        m.state = (MsgState)(o["state"] | 0);
        m.unread = o["unread"] | false;
        strlcpy(m.msgId, o["id"] | "", sizeof(m.msgId));
        strlcpy(m.text, o["text"] | "", sizeof(m.text));
        m.epoch = (time_t)(o["epoch"] | 0u);
        const uint8_t via = o["via"] | 0;
        m.via = via <= (uint8_t)MsgVia::AprsIs ? (MsgVia)via : MsgVia::Unknown;
        strlcpy(m.path, o["path"] | "", sizeof(m.path));
        strlcpy(m.relay, o["relay"] | "", sizeof(m.relay));
        m.uptimeS = 0;
    }
    Serial.printf("[msg] loaded %u messages\n", (unsigned)s_count);
}

}  // namespace

const char *msgStateName(MsgState s) {
    switch (s) {
        case MsgState::Pending: return "pending";
        case MsgState::Acked: return "acked";
        case MsgState::Rejected: return "rejected";
        case MsgState::Failed: return "failed";
        default: return "received";
    }
}

const char *msgViaName(MsgVia v) {
    switch (v) {
        case MsgVia::Tnc: return "RF (Bluetooth TNC)";
        case MsgVia::Lora: return "RF (LoRa)";
        case MsgVia::AprsIs: return "APRS-IS";
        default: return "unknown";
    }
}

void messagesBegin() {
    s_msgs = (ChatMsg *)heap_caps_calloc(kCap, sizeof(ChatMsg), MALLOC_CAP_SPIRAM);
    if (!s_msgs) s_msgs = (ChatMsg *)calloc(kCap, sizeof(ChatMsg));
    s_nextId = 1 + esp_random() % 500;
    load();
    s_dirty = false;
}

void messagesSetIncomingHook(void (*hook)(const ChatMsg &m)) { s_hook = hook; }

bool messagesSend(const String &toIn, const String &text, String &err) {
    if (!s_msgs) return false;
    String to = toIn;
    to.trim();
    to.toUpperCase();
    ax25::Address a;
    if (to.isEmpty() || to.length() > 9 || !ax25::parseAddress(to.c_str(), a)) {
        err = "Invalid callsign";
        return false;
    }
    const std::string clean = aprs::cleanMessageText(text.c_str());
    if (clean.empty()) {
        err = "Message is empty";
        return false;
    }
    ChatMsg &m = append();
    strlcpy(m.peer, to.c_str(), sizeof(m.peer));
    m.outgoing = true;
    m.state = MsgState::Pending;
    strlcpy(m.text, clean.c_str(), sizeof(m.text));
    snprintf(m.msgId, sizeof(m.msgId), "%lu", (unsigned long)(s_nextId++ % 100000));
    if (transmit(m)) m.tries = 1;
    m.nextTryMs = millis() + (m.tries ? kRetryS[0] : 10) * 1000;
    changed();
    return true;
}

bool messagesRetry(uint32_t seq, String &err) {
    for (size_t i = 0; i < s_count; i++) {
        ChatMsg &m = at(i);
        if (m.seq != seq) continue;
        if (!m.outgoing) break;
        m.state = MsgState::Pending;
        m.tries = 0;
        m.nextTryMs = millis();
        changed();
        return true;
    }
    err = "Message not found";
    return false;
}

void messagesOnRx(const aprs::Packet &p, MsgVia via, const char *path, const char *relay) {
    if (!s_msgs || p.type != aprs::Type::Message) return;
    const String me = myCall();
    if (!me.equalsIgnoreCase(p.addressee.c_str())) return;
    char from[10];
    strlcpy(from, p.source.c_str(), sizeof(from));
    upper(from);

    if (p.isAck || p.isRej) {
        for (size_t i = s_count; i-- > 0;) {
            ChatMsg &m = at(i);
            if (m.outgoing && !strcmp(m.peer, from) && p.msgId == m.msgId) {
                if (m.state == MsgState::Pending || m.state == MsgState::Failed) {
                    m.state = p.isAck ? MsgState::Acked : MsgState::Rejected;
                    alertPlay(p.isAck ? Alert::Ack : Alert::Failed);
                    changed();
                }
                break;
            }
        }
        return;
    }

    // Always (re-)ack: the sender retries until one of our acks gets through.
    if (!p.msgId.empty()) sendAck(from, p.msgId);

    // Duplicates (their retries): same id, or same text without an id, recently.
    const uint32_t now = uptimeS();
    for (size_t i = s_count; i-- > 0;) {
        const ChatMsg &m = at(i);
        if (m.outgoing || strcmp(m.peer, from)) continue;
        if (m.uptimeS == 0 || now - m.uptimeS > 1800) break;
        if (p.msgId.empty() ? (now - m.uptimeS < 60 && p.text == m.text) : p.msgId == m.msgId) return;
    }

    ChatMsg &m = append();
    strlcpy(m.peer, from, sizeof(m.peer));
    m.state = MsgState::Received;
    m.unread = true;
    strlcpy(m.msgId, p.msgId.c_str(), sizeof(m.msgId));
    strlcpy(m.text, p.text.c_str(), sizeof(m.text));
    m.via = via;
    strlcpy(m.path, path ? path : "", sizeof(m.path));
    strlcpy(m.relay, relay ? relay : "", sizeof(m.relay));
    changed();
    alertPlay(Alert::Message);
    Serial.printf("[msg] from %s: %s\n", m.peer, m.text);
    if (s_hook) s_hook(m);
}

void messagesLoop() {
    if (!s_msgs) return;
    // A card that mounted after boot: read what it holds, then write back
    // anything that arrived in the meantime.
    if (!s_loaded && sdMounted()) {
        load();
        g_msgGen++;
        if (s_dirty) save();
    }
    const uint32_t nowMs = millis();
    for (size_t i = 0; i < s_count; i++) {
        ChatMsg &m = at(i);
        if (!m.outgoing || m.state != MsgState::Pending || (int32_t)(nowMs - m.nextTryMs) < 0) continue;
        if (m.tries >= kMaxTries) {
            m.state = MsgState::Failed;
            alertPlay(Alert::Failed);
            changed();
            continue;
        }
        if (!routerCanSend()) {
            m.nextTryMs = nowMs + 10000;   // wait for the TNC / APRS-IS; doesn't use up a try
            continue;
        }
        if (transmit(m)) {
            m.nextTryMs = nowMs + kRetryS[m.tries] * 1000;
            m.tries++;
            changed();
        } else {
            m.nextTryMs = nowMs + 10000;
        }
        break;   // at most one transmission per loop pass
    }
    if (s_dirty && nowMs - s_dirtyMs > 3000) save();
}

size_t messagesCount() { return s_count; }
const ChatMsg *messagesAt(size_t i) { return i < s_count ? &at(i) : nullptr; }

std::vector<ChatThread> messagesThreads() {
    std::vector<ChatThread> t;
    for (size_t i = 0; i < s_count; i++) {
        const ChatMsg &m = at(i);
        ChatThread *th = nullptr;
        for (auto &x : t)
            if (x.peer == m.peer) th = &x;
        if (!th) {
            t.push_back({});
            th = &t.back();
            th->peer = m.peer;
        }
        th->lastText = m.text;
        th->lastOutgoing = m.outgoing;
        th->lastSeq = m.seq;
        th->lastUptimeS = m.uptimeS;
        th->lastEpoch = m.epoch;
        th->count++;
        if (m.unread) th->unread++;
    }
    std::sort(t.begin(), t.end(), [](const ChatThread &a, const ChatThread &b) { return a.lastSeq > b.lastSeq; });
    return t;
}

void messagesMarkRead(const String &peer) {
    bool any = false;
    for (size_t i = 0; i < s_count; i++) {
        ChatMsg &m = at(i);
        if (m.unread && peer.equalsIgnoreCase(m.peer)) {
            m.unread = false;
            any = true;
        }
    }
    if (any) changed();
}

void messagesDeleteThread(const String &peer) {
    // Compact the ring, keeping order.
    size_t w = 0;
    const size_t n = s_count;
    std::vector<ChatMsg> keep;
    keep.reserve(n);
    for (size_t i = 0; i < n; i++)
        if (!peer.equalsIgnoreCase(at(i).peer)) keep.push_back(at(i));
    s_head = 0;
    s_count = 0;
    for (auto &m : keep) {
        s_msgs[w++] = m;
        s_head = w % kCap;
        s_count = w;
    }
    changed();
}

uint32_t messagesUnread() {
    uint32_t n = 0;
    for (size_t i = 0; i < s_count; i++)
        if (at(i).unread) n++;
    return n;
}
