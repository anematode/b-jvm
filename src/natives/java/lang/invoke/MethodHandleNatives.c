#include <natives.h>
#include "bjvm.h"

bjvm_method_handle_kind unpack_mn_kind(struct bjvm_native_MemberName *mn) {
  return mn->flags >> 24 & 0xf;
}

bjvm_utf8 unparse_field_descriptor(bjvm_utf8 str, const bjvm_classdesc *desc) {
  switch (desc->kind) {
  case BJVM_CD_KIND_ORDINARY:
    return bprintf(str, "L%.*s;", fmt_slice(desc->name));
  case BJVM_CD_KIND_ORDINARY_ARRAY:
  case BJVM_CD_KIND_PRIMITIVE_ARRAY:
    return bprintf(str, "%s", fmt_slice(desc->name));
  case BJVM_CD_KIND_PRIMITIVE:
    return bprintf(str, "%c", desc->primitive_component);
  default:
    UNREACHABLE();
  }
}

heap_string unparse_method_type(const struct bjvm_native_MethodType *mt) {
  INIT_STACK_STRING(desc, 10000);
  bjvm_utf8 write = desc;
  write = slice(write, bprintf(write, "(").len);
  for (int i = 0; i < *array_length(mt->ptypes); ++i) {
    struct bjvm_native_Class *class = *((struct bjvm_native_Class**)array_data(mt->ptypes) + i);
    write = slice(write, unparse_field_descriptor(write, class->reflected_class).len);
  }
  write = slice(write, bprintf(write, ")").len);
  struct bjvm_native_Class *rtype = (void*)mt->rtype;
  write = slice(write, unparse_field_descriptor(write, rtype->reflected_class).len);
  desc.len = write.chars - desc.chars;
  return make_heap_str_from(desc);
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, resolve, "(Ljava/lang/invoke/MemberName;Ljava/lang/Class;)Ljava/lang/invoke/MemberName;") {
  assert(argc == 2);

  struct bjvm_native_MemberName *mn = (void*)args[0].obj;
  struct bjvm_native_Class *caller = (void*)args[1].obj;

  heap_string search_for = mn->name ? read_string_to_utf8(mn->name) : make_heap_str(0);
  bjvm_classdesc *search_on = ((struct bjvm_native_Class *)mn->clazz)->reflected_class;
  printf("SEARCHING FOR: %.*s\n", fmt_slice(search_for));
  printf("Searching on: %.*s\n", fmt_slice(search_on->name));
  printf("Search type: %.*s\n", fmt_slice(mn->type->descriptor->name));

  bjvm_method_handle_kind kind = unpack_mn_kind(mn); // TODO validate
  bool is_static = false, dynamic_dispatch = true, found = false;

  switch (kind) {
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_STATIC:
    is_static = true;
    [[fallthrough]];
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_PUT_FIELD:
    bjvm_cp_field *field = bjvm_easy_field_lookup(search_on, hslc(search_for), hslc(mn->type->descriptor->name));
    if (!field) {
      break;
    }
    bjvm_reflect_initialize_field(thread, search_on, field);
    mn->vmtarget = (void*)mn;
    mn->resolution = (void*)field->reflection_field;
    mn->vmindex = 1; // field offset
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
    is_static = true;
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
    dynamic_dispatch = false;
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    struct bjvm_native_MethodType *mt = (void*)mn->type;
    heap_string descriptor = unparse_method_type(mt);
    bjvm_cp_method *method = bjvm_easy_method_lookup(search_on, hslc(search_for), hslc(descriptor), false, false);
    free_heap_str(descriptor);

    if (!method) {
      break;
    }

    found = true;
    if (utf8_equals(method->name, "<init>")) {
      bjvm_reflect_initialize_constructor(thread, search_on, method);
      mn->resolution = (void*)method->reflection_ctor;
    } else {
      bjvm_reflect_initialize_method(thread, search_on, method);
      mn->resolution = (void*)method->reflection_method;
    }

    mn->vmtarget = (void*)mn;
    mn->vmindex = dynamic_dispatch ? 1 : -1;  // ultimately, itable or vtable entry index
    break;
  default:
    UNREACHABLE();
  }

  free_heap_str(search_for);
  return found ? (bjvm_stack_value) { .obj = (void*)mn } : value_null();
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getMemberVMInfo, "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;") {
  // Create object array of length 2. Make the first element the vmtarget and the second the vmindex as a boxed Long.
  assert(argc == 1);
  struct bjvm_native_MemberName *mn = (void*)args[0].obj;
  bjvm_obj_header *array = create_object_array(thread, bootstrap_class_create(thread, str("java/lang/Object")), 2);

  bjvm_obj_header **data = array_data(array);

  bjvm_classdesc* Long = bootstrap_class_create(thread, str("java/lang/Long"));
  bjvm_cp_method* valueFrom = bjvm_easy_method_lookup(Long, str("valueOf"), str("(J)Ljava/lang/Long;"), false, false);

  bjvm_stack_value result;
  bjvm_thread_run(thread, valueFrom, (bjvm_stack_value[]) { { .l = mn->vmindex } }, &result);
  data[0] = result.obj;

  printf("MN INDEX: %llu\n", mn->vmindex);

  data[1] = mn->vmtarget;

  return (bjvm_stack_value) { .obj = array };
}