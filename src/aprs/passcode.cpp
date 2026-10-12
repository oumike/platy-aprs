#include "passcode.h"
#include <ctype.h>

int aprsPasscode(const char *callsign) {
    if (!callsign || !*callsign) return -1;
    char base[10];
    int n = 0;
    for (const char *p = callsign; *p && *p != '-' && n < (int)sizeof(base) - 1; p++) {
        base[n++] = (char)toupper((unsigned char)*p);
    }
    if (n == 0) return -1;
    uint16_t hash = 0x73E2;
    for (int i = 0; i < n; i += 2) {
        hash ^= (uint16_t)((uint8_t)base[i] << 8);
        if (i + 1 < n) hash ^= (uint8_t)base[i + 1];
    }
    return hash & 0x7FFF;
}
