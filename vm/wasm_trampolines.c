#include "wasm_trampolines.h"
#include "util.h"

static string_hash_table jit_trampolines;
static string_hash_table interpreter_trampolines;

#define MUSTTAIL

static void init_trampolines();
static void add_pregenerated_trampolines();

// To store/look up in the hash table
static slice encode_as_slice(slice s, wasm_value_type return_type, wasm_value_type *args, s32 argc) {
  CHECK(1 + argc < (int)s.len);
  s32 i = 0;
  s.chars[i++] = return_type;
  for (int j = 0; j < argc; ++j) {
    s.chars[i++] = args[j];
  }
  return subslice_to(s, 0, i);
}

jit_trampoline get_wasm_jit_trampoline(wasm_value_type return_type, wasm_value_type *args, s32 argc) {
  init_trampolines();
  INIT_STACK_STRING(key, 257);
  key = encode_as_slice(key, return_type, args, argc);
  jit_trampoline existing = (jit_trampoline)hash_table_lookup(&jit_trampolines, key.chars, (int)key.len);
  if (existing)
    return existing;
  return nullptr;
}

interpreter_trampoline get_wasm_interpreter_trampoline(wasm_value_type return_type, wasm_value_type *args, s32 argc) {
  init_trampolines();
  INIT_STACK_STRING(key, 257);
  key = encode_as_slice(key, return_type, args, argc);
  interpreter_trampoline existing =
      (interpreter_trampoline)hash_table_lookup(&interpreter_trampolines, key.chars, (int)key.len);
  if (existing)
    return existing;
  return nullptr;
}

__attribute__((noinline)) static void init_trampolines() {
  if (jit_trampolines.entries_count > 0)
    return;
  jit_trampolines = make_hash_table(nullptr, 0.75, 16);
  interpreter_trampolines = make_hash_table(nullptr, 0.75, 16);
  add_pregenerated_trampolines();
}

#include "pregenerated_wasm_trampolines.inc"