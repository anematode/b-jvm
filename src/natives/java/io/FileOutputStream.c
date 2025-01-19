#include <natives.h>

DECLARE_NATIVE("java/io", FileOutputStream, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", FileOutputStream, writeBytes, "([BIIZ)V") {
  bjvm_obj_header *bytes = args[0].handle->obj;
  int offset = args[1].i;
  int length = args[2].i;
  char *data = (char *)ArrayData(bytes);
  for (int i = 0; i < length; ++i) {
    if (thread->vm->write_stdout)
      thread->vm->write_stdout(data[offset + i], thread->vm->write_byte_param);
    else
      fprintf(stderr, "%d ", data[offset + i]);
  }
  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("java/io", FileOutputStream, close0, "()V") { // TODO
  return value_null();
}
