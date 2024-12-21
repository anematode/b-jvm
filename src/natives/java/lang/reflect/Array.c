#include "natives.h"
#include "bjvm.h"

DECLARE_NATIVE("java/lang/reflect", Array, newArray, "(Ljava/lang/Class;I)Ljava/lang/Object;") {
  assert(argc == 2);
  struct bjvm_native_Class *class = (void*)args[0].obj;
  int32_t count = args[1].i;

  bjvm_obj_header *array = create_object_array(thread, class->reflected_class, count);
  return (bjvm_stack_value) { .obj = array };
}