//
// Created by alec on 2/7/25.
//

#ifndef ENDIAN_H
#define ENDIAN_H

#include <types.h>
#include <config.h>

#ifdef __cplusplus
extern "C++" {
#include <cstdint>
#include <type_traits>

template <typename T>
constexpr T bswap_generic(T x) {
  if constexpr (std::is_same_v<T, uint8_t>) {
    return x;
  } else if constexpr (std::is_same_v<T, uint16_t>) {
    return __builtin_bswap16(x);
  } else if constexpr (std::is_same_v<T, uint32_t>) {
    return __builtin_bswap32(x);
  } else if constexpr (std::is_same_v<T, uint64_t>) {
    return __builtin_bswap64(x);
  } else {
    static_assert(!std::is_same_v<T, T>, "Unsupported type for bswap_generic");
  }
}
}
#else
#define bswap_generic(x) _Generic((x), u8: (x), u16: bswap16((x)), u32: bswap32((x)), u64: bswap64((x)))
#endif

#define create_read_method(name, output_ty, should_swap)                                                               \
  static inline output_ty name(u8 const *ptr) {                                                                        \
    typedef union {                                                                                                    \
      u8 u[sizeof(output_ty)];                                                                                         \
      output_ty t;                                                                                                     \
    } pun;                                                                                                             \
    \
    output_ty raw = ((pun const *)ptr)->t;                                                                             \
    return (should_swap) ? bswap_generic(raw) : raw;                                                                   \
  }

#define create_reader_variants(width) \
  create_read_method(read_u##width##_le, u##width, !PLATFORM_LITTLE_ENDIAN); \
  create_read_method(read_u##width##_be, u##width, PLATFORM_LITTLE_ENDIAN);

create_reader_variants(8);
create_reader_variants(16);
create_reader_variants(32);
create_reader_variants(64);

#undef create_reader_variants
#undef create_read_method
#undef bswap_generic

#endif // ENDIAN_H
