#include <natives-dsl.h>

DECLARE_NATIVE("java/lang/ref", Finalizer, isFinalizationEnabled, "()Z") {
  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("java/lang/ref", Reference, refersTo0, "(Ljava/lang/Object;)Z") {
  assert(argc == 1);
  struct bjvm_native_Reference *ref = (void*) obj;
  return (bjvm_stack_value){.i = ref->referent == args[0].handle->obj};
}

DECLARE_NATIVE("java/lang/ref", Reference, clear0, "()V") {
  return value_null();
}