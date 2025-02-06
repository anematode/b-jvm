
#include <natives-dsl.h>

DECLARE_NATIVE("jdk/internal/perf", Perf, registerNatives, "()V") { return (bjvm_stack_value) { .i = 0 }; }
DECLARE_NATIVE("jdk/internal/perf", Perf, createLong, "(Ljava/lang/String;IIJ)Ljava/nio/ByteBuffer;") {
  // Call ByteBuffer.allocateDirect
  bjvm_classdesc *ByteBuffer = bootstrap_lookup_class(thread, STR("java/nio/ByteBuffer"));
  bjvm_cp_method *method = bjvm_method_lookup(ByteBuffer, STR("allocateDirect"), STR("(I)Ljava/nio/ByteBuffer;"), false, false);
  BJVM_CHECK(method);

  bjvm_stack_value result = call_interpreter_synchronous(thread, method, (bjvm_stack_value[]){{.i = 8}});
  BJVM_CHECK(result.obj);

  return (bjvm_stack_value) { .obj = result.obj };
}