#include "bjvm.h"
#include "natives.h"

DECLARE_NATIVE("java/lang/reflect", Executable, getParameters0, "()[Ljava/lang/reflect/Parameter;") {
  assert(argc == 0);
  // Could be a Method or a Constructor, check which one
  bjvm_obj_header *executable = obj->obj;
  bjvm_cp_method *method;
  bjvm_utf8 name = hslc(executable->descriptor->name);
  if (utf8_equals(name, "java/lang/reflect/Method")) {
    method = *bjvm_unmirror_method((void*)executable);
  } else if (utf8_equals(name, "java/lang/reflect/Constructor")) {
    method = *bjvm_unmirror_ctor((void*)executable);
  } else {
    return value_null();  // wtf
  }
  bjvm_obj_header *parameters = bjvm_reflect_get_method_parameters(thread, method);
  return (bjvm_stack_value) { .obj = parameters };
}