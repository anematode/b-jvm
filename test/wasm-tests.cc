#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../src/wasm_utils.h"
#include "../src/wasm_adapter.h"

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
  REQUIRE(ctx.bytes_len == 4);
  REQUIRE(memcmp(ctx.bytes, expected, 4) == 0);
  free(ctx.bytes);
}

TEST_CASE("Simple module", "[wasm]") {
  bjvm_wasm_module *module = bjvm_wasm_module_create();

  bjvm_wasm_value_type params_[] = {BJVM_WASM_TYPE_KIND_INT32,
                                    BJVM_WASM_TYPE_KIND_INT32};
  bjvm_wasm_type params = bjvm_wasm_make_tuple(module, params_, 2);

  bjvm_wasm_expression *body = bjvm_wasm_binop(
      module, BJVM_WASM_OP_KIND_I32_ADD, bjvm_wasm_i32_const(module, 1),
      bjvm_wasm_i32_const(module, 2));

  bjvm_wasm_expression *ifelse = bjvm_wasm_if_else(
      module,
      bjvm_wasm_unop(module, BJVM_WASM_OP_KIND_I32_EQZ,
                     bjvm_wasm_local_get(module, 0, bjvm_wasm_int32())),
      bjvm_wasm_i32_const(module, 2), body, bjvm_wasm_int32());

  bjvm_wasm_type locals = bjvm_wasm_make_tuple(module, params_, 2);
  // Types should be interned
  REQUIRE(memcmp(&locals, &params, sizeof(locals)) == 0);

  bjvm_wasm_function *fn = bjvm_wasm_add_function(
      module, params, bjvm_wasm_int32(), locals, ifelse, "add");
  bjvm_wasm_export_function(module, fn);

  bjvm_bytevector serialized = bjvm_wasm_module_serialize(module);

  bjvm_wasm_module_free(module);
  free(serialized.bytes);
}


TEST_CASE("create_adapter_to_compiled_method", "[wasm]") {
  auto example = [](bjvm_thread *thread, bjvm_stack_value *result, double a, long b) -> int {
    result->d = a + b;
    return BJVM_INTERP_RESULT_EXC;
  };
  bjvm_type_kind args[2] = {BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_LONG};
  auto adapter = create_adapter_to_compiled_method(args, 2);
  bjvm_stack_value result;
  if (adapter) {
    bjvm_stack_value args[2] = {(bjvm_stack_value){.d = 1.0}, (bjvm_stack_value){.l = 2}};
    int n = adapter(nullptr, &result, args, (void*) +example);
    REQUIRE(result.d == 3.0);
    REQUIRE(n == BJVM_INTERP_RESULT_EXC);
  }
}