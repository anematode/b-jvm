// Lightweight version of the parts of Binaryen that we need, written in C
// TODO

#ifndef WASM_UTILS_H
#define WASM_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "util.h"
#include <stdint.h>

typedef enum {
  BJVM_WASM_TYPE_KIND_VOID,
  BJVM_WASM_TYPE_KIND_INT32 = 0x7F,
  BJVM_WASM_TYPE_KIND_INT64 = 0x7E,
  BJVM_WASM_TYPE_KIND_FLOAT32 = 0x7D,
  BJVM_WASM_TYPE_KIND_FLOAT64 = 0x7C,

  // during serialization, we'll replace this with the correct type from
  // context (todo)
  BJVM_WASM_TYPE_KIND_INFER
} bjvm_wasm_value_type;

typedef struct {
  int types_len;
  bjvm_wasm_value_type types[];
} bjvm_wasm_tuple_type;

typedef struct {
  // if <= 255, it's a bjvm_basic_wasm_type, otherwise it's a pointer to a
  // bjvm_wasm_tuple_type
  uintptr_t val;
} bjvm_wasm_type;

bjvm_wasm_type bjvm_wasm_void();
bjvm_wasm_type bjvm_wasm_infer();

bjvm_wasm_type bjvm_wasm_int32();
bjvm_wasm_type bjvm_wasm_float32();
bjvm_wasm_type bjvm_wasm_float64();
bjvm_wasm_type bjvm_wasm_int64();

typedef enum {
  BJVM_WASM_EXPR_KIND_DROP,
  BJVM_WASM_EXPR_KIND_CONST,
  BJVM_WASM_EXPR_KIND_SELECT,
  BJVM_WASM_EXPR_KIND_GET_LOCAL,
  BJVM_WASM_EXPR_KIND_SET_LOCAL,
  BJVM_WASM_EXPR_KIND_UNARY_OP,
  BJVM_WASM_EXPR_KIND_BINARY_OP,
  // Instruction takes in a memory argument (align and offset)
  BJVM_WASM_EXPR_KIND_LOAD,
  BJVM_WASM_EXPR_KIND_STORE,
  // Both block and loop go here
  BJVM_WASM_EXPR_KIND_BLOCK,
  BJVM_WASM_EXPR_KIND_IF,
  // Both br and br_if go here
  BJVM_WASM_EXPR_KIND_BR,
  BJVM_WASM_EXPR_KIND_BR_TABLE,
  BJVM_WASM_EXPR_KIND_RETURN,
  BJVM_WASM_EXPR_KIND_CALL,
  BJVM_WASM_EXPR_KIND_CALL_INDIRECT,

} bjvm_wasm_expr_kind;

