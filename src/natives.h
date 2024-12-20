//
// Created by alec on 12/19/24.
//

#ifndef BJVM_NATIVES_H
#define BJVM_NATIVES_H

#include "bjvm.h"

typedef struct {
  char const* class_path;
  char const* method_name;
  char const* method_descriptor;
  bjvm_native_callback callback;
} bjvm_native_t;

size_t bjvm_get_natives_list(bjvm_native_t const* const (*natives[]));

#ifdef __APPLE__
#define BJVM_NATIVE_SECTION __attribute__((section("__DATA,__native")))
#else
#define BJVM_NATIVE_SECTION __attribute__((section(".native")))
#endif

#define DECLARE_NATIVE_CALLBACK(class_name_, method_name_) \
    static bjvm_stack_value bjvm_native_##class_name_##_##method_name_##_cb( \
        bjvm_thread *vm, bjvm_obj_header *obj, bjvm_stack_value *args, int argc)

#define DEFINE_NATIVE_INFO(package_path, class_name_, method_name_, method_descriptor_) \
    static const bjvm_native_t bjvm_native_##class_name_##_##method_name_info = { \
        .class_path = #package_path "/" #class_name_,                             \
        .method_name = #method_name_,                                            \
        .method_descriptor = method_descriptor_,                                 \
        .callback = &bjvm_native_##class_name_##_##method_name_##_cb             \
    }

#define PLACE_NATIVE_IN_SECTION(class_name_, method_name_) \
    const bjvm_native_t * const BJVM_NATIVE_SECTION \
        bjvm_native_##class_name_##_##method_name_info_p = &bjvm_native_##class_name_##_##method_name_info

#define DECLARE_NATIVE(package_path, class_name_, method_name_, method_descriptor_) \
    DECLARE_NATIVE_CALLBACK(class_name_, method_name_);                             \
    DEFINE_NATIVE_INFO(package_path, class_name_, method_name_, method_descriptor_); \
    PLACE_NATIVE_IN_SECTION(class_name_, method_name_);                             \
    DECLARE_NATIVE_CALLBACK(class_name_, method_name_)

#endif // BJVM_NATIVES_H
