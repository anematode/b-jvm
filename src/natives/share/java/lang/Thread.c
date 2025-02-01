#include <natives-dsl.h>

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
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Thread, getNextThreadIdOffset, "()J") {
  return (bjvm_stack_value){.l = (intptr_t) &thread->vm->next_thread_id};
}

DECLARE_NATIVE("java/lang", Thread, currentCarrierThread, "()Ljava/lang/Thread;") {
  return (bjvm_stack_value){.obj = (void*)thread->thread_obj};
}