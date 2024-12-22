#include <natives.h>

DECLARE_NATIVE(
    "sun/reflect", NativeMethodAccessorImpl, invoke0,
    "(Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;") {
  // TODO all this
  bjvm_cp_method *method = *bjvm_unmirror_method(args[0].obj);
  bjvm_stack_value result;

  if (method->access_flags & BJVM_ACCESS_STATIC) {

  }

  return value_null();
}
