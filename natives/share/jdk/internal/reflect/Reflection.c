#include "natives-dsl.h"

DECLARE_NATIVE("jdk/internal/reflect", Reflection, getCallerClass, "()Ljava/lang/Class;") {
  // Look a couple frames before the current frame
  int i = 2;
  stack_frame *frame = thread->stack.top;
  while (frame && i > 0) {
    frame = frame->prev;
    --i;
  }
  if (frame == nullptr)
    return value_null();
  return (stack_value){.obj = (void *)get_class_mirror(thread, get_frame_method(frame)->my_class)};
}

DECLARE_NATIVE("jdk/internal/reflect", Reflection, getClassAccessFlags, "(Ljava/lang/Class;)I") {
  obj_header *obj_ = args[0].handle->obj;
  classdesc *classdesc = unmirror_class(obj_);
  return (stack_value){.i = classdesc->access_flags};
}

DECLARE_NATIVE("jdk/internal/reflect", Reflection, areNestMates, "(Ljava/lang/Class;Ljava/lang/Class;)Z") {
  // TODO
  return (stack_value){.i = 1};
}