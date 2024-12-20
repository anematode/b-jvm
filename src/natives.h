//
// Created by alec on 12/19/24.
// This file contains definitions useful for implementing native methods.
//

#ifndef BJVM_NATIVES_H
#define BJVM_NATIVES_H

#include "bjvm.h"

#define ThrowLangException(exception_name)                                     \
  bjvm_raise_exception(thread, L"java/lang/" #exception_name, nullptr)

#define Is1DPrimitiveArray(src)                                                \
  ({                                                                           \
    bjvm_obj_header *__obj = src;                                              \
    src->descriptor->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY &&                   \
        src->descriptor->dimensions == 1;                                      \
  })

#define ThrowLangExceptionM(exception_name, fmt, ...)                          \
  do {                                                                         \
    wchar_t msg[1024];                                                         \
    swprintf(msg, 1024, fmt, __VA_ARGS__);                                     \
    bjvm_raise_exception(thread, L"java/lang/" exception_name, msg);           \
  } while (0)

extern size_t bjvm_native_count;
extern bjvm_native_t bjvm_natives[1000];

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_)                     \
  static bjvm_stack_value bjvm_native_##class_name_##_##method_name_##_cb(     \
      bjvm_thread *thread, bjvm_obj_header *obj, bjvm_stack_value *args,       \
      int argc)

#define DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,            \
                           method_descriptor_)                                 \
  __attribute__((constructor)) static void                                     \
      bjvm_native_##class_name_##_##method_name_##_init() {                    \
    bjvm_natives[bjvm_native_count++] = (bjvm_native_t){                       \
        .class_path = package_path "/" #class_name_,                           \
        .method_name = #method_name_,                                          \
        .method_descriptor = method_descriptor_,                               \
        .callback = &bjvm_native_##class_name_##_##method_name_##_cb};         \
  }

#define DECLARE_NATIVE(package_path, class_name_, method_name_,                \
                       method_descriptor_)                                     \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_);                          \
  DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,                  \
                     method_descriptor_);                                      \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_)

#endif // BJVM_NATIVES_H
