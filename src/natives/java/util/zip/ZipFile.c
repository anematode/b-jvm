#include <natives.h>

DECLARE_NATIVE("java/util/zip", ZipFile, initIDs, "()V") {
  return value_null();
}

#if 0
DECLARE_NATIVE("java/util/zip", ZipFile, open, "(Ljava/lang/String;IJZ)L") {
  assert(argc == 4);

  heap_string path = read_string_to_utf8(args[0].handle->obj);

}
#endif