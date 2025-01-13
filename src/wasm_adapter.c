// Adapter methods between compiled WebAssembly methods and interpreter methods.
// Lets us efficiently jump into JITed code, and jump from JITed code into
// interpreted code.

#include "wasm_adapter.h"
#include "bjvm.h"
#include "wasm_jit.h"
#include "wasm_utils.h"

static bjvm_stack_frame *wasm_runtime_push_empty_frame(bjvm_thread *thread, bjvm_cp_method *method) {
  return bjvm_push_plain_frame(thread, method, nullptr, 0);
}

bjvm_wasm_expression **generate_writes(bjvm_wasm_module *module, bjvm_type_kind *kinds, int kinds_len, bjvm_wasm_expression *addr) {
  bjvm_wasm_expression **result = nullptr;
  for (int i = 0; i < kinds_len; ++i) {
    int offset = offsetof(bjvm_plain_frame, values) + i * sizeof(bjvm_stack_value);
    bjvm_wasm_store_op_kind store_op;
    switch (kinds[i]) {
    case BJVM_TYPE_KIND_BOOLEAN:
    case BJVM_TYPE_KIND_BYTE:
    case BJVM_TYPE_KIND_CHAR:
    case BJVM_TYPE_KIND_SHORT:
      UNREACHABLE();
    case BJVM_TYPE_KIND_INT:
    case BJVM_TYPE_KIND_REFERENCE:
      store_op = BJVM_WASM_OP_KIND_I32_STORE;
      break;
    case BJVM_TYPE_KIND_FLOAT:
      store_op = BJVM_WASM_OP_KIND_F32_STORE;
      break;
    case BJVM_TYPE_KIND_LONG:
      store_op = BJVM_WASM_OP_KIND_I64_STORE;
      break;
    case BJVM_TYPE_KIND_DOUBLE:
      store_op = BJVM_WASM_OP_KIND_F64_STORE;
      break;
    default:
      UNREACHABLE();
    }
    arrput(result, bjvm_wasm_store(module, store_op, addr, bjvm_wasm_local_get(module, i + 3, bjvm_jvm_type_to_wasm(kinds[i])), 0, offset));
  }
  return result;
}

static void *create_adapter_to_interpreter_frame(bjvm_type_kind *kinds,
                                                 int kinds_len, bool is_native) {
  // bjvm_interpreter_result_t (*compiled)(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *result, ... args)
  static bjvm_stack_value scratch_space[256];

  enum {
    THREAD_PARAM,
    METHOD_PARAM,
    RESULT_PARAM
  };

  bjvm_wasm_module *module = bjvm_wasm_module_create();
  bjvm_wasm_value_type components[kinds_len + 3];
  int i = 0;
  for (; i < 3; ++i) {
    components[i] = BJVM_WASM_TYPE_KIND_INT32;  // thread, method, result
  }
  for (int j = 0; j < kinds_len; ++j) {
    //components[i] = bjvm_jvm_type_to_wasm(kinds[j]);
  }

  bjvm_wasm_type fn_args = bjvm_wasm_make_tuple(module, components, kinds_len + 3);
  if (is_native) {
    // Write arguments to scratch space, then call bjvm_push_frame followed by
    // bjvm_interpret.
    bjvm_wasm_expression *write_args[kinds_len];
  } else {
    // Call wasm_runtime_push_empty_frame to get a frame pointer. If it's null, return
    // EXC. Otherwise, write the arguments to the frame, then tail call
    // bjvm_interpret.
    bjvm_wasm_function *push_frame_fn = bjvm_wasm_import_runtime_function(module, "wasm_runtime_push_empty_frame", "ii", "i");
    bjvm_wasm_expression *push_frame = bjvm_wasm_call(module, push_frame_fn, (bjvm_wasm_expression*[]){
      bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32()),
      bjvm_wasm_local_get(module, METHOD_PARAM, bjvm_wasm_int32())
    }, 2);

    const int frame_local = kinds_len + 3;
    bjvm_wasm_expression *make_frame = bjvm_wasm_local_set(module, frame_local, push_frame);
    bjvm_wasm_expression *null_check = bjvm_wasm_unop(module, BJVM_WASM_OP_KIND_I32_EQZ, bjvm_wasm_local_get(module, frame_local, bjvm_wasm_int32()));
    bjvm_wasm_expression *return_exc = bjvm_wasm_return(module, bjvm_wasm_i32_const(module, BJVM_INTERP_RESULT_EXC));
    bjvm_wasm_expression *addr = bjvm_wasm_local_get(module, frame_local, bjvm_wasm_int32());
    bjvm_wasm_expression **spill = generate_writes(module, kinds, kinds_len, addr);
    bjvm_wasm_expression *call_interpret = bjvm_wasm_call(module, bjvm_wasm_import_runtime_function(module, "bjvm_interpret", "ii", "i"), (bjvm_wasm_expression*[]){
      bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32()),
      bjvm_wasm_local_get(module, frame_local, bjvm_wasm_int32())
    }, 2);
    call_interpret->call.tail_call = true;

    bjvm_wasm_expression *sequence[kinds_len + 3];
    sequence[0] = make_frame;
    sequence[1] = bjvm_wasm_if_else(module, null_check, return_exc, nullptr, bjvm_wasm_int32());
    int j = 0;
    for (; j < kinds_len; ++j) {
      sequence[j + 2] = spill[j];
    }
    sequence[j] = call_interpret;

    bjvm_wasm_expression *boi = bjvm_wasm_block(module, sequence, j, bjvm_wasm_void(), false);

    arrfree(spill);
  }

  return nullptr;
}

