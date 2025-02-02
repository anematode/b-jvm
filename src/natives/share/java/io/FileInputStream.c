#include <errno.h>
#include <natives-dsl.h>

DECLARE_NATIVE("java/io", FileInputStream, initIDs, "()V") {
  return value_null();
}

static bjvm_obj_header **get_fd(bjvm_obj_header *obj) {
  bjvm_cp_field *field = bjvm_easy_field_lookup(
      obj->descriptor, STR("fd"), STR("Ljava/io/FileDescriptor;"));
  return (void *)obj + field->byte_offset;
}

static s64 *get_native_handle(bjvm_obj_header *obj) {
  bjvm_cp_field *native_fd_field =
      bjvm_easy_field_lookup(obj->descriptor, STR("handle"), STR("J"));
  return (void *)obj + native_fd_field->byte_offset;
}

DECLARE_NATIVE("java/io", FileInputStream, open0, "(Ljava/lang/String;)V") {
  if (!args[0].handle->obj)
    return value_null();

  heap_string filename = AsHeapString(args[0].handle->obj, on_oom);

  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = fopen(filename.chars, "r");
  if (!file) {
    // TODO use errno to give a better error message
    bjvm_raise_vm_exception(thread, STR("java/io/FileNotFoundException"),
                         hslc(filename));
  } else {
    *get_native_handle(fd) = (s64)file;
  }
  free_heap_str(filename);
  return value_null();

  on_oom:
  return value_null();
}

DECLARE_NATIVE("java/io", FileInputStream, readBytes, "([BII)I") {
  assert(argc == 3);
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    // Raise java.lang.IOException TODO
    UNREACHABLE();
  }
  bjvm_obj_header *array = args[0].handle->obj;
  if (!array) {
    ThrowLangException(NullPointerException);
    return value_null();
  }
  int offset = args[1].i;
  int length = args[2].i;
  if (offset < 0 || length < 0 || offset + length > *ArrayLength(array)) {
    ThrowLangException(ArrayIndexOutOfBoundsException);
    return value_null();
  }
  int read = (int)fread(ArrayData(array) + offset, 1, length, file);
  return (bjvm_stack_value){.i = read};
}

DECLARE_NATIVE("java/io", FileInputStream, close0, "()V") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  s64 handle = *get_native_handle(fd);
  if (handle != -1 && handle != 0) {
    fclose((FILE *)handle);
  }
  *get_native_handle(fd) = -1; // make invalid again
  return value_null();
}

DECLARE_NATIVE("java/io", FileInputStream, available0, "()I") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    return (bjvm_stack_value){.i = 0};
  }
  long pos = ftell(file);
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, pos, SEEK_SET);
  return (bjvm_stack_value){.i = size - pos};
}