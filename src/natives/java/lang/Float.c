#include <natives.h>

DECLARE_NATIVE("java/lang", Float, floatToRawIntBits, "(F)I") {
  return (bjvm_stack_value){.i = args[0].i};
}
