#include <natives-dsl.h>

DECLARE_NATIVE("jdk/internal/misc", ScopedMemoryAccess, registerNatives, "()V") {
  return value_null();
}
