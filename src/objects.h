#include "bjvm.h"

uint64_t ObjNextHashCode();

/// Create a java.lang.String from a null-terminated C string.
bjvm_obj_header *MakeJavaStringUtf8(bjvm_thread *thread, char const *chars);

/// Helper for java.lang.String#length
inline int JavaStringLength(bjvm_thread *thread, bjvm_obj_header *string) {
  assert(utf8_equals(hslc(string->descriptor->name), "java/lang/String"));

  auto method = bjvm_method_lookup(string->descriptor, STR("length"),
                                   STR("()I"), false, false);
  bjvm_stack_value result;
  bjvm_thread_run(thread, method, (bjvm_stack_value[]){}, &result);

  return result.i;
}

static inline bjvm_obj_header *AllocateObject(bjvm_thread *thread,
                                              bjvm_classdesc *descriptor,
                                              size_t data_size) {
  assert(descriptor);
  assert(descriptor->state >=
         BJVM_CD_STATE_LINKED); // important to know the size
  bjvm_obj_header *obj =
      bump_allocate(thread, sizeof(bjvm_obj_header) + data_size);
  if (obj) {
    obj->mark_word = ObjNextHashCode();
    obj->descriptor = descriptor;
  }
  return obj;
}