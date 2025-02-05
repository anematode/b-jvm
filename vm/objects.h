#include "bjvm.h"
#include "cached_classdescs.h"

#ifndef OBJECTS_H
#define OBJECTS_H

u64 ObjNextHashCode();


typedef enum : s32 {
  STRING_CODER_LATIN1 = 0,
  STRING_CODER_UTF16 = 1
} string_coder_kind;

bjvm_obj_header *MakeJStringFromModifiedUTF8(bjvm_thread *thread, slice data, bool intern);
bjvm_obj_header *MakeJStringFromCString(bjvm_thread *thread, char const* data, bool intern);
bjvm_obj_header *MakeJStringFromData(bjvm_thread *thread, slice data, string_coder_kind encoding);
bjvm_obj_header *InternJString(bjvm_thread *thread, bjvm_obj_header *str);

/// Helper for java.lang.String#length
static inline int JavaStringLength(bjvm_thread *thread, bjvm_obj_header *string) {
  assert(utf8_equals(hslc(string->descriptor->name), "java/lang/String"));

  auto method = bjvm_method_lookup(string->descriptor, STR("length"),
                                   STR("()I"), false, false);
  bjvm_stack_value result = call_interpreter_synchronous(thread, method, (bjvm_stack_value[]){});

  return result.i;
}


/// Extracts the inner array of the given java.lang.String.
/// The data are either UTF-16 or latin1 encoded.
static inline bjvm_obj_header *RawStringData([[maybe_unused]] bjvm_thread const *thread, bjvm_obj_header const *string) {
  assert(string->descriptor == thread->vm->cached_classdescs->string);

  return ((struct bjvm_native_String*)string)->value;
}

static inline object AllocateObject(bjvm_thread *thread,
                                              bjvm_classdesc *descriptor,
                                              size_t data_size) {
  assert(descriptor);
  assert(descriptor->state >=
         BJVM_CD_STATE_LINKED); // important to know the size
  object obj = (object)bump_allocate(thread, sizeof(bjvm_obj_header) + data_size);
  if (obj) {
    obj->mark_word = ObjNextHashCode();
    obj->descriptor = descriptor;
  }
  return obj;
}

#endif