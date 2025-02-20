// The baseline JIT for WebAssembly, also known as the "dumb JIT".
//
// There is a one-to-one mapping between the operand stack/locals and WASM locals. Construction of the CFG
// structure is done using the "Stackifier" algorithm. Inlining is not yet implemented.

#include "dumb_jit.h"

#include <analysis.h>
#include <arrays.h>
#include <exceptions.h>
#include <math.h>
#include <objects.h>
#include <wasm/wasm_utils.h>

typedef wasm_expression* expression;

typedef struct {
  wasm_module *module;
  wasm_value_type *params;
  wasm_value_type *locals;
  wasm_type returns;
  int next_local;
} function_builder;

static wasm_type tuple_from_array(wasm_module *module, wasm_value_type *array) {
  return wasm_make_tuple(module, array, arrlen(array));
}

void init_function_builder(wasm_module *module, function_builder *builder, const wasm_value_type *params, wasm_type returns) {
  builder->module = module;
  builder->params = nullptr;
  builder->returns = returns;
  for (int i = 0; i < arrlen(params); ++i) {
    arrput(builder->params, params[i]);
  }
  builder->locals = nullptr;
  builder->next_local = arrlen(builder->params);
}

[[maybe_unused]] static int fb_new_local(function_builder *builder, wasm_value_type local) {
  int local_i = builder->next_local++;
  arrput(builder->locals, local);
  DCHECK(arrlen(builder->locals) + arrlen(builder->params) == local_i);
  return local_i;
}

wasm_function *finalize_function_builder(function_builder *builder, const char *name, expression body) {
  wasm_type params = tuple_from_array(builder->module, builder->params);
  wasm_type results = builder->returns;
  wasm_type locals = tuple_from_array(builder->module, builder->locals);

  wasm_function *fn = wasm_add_function(builder->module, params, results, locals, body, name);

  arrfree(builder->params);
  arrfree(builder->locals);

  return fn;
}


typedef struct {
  int start, end;
} loop_range_t;

typedef struct {
  // these future blocks requested a wasm block to begin here, so that
  // forward edges can be implemented with a br or br_if
  int *requested;
} bb_creations_t;

typedef struct method_jit_ctx_s {
  // new order -> block index
  int *topo_to_block;
  // block index -> new order
  int *block_to_topo;
  // Mapping from topological # to the blocks (also in topo #) which requested that a block begin here.
  bb_creations_t *creations;
  // Mapping from topological block index to a loop bjvm_wasm_expression such
  // that backward edges should continue to this loop block.
  expression *loop_headers;
  // Mapping from topological block index to a block bjvm_wasm_expression such
  // that forward edges should break out of this block.
  expression *block_ends;
  // Now implementation stuffs
  int current_block_i;
  int topo_i;
  int loop_depth;
  // For each block in topological indexing, the number of loops that contain
  // it.
  int *loop_depths;
  int *visited, *incoming_count;
  int blockc;

  wasm_module *module;
  bytecode_insn *insn;
  int curr_pc;  // program counter
  int curr_sd;  // stack depth
} method_jit_ctx;

static _Thread_local method_jit_ctx* ctx;  // current ctx

static expression deopt() {

}

static expression get_local(int local_i) {
  return nullptr;
}

static expression get_stack_slot_of_type(int stack_i, type_kind tk) {
  return nullptr;
}

static expression get_stack(int stack_i) {
  return nullptr;
}

static expression get_stack_assert(int stack_i, wasm_value_type type) {
  return nullptr;
}

static expression npe_and_exit() {
  return nullptr;
}

static expression if_exception_exit() {
  return nullptr;
}

static expression get_descriptor(expression object) {
  return wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, object, 2, offsetof(obj_header, descriptor));
}

static void emit(expression expr) {

}

static void fail() {

}

static expression thread_param() {

}

static expression spill_oops(int max_stack_i) {

}

static expression reload_oops(int max_stack_i) {

}

static expression load_jit_entry(expression method) {

}

// Import the given function with the given signature. The function must be exported with EMSCRIPTEN_KEEPALIVE.
static expression upcall_impl(void *fn, const char *fn_name, const char *sig, expression *args) {

}

#define upcall(fn, sig, args) upcall_impl(&fn, #fn, sig, args)

static u32 get_method_func_type(cp_method *method) {
  return 0;
}

static wasm_value_type to_wasm_type(type_kind result) {

}

static expression branch_target(int pc) {

}

static expression do_exit() {

}

static expression set_stack(int stack_i, expression value, wasm_value_type type) {

}

static expression set_local(int local_i, expression value, wasm_value_type type) {

}

// Call where there is only one possible target.
static void lower_monomorphic_call(bytecode_insn *insn) {
  bool is_monomorphic_vtable = insn->kind == insn_invokevtable_monomorphic || insn->kind == insn_invokeitable_monomorphic;
  bool is_invokespecial = insn->kind == insn_invokespecial_resolved;
  bool is_invokestatic = insn->kind == insn_invokestatic_resolved;

  DCHECK(is_monomorphic_vtable || is_invokespecial || is_invokestatic);

  // For this instruction, we have to de-opt if the observed class descriptor is different from the IC descriptor.
  classdesc *ic = insn->ic;
  cp_method *method = insn->ic2;
  int argc = method_argc(method);

  expression if_null_then_npe = nullptr;
  expression if_cd_different_then_deopt = nullptr;
  if (!is_invokestatic) {
    expression receiver = get_stack(ctx->curr_sd - argc);
    expression is_null = wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, receiver);
    if_null_then_npe = wasm_if_else(ctx->module, is_null, npe_and_exit(), nullptr, wasm_void());

    if (is_monomorphic_vtable) {
      expression cd_different = wasm_binop(ctx->module, WASM_OP_KIND_REF_NE,
        get_descriptor(receiver), wasm_i32_const(ctx->module, (intptr_t) ic));
      if_cd_different_then_deopt = wasm_if_else(ctx->module, cd_different, deopt(), nullptr, wasm_void());
    }
  }

  // TODO make this a direct call using a funcref if the target JIT is stable
  expression method_const = wasm_i32_const(ctx->module, (intptr_t)method);

  expression args[259];
  int arg_i = 0;
  args[arg_i++] = thread_param();
  args[arg_i++] = method_const;
  for (int j = 0; j < argc; ++j) {
    args[arg_i++] = get_stack(ctx->curr_sd - argc + j);
  }
  DCHECK(argc < 256);

  u32 functype = get_method_func_type(method);

  expression do_call = wasm_call_indirect(ctx->module, 0,
    load_jit_entry(method_const), args, argc + 2, functype);

  type_kind result = field_to_kind(&method->descriptor->return_type);
  if (result != TYPE_KIND_VOID) {
    do_call = set_stack(ctx->curr_sd - argc, do_call, to_wasm_type(result));
  }

  if (if_null_then_npe) {
    emit(if_null_then_npe);
  }
  if (if_cd_different_then_deopt) {
    emit(if_cd_different_then_deopt);
  }
  emit(spill_oops(ctx->curr_sd - argc));
  emit(do_call);
  emit(if_exception_exit()); // TODO check nothrow
  emit(reload_oops(ctx->curr_sd - argc));
}

