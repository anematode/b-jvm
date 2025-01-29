#include <natives-dsl.h>

DECLARE_NATIVE("sun/reflect", NativeMethodAccessorImpl, invoke0,
               "(Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/"
               "Object;)Ljava/lang/Object;") {
  // TODO add a lot of checks
  bjvm_cp_method *method = *bjvm_unmirror_method(args[0].handle->obj);
  bjvm_stack_value assembled[256];
  int arg_i = 0;
  bool is_static = !!(method->access_flags & BJVM_ACCESS_STATIC);
  if (!is_static) {
    assembled[arg_i++] = (bjvm_stack_value){.obj = args[1].handle->obj};
  }

  int args_count = method->descriptor->args_count;
  bjvm_obj_header **data = ArrayData(args[2].handle->obj);
  for (int i = 0; i < args_count; i++) {
    assembled[arg_i++] = (bjvm_stack_value){.obj = data[i]};
  }

  bjvm_stack_value result;
  assert(method->code);
  // TODO make this native async
  bjvm_thread_run_root(thread, method, assembled, &result);

  if (thread->current_exception) {
    bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, STR("java/lang/reflect/InvocationTargetException"));
    bjvm_obj_header *obj = new_object(thread, classdesc);

    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("(Ljava/lang/Throwable;)V"), true, false);
    int result = bjvm_thread_run_leaf(thread, method, (bjvm_stack_value[]){{.obj = obj}, {.obj = thread->current_exception}}, nullptr);
    assert(result == 0);

    thread->current_exception = obj;
  }

  return result;
}
