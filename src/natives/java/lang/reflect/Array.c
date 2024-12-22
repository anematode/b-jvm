#include "bjvm.h"
#include "natives.h"

DECLARE_NATIVE("java/lang/reflect", Array, newArray,
               "(Ljava/lang/Class;I)Ljava/lang/Object;") {
  assert(argc == 2);
  struct bjvm_native_Class *class = (void *)args[0].handle->obj;
  int32_t count = args[1].i;

  bjvm_obj_header *array =
      CreateObjectArray1D(thread, class->reflected_class, count);
  return (bjvm_stack_value){.obj = array};
}

DECLARE_NATIVE("java/lang/reflect", Array, getLength,
               "(Ljava/lang/Object;)I") {
  assert(argc == 1);
  return (bjvm_stack_value){.i = *ArrayLength(args[0].handle->obj)};
}