// Adapter methods between compiled WebAssembly methods and interpreter methods.
// Lets us efficiently jump into JITed code, and jump from JITed code into
// the interpreter.

#include "wasm_adapter.h"
#include "bjvm.h"
#include "wasm_jit.h"
#include "wasm_utils.h"

EMSCRIPTEN_KEEPALIVE
bjvm_stack_frame *wasm_runtime_push_empty_frame(bjvm_thread *thread, bjvm_cp_method *method) {
  bjvm_stack_frame *result = bjvm_push_plain_frame(thread, method, nullptr, 0);
  return result;
}

EMSCRIPTEN_KEEPALIVE
bjvm_interpreter_result_t wasm_runtime_handle_native_call(
  bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value* result, bjvm_stack_value *args, int argc) {
  bjvm_stack_frame *frame = bjvm_push_frame(thread, method, args, argc);
  if (!frame) {
    return BJVM_INTERP_RESULT_EXC;  // stack overflow, etc.
  }
  // Rejoice in the lamb
  return bjvm_interpret(thread, frame, result);
}

wasm_expression **generate_writes(wasm_module *module, bjvm_type_kind *kinds, int kinds_len, wasm_expression *addr) {
  wasm_expression **result = nullptr;
  for (int i = 0; i < kinds_len; ++i) {
    int offset = i * sizeof(bjvm_stack_value);
    wasm_store_op_kind store_op;
    switch (kinds[i]) {
    case BJVM_TYPE_KIND_BOOLEAN:
    case BJVM_TYPE_KIND_BYTE:
    case BJVM_TYPE_KIND_CHAR:
    case BJVM_TYPE_KIND_SHORT:
      UNREACHABLE();
    case BJVM_TYPE_KIND_INT:
    case BJVM_TYPE_KIND_REFERENCE:
      store_op = WASM_OP_KIND_I32_STORE;
      break;
    case BJVM_TYPE_KIND_FLOAT:
      store_op = WASM_OP_KIND_F32_STORE;
      break;
    case BJVM_TYPE_KIND_LONG:
      store_op = WASM_OP_KIND_I64_STORE;
      break;
    case BJVM_TYPE_KIND_DOUBLE:
      store_op = WASM_OP_KIND_F64_STORE;
      break;
    default:
      UNREACHABLE();
    }
    arrput(result, wasm_store(module, store_op, addr, wasm_local_get(module, i + 3, jvm_type_to_wasm(kinds[i])), 0, offset));
  }
  return result;
}

static bjvm_string_hash_table compiled_adapters, interpreter_adapters;

__attribute__((constructor))
static void init_adapters() {
  compiled_adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
  interpreter_adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
}

