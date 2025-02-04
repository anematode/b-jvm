#include <natives-dsl.h>

DECLARE_NATIVE(
    "java/lang/reflect", Proxy, defineClass0,
    "(Ljava/lang/ClassLoader;Ljava/lang/String;[BII)Ljava/lang/Class;") {
  assert(argc == 5);

  bjvm_obj_header *name = args[1].handle->obj;
  bjvm_obj_header *data = args[2].handle->obj;
  int offset = args[3].i;
  int length = args[4].i;
  bjvm_obj_header *loader = args[0].handle->obj;

  (void)loader;

  heap_string name_str = AsHeapString(name, on_oom);
  u8 *bytes = ArrayData(data) + offset;

  // Replace name_str with slashes
  for (u32 i = 0; i < name_str.len; ++i) {
    if (name_str.chars[i] == '.') {
      name_str.chars[i] = '/';
    }
  }

  bjvm_classdesc *result =
      bjvm_define_bootstrap_class(thread, hslc(name_str), bytes, length);

  free_heap_str(name_str);

  bjvm_initialize_class_t pox = {.args = {thread, result}};
  future_t f = bjvm_initialize_class(&pox); // TODO convert
  BJVM_CHECK(f.status == FUTURE_READY);

  return (bjvm_stack_value){.obj =
                                (void *)bjvm_get_class_mirror(thread, result)};

  on_oom:
  return value_null();
}