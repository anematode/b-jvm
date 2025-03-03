// Lightweight version of the parts of Binaryen that we need, written in C

#ifndef WASM_UTILS_H
#define WASM_UTILS_H

#include "classfile.h"
#include "util.h"
#include <types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  WASM_TYPE_KIND_VOID,
  WASM_TYPE_KIND_FLOAT64 = 0x7C,
  WASM_TYPE_KIND_FLOAT32 = 0x7D,
  WASM_TYPE_KIND_INT64 = 0x7E,
  WASM_TYPE_KIND_INT32 = 0x7F
} wasm_value_type;

typedef struct {
  int types_len;
  wasm_value_type types[];
} wasm_tuple_type;

typedef struct {
  // if <= 255, it's a basic_wasm_type, otherwise it's a pointer to a
  // wasm_tuple_type
  uintptr_t val;
} wasm_type;

wasm_type wasm_void();
wasm_type wasm_int32();
wasm_type wasm_float32();
wasm_type wasm_float64();
wasm_type wasm_int64();

typedef enum {
  WASM_EXPR_KIND_DROP,
  WASM_EXPR_KIND_UNREACHABLE,
  WASM_EXPR_KIND_CONST,
  WASM_EXPR_KIND_SELECT,
  WASM_EXPR_KIND_GET_LOCAL,
  WASM_EXPR_KIND_SET_LOCAL,
  WASM_EXPR_KIND_UNARY_OP,
  WASM_EXPR_KIND_BINARY_OP,
  // Instruction takes in a memory argument (align and offset)
  WASM_EXPR_KIND_LOAD,
  WASM_EXPR_KIND_STORE,
  // Subsumes (block) and (loop)
  WASM_EXPR_KIND_BLOCK,
  WASM_EXPR_KIND_IF,
  // Subsumes (br) and (br_if)
  WASM_EXPR_KIND_BR,
  WASM_EXPR_KIND_BR_TABLE,
  WASM_EXPR_KIND_RETURN,
  WASM_EXPR_KIND_CALL,
  WASM_EXPR_KIND_CALL_INDIRECT,
} wasm_expr_kind;

// Generated from
// https://github.com/WebAssembly/spec/blob/f3a0e06235d2d84bb0f3b5014da4370613886965/interpreter/binary/encode.ml
typedef enum {
  /* i32 unary */
  WASM_OP_KIND_I32_EQZ = 0x45,
  WASM_OP_KIND_REF_EQZ = 0x45,
  WASM_OP_KIND_I32_CLZ = 0x67,
  WASM_OP_KIND_I32_CTZ = 0x68,
  WASM_OP_KIND_I32_POPCNT = 0x69,

  /* i64 unary */
  WASM_OP_KIND_I64_EQZ = 0x50,
  WASM_OP_KIND_I64_CLZ = 0x79,
  WASM_OP_KIND_I64_CTZ = 0x7A,
  WASM_OP_KIND_I64_POPCNT = 0x7B,

  /* f32 unary */
  WASM_OP_KIND_F32_ABS = 0x8B,
  WASM_OP_KIND_F32_NEG = 0x8C,
  WASM_OP_KIND_F32_CEIL = 0x8D,
  WASM_OP_KIND_F32_FLOOR = 0x8E,
  WASM_OP_KIND_F32_TRUNC = 0x8F,
  WASM_OP_KIND_F32_NEAREST = 0x90,
  WASM_OP_KIND_F32_SQRT = 0x91,

  /* f64 unary */
  WASM_OP_KIND_F64_ABS = 0x99,
  WASM_OP_KIND_F64_NEG = 0x9A,
  WASM_OP_KIND_F64_CEIL = 0x9B,
  WASM_OP_KIND_F64_FLOOR = 0x9C,
  WASM_OP_KIND_F64_TRUNC = 0x9D,
  WASM_OP_KIND_F64_NEAREST = 0x9E,
  WASM_OP_KIND_F64_SQRT = 0x9F,

  WASM_OP_KIND_I32_WRAP_I64 = 0xA7,
  WASM_OP_KIND_I32_TRUNC_S_F32 = 0xA8,
  WASM_OP_KIND_I32_TRUNC_S_F64 = 0xAA,
  WASM_OP_KIND_I64_EXTEND_S_I32 = 0xAC,
  WASM_OP_KIND_I64_EXTEND_U_I32 = 0xAD,
  WASM_OP_KIND_I64_TRUNC_S_F32 = 0xAE,
  WASM_OP_KIND_I64_TRUNC_S_F64 = 0xB0,
  WASM_OP_KIND_F32_CONVERT_S_I32 = 0xB2,
  WASM_OP_KIND_F32_CONVERT_S_I64 = 0xB4,
  WASM_OP_KIND_F32_DEMOTE_F64 = 0xB6,
  WASM_OP_KIND_F64_CONVERT_S_I32 = 0xB7,
  WASM_OP_KIND_F64_CONVERT_S_I64 = 0xB9,
  WASM_OP_KIND_F64_PROMOTE_F32 = 0xBB,

  // Used by Double.doubleToRawLongBits and friends
  WASM_OP_KIND_I32_REINTERPRET_F32 = 0xBC,
  WASM_OP_KIND_I64_REINTERPRET_F64 = 0xBD,
  WASM_OP_KIND_F32_REINTERPRET_I32 = 0xBE,
  WASM_OP_KIND_F64_REINTERPRET_I64 = 0xBF,

  WASM_OP_KIND_I32_EXTEND_S_I8 = 0xC0,
  WASM_OP_KIND_I32_EXTEND_S_I16 = 0xC1,

  WASM_OP_KIND_I32_TRUNC_SAT_F32_S = 0xFC00,
  WASM_OP_KIND_I32_TRUNC_SAT_F64_S = 0xFC02,
  WASM_OP_KIND_I64_TRUNC_SAT_F32_S = 0xFC03,
  WASM_OP_KIND_I64_TRUNC_SAT_F64_S = 0xFC05
} wasm_unary_op_kind;

