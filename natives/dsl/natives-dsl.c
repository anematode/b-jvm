#include <natives-dsl.h>

void push_bjvm_native(slice class_name, slice method_name, slice signature, bjvm_native_callback cb) {
  if (class_name.chars[0] == '/')
    class_name = subslice(class_name, 1);
  bjvm_native_t native = {
        .class_path = class_name,
        .method_name = method_name,
        .method_descriptor = signature,
        .callback = cb
  };

  if (bjvm_native_count == bjvm_native_capacity) {
    bjvm_native_capacity = bjvm_native_capacity ? bjvm_native_capacity * 2 : 16;
    bjvm_native_t *bjvm_natives_ = (bjvm_native_t *)realloc(bjvm_natives, bjvm_native_capacity * sizeof(bjvm_native_t));
    DCHECK(bjvm_natives_ != nullptr);

    bjvm_natives = bjvm_natives_;
  }
  bjvm_natives[bjvm_native_count++] = native;
}