// Generated from
// https://github.com/WebAssembly/spec/blob/f3a0e06235d2d84bb0f3b5014da4370613886965/interpreter/binary/encode.ml
typedef enum {
  /* i32 unary */
  BJVM_WASM_OP_KIND_I32_EQZ = 0x45,
  BJVM_WASM_OP_KIND_I32_CLZ = 0x67,
  BJVM_WASM_OP_KIND_I32_CTZ = 0x68,
  BJVM_WASM_OP_KIND_I32_POPCNT = 0x69,

  /* i64 unary */
  BJVM_WASM_OP_KIND_I64_EQZ = 0x50,
  BJVM_WASM_OP_KIND_I64_CLZ = 0x79,
  BJVM_WASM_OP_KIND_I64_CTZ = 0x7A,
  BJVM_WASM_OP_KIND_I64_POPCNT = 0x7B,

  /* f32 unary */
  BJVM_WASM_OP_KIND_F32_ABS = 0x8B,
  BJVM_WASM_OP_KIND_F32_NEG = 0x8C,
  BJVM_WASM_OP_KIND_F32_CEIL = 0x8D,
  BJVM_WASM_OP_KIND_F32_FLOOR = 0x8E,
  BJVM_WASM_OP_KIND_F32_TRUNC = 0x8F,
  BJVM_WASM_OP_KIND_F32_NEAREST = 0x90,
  BJVM_WASM_OP_KIND_F32_SQRT = 0x91,

  /* f64 unary */
  BJVM_WASM_OP_KIND_F64_ABS = 0x99,
  BJVM_WASM_OP_KIND_F64_NEG = 0x9A,
  BJVM_WASM_OP_KIND_F64_CEIL = 0x9B,
  BJVM_WASM_OP_KIND_F64_FLOOR = 0x9C,
  BJVM_WASM_OP_KIND_F64_TRUNC = 0x9D,
  BJVM_WASM_OP_KIND_F64_NEAREST = 0x9E,
  BJVM_WASM_OP_KIND_F64_SQRT = 0x9F,

  BJVM_WASM_OP_KIND_I32_WRAP_I64 = 0xA7,
  BJVM_WASM_OP_KIND_I32_TRUNC_S_F32 = 0xA8,
  BJVM_WASM_OP_KIND_I32_TRUNC_S_F64 = 0xAA,
  BJVM_WASM_OP_KIND_I64_TRUNC_S_F32 = 0xAE,
  BJVM_WASM_OP_KIND_I64_TRUNC_S_F64 = 0xB0,
  BJVM_WASM_OP_KIND_F32_CONVERT_S_I32 = 0xB2,
  BJVM_WASM_OP_KIND_F32_CONVERT_S_I64 = 0xB4,
  BJVM_WASM_OP_KIND_F32_DEMOTE_F64 = 0xB6,
  BJVM_WASM_OP_KIND_F64_CONVERT_S_I32 = 0xB7,
  BJVM_WASM_OP_KIND_F64_CONVERT_S_I64 = 0xB9,
  BJVM_WASM_OP_KIND_F64_PROMOTE_F32 = 0xBB,

  // Used by Double.doubleToRawLongBits and friends
  BJVM_WASM_OP_KIND_I32_REINTERPRET_F32 = 0xBC,
  BJVM_WASM_OP_KIND_I64_REINTERPRET_F64 = 0xBD,
  BJVM_WASM_OP_KIND_F32_REINTERPRET_I32 = 0xBE,
  BJVM_WASM_OP_KIND_F64_REINTERPRET_I64 = 0xBF,

  BJVM_WASM_OP_KIND_I32_EXTEND_S_I8 = 0xC0,
  BJVM_WASM_OP_KIND_I32_EXTEND_S_I16 = 0xC1,

  BJVM_WASM_OP_KIND_I32_TRUNC_SAT_F32_S = 0xFC00,
  BJVM_WASM_OP_KIND_I32_TRUNC_SAT_F64_S = 0xFC02,
  BJVM_WASM_OP_KIND_I64_TRUNC_SAT_F32_S = 0xFC03,
  BJVM_WASM_OP_KIND_I64_TRUNC_SAT_F64_S = 0xFC05,
  BJVM_WASM_OP_KIND_I64_EXTEND_S_I32 = 0xFC0C,
  BJVM_WASM_OP_KIND_I64_EXTEND_U_I32 = 0xFC0D,
} bjvm_wasm_unary_op_kind;

