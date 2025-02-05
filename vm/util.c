#include "util.h"

#include <types.h>
#include <stdlib.h>

bool utf8_equals(const slice entry, const char *str) {
  size_t str_len = strlen(str);
  return str_len < U32_MAX && entry.len == (u32)str_len &&
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