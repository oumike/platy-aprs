#pragma once
#include <string>
#include "ax25/ax25.h"

// iGate and digipeater rules. Pure C++, host-testable.
namespace gating {

// One APRS-IS packet line "SRC>DST,PATH...:info", split up.
struct IsPacket {
    std::string src, dst, path, info;   // path without the leading comma
};

// False for server comments ("# ...") and malformed lines.
bool splitIsLine(const std::string &line, IsPacket &out);

// True if a comma-separated path contains TCPIP, TCPXX, NOGATE or RFONLY
// (a trailing '*' is ignored).
bool pathBlocksGating(const std::string &path);

// RF packet in TNC2 form -> APRS-IS line with "qAR,<igateCall>" appended.
// Returns false when the packet must not be gated (see aprs-is.net IGate rules):
// NOGATE/RFONLY/TCPIP/TCPXX in the path, queries, empty info. Third-party
// packets are unwrapped and the inner packet is gated under the same rules.
bool rfToIs(const std::string &tnc2, const std::string &igateCall, std::string &out);

// Info field for gating an APRS-IS packet to RF as third-party traffic:
// "}SRC>DST,TCPIP,<igateCall>*:info".
std::string isToRfInfo(const IsPacket &p, const std::string &igateCall);

// Digipeat decision for a received frame. Handles our own callsign as the next
// hop, and WIDE1-1 fill-in (becomes "<me>*,WIDE1*"). Modifies the path and
// returns true when the frame should be retransmitted.
bool digipeat(ax25::Frame &f, const ax25::Address &me);

}  // namespace gating
