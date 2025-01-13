//
// Created by Cowpox on 1/13/25.
//

#ifndef WASM_ADAPTER_H
#define WASM_ADAPTER_H

#include "bjvm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef bjvm_interpreter_result_t (*compiled_method_adapter_t)(bjvm_thread *thread,
  bjvm_stack_value *result, bjvm_stack_value* args, void *fn);

compiled_method_adapter_t create_adapter_to_compiled_method(bjvm_type_kind *kinds, int kinds_len);
void *create_adapter_to_interpreter(bjvm_type_kind *kinds, int kinds_len, bool is_native);

#ifdef __cplusplus
}
#endif

#endif //WASM_ADAPTER_H
