// Trampolines to jump between JITed and interpreted code.
//
// It's faster to have some trampolines generated as part of the main module, both to avoid excess codegen at runtime
// and because context switches between modules are fairly expensive on some runtimes. Because trampolines always either
// have a start point or end point as part of the main module, we can skip one module cross per trampoline invocation.

#ifndef WASM_TRAMPOLINES_H
#define WASM_TRAMPOLINES_H

#include "bjvm.h"
#include "wasm/wasm_utils.h"

// args is an inout arg: the values are read, and the return value is written. This matches the semantics of bytecode
// invoke instructions, since the result is pushed back onto the operand stack.
typedef void (*jit_trampoline)(void *f, vm_thread *thread, cp_method *method, stack_value *args);
jit_trampoline get_wasm_jit_trampoline(wasm_value_type return_type, wasm_value_type *stbds_args);

typedef void *interpreter_trampoline;

// (thread, method, arg1, arg2, ... argN) -> return value
interpreter_trampoline get_wasm_interpreter_trampoline(wasm_value_type return_type, wasm_value_type *stbds_args);

#endif // WASM_TRAMPOLINES_H
