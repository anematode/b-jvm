#include <errno.h>
#include <natives-dsl.h>
#ifdef EMSCRIPTEN
#include <fcntl.h>
#else
#include <sys/fcntl.h>
#endif
#include <sys/ioctl.h>
#include <unistd.h>

DECLARE_NATIVE("java/io", FileInputStream, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/io", FileInputStream, open0, "(Ljava/lang/String;)V") {
  if (!args[0].handle->obj)
    return value_null();

  heap_string filename = AsHeapString(args[0].handle->obj, on_oom);
  printf("Opening file %s\n", filename.chars);

  // this method does no allocations or yielding, so we can use the same pointer
  obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  DCHECK(fd);
  s32 unix_fd = open(filename.chars, O_RDONLY);
  StoreFieldInt(fd, "fd", unix_fd);

  if (unix_fd < 0) {
    // TODO use errno to give a better error message
    raise_vm_exception(thread, STR("java/io/FileNotFoundException"), hslc(filename));
  }

  free_heap_str(filename);
  return value_null();

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/io", FileInputStream, read0, "()I") {
  DCHECK(argc == 0);
  // this method does no allocations or yielding, so we can use the same pointer
  obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  DCHECK(fd);
  s32 unix_fd = LoadFieldInt(fd, "fd");
  if (unix_fd < 0) {
    raise_vm_exception(thread, STR("java/io/FileNotFoundException"), STR("File not found"));
    return value_null();
  }

  obj_header *array = args[0].handle->obj;
  if (!array) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  int length = 1;
  char res;

  int bytes_read;
  if (unix_fd == 0 && thread->vm->read_stdin) {
    bytes_read = thread->vm->read_stdin(&res, length, thread->vm->stdio_override_param);
  } else {
    bytes_read = (s32)read(unix_fd, &res, length);
  }

  if (bytes_read < 0) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("Error reading file"));
    return value_null();
  }

  return (stack_value){.i = bytes_read == 0 ? -1 : (int)res};
}

DECLARE_NATIVE("java/io", FileInputStream, readBytes, "([BII)I") {
  DCHECK(argc == 3);
  // this method does no allocations or yielding, so we can use the same pointer
  obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  DCHECK(fd);
  s32 unix_fd = LoadFieldInt(fd, "fd");
  if (unix_fd < 0) {
    raise_vm_exception(thread, STR("java/io/FileNotFoundException"), STR("File not found"));
    return value_null();
  }

  obj_header *array = args[0].handle->obj;
  if (!array) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  s32 offset = args[1].i;
  s32 length = args[2].i;

  if (offset < 0 || length < 0 || (s64)offset + length > ArrayLength(array)) {
    raise_vm_exception_no_msg(thread, STR("java/lang/ArrayIndexOutOfBoundsException"));
    return value_null();
  }

  char *buf = ArrayData(array) + offset;

  s32 bytes_read;
  if (unix_fd == 0 && thread->vm->read_stdin) {
    bytes_read = thread->vm->read_stdin(buf, length, thread->vm->stdio_override_param);
  } else {
    bytes_read = (s32)read(unix_fd, buf, length);
  }

  if (bytes_read < 0) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("Error reading file"));
    return value_null();
  }
  return (stack_value){.i = bytes_read == 0 ? -1 : bytes_read};
}

DECLARE_NATIVE("java/io", FileInputStream, close0, "()V") {
  // this method does no allocations or yielding, so we can use the same pointer
  obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  DCHECK(fd);

  s32 unix_fd = LoadFieldInt(fd, "fd");
  if (unix_fd != -1)
    close(unix_fd);
  StoreFieldInt(fd, "fd", -1);

  return value_null();
}

DECLARE_NATIVE("java/io", FileInputStream, available0, "()I") {
  // this method does no allocations or yielding, so we can use the same pointer
  obj_header *fd = LoadFieldObject(obj->obj, "java/io/FileDescriptor", "fd");
  DCHECK(fd);
  s32 unix_fd = LoadFieldInt(fd, "fd");
  if (unix_fd == -1) {
    return (stack_value){.i = 0};
  }

  int available;

  if (unix_fd == 0 && thread->vm->poll_available_stdin) {
    available = thread->vm->poll_available_stdin(thread->vm->stdio_override_param);
  } else {
    int err = ioctl(unix_fd, FIONREAD, &available);
    if (err < 0) {
      raise_vm_exception(thread, STR("java/io/IOException"), STR("Error getting available bytes"));
      return value_null();
    }
  }

  return (stack_value){.i = (s32)available};
}