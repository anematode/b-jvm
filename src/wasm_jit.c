// Java bytecode to WebAssembly translator

#include "wasm_jit.h"
#include "analysis.h"
#include "arrays.h"
#include "objects.h"
#include "wasm_utils.h"

#define expression bjvm_wasm_expression *

enum { THREAD_PARAM, FRAME_PARAM, RESULT_PARAM };

typedef struct {
  bjvm_wasm_function *new_object;
  bjvm_wasm_function *new_array;
  bjvm_wasm_function *raise_npe;
  bjvm_wasm_function *raise_oob;
} runtime_helpers;

typedef struct {
  // The WASM module we will emit as bytes
  bjvm_wasm_module *module;
  // The basic blocks we are building
  expression *wasm_blocks;

  // The method and (for convenience) its code and analysis
  bjvm_cp_method *method;
  bjvm_attribute_code *code;
  bjvm_code_analysis *analysis;

  // Mapping between value_index * 4 + type_kind and the wasm index for
  // local.get and local.set. -1 means not yet mapped.
  //
  // The ordering for type kind is REF/INT, FLOAT, DOUBLE, LONG.
  int *val_to_local_map;
  int next_local;

  bjvm_wasm_type *wvars;
  int wvars_count;
  int wvars_cap;

  runtime_helpers runtime;
} compile_ctx;

#define WASM_TYPES_COUNT 4

static bjvm_wasm_type jvm_type_to_wasm_kind(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return bjvm_wasm_int32();
  case BJVM_TYPE_KIND_FLOAT:
    return bjvm_wasm_float32();
  case BJVM_TYPE_KIND_DOUBLE:
    return bjvm_wasm_float64();
  case BJVM_TYPE_KIND_LONG:
    return bjvm_wasm_int64();
  case BJVM_TYPE_KIND_VOID:
    UNREACHABLE();
  }
}

static int local_kind(bjvm_wasm_type ty) {
  int result = ty.val - BJVM_WASM_TYPE_KIND_INT32;
  assert((unsigned)result < 4);
  return result;
}

int sizeof_wasm_kind(bjvm_wasm_type kind) {
  switch (kind.val) {
  case BJVM_WASM_TYPE_KIND_INT32:
  case BJVM_WASM_TYPE_KIND_FLOAT32:
    return 4;
  case BJVM_WASM_TYPE_KIND_FLOAT64:
  case BJVM_WASM_TYPE_KIND_INT64:
    return 8;
  default:
    UNREACHABLE();
  }
}

int jvm_stack_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  int i = index * WASM_TYPES_COUNT + local_kind(jvm_type_to_wasm_kind(kind));
  int *l = &ctx->val_to_local_map[i];
  if (*l == -1) {
    *VECTOR_PUSH(ctx->wvars, ctx->wvars_count, ctx->wvars_cap) =
        jvm_type_to_wasm_kind(kind);
    *l = ctx->next_local++;
  }
  return *l;
}

int jvm_local_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return jvm_stack_to_wasm_local(ctx, ctx->code->max_stack + index, kind);
}

expression get_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return bjvm_wasm_local_get(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind),
                             jvm_type_to_wasm_kind(kind));
}

expression set_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return bjvm_wasm_local_set(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind), value);
}

expression get_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return bjvm_wasm_local_get(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind),
                             jvm_type_to_wasm_kind(kind));
}

expression set_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return bjvm_wasm_local_set(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind), value);
}

