//
// Created by alec on 1/17/25.
//

#ifndef CACHED_CLASSDESCS_H
#define CACHED_CLASSDESCS_H

#include "bjvm.h"

#define __CACHED_EXCEPTION_CLASSES(X)                                          \
  X(array_store_exception, "java/lang/ArrayStoreException")                    \
  X(class_cast_exception, "java/lang/ClassCastException")                      \
  X(null_pointer_exception, "java/lang/NullPointerException")                  \
  X(negative_array_size_exception, "java/lang/NegativeArraySizeException")     \
  X(oom_error, "java/lang/OutOfMemoryError")                                   \
  X(stack_overflow_error, "java/lang/StackOverflowError")                      \
  X(exception_in_initializer_error, "java/lang/ExceptionInInitializerError")


#define __CACHED_REFLECTION_CLASSES(X)                                         \
  X(klass, "java/lang/Class")                                                  \
  X(field, "java/lang/reflect/Field")                                          \
  X(method, "java/lang/reflect/Method")                                        \
  X(parameter, "java/lang/reflect/Parameter")                                  \
  X(constructor, "java/lang/reflect/Constructor")                              \
  X(method_handle_natives, "java/lang/invoke/MethodHandleNatives")             \
  X(method_handles, "java/lang/invoke/MethodHandles")                          \
  X(method_type, "java/lang/invoke/MethodType") \
  X(constant_pool, "sun/reflect/ConstantPool")

#define __CACHED_GENERAL_CLASSES(X)                                            \
  X(object, "java/lang/Object")                                                \
  X(string, "java/lang/String")                                                \
  X(thread, "java/lang/Thread")                                                \
  X(thread_group, "java/lang/ThreadGroup")                                     \
  X(system, "java/lang/System")

#define CACHED_CLASSDESCS(X)                                                   \
  __CACHED_EXCEPTION_CLASSES(X)                                                \
  __CACHED_REFLECTION_CLASSES(X)                                               \
  __CACHED_GENERAL_CLASSES(X)

/// A list of the names of classes that are cached in the VM.
/// This list is in the same order as the fields of the bjvm_cached_classdescs
/// struct.
static const char *const cached_classdesc_paths[] = {
#define X(name, str) str,
    CACHED_CLASSDESCS(X)
#undef X
};

/// The number of cached classdescs.
static constexpr int cached_classdesc_count =
    sizeof(cached_classdesc_paths) / sizeof(char *);

/// A struct containing commonly used classdescs.
struct bjvm_cached_classdescs {
#define X(name, str) bjvm_classdesc *name;
  CACHED_CLASSDESCS(X)
#undef X
};

// should be layout-compatible with an array of pointers
_Static_assert(sizeof(struct bjvm_cached_classdescs) ==
               sizeof(struct bjvm_cached_classdescs *[cached_classdesc_count]));

#undef __CACHED_EXCEPTION_CLASSES
#undef __CACHED_REFLECTION_CLASSES
#undef __CACHED_GENERAL_CLASSES
#undef CACHED_CLASSDESCS

#endif // CACHED_CLASSDESCS_H
