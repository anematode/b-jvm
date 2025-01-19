
#include <natives.h>

DECLARE_NATIVE("jdk/internal/loader", NativeLibraries, findBuiltinLib,
               "(Ljava/lang/String;)Ljava/lang/String;") {
  return value_null();
}