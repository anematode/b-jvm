#include <natives.h>

// In general, for the Unsafe API, we use the byte offset of the field
// (be it static or nonstatic) to identify it. This works well because we're
// supposed to use the static memory offset when the object is null.

DECLARE_NATIVE("sun/misc", Unsafe, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("sun/misc", Unsafe, arrayBaseOffset, "(Ljava/lang/Class;)I") {
  return (bjvm_stack_value){.i = kArrayDataOffset};
}

DECLARE_NATIVE("sun/misc", Unsafe, shouldBeInitialized,
               "(Ljava/lang/Class;)Z") {
  bjvm_classdesc *desc = bjvm_unmirror_class(args[0].obj);
  return (bjvm_stack_value){.i = desc->state == BJVM_CD_STATE_INITIALIZED};
}

DECLARE_NATIVE("sun/misc", Unsafe, ensureClassInitialized,
               "(Ljava/lang/Class;)V") {
  bjvm_classdesc *desc = bjvm_unmirror_class(args[0].obj);
  if (desc->state != BJVM_CD_STATE_INITIALIZED)
    UNREACHABLE(); // TODO figure out what normal JVM does here
  return value_null();
}

DECLARE_NATIVE("sun/misc", Unsafe, objectFieldOffset,
               "(Ljava/lang/reflect/Field;)J") {
  assert(argc == 1);
  bjvm_cp_field *reflect_field = *bjvm_unmirror_field(args[0].obj);
  return (bjvm_stack_value){.l = reflect_field->byte_offset};
}

DECLARE_NATIVE("sun/misc", Unsafe, arrayIndexScale, "(Ljava/lang/Class;)I") {
  assert(argc == 1);
  bjvm_classdesc *desc = bjvm_unmirror_class(args[0].obj);
  switch (desc->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY:
    return (bjvm_stack_value){.i = sizeof(void *)};
  case BJVM_CD_KIND_PRIMITIVE_ARRAY:
    return (bjvm_stack_value){.i = sizeof_type_kind(desc->primitive_component)};
  case BJVM_CD_KIND_ORDINARY:
  default: // invalid
    return (bjvm_stack_value){.i = 0};
  }
}

DECLARE_NATIVE("sun/misc", Unsafe, getIntVolatile, "(Ljava/lang/Object;J)I") {
  assert(argc == 2);
  return (bjvm_stack_value){.i = *(int *)((void *)args[0].obj + args[1].l)};
}

DECLARE_NATIVE("sun/misc", Unsafe, putObjectVolatile, "(Ljava/lang/Object;JLjava/lang/Object;)V") {
  assert(argc == 3);
  *(void **)((void *)args[0].obj + args[1].l) = args[2].obj;
  return value_null();
}

DECLARE_NATIVE("sun/misc", Unsafe, compareAndSwapInt,
               "(Ljava/lang/Object;JII)Z") {
  assert(argc == 4);
  bjvm_obj_header *target = args[0].obj;
  int64_t offset = args[1].l;
  int expected = args[2].i, update = args[3].i;
  int ret = __sync_bool_compare_and_swap((int *)((void *)target + offset),
                                         expected, update);
  return (bjvm_stack_value){.i = ret};
}

DECLARE_NATIVE("sun/misc", Unsafe, compareAndSwapLong,
               "(Ljava/lang/Object;JJJ)Z") {
  assert(argc == 4);
  bjvm_obj_header *target = args[0].obj;
  int64_t offset = args[1].l;
  int64_t expected = args[2].l, update = args[3].l;
  int ret = __sync_bool_compare_and_swap((int64_t *)((void *)target + offset),
                                         expected, update);
  return (bjvm_stack_value){.l = ret};
}

DECLARE_NATIVE("sun/misc", Unsafe, compareAndSwapObject,
               "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z") {
  assert(argc == 4);
  bjvm_obj_header *target = args[0].obj;
  int64_t offset = args[1].l;
  uintptr_t expected = (uintptr_t)args[2].obj, update = (uintptr_t)args[3].obj;
  int ret = __sync_bool_compare_and_swap((uintptr_t *)((void *)target + offset),
                                         expected, update);
  return (bjvm_stack_value){.l = ret};
}

DECLARE_NATIVE("sun/misc", Unsafe, addressSize, "()I") {
  return (bjvm_stack_value){.i = sizeof(void *)};
}

DECLARE_NATIVE("sun/misc", Unsafe, allocateMemory, "(J)J") {
  assert(argc == 1);
  return (bjvm_stack_value){.l = (int64_t)malloc(args[0].l)};
}

DECLARE_NATIVE("sun/misc", Unsafe, freeMemory, "(J)V") {
  assert(argc == 1);
  free((void *)args[0].l);
  return value_null();
}

DECLARE_NATIVE("sun/misc", Unsafe, putLong, "(JJ)V") {
  assert(argc == 2);
  *(int64_t *)args[0].l = args[1].l;
  return value_null();
}

DECLARE_NATIVE("sun/misc", Unsafe, getByte, "(J)B") {
  assert(argc == 1);
  return (bjvm_stack_value){.i = *(int8_t *)args[0].l};
}

DECLARE_NATIVE("sun/misc", Unsafe, getObjectVolatile,
               "(Ljava/lang/Object;J)Ljava/lang/Object;") {
  assert(argc == 2);
  return (bjvm_stack_value){.obj = *(void **)((void *)args[0].obj + args[1].l)};
}