// Generate spill code that writes to stack->values at the appropriate location,
// or load parameters from the stack at the function beginning.
expression spill_or_load_code(compile_ctx *ctx, int pc, bool do_load,
                              int return_value) {
  int values_start = offsetof(bjvm_stack_frame, values);
  int value_size = sizeof(bjvm_stack_value);

  bjvm_compressed_bitset refs[5];
  for (int i = 0; i < 5; ++i)
    refs[i] = ctx->analysis->insn_index_to_kinds[i][pc];

  expression *result = nullptr;
  int result_count = 0, result_cap = 0;

  for (int slot = 0; slot < ctx->code->max_locals + ctx->code->max_stack;
       ++slot) {
    expression frame =
        bjvm_wasm_local_get(ctx->module, FRAME_PARAM, bjvm_wasm_int32());
    int offset = values_start + slot * value_size;

    bjvm_wasm_type wkind;
    bjvm_type_kind kind;
    bjvm_wasm_load_op_kind load_op;
    bjvm_wasm_store_op_kind store_op;
    if (bjvm_test_compressed_bitset(refs[0], slot) || // int or ref
        bjvm_test_compressed_bitset(refs[1], slot)) {
      wkind = bjvm_wasm_int32();
      kind = BJVM_TYPE_KIND_INT;
      load_op = BJVM_WASM_OP_KIND_I32_LOAD;
      store_op = BJVM_WASM_OP_KIND_I32_STORE;
    } else if (bjvm_test_compressed_bitset(refs[2], slot)) { // float
      wkind = bjvm_wasm_float32();
      kind = BJVM_TYPE_KIND_FLOAT;
      load_op = BJVM_WASM_OP_KIND_F32_LOAD;
      store_op = BJVM_WASM_OP_KIND_F32_STORE;
    } else if (bjvm_test_compressed_bitset(refs[3], slot)) { // double
      wkind = bjvm_wasm_float64();
      kind = BJVM_TYPE_KIND_DOUBLE;
      load_op = BJVM_WASM_OP_KIND_F64_LOAD;
      store_op = BJVM_WASM_OP_KIND_F64_STORE;
    } else if (bjvm_test_compressed_bitset(refs[4], slot)) { // long
      wkind = bjvm_wasm_int64();
      kind = BJVM_TYPE_KIND_LONG;
      load_op = BJVM_WASM_OP_KIND_I64_LOAD;
      store_op = BJVM_WASM_OP_KIND_I64_STORE;
    } else { // unused slot
      continue;
    }

    int wasm_local = jvm_stack_to_wasm_local(ctx, slot, kind);
    if (do_load) {
      // (local.set wasm_local (i32.load (i32.add frame (i32.const offset))))
      expression load = bjvm_wasm_load(ctx->module, load_op, frame, 0, offset);
      *VECTOR_PUSH(result, result_count, result_cap) =
          bjvm_wasm_local_set(ctx->module, wasm_local, load);
    } else {
      // (i32.store (i32.add frame (i32.const offset)) (local.get wasm_local))
      expression value = bjvm_wasm_local_get(ctx->module, wasm_local, wkind);
      *VECTOR_PUSH(result, result_count, result_cap) =
          bjvm_wasm_store(ctx->module, store_op, frame, value, 0, offset);
    }
  }

  // Make block of all the spill/load instructions
  if (!do_load) {
    // Set thread->program_counter to pc and thread->stack_depth to the stack
    // depth
    int sd = ctx->analysis->insn_index_to_stack_depth[pc];
    expression frame =
        bjvm_wasm_local_get(ctx->module, FRAME_PARAM, bjvm_wasm_int32());
    // Can do it in one 32-bit store
    expression pc_sd_const = bjvm_wasm_i32_const(ctx->module, (sd << 16) | pc);
    expression store = bjvm_wasm_store(
        ctx->module, BJVM_WASM_OP_KIND_I32_STORE, frame, pc_sd_const, 0,
        offsetof(bjvm_stack_frame, program_counter));
    *VECTOR_PUSH(result, result_count, result_cap) = store;
    *VECTOR_PUSH(result, result_count, result_cap) = bjvm_wasm_return(
        ctx->module, bjvm_wasm_i32_const(ctx->module, return_value));
  }

  expression block =
      bjvm_wasm_block(ctx->module, result, result_count, bjvm_wasm_void());
  return block;
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_new_array(bjvm_thread *thread, bjvm_classdesc *classdesc,
                             int count) {
  return CreateObjectArray1D(thread, classdesc, count);
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_push_frame(bjvm_thread *thread, bjvm_cp_method *method) {}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  return AllocateObject(thread, classdesc, classdesc->data_bytes);
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t wasm_runtime_raise_npe(bjvm_thread *thread) {
  bjvm_null_pointer_exception(thread);
  return BJVM_INTERP_RESULT_EXC;
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t
wasm_runtime_raise_array_index_oob(bjvm_thread *thread, int index, int length) {
  return BJVM_INTERP_RESULT_EXC;
}

void add_runtime_imports(compile_ctx *ctx) {
  ctx->runtime.new_object = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_new_object, "ii", "i");
  ctx->runtime.new_array = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_new_array, "iii", "i");
  ctx->runtime.raise_npe = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_npe, "i", "i");
  ctx->runtime.raise_oob = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_array_index_oob, "iii", "i");
}

