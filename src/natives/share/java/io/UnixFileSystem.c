#include "unixlike-fs.h"

#include <natives-dsl.h>
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

DECLARE_ASYNC_NATIVE("java/io", UnixFileSystem, getBooleanAttributes0,
               "(Ljava/io/File;)I", locals(), invoked_methods()) {
  unixlike_fs const *fs = unix_get_active_fs();
  if (!fs) {
    ThrowLangException(UnsupportedOperationException);
    goto exception;
  }

  // todo: replace with getPath when async natives work
  bjvm_obj_header *path = LoadFieldObject(args[0].handle->obj, "java/lang/String", "path");
  heap_string str = AsHeapString(path, exception);

  boolean_attributes attrs;
  fs_result result = fs->fs.get_attributes(hslc(str), &attrs);

  free_heap_str(str);

  if (result != FS_OK) {
    ThrowIOExceptionM("%.*s", fmt_slice(fs_result_to_string(result)));
    goto exception;
  }

  ASYNC_RETURN((bjvm_stack_value) {.i = result});

  exception:
  ASYNC_RETURN(value_null());

  ASYNC_END(value_null());
}

static heap_string canonicalize_path(slice path) {
  slice *components = nullptr;
  int count = 0, cap = 0;

  int i = 0;
  for (int j = 0; j <= path.len; ++j) {
    if (path.chars[j] == '/' || j == path.len) {
      slice slc = (slice){path.chars + i, j - i};
      if (utf8_equals(slc, "..")) {
        count = count > 0 ? count - 1 : 0;
      } else if (!utf8_equals(slc, ".") && i < j) {
        *VECTOR_PUSH(components, count, cap) = slc;
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
  if (!args[0].handle->obj) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  // Concatenate the current working directory with the given path
  object raw = RawStringData(thread, args[0].handle->obj);
  slice data = (slice) {.chars = ArrayData(raw), .len = *ArrayLength(raw)};

  heap_string canonical = canonicalize_path(data); // todo: deal with oom here

  // canonicalize_path only is looking for sequences of .. and /, whcih look the same regardless of the coding.
  // It passes other elements on as-is, meaning the canonicalized string has the same encoding as the input string
  string_coder_kind coder = ((struct bjvm_native_String *)args[0].handle->obj)->coder;
  bjvm_obj_header *result = MakeJStringFromData(thread, hslc(canonical), coder);

  free_heap_str(canonical);

  return (bjvm_stack_value){.obj = result};
}

DECLARE_NATIVE("java/io", UnixFileSystem, getLastModifiedTime,
               "(Ljava/io/File;)J") {
  bjvm_obj_header *file_obj = args[0].handle->obj;
  bjvm_obj_header *path = LoadFieldObject(file_obj, "java/lang/String", "path");

  heap_string path_str = AsHeapString(path, on_oom);

  struct stat st;
  bjvm_stack_value result = stat(path_str.chars, &st) != 0 ? value_null() : (bjvm_stack_value){.l = st.st_mtime};
  free_heap_str(path_str);
  return result;

  on_oom:
  return value_null();
}