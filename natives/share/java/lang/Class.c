#include "cached_classdescs.h"
#include "objects.h"

#include <exceptions.h>
#include <linkage.h>
#include <natives-dsl.h>
#include <reflection.h>

DECLARE_NATIVE("java/lang", Class, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", Class, getPrimitiveClass, "(Ljava/lang/String;)Ljava/lang/Class;") {
  DCHECK(argc == 1);
  if (args[0].handle->obj == nullptr) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  // this is fine because if it's a primitive type, it'll look the same regardless of how it's encoded
  obj_header *str_data = RawStringData(thread, args[0].handle->obj);
  char *chars = ArrayData(str_data);
  int len = ArrayLength(str_data);

  if (len > 10) {
    return value_null();
  }

  type_kind kind;
  if (strcmp(chars, "boolean") == 0) {
    kind = TYPE_KIND_BOOLEAN;
  } else if (strcmp(chars, "byte") == 0) {
    kind = TYPE_KIND_BYTE;
  } else if (strcmp(chars, "char") == 0) {
    kind = TYPE_KIND_CHAR;
  } else if (strcmp(chars, "short") == 0) {
    kind = TYPE_KIND_SHORT;
  } else if (strcmp(chars, "int") == 0) {
    kind = TYPE_KIND_INT;
  } else if (strcmp(chars, "long") == 0) {
    kind = TYPE_KIND_LONG;
  } else if (strcmp(chars, "float") == 0) {
    kind = TYPE_KIND_FLOAT;
  } else if (strcmp(chars, "double") == 0) {
    kind = TYPE_KIND_DOUBLE;
  } else if (strcmp(chars, "void") == 0) {
    kind = TYPE_KIND_VOID;
  } else {
    return value_null();
  }

  return (stack_value){.obj = (void *)primitive_class_mirror(thread, kind)};
}

DECLARE_NATIVE("java/lang", Class, getEnclosingMethod0, "()[Ljava/lang/Object;") {
  // "The array is expected to have three elements: the immediately enclosing
  // class, the immediately enclosing method or constructor's name (can be
  // null). the immediately enclosing method or constructor's descriptor (null
  // iff name is).
  classdesc *desc = unmirror_class(obj->obj);
  // Search the class attributes for an EnclosingMethod attribute
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  handle *array = make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->object, 3));
#define data ((obj_header **)ArrayData(array->obj))

  int error = resolve_class(thread, enclosing_method.class_info);
  CHECK(!error);
  data[0] = (void *)enclosing_method.class_info->classdesc->mirror;
  if (enclosing_method.nat != nullptr) {
    object name = MakeJStringFromModifiedUTF8(thread, enclosing_method.nat->name, true); // todo oom
    data[1] = name;
    name = MakeJStringFromModifiedUTF8(thread, enclosing_method.nat->descriptor, true);
    data[2] = name;
  }
#undef data
  stack_value result = (stack_value){.obj = array->obj};
  drop_handle(thread, array);
  return result;
}

DECLARE_NATIVE("java/lang", Class, getDeclaringClass0, "()Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  int error = resolve_class(thread, enclosing_method.class_info);
  CHECK(!error);
  return (stack_value){.obj = (void *)enclosing_method.class_info->classdesc->mirror};
}

DECLARE_NATIVE("java/lang", Class, getComponentType, "()Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  if (desc->kind == CD_KIND_ORDINARY || desc->kind == CD_KIND_PRIMITIVE) {
    // ints and ordinary objects have no components
    return value_null();
  }
  void *result = get_class_mirror(thread, desc->one_fewer_dim);
  DCHECK(result);
  return (stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, getModifiers, "()I") {
  classdesc *classdesc = unmirror_class(obj->obj);
  return (stack_value){.i = classdesc->access_flags};
}

DECLARE_NATIVE("java/lang", Class, getSuperclass, "()Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  cp_class_info *super = desc->super_class;
  if (!super || desc->access_flags & ACCESS_INTERFACE)
    return value_null();
  return (stack_value){.obj = (void *)get_class_mirror(thread, super->classdesc)};
}

DECLARE_NATIVE("java/lang", Class, getClassLoader, "()Ljava/lang/ClassLoader;") {
  classloader *cl = unmirror_class(obj->obj)->classloader;
  return (stack_value){.obj = cl->java_mirror};
}

