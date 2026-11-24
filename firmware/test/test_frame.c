// The firmware's line format and filter parsing, run on a computer: firmware/test/run.sh
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../main/frame.h"

static void line_is(int64_t us, uint32_t id, bool ext, bool rtr, uint8_t dlc, const uint8_t *d,
                    const char *want) {
    char s[64];
    int n = frame_line(s, sizeof s, us, id, ext, rtr, dlc, d);
    if (strcmp(s, want) != 0 || n != (int)strlen(want)) {
        fprintf(stderr, "got  %s\nwant %s", s, want);
        assert(0);
    }
    fputs(s, stdout);  // run.sh feeds these to tools/canlog.py
}

int main(void) {
    const uint8_t d[8] = {0x00, 0xff, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc};
    line_is(12000345, 0x1a0, false, false, 2, d, "(12.000345) can0 1A0#00FF\n");
    line_is(5, 0x7, false, false, 0, d, "(0.000005) can0 007#\n");
    line_is(1000000, 0x18daf110, true, false, 8, d, "(1.000000) can0 18DAF110#00FF123456789ABC\n");
    line_is(2500000, 0x7df, false, true, 8, d, "(2.500000) can0 7DF#R\n");
    line_is(3000000, 0x123, false, false, 15, d, "(3.000000) can0 123#00FF123456789ABC\n");  // DLC>8
    // the longest: 19-digit seconds, extended ID, 8 bytes -> must still fit 64 with the NUL
    char s[64];
    int n = frame_line(s, sizeof s, INT64_MAX, 0x1fffffff, true, false, 8, d);
    assert(n < 64 && s[n - 1] == '\n');

    uint32_t ids[4];
    assert(parse_ids("1A0,2b3,7E8", ids, 4) == 3 && ids[0] == 0x1a0 && ids[1] == 0x2b3 && ids[2] == 0x7e8);
    assert(parse_ids("1,2,3,4,5,6", ids, 4) == 4);   // at most max
    assert(parse_ids("zz", ids, 4) == 0);
    assert(parse_ids("", ids, 4) == 0);
    fprintf(stderr, "frame: ok\n");
    return 0;
}
