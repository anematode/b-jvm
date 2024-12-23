//
// Created by Cowpox on 12/23/24.
//

#ifndef TO_JS_H
#define TO_JS_H

#include <stdlib.h>

// The JS functions we create are very simple and have only four data types:
// uint32 (for pointers and numbers), int32 (for ints and longs, the latter of
// which are split into two variables because JS optimizers don't do well with
// bigints), double, and float.

// The calling convention for JS functions is that all variables take up one
// slot, except longs, which take two slots.

// There is also a set of local variables, which are not type checked. Longs
// naturally take up multiple local variables.

// FOR NOW, local variable 0 is fixed to be the branch PC, and we wrap the
// thing in a switch statement (structured control flow theorem). But this is
// very inefficient, so we'll improve it at some point.

typedef enum {
  // Double argument used for all and converted
  bjvm_js_const,

  // arg0 = arg1 + arg2
  bjvm_js_insn_add,
  // arg0 = arg1 - arg2
  bjvm_js_insn_sub,
  // arg0 = arg1 * arg2
  bjvm_js_insn_dmul,
  // arg0 = Math.imul(arg1, arg2)
  bjvm_js_insn_imul,

  // arg0 = arg1 / arg2
  bjvm_js_insn_div,
  // arg0 = arg1 % arg2
  bjvm_js_insn_rem,
  // arg0 = arg1 >> arg2
  bjvm_js_insn_shr,
  // arg0 = arg1 >>> arg2
  bjvm_js_insn_ushr,
  // arg0 = arg1 << arg2
  bjvm_js_insn_shl,
  // arg0 = arg1 & arg2
  bjvm_js_insn_and,
  // arg0 = arg1 | arg2
  bjvm_js_insn_or,
  // arg0 = arg1 ^ arg2
  bjvm_js_insn_xor,
  // arg0 = ~arg1
  bjvm_js_insn_not,
  // arg0 = -arg1
  bjvm_js_insn_neg,

  // arg0 = Math.fround(arg1)
  bjvm_js_x2f,
  // arg0 = arg1 | 0
  bjvm_js_x2i,
  // arg0 = arg1 >>> 0
  bjvm_js_x2u,
  // arg0 = arg1 * 2**32 + arg2  (correctly rounded)
  bjvm_js_l2d,
  // arg0 = Math.fround(Number(arg0))  (but w/o double rounding)
  bjvm_js_l2f,

  // arg0 = sext(HEAPI8[arg1])
  bjvm_js_load_i8,
  // arg0 = sext(HEAPI16[arg1 >> 1])
  bjvm_js_load_i16,
  // arg0 = sext(HEAPI32[arg1 >> 2])
  bjvm_js_load_i32,
  bjvm_js_load_u32,

  bjvm_js_load_f32,
  bjvm_js_load_f64,

  // HEAPI8[arg0] = arg1
  bjvm_js_store_i8,
  bjvm_js_store_i16,
  bjvm_js_store_i32,
  bjvm_js_store_u32,

  bjvm_js_store_f32,
  bjvm_js_store_f64,

  // Function call into webassembly
  bjvm_js_wasm_call,

  // Function call into JS
  bjvm_js_js_call,

  // Control-flow "instructions" (TODO flesh out)
  // arg0: switch (arg1) {
  bjvm_js_switch_on,
  // arg0: while (arg1) {
  bjvm_js_while,
  // arg0: if (arg1) {
  bjvm_js_if,
  // } else
  bjvm_js_else,
  // case arg1:   (arg1 a fixed int)
  bjvm_js_case,
  // default:
  bjvm_js_default,
  // break <arg0>
  bjvm_js_break_label,
  // continue <arg0>
  bjvm_js_continue_label,
  // }
  bjvm_js_close_block,
  // return <arg0>
  bjvm_js_return,
} bjvm_js_insn_kind;

typedef struct {
  bjvm_js_insn_kind kind;

  int *args;
  int argc;
} bjvm_js_insn;

typedef struct {
  char* code;
  int code_length;
  int current_indent;
} bjvm_js_assembler;

#endif // TO_JS_H