static void lower_vtable_call(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_invokevtable_polymorphic);

  int argc = insn->args;
  expression receiver = get_stack(ctx->curr_sd - argc);
  type_kind returns = field_to_kind(&insn->cp->methodref.descriptor->return_type);
  size_t vtable_i = (size_t)insn->ic2;

  // Look in classdesc->vtable.methods[vtable_i] for the method
  expression exit_on_npe = wasm_if_else(ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, receiver), npe_and_exit(), nullptr, wasm_void());

  expression method = get_descriptor(receiver);
  method = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, method, 0, offsetof(classdesc, vtable) + offsetof(vtable, methods));
  method = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, method, 2, (s32)(vtable_i * sizeof(void*)));

  expression args[259];
  int arg_i = 0;
  args[arg_i++] = thread_param();
  args[arg_i++] = method;
  for (int j = 0; j < argc; ++j) {
    args[arg_i++] = get_stack(ctx->curr_sd - argc + j);
  }

  emit(exit_on_npe);
  emit(spill_oops(ctx->curr_sd - insn->args));
  cp_method *resolved = insn->cp->methodref.resolved;
  expression do_call = wasm_call_indirect(ctx->module, 0, load_jit_entry(method), args, argc + 2,
    get_method_func_type(resolved));
  if (returns != TYPE_KIND_VOID) {
    do_call = set_stack(ctx->curr_sd - argc, do_call, to_wasm_type(returns));
  }
  emit(do_call);
  emit(if_exception_exit());
  emit(reload_oops(ctx->curr_sd - argc));
}

EMSCRIPTEN_KEEPALIVE
static cp_method *wasm_runtime_itable_lookup(vm_thread *thread, object target, classdesc *iface, size_t itable_i, cp_method *reference) {
  DCHECK(target && iface);
  cp_method *method = itable_lookup(target->descriptor, iface, itable_i);
  if (unlikely(!method)) {
    raise_abstract_method_error(thread, reference);
  }
}

static void lower_itable_call(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_invokeitable_polymorphic);
  // The logic here is painful so for now do an upcall to itable_lookup
  expression receiver = get_stack(ctx->curr_sd - insn->args);
  type_kind returns = field_to_kind(&insn->cp->methodref.descriptor->return_type);
  size_t itable_i = (size_t)insn->ic2;
  int argc = insn->args;

  expression exit_on_npe = wasm_if_else(ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, receiver), npe_and_exit(), nullptr, wasm_void());
  expression itable_lookup_args[5] = {
    thread_param(),
    receiver,
    wasm_i32_const(ctx->module, (intptr_t)insn->ic),
    wasm_i32_const(ctx->module, (intptr_t)itable_i),
    wasm_i32_const(ctx->module, (intptr_t)insn->cp->methodref.resolved)
  };
  expression found_method = get_stack_slot_of_type(ctx->curr_sd, WASM_TYPE_KIND_INT32);

  emit(exit_on_npe);
  // Known unused slot
  emit(set_stack(ctx->curr_sd, upcall(wasm_runtime_itable_lookup, "iiiiii", itable_lookup_args), WASM_TYPE_KIND_INT32));
  emit(if_exception_exit());  // abstract method error
  emit(spill_oops(ctx->curr_sd));

  expression args[259];
  int arg_i = 0;
  args[arg_i++] = thread_param();
  args[arg_i++] = found_method;
  for (int j = 0; j < argc; ++j) {
    args[arg_i++] = get_stack(ctx->curr_sd - argc + j);
  }

  expression do_call = wasm_call_indirect(ctx->module, 0, load_jit_entry(found_method),
    args, argc + 2, get_method_func_type(insn->cp->methodref.resolved));
  if (returns != TYPE_KIND_VOID) {
    do_call = set_stack(ctx->curr_sd - argc, do_call, to_wasm_type(returns));
  }
  emit(do_call);
  emit(if_exception_exit());
  emit(reload_oops(ctx->curr_sd - insn->args));
}

static void lower_invokecallsite(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_invokecallsite);

  struct native_CallSite *cs = insn->ic;
  struct native_MethodHandle *mh = (void *)cs->target;
  struct native_LambdaForm *form = (void *)mh->form;
  struct native_MemberName *name = (void *)form->vmentry;

  method_handle_kind kind = (name->flags >> 24) & 0xf;
  if (kind == MH_KIND_INVOKE_STATIC) {
    bool returns = form->result != -1;
    // Invoke name->vmtarget with arguments mh, args
    cp_method *invoke = name->vmtarget;
    expression method_const = wasm_i32_const(ctx->module, (intptr_t)invoke);

    // GC can move both the CallSite and MethodHandle around -- so always load it from the insn->ic which is a GC root
    expression get_mh = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD,
      wasm_i32_const(ctx->module, (intptr_t)&insn->ic), 0, 0);
    get_mh = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, get_mh, 0, offsetof(struct native_CallSite, target));

    expression args[259];
    int args_i = 0;
    args[args_i++] = thread_param();
    args[args_i++] = method_const;
    args[args_i++] = get_mh;
    for (int i = 0; i < insn->args; ++i) {
      args[args_i++] = get_stack(ctx->curr_sd - insn->args + i);
    }

    emit(spill_oops(ctx->curr_sd - insn->args));
    expression do_call = wasm_call_indirect(ctx->module, 0, load_jit_entry(method_const),
      args, insn->args + 3, get_method_func_type(invoke));
    if (returns) {
      wasm_value_type tk = to_wasm_type(field_to_kind(&invoke->descriptor->return_type));
      set_stack(ctx->curr_sd - insn->args, do_call, tk);
    }
    emit(do_call);
    emit(if_exception_exit());
    emit(reload_oops(ctx->curr_sd - insn->args));
  } else {
    UNREACHABLE();
  }
}

EMSCRIPTEN_KEEPALIVE
static object wasm_runtime_allocate_object(vm_thread *thread, classdesc *cd) {
  return AllocateObject(thread, cd, cd->instance_bytes);
}

static void lower_new_resolved(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_new_resolved);

  emit(spill_oops(ctx->curr_sd));
  expression args[2] = { thread_param(), wasm_i32_const(ctx->module, (intptr_t) insn->classdesc) };
  expression do_alloc = upcall(wasm_runtime_allocate_object, "iii", args);
  do_alloc = set_stack(ctx->curr_sd, do_alloc, WASM_TYPE_KIND_INT32);
  emit(do_alloc);
  emit(if_exception_exit());
  emit(reload_oops(ctx->curr_sd));
}

EMSCRIPTEN_KEEPALIVE
static bool wasm_runtime_instanceof(object o, classdesc *cd) {
  return o != nullptr && instanceof(o->descriptor, cd);
}

static void lower_instanceof_resolved(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_instanceof_resolved);  // instanceof(obj->descriptor, insn->classdesc)

  expression args[2] = {
    get_stack(ctx->curr_sd - 1),
    wasm_i32_const(ctx->module, (intptr_t) insn->classdesc)
  };
  expression check = upcall(wasm_runtime_instanceof, "iii", args);
  check = set_stack(ctx->curr_sd - 1, check, WASM_TYPE_KIND_INT32);
  emit(check);
}

EMSCRIPTEN_KEEPALIVE
static float wasm_runtime_frem(float a, float b) {
  return fmodf(a, b);
}

static void lower_frem(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_frem);
  expression right = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_FLOAT32);
  expression left = get_stack_assert(ctx->curr_sd - 2, WASM_TYPE_KIND_FLOAT32);
  expression args[2] = {left, right};
  expression call = upcall(wasm_runtime_frem, "fff", args);
  emit(set_stack(ctx->curr_sd - 2, call, WASM_TYPE_KIND_FLOAT32));
}