typedef enum {
  /* Relational instructions (consume 2 operands, produce i32 boolean) */
  BJVM_WASM_OP_KIND_I32_EQ = 0x46,
  BJVM_WASM_OP_KIND_I32_NE = 0x47,
  BJVM_WASM_OP_KIND_I32_LT_S = 0x48,
  BJVM_WASM_OP_KIND_I32_LT_U = 0x49,
  BJVM_WASM_OP_KIND_I32_GT_S = 0x4A,
  BJVM_WASM_OP_KIND_I32_GT_U = 0x4B,
  BJVM_WASM_OP_KIND_I32_LE_S = 0x4C,
  BJVM_WASM_OP_KIND_I32_LE_U = 0x4D,
  BJVM_WASM_OP_KIND_I32_GE_S = 0x4E,
  BJVM_WASM_OP_KIND_I32_GE_U = 0x4F,

  BJVM_WASM_OP_KIND_I64_EQ = 0x51,
  BJVM_WASM_OP_KIND_I64_NE = 0x52,
  BJVM_WASM_OP_KIND_I64_LT_S = 0x53,
  BJVM_WASM_OP_KIND_I64_LT_U = 0x54,
  BJVM_WASM_OP_KIND_I64_GT_S = 0x55,
  BJVM_WASM_OP_KIND_I64_GT_U = 0x56,
  BJVM_WASM_OP_KIND_I64_LE_S = 0x57,
  BJVM_WASM_OP_KIND_I64_LE_U = 0x58,
  BJVM_WASM_OP_KIND_I64_GE_S = 0x59,
  BJVM_WASM_OP_KIND_I64_GE_U = 0x5A,

  BJVM_WASM_OP_KIND_F32_EQ = 0x5B,
  BJVM_WASM_OP_KIND_F32_NE = 0x5C,
  BJVM_WASM_OP_KIND_F32_LT = 0x5D,
  BJVM_WASM_OP_KIND_F32_GT = 0x5E,
  BJVM_WASM_OP_KIND_F32_LE = 0x5F,
  BJVM_WASM_OP_KIND_F32_GE = 0x60,

  BJVM_WASM_OP_KIND_F64_EQ = 0x61,
  BJVM_WASM_OP_KIND_F64_NE = 0x62,
  BJVM_WASM_OP_KIND_F64_LT = 0x63,
  BJVM_WASM_OP_KIND_F64_GT = 0x64,
  BJVM_WASM_OP_KIND_F64_LE = 0x65,
  BJVM_WASM_OP_KIND_F64_GE = 0x66,

  /* Arithmetic/logical instructions (consume 2 numeric operands, produce 1
     numeric) */
  BJVM_WASM_OP_KIND_I32_ADD = 0x6A,
  BJVM_WASM_OP_KIND_I32_SUB = 0x6B,
  BJVM_WASM_OP_KIND_I32_MUL = 0x6C,
  BJVM_WASM_OP_KIND_I32_DIV_S = 0x6D,
  BJVM_WASM_OP_KIND_I32_DIV_U = 0x6E,
  BJVM_WASM_OP_KIND_I32_REM_S = 0x6F,
  BJVM_WASM_OP_KIND_I32_REM_U = 0x70,
  BJVM_WASM_OP_KIND_I32_AND = 0x71,
  BJVM_WASM_OP_KIND_I32_OR = 0x72,
  BJVM_WASM_OP_KIND_I32_XOR = 0x73,
  BJVM_WASM_OP_KIND_I32_SHL = 0x74,
  BJVM_WASM_OP_KIND_I32_SHR_S = 0x75,
  BJVM_WASM_OP_KIND_I32_SHR_U = 0x76,
  BJVM_WASM_OP_KIND_I32_ROTL = 0x77,
  BJVM_WASM_OP_KIND_I32_ROTR = 0x78,

  BJVM_WASM_OP_KIND_I64_ADD = 0x7C,
  BJVM_WASM_OP_KIND_I64_SUB = 0x7D,
  BJVM_WASM_OP_KIND_I64_MUL = 0x7E,
  BJVM_WASM_OP_KIND_I64_DIV_S = 0x7F,
  BJVM_WASM_OP_KIND_I64_DIV_U = 0x80,
  BJVM_WASM_OP_KIND_I64_REM_S = 0x81,
  BJVM_WASM_OP_KIND_I64_REM_U = 0x82,
  BJVM_WASM_OP_KIND_I64_AND = 0x83,
  BJVM_WASM_OP_KIND_I64_OR = 0x84,
  BJVM_WASM_OP_KIND_I64_XOR = 0x85,
  BJVM_WASM_OP_KIND_I64_SHL = 0x86,
  BJVM_WASM_OP_KIND_I64_SHR_S = 0x87,
  BJVM_WASM_OP_KIND_I64_SHR_U = 0x88,
  BJVM_WASM_OP_KIND_I64_ROTL = 0x89,
  BJVM_WASM_OP_KIND_I64_ROTR = 0x8A,

  BJVM_WASM_OP_KIND_F32_ADD = 0x92,
  BJVM_WASM_OP_KIND_F32_SUB = 0x93,
  BJVM_WASM_OP_KIND_F32_MUL = 0x94,
  BJVM_WASM_OP_KIND_F32_DIV = 0x95,
  BJVM_WASM_OP_KIND_F32_MIN = 0x96,
  BJVM_WASM_OP_KIND_F32_MAX = 0x97,
  BJVM_WASM_OP_KIND_F32_COPYSIGN = 0x98,

  BJVM_WASM_OP_KIND_F64_ADD = 0xA0,
  BJVM_WASM_OP_KIND_F64_SUB = 0xA1,
  BJVM_WASM_OP_KIND_F64_MUL = 0xA2,
  BJVM_WASM_OP_KIND_F64_DIV = 0xA3,
  BJVM_WASM_OP_KIND_F64_MIN = 0xA4,
  BJVM_WASM_OP_KIND_F64_MAX = 0xA5,
  BJVM_WASM_OP_KIND_F64_COPYSIGN = 0xA6,
} bjvm_wasm_binary_op_kind;

