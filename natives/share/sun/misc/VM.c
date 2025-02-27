#include <natives-dsl.h>

// so we dont conflict with the jdk/internal version
DECLARE_NATIVE_OVERLOADED("sun/misc", VM, initialize, "()V", 1) { return value_null(); }

DECLARE_NATIVE("jdk/internal/misc", VM, getNanoTimeAdjustment, "(J)J") {
  return (stack_value){.l = 0};
}