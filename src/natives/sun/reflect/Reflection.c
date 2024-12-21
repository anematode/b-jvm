#include <natives.h>

DECLARE_NATIVE("sun/reflect", Reflection, getCallerClass, "()Ljava/lang/Class;") {
  // Look at frame before latest frame
  if (thread->frames_count < 2) {
    return value_null();
  }
  bjvm_stack_frame *frame = thread->frames[thread->frames_count - 2];
  return (bjvm_stack_value){
      .obj = (void *)bjvm_get_class_mirror(thread, frame->method->my_class)};
}

DECLARE_NATIVE("sun/reflect", Reflection, getClassAccessFlags, "(Ljava/lang/Class;)I") {
  bjvm_obj_header *obj_ = args[0].obj;
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj_);
  return (bjvm_stack_value){.i = classdesc->access_flags};
}

