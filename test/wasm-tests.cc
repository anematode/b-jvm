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
  bjvm_thread *thread_param = (bjvm_thread*) 16;
  auto example = [](bjvm_thread *thread, bjvm_stack_value *result, double a, int64_t b, bjvm_obj_header *obj) -> int {
    result->d = a + b;
    REQUIRE(obj == nullptr);
    REQUIRE(16 == (uintptr_t) thread);
    return BJVM_INTERP_RESULT_EXC;
  };
  bjvm_type_kind args[3] = {BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_REFERENCE};
  auto adapter = create_adapter_to_compiled_method(args, 3);
  bjvm_stack_value result;
  if (adapter) {
    bjvm_stack_value args[3] = {(bjvm_stack_value){.d = 1.0}, (bjvm_stack_value){.l = 2}, (bjvm_stack_value) {.obj = nullptr}};
    int n = adapter(thread_param, &result, args, (void*) +example);
    REQUIRE(result.d == 3.0);
    REQUIRE(n == BJVM_INTERP_RESULT_EXC);
  }
}

TEST_CASE("create_adapter_to_interpreter", "[wasm]") {
  bjvm_vm_options options = bjvm_default_vm_options();
  std::string stdout_;
  options.write_stdout = +[] (int ch, void *param) {
    ((std::string*) param)->push_back(ch);
  };
  options.write_byte_param = &stdout_;
  options.classpath = STR("test_files/long_calls");

  bjvm_vm *vm = bjvm_create_vm(options);
  bjvm_thread *thr = bjvm_create_thread(vm, bjvm_default_thread_options());

  bjvm_classdesc *desc = must_create_class(thr, STR("LongHelper"));
  bjvm_initialize_class(thr, desc);
  bjvm_cp_method *add = bjvm_method_lookup(desc, STR("add"), STR("(JI)J"), false, false);
  bjvm_obj_header *obj = new_object(thr, desc);

  assert(add);
  assert(obj);

#ifdef EMSCRIPTEN
  auto fn = (bjvm_interpreter_result_t (*)(bjvm_thread*, bjvm_cp_method*, bjvm_stack_value*, bjvm_obj_header*, int64_t, int)) add->entry_point;
  bjvm_stack_value result;
  bjvm_interpreter_result_t ret_val = fn(thr, add, &result, obj, 100000000000LL, 2);
  REQUIRE(result.l == 100000000002LL);
  REQUIRE(ret_val == BJVM_INTERP_RESULT_OK);
  REQUIRE(thr->frames_count == 0);
#endif

  bjvm_free_thread(thr);
  bjvm_free_vm(vm);
}