static void lower_drem(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_drem);
  expression right = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_FLOAT64);
  expression left = get_stack_assert(ctx->curr_sd - 2, WASM_TYPE_KIND_FLOAT64);
  expression args[2] = {left, right};
  expression call = upcall(wasm_runtime_frem, "ddd", args);
  emit(set_stack(ctx->curr_sd - 2, call, WASM_TYPE_KIND_FLOAT64));
}

EMSCRIPTEN_KEEPALIVE
static void wasm_runtime_throw_div0(vm_thread *thread) {
  raise_div0_arithmetic_exception(thread);
}

static void lower_integral_div_rem(bytecode_insn *insn) {
  bool is_div = insn->kind == insn_ldiv || insn->kind == insn_idiv;
  DCHECK(is_div || insn->kind == insn_lrem || insn->kind == insn_irem);
  bool is_long = insn->kind == insn_ldiv || insn->kind == insn_lrem;

  wasm_value_type type = is_long ? WASM_OP_KIND_I64_EQ : WASM_OP_KIND_I32_EQ;

  // Two cases: 0 and INT_MIN / -1. This can be simplified to b == 0 ? div0 : b == -1 ? -a : a / b
  expression right = get_stack_assert(ctx->curr_sd - 1, type);
  expression left = get_stack_assert(ctx->curr_sd - 2, type);

  expression div0_args[1] = { thread_param() };
  expression div0_steps[3] = {
    spill_oops(0),
    upcall(wasm_runtime_throw_div0, "vi", div0_args),
    do_exit()
  };
  expression div0 = wasm_block(ctx->module, div0_steps, 3, wasm_void(), false);
  expression if_zero_div0 = wasm_if_else(ctx->module,
    wasm_binop(ctx->module, WASM_OP_KIND_I32_EQ, right, wasm_i32_const(ctx->module, 0)),
    div0, nullptr, wasm_void());


  expression denom_is_neg1 = wasm_binop(ctx->module, type, right,
    is_long ? wasm_i64_const(ctx->module, -1) : wasm_i32_const(ctx->module, -1));
  expression negate_numerator = wasm_binop(ctx->module, is_long ? WASM_OP_KIND_I64_SUB : WASM_OP_KIND_I32_SUB,
    is_long ? wasm_i64_const(ctx->module, 0) : wasm_i32_const(ctx->module, 0), left);
  expression do_div = wasm_binop(ctx->module, is_long ? WASM_OP_KIND_I64_DIV_S : WASM_OP_KIND_I32_DIV_S, left, right);
  expression division = wasm_if_else(ctx->module, denom_is_neg1, negate_numerator,
    do_div, is_long ? wasm_int64() : wasm_int32());
  division = set_stack(ctx->curr_sd - 2, division, type);

  emit(if_zero_div0);
  emit(division);
}

static void lower_direct_binop(bytecode_insn *insn) {
  struct binop_entry {
    wasm_value_type lhs, rhs, result;
    wasm_binary_op_kind op;
  };

#define CASE(insn, lhs, rhs, result, op) \
  [insn] = {WASM_TYPE_KIND_##lhs, WASM_TYPE_KIND_##rhs, WASM_TYPE_KIND_##result, WASM_OP_KIND_##op}

  struct binop_entry tbl[] = {
    CASE(insn_ladd, INT64, INT64, INT64, I64_ADD),
    CASE(insn_lsub, INT64, INT64, INT64, I64_SUB),
    CASE(insn_lmul, INT64, INT64, INT64, I64_MUL),
    CASE(insn_land, INT64, INT64, INT64, I64_AND),
    CASE(insn_lor, INT64, INT64, INT64, I64_OR),
    CASE(insn_lxor, INT64, INT64, INT64, I64_XOR),
    CASE(insn_iadd, INT32, INT32, INT32, I32_ADD),
    CASE(insn_isub, INT32, INT32, INT32, I32_SUB),
    CASE(insn_imul, INT32, INT32, INT32, I32_MUL),
    CASE(insn_iand, INT32, INT32, INT32, I32_AND),
    CASE(insn_ior, INT32, INT32, INT32, I32_OR),
    CASE(insn_ixor, INT32, INT32, INT32, I32_XOR),
    CASE(insn_iushr, INT32, INT32, INT32, I32_SHR_U),
    CASE(insn_ishr, INT32, INT32, INT32, I32_SHR_S),
    CASE(insn_ishl, INT32, INT32, INT32, I32_SHL),
    CASE(insn_fadd, FLOAT32, FLOAT32, FLOAT32, F32_ADD),
    CASE(insn_fsub, FLOAT32, FLOAT32, FLOAT32, F32_SUB),
    CASE(insn_fmul, FLOAT32, FLOAT32, FLOAT32, F32_MUL),
    CASE(insn_fdiv, FLOAT32, FLOAT32, FLOAT32, F32_DIV),
    CASE(insn_dadd, FLOAT64, FLOAT64, FLOAT64, F64_ADD),
    CASE(insn_dsub, FLOAT64, FLOAT64, FLOAT64, F64_SUB),
    CASE(insn_dmul, FLOAT64, FLOAT64, FLOAT64, F64_MUL),
    CASE(insn_ddiv, FLOAT64, FLOAT64, FLOAT64, F64_DIV)
  };

#undef CASE

  struct binop_entry entry = tbl[insn->kind];
}

static void lower_long_shiftop(bytecode_insn *insn) {

}

static void lower_direct_unop(bytecode_insn *insn) {
  struct unop_entry {
    wasm_value_type operand, result;
    wasm_unary_op_kind op;
  };

#define CASE(insn, operand, result, op) \
  [insn] = {WASM_TYPE_KIND_##operand, WASM_TYPE_KIND_##result, WASM_OP_KIND_##op}

  struct unop_entry tbl[] = {
    CASE(insn_d2f, FLOAT64, FLOAT32, F32_DEMOTE_F64),
    CASE(insn_d2i, FLOAT64, INT32, I32_TRUNC_SAT_F64_S),
    CASE(insn_d2l, FLOAT64, INT64, I64_TRUNC_SAT_F64_S),
    CASE(insn_f2d, FLOAT32, FLOAT64, F64_PROMOTE_F32),
    CASE(insn_f2i, FLOAT32, INT32, I32_TRUNC_SAT_F32_S),
    CASE(insn_f2l, FLOAT32, INT64, I64_TRUNC_SAT_F32_S),
    CASE(insn_i2b, INT32, INT32, I32_EXTEND_S_I8),
    CASE(insn_i2d, INT32, FLOAT64, F64_CONVERT_S_I32),
    CASE(insn_i2f, INT32, FLOAT32, F32_CONVERT_S_I32),
    CASE(insn_i2l, INT32, INT64, I64_EXTEND_S_I32),
    CASE(insn_i2s, INT32, INT32, I32_EXTEND_S_I16),
    CASE(insn_l2d, INT64, FLOAT64, F64_CONVERT_S_I64),
    CASE(insn_l2f, INT64, FLOAT32, F32_CONVERT_S_I64),
    CASE(insn_l2i, INT64, INT32, I32_WRAP_I64)
  };
#undef CASE
}

static void lower_ineg(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_ineg);
  expression value = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  expression neg = wasm_binop(ctx->module, WASM_OP_KIND_I32_SUB, wasm_i32_const(ctx->module, 0), value);
  emit(set_stack(ctx->curr_sd - 1, neg, WASM_TYPE_KIND_INT32));
}

static void lower_lneg(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_lneg);
  expression value = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT64);
  expression neg = wasm_binop(ctx->module, WASM_OP_KIND_I64_SUB, wasm_i64_const(ctx->module, 0), value);
  emit(set_stack(ctx->curr_sd - 1, neg, WASM_TYPE_KIND_INT64));
}

