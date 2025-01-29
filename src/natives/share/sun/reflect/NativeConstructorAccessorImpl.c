#include <natives-dsl.h>

CreateJavaMethodBinding(init_ite, object, "java/lang/reflect/InvocationTargetException", "<init>",
                        "(Ljava/lang/Throwable;)V", 1, object throwable);

DECLARE_ASYNC_NATIVE("sun/reflect", NativeConstructorAccessorImpl, newInstance0,
                     "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;",
                     locals(bjvm_stack_value *forward_args; bjvm_obj_header * instance),
                     invoked_methods(invoked_method(run_thread) invoked_method(init_ite)
                                         invoked_method(bjvm_initialize_class))) {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].handle->obj);

  AWAIT(bjvm_initialize_class, thread, method->my_class);
  if (thread->current_exception)
    ASYNC_RETURN(value_null());

  method = *bjvm_unmirror_ctor(args[0].handle->obj);
  self->instance = new_object(thread, method->my_class);
  self->forward_args = malloc(sizeof(bjvm_stack_value) * *ArrayLength(args[1].handle->obj));
  self->forward_args[0] = (bjvm_stack_value){.obj = self->instance};
  if (args[1].handle->obj) {
    for (int i = 0; i < *ArrayLength(args[1].handle->obj); ++i) {
      self->forward_args[i + 1] = *((bjvm_stack_value *)ArrayData(args[1].handle->obj) + i);
    }
  }

  AWAIT(run_thread, thread, method, self->forward_args);

  free(self->forward_args);

  if (thread->current_exception) {
    bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, STR("java/lang/reflect/InvocationTargetException"));

    object wrapper = new_object(thread, classdesc);
    object exception = thread->current_exception;

    thread->current_exception = nullptr;
    AWAIT(init_ite, thread, wrapper, exception);
    assert(thread->current_exception == nullptr);
#undef reset_except

    thread->current_exception = obj->obj;
  }

  ASYNC_END((bjvm_stack_value){.obj = self->instance});
}
