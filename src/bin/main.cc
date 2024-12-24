//
// Created by Cowpox on 12/10/24.
//

#include <emscripten/emscripten.h>
#include "../bjvm.h"
#include "../../test/tests-common.h"

extern "C" {
EMSCRIPTEN_KEEPALIVE
void *set_up() {
  const char* classpath[2] = {"test_files/playground/", nullptr};
  bjvm_vm_options options = bjvm_default_vm_options();
  options.load_classfile = Bjvm::Tests::load_classfile;
  options.load_classfile_param = classpath;
  options.write_stdout = +[](int ch, void *param) {
    EM_ASM_({
      process.stdout.write(String.fromCharCode($0));
    }, ch);
  };
  options.write_stderr = +[](int ch, void *param) {
    EM_ASM_({
      process.stdout.write(String.fromCharCode($0));
    }, ch);
  };
  options.write_byte_param = nullptr;

  bjvm_vm *vm = bjvm_create_vm(options);
  bjvm_thread *thr = bjvm_create_thread(vm, bjvm_default_thread_options());

  bjvm_classdesc *desc = bootstrap_class_create(thr, STR("Main"));
  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method;
  bjvm_initialize_class(thr, desc);

  method = bjvm_easy_method_lookup(desc, STR("main"),
                                   STR("([Ljava/lang/String;)V"), false, false);

  bjvm_async_run_ctx *ctx = bjvm_thread_async_run(thr, method, args, nullptr);
  return ctx;
}
}

int main() {

}