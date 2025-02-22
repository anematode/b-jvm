
#include <natives-dsl.h>

DECLARE_NATIVE("jdk/internal/perf", Perf, registerNatives, "()V") { return (stack_value){.i = 0}; }
DECLARE_NATIVE("jdk/internal/perf", Perf, createLong, "(Ljava/lang/String;IIJ)Ljava/nio/ByteBuffer;") {
  // Call ByteBuffer.allocateDirect
  classdesc *ByteBuffer = bootstrap_lookup_class(thread, STR("java/nio/ByteBuffer"));
  cp_method *method = method_lookup(ByteBuffer, STR("allocateDirect"), STR("(I)Ljava/nio/ByteBuffer;"), false, false);
  CHECK(method);

  stack_value result = call_interpreter_synchronous(thread, method, (stack_value[]){{.i = 8}});
  if (thread->current_exception)
    return value_null();
  CHECK(result.obj);

  return (stack_value){.obj = result.obj};
}