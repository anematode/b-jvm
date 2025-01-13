#include <natives.h>

DECLARE_NATIVE("java/lang", ClassLoader, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", ClassLoader, findLoadedClass0,
               "(Ljava/lang/String;)Ljava/lang/Class;") {
  assert(argc == 1);
  heap_string read = read_string_to_utf8(args[0].handle->obj);
  // Replace . with /
  for (int i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  bjvm_classdesc *cd = bjvm_hash_table_lookup(&thread->vm->classes, read.chars, read.len);
  free_heap_str(read);
  return (bjvm_stack_value){.obj = cd ? (void *)bjvm_get_class_mirror(thread, cd) : nullptr};
}

DECLARE_NATIVE("java/lang", ClassLoader, findBootstrapClass,
               "(Ljava/lang/String;)Ljava/lang/Class;") {
  assert(argc == 1);
  heap_string read = read_string_to_utf8(args[0].handle->obj);
  // Replace . with /
  for (int i = 0; i < read.len; ++i)
    if (read.chars[i] == '.')
      read.chars[i] = '/';
  bjvm_classdesc *cd = must_create_class(thread, hslc(read));
  free_heap_str(read);
  return (bjvm_stack_value){.obj = cd ? (void *)bjvm_get_class_mirror(thread, cd) : nullptr};
}

DECLARE_NATIVE("java/lang", ClassLoader, findBuiltinLib,
               "(Ljava/lang/String;)Ljava/lang/String;") {
  return value_null();
}