#include <natives.h>

DECLARE_NATIVE("java/lang", String, intern, "()Ljava/lang/String;") {
  if (obj->obj == nullptr) {
    ThrowLangException(NullPointerException);
    return value_null();
  }
  int8_t *buf;
  size_t len;
  read_string(thread, obj->obj, &buf, &len);

  heap_string utf8_str = make_heap_str(len);

  for (size_t i = 0; i < len; ++i)
    utf8_str.chars[i] = buf[i];

  bjvm_stack_value result;
  result.obj = bjvm_intern_string(thread, hslc(utf8_str));

  free_heap_str(utf8_str);

  return result;
}