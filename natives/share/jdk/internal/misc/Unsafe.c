#include "objects.h"
#include "roundrobin_scheduler.h"

#include <natives-dsl.h>

// In general, for the Unsafe API, we use the byte offset of the field
// (be it static or nonstatic) to identify it. This works well because we're
// supposed to use the static memory offset when the object is null.

DECLARE_NATIVE("jdk/internal/misc", Unsafe, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("jdk/internal/misc", Unsafe, arrayBaseOffset0, "(Ljava/lang/Class;)I") {
  return (stack_value){.i = kArrayDataOffset};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, shouldBeInitialized0, "(Ljava/lang/Class;)Z") {
  classdesc *desc = unmirror_class(args[0].handle->obj);
  return (stack_value){.i = desc->state != CD_STATE_INITIALIZED};
}

DECLARE_ASYNC_NATIVE("jdk/internal/misc", Unsafe, ensureClassInitialized0, "(Ljava/lang/Class;)V", locals(),
                     invoked_methods(invoked_method(initialize_class))) {
  classdesc *desc = unmirror_class(args[0].handle->obj);
  if (desc->state != CD_STATE_INITIALIZED) {
    AWAIT(initialize_class, thread, desc);
  }
  ASYNC_END(value_null());
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, objectFieldOffset0, "(Ljava/lang/reflect/Field;)J") {
  DCHECK(argc == 1);
  cp_field *reflect_field = *unmirror_field(args[0].handle->obj);
  return (stack_value){.l = reflect_field->byte_offset};
}

// objectFieldOffset1(Class<?> c, String name);
DECLARE_NATIVE("jdk/internal/misc", Unsafe, objectFieldOffset1, "(Ljava/lang/Class;Ljava/lang/String;)J") {
  DCHECK(argc == 2);
  classdesc *desc = unmirror_class(args[0].handle->obj);
  heap_string name;
  int err = read_string_to_utf8(thread, &name, args[1].handle->obj);
  CHECK(!err);

  s64 result = 0;

  for (int i = 0; i < desc->fields_count; ++i) {
    cp_field *field = &desc->fields[i];
    if (utf8_equals_utf8(field->name, hslc(name))) {
      result = field->byte_offset;
      break;
    }
  }

  free_heap_str(name);
  return (stack_value){.l = result};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, staticFieldOffset0, "(Ljava/lang/reflect/Field;)J") {
  DCHECK(argc == 1);
  cp_field *reflect_field = *unmirror_field(args[0].handle->obj);
  return (stack_value){.l = reflect_field->byte_offset};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, staticFieldBase0, "(Ljava/lang/reflect/Field;)Ljava/lang/Object;") {
  DCHECK(argc == 1);
  // Return pointer to static_fields
  cp_field *reflect_field = *unmirror_field(args[0].handle->obj);
  return (stack_value){.obj = (void *)reflect_field->my_class->static_fields};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, arrayIndexScale0, "(Ljava/lang/Class;)I") {
  DCHECK(argc == 1);
  classdesc *desc = unmirror_class(args[0].handle->obj);
  switch (desc->kind) {
  case CD_KIND_ORDINARY_ARRAY:
    return (stack_value){.i = sizeof(void *)};
  case CD_KIND_PRIMITIVE_ARRAY:
    return (stack_value){.i = sizeof_type_kind(desc->primitive_component)};
  case CD_KIND_ORDINARY:
  default: // invalid
    return (stack_value){.i = 0};
  }
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getIntVolatile, "(Ljava/lang/Object;J)I") {
  DCHECK(argc == 2);
  return (stack_value){.i = *(int *)((void *)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getLongVolatile, "(Ljava/lang/Object;J)J") {
  DCHECK(argc == 2);
  return (stack_value){.l = *(s64 *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putReferenceVolatile, "(Ljava/lang/Object;JLjava/lang/Object;)V") {
  DCHECK(argc == 3);
  *(void *volatile *)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].handle->obj;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putOrderedReference, "(Ljava/lang/Object;JLjava/lang/Object;)V") {
  DCHECK(argc == 3);
  *(void **)((void *)args[0].handle->obj + args[1].l) = args[2].handle->obj;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putOrderedLong, "(Ljava/lang/Object;JJ)V") {
  DCHECK(argc == 3);
  *(s64 *)((void *)args[0].handle->obj + args[1].l) = args[2].l;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putReference, "(Ljava/lang/Object;JLjava/lang/Object;)V") {
  DCHECK(argc == 3);
  *(void **)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].handle->obj;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, compareAndSetInt, "(Ljava/lang/Object;JII)Z") {
  DCHECK(argc == 4);
  obj_header *target = args[0].handle->obj;
  s64 offset = args[1].l;
  int expected = args[2].i, update = args[3].i;
  int ret = __sync_bool_compare_and_swap((int *)((uintptr_t)target + offset), expected, update);
  return (stack_value){.i = ret};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, compareAndSetLong, "(Ljava/lang/Object;JJJ)Z") {
  DCHECK(argc == 4);
  obj_header *target = args[0].handle->obj;
  s64 offset = args[1].l;
  s64 expected = args[2].l, update = args[3].l;
  int ret = __sync_bool_compare_and_swap((s64 *)((uintptr_t)target + offset), expected, update);
  return (stack_value){.l = ret};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, compareAndSetReference,
               "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z") {
  DCHECK(argc == 4);
  obj_header *target = args[0].handle->obj;
  s64 offset = args[1].l;
  uintptr_t expected = (uintptr_t)args[2].handle->obj, update = (uintptr_t)args[3].handle->obj;
  int ret = __sync_bool_compare_and_swap((uintptr_t *)((uintptr_t)target + offset), expected, update);
  return (stack_value){.l = ret};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, compareAndExchangeReference,
               "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;") {
  DCHECK(argc == 4);
  obj_header *target = args[0].handle->obj;
  s64 offset = args[1].l;
  uintptr_t expected = (uintptr_t)args[2].handle->obj, update = (uintptr_t)args[3].handle->obj;
  uintptr_t ret = __sync_val_compare_and_swap((uintptr_t *)((uintptr_t)target + offset), expected, update);
  return (stack_value){.obj = (void *)ret};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, addressSize, "()I") { return (stack_value){.i = sizeof(void *)}; }

DECLARE_NATIVE("jdk/internal/misc", Unsafe, allocateMemory0, "(J)J") {
  DCHECK(argc == 1);
  void *l = malloc(args[0].l);
  arrput(thread->vm->unsafe_allocations, l);
  return (stack_value){.l = (s64)l};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, allocateInstance, "(Ljava/lang/Class;)Ljava/lang/Object;") {
  DCHECK(argc == 1);
  classdesc *desc = unmirror_class(args[0].handle->obj);
  obj_header *o = AllocateObject(thread, desc, desc->instance_bytes);
  return (stack_value){.obj = o};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, freeMemory0, "(J)V") {
  DCHECK(argc == 1);
  free((void *)args[0].l);
  void **unsafe_allocations = thread->vm->unsafe_allocations;
  for (int i = 0; i < arrlen(unsafe_allocations); ++i) {
    if (unsafe_allocations[i] == (void *)args[0].l) {
      arrdelswap(unsafe_allocations, i);
      return value_null();
    }
  }
  fprintf(stderr, "Attempted to free memory that was not allocated by Unsafe\n");
  abort();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putLong, "(JJ)V", 1) {
  DCHECK(argc == 2);
  *(s64 *)args[0].l = args[1].l;
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putLong, "(Ljava/lang/Object;JJ)V", 2) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].handle->obj + args[1].l, &args[2].l, sizeof(s64));
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putLongVolatile, "(JJ)V", 1) {
  DCHECK(argc == 2);
  *(s64 *)args[0].l = args[1].l;
  return value_null();
}

