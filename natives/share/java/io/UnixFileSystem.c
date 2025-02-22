#include "unixlike-fs.h"

#include <natives-dsl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unixlike-fs.h"

DECLARE_NATIVE("java/io", UnixFileSystem, initIDs, "()V") { return value_null(); }

DECLARE_NATIVE("java/io", UnixFileSystem, checkAccess0, "(Ljava/io/File;I)Z") { return (stack_value){.i = 1}; }

DECLARE_ASYNC_NATIVE("java/io", UnixFileSystem, getBooleanAttributes0, "(Ljava/io/File;)I", locals(),
                     invoked_methods()) {
  obj_header *file_obj = args[0].handle->obj;
  obj_header *path = LoadFieldObject(file_obj, "java/lang/String", "path");

  heap_string path_str = AsHeapString(path, on_oom);

  struct stat st;
  int result = stat(path_str.chars, &st) != 0 ? 0 : BA_EXISTS | (S_ISDIR(st.st_mode) ? BA_DIRECTORY : BA_REGULAR);
  free_heap_str(path_str);

  ASYNC_RETURN((stack_value){.i = result});

on_oom:
  ASYNC_END(value_null());
}

static heap_string canonicalize_path(slice path) {
  slice *components = nullptr;

  u32 i = 0;
  for (u32 j = 0; j <= path.len; ++j) {
    if (path.chars[j] == '/' || j == path.len) {
      slice slc = (slice){path.chars + i, j - i};
      if (utf8_equals(slc, "..")) {
        if (arrlen(components)) {
          arrpop(components);
        }
      } else if (!utf8_equals(slc, ".") && i < j) {
        arrput(components, slc);
      }
      i = j + 1;
    }
  }

  i = 0;
  heap_string result = make_heap_str(path.len);
  for (u32 component_i = 0; component_i < arrlen(components); ++component_i) {
    result.chars[i++] = '/';
    for (u32 j = 0; j < components[component_i].len; ++j) {
      result.chars[i++] = components[component_i].chars[j];
    }
  }
  result.len = i;
  arrfree(components);
  return result;
}

DECLARE_NATIVE("java/io", UnixFileSystem, canonicalize0, "(Ljava/lang/String;)Ljava/lang/String;") {
  if (!args[0].handle->obj) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  // Concatenate the current working directory with the given path
  object raw = RawStringData(thread, args[0].handle->obj);
  slice data = (slice){.chars = ArrayData(raw), .len = ArrayLength(raw)};

  heap_string canonical = canonicalize_path(data); // todo: deal with oom here

  // canonicalize_path only is looking for sequences of .. and /, whcih look the same regardless of the coding.
  // It passes other elements on as-is, meaning the canonicalized string has the same encoding as the input string
  string_coder_kind coder = ((struct native_String *)args[0].handle->obj)->coder;
  obj_header *result = MakeJStringFromData(thread, hslc(canonical), coder);

  free_heap_str(canonical);

  return (stack_value){.obj = result};
}

DECLARE_NATIVE("java/io", UnixFileSystem, getLastModifiedTime, "(Ljava/io/File;)J") {
  obj_header *file_obj = args[0].handle->obj;
  obj_header *path = LoadFieldObject(file_obj, "java/lang/String", "path");

  heap_string path_str = AsHeapString(path, on_oom);

  struct stat st;
  stack_value result = stat(path_str.chars, &st) != 0 ? value_null() : (stack_value){.l = st.st_mtime};
  free_heap_str(path_str);
  return result;

on_oom:
  return value_null();
}