#include <natives.h>

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, registerNatives,
               "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getConstant, "(I)I") {
  assert(argc == 1);
  enum { GC_COUNT_GWT = 4, GC_LAMBDA_SUPPORT = 5 };
  switch (args[0].i) {
  case GC_COUNT_GWT:
    return (bjvm_stack_value){.i = 1};
  case GC_LAMBDA_SUPPORT:
    return (bjvm_stack_value){.i = 1};
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getNamedCon,
               "(I[Ljava/lang/Object;)I") {
  // Ignore this sanity check, which can be done by just not touching the array
  return (bjvm_stack_value){.i = 0};
}

bjvm_method_handle_kind unpack_mn_kind(struct bjvm_native_MemberName *mn) {
  return mn->flags >> 24 & 0xf;
}

bjvm_utf8 unparse_classdesc_to_field_descriptor(bjvm_utf8 str,
                                                const bjvm_classdesc *desc) {
  switch (desc->kind) {
  case BJVM_CD_KIND_ORDINARY:
    return bprintf(str, "L%.*s;", fmt_slice(desc->name));
  case BJVM_CD_KIND_ORDINARY_ARRAY:
  case BJVM_CD_KIND_PRIMITIVE_ARRAY:
    return bprintf(str, "%.*s", fmt_slice(desc->name));
  case BJVM_CD_KIND_PRIMITIVE:
    return bprintf(str, "%c", desc->primitive_component);
  default:
    UNREACHABLE();
  }
}

// Copied from MethodHandleNatives.java
enum {
  MN_IS_METHOD = 0x00010000,        // method (not constructor)
  MN_IS_CONSTRUCTOR = 0x00020000,   // constructor
  MN_IS_FIELD = 0x00040000,         // field
  MN_IS_TYPE = 0x00080000,          // nested type
  MN_CALLER_SENSITIVE = 0x00100000, // @CallerSensitive annotation detected
};

heap_string unparse_method_type(const struct bjvm_native_MethodType *mt) {
  INIT_STACK_STRING(desc, 10000);
  bjvm_utf8 write = desc;
  write = slice(write, bprintf(write, "(").len);
  for (int i = 0; i < *ArrayLength(mt->ptypes); ++i) {
    struct bjvm_native_Class *class =
        *((struct bjvm_native_Class **)ArrayData(mt->ptypes) + i);
    write = slice(write, unparse_classdesc_to_field_descriptor(
                             write, class->reflected_class)
                             .len);
  }
  write = slice(write, bprintf(write, ")").len);
  struct bjvm_native_Class *rtype = (void *)mt->rtype;
  write = slice(
      write,
      unparse_classdesc_to_field_descriptor(write, rtype->reflected_class).len);
  desc.len = write.chars - desc.chars;
  return make_heap_str_from(desc);
}

#define M ((struct bjvm_native_MemberName *)mn->obj)

void fill_mn_with_field(bjvm_thread *thread, bjvm_handle *mn,
                        bjvm_cp_field *field) {
  bjvm_classdesc *search_on = field->my_class;
  bjvm_reflect_initialize_field(thread, search_on, field);
  M->vmindex = field->byte_offset; // field offset
  M->flags |= field->access_flags;
  M->flags |= MN_IS_FIELD;
  bjvm_classdesc *field_cd =
      load_class_of_field_descriptor(thread, field->descriptor);
  M->vmtarget = field;
  M->type = (void *)bjvm_get_class_mirror(thread, field_cd);
  M->clazz = (void *)bjvm_get_class_mirror(thread, search_on);
}

void fill_mn_with_method(bjvm_thread *thread, bjvm_handle *mn,
                         bjvm_cp_method *method, bool dynamic_dispatch) {
  assert(method);
  bjvm_classdesc *search_on = method->my_class;
  if (utf8_equals(method->name, "<init>")) {
    bjvm_reflect_initialize_constructor(thread, search_on, method);
    M->flags |= MN_IS_CONSTRUCTOR;
  } else {
    bjvm_reflect_initialize_method(thread, search_on, method);
    M->flags |= MN_IS_METHOD;
  }

  M->vmtarget = method;
  M->vmindex =
      dynamic_dispatch ? 1 : -1; // ultimately, itable or vtable entry index
  M->flags |= method->access_flags;
  if (!method->is_signature_polymorphic)
    M->type = bjvm_intern_string(thread, method->unparsed_descriptor);
  M->clazz = (void *)bjvm_get_class_mirror(thread, search_on);
}

