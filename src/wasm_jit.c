// Java bytecode to WebAssembly translator

#include "wasm_jit.h"
#include "analysis.h"
#include "arrays.h"
#include "objects.h"
#include "wasm_utils.h"

#include <limits.h>

#define expression bjvm_wasm_expression *

enum { THREAD_PARAM, FRAME_PARAM, RESULT_PARAM };

typedef struct {
  bjvm_wasm_function *new_object;
  bjvm_wasm_function *new_array;
  bjvm_wasm_function *raise_npe;
  bjvm_wasm_function *raise_oob;
  bjvm_wasm_function *push_frame;
  bjvm_wasm_function *invokestatic;
  bjvm_wasm_function *invokenonstatic;

  uint32_t vm_local;
  uint32_t heap_local;
  uint32_t heap_used_local;
} runtime_helpers;

typedef struct {
  // The WASM module we will emit as bytes
  bjvm_wasm_module *module;
  // The basic blocks we are building
  expression *wasm_blocks;

  // The method and (for convenie) its code and analysis
  bjvm_cp_method *method;
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

  bjvm_wasm_value_type *wvars;
  int wvars_count;
  int wvars_cap;

  runtime_helpers runtime;
} compile_ctx;

#define WASM_TYPES_COUNT 4

bjvm_wasm_type bjvm_jvm_type_to_wasm(bjvm_type_kind kind) {
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
  int result = ty.val - BJVM_WASM_TYPE_KIND_FLOAT64;
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

int add_local(compile_ctx *ctx, bjvm_wasm_type val) {
  *VECTOR_PUSH(ctx->wvars, ctx->wvars_count, ctx->wvars_cap) = val.val;
  return ctx->next_local++;
}

int jvm_stack_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  int i = index * WASM_TYPES_COUNT + local_kind(bjvm_jvm_type_to_wasm(kind));
  int *l = &ctx->val_to_local_map[i];
  if (*l == -1) {
    *l = add_local(ctx, bjvm_jvm_type_to_wasm(kind));
  }
  return *l;
}

int jvm_local_to_wasm_local(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return jvm_stack_to_wasm_local(ctx, ctx->code->max_stack + index, kind);
}

expression get_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return bjvm_wasm_local_get(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind),
                             bjvm_jvm_type_to_wasm(kind));
}

expression set_stack_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return bjvm_wasm_local_set(ctx->module,
                             jvm_stack_to_wasm_local(ctx, index, kind), value);
}

expression get_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return bjvm_wasm_local_get(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind),
                             bjvm_jvm_type_to_wasm(kind));
}

expression set_local_value(compile_ctx *ctx, int index, bjvm_type_kind kind,
                           expression value) {
  return bjvm_wasm_local_set(ctx->module,
                             jvm_local_to_wasm_local(ctx, index, kind), value);
}

// Generate spill code that writes to stack->values at the appropriate location,
// or load parameters from the stack at the function beginning.
expression spill_or_load_code(compile_ctx *ctx, int pc, bool do_load,
                              int return_value, int start, int end) {
  int values_start = offsetof(bjvm_plain_frame, values);
  int value_size = sizeof(bjvm_stack_value);

  bjvm_compressed_bitset refs[5];
  for (int i = 0; i < 5; ++i)
    refs[i] = ctx->analysis->insn_index_to_kinds[i][pc];

  expression *result = nullptr;
  int result_count = 0, result_cap = 0;

  if (end == -1)
    end = ctx->code->max_locals + ctx->code->max_stack;

  for (int slot = start; slot < end; ++slot) {
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
    // Set thread->program_counter to pc
    expression frame =
        bjvm_wasm_local_get(ctx->module, FRAME_PARAM, bjvm_wasm_int32());
    expression pc_const = bjvm_wasm_i32_const(ctx->module, pc);
    expression store = bjvm_wasm_store(
        ctx->module, BJVM_WASM_OP_KIND_I32_STORE16, frame, pc_const, 0,
        offsetof(bjvm_plain_frame, program_counter));
    *VECTOR_PUSH(result, result_count, result_cap) = store;
    if (return_value != -1) {
      *VECTOR_PUSH(result, result_count, result_cap) = bjvm_wasm_return(
          ctx->module, bjvm_wasm_i32_const(ctx->module, return_value));
    }
  }

  expression block = bjvm_wasm_block(ctx->module, result, result_count,
                                     bjvm_wasm_void(), false);
  free(result);
  return block;
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
bjvm_stack_frame *wasm_runtime_push_frame(bjvm_thread *thread,
                                          bjvm_bytecode_insn *insn) {
  return nullptr;
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
  ctx->runtime.new_object = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_new_object, "ii", "i");
  ctx->runtime.new_array = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_make_object_array, "iii", "i");
  ctx->runtime.raise_npe = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_npe, "i", "i");
  ctx->runtime.raise_oob = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_raise_array_index_oob, "iii", "i");
  ctx->runtime.invokestatic = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_invokestatic, "iii", "i");
  ctx->runtime.invokenonstatic = bjvm_wasm_import_runtime_function(
      ctx->module, wasm_runtime_invokenonstatic, "iii", "i");

  ctx->runtime.heap_local = add_local(ctx, bjvm_wasm_int32());
  ctx->runtime.vm_local = add_local(ctx, bjvm_wasm_int32());
  ctx->runtime.heap_used_local = add_local(ctx, bjvm_wasm_int32());
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
    return BJVM_TYPE_KIND_VOID;
  }
}

