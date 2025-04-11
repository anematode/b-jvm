#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", Runtime, availableProcessors, "()I") { return (stack_value){.i = 1}; }

DECLARE_NATIVE("java/lang", Runtime, maxMemory, "()J") { return (stack_value){.l = LLONG_MAX}; }

DECLARE_NATIVE("java/lang", Runtime, gc, "()V") {
  scheduled_gc_pause(thread->vm);
  return value_null();
}

DECLARE_NATIVE("java/lang", Shutdown, beforeHalt, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", Shutdown, halt0, "(I)V") { return value_null(); }