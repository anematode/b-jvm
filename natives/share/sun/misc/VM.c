#include <natives-dsl.h>

// so we dont conflict with the jdk/internal version
DECLARE_NATIVE_OVERLOADED("sun/misc", VM, initialize, "()V", 1) { return value_null(); }
