//
// Created by Cowpox on 12/23/24.
//

#ifndef JIT_H
#define JIT_H

#include <stdlib.h>
#include "bjvm.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // Used to pass the compiled binary into JS to be instantiated
  void *data;
  uint32_t bytes;
  // Whether the function is ready to be called (because we may use
  // WebAssembly.instantiateAsync)
  bool ready;
  // Dynamically added function pointer
  bjvm_interpreter_result_t (*fn)(intptr_t thread, intptr_t frame, intptr_t result);
} bjvm_wasm_jit_compiled_method;

bjvm_wasm_jit_compiled_method* bjvm_wasm_jit_compile(bjvm_thread *thread,
                                                     const bjvm_cp_method* method);
void free_wasm_compiled_method(void *compiled_method);

void bjvm_translate();

#ifdef __cplusplus
}
#endif

#endif // JIT_H
