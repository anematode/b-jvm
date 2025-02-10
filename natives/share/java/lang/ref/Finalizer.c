#include "roundrobin_scheduler.h"


#include <natives-dsl.h>
#include <sys/time.h>

DECLARE_NATIVE("java/lang/ref", Finalizer, isFinalizationEnabled, "()Z") {
  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("java/lang/ref", Reference, refersTo0, "(Ljava/lang/Object;)Z") {
  DCHECK(argc == 1);
  struct bjvm_native_Reference *ref = (void*) obj;
  return (bjvm_stack_value){.i = ref->referent == args[0].handle->obj};
}

DECLARE_NATIVE("java/lang/ref", Reference, clear0, "()V") {
  return value_null();
}

DECLARE_ASYNC_NATIVE("java/lang/ref", Reference, waitForReferencePendingList, "()V", locals(rr_wakeup_info wakeup_info;), invoked_methods()) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  u64 time = tv.tv_sec * 1000000 + tv.tv_usec;
  u64 end = time + 1e10;

  self->wakeup_info.kind = RR_WAKEUP_SLEEP;
  self->wakeup_info.wakeup_us = end;
  ASYNC_YIELD((void*) &self->wakeup_info);
  ASYNC_END_VOID();
}

DECLARE_NATIVE("java/lang/ref", Reference, getAndClearReferencePendingList, "()Ljava/lang/ref/Reference;") {
  return value_null();
}

