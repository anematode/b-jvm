// Fast unoptimized JIT for WASM.

#include <stdint.h>
#include "classfile.h"
#include "dumb_jit.h"
#include "bjvm.h"
#include "arrays.h"
#include "cached_classdescs.h"

typedef struct {
  uint16_t pc;
  bool is_loop;
} dumb_jit_label;

typedef uint32_t wasm_local_t;
typedef struct {
  bjvm_attribute_code *code;

  bjvm_thread *thread;

  // Current stack of labels to break to -- used to calculate the correct break depth
  dumb_jit_label *labels;

  // Mapping of (index, Java type) pair to WASM local, or -1 if not yet assigned. By convention,
  // index = 0 is the bottom of the operand stack, and index = code->max_stack is the first local.
  wasm_local_t *jvm_to_wasm;

  // Current stack depth (signed i32 to avoid overflow math issues)
  int32_t sd;

  // Code bytevector that we are spewing bytes to
  bjvm_bytevector out;

  // Maximum number of object pointers which are live at a given time.
  int max_oops;
} dumb_jit_ctx;

// The current context
dumb_jit_ctx *ctx;

static wasm_local_t stack_value(bjvm_wasm_value_type type, int32_t count) {
  return ctx->sd - 1;
}

#define stack_int(offset) stack_value(BJVM_WASM_TYPE_KIND_INT32, offset)
#define stack_float(offset) stack_value(BJVM_WASM_TYPE_KIND_FLOAT32, offset)
#define stack_double(offset) stack_value(BJVM_WASM_TYPE_KIND_FLOAT64, offset)
#define stack_long(offset) stack_value(BJVM_WASM_TYPE_KIND_INT64, offset)

static void byte(uint8_t byte) {
  write_byte(&ctx->out, byte);
}

static void bytes(const uint8_t *bytes, int length) {
  write_slice(&ctx->out, bytes, length);
}

static void local_get(wasm_local_t local) {
  byte(0x20);
  bjvm_wasm_writeuint(&ctx->out, local);
}

static void local_set(wasm_local_t local) {
  byte(0x21);
  bjvm_wasm_writeuint(&ctx->out, local);
}

static void load(bjvm_wasm_load_op_kind load_op, int32_t offset) {
  byte(load_op);
  byte(0x00); // align
  bjvm_wasm_writeint(&ctx->out, offset);
}

static void store(bjvm_wasm_store_op_kind store_op, int32_t offset) {
  byte(store_op);
  byte(0x00); // align
  bjvm_wasm_writeint(&ctx->out, offset);
}

static void iconst(int32_t value) {
  byte(0x41);
  bjvm_wasm_writeint(&ctx->out, value);
}

static void lconst(int64_t value) {
  byte(0x42);
  bjvm_wasm_writeint(&ctx->out, value);
}

static void fconst(float value) {
  byte(0x43);
  uint8_t b[4];
  memcpy(b, &value, sizeof(value));
  bytes(b, sizeof(b));
}

static void dconst(double value) {
  byte(0x44);
  uint8_t b[8];
  memcpy(b, &value, sizeof(value));
  bytes(b, sizeof(b));
}

static void if_() {
  byte(0x04);
}

static void endif() {
  byte(0x0B);
}

static void deopt() {

}

static int find_handler_pc(dumb_jit_ctx *ctx, const bjvm_classdesc *throwable) {

}

static void dumb_raise_npe() {
  int jump_to = find_handler_pc(ctx, ctx->thread->vm->cached_classdescs->null_pointer_exception);
}

static void dumb_raise_array_oob() {

}

static void dumb_jit_raise_div_zero() {

}

static void dumb_lower_iadd(dumb_jit_ctx *ctx, bjvm_bytecode_insn *insn) {
  local_get(stack_int(-1));
  local_get(stack_int(-2));
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  local_set(stack_int(-2));
}

static void dumb_lower_arraylength(dumb_jit_ctx *ctx, bjvm_bytecode_insn *insn) {
  local_get(stack_int(-1));  // array
  byte(BJVM_WASM_OP_KIND_I32_EQZ);
  if_();  // deopt if NULL
  deopt();
  endif();
  local_get(stack_int(-1));
  load(BJVM_WASM_OP_KIND_I32_LOAD, kArrayLengthOffset);
  local_set(stack_int(-1));
}

