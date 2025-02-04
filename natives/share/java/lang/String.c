#include <natives-dsl.h>
#include <objects.h>

DECLARE_NATIVE("java/lang", String, intern, "()Ljava/lang/String;") {
  if (obj->obj == nullptr) {
    ThrowLangException(NullPointerException);
    return value_null();
  }

  return (bjvm_stack_value){.obj = InternJString(thread, obj->obj)};
}