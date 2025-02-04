#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", Double, doubleToRawLongBits, "(D)J") {
  return (bjvm_stack_value){.l = args[0].l};
}

DECLARE_NATIVE("java/lang", Double, longBitsToDouble, "(J)D") {
  return (bjvm_stack_value){.l = args[0].l};
}
