// Java bytecode to WebAssembly translator

#include "wasm_jit.h"
#include "analysis.h"
#include "arrays.h"
#include "objects.h"

#undef __EMSCRIPTEN__  // prevent EVERYTHING from being exported (why u do dis)
#include <binaryen-c.h>
#define __EMSCRIPTEN__

enum wasm_representable_kind {
  WASM_INT32, WASM_FLOAT32, WASM_FLOAT64, WASM_INT64
};

enum {
  THREAD_PARAM,
  FRAME_PARAM,
  RESULT_PARAM
};

typedef struct {
  // The WASM module we will emit as bytes
  BinaryenModuleRef module;
  // Used to translate arbitrary CFG into WASM structured control flow
  RelooperRef relooper;
  // List of relooper blocks (same # as # of bjvm analysis blocks). First block
  // is the entry point.
  RelooperBlockRef *relooper_blocks;

  // The method and (for convenience) its code and analysis
  bjvm_cp_method *method;
  bjvm_attribute_code *code;
  bjvm_code_analysis *analysis;

  // Mapping between value_index * 3 + type_kind and the wasm index for
  // local.get and local.set. -1 means not yet mapped.
  //
  // The ordering for type kind is REF/INT, FLOAT, DOUBLE, LONG.
  int *val_to_local_map;
  int next_local;

  BinaryenType *wvars;
  int wvars_count;
  int wvars_cap;
} bjvm_wasm_compile_ctx;

#define WASM_TYPES_COUNT 4

enum wasm_representable_kind jvm_type_to_wasm_kind(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return WASM_INT32;
  case BJVM_TYPE_KIND_FLOAT:
    return WASM_FLOAT32;
  case BJVM_TYPE_KIND_DOUBLE:
    return WASM_FLOAT64;
  case BJVM_TYPE_KIND_LONG:
    return WASM_INT64;
  case BJVM_TYPE_KIND_VOID:
    UNREACHABLE();
  }
}

BinaryenType wasm_kind_to_binaryen_type(enum wasm_representable_kind kind) {
  switch (kind) {
  case WASM_INT32:
    return BinaryenTypeInt32();
  case WASM_FLOAT32:
    return BinaryenTypeFloat32();
  case WASM_FLOAT64:
    return BinaryenTypeFloat64();
  case WASM_INT64:
    return BinaryenTypeInt64();
  default:
    UNREACHABLE();
  }
}

int sizeof_wasm_kind(enum wasm_representable_kind kind) {
  switch (kind) {
  case WASM_INT32:
  case WASM_FLOAT32:
    return 4;
  case WASM_FLOAT64:
  case WASM_INT64:
    return 8;
  default:
    UNREACHABLE();
  }
}

BinaryenType jvm_type_to_binaryen_type(bjvm_type_kind kind) {
  return wasm_kind_to_binaryen_type(jvm_type_to_wasm_kind(kind));
}

int jvm_stack_to_wasm_local(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind) {
  int i = index * WASM_TYPES_COUNT + jvm_type_to_wasm_kind(kind);
  int *l = &ctx->val_to_local_map[i];
  if (*l == -1) {
    *VECTOR_PUSH(ctx->wvars, ctx->wvars_count, ctx->wvars_cap) = jvm_type_to_binaryen_type(kind);
    *l = ctx->next_local++;
  }
  return *l;
}

int jvm_local_to_wasm_local(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return jvm_stack_to_wasm_local(ctx, ctx->code->max_stack + index, kind);
}

BinaryenExpressionRef get_stack_value(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return BinaryenLocalGet(ctx->module, jvm_stack_to_wasm_local(ctx, index, kind),
    jvm_type_to_binaryen_type(kind));
}

BinaryenExpressionRef set_stack_value(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind,
                                      BinaryenExpressionRef value) {
  return BinaryenLocalSet(ctx->module, jvm_stack_to_wasm_local(ctx, index, kind), value);
}

BinaryenExpressionRef get_local_value(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind) {
  return BinaryenLocalGet(ctx->module, jvm_local_to_wasm_local(ctx, index, kind),
    jvm_type_to_binaryen_type(kind));
}

BinaryenExpressionRef set_local_value(bjvm_wasm_compile_ctx *ctx, int index, bjvm_type_kind kind,
                                      BinaryenExpressionRef value) {
  return BinaryenLocalSet(ctx->module, jvm_local_to_wasm_local(ctx, index, kind), value);
}

