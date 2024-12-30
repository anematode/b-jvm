#include <natives.h>
#include <sys/stat.h>
#include <unistd.h>

DECLARE_NATIVE("java/io", WinNTFileSystem, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", WinNTFileSystem, getBooleanAttributes,
               "(Ljava/io/File;)I") {
  return value_null();
}

static heap_string canonicalize_path(bjvm_utf8 path) {
  bjvm_utf8 *components = nullptr;
  int count = 0, cap = 0;

  int i = 0;
  for (int j = 0; j <= path.len; ++j) {
    if (path.chars[j] == '/' || j == path.len) {
      bjvm_utf8 slice = (bjvm_utf8){path.chars + i, j - i};
      if (utf8_equals(slice, "..")) {
        count = count > 0 ? count - 1 : 0;
      } else if (!utf8_equals(slice, ".") && i < j) {
        *VECTOR_PUSH(components, count, cap) = slice;
      }
      i = j + 1;
    }
  }

  i = 0;
  heap_string result = make_heap_str(path.len);
  for (int component_i = 0; component_i < count; ++component_i) {
    result.chars[i++] = '/';
    for (int j = 0; j < components[component_i].len; ++j) {
      result.chars[i++] = components[component_i].chars[j];
    }
  }
  result.len = i;
  free(components);
  return result;
}

DECLARE_NATIVE("java/io", WinNTFileSystem, canonicalize0,
               "(Ljava/lang/String;)Ljava/lang/String;") {
  // Concatenate the current working directory with the given path
  bjvm_obj_header *path_obj = args[0].handle->obj;
  heap_string path = read_string_to_utf8(path_obj);

  heap_string canonical = canonicalize_path(hslc(path));

  bjvm_obj_header *result = make_string(thread, hslc(canonical));
  free_heap_str(canonical);
  free_heap_str(path);

  return (bjvm_stack_value){.obj = result};
}

static int get_file_path(bjvm_obj_header *obj, heap_string *result) {
  bjvm_cp_field *field = bjvm_easy_field_lookup(obj->descriptor, STR("path"),
                                                STR("Ljava/lang/String;"));
  if (!field)
    return -1;
  bjvm_obj_header *str = bjvm_get_field(obj, field).obj;
  if (!str)
    return -1;
  *result = read_string_to_utf8(str);
  return 0;
}

DECLARE_NATIVE("java/io", WinNTFileSystem, getLastModifiedTime,
               "(Ljava/io/File;)J") {
  bjvm_obj_header *file_obj = args[0].handle->obj;
  heap_string path;
  if (get_file_path(file_obj, &path) != 0)
    return value_null();
  struct stat st;
  if (stat(path.chars, &st) != 0)
    return value_null();
}