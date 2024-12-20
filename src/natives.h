//
// Created by alec on 12/19/24.
// This file contains definitions useful for implementing native methods.
//

#ifndef BJVM_NATIVES_H
#define BJVM_NATIVES_H

#include "bjvm.h"

#define ThrowLangException(exception_name)                                     \
  bjvm_raise_exception(thread, "java/lang/" #exception_name, nullptr)

#define ThrowLangExceptionM(exception_name, fmt, ...)                          \
  do {                                                                         \
    char msg[1024];                                                         \
    snprintf(msg, 1024, fmt, __VA_ARGS__);                                     \
    bjvm_raise_exception(thread, L"java/lang/" exception_name, msg);           \
  } while (0)

#ifdef __APPLE__
#define NATIVE_SECTION_NAME "__DATA,__native"
#else
#define NATIVE_SECTION_NAME ".native"
#endif

#if defined(__has_feature) && __has_feature(address_sanitizer)
#define BJVM_NATIVECALL                                                        \
  __attribute__((section(NATIVE_SECTION_NAME), no_sanitize_address))
#else
#define BJVM_NATIVECALL __attribute__((section(NATIVE_SECTION_NAME)))
#endif

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_)                     \
  static bjvm_stack_value bjvm_native_##class_name_##_##method_name_##_cb(     \
      bjvm_thread *thread, bjvm_obj_header *obj, bjvm_stack_value *args,       \
      int argc)

#define DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,            \
                           method_descriptor_)                                 \
  BJVM_NATIVECALL const bjvm_native_t                                          \
      bjvm_native_##class_name_##_##method_name_##_info = {                    \
          .class_path = package_path "/" #class_name_,                         \
          .method_name = #method_name_,                                        \
          .method_descriptor = method_descriptor_,                             \
          .callback = &bjvm_native_##class_name_##_##method_name_##_cb}

#define DECLARE_NATIVE(package_path, class_name_, method_name_,                \
                       method_descriptor_)                                     \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_);                          \
  DEFINE_NATIVE_INFO(package_path, class_name_, method_name_,                  \
                     method_descriptor_);                                      \
  DECLARE_NATIVE_CALLBACK(class_name_, method_name_)

#endif // BJVM_NATIVES_H