// Generate spill code that writes to stack->values at the appropriate location,
// or load parameters from the stack at the function beginning.
BinaryenExpressionRef spill_or_load_code(bjvm_wasm_compile_ctx *ctx, int pc, bool do_load, int return_value) {
  int values_start = offsetof(bjvm_stack_frame, values);
  int value_size = sizeof(bjvm_stack_value);

  bjvm_compressed_bitset refs[5];
  for (int i = 0; i < 5; ++i)
    refs[i] = ctx->analysis->insn_index_to_kinds[i][pc];

  BinaryenExpressionRef *result = nullptr;
  int result_count = 0, result_cap = 0;

  for (int slot = 0; slot < ctx->code->max_locals + ctx->code->max_stack; ++slot) {
    BinaryenExpressionRef frame = BinaryenLocalGet(ctx->module, FRAME_PARAM, BinaryenTypeInt32());
    BinaryenExpressionRef addr = BinaryenBinary(ctx->module, BinaryenAddInt32(),
                                                frame,
                                                BinaryenConst(ctx->module, BinaryenLiteralInt32(values_start + slot * value_size)));

    enum wasm_representable_kind wkind;

    if (bjvm_test_compressed_bitset(refs[0], slot) ||  // int or ref
        bjvm_test_compressed_bitset(refs[1], slot)) {
      wkind = WASM_INT32;
    } else if (bjvm_test_compressed_bitset(refs[2], slot)) {  // float
      wkind = WASM_FLOAT32;
    } else if (bjvm_test_compressed_bitset(refs[3], slot)) {  // double
      wkind = WASM_FLOAT64;
    } else if (bjvm_test_compressed_bitset(refs[4], slot)) {  // long
      wkind = WASM_INT64;
    } else {  // unused slot
      continue;
    }

    int wasm_local = jvm_stack_to_wasm_local(ctx, slot, wkind);
    int sz = sizeof_wasm_kind(wkind);
    BinaryenType binaryen_type = wasm_kind_to_binaryen_type(wkind);
    if (do_load) {
      // (local.set wasm_local (i32.load (i32.add frame (i32.const offset))))
      BinaryenExpressionRef load = BinaryenLoad(ctx->module,
        sz, 0, 0, 0, binaryen_type, addr, "0");
      *VECTOR_PUSH(result, result_count, result_cap) = BinaryenLocalSet(ctx->module, wasm_local, load);
    } else {
      // (i32.store (i32.add frame (i32.const offset)) (local.get wasm_local))
      BinaryenExpressionRef value = BinaryenLocalGet(ctx->module, wasm_local, binaryen_type);
      *VECTOR_PUSH(result, result_count, result_cap) = BinaryenStore(ctx->module,
        sz, 0, 0, addr, value, binaryen_type, "0");
    }
  }

  // Make block of all the spill/load instructions
  if (!do_load) {
    // Set thread->program_counter to pc
    BinaryenExpressionRef frame = BinaryenLocalGet(ctx->module, FRAME_PARAM, BinaryenTypeInt32());
    BinaryenExpressionRef pc_const = BinaryenConst(ctx->module, BinaryenLiteralInt32(pc));
    BinaryenExpressionRef store = BinaryenStore(ctx->module, 4, offsetof(bjvm_stack_frame, program_counter), 0,
      frame, pc_const, BinaryenTypeInt32(), "0");
    *VECTOR_PUSH(result, result_count, result_cap) = store;
    *VECTOR_PUSH(result, result_count, result_cap) = BinaryenReturn(ctx->module, BinaryenConst(ctx->module, BinaryenLiteralInt32(return_value)));
  }

  BinaryenExpressionRef block = BinaryenBlock(ctx->module, "0", result, result_count, BinaryenTypeNone());
  return block;
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_new_array(bjvm_thread *thread, bjvm_classdesc *classdesc, int count) {
  return CreateObjectArray1D(thread, classdesc, count);
}

EMSCRIPTEN_KEEPALIVE
void *wasm_runtime_new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  return AllocateObject(thread, classdesc, classdesc->data_bytes);
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t wasm_runtime_raise_npe(bjvm_thread* thread) {
  bjvm_null_pointer_exception(thread);
  return BJVM_INTERP_RESULT_EXC;
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t
wasm_runtime_raise_array_index_oob(bjvm_thread* thread, int index, int length) {
  return BJVM_INTERP_RESULT_EXC;
}

int add_runtime_imports(bjvm_wasm_compile_ctx *ctx) {
  BinaryenAddMemoryImport(ctx->module, "0", "env", "memory", 0);
  {
    BinaryenType params = BinaryenTypeCreate((BinaryenType[]){BinaryenTypeInt32()}, 1);
    BinaryenAddFunctionImport(ctx->module, "raise_npe", "env", "raise_npe",
        params, BinaryenTypeInt32());
  }
  {
    BinaryenType params = BinaryenTypeCreate((BinaryenType[]){BinaryenTypeInt32(), BinaryenTypeInt32(), BinaryenTypeInt32()}, 3);
    BinaryenAddFunctionImport(ctx->module, "raise_oob", "env", "raise_oob",
          params, BinaryenTypeInt32());
  }
  {
    BinaryenType params = BinaryenTypeCreate((BinaryenType[]){BinaryenTypeInt32(), BinaryenTypeInt32()}, 2);
    BinaryenAddFunctionImport(ctx->module, "new_object", "env", "new_object",
          params, BinaryenTypeInt32());
  }
  {
    BinaryenType params = BinaryenTypeCreate((BinaryenType[]){BinaryenTypeInt32(), BinaryenTypeInt32(), BinaryenTypeInt32()}, 3);
    BinaryenAddFunctionImport(ctx->module, "new_array", "env", "new_array",
          params, BinaryenTypeInt32());
  }
  return 0;
}

#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

BinaryenExpressionRef wasm_get_array_length(bjvm_wasm_compile_ctx *ctx, BinaryenExpressionRef array) {
  return BinaryenLoad(ctx->module, 4, 0, kArrayLengthOffset, 0, BinaryenTypeInt32(), array, "0");
}

BinaryenExpressionRef wasm_raise_npe(bjvm_wasm_compile_ctx *ctx) {
  // (return (call $raise_npe (local.get THREAD_PARAM)))
  return BinaryenCall(ctx->module, "raise_npe", (BinaryenExpressionRef[]){
    BinaryenLocalGet(ctx->module, THREAD_PARAM, BinaryenTypeInt32())
  }, 1, BinaryenTypeInt32());
}

BinaryenExpressionRef wasm_raise_oob(bjvm_wasm_compile_ctx *ctx, BinaryenExpressionRef index, BinaryenExpressionRef length) {
  // (return (call $raise_oob (local.get THREAD_PARAM) <index> <length>))
  return BinaryenCall(ctx->module, "raise_oob", (BinaryenExpressionRef[]){
    BinaryenLocalGet(ctx->module, THREAD_PARAM, BinaryenTypeInt32()),
    index,
    length
  }, 3, BinaryenTypeInt32());
}


bjvm_type_kind inspect_value_type(bjvm_wasm_compile_ctx *ctx, int pc, int stack_i) {
  int i = 0;
  for (; i < 5; ++i)
    if (bjvm_test_compressed_bitset(ctx->analysis->insn_index_to_kinds[i][pc], stack_i))
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

BinaryenExpressionRef wasm_move_value(bjvm_wasm_compile_ctx *ctx, int pc, int from, int to) {
  bjvm_type_kind kind = inspect_value_type(ctx, pc, from);

  return BinaryenLocalSet(ctx->module, jvm_stack_to_wasm_local(ctx, to, kind),
    BinaryenLocalGet(ctx->module, jvm_stack_to_wasm_local(ctx, from, kind),
      jvm_type_to_binaryen_type(kind)));
}

BinaryenExpressionRef wasm_lower_array_load_store(bjvm_wasm_compile_ctx *ctx,
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
  BinaryenExpressionRef index = get_stack_value(ctx, sd - 1 - is_store, BJVM_TYPE_KIND_INT);
  BinaryenExpressionRef array = get_stack_value(ctx, sd - 2 - is_store, BJVM_TYPE_KIND_REFERENCE);

  // <nullcheck> = (eqz array)
  BinaryenExpressionRef null_check = BinaryenUnary(ctx->module, BinaryenEqZInt32(), array);
  // <addrminusheader> = array + index * sizeof(component)
  BinaryenExpressionRef addr = BinaryenBinary(ctx->module, BinaryenAddInt32(),
      array,
      BinaryenBinary(ctx->module, BinaryenMulInt32(),
        index,
        BinaryenConst(ctx->module, BinaryenLiteralInt32(sizeof_type_kind(component)))));

  BinaryenExpressionRef execute;
  BinaryenType dtype = jvm_type_to_binaryen_type(component);

  if (is_store) {
    // (<dtype>.store offset=kArrayDataOffset <addr>)
    BinaryenExpressionRef value = get_stack_value(ctx, sd - 1, component);
    execute = BinaryenStore(ctx->module,
      sizeof_type_kind(component), kArrayDataOffset, 0, addr, value, dtype, "0");
  } else {
    BinaryenExpressionRef data = BinaryenLoad(ctx->module,
      sizeof_type_kind(component), 1 /* sign extend */, kArrayDataOffset, 0, dtype, addr, "0");
    execute = set_stack_value(ctx, sd - 2, component, data);
  }

  // <length> = (i32.load (i32.add <array> kArrayLengthOffset))
  BinaryenExpressionRef length = wasm_get_array_length(ctx, array);
  // <boundscheck> = (i32.ge_u <index> <length>)
  BinaryenExpressionRef bounds_check = BinaryenBinary(ctx->module, BinaryenGeUInt32(),
  index, length);
  BinaryenExpressionRef raise_oob = wasm_raise_oob(ctx, index, length);
  // <boundschecked>
  BinaryenExpressionRef bounds_checked = BinaryenIf(ctx->module, bounds_check, raise_oob, execute);
  // (if (eqz <array>) (<raise npe>) (<boundschecked>)
  BinaryenExpressionRef npe = wasm_raise_npe(ctx);
  BinaryenExpressionRef load_store = BinaryenIf(ctx->module, null_check, npe, bounds_checked);
  return load_store;
}

BinaryenExpressionRef wasm_lower_icmp_lcmp(bjvm_wasm_compile_ctx * ctx,
  const bjvm_bytecode_insn *insn, int sd, struct BinaryenLiteral zero_literal,
  struct BinaryenLiteral one_literal, struct BinaryenLiteral neg_one_literal,
  BinaryenOp lt, BinaryenOp gt) {
  bjvm_type_kind kind = insn->kind == bjvm_insn_lcmp ? BJVM_TYPE_KIND_LONG : BJVM_TYPE_KIND_INT;

  BinaryenExpressionRef right = get_stack_value(ctx, sd - 1, kind);
  BinaryenExpressionRef left = get_stack_value(ctx, sd - 2, kind);

  BinaryenExpressionRef zero = BinaryenConst(ctx->module, zero_literal);
  BinaryenExpressionRef one = BinaryenConst(ctx->module, one_literal);
  BinaryenExpressionRef neg_one = BinaryenConst(ctx->module, neg_one_literal);

  BinaryenExpressionRef cmp_greater = BinaryenBinary(ctx->module, gt, left, right);
  BinaryenExpressionRef cmp_less = BinaryenBinary(ctx->module, lt, left, right);

  BinaryenExpressionRef result = BinaryenSelect(ctx->module, cmp_greater, one, BinaryenSelect(ctx->module, cmp_less, neg_one, zero));
  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_INT, result);
}

BinaryenExpressionRef wasm_lower_long_shift(bjvm_wasm_compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd) {
  // Convert int to long
  BinaryenExpressionRef right = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
  BinaryenExpressionRef right_long = BinaryenUnary(ctx->module, BinaryenExtendUInt32(), right);

  BinaryenExpressionRef left = get_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG);
  BinaryenOp op = insn->kind == bjvm_insn_lshl ? BinaryenShlInt64() :
    insn->kind == bjvm_insn_lushr ? BinaryenShrUInt64() : BinaryenShrSInt64();
  BinaryenExpressionRef result = BinaryenBinary(ctx->module, op, left, right_long);

  return set_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_LONG, result);
}