static void dumb_lower_arrayload(dumb_jit_ctx *ctx, bjvm_bytecode_insn *insn) {
  typedef struct {
    int element_bytes;
    bjvm_wasm_value_type wasm_type;
    bjvm_wasm_load_op_kind load_op;
  } info;

  info infos[] = {
    [bjvm_insn_aaload] = {4, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_LOAD},
    [bjvm_insn_iaload] = {4, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_LOAD},
    [bjvm_insn_faload] = {4, BJVM_WASM_TYPE_KIND_FLOAT32, BJVM_WASM_OP_KIND_F32_LOAD},
    [bjvm_insn_laload] = {8, BJVM_WASM_TYPE_KIND_INT64, BJVM_WASM_OP_KIND_I64_LOAD},
    [bjvm_insn_daload] = {8, BJVM_WASM_TYPE_KIND_FLOAT64, BJVM_WASM_OP_KIND_F64_LOAD},
    [bjvm_insn_baload] = {1, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_LOAD8_U},
    [bjvm_insn_caload] = {2, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_LOAD16_U},
    [bjvm_insn_saload] = {2, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_LOAD16_S},
  };

  info I = infos[insn->kind];
  assert(I.element_bytes != 0 && "Not array load");

  local_get(stack_int(-2));  // array
  byte(BJVM_WASM_OP_KIND_I32_EQZ);
  if_();
  dumb_raise_npe();
  endif();
  local_get(stack_int(-2));
  load(BJVM_WASM_OP_KIND_I32_LOAD, kArrayLengthOffset);
  byte(BJVM_WASM_OP_KIND_I32_GT_U);
  if_();  // (unsigned)index >= array.length
  local_get(stack_int(-2));  // array
  local_get(stack_int(-1));  // index
  dumb_raise_array_oob();
  endif();
  local_get(stack_int(-2));  // array
  local_get(stack_int(-1));  // index
  if (I.element_bytes != 1) {
    iconst(I.element_bytes);
    byte(BJVM_WASM_OP_KIND_I32_MUL);
  }
  byte(BJVM_WASM_OP_KIND_I32_ADD);  // array + data * scale
  load(I.load_op, kArrayDataOffset);  // array + data * scale + data offs
  local_set(stack_value(I.wasm_type, -2));
}

static void dumb_lower_arraystore(dumb_jit_ctx *ctx, bjvm_bytecode_insn *insn) {
  typedef struct {
    int element_bytes;
    bjvm_wasm_value_type wasm_type;
    bjvm_wasm_store_op_kind store_op;
  } info;

  info infos[] = {
    [bjvm_insn_aastore] = {4, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_STORE},
    [bjvm_insn_iastore] = {4, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_STORE},
    [bjvm_insn_fastore] = {4, BJVM_WASM_TYPE_KIND_FLOAT32, BJVM_WASM_OP_KIND_F32_STORE},
    [bjvm_insn_lastore] = {8, BJVM_WASM_TYPE_KIND_INT64, BJVM_WASM_OP_KIND_I64_STORE},
    [bjvm_insn_dastore] = {8, BJVM_WASM_TYPE_KIND_FLOAT64, BJVM_WASM_OP_KIND_F64_STORE},
    [bjvm_insn_bastore] = {1, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_STORE8},
    [bjvm_insn_castore] = {2, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_STORE16},
    [bjvm_insn_sastore] = {2, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_OP_KIND_I32_STORE16},
  };

  info I = infos[insn->kind];
  assert(I.element_bytes != 0 && "Not array store");

  local_get(stack_int(-3));  // array
  byte(BJVM_WASM_OP_KIND_I32_EQZ);
  if_();
  dumb_raise_npe();
  endif();
  load(BJVM_WASM_OP_KIND_I32_LOAD, kArrayLengthOffset);
  byte(BJVM_WASM_OP_KIND_I32_GT_U);
  if_();
  local_get(stack_int(-3));  // array
  local_get(stack_int(-2));  // index
  dumb_raise_array_oob();
  endif();

  if (insn->kind == bjvm_insn_aastore) {
    // TODO use StackMapTable to omit the instanceof check when possible
    local_get(stack_int(-1));  // value
    byte(BJVM_WASM_OP_KIND_I32_EQZ);
    if_();

    else_();
    local_get(stack_int(-3));
    load(BJVM_WASM_OP_KIND_I32_ADD, offsetof(bjvm_obj_header, descriptor));
    // TODO
    endif();
  }

  local_get(stack_int(-3));  // array
  local_get(stack_int(-2));  // index
  if (I.element_bytes != 1) {
    iconst(I.element_bytes);
    byte(BJVM_WASM_OP_KIND_I32_MUL);
  }
  byte(BJVM_WASM_OP_KIND_I32_ADD);  // array + data * scale
  store(I.store_op, kArrayDataOffset);  // array + data * scale + data offs
  local_set(stack_value(I.wasm_type, -2));
}

