#include <arrays.h>
#include <linkage.h>
#include <objects.h>
#include <reflection.h>

DECLARE_ASYNC_VOID(fill_mn_with_field,
  locals(classdesc *search_on;),
  arguments(vm_thread *thread; handle *mn; cp_field *field;),
  invoked_methods(
    invoked_method(reflect_initialize_field)
    invoked_method(load_class_of_field_descriptor)
  )
)

// Copied from MethodHandleNatives.java
enum {
  MN_IS_METHOD = 0x00010000,        // method (not constructor)
  MN_IS_CONSTRUCTOR = 0x00020000,   // constructor
  MN_IS_FIELD = 0x00040000,         // field
  MN_IS_TYPE = 0x00080000,          // nested type
  MN_CALLER_SENSITIVE = 0x00100000, // @CallerSensitive annotation detected
};

#define M ((struct native_MemberName *)self->args.mn->obj)

DEFINE_ASYNC(fill_mn_with_field) {
#define field (self->args.field)
#define thread (self->args.thread)

  CHECK(field);
  AWAIT(reflect_initialize_field, thread, field->my_class, field);
  M->vmindex = field->byte_offset; // field offset
  M->flags |= field->access_flags;
  M->flags |= MN_IS_FIELD;
  AWAIT(load_class_of_field_descriptor, thread, field->my_class->classloader, field->descriptor);
  M->vmtarget = field;
  object mirror = (void *)get_class_mirror(thread, get_async_result(load_class_of_field_descriptor));
  M->type = mirror;
  mirror = (void *)get_class_mirror(thread, field->my_class);
  M->clazz = mirror;

  ASYNC_END_VOID()
#undef thread
#undef field
}

#undef M
#define M ((struct native_MemberName *)args->mn->obj)

DECLARE_ASYNC_VOID(fill_mn_with_method, locals(),
  arguments(vm_thread *thread; handle *mn; cp_method *method; bool dynamic_dispatch;),
  invoked_methods(
    invoked_method(reflect_initialize_constructor)
    invoked_method(reflect_initialize_method)
  ));

DEFINE_ASYNC(fill_mn_with_method) {
  CHECK(args->method);
  cp_method *method = args->method;
  classdesc *search_on = method->my_class;
  if (method->is_ctor) {
    AWAIT(reflect_initialize_constructor, args->thread, search_on, method);
    M->flags |= MN_IS_CONSTRUCTOR;
  } else {
    AWAIT(reflect_initialize_method, args->thread, search_on, method);
    M->flags |= MN_IS_METHOD;
  }

  M->vmtarget = method;
  M->vmindex = args->dynamic_dispatch ? 1 : -1; // ultimately, itable or vtable entry index
  M->flags |= method->access_flags;
  if (!method->is_signature_polymorphic) {
    object string = make_jstring_modified_utf8(args->thread, method->unparsed_descriptor);
    M->type = string;
  }
  object mirror = (void *)get_class_mirror(args->thread, search_on);
  M->clazz = mirror;

  ASYNC_END_VOID()
}

typedef enum { METHOD_RESOLVE_OK, METHOD_RESOLVE_NOT_FOUND, METHOD_RESOLVE_EXCEPTION } method_resolve_result;

method_handle_kind unpack_mn_kind(struct native_MemberName *mn) { return mn->flags >> 24 & 0xf; }

slice unparse_classdesc_to_field_descriptor(slice str, const classdesc *desc) {
  switch (desc->kind) {
  case CD_KIND_ORDINARY:
    return bprintf(str, "L%.*s;", fmt_slice(desc->name));
  case CD_KIND_ORDINARY_ARRAY:
  case CD_KIND_PRIMITIVE_ARRAY:
    return bprintf(str, "%.*s", fmt_slice(desc->name));
  case CD_KIND_PRIMITIVE:
    return bprintf(str, "%c", type_kind_to_char(desc->primitive_component));
  default:
    UNREACHABLE();
  }
}

heap_string unparse_method_type(const struct native_MethodType *mt) {
  INIT_STACK_STRING(desc, 10000);
  slice write = desc;
  write = subslice(write, bprintf(write, "(").len);
  for (int i = 0; i < ArrayLength(mt->ptypes); ++i) {
    struct native_Class *class = *((struct native_Class **)ArrayData(mt->ptypes) + i);
    write = subslice(write, unparse_classdesc_to_field_descriptor(write, class->reflected_class).len);
  }
  write = subslice(write, bprintf(write, ")").len);
  struct native_Class *rtype = (void *)mt->rtype;
  write = subslice(write, unparse_classdesc_to_field_descriptor(write, rtype->reflected_class).len);
  desc.len = write.chars - desc.chars;
  return make_heap_str_from(desc);
}

