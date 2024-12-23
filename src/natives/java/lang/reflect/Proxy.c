#include <natives.h>

DECLARE_NATIVE(
    "java/lang/reflect", Proxy, defineClass0,
    "(Ljava/lang/ClassLoader;Ljava/lang/String;[BII)Ljava/lang/Class;") {
  assert(argc == 5);

  bjvm_obj_header *name = args[1].handle->obj;
  bjvm_obj_header *data = args[2].handle->obj;
  int offset = args[3].i;
  int length = args[4].i;
  bjvm_obj_header *loader = args[0].handle->obj;

  heap_string name_str = read_string_to_utf8(name);
  uint8_t *bytes = ArrayData(data) + offset;

  // Replace name_str with slashes
  for (int i = 0; i < name_str.len; ++i) {
    if (name_str.chars[i] == '.') {
      name_str.chars[i] = '/';
    }
  }

  INIT_STACK_STRING(cf_name, 1000);
  cf_name = bprintf(cf_name, "%.*s.class", fmt_slice(name_str));

  bjvm_vm_preregister_classfile(thread->vm, cf_name, bytes, length);
  bjvm_classdesc *result = bootstrap_class_create(thread, hslc(name_str));

  free_heap_str(name_str);

  bjvm_initialize_class(thread, result);

  return (bjvm_stack_value){.obj =
                                (void *)bjvm_get_class_mirror(thread, result)};
}