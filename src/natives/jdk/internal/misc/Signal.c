#include <natives.h>

DECLARE_NATIVE("jdk/internal/misc", Signal, findSignal0, "(Ljava/lang/String;)I") {
  heap_string signal_name;
  if (read_string_to_utf8(thread, &signal_name, args[0].handle->obj) != 0) { // oom
    return (bjvm_stack_value){.i = -1};
  }
  free_heap_str(signal_name);
  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("jdk/internal/misc", Signal, handle0, "(IJ)J") {
  return (bjvm_stack_value){.i = 0};
}
