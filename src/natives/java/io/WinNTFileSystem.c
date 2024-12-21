#include <natives.h>

DECLARE_NATIVE("java/io", WinNTFileSystem, initIDs, "()V") {
  return value_null();
}
