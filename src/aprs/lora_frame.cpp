#include "lora_frame.h"

namespace lora_frame {

static const char kPrefix[] = {'<', '\xFF', '\x01'};

std::string encode(const std::string &tnc2) {
    std::string out(kPrefix, sizeof(kPrefix));
    out += tnc2;
    return out;
}

bool decode(const uint8_t *data, size_t len, std::string &tnc2) {
    size_t start = 0;
    if (len >= 3 && data[0] == '<' && data[1] == 0xFF && data[2] == 0x01) start = 3;
    std::string s(reinterpret_cast<const char *>(data) + start, len - start);
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n' || s.back() == '\0')) s.pop_back();
    const size_t gt = s.find('>');
    const size_t colon = s.find(':');
    if (gt == std::string::npos || gt == 0 || colon == std::string::npos || colon < gt) return false;
    for (size_t i = 0; i < colon; i++) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 0x21 || c > 0x7E) return false;   // header must be printable
    }
    tnc2 = s;
    return true;
}

}  // namespace lora_frame
