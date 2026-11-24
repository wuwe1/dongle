// The pure parts of the firmware, with no ESP-IDF in them, so test/ can run them on a computer.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// One frame as a candump -L line, newline included: "(12.000345) can0 1A0#00FF". Returns its
// length. A 64-byte buffer holds the longest (extended ID, 8 bytes).
int frame_line(char *s, size_t size, int64_t us, uint32_t id, bool extended, bool remote,
               uint8_t dlc, const uint8_t *data);

// "1A0,2B3,..." (hex) into ids; returns how many were read, at most max.
int parse_ids(const char *s, uint32_t *ids, int max);