typedef enum {
  /* Relational instructions (consume 2 operands, produce i32 boolean) */
  WASM_OP_KIND_I32_EQ = 0x46,
  WASM_OP_KIND_REF_EQ = 0x46,
  WASM_OP_KIND_I32_NE = 0x47,
  WASM_OP_KIND_REF_NE = 0x46,
  WASM_OP_KIND_I32_LT_S = 0x48,
  WASM_OP_KIND_I32_LT_U = 0x49,
  WASM_OP_KIND_I32_GT_S = 0x4A,
  WASM_OP_KIND_I32_GT_U = 0x4B,
  WASM_OP_KIND_I32_LE_S = 0x4C,
  WASM_OP_KIND_I32_LE_U = 0x4D,
  WASM_OP_KIND_I32_GE_S = 0x4E,
  WASM_OP_KIND_I32_GE_U = 0x4F,

  WASM_OP_KIND_I64_EQ = 0x51,
  WASM_OP_KIND_I64_NE = 0x52,
  WASM_OP_KIND_I64_LT_S = 0x53,
  WASM_OP_KIND_I64_LT_U = 0x54,
  WASM_OP_KIND_I64_GT_S = 0x55,
  WASM_OP_KIND_I64_GT_U = 0x56,
  WASM_OP_KIND_I64_LE_S = 0x57,
  WASM_OP_KIND_I64_LE_U = 0x58,
  WASM_OP_KIND_I64_GE_S = 0x59,
  WASM_OP_KIND_I64_GE_U = 0x5A,

  WASM_OP_KIND_F32_EQ = 0x5B,
  WASM_OP_KIND_F32_NE = 0x5C,
  WASM_OP_KIND_F32_LT = 0x5D,
  WASM_OP_KIND_F32_GT = 0x5E,
  WASM_OP_KIND_F32_LE = 0x5F,
  WASM_OP_KIND_F32_GE = 0x60,

  WASM_OP_KIND_F64_EQ = 0x61,
  WASM_OP_KIND_F64_NE = 0x62,
  WASM_OP_KIND_F64_LT = 0x63,
  WASM_OP_KIND_F64_GT = 0x64,
  WASM_OP_KIND_F64_LE = 0x65,
  WASM_OP_KIND_F64_GE = 0x66,

  /* Arithmetic/logical instructions (consume 2 numeric operands, produce 1
     numeric) */
  WASM_OP_KIND_I32_ADD = 0x6A,
  WASM_OP_KIND_I32_SUB = 0x6B,
  WASM_OP_KIND_I32_MUL = 0x6C,
  WASM_OP_KIND_I32_DIV_S = 0x6D,
  WASM_OP_KIND_I32_DIV_U = 0x6E,
  WASM_OP_KIND_I32_REM_S = 0x6F,
  WASM_OP_KIND_I32_REM_U = 0x70,
  WASM_OP_KIND_I32_AND = 0x71,
  WASM_OP_KIND_I32_OR = 0x72,
  WASM_OP_KIND_I32_XOR = 0x73,
  WASM_OP_KIND_I32_SHL = 0x74,
  WASM_OP_KIND_I32_SHR_S = 0x75,
  WASM_OP_KIND_I32_SHR_U = 0x76,
  WASM_OP_KIND_I32_ROTL = 0x77,
  WASM_OP_KIND_I32_ROTR = 0x78,

  WASM_OP_KIND_I64_ADD = 0x7C,
  WASM_OP_KIND_I64_SUB = 0x7D,
  WASM_OP_KIND_I64_MUL = 0x7E,
  WASM_OP_KIND_I64_DIV_S = 0x7F,
  WASM_OP_KIND_I64_DIV_U = 0x80,
  WASM_OP_KIND_I64_REM_S = 0x81,
  WASM_OP_KIND_I64_REM_U = 0x82,
  WASM_OP_KIND_I64_AND = 0x83,
  WASM_OP_KIND_I64_OR = 0x84,
  WASM_OP_KIND_I64_XOR = 0x85,
  WASM_OP_KIND_I64_SHL = 0x86,
  WASM_OP_KIND_I64_SHR_S = 0x87,
  WASM_OP_KIND_I64_SHR_U = 0x88,
  WASM_OP_KIND_I64_ROTL = 0x89,
  WASM_OP_KIND_I64_ROTR = 0x8A,

  WASM_OP_KIND_F32_ADD = 0x92,
  WASM_OP_KIND_F32_SUB = 0x93,
  WASM_OP_KIND_F32_MUL = 0x94,
  WASM_OP_KIND_F32_DIV = 0x95,
  WASM_OP_KIND_F32_MIN = 0x96,
  WASM_OP_KIND_F32_MAX = 0x97,
  WASM_OP_KIND_F32_COPYSIGN = 0x98,

  WASM_OP_KIND_F64_ADD = 0xA0,
  WASM_OP_KIND_F64_SUB = 0xA1,
  WASM_OP_KIND_F64_MUL = 0xA2,
  WASM_OP_KIND_F64_DIV = 0xA3,
  WASM_OP_KIND_F64_MIN = 0xA4,
  WASM_OP_KIND_F64_MAX = 0xA5,
  WASM_OP_KIND_F64_COPYSIGN = 0xA6,
} wasm_binary_op_kind;

