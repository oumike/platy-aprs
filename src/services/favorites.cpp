#include "favorites.h"
#include "config/config.h"

uint32_t g_favGen = 1;

static bool findIn(const String &list, const char *call, int &start, int &end) {
    const size_t n = strlen(call);
    if (!n) return false;
    int pos = 0;
    while (pos <= (int)list.length()) {
        int comma = list.indexOf(',', pos);
        if (comma < 0) comma = list.length();
        if ((size_t)(comma - pos) == n && !strncasecmp(list.c_str() + pos, call, n)) {
            start = pos;
            end = comma;
            return true;
        }
        pos = comma + 1;
    }
    return false;
}

bool favIs(const char *call) {
    int a, b;
    return findIn(g_config.favorites, call, a, b);
}

bool favToggle(const char *call) {
    String &f = g_config.favorites;
    int a, b;
    bool now;
    if (findIn(f, call, a, b)) {
        // Drop the entry along with one adjoining comma.
        if (b < (int)f.length()) b++;
        else if (a > 0) a--;
        f.remove(a, b - a);
        now = false;
    } else {
        String c = call;
        c.toUpperCase();
        if (f.length()) f += ',';
        f += c;
        now = true;
    }
    g_favGen++;
    configSave();
    return now;
}
