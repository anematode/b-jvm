#include <natives.h>

DECLARE_NATIVE("java/io", WinNTFileSystem, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", WinNTFileSystem, getBooleanAttributes, "(Ljava/io/File;)I") {
	return value_null();
}
