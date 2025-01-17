// Java bytecode to WebAssembly translator

#include "wasm_jit.h"
#include "analysis.h"
#include "arrays.h"
#include "objects.h"
#include "wasm_utils.h"

#include <limits.h>

#define expression wasm_expression *

enum { THREAD_PARAM, METHOD_PARAM, RESULT_PARAM };

typedef struct {
  wasm_function *new_object;
  wasm_function *new_array;
  wasm_function *raise_npe;
  wasm_function *raise_oob;
  wasm_function *push_frame;
  wasm_function *invokestatic;
  wasm_function *invokenonstatic;

  uint32_t vm_local;
  uint32_t heap_local;
  uint32_t heap_used_local;
} runtime_helpers;

typedef struct {
  // The WASM module we will emit as bytes
  wasm_module *module;
  // The basic blocks we are building
  expression *wasm_blocks;

  // The method and (for convenie) its code and analysis
  const bjvm_cp_method *method;
  bjvm_attribute_code *code;
  bjvm_code_analysis *analysis;

  // Mapping between value_index * 4 + type_kind and the wasm index for
  // local.get and local.set. -1 means not yet mapped.
  //
  // The ordering for type kind is REF/INT, FLOAT, DOUBLE, LONG.
  int *val_to_local_map;
  int next_local;

  // Local corresponding to the pushed bjvm_stack_frame
  int frame_local;

  wasm_value_type *wvars;
  int wvars_count;
  int wvars_cap;

  runtime_helpers runtime;
} compile_ctx;

#define WASM_TYPES_COUNT 4

wasm_type jvm_type_to_wasm(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return wasm_int32();
  case BJVM_TYPE_KIND_FLOAT:
    return wasm_float32();
  case BJVM_TYPE_KIND_DOUBLE:
    return wasm_float64();
  case BJVM_TYPE_KIND_LONG:
    return wasm_int64();
  case BJVM_TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
}

static int local_kind(wasm_type ty) {
  int result = ty.val - WASM_TYPE_KIND_FLOAT64;
  assert((unsigned)result < 4);
  return result;
}

int sizeof_wasm_kind(wasm_type kind) {
  switch (kind.val) {
  case WASM_TYPE_KIND_INT32:
  case WASM_TYPE_KIND_FLOAT32:
    return 4;
  case WASM_TYPE_KIND_FLOAT64:
  case WASM_TYPE_KIND_INT64:
    return 8;
  default:
    UNREACHABLE();
  }
}

int add_local(compile_ctx *ctx, wasm_type val) {
  *VECTOR_PUSH(ctx->wvars, ctx->wvars_count, ctx->wvars_cap) = val.val;
  return ctx->next_local++;
}

int jvm_stack_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  int i = index * WASM_TYPES_COUNT + local_kind(jvm_type_to_wasm(kind));
  int *l = &ctx->val_to_local_map[i];
  if (*l == -1) {
    *l = add_local(ctx, jvm_type_to_wasm(kind));
  }
  return *l;
}

int jvm_local_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return jvm_stack_to_wasm_local(ctx, ctx->code->max_stack + index, kind);
}

expression get_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return wasm_local_get(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind),
                             jvm_type_to_wasm(kind));
}

expression set_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return wasm_local_set(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind), value);
}

expression get_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return wasm_local_get(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind),
                             jvm_type_to_wasm(kind));
}

expression set_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return wasm_local_set(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind), value);
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  return AllocateObject(thread, classdesc, classdesc->instance_bytes);
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t wasm_runtime_raise_npe(bjvm_thread *thread) {
  bjvm_null_pointer_exception(thread);
  return BJVM_INTERP_RESULT_EXC;
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t
wasm_runtime_raise_array_index_oob(bjvm_thread *thread, int index, int length) {
  bjvm_array_index_oob_exception(thread, index, length);
  return BJVM_INTERP_RESULT_EXC;
}

EMSCRIPTEN_KEEPALIVE
bjvm_obj_header *wasm_runtime_make_object_array(bjvm_thread *thread, int count,
                                                bjvm_classdesc *classdesc) {
  if (count < 0)
    return nullptr; // interrupt and raise NegativeArraySizeException
  return CreateObjectArray1D(thread, classdesc, count);
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t wasm_runtime_invokestatic(bjvm_thread *thread,
                                                    bjvm_plain_frame *frame,
                                                    bjvm_bytecode_insn *insn) {
  // printf("invokestatic called!\n");
  int sd = stack_depth(frame);
  return 0; // bjvm_invokestatic(thread, frame, insn, &sd);
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t
wasm_runtime_invokenonstatic(bjvm_thread *thread, bjvm_plain_frame *frame,
                             bjvm_bytecode_insn *insn) {
  // fprintf(stderr, "invokenonstatic called from method %.*s!\n",
  // frame->method->name.len, frame->method->name.chars); dump_frame(stderr,
  // frame);
  int sd = stack_depth(frame);
  return 0;
}

void add_runtime_imports(compile_ctx *ctx) {
  ctx->runtime.new_object = wasm_import_runtime_function(
      ctx->module, wasm_runtime_new_object, "ii", "i");
  ctx->runtime.new_array = wasm_import_runtime_function(
      ctx->module, wasm_runtime_make_object_array, "iii", "i");
  ctx->runtime.raise_npe = wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_npe, "i", "i");
  ctx->runtime.raise_oob = wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_array_index_oob, "iii", "i");
  ctx->runtime.invokestatic = wasm_import_runtime_function(
      ctx->module, wasm_runtime_invokestatic, "iii", "i");
  ctx->runtime.invokenonstatic = wasm_import_runtime_function(
      ctx->module, wasm_runtime_invokenonstatic, "iii", "i");

  ctx->runtime.heap_local = add_local(ctx, wasm_int32());
  ctx->runtime.vm_local = add_local(ctx, wasm_int32());
  ctx->runtime.heap_used_local = add_local(ctx, wasm_int32());
}

#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

expression wasm_get_array_length(compile_ctx *ctx, expression array) {
  return wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, array, 0,
                        kArrayLengthOffset);
}

static expression get_desc(compile_ctx *ctx, expression obj) {
  return wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, obj, 0,
                        offsetof(bjvm_obj_header, descriptor));
}

expression wasm_raise_npe(compile_ctx *ctx) {
  // (return (call $raise_npe (local.get THREAD_PARAM)))
  return wasm_return(
      ctx->module,
      wasm_call(ctx->module, ctx->runtime.raise_npe,
                     (expression[]){wasm_local_get(
                         ctx->module, THREAD_PARAM, wasm_int32())},
                     1));
}

expression wasm_raise_oob(compile_ctx *ctx, expression index,
                          expression length) {
  // (return (call $raise_oob (local.get THREAD_PARAM) <index> <length>))
  return wasm_return(
      ctx->module, wasm_call(ctx->module, ctx->runtime.raise_oob,
                                  (expression[]){wasm_local_get(
                                                     ctx->module, THREAD_PARAM,
                                                     wasm_int32()),
                                                 index, length},
                                  3));
}

bjvm_type_kind inspect_value_type(compile_ctx *ctx, int pc, int stack_i) {
  int i = 0;
  for (; i < 5; ++i)
    if (bjvm_test_compressed_bitset(ctx->analysis->insn_index_to_kinds[i][pc],
                                    stack_i))
      break;

  switch (i) {
  case 0:
  case 1:
    return BJVM_TYPE_KIND_INT;
  case 2:
    return BJVM_TYPE_KIND_FLOAT;
  case 3:
    return BJVM_TYPE_KIND_DOUBLE;
  case 4:
    return BJVM_TYPE_KIND_LONG;
  default:
    return BJVM_TYPE_KIND_VOID;
  }
}

wasm_load_op_kind get_tk_load_op(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return WASM_OP_KIND_I32_LOAD8_U;
  case BJVM_TYPE_KIND_CHAR:
    return WASM_OP_KIND_I32_LOAD16_U;
  case BJVM_TYPE_KIND_FLOAT:
    return WASM_OP_KIND_F32_LOAD;
  case BJVM_TYPE_KIND_DOUBLE:
    return WASM_OP_KIND_F64_LOAD;
  case BJVM_TYPE_KIND_BYTE:
    return WASM_OP_KIND_I32_LOAD8_S;
  case BJVM_TYPE_KIND_SHORT:
    return WASM_OP_KIND_I32_LOAD16_S;
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return WASM_OP_KIND_I32_LOAD;
  case BJVM_TYPE_KIND_LONG:
    return WASM_OP_KIND_I64_LOAD;
  default:
    UNREACHABLE();
  }
}

static wasm_load_op_kind get_load_op(const bjvm_field_descriptor *field) {
  bjvm_type_kind kind =
      field->dimensions ? BJVM_TYPE_KIND_REFERENCE : field->base_kind;
  return get_tk_load_op(kind);
}

wasm_store_op_kind get_tk_store_op(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    return WASM_OP_KIND_I32_STORE8;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    return WASM_OP_KIND_I32_STORE16;
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return WASM_OP_KIND_I32_STORE;
  case BJVM_TYPE_KIND_FLOAT:
    return WASM_OP_KIND_F32_STORE;
  case BJVM_TYPE_KIND_DOUBLE:
    return WASM_OP_KIND_F64_STORE;
  case BJVM_TYPE_KIND_LONG:
    return WASM_OP_KIND_I64_STORE;
  default:
    UNREACHABLE();
  }
}

static wasm_store_op_kind
get_store_op(const bjvm_field_descriptor *field) {
  bjvm_type_kind kind =
      field->dimensions ? BJVM_TYPE_KIND_REFERENCE : field->base_kind;
  return get_tk_store_op(kind);
}

expression wasm_move_value(compile_ctx *ctx, int pc, int from, int to,
                           bjvm_type_kind kind) {
  return wasm_local_set(
      ctx->module, jvm_stack_to_wasm_local(ctx, to, kind),
      wasm_local_get(ctx->module, jvm_stack_to_wasm_local(ctx, from, kind),
                          jvm_type_to_wasm(kind)));
}

