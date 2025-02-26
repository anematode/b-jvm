
#include "trampolines.h"
#include "doctest/doctest.h"
#include <wasm/wasm_utils.h>
#include <array>

TEST_SUITE_BEGIN("[wasm]");

TEST_CASE("write leb128 unsigned") {
  bytevector ctx = {nullptr};
  const u8 expected[4] = {0x00, 0xE5, 0x8E, 0x26};
  wasm_writeuint(&ctx, 0);
  wasm_writeuint(&ctx, 624485);
  REQUIRE(arrlen(ctx.bytes) == 4);
  REQUIRE(memcmp(ctx.bytes, expected, 4) == 0);
  arrfree(ctx.bytes);
}

TEST_CASE("write leb128 signed") {
  bytevector ctx = {nullptr};
  const u8 expected[4] = {0x00, 0xC0, 0xBB, 0x78};
  wasm_writeint(&ctx, 0);
  wasm_writeint(&ctx, -123456);
  REQUIRE(arrlen(ctx.bytes) == 4);
  REQUIRE(memcmp(ctx.bytes, expected, 4) == 0);
  arrfree(ctx.bytes);
}

TEST_CASE("Simple module") {
  wasm_module *module = wasm_module_create();

  wasm_value_type params_[] = {WASM_TYPE_KIND_INT32, WASM_TYPE_KIND_INT32};
  wasm_type params = wasm_make_tuple(module, params_, 2);

  wasm_expression *body =
      wasm_binop(module, WASM_OP_KIND_I32_ADD, wasm_i32_const(module, 1), wasm_i32_const(module, 2));

  wasm_expression *ifelse =
      wasm_if_else(module, wasm_unop(module, WASM_OP_KIND_I32_EQZ, wasm_local_get(module, 0, wasm_int32())),
                   wasm_i32_const(module, 2), body, wasm_int32());

  wasm_type locals = wasm_make_tuple(module, params_, 2);
  // Types should be interned
  REQUIRE(memcmp(&locals, &params, sizeof(locals)) == 0);

  wasm_function *fn = wasm_add_function(module, params, wasm_int32(), locals, ifelse, "add");
  wasm_export_function(module, fn);

  bytevector serialized = wasm_module_serialize(module);

  wasm_module_free(module);
  arrfree(serialized.bytes);
}

TEST_CASE("Trampoline into function") {
  if (sizeof(void*) != 4)
    return;
  // DJIDI -> D
  // f(thread, method, args[0].i, args[1].l, args[2].i, args[3].d, args[4].i);
  auto example = [] (vm_thread *thread, cp_method *method, int arg1, s64 arg2, object arg3, double arg4, object arg5) -> double {
    REQUIRE(arg1 == 1);
    REQUIRE(arg2 == 2);
    REQUIRE(arg3 == nullptr);
    REQUIRE(arg4 == 4.0);
    REQUIRE(arg5 == nullptr);
    REQUIRE(thread == (vm_thread*)8);
    REQUIRE(method == (cp_method*)16);
    return arg4 * 2;
  };
  std::array args = {WASM_TYPE_KIND_INT32, WASM_TYPE_KIND_INT64, WASM_TYPE_KIND_INT32,
                                         WASM_TYPE_KIND_FLOAT64, WASM_TYPE_KIND_INT32};
  jit_trampoline tramp = get_jit_trampoline(WASM_TYPE_KIND_FLOAT64, args.data(), 5);
  REQUIRE(tramp != nullptr);
  object arg3 = nullptr;
  object arg5 = nullptr;
  stack_value stack[5] = {{.i = 1}, {.l = 2}, {.obj = arg3}, {.d = 4.0}, {.obj = arg5 }};

  tramp((void*) +example, (vm_thread*)8, (cp_method*)16, stack);
  REQUIRE(stack[0].d == 8.0);
}

TEST_SUITE_END;