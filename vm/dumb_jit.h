//
// Created by Cowpox on 2/15/25.
//

#ifndef DUMB_JIT_H
#define DUMB_JIT_H

#include "bjvm.h"
#include "util.h"

#include <wasm/wasm_utils.h>

typedef struct {
  u16 *count;
  int max_pc;
} pc_to_oop_count;

typedef struct {
  pc_to_oop_count pc_to_oops;
  void *entry; // (vm_thread *, cp_method *, arg1, arg2 ...) -> double
  wasm_instantiation_result *instantiation;
} dumb_jit_result;

// Reads arguments from "args", calls the entry point and writes the return value to "result". Yielding is signaled
// through the size of the async stack, and exceptions are signaled through thread->current_exception.
typedef void (*jit_adapter_t)(void *entry, vm_thread *thread, stack_value *args, stack_value *result);

typedef struct {

} dumb_jit_options;

dumb_jit_result *dumb_jit_compile(cp_method *method, dumb_jit_options options);
void free_dumb_jit_result(dumb_jit_result *result);

// Create an adapter which converts a method call of the form (thread *, method *, arg1, arg2 ...) into an appropriate
// call to bjvm_interpret.
void *create_adapter_to_interpreter(cp_method *method) { return nullptr; }

jit_adapter_t *create_adapter_to_jit(cp_method *method) { return nullptr; }

#endif // DUMB_JIT_H