DECLARE_NATIVE("java/lang", Class, getPermittedSubclasses0, "()[Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_PERMITTED_SUBCLASSES);
  if (attr == nullptr)
    return value_null();
  attribute_permitted_subclasses *permitted = &attr->permitted_subclasses;
  // Unmirror all classes first in case one of them triggers a GC
  for (int i = 0; i < permitted->entries_count; ++i) {
    cp_class_info *info = permitted->entries[i];
    if (resolve_class(thread, info)) {
      return value_null();
    }
    get_class_mirror(thread, info->classdesc);
  }
  // Create an array of the appropriate length and enjoy
  handle *array =
      make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, permitted->entries_count));
  if (array->obj == nullptr)
    return value_null();
  for (int i = 0; i < permitted->entries_count; ++i) {
    cp_class_info *info = permitted->entries[i];
    ((object *)ArrayData(array->obj))[i] = (void *)get_class_mirror(thread, info->classdesc);
  }
  return (stack_value){.obj = array->obj};
}

DECLARE_NATIVE("java/lang", Class, initClassName, "()Ljava/lang/String;") {
  classdesc *classdesc = unmirror_class(obj->obj);
  INIT_STACK_STRING(name, 1000);
  bprintf(name, "%.*s", fmt_slice(classdesc->name));
  for (u32 i = 0; i < classdesc->name.len; ++i) {
    name.chars[i] = name.chars[i] == '/' ? '.' : name.chars[i];
  }
  name.len = classdesc->name.len;
  void *str = MakeJStringFromModifiedUTF8(thread, name, true);
  StoreFieldObject(obj->obj, "java/lang/String", "name", str);
  return (stack_value){.obj = str};
}

DECLARE_NATIVE("java/lang", Class, forName0,
               "(Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/"
               "Class;)Ljava/lang/Class;") {
  // Read args[0] as a string
  obj_header *name_obj = args[0].handle->obj;
  obj_header *classloader = args[2].handle->obj;

  bool should_initialize = args[1].i;

  heap_string name_str = AsHeapString(name_obj, oom);
  exchange_slashes_and_dots((slice *)&name_str, hslc(name_str));
  classdesc *c;
  if (classloader == nullptr) {
    c = bootstrap_lookup_class(thread, hslc(name_str));
    if (thread->current_exception) { // e.g. ClassNotFoundException
      return value_null();
    }
  } else {
    // Invoke findClass on the classloader
    cp_method *find_class = method_lookup(classloader->descriptor, STR("loadClass"),
                                          STR("(Ljava/lang/String;)Ljava/lang/Class;"), true, false);
    CHECK(find_class);
    stack_value loadClass_args[2] = {{.obj = classloader}, {.obj = name_obj}};
    stack_value result = call_interpreter_synchronous(thread, find_class, loadClass_args);
    if (thread->current_exception) {
      // loadClass threw an exception, propagate it
      return value_null();
    }
    if (result.obj == nullptr) {
      // Raise ClassNotFoundException. Name uses slashes for some reason
      raise_vm_exception(thread, STR("java/lang/ClassNotFoundException"), hslc(name_str));
      return value_null();
    }
    c = unmirror_class(result.obj);
  }

  int error = link_class(thread, c);
  CHECK(!error);

  if (c && should_initialize) {
    initialize_class_t ctx = {.args = {thread, c}};
    thread->stack.synchronous_depth++;
    future_t f = initialize_class(&ctx);
    thread->stack.synchronous_depth--;
    CHECK(f.status == FUTURE_READY);

    if (ctx._result) {
      return value_null();
    }
  }

  free_heap_str(name_str);

  if (c) {
    return (stack_value){.obj = (void *)get_class_mirror(thread, c)};
  }

oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, desiredAssertionStatus0, "(Ljava/lang/Class;)Z") {
  return (stack_value){.i = 1}; // TODO add thread option
}

bool include_field(const cp_field *field, bool public_only) {
  return !public_only || field->access_flags & ACCESS_PUBLIC;
}