expression wasm_lower_lcmp(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                           int sd) {
  bjvm_type_kind kind = BJVM_TYPE_KIND_LONG;

  expression zero = wasm_i32_const(ctx->module, 0);
  expression one = wasm_i32_const(ctx->module, 1);
  expression negative_one = wasm_i32_const(ctx->module, -1);

  expression right = get_stack_value(ctx, sd - 1, kind);
  expression left = get_stack_value(ctx, sd - 2, kind);

  wasm_binary_op_kind gt = WASM_OP_KIND_I64_GT_S,
                           lt = WASM_OP_KIND_I64_LT_S;

  expression cmp_greater = wasm_binop(ctx->module, gt, left, right);
  expression cmp_less = wasm_binop(ctx->module, lt, left, right);
  expression result = wasm_select(
      ctx->module, cmp_greater, one,
      wasm_select(ctx->module, cmp_less, negative_one, zero));
  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_INT, result);
}

expression wasm_lower_long_shift(compile_ctx *ctx,
                                 const bjvm_bytecode_insn *insn, int sd) {
  // Convert int to long
  expression right = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
  expression right_long =
      wasm_unop(ctx->module, WASM_OP_KIND_I64_EXTEND_U_I32, right);

  expression left = get_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG);
  wasm_binary_op_kind op =
      insn->kind == bjvm_insn_lshl    ? WASM_OP_KIND_I64_SHL
      : insn->kind == bjvm_insn_lushr ? WASM_OP_KIND_I64_SHR_U
                                      : WASM_OP_KIND_I64_SHR_S;
  expression result = wasm_binop(ctx->module, op, left, right_long);

  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG, result);
}

expression wasm_lower_local_load_store(compile_ctx *ctx,
                                       const bjvm_bytecode_insn *insn, int sd) {
  bjvm_type_kind kind;
  bool is_store = false;

  switch (insn->kind) {
  case bjvm_insn_istore:
  case bjvm_insn_astore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_iload:
  case bjvm_insn_aload:
    kind = BJVM_TYPE_KIND_INT;
    break;
  case bjvm_insn_fstore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_fload:
    kind = BJVM_TYPE_KIND_FLOAT;
    break;
  case bjvm_insn_dstore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_dload:
    kind = BJVM_TYPE_KIND_DOUBLE;
    break;
  case bjvm_insn_lstore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_lload:
    kind = BJVM_TYPE_KIND_LONG;
    break;
  default:
    UNREACHABLE();
  }
  int index = insn->index;
  if (is_store) {
    expression value = get_stack_value(ctx, sd - 1, kind);
    return set_local_value(ctx, index, kind, value);
  }
  expression value = get_local_value(ctx, index, kind);
  return set_stack_value(ctx, sd, kind, value);
}

expression wasm_lower_iinc(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                           int sd) {
  expression local = get_local_value(ctx, insn->iinc.index, BJVM_TYPE_KIND_INT);
  expression increment = wasm_i32_const(ctx->module, insn->iinc.const_);
  expression result =
      wasm_binop(ctx->module, WASM_OP_KIND_I32_ADD, local, increment);
  expression iinc =
      set_local_value(ctx, insn->iinc.index, BJVM_TYPE_KIND_INT, result);
  return iinc;
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_pop_frame(bjvm_thread *thread) {
  if (thread->frames_count == 0)
    return nullptr;
  bjvm_stack_frame *frame = thread->frames[thread->frames_count - 1];
  thread->frames_count--;
  return frame;
}

expression wasm_lower_return(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                             int pc, int sd) {
  // Write the value into memory at "result"
  bjvm_type_kind kind = inspect_value_type(ctx, pc, sd - 1);
  expression value = get_stack_value(ctx, sd - 1, kind);
  expression store = wasm_store(
      ctx->module, get_tk_store_op(kind),
      wasm_local_get(ctx->module, RESULT_PARAM, wasm_int32()), value,
      0, 0);

  return store;
}

wasm_expression *thread(compile_ctx *ctx) {
  return wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32());
}

typedef struct {
  int start, end;
} loop_range_t;

typedef struct {
  // these future blocks requested a wasm block to begin here, so that
  // forward edges can be implemented with a br or br_if
  int *requested;
  int count;
  int cap;
} bb_creations_t;

typedef struct {
  // new order -> block index
  int *topo_to_block;
  // block index -> new order
  int *block_to_topo;
  // Mapping from topological # to the blocks (also in topo #) which requested
  // a block begin here.
  bb_creations_t *creations;
  // Mapping from topological block index to a loop wasm_expression such
  // that backward edges should continue to this loop block.
  wasm_expression **loop_headers;
  // Mapping from topological block index to a block wasm_expression such
  // that forward edges should break out of this block.
  wasm_expression **block_ends;
  // Now implementation stuffs
  int current_block_i;
  int topo_i;
  int loop_depth;
  // For each block in topological indexing, the number of loops that contain
  // it.
  int *loop_depths;
  int *visited, *incoming_count;
  int blockc;
} topo_ctx;

static wasm_expression *frame_ptr(compile_ctx *ctx) {
  return wasm_local_get(ctx->module, ctx->frame_local, wasm_int32());
}

int frame_size(const bjvm_attribute_code * code) {
  return offsetof(bjvm_plain_frame, values) +
    (code->max_stack + code->max_locals) * sizeof(bjvm_stack_value);
}

EMSCRIPTEN_KEEPALIVE
bjvm_compiled_frame *wasm_runtime_push_compiled_frame(bjvm_thread *thread, bjvm_cp_method *method, int references_count, int deopt_size) {
  if (deopt_size + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  printf("Pushed compiled frame!!\n");

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  frame->compiled.is_native = 2;
  frame->compiled.references_count = references_count;
  frame->compiled.program_counter = 0;
  frame->compiled.method = method;

  memset(frame->compiled.references, 0, references_count * sizeof(bjvm_stack_value));
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;

  return &frame->compiled;
}

int frame_references_count(compile_ctx *ctx) {
  int max = 0;
  for (int pc = 0; pc < ctx->code->insn_count; ++pc) {
    int count = bjvm_count_compressed_bitset(*ctx->analysis->insn_index_to_references);
    if (count > max)
      max = count;
  }
  return max;
}

// Construct a compiled frame, with sufficient padding to hold de-optimized
// frames.
static wasm_expression *frame_setup(compile_ctx *ctx) {
  int deopt_size = frame_size(ctx->code);
  // References count is the maximum # of references live at any given point
  int references_count = frame_references_count(ctx);

  wasm_function *push_compiled_fn = wasm_import_runtime_function(
    ctx->module, wasm_runtime_push_compiled_frame, "iiii", "i");
  wasm_expression *make_frame = wasm_call(
    ctx->module, push_compiled_fn,
    (expression[]){
      wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32()),
      wasm_i32_const(ctx->module, (int)ctx->method),
      wasm_i32_const(ctx->module, references_count),
      wasm_i32_const(ctx->module, deopt_size)
    },
    4);

  assert(!ctx->frame_local && "Frame local already assigned");
  ctx->frame_local = add_local(ctx, wasm_int32());

  make_frame = wasm_local_set(ctx->module, ctx->frame_local, make_frame);
  expression *stmts = nullptr;
  arrput(stmts, make_frame);

  expression nullcheck = wasm_if_else(
    ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, frame_ptr(ctx)),
    wasm_return(ctx->module, wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_EXC)),
    nullptr,
    wasm_int32());
  arrput(stmts, nullcheck);

  expression block = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);

  return block;
}

// Pop this method's frame from the stack and return the given interpreter code
static wasm_expression *frame_end(compile_ctx *ctx, bjvm_interpreter_result_t code) {
  wasm_function *pop_fn = wasm_import_runtime_function(
    ctx->module, wasm_runtime_pop_frame, "i", "i");
  expression call = wasm_call(
    (expression[]){
      wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32())
    },
    1);
  expression return_the_code = wasm_return(ctx->module, wasm_i32_const(ctx->module, code));
  expression *stmts = nullptr;
  arrput(stmts, call);
  arrput(stmts, return_the_code);
  expression block = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);
  return block;
}

// Construct a de-optimization code path, replacing the frame on the stack with
// an interpreter frame. (Once we implement inlining we may have to start
// generating more than just one frame.)
//
// Steps:
//  1. Write the frame header, which is 16 bytes of various info.
//  2. Write all live stack and local variables.
//  3. Tail call bjvm_interpret with the new frame.
static wasm_expression *deopt(compile_ctx *ctx, int pc, int frame_state) {
  bjvm_plain_frame dummy = {};
  dummy.is_native = 0;
  dummy.max_stack = ctx->code->max_stack;
  dummy.program_counter = pc;
  dummy.values_count = ctx->code->max_stack + ctx->code->max_locals;
  dummy.method = ctx->method;
  dummy.state = frame_state;

#ifdef EMSCRIPTEN
  static_assert(offsetof(bjvm_plain_frame, result_of_next) == 16);
#endif

  uint64_t chunks[2];
  memcpy(&chunks, &dummy, sizeof(chunks));

  wasm_expression **stmts = nullptr;
  for (int i = 0; i < 2; ++i) {
    arrput(stmts, wasm_store(
                      ctx->module, WASM_OP_KIND_I64_STORE, frame_ptr(ctx),
                      wasm_i64_const(ctx->module, chunks[i]), 0, i * sizeof(uint64_t)));
  }

  // Now spill all live values from the JVM locals/stack
  for (int stack_i = 0; stack_i < dummy.values_count; ++stack_i) {
    bjvm_type_kind kind = inspect_value_type(ctx, pc, stack_i);
    if (kind != BJVM_TYPE_KIND_VOID) {
      wasm_store_op_kind store_op = get_tk_store_op(kind);
      int wasm_local = jvm_stack_to_wasm_local(ctx, stack_i, kind);
      expression value = wasm_local_get(ctx->module, wasm_local,
                                             jvm_type_to_wasm(kind));
      int offset = offsetof(bjvm_plain_frame, values) +
        stack_i * sizeof(bjvm_stack_value);
      arrput(stmts, wasm_store(ctx->module, store_op, frame_ptr(ctx),
                                    value, 0, offset));
    }
  }

  // Now tail call bjvm_interpret
  wasm_function *interpret_fn = wasm_import_runtime_function(
  ctx->module, bjvm_interpret, "iii", "i");
  expression call = wasm_call(
  ctx->module, interpret_fn,
  (expression[]){
    wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32()),
    wasm_local_get(ctx->module, ctx->frame_local, wasm_int32()),
    wasm_local_get(ctx->module, RESULT_PARAM, wasm_int32())
  },
  3);
  arrput(stmts, call);
  call->call.tail_call = true;

  // Pack things into a wasm block
  wasm_expression *block = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);
  return block;
}