typedef enum {
  BJVM_WASM_OP_KIND_I32_LOAD = 0x28,
  BJVM_WASM_OP_KIND_I64_LOAD = 0x29,
  BJVM_WASM_OP_KIND_F32_LOAD = 0x2A,
  BJVM_WASM_OP_KIND_F64_LOAD = 0x2B,
  BJVM_WASM_OP_KIND_I32_LOAD8_S = 0x2C,
  BJVM_WASM_OP_KIND_I32_LOAD8_U = 0x2D,
  BJVM_WASM_OP_KIND_I32_LOAD16_S = 0x2E,
  BJVM_WASM_OP_KIND_I32_LOAD16_U = 0x2F,
  BJVM_WASM_OP_KIND_I64_LOAD8_S = 0x30,
  BJVM_WASM_OP_KIND_I64_LOAD8_U = 0x31,
  BJVM_WASM_OP_KIND_I64_LOAD16_S = 0x32,
  BJVM_WASM_OP_KIND_I64_LOAD16_U = 0x33,
  BJVM_WASM_OP_KIND_I64_LOAD32_S = 0x34,
  BJVM_WASM_OP_KIND_I64_LOAD32_U = 0x35,
} bjvm_wasm_load_op_kind;

typedef enum {
  BJVM_WASM_OP_KIND_I32_STORE = 0x36,
  BJVM_WASM_OP_KIND_I64_STORE = 0x37,
  BJVM_WASM_OP_KIND_F32_STORE = 0x38,
  BJVM_WASM_OP_KIND_F64_STORE = 0x39,
  BJVM_WASM_OP_KIND_I32_STORE8 = 0x3A,
  BJVM_WASM_OP_KIND_I32_STORE16 = 0x3B,
  BJVM_WASM_OP_KIND_I64_STORE8 = 0x3C,
  BJVM_WASM_OP_KIND_I64_STORE16 = 0x3D,
  BJVM_WASM_OP_KIND_I64_STORE32 = 0x3E
} bjvm_wasm_store_op_kind;

typedef struct bjvm_wasm_expression bjvm_wasm_expression;

typedef struct {
  bjvm_wasm_unary_op_kind op;
  bjvm_wasm_expression *arg;
} bjvm_wasm_unary_expression;

typedef struct {
  bjvm_wasm_binary_op_kind op;
  bjvm_wasm_expression *left;
  bjvm_wasm_expression *right;
} bjvm_wasm_binary_expression;

typedef struct {
  bjvm_wasm_load_op_kind op;
  bjvm_wasm_expression *addr;
  int align;
  int offset;
} bjvm_wasm_load_expression;