BinaryenExpressionRef wasm_lower_local_load_store(bjvm_wasm_compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd) {
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
    BinaryenExpressionRef value = get_stack_value(ctx, sd - 1, kind);
    return set_local_value(ctx, index, kind, value);
  }
  BinaryenExpressionRef value = get_local_value(ctx, index, kind);
  return set_stack_value(ctx, sd, kind, value);
}

BinaryenExpressionRef wasm_lower_iinc(bjvm_wasm_compile_ctx *ctx, const bjvm_bytecode_insn *insn, int sd) {
  BinaryenExpressionRef local = get_local_value(ctx, insn->iinc.index, BJVM_TYPE_KIND_INT);
  BinaryenExpressionRef increment = BinaryenConst(ctx->module, BinaryenLiteralInt32(insn->iinc.const_));
  BinaryenExpressionRef result = BinaryenBinary(ctx->module, BinaryenAddInt32(), local, increment);
  BinaryenExpressionRef iinc = set_local_value(ctx, insn->iinc.index, BJVM_TYPE_KIND_INT, result);
  return iinc;
}

BinaryenExpressionRef wasm_lower_return(bjvm_wasm_compile_ctx *ctx, const bjvm_bytecode_insn *insn, int pc, int sd) {
  // Write the value into memory at "result"
  bjvm_type_kind kind = inspect_value_type(ctx, pc, sd - 1);
  BinaryenExpressionRef value = get_stack_value(ctx, sd - 1, kind);
  BinaryenExpressionRef store = BinaryenStore(ctx->module, sizeof_type_kind(kind), 0, 0,
    BinaryenLocalGet(ctx->module, RESULT_PARAM, BinaryenTypeInt32()), value,
    jvm_type_to_binaryen_type(kind), "0");
  return store;
}

