// Three-address code intermediate representation.

#ifndef IR_H
#define IR_H

#include <stdint.h>
#include "classfile.h"

typedef uint32_t bjvm_var_t;
typedef uint32_t bjvm_bb_t;

typedef enum {
  BJVM_IR_TYPE_INT,
  BJVM_IR_TYPE_LONG,
  BJVM_IR_TYPE_FLOAT,
  BJVM_IR_TYPE_DOUBLE
} bjvm_ir_type_kind;

typedef enum {
  // Unary operators
  bjvm_ir_op_iadd,
} bjvm_ir_op_kind;

typedef struct {
  bjvm_insn_code_kind kind;
  // target <- lhs * rhs
  bjvm_var_t target;
  bjvm_var_t lhs, rhs;
} bjvm_ir_assign;

typedef enum {
  BJVM_IR_STORE_KIND_I8,
  BJVM_IR_STORE_KIND_I16,
  BJVM_IR_STORE_KIND_I32,
  BJVM_IR_STORE_KIND_I64,
  BJVM_IR_STORE_KIND_F32,
  BJVM_IR_STORE_KIND_F64
} bjvm_ir_store_kind;

typedef enum {
  // Sign-extended
  BJVM_IR_LOAD_KIND_I8,
  BJVM_IR_LOAD_KIND_I16,
  BJVM_IR_LOAD_KIND_I32,
  BJVM_IR_LOAD_KIND_I64,

  BJVM_IR_LOAD_KIND_U16,
  BJVM_IR_LOAD_KIND_F32,
  BJVM_IR_LOAD_KIND_F64
} bjvm_ir_load_kind;

typedef struct {
  bjvm_ir_store_kind kind;
  // mem[value] <- addr
  bjvm_var_t addr;
  bjvm_var_t value;
} bjvm_ir_store;

typedef struct {
  bjvm_ir_load_kind kind;
  // target <- mem[addr]
  bjvm_var_t target;
  bjvm_var_t addr;
} bjvm_ir_load;

typedef struct {
  bjvm_var_t ret;
} bjvm_ir_return;

typedef struct {
  bjvm_var_t target;
} bjvm_ir_phi;

typedef struct {
  //bjvm_ir_insn_kind kind;
  union {

  };
} bjvm_ir_insn;

typedef struct {

} bjvm_ir;

typedef struct {
  bjvm_ir_insn *insns;
  int count;

  bjvm_ir_insn last;
} bjvm_ir_bb;

typedef struct {

} bjvm_ir_function;

#endif //IR_H
