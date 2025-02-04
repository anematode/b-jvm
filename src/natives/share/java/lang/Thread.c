#include <natives-dsl.h>
#include <roundrobin_scheduler.h>

DECLARE_NATIVE("java/lang", Thread, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Thread, currentThread, "()Ljava/lang/Thread;") {
  return (bjvm_stack_value){.obj = (void *)thread->thread_obj};
}

DECLARE_NATIVE("java/lang", Thread, setPriority0, "(I)V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Thread, isAlive, "()Z") {
  return (bjvm_stack_value){.i = 0}; // TODO
}

DECLARE_NATIVE("java/lang", Thread, holdsLock, "(Ljava/lang/Object;)Z") {
  return (bjvm_stack_value){.i = 0}; // TODO
}

DECLARE_NATIVE("java/lang", Thread, start0, "()V") {
  rr_scheduler *scheduler = thread->vm->scheduler;
  if (!scheduler)
    return value_null();  // TODO throw error instead
  bjvm_thread *vm_thread = bjvm_create_thread(thread->vm, bjvm_default_thread_options());
  ((struct bjvm_native_Thread*)obj->obj)->vm_thread = vm_thread;

  bjvm_cp_method *run = bjvm_method_lookup(obj->obj->descriptor, STR("run"), STR("()V"), false, false);
  bjvm_stack_value argz[1] = {{.obj = obj->obj}};
  rr_scheduler_run(scheduler, (call_interpreter_t){{vm_thread, run, argz}});
  return value_null();
}

DECLARE_NATIVE("java/lang", Thread, ensureMaterializedForStackWalk, "(Ljava/lang/Object;)V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Thread, getNextThreadIdOffset, "()J") {
  return (bjvm_stack_value){.l = (intptr_t) &thread->vm->next_thread_id};
}

DECLARE_NATIVE("java/lang", Thread, currentCarrierThread, "()Ljava/lang/Thread;") {
  return (bjvm_stack_value){.obj = (void*)thread->thread_obj};
}

DECLARE_NATIVE("java/lang", Thread, interrupt0, "()V") {
  StoreFieldBoolean(obj->obj, "interrupted", true);

  [[maybe_unused]] rr_scheduler *scheduler = thread->vm->scheduler;
  // todo: inform scheduler of interrupt, cause thread to potentially awake if yielded
  // todo: maybe also only do this if previously wasn't interrupted already?

  return value_null();
}

DECLARE_ASYNC_NATIVE("java/lang", Thread, sleepNanos0, "(J)V", locals(), invoked_methods()) {
  assert(argc == 1);

  if (thread->thread_obj->interrupted) {
    thread->thread_obj->interrupted = false; // throw and reset flag
    bjvm_raise_vm_exception(thread, STR("java/lang/InterruptedException"), STR("Thread interrupted before sleeping"));
    ASYNC_RETURN_VOID();
  }

  struct timeval tv;
  gettimeofday(&tv, NULL);
  u64 time = tv.tv_sec * 1000000 + tv.tv_usec;
  u64 end = time + args[0].l / 1000;

  rr_wakeup_info *wakeup_info = malloc(sizeof(rr_wakeup_info));
  wakeup_info->kind = RR_WAKEUP_SLEEP;
  wakeup_info->wakeup_us = end;
  ASYNC_YIELD((void*)wakeup_info);
  ASYNC_END_VOID();
}

DECLARE_NATIVE("java/lang", Thread, clearInterruptEvent, "()V") {
  return value_null(); // openjdk only does something here on Windows devices
}