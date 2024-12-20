#include <natives.h>

DECLARE_NATIVE("java/lang", String, intern, "()Ljava/lang/String;") {
  if (obj == nullptr) {
    ThrowLangException(NullPointerException);
    return value_null();
  }
  short *buf;
  size_t len;
  read_string(obj, &buf, &len);
  char *data = malloc((len + 1) * sizeof(char));
  for (size_t i = 0; i < len; ++i)
    data[i] = buf[i];
  data[len] = 0;
  bjvm_stack_value result;
  result.obj = bjvm_intern_string(thread, data);
  free(data);
  return result;
}