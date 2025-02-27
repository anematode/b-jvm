#include "bjvm.h"
#include <natives-dsl.h>

DECLARE_NATIVE("java/lang/reflect", Array, newArray, "(Ljava/lang/Class;I)Ljava/lang/Object;") {
  DCHECK(argc == 2);
  if (!args[0].handle->obj)
    return value_null();
  classdesc *class = unmirror_class(args[0].handle->obj);
  initialize_class_t pox = {.args = {thread, class}};
  future_t f = initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);
  s32 count = args[1].i;
  obj_header *result;
  if (class->kind == CD_KIND_PRIMITIVE) {
    result = CreatePrimitiveArray1D(thread, class->primitive_component, count);
  } else {
    result = CreateObjectArray1D(thread, class, count);
  }
  return (stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang/reflect", Array, getLength, "(Ljava/lang/Object;)I") {
  DCHECK(argc == 1);
  return (stack_value){.i = ArrayLength(args[0].handle->obj)};
}

DECLARE_NATIVE("java/lang/reflect", Array, get, "(Ljava/lang/Object;I)Ljava/lang/Object;") {
  DCHECK(argc == 2);

  object array = args[0].handle->obj;
  switch (array->descriptor->kind) {

  case CD_KIND_ORDINARY:
    raise_vm_exception(thread, STR("java/lang/IllegalArgumentException"), STR("Argument is not an array"));
    return value_null();
  case CD_KIND_PRIMITIVE:
  default:
    UNREACHABLE();
  case CD_KIND_ORDINARY_ARRAY:
    if (args[1].i < 0 || args[1].i >= ArrayLength(array)) {
      raise_vm_exception(thread, STR("java/lang/ArrayIndexOutOfBoundsException"), STR(""));
      return value_null();
    }
    return (stack_value){.obj = ReferenceArrayLoad(array, args[1].i)};
  case CD_KIND_PRIMITIVE_ARRAY:
    CHECK("Primitives not yet implemented");
  }
  return (stack_value){.i = ArrayLength(args[0].handle->obj)};
}