typedef enum {
  WASM_OP_KIND_I32_LOAD = 0x28,
  WASM_OP_KIND_I64_LOAD = 0x29,
  WASM_OP_KIND_F32_LOAD = 0x2A,
  WASM_OP_KIND_F64_LOAD = 0x2B,
  WASM_OP_KIND_I32_LOAD8_S = 0x2C,
  WASM_OP_KIND_I32_LOAD8_U = 0x2D,
  WASM_OP_KIND_I32_LOAD16_S = 0x2E,
  WASM_OP_KIND_I32_LOAD16_U = 0x2F,
  WASM_OP_KIND_I64_LOAD8_S = 0x30,
  WASM_OP_KIND_I64_LOAD8_U = 0x31,
  WASM_OP_KIND_I64_LOAD16_S = 0x32,
  WASM_OP_KIND_I64_LOAD16_U = 0x33,
  WASM_OP_KIND_I64_LOAD32_S = 0x34,
  WASM_OP_KIND_I64_LOAD32_U = 0x35,
} wasm_load_op_kind;

typedef enum {
  WASM_OP_KIND_I32_STORE = 0x36,
  WASM_OP_KIND_I64_STORE = 0x37,
  WASM_OP_KIND_F32_STORE = 0x38,
  WASM_OP_KIND_F64_STORE = 0x39,
  WASM_OP_KIND_I32_STORE8 = 0x3A,
  WASM_OP_KIND_I32_STORE16 = 0x3B,
  WASM_OP_KIND_I64_STORE8 = 0x3C,
  WASM_OP_KIND_I64_STORE16 = 0x3D,
  WASM_OP_KIND_I64_STORE32 = 0x3E
} wasm_store_op_kind;

typedef struct wasm_expression wasm_expression;

typedef struct {
  wasm_unary_op_kind op;
  wasm_expression *arg;
} wasm_unary_expression;

typedef struct {
  wasm_binary_op_kind op;
  wasm_expression *left;
  wasm_expression *right;
} wasm_binary_expression;

