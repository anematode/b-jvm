#include <natives.h>

DECLARE_NATIVE("sun/nio/ch", NativeThread, init, "()V") {
  return value_null();
}

DECLARE_NATIVE("sun/nio/ch", NativeThread, current0, "()J") {
  return (bjvm_stack_value) {.i = 0};
}
