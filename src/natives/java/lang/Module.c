#include "objects.h"


#include <natives.h>

/**
*Module module,
boolean isOpen,
String version,
String location,
Object[] pns
*/
DECLARE_NATIVE("java/lang", Module, defineModule0,
  "(Ljava/lang/Module;ZLjava/lang/String;Ljava/lang/String;[Ljava/lang/Object;)V") {
  assert(argc == 5);

  bjvm_obj_header *name = LoadFieldObject(args[0].handle->obj, "name", "Ljava/lang/String;");
  heap_string s;
  if (read_string_to_utf8(thread, &s, name)) {
    return value_null();  // oom
  }

  bjvm_define_module(thread->vm, hslc(s), args[0].handle->obj);
  free_heap_str(s);

  return value_null();
}

DECLARE_NATIVE("java/lang", Module, addReads0,
  "(Ljava/lang/Module;Ljava/lang/Module;)V") {
  assert(argc == 2);
  return value_null();
}

DECLARE_NATIVE("java/lang", Module, addExportsToAll0,
  "(Ljava/lang/Module;Ljava/lang/String;)V") {
  assert(argc == 2);
  return value_null();
}

DECLARE_NATIVE("java/lang", Module, addExports0,
  "(Ljava/lang/Module;Ljava/lang/String;Ljava/lang/Module;)V") {
  assert(argc == 3);
  return value_null();
}