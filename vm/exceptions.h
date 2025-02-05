#ifndef EXCEPTIONS_INL_H_
#define EXCEPTIONS_INL_H_

#include "objects.h"

#include <bjvm.h>

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj);

// Helper function to raise VM-generated exceptions
int bjvm_raise_vm_exception(bjvm_thread *thread, slice exception_name, slice msg_modified_utf8);

// Raise an ArithmeticException.
static inline void raise_div0_arithmetic_exception(bjvm_thread *thread) {
  bjvm_raise_vm_exception(thread, STR("java/lang/ArithmeticException"), STR("/ by zero"));
}

// Raise an UnsatisfiedLinkError relating to the given method.
static inline void raise_unsatisfied_link_error(bjvm_thread *thread, const bjvm_cp_method *method) {
  printf("Unsatisfied link error %.*s on %.*s\n", fmt_slice(method->name), fmt_slice(method->my_class->name));
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Method %.*s on class %.*s with descriptor %.*s", fmt_slice(method->name),
          fmt_slice(method->my_class->name), fmt_slice(method->unparsed_descriptor));
  bjvm_raise_vm_exception(thread, STR("java/lang/UnsatisfiedLinkError"), err);
}

// Raise an AbstractMethodError relating to the given method.
static inline void raise_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method *method) {
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Found no concrete implementation of %.*s", fmt_slice(method->name), fmt_slice(method->my_class->name));
  bjvm_raise_vm_exception(thread, STR("java/lang/AbstractMethodError"), err);
}

// Raise a NegativeArraySizeException with the given count value.
static inline void raise_negative_array_size_exception(bjvm_thread *thread, int count) {
  INIT_STACK_STRING(err, 12);
  bprintf(err, "%d", count);
  bjvm_raise_vm_exception(thread, STR("java/lang/NegativeArraySizeException"), err);
}

// Raise a NullPointerException.
static inline void raise_null_pointer_exception(bjvm_thread *thread) {
  bjvm_raise_vm_exception(thread, STR("java/lang/NullPointerException"), null_str());
}

// Raise an ArrayStoreException.
static inline void raise_array_store_exception(bjvm_thread *thread, slice class_name) {
  bjvm_raise_vm_exception(thread, STR("java/lang/ArrayStoreException"), class_name);
}

// Raise an IncompatibleClassChangeError.
static inline void raise_incompatible_class_change_error(bjvm_thread *thread, const slice complaint) {
  bjvm_raise_vm_exception(thread, STR("java/lang/IncompatibleClassChangeError"), complaint);
}

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
static inline void raise_array_index_oob_exception(bjvm_thread *thread, int index, int length) {
  INIT_STACK_STRING(complaint, 80);
  bprintf(complaint, "Index %d out of bounds for array of length %d", index, length);
  bjvm_raise_vm_exception(thread, STR("java/lang/ArrayIndexOutOfBoundsException"), complaint);
}

#endif