#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", Object, hashCode, "()I") {
  return (bjvm_stack_value){.i = (int)obj->obj->mark_word};
}

DECLARE_NATIVE("java/lang", Object, registerNatives, "()V") {
  return value_null();
}

// Check whether the class is cloneable.
static bool cloneable(bjvm_vm *vm, bjvm_classdesc *cd) {
  for (int i = 0; i < arrlen(cd->itables.interfaces); i++) {
    if (cd->itables.interfaces[i] == vm->cached_classdescs->cloneable) {
      return true;
    }
  }
  return false;
}

DECLARE_NATIVE("java/lang", Object, clone, "()Ljava/lang/Object;") {
  switch (obj->obj->descriptor->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY: {
    bjvm_obj_header *new_array = CreateObjectArray1D(
        thread, obj->obj->descriptor->one_fewer_dim, *ArrayLength(obj->obj));
    if (new_array) {
      memcpy(ArrayData(new_array), ArrayData(obj->obj),
             *ArrayLength(obj->obj) * sizeof(void *));
    }
    return (bjvm_stack_value){.obj = new_array};
  }
  case BJVM_CD_KIND_ORDINARY: {
    // Check if the object is Cloneable
    if (!cloneable(thread->vm, obj->obj->descriptor)) {
      ThrowLangException("CloneNotSupportedException");
      return value_null();
    }

    bjvm_obj_header *new_obj = new_object(thread, obj->obj->descriptor);
    if (new_obj) {
      memcpy(new_obj + 1, obj->obj + 1,
             obj->obj->descriptor->instance_bytes - sizeof(bjvm_obj_header));
    }
    return (bjvm_stack_value){.obj = new_obj};
  }
  case BJVM_CD_KIND_PRIMITIVE_ARRAY: {
    bjvm_obj_header *new_array = CreatePrimitiveArray1D(
        thread, obj->obj->descriptor->primitive_component,
        *ArrayLength(obj->obj));
    if (!new_array) {
      return value_null();
    }
    memcpy(ArrayData(new_array), ArrayData(obj->obj),
           *ArrayLength(obj->obj) *
               sizeof_type_kind(obj->obj->descriptor->primitive_component));
    return (bjvm_stack_value){.obj = new_array};
  }
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang", Object, getClass, "()Ljava/lang/Class;") {
  return (bjvm_stack_value){
      .obj = (void *)bjvm_get_class_mirror(thread, obj->obj->descriptor)};
}

DECLARE_NATIVE("java/lang", Object, notifyAll, "()V") {
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Object, notify, "()V") {
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Object, wait, "()V") {
  return value_null(); // TODO
}
