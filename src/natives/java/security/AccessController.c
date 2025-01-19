#include <natives.h>

static bjvm_stack_value impl(bjvm_thread *thread, bjvm_obj_header *target,
                             bjvm_obj_header *context) {
  bjvm_classdesc *classdesc = target->descriptor;
  (void)context;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_method_lookup(classdesc, STR("run"), null_str(), true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = target}};
  bjvm_stack_value ret;
  bjvm_thread_run_root(thread, method, method_args, &ret);
  return ret;
}

DECLARE_NATIVE("java/security", AccessController, doPrivileged,
               "(Ljava/security/PrivilegedExceptionAction;Ljava/security/"
               "AccessControlContext;)Ljava/lang/Object;") {
  assert(argc == 2);

  return impl(thread, args[0].handle->obj, args[1].handle->obj);
}

DECLARE_NATIVE(
    "java/security", AccessController, doPrivileged,
    "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;") {
  // Look up method "run" on obj
  assert(argc == 1);

  return impl(thread, args[0].handle->obj, nullptr);
}

DECLARE_NATIVE("java/security", AccessController, getStackAccessControlContext,
               "()Ljava/security/AccessControlContext;") {
  return value_null();
}

DECLARE_NATIVE("java/security", AccessController, doPrivileged,
               "(Ljava/security/PrivilegedAction;Ljava/security/"
               "AccessControlContext;)Ljava/lang/Object;") {
  assert(argc == 2);

  bjvm_obj_header *target = args[0].handle->obj;
  bjvm_classdesc *classdesc = target->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_method_lookup(classdesc, STR("run"), null_str(), true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = target}};
  bjvm_stack_value ret;
  bjvm_thread_run_root(thread, method, method_args, &ret);
  return ret;
}

DECLARE_NATIVE("java/security", AccessController, ensureMaterializedForStackWalk,
               "(Ljava/lang/Object;)V") {
  return value_null();
}

DECLARE_NATIVE("java/security", AccessController, doPrivileged,
               "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;") {
  // Look up method "run" on obj
  assert(argc == 1);

  bjvm_obj_header *target = args[0].handle->obj;
  bjvm_classdesc *classdesc = target->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_method_lookup(classdesc, STR("run"), null_str(), true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = target}};
  bjvm_stack_value ret;
  bjvm_thread_run_root(thread, method, method_args, &ret);
  return ret;
}