bjvm_wasm_load_op_kind get_tk_load_op(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return BJVM_WASM_OP_KIND_I32_LOAD8_U;
  case BJVM_TYPE_KIND_CHAR:
    return BJVM_WASM_OP_KIND_I32_LOAD16_U;
  case BJVM_TYPE_KIND_FLOAT:
    return BJVM_WASM_OP_KIND_F32_LOAD;
  case BJVM_TYPE_KIND_DOUBLE:
    return BJVM_WASM_OP_KIND_F64_LOAD;
  case BJVM_TYPE_KIND_BYTE:
    return BJVM_WASM_OP_KIND_I32_LOAD8_S;
  case BJVM_TYPE_KIND_SHORT:
    return BJVM_WASM_OP_KIND_I32_LOAD16_S;
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return BJVM_WASM_OP_KIND_I32_LOAD;
  case BJVM_TYPE_KIND_LONG:
    return BJVM_WASM_OP_KIND_I64_LOAD;
  default:
    UNREACHABLE();
  }
}

static bjvm_wasm_load_op_kind get_load_op(const bjvm_field_descriptor *field) {
  bjvm_type_kind kind =
      field->dimensions ? BJVM_TYPE_KIND_REFERENCE : field->base_kind;
  return get_tk_load_op(kind);
}

bjvm_wasm_store_op_kind get_tk_store_op(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    return BJVM_WASM_OP_KIND_I32_STORE8;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    return BJVM_WASM_OP_KIND_I32_STORE16;
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return BJVM_WASM_OP_KIND_I32_STORE;
  case BJVM_TYPE_KIND_FLOAT:
    return BJVM_WASM_OP_KIND_F32_STORE;
  case BJVM_TYPE_KIND_DOUBLE:
    return BJVM_WASM_OP_KIND_F64_STORE;
  case BJVM_TYPE_KIND_LONG:
    return BJVM_WASM_OP_KIND_I64_STORE;
  default:
    UNREACHABLE();
  }
}

static bjvm_wasm_store_op_kind
get_store_op(const bjvm_field_descriptor *field) {
  bjvm_type_kind kind =
      field->dimensions ? BJVM_TYPE_KIND_REFERENCE : field->base_kind;
  return get_tk_store_op(kind);
}