#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

expression wasm_get_array_length(compile_ctx *ctx, expression array) {
  return bjvm_wasm_load(ctx->module, BJVM_WASM_OP_KIND_I32_LOAD, array, 0,
                        kArrayLengthOffset);
}

expression wasm_raise_npe(compile_ctx *ctx) {
  // (return (call $raise_npe (local.get THREAD_PARAM)))
  return bjvm_wasm_return(
      ctx->module,
      bjvm_wasm_call(ctx->module, ctx->runtime.raise_npe,
                     (expression[]){bjvm_wasm_local_get(
                         ctx->module, THREAD_PARAM, bjvm_wasm_int32())},
                     1));
}

expression wasm_raise_oob(compile_ctx *ctx, expression index,
                          expression length) {
  // (return (call $raise_oob (local.get THREAD_PARAM) <index> <length>))
  return bjvm_wasm_return(
      ctx->module, bjvm_wasm_call(ctx->module, ctx->runtime.raise_oob,
                                  (expression[]){bjvm_wasm_local_get(
                                                     ctx->module, THREAD_PARAM,
                                                     bjvm_wasm_int32()),
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
    UNREACHABLE();
  }
}

expression wasm_move_value(compile_ctx *ctx, int pc, int from, int to) {
  bjvm_type_kind kind = inspect_value_type(ctx, pc, from);

  return bjvm_wasm_local_set(
      ctx->module, jvm_stack_to_wasm_local(ctx, to, kind),
      bjvm_wasm_local_get(ctx->module, jvm_stack_to_wasm_local(ctx, from, kind),
                          jvm_type_to_wasm_kind(kind)));
}

expression wasm_lower_array_load_store(compile_ctx *ctx,
                                       const bjvm_bytecode_insn *insn, int sd) {
  bjvm_type_kind component;
  bool is_store = false;
  switch (insn->kind) {
  case bjvm_insn_iastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_iaload:
    component = BJVM_TYPE_KIND_INT;
    break;
  case bjvm_insn_aastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_aaload:
    component = BJVM_TYPE_KIND_REFERENCE;
    break;
  case bjvm_insn_sastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_saload:
    component = BJVM_TYPE_KIND_SHORT;
    break;
  case bjvm_insn_bastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_baload:
    component = BJVM_TYPE_KIND_BYTE;
    break;
  case bjvm_insn_castore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_caload:
    component = BJVM_TYPE_KIND_CHAR;
    break;
  case bjvm_insn_fastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_faload:
    component = BJVM_TYPE_KIND_FLOAT;
    break;
  case bjvm_insn_dastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_daload:
    component = BJVM_TYPE_KIND_DOUBLE;
    break;
  case bjvm_insn_lastore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_laload:
    component = BJVM_TYPE_KIND_LONG;
    break;
  default:
    UNREACHABLE();
  }

  // ..., array, index, [value]  ->  [value]
  //                 (store only)  (load only)
  expression index =
      get_stack_value(ctx, sd - 1 - is_store, BJVM_TYPE_KIND_INT);
  expression array =
      get_stack_value(ctx, sd - 2 - is_store, BJVM_TYPE_KIND_REFERENCE);

  // <nullcheck> = (eqz array)
  expression null_check =
      bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I32_EQZ, array);
  // <addrminusheader> = array + index * sizeof(component)
  expression addr = bjvm_wasm_binop(
      ctx->module, BJVM_WASM_OP_KIND_I32_ADD, array,
      bjvm_wasm_binop(
          ctx->module, BJVM_WASM_OP_KIND_I32_MUL, index,
          bjvm_wasm_i32_const(ctx->module, sizeof_type_kind(component))));

  expression execute;
  bjvm_wasm_type dtype = jvm_type_to_wasm_kind(component);

  if (is_store) {
    // (<dtype>.store offset=kArrayDataOffset <addr>)
    expression value = get_stack_value(ctx, sd - 1, component);
    execute = bjvm_wasm_store(ctx->module, BJVM_WASM_OP_KIND_I32_STORE, addr,
                              value, 0, kArrayDataOffset);
  } else {
    expression data = bjvm_wasm_load(ctx->module, BJVM_WASM_OP_KIND_I32_LOAD,
                                     addr, 0, kArrayDataOffset);
    execute = set_stack_value(ctx, sd - 2, component, data);
  }

  // <length> = (i32.load (i32.add <array> kArrayLengthOffset))
  expression length = wasm_get_array_length(ctx, array);
  // <boundscheck> = (i32.ge_u <index> <length>)
  expression bounds_check =
      bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_GE_U, index, length);
  expression raise_oob = wasm_raise_oob(ctx, index, length);
  // <boundschecked>
  expression bounds_checked = bjvm_wasm_if_else(
      ctx->module, bounds_check, raise_oob, execute, bjvm_wasm_void());
  // (if (eqz <array>) (<raise npe>) (<boundschecked>)
  expression npe = wasm_raise_npe(ctx);
  expression load_store = bjvm_wasm_if_else(ctx->module, null_check, npe,
                                            bounds_checked, bjvm_wasm_void());
  return load_store;
}

