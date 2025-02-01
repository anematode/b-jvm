// Adapters from interpreter frames to compiled frames, and from compiled frames to interpreter frames.

#include "../wasm/wasm_utils.h"
#include "dumb_jit.h"

enum { THREAD_PARAM, METHOD_PARAM };

static bjvm_string_hash_table interpreter_adapters;
static bjvm_string_hash_table jit_adapters;

__attribute__((constructor))
static void init_adapters() {
  interpreter_adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
  jit_adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
}

/** BEGIN INTERPRETER ADAPTERS **/

EMSCRIPTEN_KEEPALIVE
double try_interpreter_double(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame)
    return 0.0;
  future_t fut;
  bjvm_stack_value result = bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status == FUTURE_READY);
  return result.d;
}

EMSCRIPTEN_KEEPALIVE
int try_interpreter_int(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame)
    return 0;
  future_t fut;
  bjvm_stack_value result = bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status  == FUTURE_READY);
  return result.i;
}

EMSCRIPTEN_KEEPALIVE
int64_t try_interpreter_long(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame)
    return 0;
  future_t fut;
  bjvm_stack_value result = bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status  == FUTURE_READY);
  return result.l;
}

EMSCRIPTEN_KEEPALIVE
float try_interpreter_float(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame)
    return 0.0f;
  future_t fut;
  bjvm_stack_value result = bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status  == FUTURE_READY);
  return result.f;
}

EMSCRIPTEN_KEEPALIVE
void try_interpreter_void(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame)
    return;
  future_t fut;
  bjvm_interpret_2(&fut, thread, frame);
  assert(fut.status == FUTURE_READY);
}

static bjvm_wasm_expression *thread(bjvm_wasm_module *module) {
  return bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32());
}

static bjvm_wasm_load_op_kind load_ops[] = {
  [BJVM_WASM_TYPE_KIND_INT32] = BJVM_WASM_OP_KIND_I32_LOAD,
  [BJVM_WASM_TYPE_KIND_INT64] = BJVM_WASM_OP_KIND_I64_LOAD,
  [BJVM_WASM_TYPE_KIND_FLOAT32] = BJVM_WASM_OP_KIND_F32_LOAD,
  [BJVM_WASM_TYPE_KIND_FLOAT64] = BJVM_WASM_OP_KIND_F64_LOAD,
};

static bjvm_wasm_store_op_kind store_ops[] = {
  [BJVM_WASM_TYPE_KIND_INT32] = BJVM_WASM_OP_KIND_I32_STORE,
  [BJVM_WASM_TYPE_KIND_INT64] = BJVM_WASM_OP_KIND_I64_STORE,
  [BJVM_WASM_TYPE_KIND_FLOAT32] = BJVM_WASM_OP_KIND_F32_STORE,
  [BJVM_WASM_TYPE_KIND_FLOAT64] = BJVM_WASM_OP_KIND_F64_STORE,
};

