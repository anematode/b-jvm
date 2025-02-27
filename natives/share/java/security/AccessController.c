#include <natives-dsl.h>

// invokes call_interpreter and returns the result
#define IMPL_RUN_ASYNC(thread_, target_, context_)                                                                     \
  stack_value ret;                                                                                                     \
  do {                                                                                                                 \
    vm_thread *thread__ = thread_;                                                                                     \
    obj_header *target__ = target_;                                                                                    \
    obj_header *context__ = context_;                                                                                  \
    classdesc *classdesc__ = target_->descriptor;                                                                      \
    (void)context__; /* stop the compiler complaining */                                                               \
                                                                                                                       \
    DCHECK(classdesc__->kind == CD_KIND_ORDINARY);                                                                     \
    cp_method *method__ = method_lookup(classdesc__, STR("run"), null_str(), true, true);                              \
    if (!method__) {                                                                                                   \
      /* TODO figure out what JVM normally does here */                                                                \
      UNREACHABLE();                                                                                                   \
    }                                                                                                                  \
    stack_value method_args__[1] = {(stack_value){.obj = target__}};                                                   \
    AWAIT(call_interpreter, thread__, method__, method_args__);                                                        \
    ret = get_async_result(call_interpreter);                                                                          \
  } while (0);                                                                                                         \
  ASYNC_END(ret);

DECLARE_ASYNC_NATIVE("java/security", AccessController, doPrivileged,
                     "(Ljava/security/PrivilegedExceptionAction;Ljava/security/"
                     "AccessControlContext;)Ljava/lang/Object;",
                     locals(), invoked_methods(invoked_method(call_interpreter))) {
  DCHECK(argc == 2);

  IMPL_RUN_ASYNC(thread, args[0].handle->obj, args[1].handle->obj);
}

DECLARE_ASYNC_NATIVE_OVERLOADED("java/security", AccessController, doPrivileged,
                                "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;", locals(),
                                invoked_methods(invoked_method(call_interpreter)), 1) {
  DCHECK(argc == 1);

  IMPL_RUN_ASYNC(thread, args[0].handle->obj, nullptr);
}

DECLARE_NATIVE("java/security", AccessController, getStackAccessControlContext,
               "()Ljava/security/AccessControlContext;") {
  return value_null();
}

DECLARE_ASYNC_NATIVE_OVERLOADED("java/security", AccessController, doPrivileged,
                                "(Ljava/security/PrivilegedAction;Ljava/security/"
                                "AccessControlContext;)Ljava/lang/Object;",
                                locals(), invoked_methods(invoked_method(call_interpreter)), 2) {
  DCHECK(argc == 2);

  IMPL_RUN_ASYNC(thread, args[0].handle->obj, args[1].handle->obj);
}

DECLARE_NATIVE("java/security", AccessController, ensureMaterializedForStackWalk, "(Ljava/lang/Object;)V") {
  return value_null();
}

DECLARE_ASYNC_NATIVE_OVERLOADED("java/security", AccessController, doPrivileged,
                                "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;", locals(),
                                invoked_methods(invoked_method(call_interpreter)), 3) {
  // Look up method "run" on obj
  DCHECK(argc == 1);

  IMPL_RUN_ASYNC(thread, args[0].handle->obj, nullptr);
}

DECLARE_NATIVE("java/security", AccessController, getProtectionDomain,
               "(Ljava/lang/Class;)Ljava/security/ProtectionDomain;") {
  return value_null();
}