
#include "../wasm/wasm_utils.h"
#include "dumb_jit.h"

enum { THREAD_PARAM, METHOD_PARAM };

static bjvm_string_hash_table interpreter_adapters;

__attribute__((constructor))
static void init_adapters() {
  interpreter_adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
}

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

static void *create_adapter_to_interpreter_impl(bjvm_wasm_value_type *args_types, int argc, bjvm_wasm_value_type return_type) {
  bjvm_wasm_module *module = bjvm_wasm_module_create();

  bjvm_wasm_value_type final_args[259];
  args_types[THREAD_PARAM] = BJVM_WASM_TYPE_KIND_INT32;
  args_types[METHOD_PARAM] = BJVM_WASM_TYPE_KIND_INT32;
  memcpy(final_args + 2, args_types, argc);

  bjvm_wasm_function *entry;
  switch (return_type) {
  case BJVM_WASM_TYPE_KIND_VOID:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "v");
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT64:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "d");
    break;
  case BJVM_WASM_TYPE_KIND_FLOAT32:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "f");
    break;
  case BJVM_WASM_TYPE_KIND_INT32:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "i");
    break;
  case BJVM_WASM_TYPE_KIND_INT64:
    entry = bjvm_wasm_import_runtime_function(module, try_interpreter_void, "iiii", "j");
    break;
  default:
    UNREACHABLE();
  }

  bjvm_wasm_function_builder *builder = create_wasm_function_builder(module, return_type, final_args, argc + 2);

  bjvm_wasm_expression *args[4];
  args[0] = bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32());
  args[1] = bjvm_wasm_local_get(module, METHOD_PARAM, bjvm_wasm_int32());

  // Consecutively write arguments to thread->frame_buffer + thread->frame_buffer_used, then call bjvm_push_frame
  // with the pointer, and finally call bjvm_interpret_2

  uint32_t write_base = function_builder_get_local(builder, BJVM_WASM_TYPE_KIND_INT32, "write_base");


  bjvm_wasm_expression *call = bjvm_wasm_call(module, entry, args, 4);
  call->call_indirect.tail_call = true;


  // bjvm_wasm_type locals = bjvm_wasm_make_tuple(module, (bjvm_wasm_type[]){bjvm_wasm_int32()}, 1);

  // Now create and export a function called "run"
  bjvm_wasm_function *fn = bjvm_wasm_add_function(module, params, return_type, locals, call, "run");
  bjvm_wasm_export_function(module, fn);

  // Now instantiate it
  bjvm_wasm_instantiation_result *result = bjvm_wasm_instantiate_module(module, "adapter");
  if (result->status == BJVM_WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&adapters, key, kinds_len, result->run);
    return result->run;
  }

  return nullptr;
}

// Create method->interpreter_entry which lets us transition from the JITed code to the interpreter for a particular
// invocation. This is also the default value for method->jit_entry.
void *create_adapter_to_interpreter(const bjvm_cp_method *method) {
  // Compute the WASM types for each argument
  bjvm_wasm_value_type args[257];
  int argc = bjvm_argc(method), i = 0;
  assert(argc < 257 && "Too many arguments");
  if (!(method->access_flags & BJVM_ACCESS_STATIC)) {
    args[i++] = BJVM_WASM_TYPE_KIND_INT32;
  }
  for (int j = 0; j < method->descriptor->args_count; ++j) {
    bjvm_type_kind K = field_to_kind(&method->descriptor->args[j]);
    args[i++] = bjvm_jvm_type_to_wasm(K).val;
  }
  assert(i == argc && "Should have consumed all arguments");
  // Also append the return type to the adapter method hash
  bjvm_type_kind field_ty = field_to_kind(&method->descriptor->return_type);
  bjvm_wasm_value_type return_type = field_ty == BJVM_TYPE_KIND_VOID ? BJVM_WASM_TYPE_KIND_VOID : bjvm_jvm_type_to_wasm(field_ty).val;
  args[i++] = return_type;
  // Check for existing adapter
  void *existing = bjvm_hash_table_lookup(&interpreter_adapters, args, i);
  if (existing) {
    return existing;
  }

  void *created = create_adapter_to_interpreter_impl(args, argc, args[i - 1]);
  (void)bjvm_hash_table_insert(&interpreter_adapters, args, i, created);
  return created;
}

jit_adapter_t create_adapter_to_jit(const bjvm_cp_method *method) {

}