enum spill_mode {
  SPILL,
  LOAD
};

// Spill all object pointers to consecutive slots in the frame stack, so that
// GC can enjoy itself. Returns nullptr if there are no object pointers to
// spill.
static expression spill_or_load_object_pointers(compile_ctx *ctx, int pc, enum spill_mode mode) {
  int stack_slot_i = 0;
  expression *stmts = nullptr;
  for (int i = 0; i < ctx->code->total_slots; ++i) {
    bool is_obj = bjvm_test_compressed_bitset(ctx->analysis->insn_index_to_references[pc], i);
    if (!is_obj)
      continue;
    int offset = offsetof(bjvm_compiled_frame, references) + stack_slot_i * sizeof(bjvm_stack_value);
    expression op;
    if (mode == SPILL) {
      op = wasm_store(ctx->module, WASM_OP_KIND_I32_STORE,
               frame_ptr(ctx),
               get_stack_value(ctx, i, BJVM_TYPE_KIND_REFERENCE),
               0, offset);
    } else {
      op = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, frame_ptr(ctx), 0, offset);
      op = set_stack_value(ctx, i, BJVM_TYPE_KIND_REFERENCE, op);
    }
    arrput(stmts, op);
    stack_slot_i++;
  }
  if (arrlen(stmts) == 0) {
    return nullptr;
  }
  expression blk = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);
  return blk;
}

EMSCRIPTEN_KEEPALIVE
bool wasm_runtime_instanceof(bjvm_classdesc *obj, bjvm_classdesc *cls) {
  return bjvm_instanceof(obj, cls);
}

// Lower aastore and friends.
static expression lower_array_store(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int insn_i) {
  wasm_store_op_kind store_op;
  int elem_size;
  wasm_module *M = ctx->module;

  switch (insn->kind) {
  case bjvm_insn_aastore:
  case bjvm_insn_iastore:
    store_op = WASM_OP_KIND_I32_STORE;
    elem_size = 4;
    break;
  case bjvm_insn_bastore:
    store_op = WASM_OP_KIND_I32_STORE8;
    elem_size = 1;
    break;
  case bjvm_insn_castore:
  case bjvm_insn_sastore:
    store_op = WASM_OP_KIND_I32_STORE16;
    elem_size = 2;
    break;
  case bjvm_insn_fastore:
    store_op = WASM_OP_KIND_F32_STORE;
    elem_size = 4;
    break;
  case bjvm_insn_dastore:
    store_op = WASM_OP_KIND_F64_STORE;
    elem_size = 8;
    break;
  case bjvm_insn_lastore:
    store_op = WASM_OP_KIND_I64_STORE;
    elem_size = 8;
    break;
  default:
    UNREACHABLE();
  }

  // (block        "outer"
  //    (block     "deopt"
  //       (null check)
  //       (instanceof check)   -- aastore only
  //       (bounds check)
  //       execute store
  //       break to outer
  //    )
  //    (... deopt impl ...)
  // )
  expression outer_blk = wasm_block(M, nullptr, 0, wasm_void(), false);
  expression deopt_blk = wasm_block(M, nullptr, 0, wasm_void(), false);

#define DEOPT_IF(e) wasm_br(M, e, deopt_blk)

  expression array = get_local_value(ctx, sd - 3, BJVM_TYPE_KIND_REFERENCE);
  expression index = get_local_value(ctx, sd - 2, BJVM_TYPE_KIND_INT);
  expression value = get_local_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);

  expression *stmts = nullptr;
  arrput(stmts, DEOPT_IF(wasm_unop(M, WASM_OP_KIND_I32_EQZ, array)));

  if (insn->kind == bjvm_insn_aaload) {
    // TODO optimize out some of these once we have better type info. Also think
    // about faster instanceof checks
    expression target = get_desc(ctx, array);
    expression src = get_desc(ctx, value);
    expression instanceof = wasm_call(
            M, wasm_import_runtime_function(M, wasm_runtime_instanceof, "ii", "i"),
            (expression[]){target, src}, 2);

    arrput(stmts, DEOPT_IF(wasm_unop(M, WASM_OP_KIND_I32_EQZ, instanceof)));
  }

  expression arraylen = wasm_get_array_length(ctx, array);
  arrput(stmts, DEOPT_IF(wasm_binop(ctx->module, WASM_OP_KIND_I32_GE_U, index, arraylen)));

  expression addr_minus_data_base = wasm_binop(
        M, WASM_OP_KIND_I32_ADD, array,
        wasm_binop(M, WASM_OP_KIND_I32_MUL, index, wasm_i32_const(M, elem_size)));
  expression execute_store = wasm_store(M, store_op, addr_minus_data_base, value, 0, kArrayDataOffset);
  arrput(stmts, execute_store);
  arrput(stmts, wasm_br(M, nullptr, outer_blk));

  wasm_update_block(M, deopt_blk, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);

  expression do_deopt = deopt(ctx, insn_i, INVOKE_STATE_ENTRY);
  wasm_update_block(M, outer_blk, (expression[]) { deopt_blk, do_deopt }, 2, wasm_void(), false);

  return outer_blk;
}

static expression lower_array_load(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  wasm_load_op_kind load_op;
  int elem_size;
  wasm_module *M = ctx->module;

  switch (insn->kind) {
  case bjvm_insn_aaload:
  case bjvm_insn_iaload:
    load_op = WASM_OP_KIND_I32_LOAD;
    elem_size = 4;
    break;
  case bjvm_insn_baload:
    load_op = WASM_OP_KIND_I32_LOAD8_S;
    elem_size = 1;
    break;
  case bjvm_insn_caload:
    load_op = WASM_OP_KIND_I32_LOAD16_U;
    elem_size = 2;
    break;
  case bjvm_insn_saload:
    load_op = WASM_OP_KIND_I32_LOAD16_S;
    elem_size = 2;
    break;
  case bjvm_insn_faload:
    load_op = WASM_OP_KIND_F32_LOAD;
    elem_size = 4;
    break;
  case bjvm_insn_daload:
    load_op = WASM_OP_KIND_F64_LOAD;
    elem_size = 8;
    break;
  case bjvm_insn_laload:
    load_op = WASM_OP_KIND_I64_LOAD;
    elem_size = 8;
    break;
  default:
    UNREACHABLE();
  }

  // (block       "outer"
  //   (block     "deopt"
  //      (null check)
  //      (bounds check)
  //      execute load
  //      (break outer)
  //   )
  //   ( ... deopt ... )
  // )

  expression outer_blk = wasm_block(M, nullptr, 0, wasm_void(), false);
  expression deopt_blk = wasm_block(M, nullptr, 0, wasm_void(), false);

  expression array = get_local_value(ctx, sd - 2, BJVM_TYPE_KIND_REFERENCE);
  expression index = get_local_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);

  expression null_chk = DEOPT_IF(wasm_unop(M, WASM_OP_KIND_I32_EQZ, array));
  expression arraylen = wasm_get_array_length(ctx, array);

  expression bounds_chk = DEOPT_IF(wasm_binop(M, WASM_OP_KIND_I32_GE_U, index, arraylen));
  expression addr = wasm_binop(
      M, WASM_OP_KIND_I32_ADD, array,
      wasm_binop(M, WASM_OP_KIND_I32_MUL, index, wasm_i32_const(M, elem_size)));
  expression execute_load = wasm_load(M, load_op, addr, 0, kArrayDataOffset);

  expression *stmts = nullptr;
  arrput(stmts, null_chk);
  arrput(stmts, bounds_chk);
  arrput(stmts, execute_load);
  arrput(stmts, wasm_br(M, nullptr, outer_blk));

  wasm_update_block(M, deopt_blk, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);

  expression do_deopt = deopt(ctx, pc, INVOKE_STATE_ENTRY);
  wasm_update_block(M, outer_blk, (expression[]) { deopt_blk, do_deopt }, 2, wasm_void(), false);

  return outer_blk;
}

// Lower getstatic_* and putstatic_*.
static expression lower_getstatic_putstatic_resolved(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd) {
  void *field_addr = insn->ic;
  struct info {
    int op;
    bool is_store;
    bjvm_type_kind tk;
  };
  struct info table[] = {
    [bjvm_insn_getstatic_B] = { WASM_OP_KIND_I32_LOAD8_S, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getstatic_C] = { WASM_OP_KIND_I32_LOAD16_U, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getstatic_S] = { WASM_OP_KIND_I32_LOAD16_S, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getstatic_I] = { WASM_OP_KIND_I32_LOAD, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getstatic_L] = { WASM_OP_KIND_I32_LOAD, false, BJVM_TYPE_KIND_REFERENCE },
    [bjvm_insn_getstatic_F] = { WASM_OP_KIND_F32_LOAD, false, BJVM_TYPE_KIND_FLOAT },
    [bjvm_insn_getstatic_D] = { WASM_OP_KIND_F64_LOAD, false, BJVM_TYPE_KIND_DOUBLE },
    [bjvm_insn_getstatic_J] = { WASM_OP_KIND_I64_LOAD, false, BJVM_TYPE_KIND_LONG },
    [bjvm_insn_putstatic_B] = { WASM_OP_KIND_I32_STORE8, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putstatic_C] = { WASM_OP_KIND_I32_STORE16, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putstatic_S] = { WASM_OP_KIND_I32_STORE16, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putstatic_I] = { WASM_OP_KIND_I32_STORE, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putstatic_L] = { WASM_OP_KIND_I32_STORE, true, BJVM_TYPE_KIND_REFERENCE },
    [bjvm_insn_putstatic_F] = { WASM_OP_KIND_F32_STORE, true, BJVM_TYPE_KIND_FLOAT },
    [bjvm_insn_putstatic_D] = { WASM_OP_KIND_F64_STORE, true, BJVM_TYPE_KIND_DOUBLE },
    [bjvm_insn_putstatic_J] = { WASM_OP_KIND_I64_STORE, true, BJVM_TYPE_KIND_LONG },
  };

  struct info I = table[insn->kind];
  printf("%d\n", I.tk);
  if (I.is_store) {
    return wasm_store(ctx->module, I.op, wasm_i32_const(ctx->module, (int)field_addr),
                          get_stack_value(ctx, sd - 1, I.tk), 0, 0);
  }
  expression load = wasm_load(ctx->module, I.op, wasm_i32_const(ctx->module, (int)field_addr), 0, 0);
  return set_stack_value(ctx, sd, I.tk, load);
}

