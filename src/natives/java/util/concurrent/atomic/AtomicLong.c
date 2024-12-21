#include <natives.h>

DECLARE_NATIVE("java/util/concurrent/atomic", AtomicLong, VMSupportsCS8, "()Z") {
  return (bjvm_stack_value){.i = 1};
}