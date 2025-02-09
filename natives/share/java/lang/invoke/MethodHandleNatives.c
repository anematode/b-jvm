#include <linkage.h>
#include <natives-dsl.h>
#include <reflection.h>

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, registerNatives,
               "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getConstant, "(I)I") {
  DCHECK(argc == 1);
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

slice unparse_classdesc_to_field_descriptor(slice str,
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
  slice write = desc;
  write = subslice(write, bprintf(write, "(").len);
  for (int i = 0; i < *ArrayLength(mt->ptypes); ++i) {
    struct bjvm_native_Class *class =
        *((struct bjvm_native_Class **)ArrayData(mt->ptypes) + i);
    write = subslice(write, unparse_classdesc_to_field_descriptor(
                             write, class->reflected_class)
                             .len);
  }
  write = subslice(write, bprintf(write, ")").len);
  struct bjvm_native_Class *rtype = (void *)mt->rtype;
  write = subslice(
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
  DCHECK(method);
  bjvm_classdesc *search_on = method->my_class;
  if (method->is_ctor) {
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
    M->type = MakeJStringFromModifiedUTF8(thread, method->unparsed_descriptor, true);
  M->clazz = (void *)bjvm_get_class_mirror(thread, search_on);
}

typedef enum {
  METHOD_RESOLVE_OK,
  METHOD_RESOLVE_NOT_FOUND,
  METHOD_RESOLVE_EXCEPTION
} method_resolve_result;

method_resolve_result resolve_mn(bjvm_thread *thread, bjvm_handle *mn) {
  heap_string search_for =
      M->name ? AsHeapString(M->name, on_oom) : make_heap_str(0);
  bjvm_classdesc *search_on =
      ((struct bjvm_native_Class *)M->clazz)->reflected_class;
  bjvm_link_class(thread, search_on);

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
    slice field_desc =
        unparse_classdesc_to_field_descriptor(field_str, field_type);
    bjvm_cp_field *field =
        bjvm_field_lookup(search_on, hslc(search_for), field_desc);
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
  return found ? METHOD_RESOLVE_OK : METHOD_RESOLVE_NOT_FOUND;

  on_oom:
  return METHOD_RESOLVE_EXCEPTION;
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, resolve,
               "(Ljava/lang/invoke/MemberName;Ljava/lang/Class;IZ)Ljava/lang/"
               "invoke/MemberName;") {
  DCHECK(argc == 4);

  bjvm_handle *mn = (void *)args[0].handle;

  method_resolve_result found = resolve_mn(thread, mn);
  if (unlikely(found == METHOD_RESOLVE_EXCEPTION)) {
    return value_null();
  }

  if (unlikely(found == METHOD_RESOLVE_NOT_FOUND)) {
    // Raise LinkageError
    bjvm_raise_vm_exception(thread, STR("java/lang/LinkageError"),
                         STR("Failed to resolve MemberName"));
    return value_null();
  }

  return (bjvm_stack_value){.obj = (void *)mn->obj};
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandleNatives, getMemberVMInfo,
               "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;",
               locals(), invoked_methods(invoked_method(call_interpreter))) {
  // Create object array of length 2. Returns {vmindex, vmtarget},
  // boxing the vmindex as a Long.
  DCHECK(argc == 1);
#define mn ((struct bjvm_native_MemberName *) args[0].handle->obj)

  bjvm_classdesc *Long = bootstrap_lookup_class(thread, STR("java/lang/Long"));
  bjvm_cp_method *valueOf = bjvm_method_lookup(
      Long, STR("valueOf"), STR("(J)Ljava/lang/Long;"), false, false);


  AWAIT(call_interpreter, thread, valueOf, (bjvm_stack_value[]){{.l = mn->vmindex}});
  bjvm_handle *vmindex_long = bjvm_make_handle(thread, get_async_result(call_interpreter).obj);
  // todo: check exception

  bjvm_obj_header *array = CreateObjectArray1D(
      thread, bootstrap_lookup_class(thread, STR("java/lang/Object")), 2);
  // todo check exception (out of memory error)

  bjvm_obj_header **data = ArrayData(array);
  data[0] = vmindex_long->obj;
  bjvm_drop_handle(thread, vmindex_long);

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
    data[1] = (void *) mn;
    break;
  default:
    UNREACHABLE();
  }

  ASYNC_END((bjvm_stack_value) { .obj = array });
#undef mn
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, init,
               "(Ljava/lang/invoke/MemberName;Ljava/lang/Object;)V") {
  bjvm_handle *mn = args[0].handle;
  bjvm_obj_header *target = args[1].handle->obj;

  slice s = hslc(target->descriptor->name);
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
  DCHECK(argc == 1);
  struct bjvm_native_MemberName *mn = (void *)args[0].handle->obj;
  return (bjvm_stack_value){.l = mn->vmindex};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, staticFieldBase,
               "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;") {
  DCHECK(argc == 1);
  struct bjvm_native_MemberName *mn = (void *)args[0].handle->obj;
  return (bjvm_stack_value){.obj = (void*)bjvm_unmirror_class(mn->clazz)->static_fields};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, staticFieldOffset,
               "(Ljava/lang/invoke/MemberName;)J") {
  DCHECK(argc == 1);
  struct bjvm_native_MemberName *mn = (void *)args[0].handle->obj;
  return (bjvm_stack_value){.l = mn->vmindex};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getMembers,
               "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;ILjava/"
               "lang/Class;I[Ljava/lang/invoke/MemberName;)I") {
  DCHECK(argc == 7);
  // defc, matchName, matchSig, matchFlags, lookupClass, totalCount, buf

  return (bjvm_stack_value){.i = 0};
}