// Lower getfield_resolved_* and putfield_resolved_*
static expression lower_getfield_putfield_resolved(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  int byte_offs = (int)insn->ic;
  struct info {
    int op;
    bool is_store;
    bjvm_type_kind tk;
  };

  struct info table[] = {
    [bjvm_insn_getfield_B] = { WASM_OP_KIND_I32_LOAD8_S, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getfield_C] = { WASM_OP_KIND_I32_LOAD16_U, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getfield_S] = { WASM_OP_KIND_I32_LOAD16_S, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getfield_I] = { WASM_OP_KIND_I32_LOAD, false, BJVM_TYPE_KIND_INT },
    [bjvm_insn_getfield_L] = { WASM_OP_KIND_I32_LOAD, false, BJVM_TYPE_KIND_REFERENCE },
    [bjvm_insn_getfield_F] = { WASM_OP_KIND_F32_LOAD, false, BJVM_TYPE_KIND_FLOAT },
    [bjvm_insn_getfield_D] = { WASM_OP_KIND_F64_LOAD, false, BJVM_TYPE_KIND_DOUBLE },
    [bjvm_insn_getfield_J] = { WASM_OP_KIND_I64_LOAD, false, BJVM_TYPE_KIND_LONG },
    [bjvm_insn_putfield_B] = { WASM_OP_KIND_I32_STORE8, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putfield_C] = { WASM_OP_KIND_I32_STORE16, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putfield_S] = { WASM_OP_KIND_I32_STORE16, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putfield_I] = { WASM_OP_KIND_I32_STORE, true, BJVM_TYPE_KIND_INT },
    [bjvm_insn_putfield_L] = { WASM_OP_KIND_I32_STORE, true, BJVM_TYPE_KIND_REFERENCE },
    [bjvm_insn_putfield_F] = { WASM_OP_KIND_F32_STORE, true, BJVM_TYPE_KIND_FLOAT },
    [bjvm_insn_putfield_D] = { WASM_OP_KIND_F64_STORE, true, BJVM_TYPE_KIND_DOUBLE },
    [bjvm_insn_putfield_J] = { WASM_OP_KIND_I64_STORE, true, BJVM_TYPE_KIND_LONG },
  };

  // (if (null check) (deopt) (impl))

  struct info I = table[insn->kind];

  expression doit;
  if (I.is_store) {
    doit = wasm_store(ctx->module, I.op, get_stack_value(ctx, sd - 1, I.tk),
                          get_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_REFERENCE), 0, byte_offs);
  } else {
    doit = wasm_load(ctx->module, I.op, get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE), 0, byte_offs);
    doit = set_stack_value(ctx, sd - 1, I.tk, doit);
  }

  expression checked = wasm_if_else(
      ctx->module,
      wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE)),
      deopt(ctx, pc, INVOKE_STATE_ENTRY),
      doit,
      wasm_void());
  return checked;
}

static expression lower_instanceof_resolved(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  // Call into C for now
  expression ref = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
  expression call = wasm_call(
    ctx->module,
    wasm_import_runtime_function(ctx->module, wasm_runtime_instanceof, "ii", "i"),
    (expression[]){
      insn->ic,
      ref,
    },
    2);
  // Unlike checkcast, instanceof gives false on null
  call = wasm_if_else(
    ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, ref),
    call,
    wasm_i32_const(ctx->module, 0),
    wasm_int32());
  call = set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, call);
  return call;
}

static expression lower_checkcast_resolved(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  expression ref = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
  expression call = wasm_call(
    ctx->module,
    wasm_import_runtime_function(ctx->module, wasm_runtime_instanceof, "ii", "i"),
    (expression[]){
      insn->ic,
      ref,
    },
    2);
  call = wasm_if_else(
    ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, ref),
    deopt(ctx, pc, INVOKE_STATE_ENTRY),
    nullptr,
    wasm_void());
  return call;
}

expression wasm_lower_ldc(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int insn_i) {
  switch (insn->cp->kind) {
  case BJVM_CP_KIND_INTEGER:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_INT,
        wasm_i32_const(ctx->module, (int)insn->cp->integral.value));
  case BJVM_CP_KIND_FLOAT:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_FLOAT,
        wasm_f32_const(ctx->module, (float)insn->cp->floating.value));
  case BJVM_CP_KIND_DOUBLE:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_DOUBLE,
        wasm_f64_const(ctx->module, insn->cp->floating.value));
  case BJVM_CP_KIND_LONG:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_LONG,
        wasm_i64_const(ctx->module, insn->cp->integral.value));
  case BJVM_CP_KIND_STRING:
    if (!insn->ic) {  // String not yet interned
      return deopt(ctx, insn_i, INVOKE_STATE_ENTRY);
    }
    return set_stack_value(
      ctx, sd, BJVM_TYPE_KIND_REFERENCE,
      wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, wasm_i32_const(ctx->module, (int)insn->ic), 0, 0));
  default:
    return nullptr;
  }
}

// Given the function pointer and the method descriptor that it corresponds to,
// perform an indirect
expression wasm_make_indirect_call(compile_ctx *ctx, expression fn_ptr, const bjvm_method_descriptor *m, int sd, bool is_static) {
  int argc = m->args_count + !is_static;
  bjvm_type_kind arg_types[argc];
  int i = 1;
  if (!is_static)
    arg_types[i++] = BJVM_TYPE_KIND_REFERENCE;
  for (int j = 0; j < m->args_count; ++j)
    arg_types[i++] = field_to_kind(m->args + j);
  assert(i == argc);
  // sd - 1 through sd - n are the arguments. The function call looks like
  // fn_ptr(thread, method, result, ... args) -> interpreter result
  expression *args = nullptr;
  arrput(args, wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32()));
  arrput(args, wasm_local_get(ctx->module, METHOD_PARAM, wasm_int32()));
  arrput(args, wasm_local_get(ctx->module, RESULT_PARAM, wasm_int32()));
  for (int j = 0; j < argc; ++j) {
    arrput(args, get_stack_value(ctx, sd - argc + j, arg_types[j]));
  }
  wasm_value_type wasm_types[argc + 3];
  wasm_types[0] = wasm_types[1] = wasm_types[2] = wasm_int32().val;
  for (int j = 0; j < argc; ++j) {
    wasm_types[j + 3] = jvm_type_to_wasm(arg_types[j]).val;
  }
  // Now generate an appropriate function type
  wasm_type params = wasm_make_tuple(ctx->module, wasm_types, argc);
  uint32_t fn_type = bjvm_register_function_type(ctx->module, params, wasm_int32());
  // Now at last generate a call_indirect
  expression call = wasm_call_indirect(
      ctx->module, 0, fn_ptr, args, argc + 3, fn_type);
  arrfree(args);
  return call;
}

expression wasm_lower_invokeinterface_polymorphic(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int insn_i) {
  // Oh boy.
  // (block       "outer"
  //   (block     "deopt"
  //     (nullcheck)
  //     (load itables interfaces pointer)
  //     i <- 0
  //     limit <- itables count
  //     (block        "itablefound"
  //       (loop         "itablescan"
  //         (load interface i)
  //         (if equal break itablefound)
  //         i <- i + 1
  //         if i >= limit (break deopt)
  //         (continue itablescan)
  //       )
  //     )
  //     (load entries i)
  //     (if entry & 1 break deopt)
  //     (load function pointer)
  //     (call indirect)
  //     (if result is INT (deopt with INVOKE_STATE_FRAME))
  //     (if result is EXC (break deopt))
  //     (load result from result pointer)
  //     (break outer)
  //   )
  //   (... deopt ...)
  // )
  int argc = insn->args;
  assert(argc > 0);

  wasm_module *M = ctx->module;

  //   [target]    [arg1]    [arg2]
  //    sd - 3     sd - 2    sd - 1
  expression target = get_stack_value(ctx, sd - argc, BJVM_TYPE_KIND_REFERENCE);

  expression outer_blk = wasm_block(M, nullptr, 0, wasm_void(), false);
  expression deopt_blk = wasm_block(M, nullptr, 0, wasm_void(), false);

  expression null_chk = wasm_br(
      M,
      wasm_unop(M, WASM_OP_KIND_I32_EQZ, target),
      deopt_blk);

  const int classdesc = add_local(ctx, wasm_int32());
  const int itable_i_local = add_local(ctx, wasm_int32());
  const int itable_count_local = add_local(ctx, wasm_int32());

  expression load_classdesc = get_desc(ctx, target);
  load_classdesc = wasm_local_set(M, classdesc, load_classdesc);

  expression load_itable_i = wasm_load(
      M, WASM_OP_KIND_I32_LOAD, wasm_local_get(M, classdesc, wasm_int32()),
      0, offsetof(bjvm_classdesc, itables) + offsetof(bjvm_itables, interfaces));
  expression load_itable_count = wasm_local_get(M, itable_count_local, wasm_int32());
}

