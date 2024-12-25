#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../src/wasm_utils.h"

TEST_CASE("write leb128 unsigned", "[wasm]") {
  bjvm_bytevector ctx = {nullptr};
  const uint8_t expected[4] = {0x00, 0xE5, 0x8E, 0x26};
  bjvm_wasm_writeuint(&ctx, 0);
  bjvm_wasm_writeuint(&ctx, 624485);
  REQUIRE(ctx.bytes_len == 4);
  REQUIRE(memcmp(ctx.bytes, expected, 4) == 0);
  free(ctx.bytes);
}

TEST_CASE("write leb128 signed", "[wasm]") {
  bjvm_bytevector ctx = {nullptr};
  const uint8_t expected[4] = {0x00, 0xC0, 0xBB, 0x78};
  bjvm_wasm_writeint(&ctx, 0);
  bjvm_wasm_writeint(&ctx, -123456);
  for (int i = 0; i < ctx.bytes_len; ++i) {
    printf("%02X ", ctx.bytes[i]);
  }
  REQUIRE(ctx.bytes_len == 4);
  REQUIRE(memcmp(ctx.bytes, expected, 4) == 0);
  free(ctx.bytes);
}