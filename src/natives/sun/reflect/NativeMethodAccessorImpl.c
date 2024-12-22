#include <natives.h>

DECLARE_NATIVE(
    "sun/reflect", NativeMethodAccessorImpl, invoke0,
    "(Ljava/lang/reflect/Method;Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;") {
  // TODO all this
  bjvm_cp_method *method = *bjvm_unmirror_method(args[0].obj);
  bjvm_stack_value result;
  int arg_i = 0;
  if (!(method->access_flags & BJVM_ACCESS_STATIC)) {
    // Instance method. Try converting obj to the class of the method and
  }

  return value_null();
}