expression call_wrapper(compile_ctx *ctx,
  expression outer_blk,
  expression deopt_blk,
  expression *get_ptr,
  expression fn_ptr,
  const bjvm_method_descriptor *m,
  bool is_static,
  int sd, int pc) {
  // (block    "outer"
  //   (block  "deopt"
  //      (get_ptr)
  //      interp_result <- (call_indirect (fn_ptr) (args))
  //      (if interp_result is INT (deopt with INVOKE_STATE_FRAME))
  //      (if interp_result is EXC (break deopt))
  //      (load result from result pointer)
  //      (break outer)
  //   )
  //   (... deopt ...)
  // )
  wasm_module *M = ctx->module;
  int interp_result = add_local(ctx, wasm_int32());

  expression *stmts = nullptr;
  for (int i = 0; i < arrlen(get_ptr); ++i) {
    arrput(stmts, get_ptr[i]);
  }
  expression call = wasm_make_indirect_call(ctx, fn_ptr, m, sd, is_static);
  call = wasm_local_set(M, interp_result, call);
  arrput(stmts, call);

  expression interrupt_deopt = deopt(ctx, pc, INVOKE_STATE_MADE_FRAME);
  expression is_int = wasm_binop(M, WASM_OP_KIND_I32_EQ, wasm_local_get(M, interp_result, wasm_int32()), wasm_i32_const(M, BJVM_INTERP_RESULT_INT));
  interrupt_deopt = wasm_if_else(
      M,
      is_int,
      interrupt_deopt,
      nullptr,
      wasm_void());

  expression is_exc = wasm_binop(M, WASM_OP_KIND_I32_EQ, wasm_local_get(M, interp_result, wasm_int32()), wasm_i32_const(M, BJVM_INTERP_RESULT_EXC));
  expression exc_deopt = DEOPT_IF(is_exc);

  arrput(stmts, interrupt_deopt);
  arrput(stmts, exc_deopt);

  if (m->return_type.base_kind != BJVM_TYPE_KIND_VOID) {
    expression load_result = wasm_load(
        M, WASM_OP_KIND_I32_LOAD, wasm_local_get(M, RESULT_PARAM, wasm_int32()), 0, 0);
    int argc = m->args_count + !is_static;
    load_result = set_stack_value(ctx, sd - argc, m->return_type.base_kind, load_result);
    arrput(stmts, load_result);
  }

  arrput(stmts, wasm_br(M, nullptr, outer_blk));

  wasm_update_block(M, deopt_blk, stmts, arrlen(stmts), wasm_void(), false);
  expression do_deopt = deopt(ctx, pc, INVOKE_STATE_ENTRY);
  wasm_update_block(M, outer_blk, (expression[]){deopt_blk, do_deopt}, 2, wasm_void(), false);

  arrfree(stmts);
  return outer_blk;
}

// Handles invokestatic, invokespecial, and monomorphic invokevirtual and invokeinterface calls
expression wasm_lower_invokesimple(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  wasm_module *M = ctx->module;
  // (block       "outer"
  //    (block    "deopt"
  //       (nullcheck)               -- invokespecial etc. only
  //       (compare classdesc)       -- invokevirtual and invokeinterface only
  //       (call_indirect)
  //       (if result is INT (deopt with INVOKE_STATE_FRAME))
  //       (if result is EXC (break deopt))
  //       (load result from result pointer)
  //       (break outer)
  //     )
  //     (... deopt ...)
  // )
  expression outer_blk = wasm_block(M, nullptr, 0, wasm_void(), false);
  expression deopt_blk = wasm_block(M, nullptr, 0, wasm_void(), false);

  expression *get_fn_ptr = nullptr;

  expression target = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
  expression null_chk = wasm_br(
        M,
        wasm_unop(M, WASM_OP_KIND_I32_EQZ, target),
        deopt_blk);
  arrput(get_fn_ptr, null_chk);
  if (insn->kind == bjvm_insn_invokeitable_monomorphic || insn->kind == bjvm_insn_invokevtable_monomorphic) {
    assert(insn->ic2);

    expression classdesc_chk = wasm_br(
          M,
          wasm_binop(M, WASM_OP_KIND_I32_NE, get_desc(ctx, target), insn->ic2),
          deopt_blk);
    arrput(get_fn_ptr, classdesc_chk);
  }

  expression entry_point = wasm_load(
        M, WASM_OP_KIND_I32_LOAD, wasm_i32_const(M, (int)insn->ic), 0,
        offsetof(bjvm_cp_method, entry_point));
  const bjvm_cp_method *m;
  bool is_static = false;

  switch (insn->kind) {
  case bjvm_insn_invokestatic:
    m = insn->ic;
    is_static = true;
    break;
  case bjvm_insn_invokespecial:
  case bjvm_insn_invokevtable_monomorphic:
  case bjvm_insn_invokeitable_monomorphic:
    m = insn->ic;
    break;
  default:
    UNREACHABLE();
  }

  assert(!!(m->access_flags & BJVM_ACCESS_STATIC) == is_static);

  expression call = call_wrapper(ctx, outer_blk, deopt_blk, get_fn_ptr, entry_point, m->descriptor, is_static, sd, pc);
  arrfree(get_fn_ptr);
  return call;
}

const int MAX_PC_TO_EMIT = 256;

bjvm_obj_header *wasm_runtime_allocate_object(bjvm_thread *thread, bjvm_classdesc *cls, int data_size) {
  return AllocateObject(thread, cls, data_size);
}

// For now, we just perform a call to AllocateObject, but we'll make this better
// at some point. (I also plan to do escape analysis)
expression wasm_lower_new_resolved(compile_ctx *ctx,
                                     const bjvm_bytecode_insn *insn, int sd, int insn_i) {
  bjvm_classdesc *class = insn->classdesc;
  assert(class);
  assert(class->kind == BJVM_CD_KIND_ORDINARY);

  // (block
  //    (spill object pointers)
  //    (call AllocateObject)
  //    (if null return EXC)
  //    (reload object pointers)
  // )
  expression *stmts = nullptr;
  expression spill = spill_or_load_object_pointers(ctx, insn_i, SPILL);
  if (spill) {
    arrput(stmts, spill);
  }
  expression allocate = wasm_call(
      ctx->module, wasm_import_runtime_function(ctx->module, wasm_runtime_allocate_object, "iii", "i"),
      (expression[]){
        wasm_local_get(ctx->module, THREAD_PARAM, wasm_int32()),
        wasm_i32_const(ctx->module, (int)class),
        wasm_i32_const(ctx->module, class->instance_bytes)
      }, 1);
  allocate = set_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE, allocate);
  arrput(stmts, allocate);
  // TODO this will attempt to allocate the object AGAIN on oom. Is that ok?
  expression nullchk = wasm_if_else(
      ctx->module,
      wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, get_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE)),
      deopt(ctx, insn_i, INVOKE_STATE_ENTRY),
      nullptr,
      wasm_void());
  arrput(stmts, nullchk);
  expression reload = spill_or_load_object_pointers(ctx, insn_i, LOAD);
  if (reload) {
    arrput(stmts, reload);
  }
  expression result = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(result);
  return result;
}

// idiv, irem, ldiv, lrem
static expression lower_integer_div(compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd, int pc) {
  // (block
  //    (if zero check)
  //    (if overflow check)
  //    (perform division)
  // )
  wasm_binary_op_kind op;
  wasm_unary_op_kind cmp_zero;
  bjvm_type_kind ty;
  switch (insn->kind) {
    case bjvm_insn_idiv:
      op = WASM_OP_KIND_I32_DIV_S;
      ty = BJVM_TYPE_KIND_INT;
      cmp_zero = WASM_OP_KIND_I32_EQZ;
      break;
    case bjvm_insn_irem:
      op = WASM_OP_KIND_I32_REM_S;
      ty = BJVM_TYPE_KIND_INT;
      cmp_zero = WASM_OP_KIND_I32_EQZ;
      break;
    case bjvm_insn_ldiv:
      op = WASM_OP_KIND_I64_DIV_S;
      ty = BJVM_TYPE_KIND_LONG;
      cmp_zero = WASM_OP_KIND_I64_EQZ;
      break;
    case bjvm_insn_lrem:
      op = WASM_OP_KIND_I64_REM_S;
      ty = BJVM_TYPE_KIND_LONG;
      cmp_zero = WASM_OP_KIND_I64_EQZ;
      break;
  default:
    UNREACHABLE();
  }

  expression *stmts = nullptr;
  expression right = get_stack_value(ctx, sd - 1, ty);
  expression left = get_stack_value(ctx, sd - 2, ty);
  expression zero_chk = wasm_if_else(
      ctx->module,
      wasm_unop(ctx->module, cmp_zero, right),
      deopt(ctx, pc, INVOKE_STATE_ENTRY),
      nullptr,
      wasm_void());
  arrput(stmts, zero_chk);
  expression div = wasm_binop(
      ctx->module, WASM_OP_KIND_I32_DIV_S,
      left, right);
  arrput(stmts, set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_INT, div));
  expression result = wasm_block(ctx->module, stmts, arrlen(stmts), wasm_void(), false);
  arrfree(stmts);
  return result;
}

