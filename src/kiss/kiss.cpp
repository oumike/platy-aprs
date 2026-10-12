#include "kiss.h"

namespace kiss {

std::vector<uint8_t> encodeData(const uint8_t *data, size_t len, uint8_t port) {
    std::vector<uint8_t> out;
    out.reserve(len + 8);
    out.push_back(FEND);
    out.push_back((uint8_t)((port & 0x0F) << 4 | CMD_DATA));
    for (size_t i = 0; i < len; i++) {
        if (data[i] == FEND) {
            out.push_back(FESC);
            out.push_back(TFEND);
        } else if (data[i] == FESC) {
            out.push_back(FESC);
            out.push_back(TFESC);
        } else {
            out.push_back(data[i]);
        }
    }
    out.push_back(FEND);
    return out;
}

void Decoder::reset() {
    _buf.clear();
    _inFrame = false;
    _escape = false;
    _overflow = false;
}

void Decoder::feed(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        const uint8_t b = data[i];
        if (b == FEND) {
            if (_inFrame && _buf.size() > 1 && !_overflow && _cb) {
                const uint8_t type = _buf[0];
                _cb(type >> 4, type & 0x0F, _buf.data() + 1, _buf.size() - 1);
            }
            _buf.clear();
            _inFrame = true;
            _escape = false;
            _overflow = false;
            continue;
        }
        if (!_inFrame) continue;
        if (_escape) {
            _escape = false;
            if (b == TFEND) _buf.push_back(FEND);
            else if (b == TFESC) _buf.push_back(FESC);
            else _buf.push_back(b);   // protocol violation: keep the byte
        } else if (b == FESC) {
            _escape = true;
            continue;
        } else {
            _buf.push_back(b);
        }
        if (_buf.size() > _maxFrame) {
            _overflow = true;
            _buf.clear();
        }
    }
}

}  // namespace kiss
