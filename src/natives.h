#ifndef BJVM_NATIVES_H
#define BJVM_NATIVES_H

#include "bjvm.h"

#define ThrowLangException(exception_name)                                     \
  bjvm_raise_exception(thread, str("java/lang/" #exception_name), null_str())

#define ThrowLangExceptionM(exception_name, fmt, ...)                          \
  do {                                                                         \
    char msg[1024];                                                            \
    snprintf(msg, 1024, fmt, __VA_ARGS__);                                     \
    bjvm_raise_exception(thread, L"java/lang/" exception_name, msg);           \
  } while (0)

extern size_t bjvm_native_count;
extern size_t bjvm_native_capacity;
extern bjvm_native_t *bjvm_natives;

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)                     \
  static bjvm_stack_value bjvm_native_##class_name_##_##method_name_##_cb##modifier(     \
      bjvm_thread *thread, bjvm_obj_header *obj, bjvm_stack_value *args,       \
      int argc)

#define DEFINE_NATIVE_INFO(package_path, class_name_, method_name_, \
                           method_descriptor_, modifier)                                 \
  __attribute__((constructor)) static void                           \
      bjvm_native_##class_name_##_##method_name_##_init##modifier() {                    \
    if (bjvm_native_count == bjvm_native_capacity) {                           \
      bjvm_native_capacity =                                                   \
          bjvm_native_capacity ? bjvm_native_capacity * 2 : 16;                \
      bjvm_natives =                                                           \
          realloc(bjvm_natives, bjvm_native_capacity * sizeof(bjvm_native_t)); \
    }                                                                          \
    bjvm_natives[bjvm_native_count++] = (bjvm_native_t){                       \
        .class_path = str(package_path "/" #class_name_),                      \
        .method_name = str(#method_name_),                                     \
        .method_descriptor = str(method_descriptor_),                          \
        .callback = &bjvm_native_##class_name_##_##method_name_##_cb##modifier};         \
  }

#define DECLARE_NATIVE0(package_path, class_name_, method_name_, method_descriptor_, modifier)                                     \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier);                          \
  DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,                  \
                     method_descriptor_, modifier);                                      \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_, modifier)

#define DECLARE_NATIVE(package_path, class_name_, method_name_, method_descriptor_) \
    DECLARE_NATIVE0(package_path, class_name_, method_name_, method_descriptor_, __COUNTER__)

#endif // BJVM_NATIVES_H