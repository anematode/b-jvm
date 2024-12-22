#include <natives.h>

DECLARE_NATIVE(
    "sun/reflect", NativeConstructorAccessorImpl, newInstance0,
    "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;") {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].handle->obj);
  bjvm_stack_value result;
  if (bjvm_initialize_class(thread, method->my_class))
    return value_null();
  bjvm_obj_header *instance = new_object(thread, method->my_class);
  bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = instance}},
                  &result);
  return (bjvm_stack_value){.obj = instance};
}