expression wasm_move_value(compile_ctx *ctx, int pc, int from, int to,
                           bjvm_type_kind kind) {
  return bjvm_wasm_local_set(
      ctx->module, jvm_stack_to_wasm_local(ctx, to, kind),
      bjvm_wasm_local_get(ctx->module, jvm_stack_to_wasm_local(ctx, from, kind),
                          bjvm_jvm_type_to_wasm(kind)));
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
  bjvm_wasm_type dtype = bjvm_jvm_type_to_wasm(component);

  if (is_store) {
    // (<dtype>.store offset=kArrayDataOffset <addr>)
    expression value = get_stack_value(ctx, sd - 1, component);
    execute = bjvm_wasm_store(ctx->module, get_tk_store_op(component), addr,
                              value, 0, kArrayDataOffset);
  } else {
    expression data = bjvm_wasm_load(ctx->module, get_tk_load_op(component),
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

expression wasm_lower_lcmp(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                           int sd) {
  bjvm_type_kind kind = BJVM_TYPE_KIND_LONG;

  expression zero = bjvm_wasm_i32_const(ctx->module, 0);
  expression one = bjvm_wasm_i32_const(ctx->module, 1);
  expression negative_one = bjvm_wasm_i32_const(ctx->module, -1);

  expression right = get_stack_value(ctx, sd - 1, kind);
  expression left = get_stack_value(ctx, sd - 2, kind);

  bjvm_wasm_binary_op_kind gt = BJVM_WASM_OP_KIND_I64_GT_S,
                           lt = BJVM_WASM_OP_KIND_I64_LT_S;

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
      ctx->module, get_tk_store_op(kind),
      bjvm_wasm_local_get(ctx->module, RESULT_PARAM, bjvm_wasm_int32()), value,
      0, 0);
  // Also write the value into the frame preceding the current frame
  return store;
}

bjvm_wasm_expression *thread(compile_ctx *ctx) {
  return bjvm_wasm_local_get(ctx->module, THREAD_PARAM, bjvm_wasm_int32());
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
  // Mapping from topological block index to a loop bjvm_wasm_expression such
  // that backward edges should continue to this loop block.
  bjvm_wasm_expression **loop_headers;
  // Mapping from topological block index to a block bjvm_wasm_expression such
  // that forward edges should break out of this block.
  bjvm_wasm_expression **block_ends;
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

static bjvm_wasm_expression *frame_ptr(compile_ctx *ctx) {
  return bjvm_wasm_local_get(ctx->module, ctx->frame_local, bjvm_wasm_int32());
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

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  frame->compiled.is_native = 2;
  frame->compiled.references_count = references_count;
  frame->compiled.program_counter = 0;
  frame->compiled.method = method;

  memset(frame->compiled.references, 0, references_count * sizeof(bjvm_stack_value));

  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;
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
static bjvm_wasm_expression *frame_setup(compile_ctx *ctx) {
  int deopt_size = frame_size(ctx->code);
  // References count is the maximum # of references live at any given point
  int references_count = frame_references_count(ctx);

  return nullptr;
}

// Construct a de-optimization code path, replacing the frame on the stack with
// an interpreter frame. (Eventually we may generate multiple frames in the
// case of inlining.) The path will return BJVM_INTERP_RESULT_INT.
static bjvm_wasm_expression *deopt(compile_ctx *ctx, int pc, int frame_state) {
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

  bjvm_wasm_expression **stmts = nullptr;
  for (int i = 0; i < 2; ++i) {
    arrput(stmts, bjvm_wasm_store(
                      ctx->module, BJVM_WASM_OP_KIND_I64_STORE, frame_ptr(ctx),
                      bjvm_wasm_i64_const(ctx->module, chunks[i]), 0, i * sizeof(uint64_t)));
  }

  // Now spill all live values
  for (int stack_i = 0; stack_i < dummy.values_count; ++stack_i) {
    bjvm_type_kind kind = inspect_value_type(ctx, pc, stack_i);
    if (kind != BJVM_TYPE_KIND_VOID) {
      bjvm_wasm_store_op_kind store_op = get_tk_store_op(kind);
      int wasm_local = jvm_stack_to_wasm_local(ctx, stack_i, kind);
      expression value = bjvm_wasm_local_get(ctx->module, wasm_local,
                                             bjvm_jvm_type_to_wasm(kind));
      int offset = offsetof(bjvm_plain_frame, values) +
        stack_i * sizeof(bjvm_stack_value);
      arrput(stmts, bjvm_wasm_store(ctx->module, store_op, frame_ptr(ctx),
                                    value, 0, offset));
    }
  }

  // Now return BJVM_INTERP_RESULT_INT
  arrput(stmts, bjvm_wasm_return(ctx->module, bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_INT)));

  // Pack things into a block and return
  bjvm_wasm_expression *block = bjvm_wasm_block(ctx->module, stmts, arrlen(stmts), bjvm_wasm_void(), false);
  arrfree(stmts);
  return block;
}

static bjvm_wasm_expression *exception_raised(bjvm_wasm_module *module) {
  return bjvm_wasm_return(module,
                          bjvm_wasm_i32_const(module, BJVM_INTERP_RESULT_EXC));
}

static bjvm_wasm_expression *interrupt(compile_ctx *ctx, int pc) {
  return spill_or_load_code(ctx, pc, false, BJVM_INTERP_RESULT_INT, 0, -1);
}

bjvm_wasm_expression *wasm_lower_anewarray(compile_ctx *ctx,
                                           const bjvm_bytecode_insn *insn,
                                           int pc, int sd) {
  bjvm_classdesc *classdesc = insn->cp->class_info.classdesc;
  if (!classdesc)
    return nullptr;

  expression call_expr = bjvm_wasm_call(
      ctx->module, ctx->runtime.new_array,
      (expression[]){
          bjvm_wasm_local_get(ctx->module, THREAD_PARAM, bjvm_wasm_int32()),
          get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT),
          bjvm_wasm_i32_const(ctx->module, (int)classdesc),
      },
      3);
  expression store_to_stack =
      set_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE, call_expr);
  expression obj_is_null =
      bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I32_EQZ,
                     get_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE));
  // Null is returned if a GC is required or if NegativeArraySizeException is
  // thrown
  expression null_check = bjvm_wasm_if_else(
      ctx->module, obj_is_null, interrupt(ctx, pc), nullptr, bjvm_wasm_int32());

  return bjvm_wasm_block(ctx->module,
                         (expression[]){store_to_stack, null_check}, 2,
                         bjvm_wasm_void(), false);
}

