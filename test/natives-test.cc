//
// Created by alec on 1/17/25.
//

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

#include "tests-common.h"

struct async_wakeup_info {
  int index;
};

DECLARE_ASYNC_VOID( _nt_test_yield, locals(async_wakeup_info info;), arguments(int index),);
DEFINE_ASYNC(_nt_test_yield) {
  self->info.index = args->index;
  ASYNC_YIELD(&self->info);
  ASYNC_END_VOID();
}

#include <natives-dsl.h>

DECLARE_ASYNC_NATIVE("", AsyncNative, myYield, "(I)I", locals(int i),
                     invoked_methods(invoked_method(_nt_test_yield))) {
  for (self->i = 0; self->i < args[0].i; self->i++) {
    AWAIT(_nt_test_yield, self->i);
  }
  ASYNC_END((bjvm_stack_value){.i = self->i});
}

#undef _DECLARE_CACHED_STATE
#undef _RELOAD_CACHED_STATE

#define _DECLARE_CACHED_STATE(_)
#define _RELOAD_CACHED_STATE()

extern "C"
void stout_write(char *buf, int len, void *param) {
  auto vec = (std::vector<u8> *) param;
  vec->reserve(vec->size() + len);
  for (int i=0; i<len; i++)
    vec->push_back((u8) buf[i]);
}

TEST_CASE("Async natives") {
  std::vector<u8> out;

  bjvm_vm_options vm_options = bjvm_default_vm_options();
        vm_options.classpath = STR("test_files/async_natives/");
  vm_options.write_stdout = &stout_write;
  vm_options.write_stderr = &stout_write;
  vm_options.read_stdin = nullptr;
  vm_options.poll_available_stdin = nullptr;
  vm_options.stdio_override_param = &out;

  bjvm_vm *vm = bjvm_create_vm(vm_options);

  bjvm_native_t *native_ptr = &NATIVE_INFO_AsyncNative_myYield_0;
  bjvm_register_native(vm, native_ptr->class_path, native_ptr->method_name, native_ptr->method_descriptor,
                         native_ptr->callback);

  auto thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  bjvm_classdesc *desc = bootstrap_lookup_class(thread, STR("AsyncNative"));
  AWAIT_READY(bjvm_initialize_class, thread, desc);

  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method = bjvm_method_lookup(desc, STR("doAsyncThing"), STR("()V"), false, false);
  REQUIRE(method != nullptr);

  call_interpreter_t ctx = {.args = {.thread = thread, .method = method, .args = args}};
  future_t fut = {};

  for (int i = 0; i < 2; i++) {
    fut = call_interpreter(&ctx);
    REQUIRE(fut.status == FUTURE_NOT_READY);
    REQUIRE(fut.wakeup->index == i);
  }

  for (int i = 0; i < 4; i++) {
    fut = call_interpreter(&ctx);
    REQUIRE(fut.status == FUTURE_NOT_READY);
    REQUIRE(fut.wakeup->index == i);
  }

  fut = call_interpreter(&ctx);
  REQUIRE(fut.status == FUTURE_READY);

  out.push_back(0);
  REQUIRE_THAT(string{(char const*)out.data()}, Equals("2\n4\n"));

  bjvm_free_thread(thread);
  bjvm_free_vm(vm);
}