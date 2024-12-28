#include <natives.h>

bjvm_cp_entry *lookup_entry(bjvm_obj_header *obj, int index,
                            bjvm_cp_kind expected) {
  struct bjvm_native_ConstantPool *mirror = (void *)obj;
  bjvm_classdesc *desc = mirror->reflected_class;
  bjvm_constant_pool *pool = desc->pool;
  if (index < 0 && index >= pool->entries_len) {
    return nullptr;
  }
  bjvm_cp_entry *entry = &pool->entries[index];
  if (entry->kind != expected) {
    return nullptr;
  }
  return entry;
}

DECLARE_NATIVE("sun/reflect", ConstantPool, getUTF8At0,
               "(Ljava/lang/Object;I)Ljava/lang/String;") {
  bjvm_cp_entry *entry = lookup_entry(obj->obj, args[1].i, BJVM_CP_KIND_UTF8);
  return (bjvm_stack_value){.obj =
                                bjvm_intern_string(thread, hslc(entry->utf8))};
}

DECLARE_NATIVE("sun/reflect", ConstantPool, getIntAt0,
               "(Ljava/lang/Object;I)I") {
  bjvm_cp_entry *entry =
      lookup_entry(obj->obj, args[1].i, BJVM_CP_KIND_INTEGER);
  return (bjvm_stack_value){.i = entry->integral.value};
}