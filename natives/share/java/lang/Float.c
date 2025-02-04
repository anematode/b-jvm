#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", Float, floatToRawIntBits, "(F)I") {
  return (bjvm_stack_value){.i = args[0].i};
}

DECLARE_NATIVE("java/lang", Float, intBitsToFloat, "(I)F") {
  return (bjvm_stack_value){.i = args[0].i};
}