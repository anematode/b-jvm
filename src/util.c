#include "util.h"

#include <stdint.h>
#include <stdlib.h>

bool utf8_equals(const bjvm_utf8 entry, const char *str) {
  return entry.len == (int)strlen(str) &&
         memcmp(entry.chars, str, entry.len) == 0;
}

bool utf8_equals_utf8(const bjvm_utf8 left, const bjvm_utf8 right) {
  return left.len == right.len &&
         memcmp(left.chars, right.chars, left.len) == 0;
}

int convert_modified_utf8_to_chars(const char *bytes, int len, short **result,
                                   int *result_len, bool sloppy) {
  *result = malloc(len * sizeof(short)); // conservatively large
  int i = 0, j = 0;

  uint16_t idxs[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  for (int i = 0; i < 16; ++i)
    idxs[i] -= 256;

  for (; i < len; ++i) {
    // "Code points in the range '\u0001' to '\u007F' are represented by a
    // single byte"
    uint8_t byte = bytes[i];
    if (byte >= 0x01 && byte <= 0x7F) {
      (*result)[j++] = byte;
    } else if ((bytes[i] & 0xE0) == 0xC0) {
      // "Code points in the range '\u0080' to '\u07FF' are represented by two
      // bytes"
      if (i >= len - 1)
        goto inval;
      (*result)[j++] = (short)((byte & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
      i++;
    } else if ((bytes[i] & 0xF0) == 0xE0) {
      // "Code points in the range '\u0800' to '\uFFFF' are represented by three
      // bytes"
      if (i >= len - 2)
        goto inval;
      (*result)[j++] = (short)((byte & 0x0F) << 12) |
                       ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
      i += 2;
    } else if (sloppy && byte == 0) {
      break;
    } else {
      // "No byte may have the value (byte)0 or lie in the range (byte)0xf0 -
      // (byte)0xff."
      goto inval;
    }
  }
  *result_len = j;
  return 0;

inval:
  free(*result);
  return -1; // invalid UTF-8 sequence
}