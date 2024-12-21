#include <natives.h>

DECLARE_NATIVE("java/lang", Runtime, availableProcessors, "()I") {
  return (bjvm_stack_value){.i = 1};
}
