#include <natives.h>

DECLARE_NATIVE(
    "sun/reflect", NativeConstructorAccessorImpl, newInstance0,
    "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;") {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].handle->obj);
  bjvm_stack_value result;

  bjvm_initialize_class_t pox = {};
  future_t f = bjvm_initialize_class(&pox, thread, method->my_class);
  assert(f.status == FUTURE_READY);
  if (thread->current_exception)
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

  if (thread->current_exception) {
    bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, STR("java/lang/reflect/InvocationTargetException"));
    bjvm_obj_header *obj = new_object(thread, classdesc);

    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("(Ljava/lang/Throwable;)V"), true, false);
    int result = bjvm_thread_run_leaf(thread, method, (bjvm_stack_value[]){{.obj = obj}, {.obj = thread->current_exception}}, nullptr);
    assert(result == 0);

    thread->current_exception = obj;
  }

  return (bjvm_stack_value){.obj = instance};
}