// Each basic block compiles into a WASM block with epsilon type transition
static expression compile_bb(compile_ctx *ctx, const bjvm_basic_block *bb,
                             topo_ctx *topo, bool debug) {
  expression *results = nullptr;
  int result_count = 0, result_cap = 0;

#undef PUSH_EXPR
#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

#define CONVERSION_OP(from, to, op)                                            \
  {                                                                            \
    expression value = get_stack_value(ctx, sd - 1, from);                     \
    expression converted =                                                     \
        wasm_unop(ctx->module, WASM_OP_KIND_##op, value);            \
    PUSH_EXPR = set_stack_value(ctx, sd - 1, to, converted);                   \
    break;                                                                     \
  }

#define BIN_OP_SAME_TYPE(kind, op)                                             \
  {                                                                            \
    expression right = get_stack_value(ctx, sd - 1, kind);                     \
    expression left = get_stack_value(ctx, sd - 2, kind);                      \
    expression result =                                                        \
        wasm_binop(ctx->module, WASM_OP_KIND_##op, left, right);     \
    PUSH_EXPR = set_stack_value(ctx, sd - 2, kind, result);                    \
    break;                                                                     \
  }

  uint16_t *stack_depths = ctx->analysis->insn_index_to_stack_depth;
  bool outgoing_edges_processed = false;

  int expr_i = 0, i = 0, pc = bb->start_index;
  for (; i < bb->insn_count; ++i, ++pc, ++expr_i) {
    if (debug && pc > MAX_PC_TO_EMIT) {
      printf("Skipping emission of instruction %s\n",
             bjvm_insn_code_name(bb->start[pc].kind));
      goto unimplemented;
    }

    const bjvm_bytecode_insn *insn = bb->start + i;
    int sd = stack_depths[pc];

    printf("%d\n", insn->kind);

    switch (insn->kind) {
    case bjvm_insn_dload:
    case bjvm_insn_fload:
    case bjvm_insn_iload:
    case bjvm_insn_lload:
    case bjvm_insn_dstore:
    case bjvm_insn_fstore:
    case bjvm_insn_istore:
    case bjvm_insn_lstore:
    case bjvm_insn_aload:
    case bjvm_insn_astore:
      PUSH_EXPR = wasm_lower_local_load_store(ctx, insn, sd);
      break;
    case bjvm_insn_dreturn:
    case bjvm_insn_freturn:
    case bjvm_insn_ireturn:
    case bjvm_insn_lreturn:
    case bjvm_insn_return:
    case bjvm_insn_areturn: {
      if (insn->kind != bjvm_insn_return) {
        PUSH_EXPR = wasm_lower_return(ctx, insn, pc, sd);
      }
      PUSH_EXPR = frame_end(ctx, BJVM_INTERP_RESULT_OK);
      outgoing_edges_processed = true;
      break;
    }
    case bjvm_insn_nop:
      --expr_i;
      continue;
    case bjvm_insn_iaload:
    case bjvm_insn_aaload:
    case bjvm_insn_saload:
    case bjvm_insn_baload:
    case bjvm_insn_caload:
    case bjvm_insn_faload:
    case bjvm_insn_daload:
    case bjvm_insn_laload: {
      PUSH_EXPR = lower_array_load(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_aastore:
    case bjvm_insn_bastore:
    case bjvm_insn_castore:
    case bjvm_insn_dastore:
    case bjvm_insn_fastore:
    case bjvm_insn_sastore:
    case bjvm_insn_lastore:
    case bjvm_insn_iastore: {
      PUSH_EXPR = lower_array_store(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_aconst_null: {
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE,
                                  wasm_i32_const(ctx->module, 0));
      break;
    }
    case bjvm_insn_arraylength: {
      expression array = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
      expression nullcheck =
          wasm_unop(ctx->module, WASM_OP_KIND_I32_EQZ, array);
      expression length = wasm_get_array_length(ctx, array);
      expression store_length =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, length);
      PUSH_EXPR = wasm_if_else(ctx->module, nullcheck, deopt(ctx, pc, INVOKE_STATE_ENTRY),
                                    store_length, wasm_int32());
      break;
    }
    case bjvm_insn_athrow: {
      goto unimplemented;
      break;
    }

    case bjvm_insn_d2f:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_FLOAT,
                    F32_DEMOTE_F64);
    case bjvm_insn_d2i:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_INT,
                    I32_TRUNC_SAT_F64_S);
    case bjvm_insn_d2l:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_LONG,
                    I64_TRUNC_SAT_F64_S);
    case bjvm_insn_dadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, F64_ADD);
    case bjvm_insn_ddiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, F64_DIV);
    case bjvm_insn_dmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, F64_MUL);
    case bjvm_insn_dneg:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_DOUBLE, F64_NEG);
    case bjvm_insn_dsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, F64_SUB);
    case bjvm_insn_dup:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd,
                                  inspect_value_type(ctx, pc, sd - 1));
      break;
    case bjvm_insn_dup_x1:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 2));
      break;
    case bjvm_insn_dup_x2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 3));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 3,
                                  inspect_value_type(ctx, pc, sd - 3));
      break;
    case bjvm_insn_dup2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd,
                                  inspect_value_type(ctx, pc, sd - 2));
      break;
    case bjvm_insn_dup2_x1:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 3));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd + 1, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 3,
                                  inspect_value_type(ctx, pc, sd - 1));
      break;
    case bjvm_insn_dup2_x2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 3));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 4, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd + 1, sd - 3,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 4,
                                  inspect_value_type(ctx, pc, sd - 2));
      break;
    case bjvm_insn_f2d:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_DOUBLE,
                    F64_PROMOTE_F32);
    case bjvm_insn_f2i:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_INT,
                    I32_TRUNC_SAT_F32_S);
    case bjvm_insn_f2l:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_LONG,
                    I64_TRUNC_SAT_F32_S);
    case bjvm_insn_fadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, F32_ADD);
    case bjvm_insn_fdiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, F32_DIV);
    case bjvm_insn_fmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, F32_MUL);
    case bjvm_insn_fneg:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_FLOAT, F32_NEG);
    case bjvm_insn_fsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, F32_SUB);
    case bjvm_insn_i2b:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_INT, I32_EXTEND_S_I8);
    case bjvm_insn_i2c: {
      expression val = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
      expression max_char = wasm_i32_const(ctx->module, 0xFFFF);
      expression result = wasm_binop(
          ctx->module, WASM_OP_KIND_I32_AND, val, max_char);
      PUSH_EXPR = set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, result);
      break;
    }
    case bjvm_insn_i2d:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_DOUBLE,
                    F64_CONVERT_S_I32);
    case bjvm_insn_i2f:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_FLOAT,
                    F32_CONVERT_S_I32);
    case bjvm_insn_i2l:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_LONG, I64_EXTEND_S_I32);
    case bjvm_insn_i2s:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_INT, I32_EXTEND_S_I16);
    case bjvm_insn_iadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_ADD);
    case bjvm_insn_iand:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_AND);
    case bjvm_insn_imul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_MUL);
    case bjvm_insn_ineg: {
      // no unary negation in wasm
      expression zero = wasm_i32_const(ctx->module, 0);
      expression value = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
      expression result =
          wasm_binop(ctx->module, WASM_OP_KIND_I32_SUB, zero, value);
      expression store =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, result);
      PUSH_EXPR = store;
      break;
    }
    case bjvm_insn_ior:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_OR);
    case bjvm_insn_ishl:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_SHL);
    case bjvm_insn_ishr:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_SHR_S);
    case bjvm_insn_isub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_SUB);
    case bjvm_insn_iushr:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_SHR_U);
    case bjvm_insn_ixor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_XOR);
    case bjvm_insn_l2d:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_DOUBLE,
                    F64_CONVERT_S_I64);
    case bjvm_insn_l2f:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_FLOAT,
                    F32_CONVERT_S_I64);
    case bjvm_insn_l2i:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_INT, I32_WRAP_I64);
    case bjvm_insn_ladd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_ADD);
    case bjvm_insn_land:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_AND);
    case bjvm_insn_lcmp: {
      // if a > b then 1, if a < b then -1, else 0
      PUSH_EXPR = wasm_lower_lcmp(ctx, insn, sd);
      break;
    }
    case bjvm_insn_ldiv:
    case bjvm_insn_lrem:
    case bjvm_insn_idiv:
    case bjvm_insn_irem:
      lower_integer_div(ctx, insn, sd, pc);
      break;
    case bjvm_insn_lmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_MUL);
    case bjvm_insn_lneg: {
      // no unary negation in wasm
      expression zero = wasm_i64_const(ctx->module, 0);
      expression value = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_LONG);
      expression result =
          wasm_binop(ctx->module, WASM_OP_KIND_I64_SUB, zero, value);
      expression store =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_LONG, result);
      PUSH_EXPR = store;
      break;
    }
    case bjvm_insn_lor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_OR);
    case bjvm_insn_lshl:
    case bjvm_insn_lshr:
    case bjvm_insn_lushr: {
      PUSH_EXPR = wasm_lower_long_shift(ctx, insn, sd);
      break;
    }
    case bjvm_insn_lsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_SUB);
    case bjvm_insn_lxor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_XOR);
    case bjvm_insn_pop:
    case bjvm_insn_pop2: {
      // nothing, just changes stack depth
      break;
    }
    case bjvm_insn_swap: {
      // Woo-hoo
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 1));
      break;
    }
    case bjvm_insn_checkcast_resolved: {
      PUSH_EXPR = lower_checkcast_resolved(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_putfield:
    case bjvm_insn_getfield: {
      goto unimplemented;
      break;
    }
    case bjvm_insn_instanceof_resolved: {
      PUSH_EXPR = lower_instanceof_resolved(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_new_resolved: {
      PUSH_EXPR = wasm_lower_new_resolved(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_getstatic_B ... bjvm_insn_putstatic_L: {
      PUSH_EXPR = lower_getstatic_putstatic_resolved(ctx, insn, sd);
      break;
    }
    case bjvm_insn_getfield_B ... bjvm_insn_putfield_L: {
      PUSH_EXPR = lower_getfield_putfield_resolved(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_goto: {
      int next = topo->block_to_topo[bb->next[0]];
      if (next != topo->topo_i + 1) {
        expression be = next < topo->topo_i + 1 ? topo->loop_headers[next]
                                                : topo->block_ends[next];
        assert(be);
        PUSH_EXPR = wasm_br(ctx->module, nullptr, be);
      }
      outgoing_edges_processed = true;
      break;
    }
    case bjvm_insn_invokevirtual:
    case bjvm_insn_invokeinterface:
    case bjvm_insn_invokedynamic: {
      goto unimplemented;
    }
    case bjvm_insn_invokespecial:
    case bjvm_insn_invokestatic:
    case bjvm_insn_invokevtable_monomorphic:
    case bjvm_insn_invokeitable_monomorphic: {
      PUSH_EXPR = wasm_lower_invokesimple(ctx, insn, sd, pc);
      break;
    }
    case bjvm_insn_invokevtable_polymorphic: {
      goto unimplemented;
    }
    case bjvm_insn_ldc:
    case bjvm_insn_ldc2_w: {
      expression result = wasm_lower_ldc(ctx, insn, sd, pc);
      if (!result)
        goto unimplemented;
      PUSH_EXPR = result;
      break;
    }
    case bjvm_insn_if_acmpeq:
    case bjvm_insn_if_acmpne:
    case bjvm_insn_if_icmpeq:
    case bjvm_insn_if_icmpne:
    case bjvm_insn_if_icmplt:
    case bjvm_insn_if_icmpge:
    case bjvm_insn_if_icmpgt:
    case bjvm_insn_if_icmple:
    case bjvm_insn_ifeq:
    case bjvm_insn_ifne:
    case bjvm_insn_iflt:
    case bjvm_insn_ifge:
    case bjvm_insn_ifgt:
    case bjvm_insn_ifle:
    case bjvm_insn_ifnonnull:
    case bjvm_insn_ifnull: {
      int op_kinds[] = {
          WASM_OP_KIND_I32_EQ,   // if_acmpeq
          WASM_OP_KIND_I32_NE,   // if_acmpne
          WASM_OP_KIND_I32_EQ,   // if_icmpeq
          WASM_OP_KIND_I32_NE,   // if_icmpne
          WASM_OP_KIND_I32_LT_S, // if_icmplt
          WASM_OP_KIND_I32_GE_S, // if_icmpge
          WASM_OP_KIND_I32_GT_S, // if_icmpgt
          WASM_OP_KIND_I32_LE_S, // if_icmple
          WASM_OP_KIND_I32_EQ,   // ifeq
          WASM_OP_KIND_I32_NE,   // ifne
          WASM_OP_KIND_I32_LT_S, // iflt
          WASM_OP_KIND_I32_GE_S, // ifge
          WASM_OP_KIND_I32_GT_S, // ifgt
          WASM_OP_KIND_I32_LE_S, // ifle
          WASM_OP_KIND_I32_NE,   // ifnonnull
          WASM_OP_KIND_I32_EQ    // ifnull
      };

      int taken = topo->block_to_topo[bb->next[0]];
      int taken_is_backedge = bb->is_backedge[0];
      int not_taken = topo->block_to_topo[bb->next[1]];
      int not_taken_is_backedge = bb->is_backedge[1];

      // We are going to emit either a br_if/br combo or just a br_if. If the
      // taken path is a fallthrough, negate the condition and switch.
      bjvm_insn_code_kind kind = insn->kind;
      if (taken == topo->topo_i + 1) {
        int tmp = taken;
        taken = not_taken;
        not_taken = tmp;
        tmp = not_taken_is_backedge;
        not_taken_is_backedge = taken_is_backedge;
        taken_is_backedge = tmp;
        // I've ordered them so that this works out
        kind = ((kind - 1) ^ 1) + 1;
      }

      // Emit the br_if for the taken branch
      expression taken_block = taken_is_backedge ? topo->loop_headers[taken]
                                                 : topo->block_ends[taken];
      if (taken != topo->topo_i + 1) {
        bool is_binary = kind <= bjvm_insn_if_icmple;
        expression rhs = is_binary
                             ? get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT)
                             : wasm_i32_const(ctx->module, 0);
        expression lhs =
            get_stack_value(ctx, sd - 1 - is_binary, BJVM_TYPE_KIND_INT);
        wasm_binary_op_kind op = op_kinds[kind - bjvm_insn_if_acmpeq];
        expression cond = wasm_binop(ctx->module, op, lhs, rhs);
        expression br_if = wasm_br(ctx->module, cond, taken_block);
        PUSH_EXPR = br_if;
      }
      if (not_taken != topo->topo_i + 1) {
        // Emit the br for the not taken branch
        expression not_taken_block = not_taken_is_backedge
                                         ? topo->loop_headers[not_taken]
                                         : topo->block_ends[not_taken];
        PUSH_EXPR = wasm_br(ctx->module, nullptr, not_taken_block);
      }
      outgoing_edges_processed = true;
      break;
    }
    case bjvm_insn_tableswitch:
    case bjvm_insn_lookupswitch:
    case bjvm_insn_ret:
    case bjvm_insn_jsr:
      goto unimplemented;
    case bjvm_insn_iconst: {
      expression value =
          wasm_i32_const(ctx->module, (int)insn->integer_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_INT, value);
      break;
    }
    case bjvm_insn_dconst: {
      expression value = wasm_f64_const(ctx->module, insn->d_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_DOUBLE, value);
      break;
    }
    case bjvm_insn_fconst: {
      expression value = wasm_f32_const(ctx->module, insn->f_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_FLOAT, value);
      break;
    }
    case bjvm_insn_lconst: {
      expression value = wasm_i64_const(ctx->module, insn->integer_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_LONG, value);
      break;
    }
    case bjvm_insn_iinc: {
      PUSH_EXPR = wasm_lower_iinc(ctx, insn, sd);
      break;
    }
    case bjvm_insn_multianewarray: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_multianewarray(ctx, insn, sd);
      break;
    }
    case bjvm_insn_newarray: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_newarray(ctx, insn, sd);
      break;
    }
    case bjvm_insn_monitorenter:
    case bjvm_insn_monitorexit:
      break;
    case bjvm_insn_fcmpl:
    case bjvm_insn_fcmpg:
    case bjvm_insn_frem: // deprecated
    case bjvm_insn_drem: // deprecated
    case bjvm_insn_dcmpg:
    case bjvm_insn_dcmpl:
    default:
      goto unimplemented;
    }
  }

  if (0) {
  unimplemented:
    PUSH_EXPR = deopt(ctx, pc, INVOKE_STATE_ENTRY);
  } else if (!outgoing_edges_processed && bb->next_count) {
    int next = topo->block_to_topo[bb->next[0]];
    if (next != topo->topo_i + 1) {
      expression be = next < topo->topo_i + 1 ? topo->loop_headers[next]
                                              : topo->block_ends[next];
      assert(be);
      PUSH_EXPR = wasm_br(ctx->module, nullptr, be);
    }
  }

done:
  // Create a block with the expressions
  expression block = wasm_block(ctx->module, results, result_count,
                                     wasm_void(), false);
  ctx->wasm_blocks[bb->my_index] = block;

  free(results);
  return block;
}

void free_topo_ctx(topo_ctx ctx) {
  free(ctx.topo_to_block);
  free(ctx.block_to_topo);
  free(ctx.visited);
  free(ctx.incoming_count);
  free(ctx.loop_depths);
  free(ctx.block_ends);
  free(ctx.loop_headers);
  for (int i = 0; i < ctx.blockc; ++i) {
    free(ctx.creations[i].requested);
  }
  free(ctx.creations);
}

void find_block_insertion_points(bjvm_code_analysis *analy, topo_ctx *ctx) {
  // For each bb, if there is a bb preceding it (in the topological sort)
  // which branches to that bb, which is NOT its immediate predecessor in the
  // sort, we need to introduce a wasm (block) to handle the branch. We place
  // it at the point in the topo sort where the depth becomes equal to the
  // depth of the topological predecessor of bb.
  for (int i = 0; i < analy->block_count; ++i) {
    bjvm_basic_block *b = analy->blocks + i;
    for (int j = 0; j < b->prev_count; ++j) {
      int prev = b->prev[j];
      if (ctx->block_to_topo[prev] >= ctx->block_to_topo[i] - 1)
        continue;
      bb_creations_t *creations = ctx->creations + ctx->block_to_topo[b->idom];
      *VECTOR_PUSH(creations->requested, creations->count, creations->cap) =
          (ctx->block_to_topo[i] << 1) + 1;
      break;
    }
  }
}

void topo_walk_idom(bjvm_code_analysis *analy, topo_ctx *ctx) {
  int current = ctx->current_block_i;
  int start = ctx->topo_i;
  ctx->visited[current] = 1;
  ctx->block_to_topo[current] = ctx->topo_i;
  ctx->topo_to_block[ctx->topo_i++] = current;
  ctx->loop_depths[current] = ctx->loop_depth;
  bjvm_basic_block *b = analy->blocks + current;
  bool is_loop_header = b->is_loop_header;
  // Sort successors by reverse post-order in the original DFS
  bjvm_dominated_list_t idom = b->idominates;
  int *sorted = calloc(idom.count, sizeof(int));
  memcpy(sorted, idom.list, idom.count * sizeof(int));
  for (int i = 1; i < idom.count; ++i) {
    int j = i;
    while (j > 0 && analy->blocks[sorted[j - 1]].dfs_post <
                        analy->blocks[sorted[j]].dfs_post) {
      int tmp = sorted[j];
      sorted[j] = sorted[j - 1];
      sorted[j - 1] = tmp;
      j--;
    }
  }
  // Recurse on the sorted successors
  ctx->loop_depth += is_loop_header;
  for (int i = 0; i < idom.count; ++i) {
    int next = sorted[i];
    ctx->current_block_i = next;
    topo_walk_idom(analy, ctx);
  }
  ctx->loop_depth -= is_loop_header;
  bb_creations_t *creations = ctx->creations + start;
  if (is_loop_header)
    *VECTOR_PUSH(creations->requested, creations->count, creations->cap) =
        ctx->topo_i << 1;
  free(sorted);
}

topo_ctx make_topo_sort_ctx(bjvm_code_analysis *analy) {
  topo_ctx ctx;
  ctx.topo_to_block = calloc(analy->block_count, sizeof(int));
  ctx.block_to_topo = calloc(analy->block_count, sizeof(int));
  ctx.visited = calloc(analy->block_count, sizeof(int));
  ctx.incoming_count = calloc(analy->block_count, sizeof(int));
  ctx.loop_depths = calloc(analy->block_count, sizeof(int));
  ctx.creations = calloc(analy->block_count, sizeof(bb_creations_t));
  ctx.block_ends = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx.loop_headers = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx.blockc = analy->block_count;
  ctx.current_block_i = ctx.topo_i = ctx.loop_depth = 0;
  for (int i = 0; i < analy->block_count; ++i) {
    bjvm_basic_block *b = analy->blocks + i;
    for (int j = 0; j < b->next_count; ++j)
      ctx.incoming_count[b->next[j]] += !b->is_backedge[j];
  }

  // Perform a post-order traversal of the immediate dominator tree. Whenever
  // reaching a loop header, output the loop header immediately, then everything
  // in the subtree as one contiguous block. We output them in reverse postorder
  // relative to a DFS on the original CFG, to guarantee that the final
  // topological sort respects the forward edges in the original graph.
  topo_walk_idom(analy, &ctx);
  assert(ctx.topo_i == analy->block_count);
  find_block_insertion_points(analy, &ctx);
  return ctx;
}

typedef struct {
  expression ref;
  // Wasm block started here (in topological sort)
  int started_at;
  // Once we get to this basic block, close the wasm block and push the
  // expression to the stack
  int close_at;
  // Whether to emit a (loop) for this block
  bool is_loop;
} inchoate_expression;

static int cmp_ints_reverse(const void *a, const void *b) {
  return *(int *)b - *(int *)a;
}

[[nodiscard]] static wasm_type assign_input_params(compile_ctx *ctx) {
  wasm_value_type *args = nullptr;
  arrput(args, WASM_TYPE_KIND_INT32);  // thread
  arrput(args, WASM_TYPE_KIND_INT32);  // method
  arrput(args, WASM_TYPE_KIND_INT32);  // result
  if (!(ctx->method->access_flags & BJVM_ACCESS_STATIC)) {
    arrput(args, WASM_TYPE_KIND_INT32);  // this
  }
  printf("BURH: %p %p %d\n", ctx->method, ctx->method->descriptor, ctx->method->descriptor->args_count);
  for (int i = 0; i < ctx->method->descriptor->args_count; ++i) {
    bjvm_type_kind kind = field_to_kind(ctx->method->descriptor->args + i);
    wasm_type wt = jvm_type_to_wasm(kind);
    arrput(args, wt.val);
  }
  ctx->next_local = arrlen(args);
  printf("LMAO: %d\n", ctx->next_local);
  wasm_type tuple = wasm_make_tuple(ctx->module, args, arrlen(args));
  arrfree(args);
  return tuple;
}

wasm_instantiation_result *
wasm_jit_compile(bjvm_thread *thread, const bjvm_cp_method *method,
                      bool debug) {
  wasm_instantiation_result *wasm = nullptr;
  printf(
      "Requesting compile for method %.*s on class %.*s with signature %.*s\n",
      fmt_slice(method->name), fmt_slice(method->my_class->name),
      fmt_slice(method->unparsed_descriptor));

  // Resulting signature and (roughly) behavior is same as
  // bjvm_bytecode_interpret:
  // (bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *result)
  // -> bjvm_interpreter_result_t
  // the method field is IGNORED.
  // The key difference is that the frame MUST be the topmost frame, and this
  // must be the first invocation, whereas in the interpreter, we can interpret
  // things after an interrupt.
  assert(method->code);
  bjvm_scan_basic_blocks(method->code, method->code_analysis);
  bjvm_compute_dominator_tree(method->code_analysis);
  if (bjvm_attempt_reduce_cfg(method->code_analysis))
    // CFG is not reducible, so we can't compile it (yet! -- low prio)
    goto error_1;

  bjvm_code_analysis *analy = method->code_analysis;

  compile_ctx ctx = {nullptr};
  ctx.wasm_blocks = calloc(analy->block_count, sizeof(expression));
  ctx.module = wasm_module_create();
  ctx.method = method;
  ctx.analysis = analy;
  // first three used by thread, frame, result
  ctx.next_local = 3;
  ctx.code = method->code;
  int vtlmap_len = WASM_TYPES_COUNT *
                   (method->code->max_locals + method->code->max_stack) *
                   sizeof(int);
  ctx.val_to_local_map = malloc(vtlmap_len);
  memset(ctx.val_to_local_map, -1, vtlmap_len);

  // Credit to
  // https://medium.com/leaningtech/solving-the-structured-control-flow-problem-once-and-for-all-5123117b1ee2
  // for the excellent explainer.

  // Perform a topological sort using the DAG of forward edges, under the
  // constraint that loop headers must be immediately followed by all bbs
  // that they dominate, in some order. This is necessary to ensure that we
  // can continue in the loops. We will wrap these bbs in a "(loop)" wasm block.
  // Then for forward edges, we let consecutive blocks fall through, and
  // otherwise place a (block) scope ending just before the target block,
  // so that bbs who try to target it can break to it.
  topo_ctx topo = make_topo_sort_ctx(analy);

  // Assign wasm locals to the appropriate JVM local/type pairs
  wasm_type params = assign_input_params(&ctx);
  printf("REACHED\n");

  inchoate_expression *expr_stack = nullptr;
  int stack_count = 0, stack_cap = 0;
  // Push an initial boi ...
  *VECTOR_PUSH(expr_stack, stack_count, stack_cap) = (inchoate_expression){
      wasm_block(ctx.module, nullptr, 0, wasm_void(), false), 0,
      analy->block_count, false};
  printf("REACHED2\n");
  // ... which call contain the stack frame creation stub
  *VECTOR_PUSH(expr_stack, stack_count, stack_cap) = (inchoate_expression){
      frame_setup(&ctx), 0, -1,
      false};
  for (topo.topo_i = 0; topo.topo_i <= analy->block_count; ++topo.topo_i) {
    // First close off any blocks as appropriate
    for (int i = stack_count - 1; i >= 0; --i) {
      inchoate_expression *ie = expr_stack + i;
      if (ie->close_at == topo.topo_i) {
        // Take expressions i + 1 through stack_count - 1 inclusive and
        // make them the contents of the block
        expression *exprs = malloc((stack_count - i - 1) * sizeof(expression));
        for (int j = i + 1; j < stack_count; ++j)
          exprs[j - i - 1] = expr_stack[j].ref;
        wasm_update_block(ctx.module, ie->ref, exprs, stack_count - i - 1,
                               wasm_void(), ie->is_loop);
        free(exprs);
        // Update the handles for blocks and loops to break to
        if (topo.topo_i < analy->block_count)
          topo.block_ends[topo.topo_i] = nullptr;
        if (ie->is_loop)
          topo.loop_headers[ie->started_at] = nullptr;
        ie->close_at = -1;
        stack_count = i + 1;
      }
    }
    // Done pushing expressions
    if (topo.topo_i == analy->block_count)
      break;

    // Then create (block)s and (loop)s as appropriate. First create blocks
    // in reverse order of the topological order of their targets. So, if
    // at the current block we are to initiate blocks ending at block with
    // topo indices 9, 12, and 13, push the block for 13 first.

    // Blocks first
    bb_creations_t *creations = topo.creations + topo.topo_i;
    qsort(creations->requested, creations->count, sizeof(int),
          cmp_ints_reverse);
    for (int i = 0; i < creations->count; ++i) {
      int block_i = creations->requested[i];
      int is_loop = !(block_i & 1);
      block_i >>= 1;
      expression block =
          wasm_block(ctx.module, nullptr, 0, wasm_void(), is_loop);
      *VECTOR_PUSH(expr_stack, stack_count, stack_cap) =
          (inchoate_expression){block, topo.topo_i, block_i, is_loop};
      if (is_loop)
        topo.loop_headers[topo.topo_i] = block;
      else
        topo.block_ends[block_i] = block;
    }

    int block_i = topo.topo_to_block[topo.topo_i];
    bjvm_basic_block *bb = analy->blocks + block_i;
    debug = utf8_equals(method->name, "encodeArrayLoop");
    expression expr = compile_bb(&ctx, bb, &topo, debug);
    if (!expr) {
      goto error_2;
    }
    *VECTOR_PUSH(expr_stack, stack_count, stack_cap) =
        (inchoate_expression){expr, topo.topo_i, -1, false};
  }

  expression body = expr_stack[0].ref;
  free(expr_stack);

  // Add a function whose expression is the first expression on the stack
  wasm_type locals =
      wasm_make_tuple(ctx.module, ctx.wvars, ctx.wvars_count);
  printf("REACHED4\n");
  wasm_function *fn = wasm_add_function(
      ctx.module, params, wasm_int32(), locals, body, "run");
  printf("REACHED5\n");
  wasm_export_function(ctx.module, fn);

  bjvm_bytevector result = wasm_module_serialize(ctx.module);
  wasm = wasm_instantiate_module(ctx.module, method->name.chars);
  if (wasm->status != WASM_INSTANTIATION_SUCCESS) {
    // printf("Error instantiating module for method %.*s\n",
    // fmt_slice(method->name));
  } else {
    // printf("Successfully compiled method %.*s on class %.*s \n",
    // fmt_slice(method->name), fmt_slice(method->my_class->name));
    int argc = bjvm_argc(method);
    bjvm_type_kind kinds[argc];

    int i = 0;
    if (!(method->access_flags & BJVM_ACCESS_STATIC))
      kinds[i++] = BJVM_TYPE_KIND_REFERENCE;
    for (int j = 0; j < method->descriptor->args_count; ++j)
      kinds[i++] = field_to_kind(method->descriptor->args + j);

    for (int i = 0; i < argc; ++i) {
      printf("%c", kinds[i]);
    }
    printf("\n");

    wasm->adapter = create_adapter_to_compiled_method(kinds, argc);
  }

  free(result.bytes);

error_2:
  free_topo_ctx(topo);
error_1:
  free(ctx.val_to_local_map);
  free(ctx.wasm_blocks);
  free(ctx.wvars);
  wasm_module_free(ctx.module);

  return wasm;
}

void free_wasm_compiled_method(void *p) {
  if (!p)
    return;
  wasm_instantiation_result *compiled_method = p;
  free(compiled_method->exports);
  free(compiled_method);
}