static void *create_adapter_to_interpreter_impl(bjvm_wasm_value_type *args_types, int argc, bjvm_wasm_value_type return_type) {
  bjvm_wasm_module *module = bjvm_wasm_module_create();

  bjvm_wasm_value_type adapter_args[259];
  args_types[THREAD_PARAM] = BJVM_WASM_TYPE_KIND_INT32;
  args_types[METHOD_PARAM] = BJVM_WASM_TYPE_KIND_INT32;
  memcpy(adapter_args + 2, args_types, argc);

  bjvm_wasm_function *entry;
  switch (return_type) {
  case BJVM_WASM_TYPE_KIND_VOID:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "v");
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT64:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_double, "iiii", "d");
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT32:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_float, "iiii", "f");
    break;
  case BJVM_WASM_TYPE_KIND_INT32:  // used for both reference and int
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_int, "iiii", "i");
    break;
  case BJVM_WASM_TYPE_KIND_INT64:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_long, "iiii", "j");
    break;
  default:
    UNREACHABLE();
  }

  bjvm_wasm_function_builder *builder = create_wasm_function_builder(module, return_type, adapter_args, argc + 2);

  // Consecutively write arguments to thread->frame_buffer + thread->frame_buffer_used, then call bjvm_push_frame
  // with the pointer, and finally call bjvm_interpret_2

  uint32_t args_base_local = function_builder_get_local(builder, BJVM_WASM_TYPE_KIND_INT32, "write_base");

  bjvm_wasm_expression **seq = nullptr;
  bjvm_wasm_expression *frame_buffer = bjvm_wasm_load(module, BJVM_WASM_OP_KIND_I32_LOAD, thread(module), 1, offsetof(bjvm_thread, frame_buffer));
  bjvm_wasm_expression *frame_buffer_used = bjvm_wasm_load(module, BJVM_WASM_OP_KIND_I32_LOAD, thread(module), 1, offsetof(bjvm_thread, frame_buffer_used));
  bjvm_wasm_expression *args_base = bjvm_wasm_binop(module, BJVM_WASM_OP_KIND_I32_ADD, frame_buffer, frame_buffer_used);

  arrput(seq, bjvm_wasm_local_set(module, args_base_local, args_base));

  for (int i = 0; i < argc; ++i) {
    bjvm_wasm_expression *base = bjvm_wasm_local_get(module, args_base_local, bjvm_wasm_int32());
    bjvm_wasm_store_op_kind store_op;
    switch (args_types[i]) {
      case BJVM_WASM_TYPE_KIND_INT32:
        store_op = BJVM_WASM_OP_KIND_I32_STORE;
        break;
      case BJVM_WASM_TYPE_KIND_INT64:
        store_op = BJVM_WASM_OP_KIND_I64_STORE;
        break;
      case BJVM_WASM_TYPE_KIND_FLOAT32:
        store_op = BJVM_WASM_OP_KIND_F32_STORE;
        break;
      case BJVM_WASM_TYPE_KIND_FLOAT64:
        store_op = BJVM_WASM_OP_KIND_F64_STORE;
        break;
      default:
        UNREACHABLE();
    }
    bjvm_wasm_expression *get_arg = bjvm_wasm_local_get(module, i + 2, (bjvm_wasm_type) { args_types[i] });
    arrput(seq, bjvm_wasm_store(module, store_op, base, get_arg, 1, i * sizeof(bjvm_stack_value)));
  }

  bjvm_wasm_expression *args[4];
  args[0] = bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32());
  args[1] = bjvm_wasm_local_get(module, METHOD_PARAM, bjvm_wasm_int32());
  args[2] = bjvm_wasm_local_get(module, args_base_local, bjvm_wasm_int32());
  args[3] = bjvm_wasm_i32_const(module, argc);

  bjvm_wasm_expression *call = bjvm_wasm_call(module, entry, args, 4);
  call->call_indirect.tail_call = true;

  arrput(seq, call);
  function_builder_finish(builder,
    bjvm_wasm_block(module, seq, arrlen(seq), bjvm_wasm_void(), false), "run");

  arrfree(seq);

  // Now instantiate it
  bjvm_wasm_instantiation_result *result = bjvm_wasm_instantiate_module(module, "adapter");
  if (result->status == BJVM_WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&interpreter_adapters,
      (char const*)adapter_args, argc + 1 /* include return ty in hash */, result);
    return result->run;
  }

  return nullptr;
}

static bjvm_wasm_value_type *get_wasm_value_types(const bjvm_cp_method *method) {
  static bjvm_wasm_value_type args[257];
  [[maybe_unused]] int argc = bjvm_argc(method), i = 0;
  assert(argc < 257 && "Too many arguments");
  if (!(method->access_flags & BJVM_ACCESS_STATIC)) {
    args[i++] = BJVM_WASM_TYPE_KIND_INT32;
  }
  for (int j = 0; j < method->descriptor->args_count; ++j) {
    bjvm_type_kind K = field_to_kind(&method->descriptor->args[j]);
    args[i++] = bjvm_jvm_type_to_wasm(K).val;
  }
  assert(i == argc && "Should have consumed all arguments");
  return args;
}

// Create method->interpreter_entry which lets us transition from the JITed code to the interpreter for a particular
// invocation. This is also the default value for method->jit_entry.
void *create_adapter_to_interpreter(const bjvm_cp_method *method) {
  // Compute the WASM types for each argument
  bjvm_wasm_value_type *args = get_wasm_value_types(method);

  // Also append the return type to the adapter method hash
  bjvm_type_kind field_ty = field_to_kind(&method->descriptor->return_type);
  bjvm_wasm_value_type return_type = field_ty == BJVM_TYPE_KIND_VOID ? BJVM_WASM_TYPE_KIND_VOID : bjvm_jvm_type_to_wasm(field_ty).val;
  int argc = bjvm_argc(method);
  args[argc] = return_type;
  // Check for existing adapter
  bjvm_wasm_instantiation_result *existing = bjvm_hash_table_lookup(&interpreter_adapters, args, i);
  if (existing) {
    return existing->run;
  }

  return create_adapter_to_interpreter_impl(args, argc, args[argc]);
}