expression wasm_lower_icmp_lcmp(compile_ctx *ctx,
                                const bjvm_bytecode_insn *insn, int sd) {
  bool is_long = insn->kind == bjvm_insn_lcmp;
  bjvm_type_kind kind = is_long ? BJVM_TYPE_KIND_LONG : BJVM_TYPE_KIND_INT;

  expression zero = is_long ? bjvm_wasm_i64_const(ctx->module, 0)
                            : bjvm_wasm_i32_const(ctx->module, 0);
  expression one = is_long ? bjvm_wasm_i64_const(ctx->module, 1)
                           : bjvm_wasm_i32_const(ctx->module, 1);
  expression negative_one = is_long ? bjvm_wasm_i64_const(ctx->module, -1)
                                    : bjvm_wasm_i32_const(ctx->module, -1);

  expression right = get_stack_value(ctx, sd - 1, kind);
  expression left = get_stack_value(ctx, sd - 2, kind);

  bjvm_wasm_binary_op_kind gt = is_long ? BJVM_WASM_OP_KIND_I64_GT_S
                                        : BJVM_WASM_OP_KIND_I32_GT_S,
                           lt = is_long ? BJVM_WASM_OP_KIND_I64_LT_S
                                        : BJVM_WASM_OP_KIND_I32_LT_S;

  expression cmp_greater = bjvm_wasm_binop(ctx->module, gt, left, right);
  expression cmp_less = bjvm_wasm_binop(ctx->module, lt, left, right);
  expression result = bjvm_wasm_select(
      ctx->module, cmp_greater, one,
      bjvm_wasm_select(ctx->module, cmp_less, negative_one, zero));
  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_INT, result);
}

expression wasm_lower_long_shift(compile_ctx *ctx,
                                 const bjvm_bytecode_insn *insn, int sd) {
  // Convert int to long
  expression right = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
  expression right_long =
      bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I64_EXTEND_U_I32, right);

  expression left = get_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG);
  bjvm_wasm_binary_op_kind op =
      insn->kind == bjvm_insn_lshl    ? BJVM_WASM_OP_KIND_I64_SHL
      : insn->kind == bjvm_insn_lushr ? BJVM_WASM_OP_KIND_I64_SHR_U
                                      : BJVM_WASM_OP_KIND_I64_SHR_S;
  expression result = bjvm_wasm_binop(ctx->module, op, left, right_long);

  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG, result);
}

expression wasm_lower_local_load_store(compile_ctx *ctx,
                                       const bjvm_bytecode_insn *insn, int sd) {
  bjvm_type_kind kind;
  bool is_store = false;

  switch (insn->kind) {
  case bjvm_insn_istore:
    is_store = true;
    [[fallthrough]];
  case bjvm_insn_iload:
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
  expression increment = bjvm_wasm_i32_const(ctx->module, insn->iinc.const_);
  expression result =
      bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_ADD, local, increment);
  expression iinc =
      set_local_value(ctx, insn->iinc.index, BJVM_TYPE_KIND_INT, result);
  return iinc;
}

expression wasm_lower_return(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                             int pc, int sd) {
  // Write the value into memory at "result"
  bjvm_type_kind kind = inspect_value_type(ctx, pc, sd - 1);
  expression value = get_stack_value(ctx, sd - 1, kind);
  expression store = bjvm_wasm_store(
      ctx->module, BJVM_WASM_OP_KIND_I32_STORE,
      bjvm_wasm_local_get(ctx->module, RESULT_PARAM, bjvm_wasm_int32()), value,
      0, 0);
  return store;
}

bjvm_wasm_expression *thread(compile_ctx *ctx) {
  return bjvm_wasm_local_get(ctx->module, THREAD_PARAM, bjvm_wasm_int32());
}