static void lower_i2c(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_i2c);
  expression value = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  expression extend = wasm_binop(ctx->module, WASM_OP_KIND_I32_AND, value, wasm_i32_const(ctx->module, 0xffff));
  extend = set_stack(ctx->curr_sd - 1, extend, WASM_TYPE_KIND_INT32);
  emit(extend);
}

static void lower_arraylength(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_arraylength);
  expression array = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  expression if_npe = wasm_if_else(ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, array), npe_and_exit(), nullptr, wasm_void());
  expression length = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, array, 0, kArrayLengthOffset);
  length = set_stack(ctx->curr_sd - 1, length, WASM_TYPE_KIND_INT32);
  emit(if_npe);
  emit(length);
}

EMSCRIPTEN_KEEPALIVE
static void wasm_runtime_array_oob(vm_thread *thread, int index, int length) {
  raise_array_index_oob_exception(thread, index, length);
}

static void lower_array_load_store(bytecode_insn *insn) {
  wasm_load_op_kind load_op;
  wasm_store_op_kind store_op;
  type_kind data_type;
  switch (insn->kind) {
  case insn_iaload:
  case insn_iastore:
    load_op = WASM_OP_KIND_I32_LOAD;
    store_op = WASM_OP_KIND_I32_STORE;
    data_type = TYPE_KIND_INT;
    break;
  case insn_laload:
  case insn_lastore:
    load_op = WASM_OP_KIND_I64_LOAD;
    store_op = WASM_OP_KIND_I64_STORE;
    data_type = TYPE_KIND_LONG;
    break;
  case insn_faload:
  case insn_fastore:
    load_op = WASM_OP_KIND_F32_LOAD;
    store_op = WASM_OP_KIND_F32_STORE;
    data_type = TYPE_KIND_FLOAT;
    break;
  case insn_daload:
  case insn_dastore:
    load_op = WASM_OP_KIND_F64_LOAD;
    store_op = WASM_OP_KIND_F64_STORE;
    data_type = TYPE_KIND_DOUBLE;
    break;
  case insn_aaload:
  case insn_aastore:
    load_op = WASM_OP_KIND_I32_LOAD;
    store_op = WASM_OP_KIND_I32_STORE;
    data_type = TYPE_KIND_REFERENCE;
    break;
  case insn_baload:
  case insn_bastore:
    load_op = WASM_OP_KIND_I32_LOAD8_S;
    store_op = WASM_OP_KIND_I32_STORE8;
    data_type = TYPE_KIND_BYTE;
    break;
  case insn_caload:
  case insn_castore:
    load_op = WASM_OP_KIND_I32_LOAD16_U;
    store_op = WASM_OP_KIND_I32_STORE16;
    data_type = TYPE_KIND_CHAR;
    break;
  case insn_saload:
  case insn_sastore:
    load_op = WASM_OP_KIND_I32_LOAD16_S;
    store_op = WASM_OP_KIND_I32_STORE16;
    data_type = TYPE_KIND_SHORT;
    break;
  default:
    UNREACHABLE();
  }

  bool is_load = insn->kind == insn_iaload || insn->kind == insn_laload || insn->kind == insn_faload || insn->kind == insn_daload || insn->kind == insn_aaload ||
    insn->kind == insn_baload || insn->kind == insn_caload || insn->kind == insn_saload;

  expression array = get_stack_assert(ctx->curr_sd - 2 - !is_load, WASM_TYPE_KIND_INT32);
  expression index = get_stack_assert(ctx->curr_sd - 1 - !is_load, WASM_TYPE_KIND_INT32);
  expression null_check = wasm_if_else(ctx->module,
    wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, array),
    npe_and_exit(), nullptr, wasm_void());

  expression length = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, array, 0, kArrayLengthOffset);

  expression oob_args[3] = {thread_param(), index, length};
  expression oob_steps[3] = {
    spill_oops(0),
    upcall(wasm_runtime_array_oob, "viii", oob_args),
    do_exit()
  };

  expression oob_block = wasm_block(ctx->module, oob_steps, 3, wasm_void(), false);
  expression index_check = wasm_if_else(ctx->module,
    wasm_binop(ctx->module, WASM_OP_KIND_I32_GE_U, index, length),  // unsigned compare
    oob_block, nullptr, wasm_void());

  expression size_bytes = wasm_i32_const(ctx->module, sizeof_type_kind(data_type));
  expression addr = wasm_binop(ctx->module, WASM_OP_KIND_I32_ADD, array,
    wasm_binop(ctx->module, WASM_OP_KIND_I32_MUL, index, size_bytes));

  emit(null_check);
  emit(index_check);
  if (is_load) {
    expression load = wasm_load(ctx->module, load_op, addr, 0, kArrayDataOffset);
    load = set_stack(ctx->curr_sd - 2, load, to_wasm_type(data_type));
    emit(load);
  } else {
    expression value = get_stack_assert(ctx->curr_sd - 3, to_wasm_type(data_type));
    // TODO ArrayStoreException
    emit(wasm_store(ctx->module, store_op, addr, value, 0, kArrayDataOffset));
  }
}