jit_adapter_t create_adapter_to_jit(const bjvm_cp_method *method) {
  // Compute the WASM types

  enum {
    THREAD_PARAM, METHOD_PARAM, ARGS_PARAM, RESULT_PARAM
  };

  bjvm_wasm_module *module = bjvm_wasm_module_create();

  int argc = bjvm_argc(method);
  bjvm_wasm_value_type args[4] = {
    // thread *, method *, args *, result *
    BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_TYPE_KIND_INT32
  };

  bjvm_wasm_value_type *jit_args = get_wasm_value_types(method);
  bjvm_type_kind field_ty = field_to_kind(&method->descriptor->return_type);
  bjvm_wasm_value_type jit_return_ty = field_ty == BJVM_TYPE_KIND_VOID ? BJVM_WASM_TYPE_KIND_VOID : bjvm_jvm_type_to_wasm(field_ty).val;
  jit_args[argc] = jit_return_ty;
  // ok, but we need to prepend the thread and method params
  bjvm_wasm_value_type jit_signature[259] = { BJVM_WASM_TYPE_KIND_INT32, BJVM_WASM_TYPE_KIND_INT32 };
  memcpy(jit_signature + 2, jit_args, argc);

  bjvm_wasm_instantiation_result *existing = bjvm_hash_table_lookup(&jit_adapters, (char const*)jit_signature, argc + 1);
  if (existing) {
    return existing->run;
  }

  uint32_t jit_signature_ty = bjvm_register_function_type(module,
    bjvm_wasm_make_tuple(module, jit_signature, argc + 2), (bjvm_wasm_type) { jit_return_ty });

  bjvm_wasm_function_builder *builder = create_wasm_function_builder(module, BJVM_WASM_TYPE_KIND_VOID, args, 4);

  bjvm_wasm_expression **args_to_jit = nullptr;
  bjvm_wasm_expression *thread = bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32()),
    *cp_method = bjvm_wasm_local_get(module, METHOD_PARAM, bjvm_wasm_int32());
  arrput(args_to_jit, thread);
  arrput(args_to_jit, cp_method);
  bjvm_wasm_expression *args_param = bjvm_wasm_local_get(module, ARGS_PARAM, bjvm_wasm_int32());

  for (int i = 0; i < argc; ++i) {
    bjvm_wasm_load_op_kind load_ops[] = {
      [BJVM_WASM_TYPE_KIND_INT32] = BJVM_WASM_OP_KIND_I32_LOAD,
      [BJVM_WASM_TYPE_KIND_INT64] = BJVM_WASM_OP_KIND_I64_LOAD,
      [BJVM_WASM_TYPE_KIND_FLOAT32] = BJVM_WASM_OP_KIND_F32_LOAD,
      [BJVM_WASM_TYPE_KIND_FLOAT64] = BJVM_WASM_OP_KIND_F64_LOAD,
    };

    bjvm_wasm_expression *load_expr = bjvm_wasm_load(module,
      load_ops[jit_args[i]], args_param, 1, i * sizeof(bjvm_stack_value));
    arrput(args_to_jit, load_expr);
  }

  bjvm_wasm_expression *call_expr = bjvm_wasm_call_indirect(module, 0, index, args_to_jit, argc + 2, jit_signature_ty);

  if (jit_return_ty != BJVM_WASM_TYPE_KIND_VOID) {
    // We need to store the result back into the result param
    bjvm_wasm_store_op_kind store_op = store_ops[jit_return_ty];
    bjvm_wasm_expression *result_param = bjvm_wasm_local_get(module, RESULT_PARAM, bjvm_wasm_int32());

    call_expr = bjvm_wasm_store(module, store_op, result_param, call_expr, 1, 0);
  }

  function_builder_finish(builder, call_expr, "run");
  bjvm_wasm_instantiation_result *result = bjvm_wasm_instantiate_module(module, "adapter");
  if (result->status == BJVM_WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&jit_adapters, (char const*)jit_signature, argc + 1, result);
    return result->run;
  }
  return nullptr;
}