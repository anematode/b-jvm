// Fast unoptimized JIT for WASM.

#include <stdint.h>
#include "classfile.h"
#include "dumb_jit.h"

#include "analysis.h"
#include "bjvm.h"
#include "arrays.h"
#include "cached_classdescs.h"
#include "wasm/wasm_adapter.h"

typedef struct {
  uint16_t pc;
  bool is_loop;
} dumb_jit_label;

typedef uint32_t wasm_local_t;
typedef struct {
  bjvm_cp_method *method;
  bjvm_attribute_code *code;
  bjvm_code_analysis *analy;

  bjvm_thread *thread;

  // Current stack of labels to break to -- used to calculate the correct break depth
  dumb_jit_label *labels;

  // Mapping of (index, Java type) pair to WASM local, or -1 if not yet assigned. By convention,
  // index = 0 is the bottom of the operand stack, and index = code->max_stack is the first local.
  wasm_local_t *jvm_to_wasm;

  // Mapping of custom, named locals like "frame" to WASM locals
  bjvm_string_hash_table named_locals;

  // Number of parameters in the function
  int param_count;

  // Array of local types beyond the parameters
  bjvm_wasm_value_type *wasm_local_types;

  // WASM type of this function (incl. void)
  bjvm_wasm_value_type return_type;

  // The next available local variable to use.
  uint32_t next_local;

  // Current stack depth (signed i32 to avoid overflow math issues)
  int32_t sd;
  // Current program counter
  int32_t pc;

  // Code bytevector that we are spewing bytes to
  bjvm_bytevector out;

  // Maximum number of object pointers which are live at a given time.
  int max_oops;

  // The module that we are building.
  bjvm_wasm_module *module;
} dumb_jit_ctx;

// The current context
dumb_jit_ctx *ctx;

enum { THREAD_PARAM, METHOD_PARAM };

// Get the WASM local for a stack value at a given offset from the current top of the operand stack
static wasm_local_t stack_value(bjvm_wasm_value_type type, int32_t offset) {
  assert(ctx->sd + offset < ctx->code->max_stack && "Invalid stack offset");
  wasm_local_t *existing = &ctx->jvm_to_wasm[ctx->sd + offset];
  if (*existing == (wasm_local_t)-1) {
    *existing = ctx->next_local++;
  }
  return *existing;
}

// Get the WASM local for a local value at the given index
static wasm_local_t local_value(bjvm_wasm_value_type type, uint32_t index) {
  assert(index < ctx->code->max_locals && "Invalid local offset");
  wasm_local_t *existing = &ctx->jvm_to_wasm[ctx->code->max_stack + index];
  if (*existing == (wasm_local_t)-1) {
    *existing = ctx->next_local++;
  }
  return *existing;
}

