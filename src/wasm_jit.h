//
// Created by Cowpox on 12/23/24.
//

#ifndef JIT_H
#define JIT_H

#include "bjvm.h"
#include "wasm_utils.h"
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

wasm_instantiation_result *
wasm_jit_compile(bjvm_thread *thread, const bjvm_cp_method *method,
                      bool debug);
void free_wasm_compiled_method(void *compiled_method);

void bjvm_translate();
wasm_type jvm_type_to_wasm(bjvm_type_kind kind);

#ifdef __cplusplus
}
#endif

#endif // JIT_H
