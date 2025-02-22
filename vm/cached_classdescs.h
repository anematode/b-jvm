//
// Created by alec on 1/17/25.
//

#ifndef CACHED_CLASSDESCS_H
#define CACHED_CLASSDESCS_H

#include "bjvm.h"

#define __CACHED_EXCEPTION_CLASSES(X)                                                                                  \
  X(string, "java/lang/String")                                                                                        \
  X(class_not_found_exception, "java/lang/ClassNotFoundException")                                                     \
  X(stack_trace_element, "java/lang/StackTraceElement")                                                                \
  X(array_store_exception, "java/lang/ArrayStoreException")                                                            \
  X(class_cast_exception, "java/lang/ClassCastException")                                                              \
  X(null_pointer_exception, "java/lang/NullPointerException")                                                          \
  X(negative_array_size_exception, "java/lang/NegativeArraySizeException")                                             \
  X(oom_error, "java/lang/OutOfMemoryError")                                                                           \
  X(stack_overflow_error, "java/lang/StackOverflowError")                                                              \
  X(exception_in_initializer_error, "java/lang/ExceptionInInitializerError")                                           \
  X(unsatisfied_link_error, "java/lang/UnsatisfiedLinkError")                                                          \
  X(array_index_out_of_bounds_exception, "java/lang/ArrayIndexOutOfBoundsException")                                   \
  X(class_circle_error, "java/lang/ClassCircularityError")                                                             \
  X(incompatible_class_change_error, "java/lang/IncompatibleClassChangeError")                                         \
  X(abstract_method_error, "java/lang/AbstractMethodError")                                                            \
  X(illegal_access_error, "java/lang/IllegalAccessError")                                                              \
  X(illegal_argument_exception, "java/lang/IllegalArgumentException")                                                  \
  X(arithmetic_exception, "java/lang/ArithmeticException")                                                             \
  X(direct_method_handle_holder, "java/lang/invoke/DirectMethodHandle$Holder")                                         \
  X(delegating_method_handle_holder, "java/lang/invoke/DelegatingMethodHandle$Holder")                                 \
  X(lf_holder, "java/lang/invoke/LambdaForm$Holder")                                                                   \
  X(file_not_found_exception, "java/io/FileNotFoundException")                                                         \
  X(io_exception, "java/io/IOException")                                                                               \
  X(interrupted_exception, "java/lang/InterruptedException")                                                           \
  X(class_format_error, "java/lang/ClassFormatError")                                                                  \
  X(clone_not_supported_exception, "java/lang/CloneNotSupportedException")                                             \
  X(illegal_monitor_state_exception, "java/lang/IllegalMonitorStateException")                                         \
  X(internal_error, "java/lang/InternalError")                                         \
  X(illegal_state_exception, "java/lang/IllegalStateException")

#define __CACHED_REFLECTION_CLASSES(X)                                                                                 \
  X(klass, "java/lang/Class")                                                                                          \
  X(field, "java/lang/reflect/Field")                                                                                  \
  X(method, "java/lang/reflect/Method")                                                                                \
  X(parameter, "java/lang/reflect/Parameter")                                                                          \
  X(constructor, "java/lang/reflect/Constructor")                                                                      \
  X(method_handle_natives, "java/lang/invoke/MethodHandleNatives")                                                     \
  X(method_handles, "java/lang/invoke/MethodHandles")                                                                  \
  X(method_type, "java/lang/invoke/MethodType")                                                                        \
  X(constant_pool, "jdk/internal/reflect/ConstantPool")

#define __CACHED_GENERAL_CLASSES(X)                                                                                    \
  X(object, "java/lang/Object")                                                                                        \
  X(thread, "java/lang/Thread")                                                                                        \
  X(thread_group, "java/lang/ThreadGroup")                                                                             \
  X(module, "java/lang/Module")                                                                                        \
  X(system, "java/lang/System")                                                                                        \
  X(cloneable, "java/lang/Cloneable")                                                                                  \
  X(integer, "java/lang/Integer")                                                                                      \
  X(long_, "java/lang/Long")                                                                                           \
  X(float_, "java/lang/Float")                                                                                         \
  X(double_, "java/lang/Double")                                                                                       \
  X(throwable, "java/lang/Throwable")

#define CACHED_CLASSDESCS(X)                                                                                           \
  __CACHED_EXCEPTION_CLASSES(X)                                                                                        \
  __CACHED_REFLECTION_CLASSES(X)                                                                                       \
  __CACHED_GENERAL_CLASSES(X)

/// A list of the names of classes that are cached in the VM.
/// This list is in the same order as the fields of the cached_classdescs
/// struct.
static const char *const cached_classdesc_paths[] = {
#define X(name, str) str,
    CACHED_CLASSDESCS(X)
#undef X
};

/// The number of cached classdescs.
static constexpr int cached_classdesc_count = sizeof(cached_classdesc_paths) / sizeof(char *);

/// A struct containing commonly used classdescs.
struct cached_classdescs {
#define X(name, str) classdesc *name;
  CACHED_CLASSDESCS(X)
#undef X
};

// should be layout-compatible with an array of pointers
static_assert(sizeof(struct cached_classdescs) == sizeof(struct cached_classdescs *[cached_classdesc_count]));

#undef __CACHED_EXCEPTION_CLASSES
#undef __CACHED_REFLECTION_CLASSES
#undef __CACHED_GENERAL_CLASSES
#undef CACHED_CLASSDESCS

#endif // CACHED_CLASSDESCS_H
