#include <natives.h>

DECLARE_NATIVE("java/lang", Object, hashCode, "()I") {
  return (bjvm_stack_value){.i = (int)obj->mark_word};
}

DECLARE_NATIVE("java/lang", Object, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Object, clone, "()Ljava/lang/Object;") {
  switch (obj->descriptor->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY: {
    bjvm_obj_header *new_array = CreateObjectArray1D(
        thread, obj->descriptor->one_fewer_dim, *ArrayLength(obj));
    if (new_array) {
      memcpy(ArrayData(new_array), ArrayData(obj),
             *ArrayLength(obj) * sizeof(void *));
    }
    return (bjvm_stack_value){.obj = new_array};
  }
  case BJVM_CD_KIND_ORDINARY: {
    bjvm_obj_header *new_obj = new_object(thread, obj->descriptor);
    memcpy(new_obj + 1, obj + 1,
           obj->descriptor->data_bytes - sizeof(bjvm_obj_header));
    return (bjvm_stack_value){.obj = new_obj};
  }
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang", Object, getClass, "()Ljava/lang/Class;") {
  return (bjvm_stack_value){
      .obj = (void *)bjvm_get_class_mirror(thread, obj->descriptor)};
}
