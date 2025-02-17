#include <natives-dsl.h>

DECLARE_NATIVE("java/io", RandomAccessFile, initIDs, "()V") { return value_null(); }

static obj_header **get_fd(obj_header *obj) {
  cp_field *field = field_lookup(obj->descriptor, STR("fd"), STR("Ljava/io/FileDescriptor;"));
  return (void *)obj + field->byte_offset;
}

static s64 *get_native_handle(obj_header *obj) {
  cp_field *native_fd_field = field_lookup(obj->descriptor, STR("handle"), STR("J"));
  return (void *)obj + native_fd_field->byte_offset;
}

DECLARE_NATIVE("java/io", RandomAccessFile, open0, "(Ljava/lang/String;I)V") {
  if (!args[0].handle->obj)
    return value_null();
  heap_string filename = AsHeapString(args[0].handle->obj, on_oom);
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = fopen(filename.chars, "r");
  if (!file) {
    raise_vm_exception(thread, STR("java/io/FileNotFoundException"), hslc(filename));
  } else {
    *get_native_handle(fd) = (s64)file;
  }
  free_heap_str(filename);

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, read0, "()I") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("File not open"));
    return value_null();
  }
  int ch = fgetc(file);
  return (stack_value){.i = ch};
}

DECLARE_NATIVE("java/io", RandomAccessFile, seek0, "(J)V") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("File not open"));
    return value_null();
  }
  long pos = args[0].l;
  fseek(file, pos, SEEK_SET);
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, getFilePointer, "()J") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("File not open"));
    return value_null();
  }
  long pos = ftell(file);
  return (stack_value){.l = pos};
}

DECLARE_NATIVE("java/io", RandomAccessFile, close0, "()V") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (file) {
    fclose(file);
    *get_native_handle(fd) = 0;
  }
  return value_null();
}

DECLARE_NATIVE("java/io", RandomAccessFile, length0, "()J") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("File not open"));
    return value_null();
  }
  fseek(file, 0, SEEK_END);
  long length = ftell(file);
  return (stack_value){.l = length};
}


DECLARE_NATIVE("java/io", RandomAccessFile, readBytes0, "([BII)I") {
  obj_header *fd = *get_fd(obj->obj);
  DCHECK(fd);
  FILE *file = (FILE *)*get_native_handle(fd);
  if (!file) {
    raise_vm_exception(thread, STR("java/io/IOException"), STR("File not open"));
    return value_null();
  }
  obj_header *array = args[0].handle->obj;
  int offset = args[1].i;
  int length = args[2].i;
  if (offset < 0 || length < 0 || (s64)offset + length > *ArrayLength(array)) {
    raise_vm_exception(thread, STR("java/lang/IndexOutOfBoundsException"), STR("Invalid array bounds"));
    return value_null();
  }
  u8 *data = ArrayData(array);
  fseek(file, offset, SEEK_SET);
  int read = fread(data + offset, 1, length, file);
  printf("Offset, length: %d %d %d\n", offset, length, read);
  return (stack_value){.i = read};
}