// Lower getfield and putfield instructions, which set an instance field on an
// object.
bjvm_wasm_expression *
wasm_lower_getfield_putfield(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                             int sd) {
  // Get the object and value
  bjvm_cp_field_info f = insn->cp->field;
  if (!f.field) { // Not resolved yet
    return nullptr;
  }
  bool is_putfield = insn->kind == bjvm_insn_putfield;
  bjvm_field_descriptor *d = insn->cp->field.parsed_descriptor;
  bjvm_type_kind field_kind = field_to_kind(d);
  expression value_to_put =
      is_putfield ? get_stack_value(ctx, sd - 1, field_kind) : nullptr;
  expression object_to_modify =
      get_stack_value(ctx, sd - 1 - is_putfield, BJVM_TYPE_KIND_REFERENCE);
  expression null_check =
      bjvm_wasm_unop(ctx->module, BJVM_WASM_OP_KIND_I32_EQZ, object_to_modify);
  expression raise_npe = wasm_raise_npe(ctx);
  expression execute;
  if (is_putfield) {
    execute = bjvm_wasm_store(ctx->module, get_store_op(d), object_to_modify,
                              value_to_put, 0, f.field->byte_offset);
  } else {
    execute = bjvm_wasm_load(ctx->module, get_load_op(d), object_to_modify, 0,
                             f.field->byte_offset);
    execute = set_stack_value(ctx, sd - 1, field_kind, execute);
  }
  bjvm_wasm_type result_type =
      is_putfield ? bjvm_wasm_void() : bjvm_jvm_type_to_wasm(field_kind);
  return bjvm_wasm_if_else(ctx->module, null_check, raise_npe, execute,
                           result_type);
}

bjvm_wasm_expression *
wasm_lower_getstatic_putstatic(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                               int sd) {
  bjvm_cp_field_info f = insn->cp->field;
  if (!f.field) {
    return nullptr;
  }
  bool is_putstatic = insn->kind == bjvm_insn_putstatic;
  bjvm_field_descriptor *d = insn->cp->field.parsed_descriptor;
  bjvm_type_kind field_kind = field_to_kind(d);
  expression value_to_put =
      is_putstatic ? get_stack_value(ctx, sd - 1, field_kind) : nullptr;
  expression execute;
  void *field_addr = f.field->my_class->static_fields + f.field->byte_offset;
  if (is_putstatic) {
    execute = bjvm_wasm_store(ctx->module, get_store_op(d),
                              bjvm_wasm_i32_const(ctx->module, (int)field_addr),
                              value_to_put, 0, 0);
  } else {
    execute =
        bjvm_wasm_load(ctx->module, get_load_op(d),
                       bjvm_wasm_i32_const(ctx->module, (int)field_addr), 0, 0);
    execute = set_stack_value(ctx, sd, field_kind, execute);
  }
  return execute;
}

