#include <natives.h>

DECLARE_NATIVE(
    "java/security", AccessController, doPrivileged,
    "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;") {
  // Look up method "run" on obj
  assert(argc == 1);

  bjvm_obj_header *target = args[0].obj;
  bjvm_classdesc *classdesc = target->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_easy_method_lookup(classdesc, str("run"), null_str(), true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = target}};
  bjvm_stack_value ret;
  bjvm_thread_run(thread, method, method_args, &ret);
  return ret;
}

DECLARE_NATIVE("java/security", AccessController, getStackAccessControlContext,
               "()Ljava/security/AccessControlContext;") {
  return value_null();
}

DECLARE_NATIVE("java/security", AccessController, doPrivileged,
               "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;") {
  // Look up method "run" on obj
  assert(argc == 1);

  bjvm_obj_header *target = args[0].obj;
  bjvm_classdesc *classdesc = target->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_easy_method_lookup(classdesc, str("run"), null_str(), true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = target}};
  bjvm_stack_value ret;
  bjvm_thread_run(thread, method, method_args, &ret);
  return ret;
}