static void lower_fused_compare(bytecode_insn *insn) {
  // fcmpg(a, b): a > b ? 1 : (a < b ? -1 : (a == b ? 0 : 1))
  // fcmpl(a, b): a > b ? 1 : (a < b ? -1 : (a == b ? 0 : -1))
  // Hence fcmpg returns 1 on NaN and fcmpl returns -1 on NaN.
  // For float compares we need to decide 1. which comparison to use and 2. whether to flip the branches, because
  // !(a >= b) is not equivalent to a < b in the presence of NaNs.

  bool fp_compare = insn->kind == insn_fcmpg || insn->kind == insn_fcmpl ||
    insn->kind == insn_dcmpg || insn->kind == insn_dcmpl;
  DCHECK(fp_compare || insn->kind == insn_lcmp);

  bytecode_insn *branch = insn + 1;
  CHECK(branch->kind == insn_iflt || branch->kind == insn_ifge || branch->kind == insn_ifgt || branch->kind == insn_ifle ||
    branch->kind == insn_ifeq || branch->kind == insn_ifne);

  bool taken_a_lt_b = branch->kind == insn_iflt || branch->kind == insn_ifle || branch->kind == insn_ifeq;
  bool taken_a_eq_b = branch->kind == insn_ifeq || branch->kind == insn_ifge || branch->kind == insn_ifle;
  bool taken_a_gt_b = branch->kind == insn_ifgt || branch->kind == insn_ifge || branch->kind == insn_ifne;
  bool taken_unordered = (branch->kind == insn_fcmpg || branch->kind == insn_dcmpg) ? taken_a_gt_b : taken_a_lt_b;

  // Based on this table we can then select the right operation and branch ordering. Because all comparisons are false
  // when at least one operand is NaN, the branches have to be flipped iff taken_unordered is true.
  if (taken_unordered) {  // swap to account for the branches which will be flipped
    int tmp = taken_a_lt_b;
    taken_a_lt_b = taken_a_gt_b;
    taken_a_gt_b = tmp;
  }

  wasm_binary_op_kind op;
  if (taken_a_lt_b && taken_a_eq_b) { // we'll adjust f32 compares to f64 compares
    op = fp_compare ? WASM_OP_KIND_F32_LE : WASM_OP_KIND_I64_LE_S;
  } else if (taken_a_lt_b && !taken_a_eq_b) {
    op = fp_compare ? WASM_OP_KIND_F32_LT : WASM_OP_KIND_I64_LT_S;
  } else if (taken_a_gt_b && taken_a_eq_b) {
    op = fp_compare ? WASM_OP_KIND_F32_GE : WASM_OP_KIND_I64_GE_S;
  } else if (taken_a_gt_b && !taken_a_eq_b) {
    op = fp_compare ? WASM_OP_KIND_F32_GT : WASM_OP_KIND_I64_GT_S;
  } else if (taken_a_eq_b) {
    op = fp_compare ? WASM_OP_KIND_F32_EQ : WASM_OP_KIND_I64_EQ;
  } else {
    op = fp_compare ? WASM_OP_KIND_F32_NE : WASM_OP_KIND_I64_NE;
  }

  if (fp_compare && (insn->kind == insn_dcmpg || insn->kind == insn_dcmpl)) {
    // Adjust the operation to use f64 instead of f32
    op += WASM_OP_KIND_F64_EQ - WASM_OP_KIND_F32_EQ;
  }

  expression cmp = wasm_binop(ctx->module, op, get_stack(ctx->curr_sd - 2), get_stack(ctx->curr_sd - 1));
  expression taken = branch_target(insn->index);
  expression not_taken = branch_target(ctx->curr_pc + 2);
  if (taken_unordered) {
    expression tmp = taken;
    taken = not_taken;
    not_taken = tmp;
  }
  emit(wasm_br(ctx->module, cmp, taken));
  emit(wasm_br(ctx->module, nullptr, not_taken));
}

EMSCRIPTEN_KEEPALIVE
static bool wasm_runtime_checkcast(vm_thread *thread, object o, classdesc *cd) {
  if (o == nullptr || instanceof(o->descriptor, cd))
    return false;
  raise_class_cast_exception(thread, o->descriptor, cd);
  return true;
}

static void lower_checkcast_resolved(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_checkcast_resolved);  // instanceof(obj->descriptor, insn->classdesc)
  expression receiver = get_stack(ctx->curr_sd - 1);
  expression args[3] = {
    thread_param(),
    receiver,
    wasm_i32_const(ctx->module, (intptr_t) insn->classdesc)
  };
  expression check = upcall(wasm_runtime_checkcast, "iiii", args);
  check = wasm_if_else(ctx->module, check, do_exit(), nullptr, wasm_void());
  emit(spill_oops(0));
  emit(check);
}

static int lower_ldc(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_ldc2_w || insn->kind == insn_ldc);
  if (insn->kind == insn_ldc2_w) {
    UNREACHABLE();  // should have been removed at analysis time and converted to dconst or lconst
  }

  cp_entry *ent = insn->cp;
  if (ent->kind == CP_KIND_STRING) {
    if (!ent->string.interned)
      return -1;
    expression load_string = wasm_i32_const(ctx->module, (intptr_t)&ent->string.interned);
    load_string = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, load_string, 0, 0);
    load_string = set_stack(ctx->curr_sd, load_string, WASM_TYPE_KIND_INT32);
    emit(load_string);
  } else if (ent->kind == CP_KIND_CLASS) {
    if (!ent->class_info.vm_object)
      return -1;

    expression load_class = wasm_i32_const(ctx->module, (intptr_t)&ent->class_info.vm_object);
    load_class = wasm_load(ctx->module, WASM_OP_KIND_I32_LOAD, load_class, 0, 0);
    load_class = set_stack(ctx->curr_sd, load_class, WASM_TYPE_KIND_INT32);
    emit(load_class);
  }
}

static void lower_constant(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_aconst_null || insn->kind == insn_iconst || insn->kind == insn_dconst ||
    insn->kind == insn_fconst || insn->kind == insn_lconst);
  switch (insn->kind) {
  case insn_aconst_null:
    emit(set_stack(ctx->curr_sd, wasm_i32_const(ctx->module, 0), WASM_TYPE_KIND_INT32));
    break;
  case insn_iconst:
    emit(set_stack(ctx->curr_sd, wasm_i32_const(ctx->module, (int)insn->integer_imm), WASM_TYPE_KIND_INT32));
    break;
  case insn_lconst:
    emit(set_stack(ctx->curr_sd, wasm_i64_const(ctx->module, insn->integer_imm), WASM_TYPE_KIND_INT64));
    break;
  case insn_fconst:
    emit(set_stack(ctx->curr_sd, wasm_f32_const(ctx->module, insn->f_imm), WASM_TYPE_KIND_FLOAT32));
    break;
  case insn_dconst:
    emit(set_stack(ctx->curr_sd, wasm_f64_const(ctx->module, insn->d_imm), WASM_TYPE_KIND_FLOAT64));
    break;
  }
}

static void lower_return(bytecode_insn *insn) {
  switch (insn->kind) {
  case insn_return:
    emit(do_exit());
    break;
  default:
    emit(wasm_return(ctx->module, get_stack(ctx->curr_sd - 1)));
    break;
  }
}

static void lower_athrow(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_athrow);
  expression exception = get_stack(ctx->curr_sd - 1);
  // Store to thread->current_exception
  expression store_exception = wasm_store(ctx->module, WASM_OP_KIND_I32_STORE, thread_param(), exception, 0, offsetof(vm_thread, current_exception));
  emit(store_exception);
}

static void lower_dneg(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_dneg);
  expression value = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_FLOAT64);
  expression neg = wasm_binop(ctx->module, WASM_OP_KIND_F64_SUB, wasm_f64_const(ctx->module, 0), value);
  emit(set_stack(ctx->curr_sd - 1, neg, WASM_TYPE_KIND_FLOAT64));
}

static void lower_fneg(bytecode_insn *insn) {
  DCHECK(insn->kind == insn_fneg);
  expression value = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_FLOAT32);
  expression neg = wasm_binop(ctx->module, WASM_OP_KIND_F32_SUB, wasm_f32_const(ctx->module, 0), value);
  emit(set_stack(ctx->curr_sd - 1, neg, WASM_TYPE_KIND_FLOAT32));
}

static void lower_stack_manipulation(bytecode_insn *insn) {

}

static void lower_branch(bytecode_insn *insn) {

}

