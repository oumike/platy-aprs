#pragma once
#include <stddef.h>
#include <stdint.h>
#include <functional>
#include <vector>

// KISS framing (TNC <-> host). Pure C++, host-testable.
namespace kiss {

constexpr uint8_t FEND = 0xC0;
constexpr uint8_t FESC = 0xDB;
constexpr uint8_t TFEND = 0xDC;
constexpr uint8_t TFESC = 0xDD;
constexpr uint8_t CMD_DATA = 0x00;

// Wraps an AX.25 frame as a KISS data frame for the given port.
std::vector<uint8_t> encodeData(const uint8_t *data, size_t len, uint8_t port = 0);

class Decoder {
public:
    // port = high nibble, cmd = low nibble of the KISS type byte.
    using FrameCb = std::function<void(uint8_t port, uint8_t cmd, const uint8_t *data, size_t len)>;
    explicit Decoder(FrameCb cb, size_t maxFrame = 1024) : _cb(std::move(cb)), _maxFrame(maxFrame) {}
    void feed(const uint8_t *data, size_t len);
    void reset();

private:
    FrameCb _cb;
    size_t _maxFrame;
    std::vector<uint8_t> _buf;
    bool _inFrame = false;
    bool _escape = false;
    bool _overflow = false;
};

}  // namespace kiss
