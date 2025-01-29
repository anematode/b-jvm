#include <natives-dsl.h>

void push_bjvm_native(bjvm_utf8 class_name, bjvm_utf8 method_name, bjvm_utf8 signature, bjvm_native_callback cb) {
  if (class_name.chars[0] == '/')
    class_name = slice(class_name, 1);
  bjvm_native_t native = {
        .class_path = class_name,
        .method_name = method_name,
        .method_descriptor = signature,
        .callback = cb
  };

  if (bjvm_native_count == bjvm_native_capacity) {
    bjvm_native_capacity = bjvm_native_capacity ? bjvm_native_capacity * 2 : 16;
    bjvm_native_t *bjvm_natives_ = (bjvm_native_t *)realloc(bjvm_natives, bjvm_native_capacity * sizeof(bjvm_native_t));
    assert(bjvm_natives_ != nullptr);

    bjvm_natives = bjvm_natives_;
  }
  bjvm_natives[bjvm_native_count++] = native;
}
