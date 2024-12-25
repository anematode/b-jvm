// Lightweight version of the parts of Binaryen that we need, written in C
// TODO

#ifndef WASM_UTILS_H
#define WASM_UTILS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "util.h"

typedef enum {
  BJVM_WASM_TYPE_KIND_VOID,
  BJVM_WASM_TYPE_KIND_INT32 = 0x7F,
  BJVM_WASM_TYPE_KIND_FLOAT32 = 0x7D,
  BJVM_WASM_TYPE_KIND_FLOAT64 = 0x7C,
  BJVM_WASM_TYPE_KIND_INT64 = 0x7E
} bjvm_basic_wasm_type;

typedef struct {
  // If val <= INT64, then it is a basic type; otherwise, it is a globally
  // interned pointer to a tuple type. (Same as done in Binaryen)
  uintptr_t val;
} bjvm_wasm_type;

typedef struct {

} bjvm_;

typedef enum {
  BJVM_WASM_EXPR_KIND_UNARY_OP,
} bjvm_wasm_expr_kind;

typedef enum {
  TODO
} bjvm_wasm_unary_op_kind;

typedef struct {
  bjvm_wasm_unary_op_kind op;
  bjvm_wasm_type type;
} bjvm_wasm_unary_expression;

typedef struct bjvm_wasm_expression bjvm_wasm_expression;

typedef struct {
  bjvm_wasm_expression *exprs;
  int expr_count;
  int expr_cap;
} bjvm_wasm_expression_list;

typedef struct {

} bjvm_wasm_block_expression;

typedef struct bjvm_wasm_expression {
  bjvm_wasm_expr_kind kind;
  // type of the expression itself, e.g. (i32.add (i32.const 1) (i32.const 2))
  // would have type i32
  bjvm_wasm_type expr_type;

  union {
    bjvm_wasm_unary_expression unary_op;
  };
} bjvm_wasm_expression;

typedef struct {

} bjvm_wasm_function;

typedef struct {

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

void bjvm_wasm_writeuint(bjvm_bytevector *ctx, uint64_t value);
void bjvm_wasm_writeint(bjvm_bytevector *ctx, int64_t value);

bjvm_wasm_module* bjvm_wasm_module_create();
// It is the caller's responsibility to free the returned bytevector.
bjvm_bytevector bjvm_wasm_module_serialize(bjvm_wasm_module *module);
void bjvm_wasm_module_destroy(bjvm_wasm_module* module);

bool bjvm_wasm_is_type_tuple(bjvm_wasm_type type);
bjvm_wasm_expression *bjvm_wasm_make_binop(
    bjvm_wasm_module *module, bjvm_wasm_expression *left, bjvm_wasm_expression *right);

#ifdef __cplusplus
}
#endif

#endif //WASM_UTILS_H
