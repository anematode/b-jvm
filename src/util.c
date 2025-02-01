#include "util.h"

#include <stdint.h>
#include <stdlib.h>

bool utf8_equals(const slice entry, const char *str) {
  return entry.len == (int)strlen(str) &&
         memcmp(entry.chars, str, entry.len) == 0;
}

bool utf8_equals_utf8(const slice left, const slice right) {
  return left.len == right.len &&
         memcmp(left.chars, right.chars, left.len) == 0;
}

bool utf8_ends_with(slice str, slice ending) {
  if (ending.len > str.len) return false;

  return memcmp(str.chars + str.len - ending.len, ending.chars, ending.len) == 0;
}