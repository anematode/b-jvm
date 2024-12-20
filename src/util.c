#include "util.h"

#include <stdlib.h>

bool utf8_equals(const bjvm_utf8 entry, const char *str) {
  if (entry.len != (int)strlen(str))
    return false;
  for (int i = 0; i < entry.len; ++i)
    if (entry.chars[i] != str[i])
      return false;
  return true;
}

bool utf8_equals_utf8(const bjvm_utf8 left, const bjvm_utf8 right) {
  if (left.len != right.len)
    return false;
  return memcmp(left.chars, right.chars, left.len) == 0;
}

char *lossy_utf8_entry_to_chars(const bjvm_utf8 utf8) {
  char *result = malloc(utf8.len + 1);
  int i = 0;
  for (; i < utf8.len; ++i) {
    result[i] = (char)utf8.chars[i];
  }
  result[i] = '\0';
  return result;
}

bjvm_utf8 bjvm_make_utf8(const char *c_literal) {
  return (bjvm_utf8){.chars = strdup(c_literal), .len = strlen(c_literal)};
}