typedef struct {
  wasm_load_op_kind op;
  wasm_expression *addr;
  int align;
  int offset;
} wasm_load_expression;

typedef struct {
  wasm_store_op_kind op;
  wasm_expression *addr;
  wasm_expression *value;
  int align;
  int offset;
} wasm_store_expression;

typedef struct {
  wasm_expression *condition;
  wasm_expression *true_expr;
  wasm_expression *false_expr;
} wasm_select_expression;

typedef struct {
  wasm_expression *condition; // Condition to break on (may be null)
  wasm_expression *break_to;  // Pointer to enclosing block
} wasm_br_expression;

typedef struct {
  wasm_expression *condition; // Condition to switch on
  wasm_expression **exprs;    // value to break to for each index
  int expr_count;
  wasm_expression *dflt; // default to break to
} wasm_br_table_expression;

typedef struct wasm_function wasm_function;

typedef struct {
  wasm_expression *condition; // may be null
  wasm_expression *expr;
  wasm_function *to_call;
  wasm_expression **args;
  int arg_count;
  bool tail_call;
} wasm_call_expression;

typedef struct {
  int table_index;
  wasm_expression *index;

  wasm_expression **args;
  u32 function_type;
  int arg_count;
  bool tail_call;
} wasm_call_indirect_expression;

typedef struct {
  wasm_expression **exprs;
  int expr_count;
} wasm_expression_list;

// Used for both (block ...) and (loop ...)
typedef struct {
  bool is_loop;
  wasm_expression_list list;
} wasm_block_expression;

typedef struct {
  wasm_expression *condition;
  wasm_expression *true_expr;
  wasm_expression *false_expr; // May be null
} wasm_if_expression;

typedef enum {
  WASM_LITERAL_KIND_I32 = 0x41,
  WASM_LITERAL_KIND_I64 = 0x42,
  WASM_LITERAL_KIND_F32 = 0x43,
  WASM_LITERAL_KIND_F64 = 0x44,
} wasm_literal_kind;

typedef struct {
  wasm_literal_kind kind;
  char bytes[8]; // little endian ofc
} wasm_literal;

typedef struct {
  u32 local_index;
  wasm_expression *value;
} wasm_local_set_expression;

typedef struct wasm_expression {
  wasm_expr_kind kind;
  // type of the expression itself, e.g. (i32.add (i32.const 1) (i32.const 2))
  // would have type i32.
  wasm_type expr_type;

  union {
    wasm_unary_expression unary_op;
    wasm_binary_expression binary_op;
    wasm_block_expression block;
    wasm_if_expression if_;
    wasm_select_expression select;
    wasm_br_expression br;
    wasm_br_table_expression br_table;
    wasm_call_expression call;
    wasm_call_indirect_expression call_indirect;
    wasm_literal literal;
    wasm_load_expression load;
    wasm_store_expression store;
    wasm_local_set_expression local_set;

    u32 local_get;
    wasm_expression *return_expr;
  };
} wasm_expression;

typedef struct wasm_function {
  const char *name; // both internal and external, for convenience
  wasm_type params;
  wasm_type results;
  // Tuple type, converted to a list of basic types during serialisation
  wasm_type locals;
  wasm_expression *body;
  u32 my_index;

  bool exported;
} wasm_function;

typedef struct {
  wasm_type params;
  wasm_type results;
} wasm_ser_function_type;

typedef struct {
  u32 type;
  wasm_function *associated;
} wasm_func_import;

typedef enum {
  WASM_IMPORT_KIND_FUNC = 0x00,
  WASM_IMPORT_KIND_TABLE = 0x01,
  WASM_IMPORT_KIND_MEMORY = 0x02,
} wasm_import_kind;

typedef struct {
  const char *module; // e.g. env
  const char *name;   // e.g. raise_npe

  union {
    wasm_func_import func;
    // Put here more imports down the line besides
    // the fixed memory and table imports
  };
} wasm_import;

typedef struct {
  /** Used during construction of the module */
  wasm_tuple_type **interned_result_types;

  wasm_import *imports;

  wasm_function **functions;

  /** Used during serialisation only */
  wasm_ser_function_type *fn_types; // wasm_function_type*

  u32 fn_index;

  arena arena;
} wasm_module;

// Used when writing out the WASM to a series of bytes.
typedef struct {
  u8 *bytes;
} bytevector;

// LEB128 encodings
void wasm_writeuint(bytevector *ctx, u64 value);
void wasm_writeint(bytevector *ctx, s64 value);

