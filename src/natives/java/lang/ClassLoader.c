#include <natives.h>

DECLARE_NATIVE("java/lang", ClassLoader, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, findLoadedClass0,
               "(Ljava/lang/String;)Ljava/lang/Class;") {
  assert(argc == 1);
  heap_string read = read_string_to_utf8(args[0].obj);
  // Replace . with /
  for (size_t i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  bjvm_classdesc *cd = bootstrap_class_create(
      thread, hslc(read)); // TODO don't actually create the class lol
  free_heap_str(read);
  return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, cd)};
}

DECLARE_NATIVE("java/lang", ClassLoader, findBuiltinLib,
               "(Ljava/lang/String;)Ljava/lang/String;") {
  return value_null();
}