// Get or create a local with the given name and type. This is used for named locals like "frame"
static wasm_local_t get_or_create_local(bjvm_wasm_value_type type, const char *name) {
  wasm_local_t local = (wasm_local_t)bjvm_hash_table_lookup(&ctx->named_locals, name, -1);
  if (local == 0) {
    local = ctx->next_local++;
    (void)bjvm_hash_table_insert(&ctx->named_locals, name, -1, (void *)local);
  }
  return local;
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

static void local_tee(wasm_local_t local) {
  byte(0x22);
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

static void else_() {
  byte(0x05);
}

static void endif() {
  byte(0x0B);
}

static void return_() {
  byte(0x0F);
}

static void return_zero() {
  switch (ctx->return_type) {
  case BJVM_WASM_TYPE_KIND_INT32:
    iconst(0);
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT32:
    fconst(0.0f);
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT64:
    dconst(0.0);
    break;
  case BJVM_WASM_TYPE_KIND_INT64:
    lconst(0);
    break;
  default: // void
    break;
  }
  return_();
}

// Call the given function, with the given return type and argument types. Corresponds to a call_indirect instruction
// into the function table.
static void vm_call(void *function, bjvm_wasm_type returns, const char* params) {
  bjvm_wasm_type params_type = bjvm_wasm_string_to_tuple(ctx->module, params);
  uint32_t fn_type = bjvm_register_function_type(ctx->module, params_type, returns);
  iconst((int)(uintptr_t)function);
  byte(0x11);
  bjvm_wasm_writeuint(&ctx->out, fn_type);
  byte(0x00); // table index
}

// Push a ptr to the current thread.
static void thread() {
  local_get(THREAD_PARAM);
}

static wasm_local_t frame_local() {
  get_or_create_local(BJVM_WASM_TYPE_KIND_INT32, "frame");
}

static void set_program_counter() {
  local_get(frame_local());
  iconst(ctx->pc);
  store(BJVM_WASM_OP_KIND_I32_STORE16,
    offsetof(bjvm_stack_frame, compiled) + offsetof(bjvm_compiled_frame, program_counter));
}

static bjvm_stack_value process_deopt(bjvm_thread *thread, bjvm_stack_frame *frame) {
  frame->is_native = 0;
  bjvm_cp_method *method = frame->method;
  method->deopt_count++;
  method->jit_entry = method->interpreter_entry;
  memset(&frame->async_frame, 0, sizeof(frame->async_frame));
  future_t fut;
  bjvm_stack_value result = bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status == FUTURE_READY);
  return result;
}

static double deopt_sled_double(bjvm_thread* thread, bjvm_stack_frame *frame) {
  return process_deopt(thread, frame).d;
}

static int64_t deopt_sled_long(bjvm_thread *thread, bjvm_stack_frame *frame) {
  return process_deopt(thread, frame).l;
}

static int32_t deopt_sled_int(bjvm_thread *thread, bjvm_stack_frame *frame) {
  return process_deopt(thread, frame).i;
}

static void deopt_sled_void(bjvm_thread *thread, bjvm_stack_frame *frame) {
  process_deopt(thread, frame);
}

static void deopt() {
  // We need to set up a bjvm_plain_frame at the same location as our current frame, with all appropriate stack
  // variables and locals. We then need to call deopt_sled with the thread and frame, which will in turn call
  // bjvm_interpret.
}

static void dumb_raise_npe() {
  set_program_counter();
  thread();
  vm_call(bjvm_null_pointer_exception, bjvm_wasm_void(), "i");
  return_zero();
}

static void dumb_raise_array_oob() {
  set_program_counter();
  vm_call(bjvm_array_index_oob_exception, bjvm_wasm_void(), "iii");
  return_zero();
}

static void dumb_jit_raise_div_zero() {
  set_program_counter();
  vm_call(bjvm_arithmetic_exception, bjvm_wasm_void(), "i");
  return_zero();
}

static void dumb_lower_iadd(bjvm_bytecode_insn *insn) {
  local_get(stack_int(-1));
  local_get(stack_int(-2));
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  local_set(stack_int(-2));
}

static void dumb_lower_arraylength(bjvm_bytecode_insn *insn) {
  local_get(stack_int(-1));  // array
  byte(BJVM_WASM_OP_KIND_I32_EQZ);
  if_();  // raise NPE if null
  dumb_raise_npe();
  endif();
  local_get(stack_int(-1));
  load(BJVM_WASM_OP_KIND_I32_LOAD, kArrayLengthOffset);
  local_set(stack_int(-1));
}

static void dumb_lower_arrayload(bjvm_bytecode_insn *insn) {
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
  thread();
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

static void dumb_lower_arraystore(bjvm_bytecode_insn *insn) {
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

  if (insn->kind == bjvm_insn_ldiv || insn->kind == bjvm_insn_lrem) {
    local_get(stack_long(-1)); // divisor
    byte(BJVM_WASM_OP_KIND_I64_EQZ);
    if_();
    dumb_jit_raise_div_zero();
    endif();
  }

  if (insn->kind == bjvm_insn_ldiv) {  // LLONG_MIN / -1, undefined according to wasm spec
    local_get(stack_long(-2));
    lconst(LLONG_MIN);
    byte(BJVM_WASM_OP_KIND_I64_EQ);
    local_get(stack_long(-1));
    lconst(-1);
    byte(BJVM_WASM_OP_KIND_I64_EQ);
    byte(BJVM_WASM_OP_KIND_I32_AND);
    if_();
    lconst(LLONG_MIN);
    local_set(stack_long(-2));
    else_();
  }

  local_get(stack_long(-2));
  local_get(stack_long(-1));
  byte(op);
  local_set(stack_long(-2));

  if (insn->kind == bjvm_insn_ldiv) {
    endif();
  }
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

  if (insn->kind == bjvm_insn_idiv || insn->kind == bjvm_insn_irem) {
    local_get(stack_long(-1)); // divisor
    byte(BJVM_WASM_OP_KIND_I32_EQZ);
    if_();
    dumb_jit_raise_div_zero();
    endif();
  }

  if (insn->kind == bjvm_insn_idiv) {  // handle INT_MIN / -1 which is undefined according to wasm
    local_get(stack_long(-2));
    lconst(LLONG_MIN);
    byte(BJVM_WASM_OP_KIND_I32_EQ);
    local_get(stack_long(-1));
    lconst(-1);
    byte(BJVM_WASM_OP_KIND_I32_EQ);
    byte(BJVM_WASM_OP_KIND_I32_AND);
    if_();
    lconst(LLONG_MIN);
    local_set(stack_long(-2));
    else_();
  }

  local_get(stack_int(-2));
  local_get(stack_int(-1));
  byte(op);
  local_set(stack_int(-2));

  if (insn->kind == bjvm_insn_idiv) {
    endif();
  }
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

static void raise_stack_overflow(bjvm_thread *thread) {
  bjvm_raise_exception_object(thread, thread->stack_overflow_error);
}

static void push_frame_helper(bjvm_thread *thread, bjvm_stack_frame *frame) {
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;
}

// Push a "compiled frame" of the appropriate size, returning on stack overflow
static void dumb_lower_push_frame() {
  // The bjvm_stack_frame will begin at thread->frame_buffer + thread->frame_buffer_used
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frame_buffer));
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frame_buffer_used));
  wasm_local_t fb_used = get_or_create_local(BJVM_WASM_TYPE_KIND_INT32, "frame_buffer_used");
  local_tee(fb_used);
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  local_set(frame_local());

  // In case of a de-opt, we will need sizeof(bjvm_stack_frame) + max_stack * sizeof(bjvm_stack_value) bytes to
  // hold the frame
  int frame_size = sizeof(bjvm_stack_frame) + ctx->code->max_stack * sizeof(bjvm_stack_value);

  // Compute the new value of frame_buffer_used
  local_get(fb_used);
  iconst(frame_size);
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  local_tee(fb_used);

  // If frame_buffer_used > frame_buffer_capacity raise stack overflow and return
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frame_buffer_capacity));
  byte(BJVM_WASM_OP_KIND_I32_GT_U);
  if_();
  thread();
  vm_call(raise_stack_overflow, bjvm_wasm_void(), "i");
  return_zero();
  endif();

  // Store the new value of frame_buffer_used
  thread();
  local_get(fb_used);
  store(BJVM_WASM_OP_KIND_I32_STORE, offsetof(bjvm_thread, frame_buffer_used));

  // thread->frame_count >= thread->frames_capacity
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frames_count));
  wasm_local_t frames_count = get_or_create_local(BJVM_WASM_TYPE_KIND_INT32, "frames_count");
  local_tee(frames_count);
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frames_cap));
  byte(BJVM_WASM_OP_KIND_I32_GE_U);
  if_();
  thread();
  local_get(frame_local());
  vm_call(push_frame_helper, bjvm_wasm_void(), "ii");
  else_();

  local_get(frame_local());
  local_get(frames_count);
  iconst(sizeof(bjvm_stack_frame*));
  byte(BJVM_WASM_OP_KIND_I32_MUL);
  thread();
  load(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_thread, frames));
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  store(BJVM_WASM_OP_KIND_I32_STORE, 0);  // frames[frames_count] = frame

  thread();
  local_get(frames_count);
  iconst(1);
  byte(BJVM_WASM_OP_KIND_I32_ADD);
  store(BJVM_WASM_OP_KIND_I32_STORE, offsetof(bjvm_thread, frames_count));  // thread->frames_count++
  endif();

  // Now set up the important information about the frame
  bjvm_stack_frame fake_frame;
  fake_frame.is_native = 2;
  fake_frame.is_async_suspended = 0;
  fake_frame.num_locals = 0;
  fake_frame.method = ctx->method;

  // On WASM 32-bit, this constitutes the first 64 bits, so we can execute an I64 const store to set this up
  local_get(frame_local());
  int64_t data;
  memcpy(&data, &fake_frame, sizeof(data));
  lconst(data);
  store(BJVM_WASM_OP_KIND_I64_STORE, 0);

  // Set the compiled program counter and oop count
  local_get(frame_local());
  iconst(0);
  store(BJVM_WASM_OP_KIND_I32_LOAD, offsetof(bjvm_stack_frame, compiled));
}

