#ifndef NATIVES_DSL_H
#define NATIVES_DSL_H

#include "arrays.h"
#include "bjvm.h"
#include <natives-dsl.h>
#include <stddef.h>
#include <exceptions.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

maybe_extern_begin;
void push_bjvm_native(slice class_name, slice method_name, slice signature, bjvm_native_callback native);

static inline void __obj_store_field(bjvm_obj_header *thing, slice field_name, bjvm_stack_value value,
                                     slice desc) {
  bjvm_cp_field *field = bjvm_easy_field_lookup(thing->descriptor, field_name, desc);
  assert(field);

  bjvm_set_field(thing, field, value);
}

static inline bjvm_stack_value __obj_load_field(bjvm_obj_header *thing, slice field_name, slice desc) {
  bjvm_cp_field *field = bjvm_easy_field_lookup(thing->descriptor, field_name, desc);
  assert(field);

  return bjvm_get_field(thing, field);
}

maybe_extern_end;

#define ThrowLangException(exception_name)                                                                             \
  bjvm_raise_vm_exception(thread, STR("java/lang/" #exception_name), null_str())

#define ThrowLangExceptionM(exception_name, fmt, ...)                                                                  \
  do {                                                                                                                 \
    char msg[1024];                                                                                                    \
    size_t size = snprintf(msg, 1024, fmt, __VA_ARGS__);                                                               \
    slice msg_slice = {msg, size};                                                                                 \
    bjvm_raise_vm_exception(thread, STR("java/lang/" exception_name), msg_slice);                                      \
  } while (0)

#define MakeHandle(obj) bjvm_make_handle(thread, obj)

static inline bjvm_obj_header *check_is_object(bjvm_obj_header *thing) { return thing; }

#define AsHeapString(expr, on_oom)                                                                                     \
  ({                                                                                                                   \
    bjvm_obj_header *__val = check_is_object(expr);                                                                    \
    heap_string __hstr;                                                                                                \
    if (read_string_to_utf8(thread, &__hstr, __val) != 0) {                                                            \
      assert(thread->current_exception);                                                                               \
      goto on_oom;                                                                                                     \
    }                                                                                                                  \
    __hstr;                                                                                                            \
  })

static inline object __LoadFieldObject(bjvm_obj_header *thing, slice desc, slice name) {
  return __obj_load_field(thing, name, desc).obj;
}

static inline void __StoreFieldObject(bjvm_obj_header *thing, slice desc, slice name, object value) {
  __obj_store_field(thing, name, (bjvm_stack_value){.obj = value}, desc);
}

#define StoreFieldObject(obj, type, name, value) __StoreFieldObject(obj, STR("L" type ";"), STR(name), value)
#define LoadFieldObject(obj, type, name) __LoadFieldObject(obj, STR("L" type ";"), STR(name))

#define GeneratePrimitiveStoreField(type_cap, type, stack_field, desc, modifier)                                       \
  static inline void __StoreField##type_cap(bjvm_obj_header *thing, slice name, type value) {                      \
    __obj_store_field(thing, name, (bjvm_stack_value){.stack_field = value modifier}, STR(#desc));                     \
  }

#define GeneratePrimitiveLoadField(type_cap, type, stack_field, desc)                                                  \
  static inline type __LoadField##type_cap(bjvm_obj_header *thing, slice name) {                                   \
    return __obj_load_field(thing, name, STR(#desc)).stack_field;                                                      \
  }

GeneratePrimitiveStoreField(Byte, jbyte, i, B, );
GeneratePrimitiveStoreField(Char, jchar, i, C, );
GeneratePrimitiveStoreField(Int, jint, i, I, );
GeneratePrimitiveStoreField(Long, jlong, l, J, );
GeneratePrimitiveStoreField(Float, jfloat, f, F, );
GeneratePrimitiveStoreField(Double, jdouble, d, D, );
GeneratePrimitiveStoreField(Boolean, jboolean, i, Z, &1);

GeneratePrimitiveLoadField(Byte, jbyte, i, B);
GeneratePrimitiveLoadField(Char, jchar, i, C);
GeneratePrimitiveLoadField(Int, jint, i, I);
GeneratePrimitiveLoadField(Long, jlong, l, J);
GeneratePrimitiveLoadField(Float, jfloat, f, F);
GeneratePrimitiveLoadField(Double, jdouble, d, D);
GeneratePrimitiveLoadField(Boolean, jboolean, i, Z);

#define StoreFieldByte(obj, name, value) __StoreFieldByte(obj, STR(name), value)
#define StoreFieldChar(obj, name, value) __StoreFieldChar(obj, STR(name), value)
#define StoreFieldInt(obj, name, value) __StoreFieldInt(obj, STR(name), value)
#define StoreFieldLong(obj, name, value) __StoreFieldLong(obj, STR(name), value)
#define StoreFieldFloat(obj, name, value) __StoreFieldFloat(obj, STR(name), value)
#define StoreFieldDouble(obj, name, value) __StoreFieldDouble(obj, STR(name), value)
#define StoreFieldBoolean(obj, name, value) __StoreFieldBoolean(obj, STR(name), value)

#define LoadFieldByte(obj, name) __LoadFieldByte(obj, STR(name))
#define LoadFieldChar(obj, name) __LoadFieldChar(obj, STR(name))
#define LoadFieldInt(obj, name) __LoadFieldInt(obj, STR(name))
#define LoadFieldLong(obj, name) __LoadFieldLong(obj, STR(name))
#define LoadFieldFloat(obj, name) __LoadFieldFloat(obj, STR(name))
#define LoadFieldDouble(obj, name) __LoadFieldDouble(obj, STR(name))
#define LoadFieldBoolean(obj, name) __LoadFieldBoolean(obj, STR(name))

#undef GenerateStoreField
#undef GeneratePrimitiveLoadField

#define HandleIsNull(expr) ((expr)->obj == nullptr)

#define force_expand_args(macro_name, ...) macro_name(__VA_ARGS__)

extern size_t bjvm_native_count;
extern size_t bjvm_native_capacity;
extern bjvm_native_t *bjvm_natives;

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)                                                   \
  static bjvm_stack_value class_name_##_##method_name_##_cb##modifier(                                                 \
      [[maybe_unused]] bjvm_thread *thread, [[maybe_unused]] bjvm_handle *obj, [[maybe_unused]] bjvm_value *args,      \
      [[maybe_unused]] uint8_t argc)

#define create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier, async_sz,       \
                                variant)                                                                               \
  maybe_extern_begin;                                                                                                  \
  __attribute__((constructor)) static void class_name_##_##method_name_##_init##modifier() {                           \
    push_bjvm_native(                                                                                                  \
        STR(package_path "/" #class_name_), STR(#method_name_), STR(method_descriptor_),                               \
        (bjvm_native_callback){.async_ctx_bytes = async_sz,                                                            \
                               .variant = (variant##_native_callback) & class_name_##_##method_name_##_cb##modifier}); \
  };                                                                                                                   \
  maybe_extern_end

#define DECLARE_NATIVE_(package_path, class_name_, method_name_, method_descriptor_, modifier)                         \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier);                                                        \
  create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier, 0, sync)              \
      DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)

#define DECLARE_NATIVE(package_path, class_name_, method_name_, method_descriptor_)                                    \
  force_expand_args(DECLARE_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, __COUNTER__)

#ifdef __cplusplus
#define check_field_offset(m_name, member_a, member_b)
#else
// this breaks clion for some reason
#define check_field_offset(m_name, member_a, member_b)                                                                 \
  static_assert(offsetof(struct m_name##_s, member_a) == offsetof(async_natives_args, member_b),                      \
                 #member_a " mismatch " #member_b);
#endif

#define create_async_declaration(name, locals, async_methods)                                                          \
  DECLARE_ASYNC(                                                                                                       \
    bjvm_stack_value, \
    name, \
    locals,\
    arguments(bjvm_thread *thread; bjvm_handle *obj; bjvm_value *args; uint8_t argc), \
    async_methods\
  );              \
  /* the arguments struct for this needs to be compatible with the async_natives_args struct */                        \
  /* todo: maybe just reuse the async_natives_args struct, rather than making a separate (but equivalent) struct for   \
   * each native */                                                                                                    \
  check_field_offset(name, args.thread, args.thread);                                                                       \
  check_field_offset(name, args.obj, args.obj);                                                                             \
  check_field_offset(name, args.args, args.args);                                                                           \
  check_field_offset(name, args.argc, args.argc);                                                                           \
  check_field_offset(name, _state, stage); \
  check_field_offset(name, _result, result);

#undef _DECLARE_CACHED_STATE
#undef _RELOAD_CACHED_STATE

#define _DECLARE_CACHED_STATE(_)                                                                                       \
  [[maybe_unused]] bjvm_thread *thread = self->args.thread;                                                            \
  [[maybe_unused]] bjvm_value *args = self->args.args;                                                                 \
  [[maybe_unused]] bjvm_handle *obj = self->args.obj;                                                                  \
  [[maybe_unused]] uint8_t argc = self->args.argc;

#define _RELOAD_CACHED_STATE()                                                                                         \
  do {                                                                                                                 \
    thread = self->args.thread;                                                                                        \
    args = self->args.args;                                                                                            \
    obj = self->args.obj;                                                                                              \
    argc = self->args.argc;                                                                                            \
  } while (0)

#define DECLARE_ASYNC_NATIVE_(package_path, class_name_, method_name_, method_descriptor_, locals,                     \
                              invoked_async_methods, modifier)                                            \
  create_async_declaration(class_name_##_##method_name_##_cb##modifier, locals, invoked_async_methods);                \
  create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier,                       \
                          sizeof(struct class_name_##_##method_name_##_cb##modifier##_s), async);                      \
  DEFINE_ASYNC(class_name_##_##method_name_##_cb##modifier)

#define DECLARE_ASYNC_NATIVE(package_path, class_name_, method_name_, method_descriptor_, locals,                      \
                             invoked_async_methods)                                                                    \
  force_expand_args(DECLARE_ASYNC_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, locals, invoked_async_methods, __COUNTER__)

#define empty(...)

#define CreateJavaMethodBinding(binding_name, return_type, class_name, method_name, method_descriptor, argc_, args_)   \
  DECLARE_ASYNC(return_type, binding_name, \
    locals(), \
    bjvm_thread *thread; object receiver; args_;, \
    invoked_methods(invoked_method(call_interpreter)) \
  );                                                         \
                                                                                                                       \
  DEFINE_ASYNC_(, empty, binding_name) {                                                                           \
    /* inline cache here? */                                                                                           \
    bjvm_cp_method *method =                                                                                           \
        bjvm_method_lookup(self->args.receiver->descriptor, STR(method_name), STR(method_descriptor), true, true);     \
    assert(method);                                                                                                    \
    assert((sizeof(self->args) - sizeof(bjvm_thread *)) / sizeof(bjvm_stack_value) ==                                  \
           method->descriptor->args_count + 1);                                                                        \
    assert((sizeof(self->args) - sizeof(bjvm_thread *)) % sizeof(bjvm_stack_value) == 0);                              \
    AWAIT_INNER_(empty, &self->invoked_async_methods.call_interpreter, call_interpreter, self->args.thread, method,                \
                 (bjvm_stack_value *)self->args.receiver);                                                             \
    bjvm_stack_value result = get_async_result(call_interpreter);                                                            \
    ASYNC_END(*((return_type *)&result));                                                                              \
  }

#define CallMethod(receiver, name, desc, result, ...)                                                                  \
  do {                                                                                                                 \
    bjvm_cp_method *method = bjvm_method_lookup(receiver->descriptor, STR(name), STR(desc), true, true);               \
    assert(method);                                                                                                    \
    bjvm_stack_value args[] = {receiver, __VA_ARGS__};                                                                 \
    assert((sizeof(args) / sizeof(args[0])) == method->descriptor.args_count);                                         \
    AWAIT(call_interpreter, thread, method, &args);                                                                          \
    if (result != nullptr) {                                                                                           \
      *result = get_async_result(call_interpreter);                                                                          \
    }                                                                                                                  \
  } while (0)
#endif