int wasm_compile_block(compile_ctx *ctx, const bjvm_basic_block *bb) {
  expression *results = nullptr;
  int result_count = 0, result_cap = 0;

#undef PUSH_EXPR
#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

#define CONVERSION_OP(from, to, op)                                            \
  {                                                                            \
    expression value = get_stack_value(ctx, sd - 1, from);                     \
    expression converted =                                                     \
        bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_##op, value);            \
    PUSH_EXPR = set_stack_value(ctx, sd - 1, to, converted);                   \
    break;                                                                     \
  }

#define BIN_OP_SAME_TYPE(kind, op)                                             \
  {                                                                            \
    expression right = get_stack_value(ctx, sd - 1, kind);                     \
    expression left = get_stack_value(ctx, sd - 2, kind);                      \
    expression result =                                                        \
        bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_##op, left, right);     \
    PUSH_EXPR = set_stack_value(ctx, sd - 2, kind, result);                    \
    break;                                                                     \
  }

  uint16_t *stack_depths = ctx->analysis->insn_index_to_stack_depth;

  int expr_i = 0;
  for (int i = 0, pc = bb->start_index; i < bb->insn_count;
       ++i, ++pc, ++expr_i) {
    const bjvm_bytecode_insn *insn = bb->start + i;

    int sd = stack_depths[pc];
    switch (insn->kind) {
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
    case bjvm_insn_laload:
    case bjvm_insn_aastore:
    case bjvm_insn_bastore:
    case bjvm_insn_castore:
    case bjvm_insn_dastore:
    case bjvm_insn_fastore:
    case bjvm_insn_sastore:
    case bjvm_insn_lastore:
    case bjvm_insn_iastore: {
      PUSH_EXPR = wasm_lower_array_load_store(ctx, insn, sd);
      break;
    }
    case bjvm_insn_aconst_null: {
      PUSH_EXPR = set_local_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE,
                                  bjvm_wasm_i32_const(ctx->module, 0));
      break;
    }
    case bjvm_insn_areturn:
      UNREACHABLE();
    case bjvm_insn_arraylength: {
      expression array = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
      expression nullcheck =
          bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I32_EQZ, array);
      expression raise_npe = wasm_raise_npe(ctx);
      expression length = wasm_get_array_length(ctx, array);
      expression store_length =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, length);
      PUSH_EXPR = bjvm_wasm_if_else(ctx->module, nullcheck, raise_npe,
                                    store_length, bjvm_wasm_int32());
      break;
    }
    case bjvm_insn_athrow: {
      expression exception =
          get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
      int store_offs = offsetof(bjvm_thread, current_exception);
      expression store =
          bjvm_wasm_store(ctx->module, BJVM_WASM_OP_KIND_I32_STORE, thread(ctx),
                          exception, 0, store_offs);
      expression execute = bjvm_wasm_if_else(
          ctx->module,
          bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I32_EQZ, exception),
          wasm_raise_npe(ctx), store, bjvm_wasm_void());
      PUSH_EXPR = execute;
      expression ret = bjvm_wasm_return(
          ctx->module,
          bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_EXC));
      PUSH_EXPR = ret;
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
    case bjvm_insn_dreturn:
    case bjvm_insn_freturn:
    case bjvm_insn_ireturn:
    case bjvm_insn_lreturn: {
      PUSH_EXPR = wasm_lower_return(ctx, insn, pc, sd);
      PUSH_EXPR = bjvm_wasm_return(
          ctx->module, bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_OK));
      break;
    }
    case bjvm_insn_dsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, F64_SUB);
    case bjvm_insn_dup:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd);
      break;
    case bjvm_insn_dup_x1:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2);
      break;
    case bjvm_insn_dup_x2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 2);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 3);
      break;
    case bjvm_insn_dup2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd);
      break;
    case bjvm_insn_dup2_x1:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd + 1, sd - 3);
      break;
    case bjvm_insn_dup2_x2:
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd + 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 3, sd - 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 4, sd - 2);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 3);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd + 1, sd - 4);
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
      expression max_char = bjvm_wasm_i32_const(ctx->module, 0xFFFF);
      expression result = bjvm_wasm_binop(
          ctx->module, BJVM_WASM_OP_KIND_I32_AND, val, max_char);
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
    case bjvm_insn_idiv:
      // TODO check for zero
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_DIV_S);
    case bjvm_insn_imul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_MUL);
    case bjvm_insn_ineg:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_SUB);
    case bjvm_insn_ior:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_OR);
    case bjvm_insn_irem:
      // TODO check for zero
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, I32_REM_S);
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
      PUSH_EXPR = wasm_lower_icmp_lcmp(ctx, insn, sd);
      break;
    }
    case bjvm_insn_ldiv:
      // TODO check for zero
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_DIV_S);
    case bjvm_insn_lmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_MUL);
    case bjvm_insn_lneg:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_LONG, I64_SUB);
    case bjvm_insn_lor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_OR);
    case bjvm_insn_lrem:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_REM_S);
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
      break;
    case bjvm_insn_pop:
    case bjvm_insn_pop2: {
      // nothing, just changes stack depth
      break;
    }
    case bjvm_insn_return: {
      // Just return OK
      PUSH_EXPR = bjvm_wasm_return(
          ctx->module, bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_OK));
      break;
    }
    case bjvm_insn_swap: {
      // Woo-hoo
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1);
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2);
      break;
    }
    case bjvm_insn_anewarray: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_anewarray(ctx, insn, sd);
      break;
    }
    case bjvm_insn_checkcast: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_checkcast(ctx, insn, sd);
      break;
    }
    case bjvm_insn_getfield: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_getfield(ctx, insn, sd, r);
      break;
    }
    case bjvm_insn_getstatic: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_getstatic(ctx, insn, sd);
      break;
    }
    case bjvm_insn_instanceof: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_instanceof(ctx, insn, sd);
      break;
    }
    case bjvm_insn_invokedynamic: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_invokedynamic(ctx, insn, sd);
      break;
    }
    case bjvm_insn_new: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_new(ctx, insn, sd);
      break;
    }
    case bjvm_insn_putfield: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_putfield(ctx, insn, sd);
      break;
    }
    case bjvm_insn_putstatic: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_putstatic(ctx, insn, sd);
      break;
    }
    case bjvm_insn_invokevirtual: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_invokevirtual(ctx, insn, sd);
      break;
    }
    case bjvm_insn_invokespecial: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_invokespecial(ctx, insn, sd);
      break;
    }
    case bjvm_insn_invokestatic: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_invokestatic(ctx, insn, sd);
      break;
    }
    case bjvm_insn_ldc:
    case bjvm_insn_ldc2_w: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_ldc(ctx, insn, sd);
      break;
    }
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
    case bjvm_insn_goto:
    case bjvm_insn_jsr:
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
    case bjvm_insn_ifnull:
    case bjvm_insn_tableswitch:
    case bjvm_insn_lookupswitch:
    case bjvm_insn_ret:
      goto done; // will be handled in the next pass
    case bjvm_insn_iconst: {
      expression value =
          bjvm_wasm_i32_const(ctx->module, (int)insn->integer_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_INT, value);
      break;
    }
    case bjvm_insn_dconst: {
      expression value = bjvm_wasm_f64_const(ctx->module, insn->d_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_DOUBLE, value);
      break;
    }
    case bjvm_insn_fconst: {
      expression value = bjvm_wasm_f32_const(ctx->module, insn->f_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_FLOAT, value);
      break;
    }
    case bjvm_insn_lconst: {
      expression value = bjvm_wasm_i64_const(ctx->module, insn->integer_imm);
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_LONG, value);
      break;
    }
    case bjvm_insn_iinc: {
      PUSH_EXPR = wasm_lower_iinc(ctx, insn, sd);

      break;
    }
    case bjvm_insn_invokeinterface: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_invokeinterface(ctx, insn, sd);
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
    case bjvm_insn_fcmpl:
    case bjvm_insn_fcmpg:
    case bjvm_insn_frem: // deprecated
    case bjvm_insn_drem: // deprecated
    case bjvm_insn_dcmpg:
    case bjvm_insn_dcmpl:
    case bjvm_insn_monitorenter:
    case bjvm_insn_monitorexit:
      goto unimplemented;
    }
  }

  if (0) {
  unimplemented:
    // Spill the stack
    PUSH_EXPR =
        spill_or_load_code(ctx, bb->start_index, false, BJVM_INTERP_RESULT_INT);
  }

