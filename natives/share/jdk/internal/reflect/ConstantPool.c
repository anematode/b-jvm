#include "natives-dsl.h"

cp_entry *lookup_entry(obj_header *obj, int index, cp_kind expected) {
  struct native_ConstantPool *mirror = (void *)obj;
  classdesc *desc = mirror->reflected_class;
  constant_pool *pool = desc->pool;
  if (index < 0 && index >= pool->entries_len) {
    return nullptr;
  }
  cp_entry *entry = &pool->entries[index];
  if (entry->kind != expected) {
    return nullptr;
  }
  return entry;
}

DECLARE_NATIVE("jdk/internal/reflect", ConstantPool, getUTF8At0, "(Ljava/lang/Object;I)Ljava/lang/String;") {
  cp_entry *entry = lookup_entry(obj->obj, args[1].i, CP_KIND_UTF8);
  if (!entry) {
    return (stack_value){.obj = nullptr};
  }
  return (stack_value){.obj = MakeJStringFromModifiedUTF8(thread, entry->utf8, true)};
}

DECLARE_NATIVE("jdk/internal/reflect", ConstantPool, getIntAt0, "(Ljava/lang/Object;I)I") {
  cp_entry *entry = lookup_entry(obj->obj, args[1].i, CP_KIND_INTEGER);
  if (!entry) {
    return (stack_value){.obj = nullptr};
  }
  return (stack_value){.i = (int)entry->integral.value};
}

DECLARE_NATIVE("jdk/internal/reflect", ConstantPool, getDoubleAt0, "(Ljava/lang/Object;I)D") {
  cp_entry *entry = lookup_entry(obj->obj, args[1].i, CP_KIND_DOUBLE);
  if (!entry) {
    return (stack_value){.obj = nullptr};
  }
  return (stack_value){.d = entry->floating.value};
}