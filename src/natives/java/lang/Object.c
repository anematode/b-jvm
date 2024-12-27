#include <natives.h>

DECLARE_NATIVE("java/lang", Object, hashCode, "()I") {
  return (bjvm_stack_value){.i = (int)obj->obj->mark_word};
}

DECLARE_NATIVE("java/lang", Object, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Object, clone, "()Ljava/lang/Object;") {
  switch (obj->obj->descriptor->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY: {
    bjvm_obj_header *new_array = CreateObjectArray1D(
        thread, obj->obj->descriptor->one_fewer_dim, *ArrayLength(obj->obj), true);
    if (new_array) {
      memcpy(ArrayData(new_array), ArrayData(obj->obj),
             *ArrayLength(obj->obj) * sizeof(void *));
    }
    return (bjvm_stack_value){.obj = new_array};
  }
  case BJVM_CD_KIND_ORDINARY: {
    bjvm_obj_header *new_obj = new_object(thread, obj->obj->descriptor);
    memcpy(new_obj + 1, obj->obj + 1,
           obj->obj->descriptor->data_bytes - sizeof(bjvm_obj_header));
    return (bjvm_stack_value){.obj = new_obj};
  }
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang", Object, getClass, "()Ljava/lang/Class;") {
  return (bjvm_stack_value){
      .obj = (void *)bjvm_get_class_mirror(thread, obj->obj->descriptor)};
}