done:

  PUSH_EXPR = nullptr; // make space for a final branch

  // Create a block with the expressions
  expression block =
      bjvm_wasm_block(ctx->module, results, result_count, bjvm_wasm_void());
  ctx->wasm_blocks[bb->my_index] = block;

  free(results);
  return 0;
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
  // block_i -> new order
  int *topo;
  // block_i that's a loop header -> range of blocks in the topo sort to wrap
  // in a loop header so that backward edges can enjoy themselves
  loop_range_t *loop_ranges;
  // Mapping from topological # to the blocks (also in topo #) which requested
  // a block begin here.
  bb_creations_t *creations;
  // Mapping from topological block index to a loop bjvm_wasm_expression such
  // that backward edges should continue to this loop block.
  bjvm_wasm_expression **loop_headers;
  // Mapping from topological block index to a block bjvm_wasm_expression such
  // that forward edges should break out of this block.
  bjvm_wasm_expression **block_ends;
  // Now implementation stuffs
  int current;
  int topo_i;
  int loop_depth;
  // For each block in topological indexing, the number of loops that contain
  // it.
  int *loop_depths;
  int *visited, *incoming_count;
  int blockc;
} topo_ctx;

void topo_dfs(bjvm_code_analysis *analy, topo_ctx *ctx) {
  // Visit edges from the current node to next nodes. If all incoming edges of a
  // successor are visited, then that successor should be pushed to the
  // result array, and is ripe for DFS enjoyment.
  int current = ctx->current;
  ctx->visited[current] = 1;
  ctx->loop_depths[ctx->topo_i] = ctx->loop_depth;
  assert(ctx->incoming_count[current] == 0);
  bjvm_basic_block *b = analy->blocks + ctx->current;
  if (b->is_loop_header) {
    int start = ctx->topo_i;
    // Make sure to process dominated nodes first! (i.e., have a full for-loop
    // pass where not a single dominated node is skipped)
    while (true) {
      bool all_dominated_visited = true;
      for (int i = 0; i < b->next_count; ++i) {
        int next = b->next[i];
        if (bjvm_query_dominance(b, analy->blocks + next) &&
            !ctx->visited[next]) {
          assert(ctx->incoming_count[next] != 0);
          if (ctx->incoming_count[next] == 1) {
            ctx->topo[ctx->topo_i++] = ctx->current = next;
            --ctx->incoming_count[next];
            ctx->loop_depth++;
            topo_dfs(analy, ctx);
            ctx->loop_depth--;
          } else {
            all_dominated_visited = false;
          }
        }
      }
      if (all_dominated_visited)
        break;
    }
    ctx->loop_ranges[current].start = start;
    ctx->loop_ranges[current].end = ctx->topo_i;
  }

  for (int i = 0; i < b->next_count; ++i) {
    int next = b->next[i];
    if (ctx->visited[next])
      continue;
    if (--ctx->incoming_count[next] == 0) {
      ctx->topo[ctx->topo_i++] = ctx->current = next;
      topo_dfs(analy, ctx);
    }
  }
}