DECLARE_NATIVE("java/lang", Class, getDeclaredFields0, "(Z)[Ljava/lang/reflect/Field;") {
  classdesc *class = unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int fields = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->fields_count; ++i) {
    cp_field *field = class->fields + i;
    if (include_field(field, public_only)) {
      reflect_initialize_field_t ctx = {.args = {thread, class, field}};
      thread->stack.synchronous_depth++;
      future_t f = reflect_initialize_field(&ctx);
      CHECK(f.status == FUTURE_READY);
      thread->stack.synchronous_depth--;
      ++fields;
    }
  }
  classdesc *Field = cached_classes(thread->vm)->field;
  obj_header *result = CreateObjectArray1D(thread, Field, fields);
  if (!result)
    return value_null();
  struct native_Field **data = ArrayData(result);
  for (int i = 0, j = 0; i < class->fields_count; ++i) {
    cp_field *field = class->fields + i;
    if (include_field(field, public_only))
      data[j++] = field->reflection_field;
  }
  return (stack_value){.obj = result};
}

bool include_ctor(const cp_method *method, bool public_only) {
  return method->is_ctor && (!public_only || method->access_flags & ACCESS_PUBLIC);
}

// Get a list of all constructors on a class, optionally filtering by
// public_only
DECLARE_NATIVE("java/lang", Class, getDeclaredConstructors0, "(Z)[Ljava/lang/reflect/Constructor;") {
  classdesc *class = unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int ctors = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->methods_count; ++i) {
    cp_method *method = class->methods + i;
    if (include_ctor(method, public_only)) {
      reflect_initialize_constructor_t ctx = {.args = {thread, class, method}};
      thread->stack.synchronous_depth++;
      future_t f = reflect_initialize_constructor(&ctx);
      CHECK(f.status == FUTURE_READY);
      thread->stack.synchronous_depth--;
      ++ctors;
    }
  }
  // Then create the array
  classdesc *Ctor = cached_classes(thread->vm)->constructor;
  obj_header *result = CreateObjectArray1D(thread, Ctor, ctors);
  struct native_Constructor **data = ArrayData(result);
  int j = 0;
  for (int i = 0; i < class->methods_count; ++i) {
    cp_method *method = class->methods + i;
    if (include_ctor(method, public_only)) {
      data[j++] = method->reflection_ctor;
    }
  }
  return (stack_value){.obj = result};
}

bool include_method(const cp_method *method, bool public_only) {
  return !method->is_ctor && !method->is_clinit && (!public_only || method->access_flags & ACCESS_PUBLIC);
}

// Get a list of all methods on a class, optionally filtering by public_only
DECLARE_NATIVE("java/lang", Class, getDeclaredMethods0, "(Z)[Ljava/lang/reflect/Method;") {
  DCHECK(argc == 1);
  classdesc *class = unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int methods = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->methods_count; ++i) {
    cp_method *method = class->methods + i;
    if (include_method(method, public_only)) {
      reflect_initialize_method_t ctx = {.args = {thread, class, method}};
      thread->stack.synchronous_depth++;
      future_t f = reflect_initialize_method(&ctx);
      CHECK(f.status == FUTURE_READY);
      thread->stack.synchronous_depth--;
      ++methods;
    }
  }
  // Then create the array
  classdesc *Method = cached_classes(thread->vm)->method;
  link_class(thread, Method);
  obj_header *result = CreateObjectArray1D(thread, Method, methods);
  struct native_Method **data = ArrayData(result);
  for (int i = 0, j = 0; i < class->methods_count; ++i) {
    cp_method *method = class->methods + i;
    if (include_method(method, public_only))
      data[j++] = method->reflection_method;
  }
  return (stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, getDeclaredClasses0, "()[Ljava/lang/Class;") {
  classdesc *cd = unmirror_class(obj->obj);
  attribute_inner_classes *inner_classes = cd->inner_classes;
  int count = 0;
  if (inner_classes) {
    count = inner_classes->count;
  }
  handle *ret = make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, count));
  if (inner_classes) {
    for (int i = 0; i < count; ++i) {
      cp_class_info *info = inner_classes->classes[i];
      resolve_class(thread, info);
      if (!info->classdesc)
        continue;
      obj_header *mirror = (void *)get_class_mirror(thread, info->classdesc);
      *((obj_header **)ArrayData(ret->obj) + i) = mirror;
    }
  }
  object result = ret->obj;
  drop_handle(thread, ret);
  return (stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, isPrimitive, "()Z") {
  return (stack_value){.i = unmirror_class(obj->obj)->kind == CD_KIND_PRIMITIVE};
}

