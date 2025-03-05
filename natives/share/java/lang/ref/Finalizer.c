#include "roundrobin_scheduler.h"

#include <natives-dsl.h>
#include <sys/time.h>

DECLARE_NATIVE("java/lang/ref", Finalizer, isFinalizationEnabled, "()Z") { return (stack_value){.i = 0}; }

DECLARE_NATIVE("java/lang/ref", Reference, refersTo0, "(Ljava/lang/Object;)Z") {
  DCHECK(argc == 1);
  struct native_Reference *ref = (void *)obj;
  return (stack_value){.i = ref->referent == args[0].handle->obj};
}

DECLARE_NATIVE("java/lang/ref", Reference, clear0, "()V") { return value_null(); }

DECLARE_ASYNC_NATIVE("java/lang/ref", Reference, waitForReferencePendingList, "()V",
                     locals(rr_wakeup_info wakeup_info;), invoked_methods()) {
  DEBUG_PEDANTIC_YIELD(self->wakeup_info);

  while (!thread->vm->reference_pending_list) {
    self->wakeup_info.kind = RR_WAKEUP_REFERENCE_PENDING;
    self->wakeup_info.wakeup_us = INT64_MAX;
    ASYNC_YIELD((void *)&self->wakeup_info);
    DEBUG_PEDANTIC_YIELD(self->wakeup_info);
  }

  ASYNC_END_VOID();
}

DECLARE_NATIVE("java/lang/ref", Reference, getAndClearReferencePendingList, "()Ljava/lang/ref/Reference;") {
  object list = (object)thread->vm->reference_pending_list;
  thread->vm->reference_pending_list = nullptr;
  return (stack_value){.obj = list};
}
