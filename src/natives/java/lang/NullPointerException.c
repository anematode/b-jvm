#include <natives.h>

DECLARE_NATIVE("java/lang", NullPointerException, getExtendedNPEMessage, "()Ljava/lang/String;") {
  // https://github.com/anematode/b-jvm/issues/25
  return value_null();
}
