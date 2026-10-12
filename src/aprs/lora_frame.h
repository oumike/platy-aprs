#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string>

// LoRa APRS (OE5BPA / CA2RXU convention): each LoRa packet is "<\xFF\x01"
// followed by one TNC2-format line (SRC>DST,PATH:info), no AX.25 framing.
namespace lora_frame {

// Wraps a TNC2 line for transmission.
std::string encode(const std::string &tnc2);

// Unwraps a received packet into a TNC2 line (trailing CR/LF/NUL removed).
// Packets without the prefix are accepted if they look like TNC2 text, since
// some trackers omit it. Returns false for anything that is not APRS.
bool decode(const uint8_t *data, size_t len, std::string &tnc2);

}  // namespace lora_frame
