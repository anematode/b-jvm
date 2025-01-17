#include <natives.h>

DECLARE_NATIVE(
    "sun/reflect", NativeConstructorAccessorImpl, newInstance0,
    "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;") {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].handle->obj);
  bjvm_stack_value result;
  if (bjvm_initialize_class(thread, method->my_class))
    return value_null();
  bjvm_obj_header *instance = new_object(thread, method->my_class);
  bjvm_stack_value forward_args[256];
  forward_args[0] = (bjvm_stack_value){.obj = instance};
  if (args[1].handle->obj) {
    for (int i = 0; i < *ArrayLength(args[1].handle->obj); ++i) {
      forward_args[i + 1] =
          *((bjvm_stack_value *)ArrayData(args[1].handle->obj) + i);
    }
  }
  bjvm_thread_run_root(thread, method, forward_args, &result);
  return (bjvm_stack_value){.obj = instance};
}