typedef struct {
  bjvm_wasm_store_op_kind op;
  bjvm_wasm_expression *addr;
  bjvm_wasm_expression *value;
  int align;
  int offset;
} bjvm_wasm_store_expression;

typedef struct {
  bjvm_wasm_expression *condition;
  bjvm_wasm_expression *true_expr;
  bjvm_wasm_expression *false_expr;
} bjvm_wasm_select_expression;

typedef struct {
  bjvm_wasm_expression *condition; // Condition to break on (may be null)
  bjvm_wasm_expression *break_to;  // Pointer to enclosing block
} bjvm_wasm_br_expression;

typedef struct {
  bjvm_wasm_expression *condition; // Condition to switch on
  bjvm_wasm_expression **exprs;    // value to break to for each index
  int expr_count;
  bjvm_wasm_expression *dflt; // default to break to
} bjvm_wasm_br_table_expression;

typedef struct bjvm_wasm_function bjvm_wasm_function;

typedef struct {
  bjvm_wasm_expression *condition; // may be null
  bjvm_wasm_expression *expr;
  bjvm_wasm_function *to_call;
  bjvm_wasm_expression **args;
  int arg_count;
} bjvm_wasm_call_expression;

typedef struct {
  int table_index;
  bjvm_wasm_expression *index;

  bjvm_wasm_expression **args;
  int arg_count;
} bjvm_wasm_call_indirect_expression;

typedef struct {
  bjvm_wasm_expression **exprs;
  int expr_count;
} bjvm_wasm_expression_list;

// Used for both (block ...) and (loop ...)
typedef struct {
  bool is_loop;
  bjvm_wasm_expression_list list;
} bjvm_wasm_block_expression;

typedef struct {
  bjvm_wasm_expression *condition;
  bjvm_wasm_expression *true_expr;
  bjvm_wasm_expression *false_expr; // May be null
} bjvm_wasm_if_expression;

typedef enum {
  BJVM_WASM_LITERAL_KIND_I32 = 0x41,
  BJVM_WASM_LITERAL_KIND_I64 = 0x42,
  BJVM_WASM_LITERAL_KIND_F32 = 0x43,
  BJVM_WASM_LITERAL_KIND_F64 = 0x44,
} bjvm_wasm_literal_kind;

typedef struct {
  bjvm_wasm_literal_kind kind;
  char bytes[8]; // little endian ofc
} bjvm_wasm_literal;

typedef struct {
  uint32_t local_index;
  bjvm_wasm_expression *value;
} bjvm_wasm_local_set_expression;

typedef struct bjvm_wasm_expression {
  bjvm_wasm_expr_kind kind;
  // type of the expression itself, e.g. (i32.add (i32.const 1) (i32.const 2))
  // would have type i32.
  bjvm_wasm_type expr_type;

  union {
    bjvm_wasm_unary_expression unary_op;
    bjvm_wasm_binary_expression binary_op;
    bjvm_wasm_block_expression block;
    bjvm_wasm_if_expression if_;
    bjvm_wasm_select_expression select;
    bjvm_wasm_br_expression br;
    bjvm_wasm_br_table_expression br_table;
    bjvm_wasm_call_expression call;
    bjvm_wasm_call_indirect_expression call_indirect;
    bjvm_wasm_literal literal;
    bjvm_wasm_load_expression load;
    bjvm_wasm_store_expression store;
    bjvm_wasm_local_set_expression local_set;

    uint32_t local_get;
    bjvm_wasm_expression *return_expr;
  };
} bjvm_wasm_expression;

typedef struct bjvm_wasm_function {
  const char *name; // both internal and external, for convenience
  bjvm_wasm_type params;
  bjvm_wasm_type results;
  // Tuple type, converted to a list of basic types during serialisation
  bjvm_wasm_type locals;
  bjvm_wasm_expression *body;
  uint32_t my_index;

  bool exported;
} bjvm_wasm_function;

