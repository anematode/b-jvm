#include "analysis.h"
#include "objects.h"


#include <natives-dsl.h>

DECLARE_NATIVE("java/lang", NullPointerException, getExtendedNPEMessage, "()Ljava/lang/String;") {
#define T ((struct bjvm_native_Throwable *)obj->obj)
  bjvm_cp_method *method = T->method;
  if (!method) {  // We weren't able to record the exact source of the NPE
    return value_null();
  }
  int pc = T->faulting_insn;

  heap_string message;
  if (get_extended_npe_message(method, pc, &message)) {
    return value_null();
  }
  bjvm_obj_header *result = MakeJStringFromModifiedUTF8(thread, hslc(message), false);
  free_heap_str(message);
  return (bjvm_stack_value){.obj = result};
}
