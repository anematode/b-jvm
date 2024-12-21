#include <math.h>
#include <natives.h>

DECLARE_NATIVE("java/lang", StrictMath, log, "(D)D") {
  assert(argc == 1);
  return (bjvm_stack_value){.d = log(args[0].d)};
}