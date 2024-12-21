#include <natives.h>

DECLARE_NATIVE("java/io", FileDescriptor, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", FileDescriptor, set, "(I)J") {
  return (bjvm_stack_value){.l = args[0].i};
}