void free_topo_ctx(topo_ctx ctx) {
  free(ctx.topo);
  free(ctx.loop_ranges);
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
  // For each block, find the earliest block (in the topological sort) which
  // branches to that block, which is NOT the block itself or its immediate
  // predecessor in the sort.
  for (int i = 0; i < analy->block_count; ++i) {
    bjvm_basic_block *b = analy->blocks + i;
    int earliest = -1;
    for (int j = 0; j < b->prev_count; ++j) {
      int prev = b->prev[j];
      if (ctx->topo[prev] < i - 1)
        earliest = ctx->topo[prev];
    }
    // Then walk backwards until the stack depth is the same. This is where
    // we'll put the block.
    if (earliest != -1) {
      while (ctx->loop_depths[earliest] != ctx->loop_depths[ctx->topo[i]]) {
        earliest--;
        assert(earliest >= 0);
      }
    }
    bb_creations_t *creations = ctx->creations + earliest;
    *VECTOR_PUSH(creations->requested, creations->count, creations->cap) = i;
  }
}

topo_ctx restricted_topo_sort(bjvm_code_analysis *analy) {
  topo_ctx ctx;
  ctx.topo = calloc(analy->block_count, sizeof(int));
  ctx.visited = calloc(analy->block_count, sizeof(int));
  ctx.incoming_count = calloc(analy->block_count, sizeof(int));
  ctx.loop_depths = calloc(analy->block_count, sizeof(int));
  ctx.creations = calloc(analy->block_count, sizeof(bb_creations_t));
  ctx.block_ends = calloc(analy->block_count, sizeof(bjvm_wasm_expression *));
  ctx.loop_headers = calloc(analy->block_count, sizeof(bjvm_wasm_expression *));
  ctx.blockc = analy->block_count;
  ctx.current = ctx.topo_i = ctx.loop_depth = 0;
  ctx.loop_ranges = calloc(analy->block_count, sizeof(loop_range_t));
  for (int i = 0; i < analy->block_count; ++i) {
    bjvm_basic_block *b = analy->blocks + i;
    for (int j = 0; j < b->next_count; ++j)
      ctx.incoming_count[b->next[j]] += !b->is_backedge[j];
  }
  topo_dfs(analy, &ctx);
  find_block_insertion_points(analy, &ctx);
  return ctx;
}