void lower_get_put_resolved(bytecode_insn *insn) {
  wasm_load_op_kind load_op;
  wasm_store_op_kind store_op;
  type_kind type;

  bool is_getfield = insn->kind >= insn_getfield_B && insn->kind <= insn_getfield_L;
  bool is_putfield = insn->kind >= insn_putfield_B && insn->kind <= insn_getfield_L;
  bool is_getstatic = insn->kind >= insn_getstatic_B && insn->kind <= insn_getstatic_L;
  bool is_putstatic = insn->kind >= insn_putstatic_B && insn->kind <= insn_putstatic_L;

  switch (insn->kind) {
  default: UNREACHABLE();
  case insn_getfield_B:
  case insn_putfield_B:
  case insn_getstatic_B:
  case insn_putstatic_B:
    load_op = WASM_OP_KIND_I32_LOAD8_S;
    store_op = WASM_OP_KIND_I32_STORE8;
    type = TYPE_KIND_BYTE;
    break;
  case insn_getfield_C:
  case insn_putfield_C:
  case insn_getstatic_C:
  case insn_putstatic_C:
    load_op = WASM_OP_KIND_I32_LOAD16_U;
    store_op = WASM_OP_KIND_I32_STORE16;
    type = TYPE_KIND_CHAR;
    break;
  case insn_getfield_S:
  case insn_putfield_S:
  case insn_getstatic_S:
  case insn_putstatic_S:
    load_op = WASM_OP_KIND_I32_LOAD16_S;
    store_op = WASM_OP_KIND_I32_STORE16;
    type = TYPE_KIND_SHORT;
    break;
  case insn_getfield_I:
  case insn_putfield_I:
  case insn_getstatic_I:
  case insn_putstatic_I:
    load_op = WASM_OP_KIND_I32_LOAD;
    store_op = WASM_OP_KIND_I32_STORE;
    type = TYPE_KIND_INT;
    break;
  case insn_getfield_J:
  case insn_putfield_J:
  case insn_getstatic_J:
  case insn_putstatic_J:
    load_op = WASM_OP_KIND_I64_LOAD;
    store_op = WASM_OP_KIND_I64_STORE;
    type = TYPE_KIND_LONG;
    break;
  case insn_getfield_F:
  case insn_putfield_F:
  case insn_getstatic_F:
  case insn_putstatic_F:
    load_op = WASM_OP_KIND_F32_LOAD;
    store_op = WASM_OP_KIND_F32_STORE;
    type = TYPE_KIND_FLOAT;
    break;
  case insn_getfield_D:
  case insn_putfield_D:
  case insn_getstatic_D:
  case insn_putstatic_D:
    load_op = WASM_OP_KIND_F64_LOAD;
    store_op = WASM_OP_KIND_F64_STORE;
    type = TYPE_KIND_DOUBLE;
    break;
  case insn_getfield_L:
  case insn_putfield_L:
  case insn_getstatic_L:
  case insn_putstatic_L:
    load_op = WASM_OP_KIND_I32_LOAD;
    store_op = WASM_OP_KIND_I32_STORE;
    type = TYPE_KIND_REFERENCE;
    break;
  }

  expression addr;
  int offset;
  if (is_putfield || is_getfield) {
    expression receiver = get_stack_assert(ctx->curr_sd - 1 - is_putfield, WASM_TYPE_KIND_INT32);
    expression if_null_npe = wasm_if_else(ctx->module,
      wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, receiver), npe_and_exit(), nullptr, wasm_void());
    emit(if_null_npe);
    addr = receiver;
    offset = (int)insn->ic2;
  } else {
    addr = wasm_i32_const(ctx->module, (int)(intptr_t)insn->ic);
    offset = 0;
  }

  if (is_putfield || is_putstatic) {
    emit(wasm_store(ctx->module, store_op, addr, get_stack(ctx->curr_sd - 1), 0, offset));
  } else {
    int store_to = is_getstatic ? ctx->curr_sd : ctx->curr_sd - 1 - is_putfield;
    emit(set_stack(store_to, wasm_load(ctx->module, load_op, addr, 0, offset), to_wasm_type(type)));
  }
}

void lower_iinc(bytecode_insn *insn) {
  expression expr = get_local(insn->iinc.index);
  expr = wasm_binop(ctx->module, WASM_OP_KIND_I32_ADD, expr, wasm_i32_const(ctx->module, insn->iinc.const_));
  emit(set_local(insn->iinc.index, expr, WASM_TYPE_KIND_INT32));
}

void lower_local_load(bytecode_insn * insn) {
  wasm_value_type type;
  switch (insn->kind) {
  case insn_dload:
    type = WASM_TYPE_KIND_FLOAT64;
    break;
  case insn_fload:
    type = WASM_TYPE_KIND_FLOAT32;
    break;
  case insn_iload:
    type = WASM_TYPE_KIND_INT32;
    break;
  case insn_lload:
    type = WASM_TYPE_KIND_INT64;
    break;
  default:
    UNREACHABLE();
  }
  emit(set_stack(ctx->curr_sd, get_local(insn->index), type));
}

void lower_local_store(bytecode_insn * insn) {
  wasm_value_type type;
  switch (insn->kind) {
  case insn_dstore:
    type = WASM_TYPE_KIND_FLOAT64;
    break;
  case insn_fstore:
    type = WASM_TYPE_KIND_FLOAT32;
    break;
  case insn_istore:
    type = WASM_TYPE_KIND_INT32;
    break;
  case insn_lstore:
    type = WASM_TYPE_KIND_INT64;
    break;
  default:
    UNREACHABLE();
  }
  emit(set_local(insn->index, get_stack(ctx->curr_sd - 1), type));
}

EMSCRIPTEN_KEEPALIVE
obj_header *wasm_runtime_newarray(vm_thread *thread, type_kind array_type, s32 count) {
  if (unlikely(count < 0)) {
    raise_negative_array_size_exception(thread, count);
    return nullptr;
  }
  obj_header *array = CreatePrimitiveArray1D(thread, array_type, count);
  return array;
}

void lower_newarray(bytecode_insn *insn) {
  emit(spill_oops(0));
  expression count = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  expression args[3] = {thread_param(), wasm_i32_const(ctx->module, insn->array_type), count};
  expression newarray = upcall(wasm_runtime_newarray, "iiii", args);
  emit(set_stack(ctx->curr_sd - 1, newarray, WASM_TYPE_KIND_INT32));

  newarray = get_stack_slot_of_type(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  // Return immediately if null
  emit(wasm_if_else(ctx->module, wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, newarray), do_exit(), nullptr, wasm_void()));
}

EMSCRIPTEN_KEEPALIVE
obj_header *wasm_runtime_anewarray(vm_thread *thread, classdesc *type, s32 count) {
  if (count < 0) {
    raise_negative_array_size_exception(thread, count);
    return nullptr;
  }
  obj_header *array = CreateObjectArray1D(thread, type, count);
  return array;
}

