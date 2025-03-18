#include "natives-dsl.h"

bool ignore_frame_for_get_caller_class(stack_frame *frame) {
  classdesc *cd = frame->method->my_class;
  if (strncmp(cd->name.chars, "java/lang/invoke", 16) == 0)
    return true;
  if (strncmp(cd->name.chars, "jdk/internal/reflect", 20) == 0)
    return true;
  if (strncmp(cd->name.chars, "sun/util/invoke", 15) == 0)
    return true;
  return false;
}

DECLARE_NATIVE("jdk/internal/reflect", Reflection, getCallerClass, "()Ljava/lang/Class;") {
  // Look a couple frames before the current frame
  int i = 2;
  stack_frame *frame = thread->stack.top;
  while (frame && i > 0) {
    frame = frame->prev;
    --i;
  }
  while (frame && ignore_frame_for_get_caller_class(frame))
    frame = frame->prev;
  if (frame == nullptr)
    return value_null();
  return (stack_value){.obj = (void *)get_class_mirror(thread, frame->method->my_class)};
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