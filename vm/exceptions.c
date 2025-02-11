#include <exceptions.h>
#include <objects.h>

void raise_exception_object(vm_thread *thread, obj_header *obj) {
#if AGGRESSIVE_DEBUG
  printf("Raising exception of type %s\n", obj->descriptor->name);
#endif

#define T ((struct native_Throwable *)obj)
  if (arrlen(thread->frames) > 0) {
    stack_frame *frame = arrlast(thread->frames);
    if (!is_frame_native(frame)) {
      T->faulting_insn = frame->plain.program_counter;
      T->method = frame->method;
    }
  }
  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int raise_vm_exception(vm_thread *thread, const slice exception_name, slice msg_utf8) {
  classdesc *classdesc = bootstrap_lookup_class(thread, exception_name);
  DCHECK(!thread->current_exception);
  DCHECK(classdesc->state == CD_STATE_INITIALIZED, "VM-generated exceptions should be initialised at VM boot");

  // Create the exception object
  handle *handle = make_handle(thread, new_object(thread, classdesc));

  if (msg_utf8.chars) {
    object msg = MakeJStringFromModifiedUTF8(thread, msg_utf8, false);
    cp_method *method = method_lookup(classdesc, STR("<init>"), STR("(Ljava/lang/String;)V"), true, false);
    call_interpreter_synchronous(thread, method, (stack_value[]){{.obj = handle->obj}, {.obj = msg}}); // no return val, it's a constructor
  } else {
    cp_method *method = method_lookup(classdesc, STR("<init>"), STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, (stack_value[]){{.obj = handle->obj}}); // no return val, it's a constructor
  }

#ifndef EMSCRIPTEN
  // fprintf(stderr, "Exception: %.*s: %.*s\n", fmt_slice(exception_name), fmt_slice(exception_string));
#endif
  raise_exception_object(thread, handle->obj);

  drop_handle(thread, handle);
  return 0;
}

void raise_div0_arithmetic_exception(vm_thread *thread){
  raise_vm_exception(thread, STR("java/lang/ArithmeticException"), STR("/ by zero"));
}

void raise_unsatisfied_link_error(vm_thread *thread, const cp_method *method){
  printf("Unsatisfied link error %.*s on %.*s\n", fmt_slice(method->name), fmt_slice(method->my_class->name));
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Method %.*s on class %.*s with descriptor %.*s", fmt_slice(method->name),
          fmt_slice(method->my_class->name), fmt_slice(method->unparsed_descriptor));
  raise_vm_exception(thread, STR("java/lang/UnsatisfiedLinkError"), err);
}

void raise_abstract_method_error(vm_thread *thread, const cp_method *method){
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Found no concrete implementation of %.*s", fmt_slice(method->name), fmt_slice(method->my_class->name));
  raise_vm_exception(thread, STR("java/lang/AbstractMethodError"), err);
}

void raise_negative_array_size_exception(vm_thread *thread, int count){
  INIT_STACK_STRING(err, 12);
  bprintf(err, "%d", count);
  raise_vm_exception(thread, STR("java/lang/NegativeArraySizeException"), err);
}

void raise_null_pointer_exception(vm_thread *thread){
  raise_vm_exception(thread, STR("java/lang/NullPointerException"), null_str());
}

void raise_array_store_exception(vm_thread *thread, const heap_string *class_name){
  raise_vm_exception(thread, STR("java/lang/ArrayStoreException"), hslc(*(heap_string*)class_name));
}

void raise_incompatible_class_change_error(vm_thread *thread, const slice complaint){
  raise_vm_exception(thread, STR("java/lang/IncompatibleClassChangeError"), complaint);
}

void raise_array_index_oob_exception(vm_thread *thread, int index, int length){
  INIT_STACK_STRING(complaint, 80);
  bprintf(complaint, "Index %d out of bounds for array of length %d", index, length);
  raise_vm_exception(thread, STR("java/lang/ArrayIndexOutOfBoundsException"), complaint);
}

void raise_class_cast_exception(vm_thread *thread, const classdesc *from, const classdesc *to){
  INIT_STACK_STRING(complaint, 1000);
  INIT_STACK_STRING(from_str, 1000);
  INIT_STACK_STRING(to_str, 1000);

  exchange_slashes_and_dots(&from_str, hslc(from->name));
  exchange_slashes_and_dots(&to_str, hslc(to->name));

  complaint = bprintf(complaint, "%.*s cannot be cast to %.*s", fmt_slice(from_str), fmt_slice(to_str));
  raise_vm_exception(thread, STR("java/lang/ClassCastException"), complaint);
}

void raise_illegal_monitor_state_exception(vm_thread *thread){
  raise_vm_exception(thread, STR("java/lang/IllegalMonitorStateException"), null_str());
}