static bjvm_string_hash_table adapters;

__attribute__((constructor))
static void init_adapters() {
  adapters = bjvm_make_hash_table(nullptr, 0.75, 16);
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
  void *existing = bjvm_hash_table_lookup(&adapters, key, kinds_len);
  if (existing) {
    return existing;
  }

  enum {
    THREAD_PARAM, RESULT_PARAM, ARGS_PARAM, FN_PARAM
  };
  bjvm_wasm_module *module = bjvm_wasm_module_create();
  bjvm_wasm_expression **args = nullptr;

  arrput(args, bjvm_wasm_local_get(module, THREAD_PARAM, bjvm_wasm_int32()));
  arrput(args, bjvm_wasm_local_get(module, RESULT_PARAM, bjvm_wasm_int32()));

  for (int i = 0; i < kinds_len; ++i) {
    int offset = i * sizeof(bjvm_stack_value);
    bjvm_wasm_load_op_kind load_op;
    switch (kinds[i]) {
    case BJVM_TYPE_KIND_BOOLEAN:
    case BJVM_TYPE_KIND_BYTE:
    case BJVM_TYPE_KIND_CHAR:
    case BJVM_TYPE_KIND_SHORT:
      UNREACHABLE();
    case BJVM_TYPE_KIND_INT:
    case BJVM_TYPE_KIND_REFERENCE:
      load_op = BJVM_WASM_OP_KIND_I32_LOAD;
      break;
    case BJVM_TYPE_KIND_FLOAT:
      load_op = BJVM_WASM_OP_KIND_F32_LOAD;
      break;
    case BJVM_TYPE_KIND_LONG:
      load_op = BJVM_WASM_OP_KIND_I64_LOAD;
      break;
    case BJVM_TYPE_KIND_DOUBLE:
      load_op = BJVM_WASM_OP_KIND_F64_LOAD;
      break;
    default:
      UNREACHABLE();
    }
    arrput(args, bjvm_wasm_load(module, load_op, bjvm_wasm_local_get(module, ARGS_PARAM, bjvm_wasm_int32()), 0, offset));
  }

  // Now indirect tail call
  bjvm_wasm_value_type args_types[kinds_len + 2];
  args_types[0] = BJVM_WASM_TYPE_KIND_INT32;
  args_types[1] = BJVM_WASM_TYPE_KIND_INT32;
  for (int i = 0; i < kinds_len; ++i) {
    //args_types[i + 2] = bjvm_jvm_type_to_wasm(kinds[i]);
  }

  uint32_t type = bjvm_register_function_type(module, bjvm_wasm_make_tuple(module, args_types, kinds_len + 2), bjvm_wasm_int32());
  bjvm_wasm_expression *call = bjvm_wasm_call_indirect(module, 0,
    bjvm_wasm_local_get(module, FN_PARAM, bjvm_wasm_int32()), args, kinds_len + 2, type);
  call->call_indirect.tail_call = true;

  bjvm_wasm_type locals; // = bjvm_wasm_make_tuple(module, (bjvm_wasm_type[]){bjvm_wasm_int32()}, 1);

  // Now create and export a function called "run"
  bjvm_wasm_function *fn; // = bjvm_wasm_add_function(module, bjvm_wasm_make_tuple(module, (bjvm_wasm_type[]){bjvm_wasm_int32(), bjvm_wasm_int32(), bjvm_wasm_int32(), bjvm_wasm_int32()}, 4), bjvm_wasm_int32(), locals, call, "run");
  bjvm_wasm_export_function(module, fn);

  // Now instantiate it
  bjvm_wasm_instantiation_result *result = bjvm_wasm_instantiate_module(module, "adapter");
  if (result->status == BJVM_WASM_INSTANTIATION_SUCCESS) {
    (void)bjvm_hash_table_insert(&adapters, key, kinds_len, result->run);
    return result->run;
  }

  return nullptr;
}