void dumb_lower_aconst_null(bjvm_bytecode_insn * insn) {
  iconst(0);
  local_set(stack_int(0));
}

void dumb_lower_return(bjvm_bytecode_insn *insn) {
  switch (ctx->return_type) {
  case BJVM_WASM_TYPE_KIND_INT32:
    local_get(stack_int(-1));
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT32:
    local_get(stack_float(-1));
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT64:
    local_get(stack_double(-1));
    break;
  case BJVM_WASM_TYPE_KIND_INT64:
    local_get(stack_long(-1));
    break;
  default:
    break;
  }
  byte(0x0F);
}

void dumb_lower_athrow() {
  // We only JIT methods without catch handlers
  thread();
  local_get(stack_int(-1));
  store(BJVM_WASM_OP_KIND_I32_STORE, offsetof(bjvm_thread, current_exception));
  return_zero();
}

void dumb_lower_unop(bjvm_wasm_value_type in, bjvm_wasm_value_type out, bjvm_wasm_unary_op_kind op) {
  local_get(stack_value(in, -1));
  if (op > 0xFF) {
    byte(op >> 8);
  }
  byte(op & 0xFF);
  local_set(stack_value(out, -1));
}

void dumb_lower_binop(bjvm_wasm_value_type in1, bjvm_wasm_value_type in2, bjvm_wasm_value_type out, bjvm_wasm_binary_op_kind op) {
  local_get(stack_value(in1, -2));
  local_get(stack_value(in2, -1));
  byte(op);
  local_set(stack_value(out, -2));
}

