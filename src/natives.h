#ifndef BJVM_NATIVES_H
#define BJVM_NATIVES_H

#include "arrays.h"
#include "bjvm.h"

#define ThrowLangException(exception_name)                                     \
  bjvm_raise_vm_exception(thread, STR("java/lang/" #exception_name), null_str())

#define ThrowLangExceptionM(exception_name, fmt, ...)                          \
  do {                                                                         \
    char msg[1024];                                                            \
    snprintf(msg, 1024, fmt, __VA_ARGS__);                                     \
    bjvm_raise_vm_exception(thread, L"java/lang/" exception_name, msg);        \
  } while (0)

static inline bjvm_obj_header *check_is_object(bjvm_obj_header *thing) {
  return thing;
}

#define AsHeapString(expr, on_oom)                                             \
  ({                                                                           \
    bjvm_obj_header *__val = check_is_object(expr);                            \
    heap_string __hstr;                                                        \
    if (read_string_to_utf8(thread, &__hstr, __val) !=                         \
        0) {                                               \
      assert(thread->current_exception);                                       \
      goto on_oom;                                                             \
    }                                                                          \
    __hstr;                                                                    \
  })

#define LoadFieldObject(receiver, name, desc)                                  \
  ({                                                                           \
    _Static_assert((desc)[0] == 'L', "descriptor must be an object type");     \
    bjvm_cp_field *field =                                                     \
        bjvm_easy_field_lookup(receiver->descriptor, STR(name), STR(desc));    \
    assert(field && name);                                                     \
    bjvm_get_field(receiver, field).obj;                                       \
  })

#define HandleIsNull(expr) ((expr)->obj == nullptr)

extern size_t bjvm_native_count;
extern size_t bjvm_native_capacity;
extern bjvm_native_t *bjvm_natives;

static void _push_bjvm_native(bjvm_native_t native) {
  if (bjvm_native_count == bjvm_native_capacity) {
    bjvm_native_capacity = bjvm_native_capacity ? bjvm_native_capacity * 2 : 16;
    bjvm_native_t *bjvm_natives_ = (bjvm_native_t *)
        realloc(bjvm_natives, bjvm_native_capacity * sizeof(bjvm_native_t));
    assert(bjvm_natives_ != nullptr);

    bjvm_natives = bjvm_natives_;
  }
  bjvm_natives[bjvm_native_count++] = native;
}

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)           \
  static bjvm_stack_value                                                      \
      bjvm_native_##class_name_##_##method_name_##_cb##modifier(               \
          [[maybe_unused]] bjvm_thread *thread,                                \
          [[maybe_unused]] bjvm_handle *obj,                                   \
          [[maybe_unused]] bjvm_value *args, [[maybe_unused]] int argc)

#define DECLARE_ASYNC_NATIVE_CALLBACK(class_name_, method_name_, modifier)     \
  static bjvm_interpreter_result_t                                             \
      bjvm_native_##class_name_##_##method_name_##_cb##modifier(               \
          [[maybe_unused]] bjvm_thread *thread,                                \
          [[maybe_unused]] bjvm_handle *obj,                                   \
          [[maybe_unused]] bjvm_value *args, [[maybe_unused]] int argc,        \
          [[maybe_unused]] bjvm_stack_value *result,                           \
          [[maybe_unused]] void **sm_state)

#define DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,            \
                           method_descriptor_, modifier, async)                \
  __attribute__((constructor)) static void                                     \
      bjvm_native_##class_name_##_##method_name_##_init##modifier() {          \
    _push_bjvm_native((bjvm_native_t){                                         \
        .class_path = STR(package_path "/" #class_name_),                      \
        .method_name = STR(#method_name_),                                     \
        .method_descriptor = STR(method_descriptor_),                          \
        .callback = {                                                          \
            async,                                                             \
            &bjvm_native_##class_name_##_##method_name_##_cb##modifier}});     \
  }

#define DECLARE_NATIVE0(DECLARE, package_path, class_name_, method_name_,      \
                        method_descriptor_, modifier, async)                   \
  DECLARE(class_name_, method_name_, modifier);                                \
  DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,                  \
                     method_descriptor_, modifier, async)                      \
  DECLARE(class_name_, method_name_, modifier)

#define DECLARE_NATIVE(package_path, class_name_, method_name_,                \
                       method_descriptor_)                                     \
  DECLARE_NATIVE0(DECLARE_NATIVE_CALLBACK, package_path, class_name_,          \
                  method_name_, method_descriptor_, __COUNTER__, false)

#define DECLARE_ASYNC_NATIVE(package_path, class_name_, method_name_,          \
                             method_descriptor_)                               \
  DECLARE_NATIVE0(DECLARE_ASYNC_NATIVE_CALLBACK, package_path, class_name_,    \
                  method_name_, method_descriptor_, __COUNTER__, true)

#endif // BJVM_NATIVES_H