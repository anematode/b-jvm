#include "bjvm.h"
#include <natives.h>

// TODO make these async
DECLARE_NATIVE("java/lang/invoke", MethodHandle, linkToVirtual,
               "([Ljava/lang/Object;)Ljava/lang/Object;") {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  bjvm_stack_value *unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = (i == 0)
                              ? BJVM_TYPE_KIND_REFERENCE
                              : field_to_kind(&method->descriptor->args[i - 1]);
    unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
                       : load_stack_value(&args[i], kind);
  }


  bjvm_stack_value result;
  bjvm_thread_run_root(thread, method, unhandled, &result);
  free(unhandled);
  return result;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandle, linkToSpecial,
               "([Ljava/lang/Object;)Ljava/lang/Object;") {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);

  bjvm_stack_value *unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = (i == 0)
                              ? BJVM_TYPE_KIND_REFERENCE
                              : field_to_kind(&method->descriptor->args[i - 1]);
    unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
    : load_stack_value(&args[i], kind);
  }


  bjvm_stack_value result;
  bjvm_thread_run_root(thread, method, unhandled, &result);
  free(unhandled);
  return result;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandle, linkToStatic,
               "([Ljava/lang/Object;)Ljava/lang/Object;") {
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].handle->obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);
  bjvm_stack_value *unhandled = malloc(sizeof(bjvm_stack_value) * argc);
  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    bjvm_type_kind kind = field_to_kind(&method->descriptor->args[i]);
    unhandled[j] = kind == BJVM_TYPE_KIND_REFERENCE
                       ? (bjvm_stack_value){.obj = args[i].handle->obj}
                       : load_stack_value(&args[i], kind);
  }

  bjvm_stack_value result;
  bjvm_thread_run_root(thread, method, unhandled, &result);
  free(unhandled);
  return result;
}