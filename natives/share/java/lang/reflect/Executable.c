#include "bjvm.h"
#include <natives-dsl.h>
#include <reflection.h>

DECLARE_NATIVE("java/lang/reflect", Executable, getParameters0, "()[Ljava/lang/reflect/Parameter;") {
  DCHECK(argc == 0);
  // Could be a Method or a Constructor, check which one
  obj_header *executable = obj->obj;
  cp_method *method;
  slice name = executable->descriptor->name;
  if (utf8_equals(name, "java/lang/reflect/Method")) {
    method = *unmirror_method((void *)executable);
  } else if (utf8_equals(name, "java/lang/reflect/Constructor")) {
    method = *unmirror_ctor((void *)executable);
  } else {
    return value_null(); // wtf
  }
  obj_header *parameters = reflect_get_method_parameters(thread, method);
  return (stack_value){.obj = parameters};
}