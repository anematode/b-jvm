
#include <natives.h>
#include <unistd.h>
#include <sys/stat.h>

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, init, "()I") {
  return value_null();
}

DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, getcwd, "()[B") {
  INIT_STACK_STRING(cwd, 1024);
  char* p = getcwd(cwd.chars, 1024);
  if (p == nullptr) {
    return value_null();
  }
  cwd.len = strlen(cwd.chars);
  bjvm_obj_header *array = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, cwd.len);
  if (!array) {
    return value_null();
  }
  memcpy(ArrayData(array), cwd.chars, cwd.len);
  return (bjvm_stack_value){.obj = array};
}


DECLARE_NATIVE("sun/nio/fs", UnixNativeDispatcher, stat0, "(JLsun/nio/fs/UnixFileAttributes;)I") {
  struct stat st;

  return value_null();
}