DECLARE_NATIVE("java/lang", Class, isInterface, "()Z") {
  return (stack_value){.i = !!(unmirror_class(obj->obj)->access_flags & ACCESS_INTERFACE)};
}

DECLARE_NATIVE("java/lang", Class, isAssignableFrom, "(Ljava/lang/Class;)Z") {
  classdesc *this_desc = unmirror_class(obj->obj);
  CHECK(!link_class(thread, this_desc));

  if (!args[0].handle->obj) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  classdesc *other_desc = unmirror_class(args[0].handle->obj);
  CHECK(!link_class(thread, other_desc));

  bool instanceof_ = other_desc == this_desc;
  if (!instanceof_ && (other_desc->kind != CD_KIND_PRIMITIVE && this_desc->kind != CD_KIND_PRIMITIVE)) {
    instanceof_ = instanceof(other_desc, this_desc);
  }
  return (stack_value){.i = instanceof_};
}

// Returns false when passed object is null
DECLARE_NATIVE("java/lang", Class, isInstance, "(Ljava/lang/Object;)Z") {
  classdesc *this_desc = unmirror_class(obj->obj);
  int result = 0;
  object arg = args[0].handle->obj;
  if (arg && this_desc->kind != CD_KIND_PRIMITIVE) {
    result = instanceof(arg->descriptor, this_desc);
  }
  return (stack_value){.i = result};
}

DECLARE_NATIVE("java/lang", Class, isArray, "()Z") {
  classdesc *desc = unmirror_class(obj->obj);
  return (stack_value){.i = desc->kind == CD_KIND_ORDINARY_ARRAY || desc->kind == CD_KIND_PRIMITIVE_ARRAY};
}

DECLARE_NATIVE("java/lang", Class, isHidden, "()Z") {
  bool is_hidden = unmirror_class(obj->obj)->is_hidden;
  return (stack_value){.i = is_hidden};
}

DECLARE_NATIVE("java/lang", Class, getNestHost0, "()Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  cp_class_info *nest_host = desc->nest_host;
  if (!nest_host || !resolve_class(thread, nest_host)) {
    return value_null();
  }

  return (stack_value){.obj = (void *)get_class_mirror(thread, nest_host->classdesc)};
}

DECLARE_NATIVE("java/lang", Class, getConstantPool, "()Ljdk/internal/reflect/ConstantPool;") {
  classdesc *desc = unmirror_class(obj->obj);
  return (stack_value){.obj = (void *)get_constant_pool_mirror(thread, desc)};
}

DECLARE_NATIVE("java/lang", Class, getRawAnnotations, "()[B") {
  classdesc *desc = unmirror_class(obj->obj);
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS);
  if (attr) {
    attribute_runtime_visible_annotations r = attr->annotations;
    obj_header *array = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, r.length);
    memcpy(ArrayData(array), r.data, r.length);
    return (stack_value){.obj = array};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getRawTypeAnnotations, "()[B") {
  classdesc *desc = unmirror_class(obj->obj);
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS);
  if (attr) {
    attribute_runtime_visible_type_annotations r = attr->type_annotations;
    obj_header *array = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, r.length);
    memcpy(ArrayData(array), r.data, r.length);
    return (stack_value){.obj = array};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getInterfaces0, "()[Ljava/lang/Class;") {
  classdesc *desc = unmirror_class(obj->obj);
  handle *array =
      make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, desc->interfaces_count));

  for (int i = 0; i < desc->interfaces_count; ++i) {
    cp_class_info *info = desc->interfaces[i];
    classdesc *iface = info->classdesc;
    obj_header *mirror = (void *)get_class_mirror(thread, iface);
    *((obj_header **)ArrayData(array->obj) + i) = mirror;
  }

  stack_value result = (stack_value){.obj = array->obj};
  drop_handle(thread, array);
  return result;
}

DECLARE_NATIVE("java/lang", Class, getGenericSignature0, "()Ljava/lang/String;") {
  classdesc *desc = unmirror_class(obj->obj);
  attribute *attr = find_attribute_by_kind(desc, ATTRIBUTE_KIND_SIGNATURE);
  if (attr) {
    return (stack_value){.obj = MakeJStringFromModifiedUTF8(thread, attr->signature.utf8, true)};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getProtectionDomain0, "()Ljava/security/ProtectionDomain;") {
  return value_null(); // TODO
}
