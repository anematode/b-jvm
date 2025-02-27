#include <math.h>
#include <natives-dsl.h>
#include <objects.h>
#include <unistd.h>
#include <util.h>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/time.h>
#define USE_SYS_TIME
#endif

DECLARE_NATIVE("java/lang", System, mapLibraryName, "(Ljava/lang/String;)Ljava/lang/String;") {
  struct native_String *orig_name = (struct native_String *)args[0].handle->obj;
  heap_string str = AsHeapString((object)orig_name, on_oom);

  if (heap_str_append(&str, STR(".lib"))) {
    thread->current_exception = thread->out_of_mem_error;
    goto on_oom;
  }

  obj_header *result = MakeJStringFromData(thread, hslc(str), orig_name->coder);
  if (!result) {
    thread->current_exception = thread->out_of_mem_error;
    goto on_oom;
  }

  free_heap_str(str);

  return (stack_value){.obj = result};

on_oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", System, arraycopy, "(Ljava/lang/Object;ILjava/lang/Object;II)V") {
  DCHECK(argc == 5);
  obj_header *src = args[0].handle->obj;
  obj_header *dest = args[2].handle->obj;
  if (src == nullptr || dest == nullptr) {
    raise_null_pointer_exception(thread);
    return value_null();
  }
  if (src->descriptor->kind == CD_KIND_ORDINARY) {
    raise_array_store_exception(thread, STR("source is not an array"));
    return value_null();
  }
  if (dest->descriptor->kind == CD_KIND_ORDINARY) {
    raise_array_store_exception(thread, STR("destination is not an array"));
    return value_null();
  }
  bool src_is_1d_primitive = Is1DPrimitiveArray(src), dst_is_1d_primitive = Is1DPrimitiveArray(dest);
  if (src_is_1d_primitive != dst_is_1d_primitive ||
      (src_is_1d_primitive && src->descriptor->primitive_component != dest->descriptor->primitive_component)) {
    raise_array_store_exception(thread, STR("source and destination are not compatible"));
    return value_null();
  }

  int src_pos = args[1].i;
  int dest_pos = args[3].i;
  int length = args[4].i;
  int src_length = ArrayLength(src);
  int dest_length = ArrayLength(dest);
  // Verify that everything is in bounds
  // TODO add more descriptive error messages
  if (src_pos < 0 || dest_pos < 0 || length < 0 || (s64)src_pos + length > src_length ||
      (s64)dest_pos + length > dest_length) {
    raise_vm_exception_no_msg(thread, STR("java/lang/ArrayIndexOutOfBoundsException"));
    return value_null();
  }

  // We can copy primitive arrays directly.
  // For reference arrays, if the component type of the src class is an
  // instanceof the destination class, then we don't need to perform any checks.
  // Otherwise, we need to perform an instanceof check on each element and raise
  // an ArrayStoreException as appropriate.
  if (src_is_1d_primitive || instanceof(src->descriptor->one_fewer_dim, dest->descriptor->one_fewer_dim)) {
    size_t element_size = sizeof(void *);

    if (src_is_1d_primitive) {
      switch (src->descriptor->primitive_component) {
#define CASE(type, underlying)                                                                                         \
  case TYPE_KIND_##type:                                                                                               \
    element_size = sizeof(underlying);                                                                                 \
    break;
        CASE(BYTE, s8)
        CASE(CHAR, u16)
        CASE(DOUBLE, double)
        CASE(FLOAT, float)
        CASE(INT, s32)
        CASE(LONG, s64)
        CASE(SHORT, s16)
        CASE(BOOLEAN, u8)
#undef CASE

      default:
        UNREACHABLE();
      }
    }

    memmove((char *)ArrayData(dest) + dest_pos * element_size, (char *)ArrayData(src) + src_pos * element_size,
            length * element_size);

    return value_null();
  }

  for (int i = 0; i < length; ++i) {
    // may-alias case handled above
    obj_header *src_elem = ((obj_header **)ArrayData(src))[src_pos + i];
    if (src_elem && !instanceof(src_elem->descriptor, dest->descriptor->one_fewer_dim)) {
      raise_array_store_exception(thread, STR("source and destination are not compatible"));
      return value_null();
    }
    ((obj_header **)ArrayData(dest))[dest_pos + i] = src_elem;
  }

  return value_null();
}

DECLARE_NATIVE("java/lang", System, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", System, setOut0, "(Ljava/io/PrintStream;)V") {
  // Look up the field System.out
  classdesc *system_class = cached_classes(thread->vm)->system;
  StoreStaticFieldObject(system_class, "java/io/PrintStream", "out", args[0].handle->obj);
  return value_null();
}

DECLARE_NATIVE("java/lang", System, setIn0, "(Ljava/io/InputStream;)V") {
  // Look up the field System.in
  classdesc *system_class = cached_classes(thread->vm)->system;
  StoreStaticFieldObject(system_class, "java/io/InputStream", "in", args[0].handle->obj);
  return value_null();
}

DECLARE_NATIVE("java/lang", System, setErr0, "(Ljava/io/PrintStream;)V") {
  classdesc *system_class = cached_classes(thread->vm)->system;
  StoreStaticFieldObject(system_class, "java/io/PrintStream", "err", args[0].handle->obj);
  return value_null();
}

DECLARE_NATIVE("java/lang", System, identityHashCode, "(Ljava/lang/Object;)I") {
  assert(argc == 1);
  return (stack_value){.i = get_object_hash_code(thread->vm, args[0].handle->obj)};
}

s64 micros() {
#ifdef EMSCRIPTEN
  return (s64)(emscripten_get_now() * 1000);
#elifdef USE_SYS_TIME
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec * 1000000 + tv.tv_usec;
#else
  return time(NULL) * 1000;
#endif
}

DECLARE_NATIVE("java/lang", System, currentTimeMillis, "()J") { return (stack_value){.l = micros() / 1000}; }

int calls = 0;
DECLARE_NATIVE("java/lang", System, nanoTime, "()J") { return (stack_value){.l = micros() * 1000}; }
