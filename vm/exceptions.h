#ifndef EXCEPTIONS_INL_H_
#define EXCEPTIONS_INL_H_

#include "objects.h"

#include <bjvm.h>

__attribute__((noinline))
void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj);

// Helper function to raise VM-generated exceptions
__attribute__((noinline))
int bjvm_raise_vm_exception(bjvm_thread *thread, slice exception_name, slice msg_modified_utf8);

// Raise an ArithmeticException.
__attribute__((noinline))
void raise_div0_arithmetic_exception(bjvm_thread *thread) ;

// Raise an UnsatisfiedLinkError relating to the given method.
__attribute__((noinline))
void raise_unsatisfied_link_error(bjvm_thread *thread, const bjvm_cp_method *method) ;

// Raise an AbstractMethodError relating to the given method.
__attribute__((noinline))
void raise_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method *method) ;

// Raise a NegativeArraySizeException with the given count value.
__attribute__((noinline))
void raise_negative_array_size_exception(bjvm_thread *thread, int count) ;

// Raise a NullPointerException.
__attribute__((noinline))
void raise_null_pointer_exception(bjvm_thread *thread) ;

// Raise an ArrayStoreException.
__attribute__((noinline))
void raise_array_store_exception(bjvm_thread *thread, slice class_name) ;

// Raise an IncompatibleClassChangeError.
__attribute__((noinline))
void raise_incompatible_class_change_error(bjvm_thread *thread, const slice complaint) ;

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
__attribute__((noinline))
void raise_array_index_oob_exception(bjvm_thread *thread, int index, int length) ;

// Raise a ClassCastException regarding the two class descriptors
__attribute__((noinline))
void raise_class_cast_exception(bjvm_thread *thread, const bjvm_classdesc *from, const bjvm_classdesc *to) ;

__attribute__((noinline))
void raise_illegal_monitor_state_exception(bjvm_thread *thread) ;

#endif