DECLARE_ASYNC(method_resolve_result, resolve_mn,
  locals(heap_string search_for; ),
  arguments(vm_thread *thread; handle *mn; ),
  invoked_methods(
    invoked_method(fill_mn_with_field)
    invoked_method(fill_mn_with_method)
  ));

DEFINE_ASYNC(resolve_mn) {
  if (M->name) {
    read_string_to_utf8(args->thread, &self->search_for, M->name);
  } else {
    self->search_for = make_heap_str(0);
  }
  classdesc *search_on = ((struct native_Class *)M->clazz)->reflected_class;
  link_class(args->thread, search_on);

  method_handle_kind kind = unpack_mn_kind(M); // TODO validate
  M->flags &= (int)0xFF000000U;
  bool dynamic_dispatch, found = false;

  if (kind == MH_KIND_GET_STATIC || kind == MH_KIND_PUT_STATIC || kind == MH_KIND_GET_FIELD ||
      kind == MH_KIND_PUT_FIELD) {
    classdesc *field_type = unmirror_class(M->type);
    INIT_STACK_STRING(field_str, 1000);
    slice field_desc = unparse_classdesc_to_field_descriptor(field_str, field_type);
    cp_field *field = field_lookup(search_on, hslc(self->search_for), field_desc);
    if (!field) {
      M->type = nullptr;
    } else {
      found = true;
      AWAIT(fill_mn_with_field, args->thread, args->mn, field);
    }
  } else if (kind == MH_KIND_INVOKE_STATIC || kind == MH_KIND_INVOKE_SPECIAL || kind == MH_KIND_NEW_INVOKE_SPECIAL ||
             kind == MH_KIND_INVOKE_VIRTUAL || kind == MH_KIND_INVOKE_INTERFACE) {
    dynamic_dispatch = kind == MH_KIND_INVOKE_VIRTUAL || kind == MH_KIND_INVOKE_INTERFACE;

    struct native_MethodType *mt = (void *)M->type;
    heap_string descriptor = unparse_method_type(mt);

    cp_method *method = method_lookup(search_on, hslc(self->search_for), hslc(descriptor), true, false);
    free_heap_str(descriptor);

    if (!method) {
      M->type = nullptr;
    } else {
      found = true;
      AWAIT(fill_mn_with_method, args->thread, args->mn, method, dynamic_dispatch);
    }
  } else {
    UNREACHABLE();
  }

  free_heap_str(self->search_for);
  ASYNC_RETURN(found ? METHOD_RESOLVE_OK : METHOD_RESOLVE_NOT_FOUND);

on_oom:
  ASYNC_END(METHOD_RESOLVE_EXCEPTION);
}

#include <natives-dsl.h>

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getConstant, "(I)I") {
  DCHECK(argc == 1);
  enum { GC_COUNT_GWT = 4, GC_LAMBDA_SUPPORT = 5 };
  switch (args[0].i) {
  case GC_COUNT_GWT:
    return (stack_value){.i = 1};
  case GC_LAMBDA_SUPPORT:
    return (stack_value){.i = 1};
  default:
    UNREACHABLE();
  }
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getNamedCon, "(I[Ljava/lang/Object;)I") {
  // Ignore this sanity check, which can be done by just not touching the array
  return (stack_value){.i = 0};
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandleNatives, resolve,
                     "(Ljava/lang/invoke/MemberName;Ljava/lang/Class;IZ)Ljava/lang/"
                     "invoke/MemberName;",
                     locals(handle *mn), invoked_methods(invoked_method(resolve_mn))) {
  DCHECK(argc == 4);

  self->mn = (void *)args[0].handle;

  AWAIT(resolve_mn, thread, self->mn);
  method_resolve_result found = get_async_result(resolve_mn);
  if (unlikely(found == METHOD_RESOLVE_EXCEPTION)) {
    ASYNC_RETURN(value_null());
  }

  if (unlikely(found == METHOD_RESOLVE_NOT_FOUND)) {
    raise_vm_exception(thread, STR("java/lang/LinkageError"), STR("Failed to resolve MemberName"));
    ASYNC_RETURN(value_null());
  }

  ASYNC_END((stack_value){.obj = (void *)self->mn->obj});
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandleNatives, getMemberVMInfo,
                     "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;", locals(),
                     invoked_methods(invoked_method(call_interpreter))) {
  // Create object array of length 2. Returns {vmindex, vmtarget},
  // boxing the vmindex as a Long.
  DCHECK(argc == 1);
#define mn ((struct native_MemberName *)args[0].handle->obj)

  classdesc *Long = cached_classes(thread->vm)->long_;
  cp_method *valueOf = method_lookup(Long, STR("valueOf"), STR("(J)Ljava/lang/Long;"), false, false);

  AWAIT(call_interpreter, thread, valueOf, (stack_value[]){{.l = mn->vmindex}});
  handle *vmindex_long = make_handle(thread, get_async_result(call_interpreter).obj);
  // todo: check exception

  obj_header *array = CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, 2);
  // todo check exception (out of memory error)

  obj_header **data = ArrayData(array);
  data[0] = vmindex_long->obj;
  drop_handle(thread, vmindex_long);

  // either mn->type or mn itself depending on the kind
  method_handle_kind unpacked = unpack_mn_kind(mn);
  if (unpacked == MH_KIND_GET_STATIC || unpacked == MH_KIND_PUT_STATIC || unpacked == MH_KIND_GET_FIELD ||
      unpacked == MH_KIND_PUT_FIELD) {
    data[1] = mn->type;
  } else if (unpacked == MH_KIND_INVOKE_STATIC || unpacked == MH_KIND_INVOKE_SPECIAL ||
             unpacked == MH_KIND_NEW_INVOKE_SPECIAL || unpacked == MH_KIND_INVOKE_VIRTUAL ||
             unpacked == MH_KIND_INVOKE_INTERFACE) {
    data[1] = (void *)mn;
  } else {
    UNREACHABLE();
  }

  ASYNC_END((stack_value){.obj = array});