int wasm_compile_block(bjvm_wasm_compile_ctx *ctx, const bjvm_basic_block *bb) {
  BinaryenExpressionRef *results = nullptr;
  int result_count = 0, result_cap = 0;

#undef PUSH_EXPR
#define PUSH_EXPR *VECTOR_PUSH(results, result_count, result_cap)

#define CONVERSION_OP(from, to, op) { \
BinaryenExpressionRef value = get_stack_value(ctx, sd - 1, from); \
BinaryenExpressionRef converted = BinaryenUnary(ctx->module, Binaryen##op(), value); \
PUSH_EXPR = set_stack_value(ctx, sd - 1, to, converted); \
break; \
}

#define BIN_OP_SAME_TYPE(kind, op) { \
BinaryenExpressionRef right = get_stack_value(ctx, sd - 1, kind); \
BinaryenExpressionRef left = get_stack_value(ctx, sd - 2, kind); \
BinaryenExpressionRef result = BinaryenBinary(ctx->module, Binaryen##op(), left, right); \
PUSH_EXPR = set_stack_value(ctx, sd - 2, kind, result); \
break; \
}

  uint16_t *stack_depths = ctx->analysis->insn_index_to_stack_depth;

  int expr_i = 0;
  for (int i = 0, pc = bb->start_index; i < bb->insn_count; ++i, ++pc, ++expr_i) {
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
    case bjvm_insn_iastore:
    {
      PUSH_EXPR = wasm_lower_array_load_store(ctx, insn, sd);
      break;
    }
    case bjvm_insn_aconst_null: {
      PUSH_EXPR = set_local_value(ctx, sd, BJVM_TYPE_KIND_REFERENCE, BinaryenConst(ctx->module, BinaryenLiteralInt32(0)));
      break;
    }
    case bjvm_insn_areturn:
      UNREACHABLE();
    case bjvm_insn_arraylength: {
      BinaryenExpressionRef array = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
      BinaryenExpressionRef nullcheck = BinaryenUnary(ctx->module, BinaryenEqZInt32(), array);
      BinaryenExpressionRef raise_npe = wasm_raise_npe(ctx);
      BinaryenExpressionRef length = wasm_get_array_length(ctx, array);
      PUSH_EXPR = BinaryenIf(ctx->module, nullcheck, raise_npe, length);
      break;
    }
    case bjvm_insn_athrow: {
      BinaryenExpressionRef exception = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_REFERENCE);
      int store_offs = offsetof(bjvm_thread, current_exception);
      BinaryenExpressionRef store = BinaryenStore(ctx->module, 4, store_offs, 0,
        BinaryenLocalGet(ctx->module, THREAD_PARAM, BinaryenTypeInt32()),
        exception, BinaryenTypeInt32(), "0");
      BinaryenExpressionRef execute = BinaryenIf(ctx->module,
        BinaryenUnary(ctx->module, BinaryenEqZInt32(), exception),
        wasm_raise_npe(ctx), store);
      PUSH_EXPR = execute;
      BinaryenExpressionRef ret = BinaryenReturn(ctx->module, BinaryenConst(ctx->module, BinaryenLiteralInt32(BJVM_INTERP_RESULT_EXC)));
      PUSH_EXPR = ret;
      break;
    }

    case bjvm_insn_d2f:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_FLOAT, DemoteFloat64);
    case bjvm_insn_d2i:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_INT, TruncSFloat64ToInt32);
    case bjvm_insn_d2l:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_LONG, TruncSFloat64ToInt64);
    case bjvm_insn_dadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, AddFloat64);
    case bjvm_insn_ddiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, DivFloat64);
    case bjvm_insn_dmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, MulFloat64);
    case bjvm_insn_dneg:
      CONVERSION_OP(BJVM_TYPE_KIND_DOUBLE, BJVM_TYPE_KIND_DOUBLE, NegFloat64);
    case bjvm_insn_dreturn:
    case bjvm_insn_freturn:
    case bjvm_insn_ireturn:
    case bjvm_insn_lreturn: {
      PUSH_EXPR = wasm_lower_return(ctx, insn, pc, sd);
      PUSH_EXPR = BinaryenReturn(ctx->module, BinaryenConst(ctx->module, BinaryenLiteralInt32(BJVM_INTERP_RESULT_OK)));
      break;
    }
    case bjvm_insn_dsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_DOUBLE, SubFloat64);
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
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_DOUBLE, PromoteFloat32);
    case bjvm_insn_f2i:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_INT, TruncSFloat32ToInt32);
    case bjvm_insn_f2l:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_LONG, TruncSFloat32ToInt64);
    case bjvm_insn_fadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, AddFloat32);
    case bjvm_insn_fdiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, DivFloat32);
    case bjvm_insn_fmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, MulFloat32);
    case bjvm_insn_fneg:
      CONVERSION_OP(BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_FLOAT, NegFloat32);
    case bjvm_insn_fsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_FLOAT, SubFloat32);
    case bjvm_insn_i2b:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_INT, ExtendS8Int32)
    case bjvm_insn_i2c: {
      BinaryenExpressionRef val = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
      BinaryenExpressionRef max_char = BinaryenConst(ctx->module, BinaryenLiteralInt32(0xFFFF));
      BinaryenExpressionRef result = BinaryenBinary(ctx->module, BinaryenAndInt32(), val, max_char);
      PUSH_EXPR = set_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT, result);
      break;
    }
    case bjvm_insn_i2d:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_DOUBLE, ConvertSInt32ToFloat64)
    case bjvm_insn_i2f:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_FLOAT, ConvertSInt32ToFloat32)
    case bjvm_insn_i2l:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_LONG, ExtendSInt32)
    case bjvm_insn_i2s:
      CONVERSION_OP(BJVM_TYPE_KIND_INT, BJVM_TYPE_KIND_INT, ExtendS16Int32)
    case bjvm_insn_iadd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, AddInt32);
    case bjvm_insn_iand:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, AndInt32);
    case bjvm_insn_idiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, DivSInt32);
    case bjvm_insn_imul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, MulInt32);
    case bjvm_insn_ineg:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, SubInt32);
    case bjvm_insn_ior:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, OrInt32);
    case bjvm_insn_irem:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, RemSInt32);
    case bjvm_insn_ishl:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, ShlInt32);
    case bjvm_insn_ishr:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, ShrSInt32);
    case bjvm_insn_isub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, SubInt32);
    case bjvm_insn_iushr:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, ShrUInt32);
    case bjvm_insn_ixor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_INT, XorInt32);
    case bjvm_insn_l2d:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_DOUBLE, ConvertSInt64ToFloat64);
    case bjvm_insn_l2f:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_FLOAT, ConvertSInt64ToFloat32);
    case bjvm_insn_l2i:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_INT, WrapInt64);
    case bjvm_insn_ladd:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, AddInt64);
    case bjvm_insn_land:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, AndInt64);
    case bjvm_insn_lcmp: {
      // if a > b then 1, if a < b then -1, else 0
      PUSH_EXPR = wasm_lower_icmp_lcmp(ctx, insn, sd,
        BinaryenLiteralInt32(0), BinaryenLiteralInt32(1),
        BinaryenLiteralInt32(-1), BinaryenLtSInt64(), BinaryenGtSInt64());
      break;
    }
    case bjvm_insn_ldiv:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, DivSInt64);
    case bjvm_insn_lmul:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, MulInt64);
    case bjvm_insn_lneg:
      CONVERSION_OP(BJVM_TYPE_KIND_LONG, BJVM_TYPE_KIND_LONG, SubInt64);
    case bjvm_insn_lor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, OrInt64);
    case bjvm_insn_lrem:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, RemSInt64);
    case bjvm_insn_lshl:
    case bjvm_insn_lshr:
    case bjvm_insn_lushr: {
      PUSH_EXPR = wasm_lower_long_shift(ctx, insn, sd);
      break;
    }
    case bjvm_insn_lsub:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, SubInt64);
    case bjvm_insn_lxor:
      BIN_OP_SAME_TYPE(BJVM_TYPE_KIND_LONG, XorInt64);
      break;
    case bjvm_insn_pop:
    case bjvm_insn_pop2: {
      // nothing, just changes stack depth
      break;
    }
    case bjvm_insn_return: {
      // Just return OK
      PUSH_EXPR = BinaryenReturn(ctx->module, BinaryenConst(ctx->module, BinaryenLiteralInt32(BJVM_INTERP_RESULT_OK)));
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
      //PUSH_EXPR = wasm_lower_invokespecial(ctx, insn, sd);
      break;
    }
    case bjvm_insn_invokestatic: {
      goto unimplemented;
      //PUSH_EXPR = wasm_lower_invokestatic(ctx, insn, sd);
      break;
    }
    case bjvm_insn_ldc:
    case bjvm_insn_ldc2_w: {
      goto unimplemented;
      //PUSH_EXPR = wasm_lower_ldc(ctx, insn, sd);
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
      goto done;  // will be handled in the next pass
    case bjvm_insn_iconst: {
      BinaryenExpressionRef value = BinaryenConst(ctx->module, BinaryenLiteralInt32((int)insn->integer_imm));
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_INT, value);
      break;
    }
    case bjvm_insn_dconst: {
      BinaryenExpressionRef value = BinaryenConst(ctx->module, BinaryenLiteralFloat64(insn->d_imm));
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_DOUBLE, value);
      break;
    }
    case bjvm_insn_fconst: {
      BinaryenExpressionRef value = BinaryenConst(ctx->module, BinaryenLiteralFloat32(insn->f_imm));
      PUSH_EXPR = set_stack_value(ctx, sd, BJVM_TYPE_KIND_FLOAT, value);
      break;
    }
    case bjvm_insn_lconst: {
      BinaryenExpressionRef value = BinaryenConst(ctx->module, BinaryenLiteralInt64(insn->integer_imm));
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
    PUSH_EXPR = spill_or_load_code(ctx, bb->start_index, false, BJVM_INTERP_RESULT_INT);
  }

  done:

  // Create a block with the expressions
  BinaryenExpressionRef block = BinaryenBlock(ctx->module, nullptr, results, result_count, BinaryenTypeAuto());

  // Print block
  BinaryenExpressionPrint(block);

  // Add the block to the relooper
  ctx->relooper_blocks[bb->my_index] = RelooperAddBlock(ctx->relooper, block);

  free(results);
  return 0;
}

