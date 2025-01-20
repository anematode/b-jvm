#include "unixlike-fs.h"

#include <natives.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unixlike-fs.h"
#include "io-natives.h"

DECLARE_NATIVE("java/io", UnixFileSystem, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", UnixFileSystem, initIDs, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/io", UnixFileSystem, getBooleanAttributes0,
               "(Ljava/io/File;)I") {
  unixlike_fs const *fs = unix_get_active_fs();
  if (!fs) {
    ThrowLangException(UnsupportedOperationException);
    goto exception;
  }

  // todo: replace with getPath when async natives work
  bjvm_obj_header *path = LoadFieldObject(args[0].handle->obj, "path", "Ljava/lang/String;");
  heap_string str = AsHeapString(path, exception);

  boolean_attributes attrs;
  fs_result result = fs->fs.get_attributes(hslc(str), &attrs);

  free_heap_str(str);

  if (result != FS_OK) {
    ThrowIOExceptionM("%.*s", fmt_slice(fs_result_to_string(result)));
    goto exception;
  }

  return (bjvm_stack_value) {.i = result};

  exception:
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

DECLARE_NATIVE("java/io", UnixFileSystem, canonicalize0,
               "(Ljava/lang/String;)Ljava/lang/String;") {
  // Concatenate the current working directory with the given path
  heap_string path = AsHeapString(args[0].handle->obj, on_oom);

  heap_string canonical = canonicalize_path(hslc(path)); // todo: deal with oom here

  bjvm_obj_header *result = make_string(thread, hslc(canonical));
  free_heap_str(canonical);
  free_heap_str(path);

  return (bjvm_stack_value){.obj = result};

  on_oom:
  return value_null();
}

DECLARE_NATIVE("java/io", UnixFileSystem, getLastModifiedTime,
               "(Ljava/io/File;)J") {
  bjvm_obj_header *file_obj = args[0].handle->obj;
  bjvm_obj_header *path = LoadFieldObject(file_obj, "path", "Ljava/lang/String;");

  heap_string path_str = AsHeapString(path, on_oom);

  struct stat st;
  bjvm_stack_value result = stat(path_str.chars, &st) != 0 ? value_null() : (bjvm_stack_value){.l = st.st_mtime};
  free_heap_str(path_str);
  return result;

  on_oom:
  return value_null();
}