void lower_anewarray_resolved(bytecode_insn *insn) {
  emit(spill_oops(0));
  expression count = get_stack_assert(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  expression args[3] = {thread_param(), wasm_i32_const(ctx->module, (intptr_t)insn->classdesc), count};
  expression newarray = upcall(wasm_runtime_anewarray, "iiii", args);
  emit(set_stack(ctx->curr_sd - 1, newarray, WASM_TYPE_KIND_INT32));

  newarray = get_stack_slot_of_type(ctx->curr_sd - 1, WASM_TYPE_KIND_INT32);
  // Return immediately if null
  emit(wasm_if_else(ctx->module, wasm_unop(ctx->module, WASM_OP_KIND_REF_EQZ, newarray), do_exit(), nullptr, wasm_void()));
}

static int lower_instruction(bytecode_insn *insn) {
  switch (insn->kind) {
  case insn_nop:
    UNREACHABLE();
  case insn_aaload:
  case insn_aastore:
  case insn_baload:
  case insn_bastore:
  case insn_caload:
  case insn_castore:
  case insn_daload:
  case insn_dastore:
  case insn_faload:
  case insn_fastore:
  case insn_iaload:
  case insn_iastore:
  case insn_laload:
  case insn_lastore:
  case insn_saload:
  case insn_sastore:
    lower_array_load_store(insn);
    return 0;
  case insn_aconst_null:
    lower_constant(insn);
    return 0;
  case insn_areturn:
  case insn_dreturn:
  case insn_freturn:
  case insn_ireturn:
  case insn_lreturn:
  case insn_return:
    lower_return(insn);
    return 0;
  case insn_arraylength:
    lower_arraylength(insn);
    return 0;
  case insn_athrow:
    lower_athrow(insn);
    return 0;
  case insn_d2f:
  case insn_d2i:
  case insn_d2l:
  case insn_f2d:
  case insn_f2i:
  case insn_f2l:
  case insn_i2b:
  case insn_i2d:
  case insn_i2f:
  case insn_i2l:
  case insn_i2s:
  case insn_ior:
  case insn_l2d:
  case insn_l2f:
  case insn_l2i:
  case insn_sqrt:
    lower_direct_unop(insn);
    return 0;
  case insn_dadd:
  case insn_ddiv:
  case insn_dmul:
  case insn_dsub:
  case insn_fadd:
  case insn_fdiv:
  case insn_fmul:
  case insn_fsub:
  case insn_iadd:
  case insn_imul:
  case insn_ishl:
  case insn_ishr:
  case insn_isub:
  case insn_iushr:
  case insn_ixor:
  case insn_ladd:
  case insn_land:
  case insn_lmul:
  case insn_lor:
  case insn_lsub:
  case insn_lxor:
    lower_direct_binop(insn);
    return 0;
  case insn_dcmpg:
  case insn_dcmpl:
  case insn_fcmpg:
  case insn_fcmpl:
  case insn_lcmp:
    lower_fused_compare(insn);
    return 0;
  case insn_drem:
    lower_frem(insn);
    return 0;
  case insn_dneg:
    lower_dneg(insn);
    return 0;
  case insn_dup:
  case insn_dup_x1:
  case insn_dup_x2:
  case insn_dup2:
  case insn_dup2_x1:
  case insn_dup2_x2:
  case insn_swap:
  case insn_pop:
  case insn_pop2:
    lower_stack_manipulation(insn);
    return 0;
  case insn_fneg:
    lower_fneg(insn);
    return 0;
  case insn_frem:
    lower_frem(insn);
    return 0;
  case insn_i2c:
    lower_i2c(insn);
    return 0;
  case insn_idiv:
  case insn_irem:
  case insn_ldiv:
  case insn_lrem:
    lower_integral_div_rem(insn);
    return 0;
  case insn_ineg:
    lower_ineg(insn);
    return 0;
  case insn_lneg:
    lower_lneg(insn);
    return 0;
  case insn_lshl:
  case insn_lshr:
  case insn_lushr:
    lower_long_shiftop(insn);
    return 0;
  case insn_monitorenter:
  case insn_monitorexit:
    break;
  case insn_anewarray:
  case insn_checkcast:
  case insn_getfield:
  case insn_getstatic:
  case insn_instanceof:
  case insn_invokedynamic:
  case insn_new:
  case insn_putfield:
  case insn_putstatic:
  case insn_invokevirtual:
  case insn_invokespecial:
  case insn_invokestatic:
    break;
  case insn_ldc:
  case insn_ldc2_w:
    if (lower_ldc(insn))
      break;
    return 0;
  case insn_dload:
  case insn_fload:
  case insn_iload:
  case insn_lload:
  case insn_aload:
    lower_local_load(insn);
    return 0;
  case insn_dstore:
  case insn_fstore:
  case insn_istore:
  case insn_lstore:
  case insn_astore:
    lower_local_store(insn);
    return 0;
  case insn_goto:
  case insn_if_acmpeq:
  case insn_if_acmpne:
  case insn_if_icmpeq:
  case insn_if_icmpne:
  case insn_if_icmplt:
  case insn_if_icmpge:
  case insn_if_icmpgt:
  case insn_if_icmple:
  case insn_ifeq:
  case insn_ifne:
  case insn_iflt:
  case insn_ifge:
  case insn_ifgt:
  case insn_ifle:
  case insn_ifnonnull:
  case insn_ifnull:
    lower_branch(insn);
  case insn_jsr:
    break;
  case insn_iconst:
  case insn_dconst:
  case insn_fconst:
  case insn_lconst:
    lower_constant(insn);
    return 0;
  case insn_iinc:
    lower_iinc(insn);
    return 0;
  case insn_invokeinterface:
    break;
  case insn_multianewarray:  // TODO implement this (probably one runtime fn for each # of dimensions + fallback)
    break;
  case insn_newarray:
    lower_newarray(insn);
    return 0;
  case insn_tableswitch: // TODO
    //lower_tableswitch(insn);
    break;
  case insn_lookupswitch:
    //lower_lookupswitch(insn);
    break;
  case insn_ret:
    break;
  case insn_anewarray_resolved:
    lower_anewarray_resolved(insn);
    return 0;
  case insn_checkcast_resolved:
    lower_checkcast_resolved(insn);
    return 0;
  case insn_instanceof_resolved:
    lower_instanceof_resolved(insn);
    return 0;
  case insn_new_resolved:
    lower_new_resolved(insn);
    return 0;
  case insn_invokevtable_monomorphic:
  case insn_invokeitable_monomorphic:
  case insn_invokespecial_resolved:
  case insn_invokestatic_resolved:
    lower_monomorphic_call(insn);
    return 0;
  case insn_invokevtable_polymorphic:
    lower_vtable_call(insn);
    return 0;
  case insn_invokeitable_polymorphic:
    lower_itable_call(insn);
    return 0;
  case insn_invokecallsite:
    lower_invokecallsite(insn);
    return 0;
  case insn_invokesigpoly:
    break;
  case insn_getfield_B:
  case insn_getfield_C:
  case insn_getfield_S:
  case insn_getfield_I:
  case insn_getfield_J:
  case insn_getfield_F:
  case insn_getfield_D:
  case insn_getfield_Z:
  case insn_getfield_L:
  case insn_putfield_B:
  case insn_putfield_C:
  case insn_putfield_S:
  case insn_putfield_I:
  case insn_putfield_J:
  case insn_putfield_F:
  case insn_putfield_D:
  case insn_putfield_Z:
  case insn_putfield_L:
    lower_get_put_resolved(insn);
    return 0;
  case insn_getstatic_B:
  case insn_getstatic_C:
  case insn_getstatic_S:
  case insn_getstatic_I:
  case insn_getstatic_J:
  case insn_getstatic_F:
  case insn_getstatic_D:
  case insn_getstatic_Z:
  case insn_getstatic_L:
  case insn_putstatic_B:
  case insn_putstatic_C:
  case insn_putstatic_S:
  case insn_putstatic_I:
  case insn_putstatic_J:
  case insn_putstatic_F:
  case insn_putstatic_D:
  case insn_putstatic_Z:
  case insn_putstatic_L:
    lower_get_put_resolved(insn);
    return 0;
  }

  // De-opt if we can't lower this instruction
  emit(deopt());
  return -1;
}

void free_topo_ctx(method_jit_ctx ctx) {
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

void find_block_insertion_points(code_analysis *analy, method_jit_ctx *ctx) {
  // For each bb, if there is a bb preceding it (in the topological sort)
  // which branches to that bb, which is NOT its immediate predecessor in the
  // sort, we need to introduce a wasm (block) to handle the branch. We place
  // it at the point in the topo sort where the depth becomes equal to the
  // depth of the topological predecessor of bb.
  for (int i = 0; i < analy->block_count; ++i) {
    basic_block *b = analy->blocks + i;
    for (int j = 0; j < arrlen(b->prev); ++j) {
      int prev = b->prev[j];
      if (ctx->block_to_topo[prev] >= ctx->block_to_topo[i] - 1)
        continue;
      bb_creations_t *creations = ctx->creations + ctx->block_to_topo[b->idom];
      arrput(creations->requested, (ctx->block_to_topo[i] << 1) + 1);
      break;
    }
  }
}

void topo_walk_idom(code_analysis *analy, method_jit_ctx *ctx) {
  int current = ctx->current_block_i;
  int start = ctx->topo_i;
  ctx->visited[current] = 1;
  ctx->block_to_topo[current] = ctx->topo_i;
  ctx->topo_to_block[ctx->topo_i++] = current;
  ctx->loop_depths[current] = ctx->loop_depth;
  basic_block *b = analy->blocks + current;
  bool is_loop_header = b->is_loop_header;
  // Sort successors by reverse post-order in the original DFS
  dominated_list_t idom = b->idominates;
  int *sorted = malloc(arrlen(idom.list) * sizeof(int));
  memcpy(sorted, idom.list, arrlen(idom.list) * sizeof(int));
  for (int i = 1; i < arrlen(idom.list); ++i) {
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
  for (int i = 0; i < arrlen(idom.list); ++i) {
    int next = sorted[i];
    ctx->current_block_i = next;
    topo_walk_idom(analy, ctx);
  }
  ctx->loop_depth -= is_loop_header;
  bb_creations_t *creations = ctx->creations + start;
  if (is_loop_header)
    arrput(creations->requested, ctx->topo_i << 1);
  free(sorted);
}

method_jit_ctx *make_ctx(code_analysis *analy) {
  method_jit_ctx *ctx = calloc(1, sizeof(*ctx));
  ctx->topo_to_block = calloc(analy->block_count, sizeof(int));
  ctx->block_to_topo = calloc(analy->block_count, sizeof(int));
  ctx->visited = calloc(analy->block_count, sizeof(int));
  ctx->incoming_count = calloc(analy->block_count, sizeof(int));
  ctx->loop_depths = calloc(analy->block_count, sizeof(int));
  ctx->creations = calloc(analy->block_count, sizeof(bb_creations_t));
  ctx->block_ends = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx->loop_headers = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx->blockc = analy->block_count;
  ctx->current_block_i = ctx->topo_i = ctx->loop_depth = 0;
  for (int i = 0; i < analy->block_count; ++i) {
    basic_block *b = analy->blocks + i;
    for (int j = 0; j < arrlen(b->next); ++j)
      ctx->incoming_count[b->next[j]] += !b->is_backedge[j];
  }
  // Perform a post-order traversal of the immediate dominator tree. Whenever
  // reaching a loop header, output the loop header immediately, then everything
  // in the subtree as one contiguous block. We output them in reverse postorder
  // relative to a DFS on the original CFG, to guarantee that the final
  // topological sort respects the forward edges in the original graph.
  topo_walk_idom(analy, &ctx);
  assert(ctx->topo_i == analy->block_count);
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

void free_dumb_jit_result(dumb_jit_result *result){
  free(result->pc_to_oops.count);
  free(result);
}

expression compile_bb(basic_block *bb) {
  return nullptr;
}

dumb_jit_result *dumb_jit_compile(cp_method *method, dumb_jit_options options) {
  attribute_code *code = method->code;
  code_analysis *analy = method->code_analysis;

  DCHECK(code, "Method has no code");
  DCHECK(analy, "Method has no code analysis");

  compute_dominator_tree(analy);
  int fail = attempt_reduce_cfg(analy);
  if (fail) {
    return nullptr;
  }

  pc_to_oop_count pc_to_oops = {};
  pc_to_oops.count = calloc(code->insn_count, sizeof(u16));
  pc_to_oops.max_pc = code->insn_count;

  wasm_module *module = wasm_module_create();
  ctx = make_ctx(analy);
  inchoate_expression *expr_stack = nullptr;

  // Push an initial boi
  *arraddnptr(expr_stack, 1) = (inchoate_expression){
      wasm_block(module, nullptr, 0, wasm_void(), false), 0,
      analy->block_count, false};

  // Iterate through each block
  for (ctx->topo_i = 0; ctx->topo_i <= analy->block_count; ++ctx->topo_i) {
    // First close off any blocks as appropriate
    int stack_count = arrlen(expr_stack);
    for (int i = stack_count - 1; i >= 0; --i) {
      inchoate_expression *expr = expr_stack + i;
      if (expr->close_at == ctx->topo_i) {
        // Take expressions i + 1 through stack_count - 1 inclusive and
        // make them the contents of the block
        expression *exprs = alloca((stack_count - i - 1) * sizeof(expression));
        for (int j = i + 1; j < stack_count; ++j)
          exprs[j - i - 1] = expr_stack[j].ref;
        wasm_update_block(module, expr->ref, exprs, stack_count - i - 1,
                               wasm_void(), expr->is_loop);
        // Update the handles for blocks and loops to break to
        if (ctx->topo_i < analy->block_count)
          ctx->block_ends[ctx->topo_i] = nullptr;
        if (expr->is_loop)
          ctx->loop_headers[expr->started_at] = nullptr;
        expr->close_at = -1;
        stack_count = i + 1;
      }
    }
    // Done pushing expressions
    if (ctx->topo_i == analy->block_count)
      break;
    // Then create (block)s and (loop)s as appropriate. First create blocks
    // in reverse order of the topological order of their targets. So, if
    // at the current block we are to initiate blocks ending at block with
    // topo indices 9, 12, and 13, push the block for 13 first.
    //
    // Because blocks have a low bit of 1 in the encoding, they are larger in index and placed first.
    bb_creations_t *creations = ctx->creations + ctx->topo_i;
    qsort(creations->requested, arrlen(creations->requested), sizeof(int),
          cmp_ints_reverse);
    for (int i = 0; i < arrlen(creations->requested); ++i) {
      int block_i = creations->requested[i];
      int is_loop = !(block_i & 1);
      block_i >>= 1;
      expression block = wasm_block(module, nullptr, 0, wasm_void(), is_loop);
      *arraddnptr(expr_stack, 1) =
          (inchoate_expression){block, ctx->topo_i, block_i, is_loop};
      if (is_loop)
        ctx->loop_headers[ctx->topo_i] = block;
      else
        ctx->block_ends[block_i] = block;
    }
    int block_i = ctx->topo_to_block[ctx->topo_i];
    [[maybe_unused]] basic_block *bb = analy->blocks + block_i;
    expression expr = compile_bb(bb);
    if (!expr) {
      goto error_2;
    }
    *arraddnptr(expr_stack, 1) =
        (inchoate_expression){expr, ctx->topo_i, -1, false};
  }
  [[maybe_unused]] expression body = expr_stack[0].ref;

error_2:
  free_topo_ctx(*ctx);
  free(ctx);
  return nullptr;
}