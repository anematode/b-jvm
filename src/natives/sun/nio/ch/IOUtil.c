#include <natives.h>

DECLARE_NATIVE("sun/nio/ch", IOUtil, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("sun/nio/ch", IOUtil, iovMax, "()I") {
  return (bjvm_stack_value){.i = IOV_MAX};
}

DECLARE_NATIVE("sun/nio/ch", IOUtil, writevMax, "()J") {
  return (bjvm_stack_value){.l = IOV_MAX};
}