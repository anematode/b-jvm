#ifndef NATIVES_DSL_H
#define NATIVES_DSL_H

#include "arrays.h"
#include "bjvm.h"
#include <objects.h>
#include <exceptions.h>
#include <natives-dsl.h>
#include <stddef.h>

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

maybe_extern_begin;
void push_native(slice class_name, slice method_name, slice signature, native_callback native);

maybe_extern_end;

static obj_header *check_is_object(obj_header *thing) { return thing; }

#define AsHeapString(expr, on_oom)                                                                                     \
  ({                                                                                                                   \
    obj_header *__val = check_is_object(expr);                                                                         \
    heap_string __hstr;                                                                                                \
    if (read_string_to_utf8(thread, &__hstr, __val) != 0) {                                                            \
      DCHECK(thread->current_exception);                                                                               \
      goto on_oom;                                                                                                     \
    }                                                                                                                  \
    __hstr;                                                                                                            \
  })

#define HandleIsNull(expr) ((expr)->obj == nullptr)

#define force_expand_args(macro_name, ...) macro_name(__VA_ARGS__)

extern size_t native_count;
extern size_t native_capacity;
extern native_t *natives;

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)                                                   \
  __attribute__((used)) stack_value class_name_##_##method_name_##_cb##modifier(                                       \
      [[maybe_unused]] vm_thread *thread, [[maybe_unused]] handle *obj, [[maybe_unused]] value *args,                  \
      [[maybe_unused]] u8 argc)