wasm_module *wasm_module_create();
// It is the caller's responsibility to free the returned bytevector.
bytevector wasm_module_serialize(wasm_module *module);
void wasm_module_free(wasm_module *module);

// Make a result type out of the given components
wasm_type wasm_make_tuple(wasm_module *module, wasm_value_type *components, int length);
// Returns null if the type is not a result type
wasm_tuple_type *wasm_get_tuple_type(wasm_type type);
// Aborts if the type is a result type
wasm_value_type wasm_get_basic_type(wasm_type type);

wasm_function *wasm_add_function(wasm_module *module, wasm_type params, wasm_type results, wasm_type locals,
                                 wasm_expression *body, const char *name);

void wasm_export_function(wasm_module *module, wasm_function *fn);

wasm_type wasm_string_to_tuple(wasm_module *module, const char *str);

wasm_function *wasm_import_runtime_function_impl(wasm_module *module, const char *c_name,
                                                 const char *sig /* e.g. viii */);

wasm_expression *wasm_i32_const(wasm_module *module, int value);
wasm_expression *wasm_f32_const(wasm_module *module, float value);
wasm_expression *wasm_f64_const(wasm_module *module, double value);
wasm_expression *wasm_i64_const(wasm_module *module, s64 value);
wasm_expression *wasm_local_get(wasm_module *module, u32 index, wasm_type kind);
wasm_expression *wasm_local_set(wasm_module *module, u32 index, wasm_expression *value);
wasm_expression *wasm_unreachable(wasm_module *module);
wasm_expression *wasm_unop(wasm_module *module, wasm_unary_op_kind op, wasm_expression *expr);
wasm_expression *wasm_binop(wasm_module *module, wasm_binary_op_kind op, wasm_expression *left, wasm_expression *right);
wasm_expression *wasm_select(wasm_module *module, wasm_expression *condition, wasm_expression *true_expr,
                             wasm_expression *false_expr);
wasm_expression *wasm_block(wasm_module *module, wasm_expression **exprs, int expr_count, wasm_type type, bool is_loop);
wasm_expression *wasm_update_block(wasm_module *module, wasm_expression *existing_block, wasm_expression **exprs,
                                   int expr_count, wasm_type type, bool is_loop);
wasm_expression *wasm_br(wasm_module *module, wasm_expression *condition, wasm_expression *break_to);
wasm_expression *wasm_call(wasm_module *module, wasm_function *fn, wasm_expression **args, int arg_count);
wasm_expression *wasm_call_indirect(wasm_module *module, int table_index, wasm_expression *index,
                                    wasm_expression **args, int arg_count, u32 functype);
wasm_expression *wasm_load(wasm_module *module, wasm_load_op_kind op, wasm_expression *addr, int align, int offset);
wasm_expression *wasm_store(wasm_module *module, wasm_store_op_kind op, wasm_expression *addr, wasm_expression *value,
                            int align, int offset);
wasm_expression *wasm_if_else(wasm_module *module, wasm_expression *cond, wasm_expression *true_expr,
                              wasm_expression *false_expr, wasm_type type);
wasm_expression *wasm_return(wasm_module *module, wasm_expression *expr);

u32 register_function_type(wasm_module *module, wasm_type params, wasm_type results);

typedef enum {
  WASM_INSTANTIATION_SUCCESS,
  // instantiateAsync was used, and js_promise is set
  WASM_INSTANTIATION_PENDING,
  WASM_INSTANTIATION_FAIL,
} wasm_instantiation_status;

typedef struct {
  heap_string name;
  // All exported functions are stored as a void pointer
  void *export_;
} wasm_instantiation_export;

typedef struct {
  wasm_instantiation_status status;
  int js_promise;

  wasm_instantiation_export *exports; // unused for now

  void *run;
} wasm_instantiation_result;

void free_wasm_instantiation_result(wasm_instantiation_result *result);
wasm_instantiation_result *wasm_instantiate_module(wasm_module *module, const char *debug_name);

/** fma_support.c, WASM only */

#ifdef EMSCRIPTEN
typedef float (*wasm_fmaf_handle)(float a, float b, float c);
typedef double (*wasm_fma_handle)(double a, double b, double c);

extern wasm_fmaf_handle wasm_fmaf_impl;
extern wasm_fma_handle wasm_fma_impl;

void wasm_init_fma_handles();
#endif

#ifdef __cplusplus
}
#endif

#endif // WASM_UTILS_H
