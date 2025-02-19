#include "wasm_trampolines.h"
#include "util.h"

static string_hash_table jit_trampolines;
static string_hash_table interpreter_trampolines;

#define MUSTTAIL

// To store/look up in the hash table
static slice encode_as_slice(slice s, wasm_value_type return_type, wasm_value_type* stbds_args) {
  CHECK(1 + arrlenu(stbds_args) < s.len);
  int i = 0;
  s.chars[i++] = return_type;
  for (size_t j = 0; j < arrlenu(stbds_args); ++j) {
    s.chars[i++] = stbds_args[j];
  }
  return subslice_to(s, 0, i);
}

jit_trampoline get_wasm_jit_trampoline(wasm_value_type return_type, wasm_value_type* stbds_args) {
  INIT_STACK_STRING(key, 257);
  key = encode_as_slice(key, return_type, stbds_args);
  jit_trampoline existing = (jit_trampoline)hash_table_lookup(&jit_trampolines, key.chars, (int)key.len);
  if (existing)
    return existing;
  return nullptr;
}

interpreter_trampoline get_wasm_interpreter_trampoline(wasm_value_type return_type, wasm_value_type* stbds_args) {
  INIT_STACK_STRING(key, 257);
  key = encode_as_slice(key, return_type, stbds_args);
  jit_trampoline existing = (jit_trampoline)hash_table_lookup(&interpreter_trampolines, key.chars, (int)key.len);
  if (existing)
    return existing;
  return nullptr;
}

static void add_pregenerated_trampolines();
[[maybe_unused]] static void init_trampolines() {
  jit_trampolines = make_hash_table(nullptr, 0.75, 16);
  interpreter_trampolines = make_hash_table(nullptr, 0.75, 16);

  add_pregenerated_trampolines();
}

#include "pregenerated_wasm_trampolines.inc"