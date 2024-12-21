#include "bjvm.h"

/// Create a java.lang.String from a null-terminated C string.
bjvm_obj_header *MakeJavaStringUtf8(bjvm_thread *thread, char const *chars);

/// Helper for java.lang.String#length
inline int JavaStringLength(bjvm_thread *thread, bjvm_obj_header *string) {
  assert(utf8_equals(hslc(string->descriptor->name), "java/lang/String"));

  auto method = bjvm_easy_method_lookup(string->descriptor, str("length"),
                                        str("()I"), false, false);
  bjvm_stack_value result;
  bjvm_thread_run(thread, method, (bjvm_stack_value[]){}, &result);

  return result.i;
}

uint64_t ObjNextHashCode();