#include "bjvm.h"
#include <natives-dsl.h>

DECLARE_NATIVE("java/lang/reflect", Array, newArray,
               "(Ljava/lang/Class;I)Ljava/lang/Object;") {
  assert(argc == 2);
  if (!args[0].handle->obj)
    return value_null();
  bjvm_classdesc *class = bjvm_unmirror_class(args[0].handle->obj);
  bjvm_initialize_class_t pox = {.args = {thread, class}};
  future_t f = bjvm_initialize_class(&pox);
  assert(f.status == FUTURE_READY);
  int32_t count = args[1].i;
  bjvm_obj_header *result;
  if (class->kind == BJVM_CD_KIND_PRIMITIVE) {
    result = CreatePrimitiveArray1D(thread, class->primitive_component, count);
  } else {
    result = CreateObjectArray1D(thread, class, count);
  }
  return (bjvm_stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang/reflect", Array, getLength, "(Ljava/lang/Object;)I") {
  assert(argc == 1);
  return (bjvm_stack_value){.i = *ArrayLength(args[0].handle->obj)};
}