bjvm_wasm_expression *wasm_lower_ldc(compile_ctx *ctx,
                                     const bjvm_bytecode_insn *insn, int sd) {
  switch (insn->cp->kind) {
  case BJVM_CP_KIND_INTEGER:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_INT,
        bjvm_wasm_i32_const(ctx->module, (int)insn->cp->integral.value));
  case BJVM_CP_KIND_FLOAT:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_FLOAT,
        bjvm_wasm_f32_const(ctx->module, (float)insn->cp->floating.value));
  case BJVM_CP_KIND_DOUBLE:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_DOUBLE,
        bjvm_wasm_f64_const(ctx->module, insn->cp->floating.value));
  case BJVM_CP_KIND_LONG:
    return set_stack_value(
        ctx, sd, BJVM_TYPE_KIND_LONG,
        bjvm_wasm_i64_const(ctx->module, insn->cp->integral.value));
  default:
    return nullptr;
  }
}

expression wasm_lower_invoke(compile_ctx *ctx, const bjvm_bytecode_insn *insn,
                             int pc, int sd) {
  // For now, we do everything in the runtime. Eventually we'll do something
  // more clever.
  int args = insn->cp->methodref.descriptor->args_count +
             (insn->kind != bjvm_insn_invokestatic);

  // Spill the top n variables of the stack
  expression spill = spill_or_load_code(ctx, pc, false, -1, 0, sd);
  bjvm_wasm_function *target = insn->kind == bjvm_insn_invokestatic
                                   ? ctx->runtime.invokestatic
                                   : ctx->runtime.invokenonstatic;
  expression call = bjvm_wasm_call(
      ctx->module, target,
      (expression[]){
          bjvm_wasm_local_get(ctx->module, THREAD_PARAM, bjvm_wasm_int32()),
          bjvm_wasm_local_get(ctx->module, FRAME_PARAM, bjvm_wasm_int32()),
          bjvm_wasm_i32_const(ctx->module, (int)insn),
      },
      3);

  // If an interrupt or exception was raised, interrupt
  expression interrupt_check = bjvm_wasm_if_else(
      ctx->module,
      bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_NE, call,
                      bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_OK)),
      interrupt(ctx, pc), nullptr, bjvm_wasm_int32());

  // Now load in the return value
  bool is_void = insn->cp->methodref.descriptor->return_type.base_kind ==
                 BJVM_TYPE_KIND_VOID;
  expression load;
  if (!is_void)
    load = spill_or_load_code(ctx, pc + 1, true, -1, sd - args, sd - args + 1);
  expression sequence = bjvm_wasm_block(ctx->module,
                                        (expression[]){
                                            spill,
                                            interrupt_check,
                                            load,
                                        },
                                        2 + !is_void, bjvm_wasm_void(), false);

  return sequence;
}

const int MAX_PC_TO_EMIT = 256;