static bjvm_type_kind inspect_jvm_value_impl(int32_t test) {
  assert(test >= 0 && test < ctx->code->max_stack + ctx->code->max_locals);
  if (bjvm_test_compressed_bitset(ctx->analy->insn_index_to_doubles[ctx->pc], test)) {
    return BJVM_TYPE_KIND_DOUBLE;
  }
  if (bjvm_test_compressed_bitset(ctx->analy->insn_index_to_longs[ctx->pc], test)) {
    return BJVM_TYPE_KIND_LONG;
  }
  if (bjvm_test_compressed_bitset(ctx->analy->insn_index_to_floats[ctx->pc], test)) {
    return BJVM_TYPE_KIND_FLOAT;
  }
  if (bjvm_test_compressed_bitset(ctx->analy->insn_index_to_ints[ctx->pc], test)) {
    return BJVM_TYPE_KIND_INT;
  }
  if (bjvm_test_compressed_bitset(ctx->analy->insn_index_to_references[ctx->pc], test)) {
    return BJVM_TYPE_KIND_REFERENCE;
  }
  return BJVM_WASM_TYPE_KIND_VOID;
}

// Given the offset from the top of the stack
static bjvm_type_kind inspect_stack_kind(int32_t offset) {
  return inspect_jvm_value_impl(ctx->sd + offset);
}

static bjvm_wasm_value_type inspect_local_kind(int32_t offset) {
  return inspect_jvm_value_impl(ctx->code->max_stack + offset);
}

void dumb_lower_dup_like(bjvm_bytecode_insn *insn) {

}