void *create_adapter_to_interpreter(bjvm_type_kind *kinds, int kinds_len, bool is_native) {
  static bjvm_stack_value scratch_space[256];

  assert(kinds_len < 256);

  char key[257] = {0};
  int key_len = 0;
  for (; key_len < kinds_len; ++key_len) {
    key[key_len] = kinds[key_len];
  }
  key[key_len] = is_native ? 'N' : 'n';
  void *existing = bjvm_hash_table_lookup(&interpreter_adapters, key, key_len);
  if (existing) {
    return ((wasm_instantiation_result *)existing)->run;
  }

  enum {
    THREAD_PARAM,
    METHOD_PARAM,
    RESULT_PARAM
  };

  wasm_module *module = wasm_module_create();
  wasm_value_type components[kinds_len + 3];
  int i = 0;
  for (; i < 3; ++i) {
    components[i] = WASM_TYPE_KIND_INT32;  // thread, method, result
  }
  for (int j = 0; j < kinds_len; ++j, ++i) {
    components[i] = jvm_type_to_wasm(kinds[j]).val;
  }

  wasm_type fn_args = wasm_make_tuple(module, components, i);
  wasm_type locals = wasm_void();
  wasm_expression *boi;
  if (is_native) {
    // Write arguments to scratch space, then call wasm_runtime_
    wasm_expression **spill = generate_writes(module, kinds, kinds_len, wasm_i32_const(module, (intptr_t)scratch_space));
    wasm_function *handle_native_fn = wasm_import_runtime_function(module, wasm_runtime_handle_native_call, "iiiii", "i");

    wasm_expression *call_handle_native = wasm_call(module, handle_native_fn, (wasm_expression*[]){
      wasm_local_get(module, THREAD_PARAM, wasm_int32()),
      wasm_local_get(module, METHOD_PARAM, wasm_int32()),
      wasm_local_get(module, RESULT_PARAM, wasm_int32()),
      wasm_i32_const(module, (intptr_t)scratch_space),
      wasm_i32_const(module, kinds_len)
    }, 5);
    call_handle_native->call.tail_call = true;

    wasm_expression *sequence[kinds_len + 1];
    for (int j = 0; j < kinds_len; ++j) {
      sequence[j] = spill[j];
    }
    sequence[kinds_len] = call_handle_native;
    boi = wasm_block(module, sequence, kinds_len + 1, wasm_void(), false);
    arrfree(spill);
  } else {
    // Call wasm_runtime_push_empty_frame to get a frame pointer. If it's null, return
    // EXC. Otherwise, write the arguments to the frame, then tail call
    // bjvm_interpret.
    wasm_function *push_frame_fn = wasm_import_runtime_function(module, wasm_runtime_push_empty_frame, "ii", "i");
    wasm_expression *push_frame = wasm_call(module, push_frame_fn, (wasm_expression*[]){
      wasm_local_get(module, THREAD_PARAM, wasm_int32()),
      wasm_local_get(module, METHOD_PARAM, wasm_int32())
    }, 2);

    const int frame_local = kinds_len + 3, addr_local = kinds_len + 4;
    locals = wasm_make_tuple(module, (wasm_value_type[]){
      WASM_TYPE_KIND_INT32, WASM_TYPE_KIND_INT32
    }, 2);
    wasm_expression *make_frame = wasm_local_set(module, frame_local, push_frame);
    wasm_expression *null_check = wasm_unop(module, WASM_OP_KIND_I32_EQZ, wasm_local_get(module, frame_local, wasm_int32()));
    wasm_expression *return_exc = wasm_return(module, wasm_i32_const(module, BJVM_INTERP_RESULT_EXC));
    wasm_expression *addr = wasm_local_get(module, frame_local, wasm_int32());
    addr = wasm_binop(module, WASM_OP_KIND_I32_ADD, addr, wasm_i32_const(module, offsetof(bjvm_plain_frame, values)));
    wasm_expression *max_stack = wasm_load(module, WASM_OP_KIND_I32_LOAD16_U, wasm_local_get(module, frame_local, wasm_int32()), 0, offsetof(bjvm_plain_frame, max_stack));
    max_stack = wasm_binop(module, WASM_OP_KIND_I32_MUL, max_stack, wasm_i32_const(module, sizeof(bjvm_stack_value)));
    addr = wasm_binop(module, WASM_OP_KIND_I32_ADD, addr, max_stack);
    wasm_expression *make_addr = wasm_local_set(module, addr_local, addr);
    wasm_expression *get_addr = wasm_local_get(module, addr_local, wasm_int32());
    wasm_expression **spill = generate_writes(module, kinds, kinds_len, get_addr);
    wasm_expression *call_interpret = wasm_call(module, wasm_import_runtime_function(module, bjvm_interpret, "iii", "i"), (wasm_expression*[]){
      wasm_local_get(module, THREAD_PARAM, wasm_int32()),
      wasm_local_get(module, frame_local, wasm_int32()),
      wasm_local_get(module, RESULT_PARAM, wasm_int32())
    }, 3);
    call_interpret->call.tail_call = true;

    wasm_expression *sequence[kinds_len + 4];
    sequence[0] = make_frame;
    sequence[1] = wasm_if_else(module, null_check, return_exc, nullptr, wasm_int32());
    sequence[2] = make_addr;
    int j = 0;
    for (; j < kinds_len; ++j) {
      sequence[j + 3] = spill[j];
    }
    sequence[j++ + 3] = call_interpret;
    boi = wasm_block(module, sequence, j + 3, wasm_void(), false);

    arrfree(spill);
  }

  wasm_function *fn = wasm_add_function(module, fn_args, wasm_int32(), locals, boi, "run");
  wasm_export_function(module, fn);

  INIT_STACK_STRING(module_name, 100);
  bprintf(module_name, "interpreteradapter-%s", key);

  wasm_instantiation_result *result = wasm_instantiate_module(module, module_name.chars);

  if (result->status == WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&interpreter_adapters, key, key_len, result);
    return result->run;
  }

  bjvm_free_wasm_instantiation_result(result);
  return nullptr;
}