bjvm_wasm_expression *wasm_lower_new(compile_ctx *ctx,
                                     const bjvm_bytecode_insn *insn, int sd) {
  bjvm_classdesc *class = insn->cp->class_info.classdesc;
  if (!class) {
    return nullptr;
  }
  int reserve_bytes = insn->cp->class_info.classdesc->instance_bytes;
  // round up to 8 bytes
  reserve_bytes = (reserve_bytes + 7) & ~7;

  expression load_vm_ptr = bjvm_wasm_load(
      ctx->module, BJVM_WASM_OP_KIND_I32_LOAD,
      bjvm_wasm_local_get(ctx->module, THREAD_PARAM, bjvm_wasm_int32()), 0,
      offsetof(bjvm_thread, vm));
  load_vm_ptr =
      bjvm_wasm_local_set(ctx->module, ctx->runtime.vm_local, load_vm_ptr);
  expression vm_ptr = bjvm_wasm_local_get(ctx->module, ctx->runtime.vm_local,
                                          bjvm_wasm_int32());

  expression heap_ptr = bjvm_wasm_load(ctx->module, BJVM_WASM_OP_KIND_I32_LOAD,
                                       vm_ptr, 0, offsetof(bjvm_vm, heap));

  expression load_heap_used =
      bjvm_wasm_load(ctx->module, BJVM_WASM_OP_KIND_I32_LOAD, vm_ptr, 0,
                     offsetof(bjvm_vm, heap_used));
  load_heap_used = bjvm_wasm_local_set(
      ctx->module, ctx->runtime.heap_used_local, load_heap_used);
  expression heap_used = bjvm_wasm_local_get(
      ctx->module, ctx->runtime.heap_used_local, bjvm_wasm_int32());

  expression new_heap_used =
      bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_ADD, heap_used,
                      bjvm_wasm_i32_const(ctx->module, reserve_bytes));
  expression store_heap_used =
      bjvm_wasm_store(ctx->module, BJVM_WASM_OP_KIND_I32_STORE, vm_ptr,
                      new_heap_used, 0, offsetof(bjvm_vm, heap_used));

  expression object = bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_ADD,
                                      heap_ptr, heap_used);
  expression store_object =
      set_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE, object);

  expression store_classdesc =
      bjvm_wasm_store(ctx->module, BJVM_WASM_OP_KIND_I32_STORE,
                      get_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE),
                      bjvm_wasm_i32_const(ctx->module, (int)class), 0,
                      offsetof(bjvm_obj_header, descriptor));

  expression sequence = bjvm_wasm_block(
      ctx->module,
      (expression[]){load_vm_ptr, load_heap_used, store_heap_used, store_object,
                     store_classdesc},
      5, bjvm_wasm_void(), false);
  return sequence;
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
  bool outgoing_edges_processed = false;

  int expr_i = 0, i = 0, pc = bb->start_index;
  for (; i < bb->insn_count; ++i, ++pc, ++expr_i) {
    if (debug && pc > MAX_PC_TO_EMIT) {
      printf("Skipping emission of instruction %s\n",
             bjvm_insn_code_name(bb->start[i].kind));
      goto unimplemented;
    }

    const bjvm_bytecode_insn *insn = bb->start + i;
    int sd = stack_depths[pc];

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
    case bjvm_insn_areturn: {
      PUSH_EXPR = wasm_lower_return(ctx, insn, pc, sd);
      PUSH_EXPR = bjvm_wasm_return(
          ctx->module, bjvm_wasm_i32_const(ctx->module, BJVM_INTERP_RESULT_OK));
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
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE,
                                  bjvm_wasm_i32_const(ctx->module, 0));
      break;
    }
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
      // Spill all locals
      PUSH_EXPR = spill_or_load_code(ctx, pc, false, BJVM_INTERP_RESULT_EXC,
                                     ctx->code->max_stack, -1);
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
    case bjvm_insn_ineg: {
      // no unary negation in wasm
      expression zero = bjvm_wasm_i32_const(ctx->module, 0);
      expression value = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
      expression result =
          bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I32_SUB, zero, value);
      expression store =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, result);
      PUSH_EXPR = store;
      break;
    }
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
      PUSH_EXPR = wasm_lower_lcmp(ctx, insn, sd);
      break;
    }
    case bjvm_insn_ldiv:
      // TODO check for zero
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_DIV_S);
    case bjvm_insn_lmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, I64_MUL);
    case bjvm_insn_lneg: {
      // no unary negation in wasm
      expression zero = bjvm_wasm_i64_const(ctx->module, 0);
      expression value = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_LONG);
      expression result =
          bjvm_wasm_binop(ctx->module, BJVM_WASM_OP_KIND_I64_SUB, zero, value);
      expression store =
          set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_LONG, result);
      PUSH_EXPR = store;
      break;
    }
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
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 1, sd,
                                  inspect_value_type(ctx, pc, sd - 1));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd - 2, sd - 1,
                                  inspect_value_type(ctx, pc, sd - 2));
      PUSH_EXPR = wasm_move_value(ctx, pc, sd, sd - 2,
                                  inspect_value_type(ctx, pc, sd - 1));
      break;
    }
#if 0
    case bjvm_insn_anewarray: {
      PUSH_EXPR = wasm_lower_anewarray(ctx, insn, sd);
      break;
    }