int wasm_compile_block_edges(bjvm_wasm_compile_ctx *ctx, const bjvm_basic_block *bb) {
  // Look at the last instruction and enjoy
  const bjvm_bytecode_insn *insn = bb->start + bb->insn_count - 1;
  int sd = ctx->analysis->insn_index_to_stack_depth[bb->start_index + bb->insn_count - 1];

  BinaryenOp ops[] = {
    BinaryenEqInt32(), // if_acmpeq
    BinaryenNeInt32(), // if_acmpne
    BinaryenEqInt32(), // if_icmpeq
    BinaryenNeInt32(), // if_icmpne
    BinaryenLtSInt32(), // if_icmplt
    BinaryenGeSInt32(), // if_icmpge
    BinaryenGtSInt32(), // if_icmpgt
    BinaryenLeSInt32(), // if_icmple
    BinaryenEqInt32(), // ifeq
    BinaryenNeInt32(), // ifne
    BinaryenLtSInt32(), // iflt
    BinaryenGeSInt32(), // ifge
    BinaryenGtSInt32(), // ifgt
    BinaryenLeSInt32(), // ifle
    BinaryenEqInt32(), // ifnonnull
    BinaryenEqInt32() // ifnull
  };

  bool compare_with_zero = false;

  switch (insn->kind) {
  case bjvm_insn_goto:
    // No condition
    RelooperAddBranch(ctx->relooper_blocks[bb->my_index],
      ctx->relooper_blocks[bb->next[0]], nullptr, nullptr);
    return 0;
  case bjvm_insn_jsr:
    return -1;
  case bjvm_insn_ifeq:
  case bjvm_insn_ifne:
  case bjvm_insn_iflt:
  case bjvm_insn_ifge:
  case bjvm_insn_ifgt:
  case bjvm_insn_ifle:
  case bjvm_insn_ifnonnull:
  case bjvm_insn_ifnull:
    compare_with_zero = true;
    [[fallthrough]];
  case bjvm_insn_if_acmpeq:
  case bjvm_insn_if_acmpne:
  case bjvm_insn_if_icmpeq:
  case bjvm_insn_if_icmpne:
  case bjvm_insn_if_icmplt:
  case bjvm_insn_if_icmpge:
  case bjvm_insn_if_icmpgt:
  case bjvm_insn_if_icmple: {
    // Compare the two int values
    BinaryenOp op = ops[insn->kind - bjvm_insn_if_acmpeq];
    BinaryenExpressionRef right, left;
    if (compare_with_zero) {
      right = BinaryenConst(ctx->module, BinaryenLiteralInt32(0));
      left = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
    } else {
      right = get_stack_value(ctx, sd - 1, BJVM_TYPE_KIND_INT);
      left = get_stack_value(ctx, sd - 2, BJVM_TYPE_KIND_INT);
    }

    BinaryenExpressionRef condition = BinaryenBinary(ctx->module, op, left, right);

    RelooperAddBranch(ctx->relooper_blocks[bb->my_index],
      ctx->relooper_blocks[bb->next[0]], condition, nullptr);
    RelooperAddBranch(ctx->relooper_blocks[bb->my_index],
      ctx->relooper_blocks[bb->next[1]], nullptr, nullptr);
    return 0;
  }
  case bjvm_insn_tableswitch:
  case bjvm_insn_lookupswitch:
  case bjvm_insn_ret:
    return -1;  // unimplemented
  case bjvm_insn_return:
  case bjvm_insn_ireturn:
  case bjvm_insn_freturn:
  case bjvm_insn_dreturn:
  case bjvm_insn_lreturn:
  case bjvm_insn_areturn:
    return 0;  // nothing to do
  default: {
    // Link one block to next
    RelooperAddBranch(ctx->relooper_blocks[bb->my_index],
      ctx->relooper_blocks[bb->my_index + 1], nullptr, nullptr);
    return 0;
  }
  }
}

