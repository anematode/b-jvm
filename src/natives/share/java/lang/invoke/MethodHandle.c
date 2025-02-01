#include "bjvm.h"
#include <natives-dsl.h>

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandle, linkToVirtual,
               "([Ljava/lang/Object;)Ljava/lang/Object;",
               locals(bjvm_stack_value *unhandled;), invoked_methods(invoked_method(call_interpreter))) {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  self->unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  if (!self->unhandled) {
    bjvm_out_of_memory(thread);
    ASYNC_RETURN(value_null());
  }

  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = (i == 0)
                              ? BJVM_TYPE_KIND_REFERENCE
                              : field_to_kind(&method->descriptor->args[i - 1]);
    self->unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
    : load_stack_value(&args[i], kind);
  }

  method = bjvm_vtable_lookup(args[0].handle->obj->descriptor, method->vtable_index);
  assert(method);

  AWAIT(call_interpreter, thread, method, self->unhandled);

  free(self->unhandled);
  ASYNC_END(get_async_result(call_interpreter));
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandle, linkToInterface,
               "([Ljava/lang/Object;)Ljava/lang/Object;",
               locals(bjvm_stack_value *unhandled;), invoked_methods(invoked_method(call_interpreter))) {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  self->unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  if (!self->unhandled) {
    bjvm_out_of_memory(thread);
    ASYNC_RETURN(value_null());
  }

  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = (i == 0)
                              ? BJVM_TYPE_KIND_REFERENCE
                              : field_to_kind(&method->descriptor->args[i - 1]);
    self->unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
    : load_stack_value(&args[i], kind);
  }

  method = bjvm_itable_lookup(args[0].handle->obj->descriptor, method->my_class, method->itable_index);
  assert(method);

  AWAIT(call_interpreter, thread, method, self->unhandled);
  free(self->unhandled);
  ASYNC_END(get_async_result(call_interpreter));
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandle, linkToSpecial,
               "([Ljava/lang/Object;)Ljava/lang/Object;",
               locals(bjvm_stack_value *unhandled;), invoked_methods(invoked_method(call_interpreter))) {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  self->unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  if (!self->unhandled) {
    bjvm_out_of_memory(thread);
    ASYNC_RETURN(value_null());
  }

  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = (i == 0)
                              ? BJVM_TYPE_KIND_REFERENCE
                              : field_to_kind(&method->descriptor->args[i - 1]);
    self->unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
    : load_stack_value(&args[i], kind);
  }


  AWAIT(call_interpreter, thread, method, self->unhandled);
  free(self->unhandled);
  ASYNC_END(get_async_result(call_interpreter));
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandle, linkToStatic,
               "([Ljava/lang/Object;)Ljava/lang/Object;",
               locals(bjvm_stack_value *unhandled;), invoked_methods(invoked_method(call_interpreter))) {
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  self->unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  if (!self->unhandled) {
    bjvm_out_of_memory(thread);
    ASYNC_RETURN(value_null());
  }

  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = field_to_kind(&method->descriptor->args[i]);
    self->unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
                       : load_stack_value(&args[i], kind);
  }

  AWAIT(call_interpreter, thread, method, self->unhandled);
  free(self->unhandled);
  ASYNC_END(get_async_result(call_interpreter));
}