// Returns non-zero if it failed to lower the instruction (and so a conversion to interpreter should be issued)
static int dumb_lower_instruction(int pc) {
  bjvm_bytecode_insn *insn = &ctx->code->code[pc];
  ctx->sd = ctx->analy->insn_index_to_stack_depth[pc];

#define F64 BJVM_WASM_TYPE_KIND_FLOAT64
#define F32 BJVM_WASM_TYPE_KIND_FLOAT32
#define I32 BJVM_WASM_TYPE_KIND_INT32
#define I64 BJVM_WASM_TYPE_KIND_INT64

  switch (insn->kind) {
  case bjvm_insn_nop:
    break;
  case bjvm_insn_aaload:
  case bjvm_insn_baload:
  case bjvm_insn_caload:
  case bjvm_insn_daload:
    dumb_lower_arrayload(insn);
    break;
  case bjvm_insn_aastore:
  case bjvm_insn_bastore:
  case bjvm_insn_castore:
  case bjvm_insn_dastore:
    dumb_lower_arraystore(insn);
    break;
  case bjvm_insn_aconst_null:
    dumb_lower_aconst_null(insn);
    break;
  case bjvm_insn_areturn:
  case bjvm_insn_dreturn:
    dumb_lower_return(insn);
    break;
  case bjvm_insn_arraylength:
    dumb_lower_arraylength(insn);
    break;
  case bjvm_insn_athrow:
    dumb_lower_athrow();
    break;
  case bjvm_insn_d2f:
    dumb_lower_unop(F64, F32, BJVM_WASM_OP_KIND_F32_DEMOTE_F64);
    break;
  case bjvm_insn_d2i:
    dumb_lower_unop(F64, I32, BJVM_WASM_OP_KIND_I32_TRUNC_SAT_F64_S);
    break;
  case bjvm_insn_d2l:
    dumb_lower_unop(F64, I64, BJVM_WASM_OP_KIND_I64_TRUNC_SAT_F64_S);
    break;
  case bjvm_insn_dadd:
    dumb_lower_binop(F64, F64, F64, BJVM_WASM_OP_KIND_F64_ADD);
    break;
  case bjvm_insn_dcmpg:
  case bjvm_insn_dcmpl:
    return -1;
  case bjvm_insn_ddiv:
    dumb_lower_binop(F64, F64, F64, BJVM_WASM_OP_KIND_F64_DIV);
    break;
  case bjvm_insn_dmul:
    dumb_lower_binop(F64, F64, F64, BJVM_WASM_OP_KIND_F64_MUL);
    break;
  case bjvm_insn_dneg:
    dumb_lower_unop(F64, F64, BJVM_WASM_OP_KIND_F64_NEG);
    break;
  case bjvm_insn_drem:
    return -1;
  case bjvm_insn_dsub:
    dumb_lower_binop(F64, F64, F64, BJVM_WASM_OP_KIND_F64_SUB);
    break;
  case bjvm_insn_dup:
  case bjvm_insn_dup_x1:
  case bjvm_insn_dup_x2:
  case bjvm_insn_dup2:
  case bjvm_insn_dup2_x1:
  case bjvm_insn_dup2_x2:
    dumb_lower_dup_like(insn);
    break;
break;case bjvm_insn_f2d:
break;case bjvm_insn_f2i:
break;case bjvm_insn_f2l:
break;case bjvm_insn_fadd:
break;case bjvm_insn_faload:
break;case bjvm_insn_fastore:
break;case bjvm_insn_fcmpg:
break;case bjvm_insn_fcmpl:
break;case bjvm_insn_fdiv:
break;case bjvm_insn_fmul:
break;case bjvm_insn_fneg:
break;case bjvm_insn_frem:
break;case bjvm_insn_freturn:
break;case bjvm_insn_fsub:
break;case bjvm_insn_i2b:
break;case bjvm_insn_i2c:
break;case bjvm_insn_i2d:
break;case bjvm_insn_i2f:
break;case bjvm_insn_i2l:
break;case bjvm_insn_i2s:
break;case bjvm_insn_iadd:
break;case bjvm_insn_iaload:
break;case bjvm_insn_iand:
break;case bjvm_insn_iastore:
break;case bjvm_insn_idiv:
break;case bjvm_insn_imul:
break;case bjvm_insn_ineg:
break;case bjvm_insn_ior:
break;case bjvm_insn_irem:
break;case bjvm_insn_ireturn:
break;case bjvm_insn_ishl:
break;case bjvm_insn_ishr:
break;case bjvm_insn_isub:
break;case bjvm_insn_iushr:
break;case bjvm_insn_ixor:
break;case bjvm_insn_l2d:
break;case bjvm_insn_l2f:
break;case bjvm_insn_l2i:
break;case bjvm_insn_ladd:
break;case bjvm_insn_laload:
break;case bjvm_insn_land:
break;case bjvm_insn_lastore:
break;case bjvm_insn_lcmp:
break;case bjvm_insn_ldiv:
break;case bjvm_insn_lmul:
break;case bjvm_insn_lneg:
break;case bjvm_insn_lor:
break;case bjvm_insn_lrem:
break;case bjvm_insn_lreturn:
break;case bjvm_insn_lshl:
break;case bjvm_insn_lshr:
break;case bjvm_insn_lsub:
break;case bjvm_insn_lushr:
break;case bjvm_insn_lxor:
break;case bjvm_insn_monitorenter:
break;case bjvm_insn_monitorexit:
break;case bjvm_insn_pop:
break;case bjvm_insn_pop2:
break;case bjvm_insn_return:
break;case bjvm_insn_saload:
break;case bjvm_insn_sastore:
  case bjvm_insn_swap:
    return -1;
  case bjvm_insn_anewarray:
  case bjvm_insn_checkcast:
  case bjvm_insn_getfield:
  case bjvm_insn_getstatic:
  case bjvm_insn_instanceof:
  case bjvm_insn_invokedynamic:
  case bjvm_insn_new:
  case bjvm_insn_putfield:
  case bjvm_insn_putstatic:
  case bjvm_insn_invokevirtual:
  case bjvm_insn_invokespecial:
  case bjvm_insn_invokestatic:
    return -1;
  case bjvm_insn_ldc:
break;case bjvm_insn_ldc2_w:
break;case bjvm_insn_dload:
break;case bjvm_insn_fload:
break;case bjvm_insn_iload:
break;case bjvm_insn_lload:
break;case bjvm_insn_dstore:
break;case bjvm_insn_fstore:
break;case bjvm_insn_istore:
break;case bjvm_insn_lstore:
break;case bjvm_insn_aload:
break;case bjvm_insn_astore:
break;case bjvm_insn_goto:
break;case bjvm_insn_jsr:
break;case bjvm_insn_if_acmpeq:
break;case bjvm_insn_if_acmpne:
break;case bjvm_insn_if_icmpeq:
break;case bjvm_insn_if_icmpne:
break;case bjvm_insn_if_icmplt:
break;case bjvm_insn_if_icmpge:
break;case bjvm_insn_if_icmpgt:
break;case bjvm_insn_if_icmple:
break;case bjvm_insn_ifeq:
break;case bjvm_insn_ifne:
break;case bjvm_insn_iflt:
break;case bjvm_insn_ifge:
break;case bjvm_insn_ifgt:
break;case bjvm_insn_ifle:
break;case bjvm_insn_ifnonnull:
break;case bjvm_insn_ifnull:
break;case bjvm_insn_iconst:
break;case bjvm_insn_dconst:
break;case bjvm_insn_fconst:
break;case bjvm_insn_lconst:
break;case bjvm_insn_iinc:
break;case bjvm_insn_invokeinterface:
break;case bjvm_insn_multianewarray:
break;case bjvm_insn_newarray:
break;case bjvm_insn_tableswitch:
break;case bjvm_insn_lookupswitch:
break;case bjvm_insn_ret:
break;case bjvm_insn_anewarray_resolved:
break;case bjvm_insn_checkcast_resolved:
break;case bjvm_insn_instanceof_resolved:
break;case bjvm_insn_new_resolved:
break;case bjvm_insn_invokevtable_monomorphic:
break;case bjvm_insn_invokevtable_polymorphic:
break;case bjvm_insn_invokeitable_monomorphic:
break;case bjvm_insn_invokeitable_polymorphic:
break;case bjvm_insn_invokespecial_resolved:
break;case bjvm_insn_invokestatic_resolved:
break;case bjvm_insn_invokecallsite:
break;case bjvm_insn_invokesigpoly:
break;case bjvm_insn_getfield_B:
break;case bjvm_insn_getfield_C:
break;case bjvm_insn_getfield_S:
break;case bjvm_insn_getfield_I:
break;case bjvm_insn_getfield_J:
break;case bjvm_insn_getfield_F:
break;case bjvm_insn_getfield_D:
break;case bjvm_insn_getfield_Z:
break;case bjvm_insn_getfield_L:
break;case bjvm_insn_putfield_B:
break;case bjvm_insn_putfield_C:
break;case bjvm_insn_putfield_S:
break;case bjvm_insn_putfield_I:
break;case bjvm_insn_putfield_J:
break;case bjvm_insn_putfield_F:
break;case bjvm_insn_putfield_D:
break;case bjvm_insn_putfield_Z:
break;case bjvm_insn_putfield_L:
break;case bjvm_insn_getstatic_B:
break;case bjvm_insn_getstatic_C:
break;case bjvm_insn_getstatic_S:
break;case bjvm_insn_getstatic_I:
break;case bjvm_insn_getstatic_J:
break;case bjvm_insn_getstatic_F:
break;case bjvm_insn_getstatic_D:
break;case bjvm_insn_getstatic_Z:
break;case bjvm_insn_getstatic_L:
break;case bjvm_insn_putstatic_B:
break;case bjvm_insn_putstatic_C:
break;case bjvm_insn_putstatic_S:
break;case bjvm_insn_putstatic_I:
break;case bjvm_insn_putstatic_J:
break;case bjvm_insn_putstatic_F:
break;case bjvm_insn_putstatic_D:
break;case bjvm_insn_putstatic_Z:
break;case bjvm_insn_putstatic_L:
break;case bjvm_insn_dsqrt:
break;}

  return -1;
}