#undef mn
}

DECLARE_ASYNC_NATIVE("java/lang/invoke", MethodHandleNatives, init,
                     "(Ljava/lang/invoke/MemberName;Ljava/lang/Object;)V", locals(handle *mn),
                     invoked_methods(invoked_method(fill_mn_with_method) invoked_method(fill_mn_with_field))) {
  self->mn = args[0].handle;
  obj_header *target = args[1].handle->obj;

  slice s = target->descriptor->name;
  if (utf8_equals(s, "java/lang/reflect/Method")) {
    cp_method *m = unmirror_method(target);
    AWAIT(fill_mn_with_method, thread, self->mn, m, true);

#undef M
#define M ((struct native_MemberName *)self->mn->obj)

    M->flags |= (m->access_flags & ACCESS_STATIC) ? MH_KIND_INVOKE_STATIC << 24 : MH_KIND_INVOKE_VIRTUAL << 24;
  } else if (utf8_equals(s, "java/lang/reflect/Constructor")) {
    AWAIT(fill_mn_with_method, thread, self->mn, unmirror_ctor(target), true);
    M->flags |= MH_KIND_NEW_INVOKE_SPECIAL << 24;
  } else if (utf8_equals(s, "java/lang/reflect/Field")) {
    cp_field *field = *unmirror_field(target);
    AWAIT(fill_mn_with_field, thread, self->mn, field);
    M->flags |= (field->access_flags & ACCESS_STATIC) ? MH_KIND_GET_STATIC << 24 : MH_KIND_GET_FIELD << 24;
  } else {
    UNREACHABLE();
  }
  M->resolution = nullptr;
  ASYNC_END(value_null());
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, objectFieldOffset, "(Ljava/lang/invoke/MemberName;)J") {
  DCHECK(argc == 1);
  struct native_MemberName *mn = (void *)args[0].handle->obj;
  return (stack_value){.l = mn->vmindex};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, staticFieldBase,
               "(Ljava/lang/invoke/MemberName;)Ljava/lang/Object;") {
  DCHECK(argc == 1);
  struct native_MemberName *mn = (void *)args[0].handle->obj;
  return (stack_value){.obj = (void *)unmirror_class(mn->clazz)->static_fields};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, staticFieldOffset, "(Ljava/lang/invoke/MemberName;)J") {
  DCHECK(argc == 1);
  struct native_MemberName *mn = (void *)args[0].handle->obj;
  return (stack_value){.l = mn->vmindex};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, getMembers,
               "(Ljava/lang/Class;Ljava/lang/String;Ljava/lang/String;ILjava/"
               "lang/Class;I[Ljava/lang/invoke/MemberName;)I") {
  DCHECK(argc == 7);
  // defc, matchName, matchSig, matchFlags, lookupClass, totalCount, buf

  return (stack_value){.i = 0};
}

DECLARE_NATIVE("java/lang/invoke", MethodHandleNatives, clearCallSiteContext,
               "(Ljava/lang/invoke/MethodHandleNatives$CallSiteContext;)V") {
  return value_null();
}