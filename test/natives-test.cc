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

#include "tests-common.h"

struct async_wakeup_info {
  int index;
};

DECLARE_ASYNC(int, _nt_test_yield, locals(async_wakeup_info info;), arguments(int index),);
DEFINE_ASYNC(_nt_test_yield) {
  self->info.index = args->index;
  ASYNC_YIELD(&self->info);
  ASYNC_END(9);
}

#include <natives-dsl.h>

DECLARE_ASYNC_NATIVE("", AsyncNative, asyncNativeMethod, "(I)I", locals(),
                     invoked_methods(invoked_method(_nt_test_yield))) {
  AWAIT(_nt_test_yield, 1);
  ASYNC_END((bjvm_stack_value){.i = get_async_result(_nt_test_yield) * 2});
}

#undef _DECLARE_CACHED_STATE
#undef _RELOAD_CACHED_STATE

#define _DECLARE_CACHED_STATE(_)
#define _RELOAD_CACHED_STATE()

TEST_CASE("Async natives") {
  bjvm_vm_options vm_options = bjvm_default_vm_options();
        vm_options.classpath = STR("test_files/async_natives/");
  bjvm_vm *vm = bjvm_create_vm(vm_options);
  auto thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  bjvm_classdesc *desc = bootstrap_lookup_class(thread, STR("AsyncNative"));
  AWAIT_READY(bjvm_initialize_class, thread, desc);

  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method = bjvm_method_lookup(desc, STR("asyncNativeMethod"), STR("(I)I"), false, false);
  REQUIRE(method != nullptr);

  call_interpreter_t ctx = {.args = {.thread = thread, .method = method, .args = args}};
  future_t fut = {};
  while (fut.status == FUTURE_NOT_READY)
    fut = call_interpreter(&ctx);
}