#define create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier, async_sz,       \
                                variant)                                                                               \
  __attribute__((used)) native_t NATIVE_INFO_##class_name_##_##method_name_##_##modifier =                             \
      (native_t){.class_path = STR(package_path "/" #class_name_),                                                     \
                 .method_name = STR(#method_name_),                                                                    \
                 .method_descriptor = STR(method_descriptor_),                                                         \
                 .callback = (native_callback){.async_ctx_bytes = async_sz,                                            \
                                               .variant = &class_name_##_##method_name_##_cb##modifier}};

#define DECLARE_NATIVE_(package_path, class_name_, method_name_, method_descriptor_, modifier)                         \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier);                                                        \
  create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier, 0, sync)              \
      DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)

#define DECLARE_NATIVE(package_path, class_name_, method_name_, method_descriptor_)                                    \
  force_expand_args(DECLARE_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, 0)

#define DECLARE_NATIVE_OVERLOADED(package_path, class_name_, method_name_, method_descriptor_, overload_idx)           \
  force_expand_args(DECLARE_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, overload_idx)

#ifdef __cplusplus
#define check_field_offset(m_name, member_a, member_b)
#else
// this breaks clion for some reason
#define check_field_offset(m_name, member_a, member_b)                                                                 \
  static_assert(offsetof(struct m_name##_s, member_a) == offsetof(async_natives_args, member_b),                       \
                #member_a " mismatch " #member_b);
#endif

#define create_async_declaration(name, locals, async_methods)                                                          \
  DECLARE_ASYNC(                                                                                                       \
    stack_value, \
    name, \
    locals,\
    arguments(vm_thread *thread; handle *obj; value *args; u8 argc), \
    async_methods\
  );       \
  /* the arguments struct for this needs to be compatible with the async_natives_args struct */                        \
  /* todo: maybe just reuse the async_natives_args struct, rather than making a separate (but equivalent) struct for   \
   * each native */                                                                                                    \
  check_field_offset(name, args.thread, args.thread);                                                                  \
  check_field_offset(name, args.obj, args.obj);                                                                        \
  check_field_offset(name, args.args, args.args);                                                                      \
  check_field_offset(name, args.argc, args.argc);                                                                      \
  check_field_offset(name, _state, stage);                                                                             \
  check_field_offset(name, _result, result);

#undef _DECLARE_CACHED_STATE
#undef _RELOAD_CACHED_STATE

#define _DECLARE_CACHED_STATE(_)                                                                                       \
  [[maybe_unused]] vm_thread *thread = self->args.thread;                                                              \
  [[maybe_unused]] value *args = self->args.args;                                                                      \
  [[maybe_unused]] handle *obj = self->args.obj;                                                                       \
  [[maybe_unused]] u8 argc = self->args.argc;

#define _RELOAD_CACHED_STATE()                                                                                         \
  do {                                                                                                                 \
    thread = self->args.thread;                                                                                        \
    args = self->args.args;                                                                                            \
    obj = self->args.obj;                                                                                              \
    argc = self->args.argc;                                                                                            \
  } while (0)

#define DECLARE_ASYNC_NATIVE_(package_path, class_name_, method_name_, method_descriptor_, locals,                     \
                              invoked_async_methods, modifier)                                                         \
  create_async_declaration(class_name_##_##method_name_##_cb##modifier, locals, invoked_async_methods);                \
  create_init_constructor(package_path, class_name_, method_name_, method_descriptor_, modifier,                       \
                          sizeof(struct class_name_##_##method_name_##_cb##modifier##_s), async);                      \
  DEFINE_ASYNC_(, cached_state_prelude, class_name_##_##method_name_##_cb##modifier)

#define DECLARE_ASYNC_NATIVE(package_path, class_name_, method_name_, method_descriptor_, locals,                      \
                             invoked_async_methods)                                                                    \
  force_expand_args(DECLARE_ASYNC_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, locals,        \
                    invoked_async_methods, 0)

#define DECLARE_ASYNC_NATIVE_OVERLOADED(package_path, class_name_, method_name_, method_descriptor_, locals,           \
                                        invoked_async_methods, overload_idx)                                           \
  force_expand_args(DECLARE_ASYNC_NATIVE_, package_path, class_name_, method_name_, method_descriptor_, locals,        \
                    invoked_async_methods, overload_idx)

#define empty(...)

#define CreateJavaMethodBinding(binding_name, return_type, class_name, method_name, method_descriptor, argc_, args_)   \
  DECLARE_ASYNC(return_type, binding_name, \
    locals(), \
    vm_thread *thread; object receiver; args_;, \
    invoked_methods(invoked_method(call_interpreter)) \
  );                                                   \
                                                                                                                       \
  DEFINE_ASYNC_(, empty, binding_name) {                                                                               \
    /* inline cache here? */                                                                                           \
    cp_method *method =                                                                                                \
        method_lookup(self->args.receiver->descriptor, STR(method_name), STR(method_descriptor), true, true);          \
    DCHECK(method);                                                                                                    \
    DCHECK((sizeof(self->args) - sizeof(vm_thread *)) / sizeof(stack_value) == method->descriptor->args_count + 1);    \
    DCHECK((sizeof(self->args) - sizeof(vm_thread *)) % sizeof(stack_value) == 0);                                     \
    AWAIT_INNER_(empty, &self->invoked_async_methods.call_interpreter, call_interpreter, self->args.thread, method,    \
                 (stack_value *)self->args.receiver);                                                                  \
    stack_value result = get_async_result(call_interpreter);                                                           \
    ASYNC_END(*((return_type *)&result));                                                                              \
  }

#define CallMethod(receiver, name, desc, result, ...)                                                                  \
  do {                                                                                                                 \
    cp_method *method = method_lookup(receiver->descriptor, STR(name), STR(desc), true, true);                         \
    DCHECK(method);                                                                                                    \
    stack_value args[] = {receiver, __VA_ARGS__};                                                                      \
    DCHECK((sizeof(args) / sizeof(args[0])) == method->descriptor.args_count);                                         \
    AWAIT(call_interpreter, thread, method, &args);                                                                    \
    if (result != nullptr) {                                                                                           \
      *result = get_async_result(call_interpreter);                                                                    \
    }                                                                                                                  \
  } while (0)
#endif