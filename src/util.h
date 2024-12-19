#ifndef UTIL_H
#define UTIL_H

#include <string.h>

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define __bswap_16(x) OSSwapInt16(x)
#define __bswap_32(x) OSSwapInt32(x)
#define __bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

#if !defined(__cplusplus) && !defined(nullptr) && \
   (!defined(__STDC_VERSION__) || __STDC_VERSION__ <= 201710)
/* -Wundef is avoided by using short circuiting in the condition */
#define nullptr ((void*)0)
#endif


#define UNREACHABLE(optional_msg)                                              \
  do {                                                                         \
    fprintf(stderr, "Unreachable code reached at %s:%d. \n" optional_msg,      \
            __FILE__, __LINE__);                                               \
    abort();                                                                   \
  } while (0)

#define VECTOR_PUSH(vector, vector_count, vector_cap)                          \
  ({                                                                           \
    if ((vector_count) >= (vector_cap)) {                                      \
      int new_cap;                                                             \
      int overflow = __builtin_mul_overflow((vector_cap), 2, &new_cap);        \
      assert(!overflow);                                                       \
      if (new_cap < 2)                                                         \
        new_cap = 2;                                                           \
      void *next = realloc(vector, new_cap * sizeof(*vector));                 \
      assert(next);                                                            \
      (vector_cap) = new_cap;                                                  \
      vector = next;                                                           \
    }                                                                          \
    &vector[(vector_count)++];                                                 \
  })

typedef struct {
  wchar_t *chars;
  int len;
} bjvm_utf8;

static bool utf8_equals(const bjvm_utf8 *entry, const char *str) {
  if (entry->len != (int)strlen(str))
    return false;
  for (int i = 0; i < entry->len; ++i)
    if (entry->chars[i] != str[i])
      return false;
  return true;
}

static bool utf8_equals_utf8(const bjvm_utf8 *left, const bjvm_utf8 *right) {
  if (left->len != right->len)
    return false;
  return wmemcmp(left->chars, right->chars, left->len) == 0;
}

#endif