bool resolve_mn(bjvm_thread *thread, bjvm_handle *mn) {
  heap_string search_for =
      M->name ? read_string_to_utf8(M->name) : make_heap_str(0);
  bjvm_classdesc *search_on =
      ((struct bjvm_native_Class *)M->clazz)->reflected_class;

  bjvm_method_handle_kind kind = unpack_mn_kind(M); // TODO validate
  M->flags &= (int)0xFF000000U;
  bool dynamic_dispatch = true, found = false;

  switch (kind) {
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_STATIC:
    [[fallthrough]];
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_PUT_FIELD:
    bjvm_classdesc *field_type = bjvm_unmirror_class(M->type);
    INIT_STACK_STRING(field_str, 1000);
    bjvm_utf8 field_desc =
        unparse_classdesc_to_field_descriptor(field_str, field_type);
    bjvm_cp_field *field =
        bjvm_easy_field_lookup(search_on, hslc(search_for), field_desc);
    if (!field) {
      break;
    }
    found = true;
    fill_mn_with_field(thread, mn, field);
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
    dynamic_dispatch = false;
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    struct bjvm_native_MethodType *mt = (void *)M->type;
    heap_string descriptor = unparse_method_type(mt);
    bjvm_cp_method *method = bjvm_method_lookup(search_on, hslc(search_for),
                                                hslc(descriptor), true, false);
    free_heap_str(descriptor);

    if (!method) {
      M->type = nullptr;
      break;
    }

    found = true;
    fill_mn_with_method(thread, mn, method, dynamic_dispatch);
    break;
  default:
    UNREACHABLE();
  }

  free_heap_str(search_for);
  return found;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, resolve,
               "(Ljava/lang/invoke/MemberName;Ljava/lang/Class;)Ljava/lang/"
               "invoke/MemberName;") {
  assert(argc == 2);

  struct bjvm_native_Class *caller = (void *)args[1].handle->obj;
  bjvm_handle *mn = (void *)args[0].handle;

  bool found = resolve_mn(thread, mn);
  (void)found;
  if (!found) {
    // Raise LinkageError
    bjvm_raise_vm_exception(thread, STR("java/lang/LinkageError"),
                         STR("Failed to resolve MemberName"));
    return value_null();
  }

  return (bjvm_stack_value){.obj = (void *)mn->obj};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getMemberVMInfo,
               "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;") {
  // Create object array of length 2. Make the first element the vmtarget and
  // the second the vmindex as a boxed Long.
  assert(argc == 1);
  struct bjvm_native_MemberName *mn = (void *)args[0].handle->obj;
  bjvm_obj_header *array = CreateObjectArray1D(
      thread, bootstrap_lookup_class(thread, STR("java/lang/Object")), 2);

  bjvm_obj_header **data = ArrayData(array);

  bjvm_classdesc *Long = bootstrap_lookup_class(thread, STR("java/lang/Long"));
  bjvm_cp_method *valueFrom = bjvm_method_lookup(
      Long, STR("valueOf"), STR("(J)Ljava/lang/Long;"), false, false);

  bjvm_stack_value result;
  bjvm_thread_run_root(thread, valueFrom, (bjvm_stack_value[]){{.l = mn->vmindex}},
                  &result);
  data[0] = result.obj;

  // either mn->type or mn itself depending on the kind
  switch (unpack_mn_kind(mn)) {
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_STATIC:
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_PUT_FIELD:
    data[1] = mn->type;
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    data[1] = (void *)mn;
    break;
  default:
    UNREACHABLE();
  }

  return (bjvm_stack_value){.obj = array};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, init,
               "(Ljava/lang/invoke/MemberName;Ljava/lang/Object;)V") {
  bjvm_handle *mn = args[0].handle;
  bjvm_obj_header *target = args[1].handle->obj;

  bjvm_utf8 s = hslc(target->descriptor->name);
  if (utf8_equals(s, "java/lang/reflect/Method")) {
    bjvm_cp_method *m = *bjvm_unmirror_method(target);
    fill_mn_with_method(thread, mn, m, true);
    M->flags |= (m->access_flags & BJVM_ACCESS_STATIC)
                    ? BJVM_MH_KIND_INVOKE_STATIC << 24
                    : BJVM_MH_KIND_INVOKE_VIRTUAL << 24;
  } else if (utf8_equals(s, "java/lang/reflect/Constructor")) {
    fill_mn_with_method(thread, mn, *bjvm_unmirror_ctor(target), true);
    M->flags |= BJVM_MH_KIND_NEW_INVOKE_SPECIAL << 24;
  } else if (utf8_equals(s, "java/lang/reflect/Field")) {
    bjvm_cp_field *field = *bjvm_unmirror_field(target);
    fill_mn_with_field(thread, mn, field);
    M->flags |= (field->access_flags & BJVM_ACCESS_STATIC)
                    ? BJVM_MH_KIND_GET_STATIC << 24
                    : BJVM_MH_KIND_GET_FIELD << 24;
  } else {
    UNREACHABLE();
  }
  M->resolution = nullptr;
  return value_null();
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, objectFieldOffset,
               "(Ljava/lang/invoke/MemberName;)J") {
  assert(argc == 1);
  struct bjvm_native_MemberName *mn = (void *)args[0].handle->obj;
  return (bjvm_stack_value){.l = mn->vmindex};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getMembers,
               "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;ILjava/"
               "lang/Class;I[Ljava/lang/invoke/MemberName;)I") {
  assert(argc == 7);
  // defc, matchName, matchSig, matchFlags, lookupClass, totalCount, buf

  return (bjvm_stack_value){.i = 0};
}