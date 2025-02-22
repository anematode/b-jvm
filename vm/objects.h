#include "bjvm.h"
#include "cached_classdescs.h"

#include <gc.h>

#ifndef OBJECTS_H
#define OBJECTS_H

u64 ObjNextHashCode();

typedef enum : s32 { STRING_CODER_LATIN1 = 0, STRING_CODER_UTF16 = 1 } string_coder_kind;

obj_header *MakeJStringFromModifiedUTF8(vm_thread *thread, slice data, bool intern);
obj_header *MakeJStringFromCString(vm_thread *thread, char const *data, bool intern);
obj_header *MakeJStringFromData(vm_thread *thread, slice data, string_coder_kind encoding);
obj_header *InternJString(vm_thread *thread, obj_header *str);

/// Helper for java.lang.String#length
static inline int JavaStringLength(vm_thread *thread, obj_header *string) {
  DCHECK(utf8_equals(string->descriptor->name, "java/lang/String"));

  auto method = method_lookup(string->descriptor, STR("length"), STR("()I"), false, false);
  stack_value result = call_interpreter_synchronous(thread, method, (stack_value[]){});

  return result.i;
}

/// Extracts the inner array of the given java.lang.String.
/// The data are either UTF-16 or latin1 encoded.
static inline obj_header *RawStringData([[maybe_unused]] vm_thread const *thread, obj_header const *string) {
  DCHECK(utf8_equals(string->descriptor->name, "java/lang/String"));
  return ((struct native_String *)string)->value;
}

static inline object AllocateObject(vm_thread *thread, classdesc *descriptor, size_t allocation_size) {
  DCHECK(descriptor);
  DCHECK(descriptor->state >= CD_STATE_LINKED); // important to know the size
  object obj = (object)bump_allocate(thread, allocation_size);
  if (obj) {
    obj->header_word.expanded_data = (monitor_data *)(uintptr_t)IS_MARK_WORD;
    obj->descriptor = descriptor;
    DCHECK(size_of_object(obj) <= allocation_size);
  }
  return obj;
}

// Get the hash code of the object, computing it if it is not already computed.
s32 get_object_hash_code(object o);

#endif