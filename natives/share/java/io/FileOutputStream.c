#include <natives-dsl.h>
#include <unistd.h>

DECLARE_NATIVE("java/io", FileOutputStream, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", FileOutputStream, writeBytes, "([BIIZ)V") {
  bjvm_obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  s32 unix_fd = LoadFieldInt(fd, "fd");

  bjvm_obj_header *bytes = args[0].handle->obj;
  s32 offset = args[1].i;
  s32 length = args[2].i;
  [[maybe_unused]] bool append = args[3].i; // todo: append to first advance the position to the end of file
  char *data = (char *) ArrayData(bytes);

  if (offset < 0 || length < 0 || (long) offset + length > *ArrayLength(bytes)) {
    ThrowLangException(ArrayIndexOutOfBoundsException);
    return value_null();
  }

  char *buf = data + offset;

  if (unix_fd == 1 && thread->vm->write_stdout) {
    thread->vm->write_stdout(buf, length, thread->vm->stdio_override_param);
  } else if (unix_fd == 2 && thread->vm->write_stderr) {
    thread->vm->write_stderr(buf, length, thread->vm->stdio_override_param);
  } else { // do an actual syscall
    while (length > 0) {
      s32 written = (s32) write(unix_fd, buf, length);

      if (written < 0) {
        bjvm_raise_vm_exception(thread, STR("java/io/IOException"), STR("Error writing file"));
        return value_null();
      }

      length -= written;
      buf += written;
    }
  }
  return value_null();
}

DECLARE_NATIVE("java/io", FileOutputStream, close0, "()V") {
  // this method does no allocations or yielding, so we can use the same pointer
  bjvm_obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  assert(fd);

  s32 unix_fd = LoadFieldInt(fd, "fd");
  if (unix_fd != -1) close(unix_fd);
  StoreFieldInt(fd, "fd", -1);

  return value_null();
}