#endif
    case bjvm_insn_checkcast: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_checkcast(ctx, insn, sd);
      break;
    }
    case bjvm_insn_putfield:
    case bjvm_insn_getfield: {
      expression result = wasm_lower_getfield_putfield(ctx, insn, sd);
      if (!result)
        goto unimplemented;
      PUSH_EXPR = result;
      break;
    }
    case bjvm_insn_instanceof: {
      goto unimplemented;
      // PUSH_EXPR = wasm_lower_instanceof(ctx, insn, sd);
      break;
    }
    case bjvm_insn_new: {
      expression result = wasm_lower_new(ctx, insn, sd);
      if (!result)
        goto unimplemented;
      PUSH_EXPR = result;
      break;
    }
    case bjvm_insn_putstatic:
    case bjvm_insn_getstatic: {
      expression result = wasm_lower_getstatic_putstatic(ctx, insn, sd);
      if (!result)
        goto unimplemented;
      PUSH_EXPR = result;
      break;
    }
    case bjvm_insn_goto: {
      int next = topo->block_to_topo[bb->next[0]];
      if (next != topo->topo_i + 1) {
        expression be = next < topo->topo_i + 1 ? topo->loop_headers[next]
                                                : topo->block_ends[next];
        assert(be);
        PUSH_EXPR = bjvm_wasm_br(ctx->module, nullptr, be);
      }
      outgoing_edges_processed = true;
      break;
    }
    case bjvm_insn_invokespecial:
    case bjvm_insn_invokestatic:
    case bjvm_insn_invokevirtual:
    case bjvm_insn_invokeinterface:
    case bjvm_insn_invokedynamic: {
      goto unimplemented;
    }
    case bjvm_insn_invokevtable_monomorphic:
    case bjvm_insn_invokevtable_polymorphic: {
      goto unimplemented;
    }
    case bjvm_insn_ldc:
    case bjvm_insn_ldc2_w: {
      expression result = wasm_lower_ldc(ctx, insn, sd);
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
          BJVM_WASM_OP_KIND_I32_EQ,   // if_acmpeq
          BJVM_WASM_OP_KIND_I32_NE,   // if_acmpne
          BJVM_WASM_OP_KIND_I32_EQ,   // if_icmpeq
          BJVM_WASM_OP_KIND_I32_NE,   // if_icmpne
          BJVM_WASM_OP_KIND_I32_LT_S, // if_icmplt
          BJVM_WASM_OP_KIND_I32_GE_S, // if_icmpge
          BJVM_WASM_OP_KIND_I32_GT_S, // if_icmpgt
          BJVM_WASM_OP_KIND_I32_LE_S, // if_icmple
          BJVM_WASM_OP_KIND_I32_EQ,   // ifeq
          BJVM_WASM_OP_KIND_I32_NE,   // ifne
          BJVM_WASM_OP_KIND_I32_LT_S, // iflt
          BJVM_WASM_OP_KIND_I32_GE_S, // ifge
          BJVM_WASM_OP_KIND_I32_GT_S, // ifgt
          BJVM_WASM_OP_KIND_I32_LE_S, // ifle
          BJVM_WASM_OP_KIND_I32_NE,   // ifnonnull
          BJVM_WASM_OP_KIND_I32_EQ    // ifnull
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
                             : bjvm_wasm_i32_const(ctx->module, 0);
        expression lhs =
            get_stack_value(ctx, sd - 1 - is_binary, BJVM_TYPE_KIND_INT);
        bjvm_wasm_binary_op_kind op = op_kinds[kind - bjvm_insn_if_acmpeq];
        expression cond = bjvm_wasm_binop(ctx->module, op, lhs, rhs);
        expression br_if = bjvm_wasm_br(ctx->module, cond, taken_block);
        PUSH_EXPR = br_if;
      }
      if (not_taken != topo->topo_i + 1) {
        // Emit the br for the not taken branch
        expression not_taken_block = not_taken_is_backedge
                                         ? topo->loop_headers[not_taken]
                                         : topo->block_ends[not_taken];
        PUSH_EXPR = bjvm_wasm_br(ctx->module, nullptr, not_taken_block);
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
    fprintf(stderr, "Rejecting JIT because of unimplemented instruction %s\n",
            bjvm_insn_code_name(bb->start[i].kind));
    PUSH_EXPR =
        spill_or_load_code(ctx, pc, false, BJVM_INTERP_RESULT_INT, 0, -1);
  } else if (!outgoing_edges_processed && bb->next_count) {
    int next = topo->block_to_topo[bb->next[0]];
    if (next != topo->topo_i + 1) {
      expression be = next < topo->topo_i + 1 ? topo->loop_headers[next]
                                              : topo->block_ends[next];
      assert(be);
      PUSH_EXPR = bjvm_wasm_br(ctx->module, nullptr, be);
    }
  }