// Invoke the compiled method from interpreter land, given a pointer to the
// arguments and a pointer to the method (which must be registered in the
// appropriate WebAssembly.Table).
compiled_method_adapter_t create_adapter_to_compiled_method(bjvm_type_kind *kinds, int kinds_len) {
  assert(kinds_len < 256);
  char key[256];
  for (int i = 0; i < kinds_len; ++i)
    key[i] = kinds[i];

  // Check for existing adapter
  wasm_instantiation_result *existing = bjvm_hash_table_lookup(&compiled_adapters, key, kinds_len);
  if (existing) {
    return existing->run;
  }

  enum {
    THREAD_PARAM, RESULT_PARAM, ARGS_PARAM, FN_PARAM
  };
  wasm_module *module = wasm_module_create();
  wasm_expression **args = nullptr;

  arrput(args, wasm_local_get(module, THREAD_PARAM, wasm_int32()));
  arrput(args, wasm_i32_const(module, 0));
  arrput(args, wasm_local_get(module, RESULT_PARAM, wasm_int32()));

  for (int i = 0; i < kinds_len; ++i) {
    int offset = i * sizeof(bjvm_stack_value);
    wasm_load_op_kind load_op;
    switch (kinds[i]) {
    case BJVM_TYPE_KIND_BOOLEAN:
    case BJVM_TYPE_KIND_BYTE:
    case BJVM_TYPE_KIND_CHAR:
    case BJVM_TYPE_KIND_SHORT:
      UNREACHABLE();
    case BJVM_TYPE_KIND_INT:
    case BJVM_TYPE_KIND_REFERENCE:
      load_op = WASM_OP_KIND_I32_LOAD;
      break;
    case BJVM_TYPE_KIND_FLOAT:
      load_op = WASM_OP_KIND_F32_LOAD;
      break;
    case BJVM_TYPE_KIND_LONG:
      load_op = WASM_OP_KIND_I64_LOAD;
      break;
    case BJVM_TYPE_KIND_DOUBLE:
      load_op = WASM_OP_KIND_F64_LOAD;
      break;
    default:
      UNREACHABLE();
    }
    arrput(args, wasm_load(module, load_op, wasm_local_get(module, ARGS_PARAM, wasm_int32()), 0, offset));
  }

  // Now indirect tail call
  wasm_value_type args_types[kinds_len + 3];
  args_types[0] = WASM_TYPE_KIND_INT32;
  args_types[1] = WASM_TYPE_KIND_INT32;
  args_types[2] = WASM_TYPE_KIND_INT32;
  for (int i = 0; i < kinds_len; ++i) {
    args_types[i + 3] = jvm_type_to_wasm(kinds[i]).val;
  }

  uint32_t type = bjvm_register_function_type(module, wasm_make_tuple(module, args_types, kinds_len + 3), wasm_int32());
  wasm_expression *call = wasm_call_indirect(module, 0,
    wasm_local_get(module, FN_PARAM, wasm_int32()), args, kinds_len + 3, type);
  call->call_indirect.tail_call = true;

  wasm_type locals = wasm_make_tuple(module, (wasm_value_type[]){ WASM_TYPE_KIND_INT32 }, 1);

  wasm_value_type arg_types[4];
  for (int i = 0; i < 4; ++i)
    arg_types[i] = WASM_TYPE_KIND_INT32;

  // Now create and export a function called "run"
  wasm_function *fn = wasm_add_function(module, wasm_make_tuple(module, arg_types, 4), wasm_int32(), locals, call, "run");
  wasm_export_function(module, fn);

  // Now instantiate it
  wasm_instantiation_result *result = wasm_instantiate_module(module, "adapter");
  wasm_module_free(module);

  arrfree(args);

  if (result->status == WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&compiled_adapters, key, kinds_len, result);
    return result->run;
  }

  bjvm_free_wasm_instantiation_result(result);
  return nullptr;
}