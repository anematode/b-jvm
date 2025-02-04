#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", StringUTF16, isBigEndian, "()Z") {
  assert(argc == 0);
  return (bjvm_stack_value){.i = 0};
}