bjvm_wasm_jit_compiled_method* bjvm_wasm_jit_compile(bjvm_thread *thread,
                                                     const bjvm_cp_method* method) {
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
  bjvm_code_analysis *analy = method->code_analysis;

  bjvm_wasm_compile_ctx ctx = { nullptr };
  ctx.module = BinaryenModuleCreate();
  ctx.relooper = RelooperCreate(ctx.module);
  ctx.analysis = analy;
  ctx.next_local = 3;  // first three used by thread, frame, result
  for (int i = 0; i < 3; ++i) {
    *VECTOR_PUSH(ctx.wvars, ctx.wvars_count, ctx.wvars_cap) = BinaryenTypeInt32();
  }
  ctx.relooper_blocks = calloc(analy->block_count, sizeof(BinaryenExpressionRef));
  ctx.code = method->code;
  int vtlmap_len = WASM_TYPES_COUNT * (method->code->max_locals + method->code->max_stack) * sizeof(int);
  ctx.val_to_local_map = malloc(vtlmap_len);
  memset(ctx.val_to_local_map, -1, vtlmap_len);

  for (int block_i = 0; block_i < analy->block_count; ++block_i) {
    bjvm_basic_block *b = analy->blocks + block_i;
    if (wasm_compile_block(&ctx, b)) {
    printf("ERROR3");
      goto error;
    }
  }

  for (int block_i = 0; block_i < analy->block_count; ++block_i) {
    bjvm_basic_block *b = analy->blocks + block_i;
    if (wasm_compile_block_edges(&ctx, b)) {
      printf("ERROR2");
      goto error;
    }
  }

  BinaryenType params[3] = {BinaryenTypeInt32(), BinaryenTypeInt32(), BinaryenTypeInt32()};
  BinaryenType results = BinaryenTypeInt32();

  if (add_runtime_imports(&ctx)) {
    printf("ERROR1");
    goto error;
  }

  // Print it out
  BinaryenExpressionRef final = RelooperRenderAndDispose(ctx.relooper, ctx.relooper_blocks[0], ctx.next_local++);
  *VECTOR_PUSH(ctx.wvars, ctx.wvars_count, ctx.wvars_cap) = BinaryenTypeInt32();
  ok = 1;

error:
  bjvm_wasm_jit_compiled_method *compiled_method = nullptr;
  if (ok) {
    BinaryenFunctionRef run = BinaryenAddFunction(ctx.module, "run",
      BinaryenTypeCreate(params, 3), results, ctx.wvars, ctx.wvars_count, final);

    // Create the export
    BinaryenAddFunctionExport(ctx.module, "run", "run");

    // Print the module
    BinaryenModulePrint(ctx.module);

    // Write out to bytes
    BinaryenModuleAllocateAndWriteResult result =
      BinaryenModuleAllocateAndWrite(ctx.module, nullptr);

#ifdef EMSCRIPTEN
    void *function_ptr = EM_ASM_PTR({
      try {
        var module = new WebAssembly.Module(HEAPU8.subarray($1, $2));
        var instance = new WebAssembly.Instance(module, {
          env: {
            memory: wasmMemory,
            raise_npe: wasmExports["wasm_runtime_raise_npe"],
            raise_oob: wasmExports["wasm_runtime_raise_array_index_oob"],
            new_array: wasmExports["wasm_runtime_new_array"],
            new_object: wasmExports["wasm_runtime_new_object"]
          }
        });
        return addFunction(instance.exports.run, 'iiii');
      } catch (e) {
        console.log(e);
        return 0;
      }
    }, "iiii", (uintptr_t)result.binary, (uintptr_t)(result.binary + result.binaryBytes));
#else
    void *function_ptr = nullptr;
#endif

    if (function_ptr != nullptr) {
      compiled_method = calloc(1, sizeof(bjvm_wasm_jit_compiled_method));

      printf("%p\n", function_ptr);

      compiled_method->ready = true;
      compiled_method->fn = function_ptr;
    }

    free(result.binary);
  }

  // Clean up the module, which owns all the objects we created above
  BinaryenModuleDispose(ctx.module);

  return compiled_method;
}

void free_wasm_compiled_method(void *p) {
  if (!p) return;
  bjvm_wasm_jit_compiled_method *compiled_method = p;
  free(compiled_method);
}