DECLARE_ASYNC_NATIVE("jdk/internal/misc", Unsafe, park, "(ZJ)V", locals(rr_wakeup_info wakeup_info), invoked_methods()) {
  DCHECK(argc == 2);
  bool isAbsolute = args[0].i;
  s64 time = args[1].l; // elapsed nanos if !absolute, else epoch millis deadline

  u64 start_us = get_unix_us();
  u64 deadline_us;

  if (isAbsolute) {
    if (time < 0) ASYNC_RETURN_VOID(); // negative epoch time
    deadline_us = (u64)time * 1000; // epoch millis to micros
    if (deadline_us < start_us) ASYNC_RETURN_VOID(); // already happened
  } else {
    if (time < 0) ASYNC_RETURN_VOID(); // negative duration nanos, already elapsed
    deadline_us = time == 0 ? 0 : start_us + (time / 1000); // 0 duration means no timeout
  }

  rr_scheduler *scheduler = thread->vm->scheduler;
  assert(scheduler && "Cannot park thread without a scheduler!");

  self->wakeup_info.kind = RR_THREAD_PARK;
  self->wakeup_info.wakeup_us = deadline_us;
  ASYNC_YIELD((void *)&self->wakeup_info);

  ASYNC_END(value_null());
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, unpark, "(Ljava/lang/Object;)V") {
  // todo: add checks that this is actually a thread object

  rr_scheduler *scheduler = thread->vm->scheduler;
  assert(scheduler && "Cannot park thread without a scheduler!");
  struct native_Thread *thr = (struct native_Thread *) args[0].handle->obj;

  [[maybe_unused]] int err = set_unpark_permit((vm_thread *) thr->eetop);
  assert(!err && "Tried to unpark a non-alive thread");
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putLongVolatile, "(Ljava/lang/Object;JJ)V", 2) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].handle->obj + args[1].l, &args[2].l, sizeof(s64));
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putInt, "(Ljava/lang/Object;JI)V") {
  DCHECK(argc == 3);
  *(s32 *)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].i;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putIntVolatile, "(Ljava/lang/Object;JI)V") {
  DCHECK(argc == 3);
  *(s32 *)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].i;
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putShort, "(JS)V", 1) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].l, &args[1].i, sizeof(short));
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putShort, "(Ljava/lang/Object;JS)V", 2) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].handle->obj + args[1].l, &args[2].i, sizeof(short));
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putDouble, "(JD)V", 1) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].l, &args[1].d, sizeof(double));
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, putDouble, "(Ljava/lang/Object;JD)V", 2) {
  DCHECK(argc == 3);
  memcpy((char *)args[0].handle->obj + args[1].l, &args[2].d, sizeof(double));
  return value_null();
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, getDouble, "(Ljava/lang/Object;J)D", 1) {
  DCHECK(argc == 2);
  return (stack_value){.d = *(double *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putByte, "(Ljava/lang/Object;JB)V") {
  DCHECK(argc == 3);
  *(s8 *)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].i;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, putBoolean, "(Ljava/lang/Object;JZ)V") {
  DCHECK(argc == 3);
  *(u8 *)((uintptr_t)args[0].handle->obj + args[1].l) = args[2].i;
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getReference, "(Ljava/lang/Object;J)Ljava/lang/Object;") {
  DCHECK(argc == 2);
  return (stack_value){.obj = *(void **)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getInt, "(Ljava/lang/Object;J)I") {
  DCHECK(argc == 2);
  return (stack_value){.i = *(int *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getShort, "(Ljava/lang/Object;J)S") {
  DCHECK(argc == 2);
  return (stack_value){.i = *(short *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getByte, "(Ljava/lang/Object;J)B") {
  DCHECK(argc == 2);
  return (stack_value){.i = *(s8 *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getBoolean, "(Ljava/lang/Object;J)Z") {
  DCHECK(argc == 2);
  return (stack_value){.i = (bool)*(u8 *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getLong, "(Ljava/lang/Object;J)J") {
  DCHECK(argc == 2);
  return (stack_value){.l = *(s64 *)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE_OVERLOADED("jdk/internal/misc", Unsafe, getByte, "(J)B", 1) {
  DCHECK(argc == 1);
  return (stack_value){.i = *(s8 *)args[0].l};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, getReferenceVolatile, "(Ljava/lang/Object;J)Ljava/lang/Object;") {
  DCHECK(argc == 2);
  return (stack_value){.obj = *(void **)((uintptr_t)args[0].handle->obj + args[1].l)};
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, defineClass,
               "(Ljava/lang/String;[BIILjava/lang/ClassLoader;Ljava/security/"
               "ProtectionDomain;)Ljava/lang/Class;") {
  DCHECK(argc == 6);

  // TODO more validation of this stuff

  obj_header *name = args[0].handle->obj;
  obj_header *data = args[1].handle->obj;
  int offset = args[2].i;
  int length = args[3].i;
  obj_header *loader = args[4].handle->obj;
  obj_header *pd = args[5].handle->obj;

  (void)loader;
  (void)pd;

  heap_string name_str = AsHeapString(name, on_oom);
  u8 *bytes = ArrayData(data) + offset;

  // Replace name_str with slashes
  for (u32 i = 0; i < name_str.len; ++i) {
    if (name_str.chars[i] == '.') {
      name_str.chars[i] = '/';
    }
  }

  INIT_STACK_STRING(cf_name, 1000);
  cf_name = bprintf(cf_name, "%.*s.class", fmt_slice(name_str));

  classdesc *result = define_bootstrap_class(thread, hslc(name_str), bytes, length);

  free_heap_str(name_str);

  initialize_class_t pox = {.args = {thread, result}};
  future_t f = initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);

  return (stack_value){.obj = (void *)get_class_mirror(thread, result)};

on_oom:
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, storeFence, "()V") {
  __sync_synchronize();
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, fullFence, "()V") {
  __sync_synchronize();
  return value_null();
}

DECLARE_NATIVE("jdk/internal/misc", Unsafe, copyMemory0, "(Ljava/lang/Object;JLjava/lang/Object;JJ)V") {
  DCHECK(argc == 5);
  void *src = (void *)((uintptr_t)args[0].handle->obj + args[1].l);
  void *dst = (void *)((uintptr_t)args[2].handle->obj + args[3].l);
  size_t len = args[4].l;
  if (len > 0) {
    memcpy(dst, src, len);
  }
  return value_null();
}

// setMemory0(Object o, long offset, long bytes, byte value);
DECLARE_NATIVE("jdk/internal/misc", Unsafe, setMemory0, "(Ljava/lang/Object;JJB)V") {
  assert(argc == 4);
  memset((void *)((uintptr_t)args[0].handle->obj + args[1].l), args[3].i, args[2].l);
  return value_null();
}