done:

  // Create a block with the expressions
  expression block = bjvm_wasm_block(ctx->module, results, result_count,
                                     bjvm_wasm_void(), false);
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
  ctx.block_ends = calloc(analy->block_count, sizeof(bjvm_wasm_expression *));
  ctx.loop_headers = calloc(analy->block_count, sizeof(bjvm_wasm_expression *));
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

bjvm_wasm_instantiation_result *
bjvm_wasm_jit_compile(bjvm_thread *thread, const bjvm_cp_method *method,
                      bool debug) {
  bjvm_wasm_instantiation_result *wasm = nullptr;
  printf(
      "Requesting compile for method %.*s on class %.*s with signature %.*s\n",
      fmt_slice(method->name), fmt_slice(method->my_class->name),
      fmt_slice(method->unparsed_descriptor));

  // Resulting signature and (roughly) behavior is same as
  // bjvm_bytecode_interpret:
  // (bjvm_thread *thread, bjvm_stack_frame *frame, bjvm_stack_value *result)
  // -> bjvm_interpreter_result_t
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
  // can continue in the loops. We will wrap these bbs in a "(loop)" wasm block.
  // Then for forward edges, we let consecutive blocks fall through, and
  // otherwise place a (block) scope ending just before the target block,
  // so that bbs who try to target it can break to it.
  topo_ctx topo = make_topo_sort_ctx(analy);

  add_runtime_imports(&ctx);

  inchoate_expression *expr_stack = nullptr;
  int stack_count = 0, stack_cap = 0;
  // Push an initial boi
  *VECTOR_PUSH(expr_stack, stack_count, stack_cap) = (inchoate_expression){
      bjvm_wasm_block(ctx.module, nullptr, 0, bjvm_wasm_void(), false), 0,
      analy->block_count, false};
  // Push a block to load stuff from the stack
  *VECTOR_PUSH(expr_stack, stack_count, stack_cap) = (inchoate_expression){
      spill_or_load_code(&ctx, 0, true, BJVM_INTERP_RESULT_INT, 0, -1), 0, -1,
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
        bjvm_wasm_update_block(ctx.module, ie->ref, exprs, stack_count - i - 1,
                               bjvm_wasm_void(), ie->is_loop);
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
          bjvm_wasm_block(ctx.module, nullptr, 0, bjvm_wasm_void(), is_loop);
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
  bjvm_wasm_type params = bjvm_wasm_string_to_tuple(ctx.module, "iii");
  bjvm_wasm_type locals =
      bjvm_wasm_make_tuple(ctx.module, ctx.wvars, ctx.wvars_count);
  bjvm_wasm_function *fn = bjvm_wasm_add_function(
      ctx.module, params, bjvm_wasm_int32(), locals, body, "run");
  bjvm_wasm_export_function(ctx.module, fn);

  bjvm_bytevector result = bjvm_wasm_module_serialize(ctx.module);
  wasm = bjvm_wasm_instantiate_module(ctx.module, method->name.chars);
  if (wasm->status != BJVM_WASM_INSTANTIATION_SUCCESS) {
    // printf("Error instantiating module for method %.*s\n",
    // fmt_slice(method->name));
  } else {
    // printf("Successfully compiled method %.*s on class %.*s \n",
    // fmt_slice(method->name), fmt_slice(method->my_class->name));
  }

  free(result.bytes);

error_2:
  free_topo_ctx(topo);
error_1:
  free(ctx.val_to_local_map);
  free(ctx.wasm_blocks);
  free(ctx.wvars);
  bjvm_wasm_module_free(ctx.module);

  return wasm;
}

void free_wasm_compiled_method(void *p) {
  if (!p)
    return;
  bjvm_wasm_instantiation_result *compiled_method = p;
  free(compiled_method->exports);
  free(compiled_method);
}