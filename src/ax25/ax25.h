#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

// AX.25 UI frames and TNC2 text form. Pure C++, host-testable.
namespace ax25 {

struct Address {
    std::string call;   // uppercase, no SSID, 1..6 chars
    uint8_t ssid = 0;
    bool repeated = false;   // H bit (digipeaters) — rendered as '*' in TNC2

    std::string str() const;   // "CALL-SSID" (no '*')
    bool operator==(const Address &o) const { return call == o.call && ssid == o.ssid; }
};

struct Frame {
    Address dst;
    Address src;
    std::vector<Address> digis;   // max 8
    uint8_t control = 0x03;       // UI
    uint8_t pid = 0xF0;           // no layer 3
    std::string info;             // may contain arbitrary bytes
};

// "N0CALL-9" / "WIDE1-1*". Returns false for malformed text.
bool parseAddress(const std::string &text, Address &out);

bool decode(const uint8_t *data, size_t len, Frame &out);
std::vector<uint8_t> encode(const Frame &f);

// "SRC>DST,DIGI*,DIGI:info". The '*' follows the last digipeater whose H bit
// is set, as most software prints it.
std::string toTnc2(const Frame &f);
bool fromTnc2(const std::string &line, Frame &out);

// Build a digipeater path from "WIDE1-1,WIDE2-1". Empty string -> no digis.
bool parsePath(const std::string &path, std::vector<Address> &out);

}  // namespace ax25
