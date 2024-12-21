#include <natives.h>
#include "bjvm.h"

bjvm_method_handle_kind unpack_mn_kind(struct bjvm_native_MemberName *mn) {
  return mn->flags >> 24 & 0xf;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, resolve, "(Ljava/lang/invoke/MemberName;Ljava/lang/Class;)Ljava/lang/invoke/MemberName;") {
  assert(argc == 2);

  struct bjvm_native_MemberName *mn = (void*)args[0].obj;
  struct bjvm_native_Class *caller = (void*)args[1].obj;

  heap_string search_for = read_string_to_utf8(mn->name);
  bjvm_classdesc *search_on = ((struct bjvm_native_Class *)mn->clazz)->reflected_class;
  printf("SEARCHING FOR: %.*s\n", fmt_slice(search_for));
  printf("Searching on: %.*s\n", fmt_slice(search_on->name));

  bjvm_method_handle_kind kind = unpack_mn_kind(mn); // TODO validate
  bool is_static = false;

  switch (kind) {
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_STATIC:
    is_static = true;
    [[fallthrough]];
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_PUT_FIELD:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
    is_static = true;
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    break;
  default:
    UNREACHABLE();
  }

  return value_null();
}