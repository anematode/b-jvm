//
// Created by Cowpox on 1/27/25.
//

#ifndef DUMB_JIT_H
#define DUMB_JIT_H

#include "classfile.h"
#include "wasm_utils.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  bool inline_monomorphic;
  bool debug;
} dumb_jit_options;

// Create a dumb JIT instance for the method. Returns 0 on success (or if the method has already been generated).
// The function pointer replaces the into-interpreter adapter in jit_entry.
static int dumb_jit_compile(bjvm_cp_method *method, dumb_jit_options options);

#ifdef __cplusplus
}
#endif

#endif //DUMB_JIT_H
