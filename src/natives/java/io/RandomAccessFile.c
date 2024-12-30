#include <natives.h>

DECLARE_NATIVE("java/io", RandomAccessFile, initIDs, "()V") {
  return value_null();
}

static bjvm_obj_header **get_fd(bjvm_obj_header *obj) {
  bjvm_cp_field *field = bjvm_easy_field_lookup(
      obj->descriptor, STR("fd"), STR("Ljava/io/FileDescriptor;"));
  return (void *)obj + field->byte_offset;
}

static int64_t *get_native_handle(bjvm_obj_header *obj) {
  bjvm_cp_field *native_fd_field =
      bjvm_easy_field_lookup(obj->descriptor, STR("handle"), STR("J"));
  return (void *)obj + native_fd_field->byte_offset;
}

DECLARE_NATIVE("java/io", RandomAccessFile, open0, "(Ljava/lang/String;I)V") {
  if (!args[0].handle->obj)
    return value_null();
  heap_string filename = read_string_to_utf8(args[0].handle->obj);
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = fopen(filename.chars, "r");
  if (!file) {
    bjvm_raise_exception(thread, STR("java/io/FileNotFoundException"),
                         hslc(filename));
  } else {
    *get_native_handle(fd) = (int64_t)file;
  }
  free_heap_str(filename);
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, read0, "()I") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    bjvm_raise_exception(thread, STR("java/io/IOException"),
                         STR("File not open"));
    return value_null();
  }
  int ch = fgetc(file);
  return (bjvm_stack_value){.i = ch};
}

DECLARE_NATIVE("java/io", RandomAccessFile, seek0, "(J)V") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    bjvm_raise_exception(thread, STR("java/io/IOException"),
                         STR("File not open"));
    return value_null();
  }
  long pos = args[0].l;
  fseek(file, pos, SEEK_SET);
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, getFilePointer, "()J") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    bjvm_raise_exception(thread, STR("java/io/IOException"),
                         STR("File not open"));
    return value_null();
  }
  long pos = ftell(file);
  return (bjvm_stack_value){.l = pos};
}

DECLARE_NATIVE("java/io", RandomAccessFile, close0, "()V") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (file) {
    fclose(file);
    *get_native_handle(fd) = 0;
  }
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, length, "()J") {
  bjvm_obj_header *fd = *get_fd(obj->obj);
  assert(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    bjvm_raise_exception(thread, STR("java/io/IOException"),
                         STR("File not open"));
    return value_null();
  }
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  return (bjvm_stack_value){.l = length};
}