static bjvm_wasm_expression *recurse(bjvm_code_analysis *analy,
                                     topo_ctx *topo) {
  // This function returns blocks and loop expressions. When we hit a bb
  // which has wasm block and/or loop headers, we recurse into building those.
}

bjvm_wasm_jit_compiled_method *
bjvm_wasm_jit_compile(bjvm_thread *thread, const bjvm_cp_method *method) {
  // Resulting signature and (roughly) behavior is same as
  // bjvm_bytecode_interpret:
  // (bjvm_thread *thread, bjvm_stack_frame *frame, bjvm_stack_value *result)
  // -> bjvm_interpreter_result_t
  // The key difference is that the frame MUST be the topmost frame, and this
  // must be the first invocation, whereas in the interpreter, we can interpret
  // things after an interrupt.
  int ok = 0;
  assert(method->code);
  bjvm_scan_basic_blocks(method->code, method->code_analysis);
  bjvm_compute_dominator_tree(method->code_analysis);
  if (bjvm_attempt_reduce_cfg(method->code_analysis))
    // CFG is not reducible, so we can't compile it (yet! -- low prio)
    goto error;

  bjvm_code_analysis *analy = method->code_analysis;

  compile_ctx ctx = {nullptr};
  ctx.wasm_blocks = calloc(analy->block_count, sizeof(expression));
  ctx.module = bjvm_wasm_module_create();
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
  // can continue in the loops. We will wrap these bbs in a (loop) wasm block.
  // Then for forward edges, we let consecutive blocks fall through (i.e.,
  // place them consecutively), and otherwise place a (block) scope ending
  // just before the target block, so that mfs who try to target it can enjoy
  // themselves.
  topo_ctx topo = restricted_topo_sort(analy);
  topo.topo_i = 0;
  expression final = recurse(analy, &topo);

  if (final == nullptr) {
    goto error;
  }

  add_runtime_imports(&ctx);

  *VECTOR_PUSH(ctx.wvars, ctx.wvars_count, ctx.wvars_cap) = bjvm_wasm_int32();
  ok = 1;

error:
  bjvm_wasm_jit_compiled_method *compiled_method = nullptr;
  if (ok) {
#ifdef EMSCRIPTEN
    void *function_ptr = EM_ASM_PTR(
        {
          try {
            var module = new WebAssembly.Module(HEAPU8.subarray($1, $2));
            var instance = new WebAssembly.Instance(module, {
              env : {
                memory : wasmMemory,
                raise_npe : wasmExports["wasm_runtime_raise_npe"],
                raise_oob : wasmExports["wasm_runtime_raise_array_index_oob"],
                new_array : wasmExports["wasm_runtime_new_array"],
                new_object : wasmExports["wasm_runtime_new_object"]
              }
            });
            return addFunction(instance.exports.run, 'iiii');
          } catch (e) {
            console.log(e);
            return 0;
          }
        },
        "iiii", (uintptr_t)result.binary,
        (uintptr_t)(result.binary + result.binaryBytes));
#else
    void *function_ptr = nullptr;
#endif

    if (function_ptr != nullptr) {
      compiled_method = calloc(1, sizeof(bjvm_wasm_jit_compiled_method));

      printf("%p\n", function_ptr);

      compiled_method->ready = true;
      compiled_method->fn = function_ptr;
    }
  }

  free_topo_ctx(topo);

  return compiled_method;
}

void free_wasm_compiled_method(void *p) {
  if (!p)
    return;
  bjvm_wasm_jit_compiled_method *compiled_method = p;
  free(compiled_method);
}