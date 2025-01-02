#ifndef UTIL_H
#define UTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define __bswap_16(x) OSSwapInt16(x) // NOLINT(*-reserved-identifier)
#define __bswap_32(x) OSSwapInt32(x) // NOLINT(*-reserved-identifier)
#define __bswap_64(x) OSSwapInt64(x) // NOLINT(*-reserved-identifier)
#else
#include <byteswap.h>
#endif

#if !defined(__cplusplus) && !defined(nullptr) &&                              \
    (!defined(__STDC_VERSION__) || __STDC_VERSION__ <= 201710)
/* -Wundef is avoided by using short circuiting in the condition */
#define nullptr ((void *)0)
#endif

#define UNREACHABLE(optional_msg)                                              \
  do {                                                                         \
    fprintf(stderr, "Unreachable code reached at %s:%d. \n" optional_msg,      \
            __FILE__, __LINE__);                                               \
    abort();                                                                   \
  } while (0)

#define VECTOR_PUSH(vector, vector_count, vector_cap)                          \
  ({                                                                           \
    if (__builtin_expect((vector_count) >= (vector_cap), 0)) {                 \
      int new_cap = vector_cap * 2 + 1;                                        \
      void *_next = realloc(vector, new_cap * sizeof(*vector));                \
      (vector_cap) = new_cap;                                                  \
      vector = _next;                                                          \
    }                                                                          \
    &vector[(vector_count)++];                                                 \
  })

typedef struct {
  char *chars;
  int len;
} bjvm_utf8;

typedef struct {
  char *chars;
  int len;
  int cap; // including null byte
} heap_string;

#define INIT_STACK_STRING(name, buffer_size)                                   \
  char name##_chars[buffer_size + 1] = {0};                                    \
  bjvm_utf8 name = {.chars = name##_chars, .len = buffer_size}
#define null_str() ((bjvm_utf8){.chars = nullptr, .len = 0})

/// Slices the given string from the given start index to the end.
static inline bjvm_utf8 slice(bjvm_utf8 str, int start) {
  return (bjvm_utf8){.chars = str.chars + start, .len = str.len - start};
}

/// Slices the given string from the given start index to the given end index.
static inline bjvm_utf8 slice_to(bjvm_utf8 str, int start, int end) {
  return (bjvm_utf8){.chars = str.chars + start, .len = end - start};
}

/// Uses the given format string and arguments to print a string into the given
/// buffer; returns a slice of the buffer containing the string.
static inline bjvm_utf8 bprintf(bjvm_utf8 buffer, const char *format, ...) {
  va_list args;
  va_start(args, format);
  int len = vsnprintf(buffer.chars, buffer.len + 1, format, args);
  va_end(args);
  return (bjvm_utf8){.chars = buffer.chars, .len = len};
}

/// Used to safely (?) build up a string in a heap-allocated buffer.
static inline int build_str(heap_string *str, int write, const char *format,
                            ...) {
  va_list args;
  va_start(args, format);
  int len = vsnprintf(str->chars + write, str->len - write + 1, format, args);
  va_end(args);
  if (len > str->len - write) {
    str->len = write + len;
    // will be at least 1 greater than str->len, to accomodate null terminator
    str->cap = 1 << (sizeof(str->cap) * 8 - __builtin_clz(str->len));
    str->chars = (char *)realloc(str->chars, str->cap);
    va_start(args, format);
    len = vsnprintf(str->chars + write, str->len - write + 1, format, args);
    va_end(args);
  }
  return write + len;
}

/// Mallocates a new heap string with the given length.
static inline heap_string make_heap_str(int len) {
  return (heap_string){
      .chars = (char *)calloc(len + 1, 1), .len = len, .cap = len + 1};
}

/// Creates a heap string from the given slice.
static inline heap_string make_heap_str_from(bjvm_utf8 slice) {
  heap_string str = make_heap_str(slice.len);
  memcpy(str.chars, slice.chars, slice.len);
  return str;
}

/// Truncates the given heap string to the given length.
static inline void heap_str_truncate(heap_string str, int len) {
  assert(len <= str.len);
  str.len = len;
}

/// Frees the given heap string.
static inline void free_heap_str(heap_string str) {
  free(str.chars);
  str.chars = nullptr;
}

/// Creates a slice of the given heap string.
static inline bjvm_utf8 hslc(heap_string str) {
  return (bjvm_utf8){.chars = str.chars, .len = str.len};
}

#define fmt_slice(slice) (int)(slice).len, (slice).chars

#define STR(literal)                                                           \
  ((bjvm_utf8){.chars = (char *)(literal), .len = sizeof(literal) - 1})

bool utf8_equals(const bjvm_utf8 entry, const char *str);
bool utf8_equals_utf8(const bjvm_utf8 left, const bjvm_utf8 right);

int convert_modified_utf8_to_chars(const char *bytes, int len, short **result,
                                   int *result_len, bool sloppy);

// These are unlikely (ha!) to actually improve codegen, but are actually kind
// of nice to indicate what we "think" is going to happen. Long term we might
// use these macro sites to instrument certain operations and see what "unhappy"
// cases are more common than we thought.
#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

#ifdef __cplusplus
}
#endif

#endif