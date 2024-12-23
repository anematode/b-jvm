#include <natives.h>

DECLARE_NATIVE("sun/reflect", ConstantPool, getUTF8At0,
               "(Ljava/lang/Object;I)Ljava/lang/String;") {
  struct bjvm_native_ConstantPool *mirror = (void *)obj->obj;
  bjvm_classdesc *desc = mirror->reflected_class;
  bjvm_constant_pool *pool = desc->pool;

  int index = args[1].i;
  assert(index >= 0 && index < pool->entries_len);

  bjvm_cp_entry *entry = &pool->entries[index];
  if (entry->kind != BJVM_CP_KIND_UTF8) {
    return value_null();
  }

  return (bjvm_stack_value){.obj =
                                bjvm_intern_string(thread, hslc(entry->utf8))};
}