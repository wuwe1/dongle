#include "frame.h"

#include <stdio.h>
#include <stdlib.h>

int frame_line(char *s, size_t size, int64_t us, uint32_t id, bool extended, bool remote,
               uint8_t dlc, const uint8_t *data) {
    int n = snprintf(s, size, "(%lld.%06lld) can0 %0*lX#", (long long)(us / 1000000),
                     (long long)(us % 1000000), extended ? 8 : 3, (unsigned long)id);
    if (remote) {
        n += snprintf(s + n, size - n, "R");
    } else {
        int len = dlc > 8 ? 8 : dlc;  // classic CAN carries at most 8 bytes whatever the DLC says
        for (int i = 0; i < len; i++) n += snprintf(s + n, size - n, "%02X", data[i]);
    }
    n += snprintf(s + n, size - n, "\n");
    return n;
}

int parse_ids(const char *s, uint32_t *ids, int max) {
    int n = 0;
    while (*s && n < max) {
        char *end;
        unsigned long id = strtoul(s, &end, 16);
        if (end == s) break;
        ids[n++] = id;
        s = *end == ',' ? end + 1 : end;
    }
    return n;
}
