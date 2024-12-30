#include <natives.h>

DECLARE_NATIVE("sun/reflect", Reflection, getCallerClass,
               "()Ljava/lang/Class;") {
  // Look at frame before latest frame
  if (thread->frames_count < 3) {
    return value_null();
  }
  bjvm_stack_frame *frame = thread->frames[thread->frames_count - 3];
  return (bjvm_stack_value){
      .obj = (void *)bjvm_get_class_mirror(thread, bjvm_get_frame_method(frame)->my_class)};
}

DECLARE_NATIVE("sun/reflect", Reflection, getClassAccessFlags,
               "(Ljava/lang/Class;)I") {
  bjvm_obj_header *obj_ = args[0].handle->obj;
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj_);
  return (bjvm_stack_value){.i = classdesc->access_flags};
}