static void dumb_lower_long_binop(bjvm_bytecode_insn *insn) {
  bjvm_wasm_binary_op_kind ops[] = {
    [bjvm_insn_ladd] = BJVM_WASM_OP_KIND_I64_ADD,
    [bjvm_insn_lsub] = BJVM_WASM_OP_KIND_I64_SUB,
    [bjvm_insn_lmul] = BJVM_WASM_OP_KIND_I64_MUL,
    [bjvm_insn_ldiv] = BJVM_WASM_OP_KIND_I64_DIV_S,
    [bjvm_insn_lrem] = BJVM_WASM_OP_KIND_I64_REM_S,
    [bjvm_insn_land] = BJVM_WASM_OP_KIND_I64_AND,
    [bjvm_insn_lor] = BJVM_WASM_OP_KIND_I64_OR,
    [bjvm_insn_lxor] = BJVM_WASM_OP_KIND_I64_XOR
  };

  bjvm_wasm_binary_op_kind op = ops[insn->kind];
  assert(op != 0 && "Not a long binop");

  if (insn->kind == BJVM_WASM_OP_KIND_I64_DIV_S || insn->kind == BJVM_WASM_OP_KIND_I32_DIV_S) {
    local_get(stack_long(-1)); // divisor
    byte(BJVM_WASM_OP_KIND_I64_EQZ);
    if_();
    dumb_jit_raise_div_zero();
    endif();
  }

  local_get(stack_long(-2));
  local_get(stack_long(-1));
  byte(op);
  local_set(stack_long(-2));
}

static void dumb_lower_int_binop(bjvm_bytecode_insn *insn) {
  bjvm_wasm_binary_op_kind ops[] = {
    [bjvm_insn_iadd] = BJVM_WASM_OP_KIND_I32_ADD,
    [bjvm_insn_isub] = BJVM_WASM_OP_KIND_I32_SUB,
    [bjvm_insn_imul] = BJVM_WASM_OP_KIND_I32_MUL,
    [bjvm_insn_idiv] = BJVM_WASM_OP_KIND_I32_DIV_S,
    [bjvm_insn_irem] = BJVM_WASM_OP_KIND_I32_REM_S,
    [bjvm_insn_iand] = BJVM_WASM_OP_KIND_I32_AND,
    [bjvm_insn_ior] = BJVM_WASM_OP_KIND_I32_OR,
    [bjvm_insn_ixor] = BJVM_WASM_OP_KIND_I32_XOR
  };

  bjvm_wasm_binary_op_kind op = ops[insn->kind];
  assert(op != 0 && "Not an int binop");

  local_get(stack_int(-2));
  local_get(stack_int(-1));
  byte(op);
  local_set(stack_int(-2));
}

static void dumb_lower_iconst(bjvm_bytecode_insn *insn) {
  int32_t narrowed = (int32_t)insn->integer_imm;
  assert((int64_t)narrowed == insn->integer_imm);
  iconst(narrowed);
  local_set(stack_int(0));
}

static void dumb_lower_lconst(bjvm_bytecode_insn *insn) {
  lconst(insn->integer_imm);
  local_set(stack_long(0));
}

static void dumb_lower_fconst(bjvm_bytecode_insn *insn) {
  fconst(insn->f_imm);
  local_set(stack_float(0));
}

static void dumb_lower_dconst(bjvm_bytecode_insn *insn) {
  dconst(insn->d_imm);
  local_set(stack_double(0));
}

// Push a "compiled frame" of the appropriate size, returning on StackOverflow.
static void dumb_lower_push_frame(dumb_jit_ctx *ctx) {

}