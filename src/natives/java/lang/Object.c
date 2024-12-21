
#include <natives.h>

DECLARE_NATIVE("java/lang", Object, hashCode, "()I") {
  return (bjvm_stack_value){.i = (int)obj->mark_word};
}