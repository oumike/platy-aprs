#pragma once
#include <Arduino.h>
#include <vector>
#include "aprs/aprs_parser.h"

// APRS person-to-person messaging: send with retries until acked, ack and
// de-duplicate incoming, conversation history saved to the SD card.
// Loop task only.

enum class MsgState : uint8_t { Received = 0, Pending = 1, Acked = 2, Rejected = 3, Failed = 4 };
const char *msgStateName(MsgState s);

struct ChatMsg {
    uint32_t seq;        // increasing, never 0
    char peer[10];       // the other station
    bool outgoing;
    MsgState state;
    bool unread;
    uint8_t tries;
    char msgId[6];
    char text[68];
    time_t epoch;        // 0 when the clock was not set
    uint32_t uptimeS;
    uint32_t nextTryMs;  // outgoing + Pending only
};

struct ChatThread {
    String peer;
    String lastText;
    bool lastOutgoing;
    uint32_t lastSeq;
    uint32_t lastUptimeS;
    time_t lastEpoch;
    uint16_t unread;
    uint16_t count;
};

void messagesBegin();   // after sdBegin()
void messagesLoop();
void messagesOnRx(const aprs::Packet &p);

// Queues a message (sent right away, then retried until acked).
bool messagesSend(const String &to, const String &text, String &err);
// Retry a failed message now.
bool messagesRetry(uint32_t seq, String &err);

size_t messagesCount();
const ChatMsg *messagesAt(size_t i);   // 0 = oldest
std::vector<ChatThread> messagesThreads();   // most recent first
void messagesMarkRead(const String &peer);
void messagesDeleteThread(const String &peer);
uint32_t messagesUnread();

// Called for each new incoming message addressed to us (for UI banners).
void messagesSetIncomingHook(void (*hook)(const ChatMsg &m));

// Bumped on any change.
extern uint32_t g_msgGen;
