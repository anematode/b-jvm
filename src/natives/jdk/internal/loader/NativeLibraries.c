
#include <natives.h>

DECLARE_NATIVE("jdk/internal/loader", NativeLibraries, findBuiltinLib,
               "(Ljava/lang/String;)Ljava/lang/String;") {

  heap_string str = AsHeapString(args[0].handle->obj, on_oom);
  bool matches_nio = utf8_ends_with(hslc(str), STR(".bjvm_lib"));
  free_heap_str(str);

  if (matches_nio) {
    return (bjvm_stack_value){.obj = args[0].handle->obj};
  }

  on_oom:
  return value_null();
}

DECLARE_NATIVE("jdk/internal/loader", NativeLibraries, load, "(Ljdk/internal/loader/NativeLibraries$NativeLibraryImpl;Ljava/lang/String;ZZ)Z") {
  return (bjvm_stack_value){.i = 1};
}