typedef struct {
  bjvm_wasm_type params;
  bjvm_wasm_type results;
} bjvm_wasm_ser_function_type;

typedef struct {
  uint32_t type;
  bjvm_wasm_function *associated;
} bjvm_wasm_func_import;

typedef enum {
  BJVM_WASM_IMPORT_KIND_FUNC = 0x00,
  BJVM_WASM_IMPORT_KIND_MEMORY = 0x02,
} bjvm_wasm_import_kind;

typedef struct {
  const char *module; // e.g. env
  const char *name;   // e.g. raise_npe

  union {
    bjvm_wasm_func_import func;
    // Put here more imports down the line besides
    // the fixed memory and table imports
  };
} bjvm_wasm_import;

typedef struct {
  /** Used during construction of the module */
  bjvm_wasm_tuple_type **interned_result_types;
  int result_types_count;
  int result_types_cap;

  bjvm_wasm_import *imports;
  int import_count;
  int import_cap;

  bjvm_wasm_function **functions;
  int function_count;
  int function_cap;

  /** Used during serialisation only */
  bjvm_wasm_ser_function_type *fn_types; // bjvm_wasm_function_type*
  int fn_types_count;
  int fn_types_cap;

  uint32_t fn_index;

  /** Allocation stuffs */

  // Vector of immovable arena regions, all collectively freed when the module
  // is destroyed
  char **arenas;
  int arenas_count;
  int arenas_cap;
  int last_arena_used;
} bjvm_wasm_module;

// Used when writing out the WASM to a series of bytes.
typedef struct {
  // Bytes of the currenf
  uint8_t *bytes;
  size_t bytes_len;
  size_t bytes_cap;
} bjvm_bytevector;

// LEB128 encodings
void bjvm_wasm_writeuint(bjvm_bytevector *ctx, uint64_t value);
void bjvm_wasm_writeint(bjvm_bytevector *ctx, int64_t value);

bjvm_wasm_module *bjvm_wasm_module_create();
// It is the caller's responsibility to free the returned bytevector.
bjvm_bytevector bjvm_wasm_module_serialize(bjvm_wasm_module *module);
void bjvm_wasm_module_destroy(bjvm_wasm_module *module);

// Make a result type out of the given components
bjvm_wasm_type bjvm_wasm_make_tuple(bjvm_wasm_module *module,
                                    bjvm_wasm_value_type *components,
                                    int length);
// Returns null if the type is not a result type
bjvm_wasm_tuple_type *bjvm_wasm_get_tuple_type(bjvm_wasm_type type);
// Aborts if the type is a result type
bjvm_wasm_value_type bjvm_wasm_get_basic_type(bjvm_wasm_type type);

bjvm_wasm_function *
bjvm_wasm_add_function(bjvm_wasm_module *module, bjvm_wasm_type params,
                       bjvm_wasm_type results, bjvm_wasm_type locals,
                       bjvm_wasm_expression *body, const char *name);

void bjvm_wasm_export_function(bjvm_wasm_module *module,
                               bjvm_wasm_function *fn);

bjvm_wasm_function *
bjvm_wasm_import_runtime_function_impl(bjvm_wasm_module *module,
                                       const char *c_name,
                                       const char *params, // e.g. iii
                                       const char *result, // e.g. v
                                       void *dummy);

// The function must be marked EMSCRIPTEN_KEEPALIVE for this to work!
#define bjvm_wasm_import_runtime_function(module, c_name, params, result)      \
  bjvm_wasm_import_runtime_function_impl(module, #c_name, params, result,      \
                                         &c_name)

bjvm_wasm_expression *bjvm_wasm_i32_const(bjvm_wasm_module *module, int value);
bjvm_wasm_expression *bjvm_wasm_f32_const(bjvm_wasm_module *module,
                                          float value);
bjvm_wasm_expression *bjvm_wasm_f64_const(bjvm_wasm_module *module,
                                          double value);
bjvm_wasm_expression *bjvm_wasm_i64_const(bjvm_wasm_module *module,
                                          int64_t value);
