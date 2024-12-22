#include <natives.h>
#include "bjvm.h"

DECLARE_NATIVE("java/lang/invoke", MethodHandle, linkToVirtual,
               "([Ljava/lang/Object;)Ljava/lang/Object;") {
  // first argc - 1 args should be passed on the stack, last arg is a MemberName
  // which should be used to resolve the method to call
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);
  bjvm_stack_frame *new_frame = bjvm_push_frame(thread, method);
  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    new_frame->values[new_frame->max_stack + j] = args[i];
    if (i >= 1)
      j += bjvm_is_field_wide(method->parsed_descriptor->args[i - 1]);
  }
  bjvm_stack_value result;
  bjvm_bytecode_interpret(thread, new_frame, &result);
  bjvm_pop_frame(thread, new_frame);
  return result;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandle, linkToStatic,
               "([Ljava/lang/Object;)Ljava/lang/Object;") {
  struct bjvm_native_MemberName *mn = (void *)args[argc - 1].obj;
  bjvm_cp_method *method = mn->vmtarget;
  assert(method);
  bjvm_stack_frame *new_frame = bjvm_push_frame(thread, method);
  for (int i = 0, j = 0; i < argc - 1; ++i, ++j) {
    new_frame->values[new_frame->max_stack + j] = args[i];
    j += bjvm_is_field_wide(method->parsed_descriptor->args[i]);
  }
  bjvm_stack_value result;
  bjvm_bytecode_interpret(thread, new_frame, &result);
  bjvm_pop_frame(thread, new_frame);
  return result;
}