bjvm_wasm_expression *bjvm_wasm_local_get(bjvm_wasm_module *module,
                                          uint32_t index, bjvm_wasm_type kind);
bjvm_wasm_expression *bjvm_wasm_local_set(bjvm_wasm_module *module,
                                          uint32_t index,
                                          bjvm_wasm_expression *value);
bjvm_wasm_expression *bjvm_wasm_unop(bjvm_wasm_module *module,
                                     bjvm_wasm_unary_op_kind op,
                                     bjvm_wasm_expression *expr);
bjvm_wasm_expression *bjvm_wasm_binop(bjvm_wasm_module *module,
                                      bjvm_wasm_binary_op_kind op,
                                      bjvm_wasm_expression *left,
                                      bjvm_wasm_expression *right);
bjvm_wasm_expression *bjvm_wasm_select(bjvm_wasm_module *module,
                                       bjvm_wasm_expression *condition,
                                       bjvm_wasm_expression *true_expr,
                                       bjvm_wasm_expression *false_expr);
bjvm_wasm_expression *bjvm_wasm_block(bjvm_wasm_module *module,
                                      bjvm_wasm_expression **exprs,
                                      int expr_count, bjvm_wasm_type type);
bjvm_wasm_expression *bjvm_wasm_br(bjvm_wasm_module *module,
                                   bjvm_wasm_expression *condition,
                                   bjvm_wasm_expression *break_to);
bjvm_wasm_expression *bjvm_wasm_call(bjvm_wasm_module *module,
                                     bjvm_wasm_function *fn,
                                     bjvm_wasm_expression **args,
                                     int arg_count);
bjvm_wasm_expression *bjvm_wasm_call_indirect(bjvm_wasm_module *module,
                                              int table_index,
                                              bjvm_wasm_expression *index,
                                              bjvm_wasm_expression **args,
                                              int arg_count);
bjvm_wasm_expression *bjvm_wasm_load(bjvm_wasm_module *module,
                                     bjvm_wasm_load_op_kind op,
                                     bjvm_wasm_expression *addr, int align,
                                     int offset);
bjvm_wasm_expression *bjvm_wasm_store(bjvm_wasm_module *module,
                                      bjvm_wasm_store_op_kind op,
                                      bjvm_wasm_expression *addr,
                                      bjvm_wasm_expression *value, int align,
                                      int offset);
bjvm_wasm_expression *bjvm_wasm_if_else(bjvm_wasm_module *module,
                                        bjvm_wasm_expression *cond,
                                        bjvm_wasm_expression *true_expr,
                                        bjvm_wasm_expression *false_expr,
                                        bjvm_wasm_type type);
bjvm_wasm_expression *bjvm_wasm_return(bjvm_wasm_module *module,
                                       bjvm_wasm_expression *expr);
bjvm_wasm_load_op_kind bjvm_wasm_get_load_op(bjvm_wasm_type type);
bjvm_wasm_store_op_kind bjvm_wasm_get_store_op(bjvm_wasm_type type);

typedef enum {
  BJVM_WASM_INSTANTIATION_SUCCESS,
  // instantiateAsync was used, and js_promise is set
  BJVM_WASM_INSTANTIATION_PENDING,
  BJVM_WASM_INSTANTIATION_FAIL,
} bjvm_wasm_instantiation_status;

typedef struct {
  heap_string name;
  // All exported functions are stored as a void pointer
  void *export_;
} bjvm_wasm_instantiation_export;

typedef struct {
  bjvm_wasm_instantiation_status status;
  int js_promise;

  bjvm_wasm_instantiation_export **exports;
  int export_count;
  int export_cap;
} bjvm_wasm_instantiation_result;

void bjvm_free_wasm_instantiation_result(
    bjvm_wasm_instantiation_result *result);
bjvm_wasm_instantiation_result *
bjvm_wasm_instantiate_module(bjvm_wasm_module *module);

#ifdef __cplusplus
}
#endif

#endif // WASM_UTILS_H
