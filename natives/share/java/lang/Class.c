#include "cached_classdescs.h"
#include "objects.h"

#include <exceptions.h>
#include <linkage.h>
#include <natives-dsl.h>
#include <reflection.h>

static bjvm_attribute *find_attribute_by_kind(bjvm_classdesc *desc, bjvm_attribute_kind kind) {
  for (int i = 0; i < desc->attributes_count; ++i) {
    if (desc->attributes[i].kind == kind) {
      return desc->attributes + i;
    }
  }
  return nullptr;
}

DECLARE_NATIVE("java/lang", Class, registerNatives, "()V") { return value_null(); }

DECLARE_NATIVE("java/lang", Class, getPrimitiveClass, "(Ljava/lang/String;)Ljava/lang/Class;") {
  DCHECK(argc == 1);
  if (args[0].handle->obj == nullptr) {
    raise_null_pointer_exception(thread);
    return value_null();
  }

  // this is fine because if it's a primitive type, it'll look the same regardless of how it's encoded
  bjvm_obj_header *str_data = RawStringData(thread, args[0].handle->obj);
  char *chars = ArrayData(str_data);
  int len = *ArrayLength(str_data);

  if (len > 10) {
    return value_null();
  }

  bjvm_type_kind kind;
  if (strcmp(chars, "boolean") == 0) {
    kind = BJVM_TYPE_KIND_BOOLEAN;
  } else if (strcmp(chars, "byte") == 0) {
    kind = BJVM_TYPE_KIND_BYTE;
  } else if (strcmp(chars, "char") == 0) {
    kind = BJVM_TYPE_KIND_CHAR;
  } else if (strcmp(chars, "short") == 0) {
    kind = BJVM_TYPE_KIND_SHORT;
  } else if (strcmp(chars, "int") == 0) {
    kind = BJVM_TYPE_KIND_INT;
  } else if (strcmp(chars, "long") == 0) {
    kind = BJVM_TYPE_KIND_LONG;
  } else if (strcmp(chars, "float") == 0) {
    kind = BJVM_TYPE_KIND_FLOAT;
  } else if (strcmp(chars, "double") == 0) {
    kind = BJVM_TYPE_KIND_DOUBLE;
  } else if (strcmp(chars, "void") == 0) {
    kind = BJVM_TYPE_KIND_VOID;
  } else {
    return value_null();
  }

  return (bjvm_stack_value){.obj = (void *)bjvm_primitive_class_mirror(thread, kind)};
}

DECLARE_NATIVE("java/lang", Class, getEnclosingMethod0, "()[Ljava/lang/Object;") {
  // "The array is expected to have three elements: the immediately enclosing
  // class, the immediately enclosing method or constructor's name (can be
  // null). the immediately enclosing method or constructor's descriptor (null
  // iff name is).
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  // Search the class attributes for an EnclosingMethod attribute
  bjvm_attribute *attr = find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  bjvm_attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  bjvm_handle *array = bjvm_make_handle(thread,
    CreateObjectArray1D(thread, bootstrap_lookup_class(thread, STR("java/lang/Object")), 3));
#define data ((bjvm_obj_header **)ArrayData(array->obj))

  int error = bjvm_resolve_class(thread, enclosing_method.class_info);
  CHECK(!error);
  data[0] = (void *)enclosing_method.class_info->classdesc->mirror;
  if (enclosing_method.nat != nullptr) {
    data[1] = MakeJStringFromModifiedUTF8(thread, enclosing_method.nat->name, true);
    data[2] = MakeJStringFromModifiedUTF8(thread, enclosing_method.nat->descriptor, true);
  }
#undef data
  bjvm_stack_value result = (bjvm_stack_value) { .obj = array->obj };
  bjvm_drop_handle(thread, array);
  return result;
}

DECLARE_NATIVE("java/lang", Class, getDeclaringClass0, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_attribute *attr = find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  bjvm_attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  int error = bjvm_resolve_class(thread, enclosing_method.class_info);
  CHECK(!error);
  return (bjvm_stack_value){.obj = (void *)enclosing_method.class_info->classdesc->mirror};
}

DECLARE_NATIVE("java/lang", Class, getComponentType, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  if (desc->kind == BJVM_CD_KIND_ORDINARY || desc->kind == BJVM_CD_KIND_PRIMITIVE) {
    // ints and ordinary objects have no components
    return value_null();
  }
  void *result = bjvm_get_class_mirror(thread, desc->one_fewer_dim);
  DCHECK(result);
  return (bjvm_stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, getModifiers, "()I") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj->obj);
  return (bjvm_stack_value){.i = classdesc->access_flags};
}

DECLARE_NATIVE("java/lang", Class, getSuperclass, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_cp_class_info *super = desc->super_class;
  if (!super || desc->access_flags & BJVM_ACCESS_INTERFACE)
    return value_null();
  return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, super->classdesc)};
}

DECLARE_NATIVE("java/lang", Class, getClassLoader, "()Ljava/lang/ClassLoader;") {
  printf("TODO: getClassLoader\n");
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Class, getPermittedSubclasses0, "()[Ljava/lang/Class;") {
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Class, initClassName, "()Ljava/lang/String;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj->obj);
  INIT_STACK_STRING(name, 1000);
  bprintf(name, "%.*s", fmt_slice(classdesc->name));
  for (u32 i = 0; i < classdesc->name.len; ++i) {
    name.chars[i] = name.chars[i] == '/' ? '.' : name.chars[i];
  }
  name.len = classdesc->name.len;
  return (bjvm_stack_value){.obj = MakeJStringFromModifiedUTF8(thread, name, true)};
}

DECLARE_NATIVE("java/lang", Class, forName0,
               "(Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/"
               "Class;)Ljava/lang/Class;") {
  // Read args[0] as a string
  bjvm_obj_header *name_obj = args[0].handle->obj;

  heap_string name_str = AsHeapString(name_obj, oom);
  for (size_t i = 0; i < name_str.len; ++i) {
    name_str.chars[i] = name_str.chars[i] == '.' ? '/' : name_str.chars[i];
  }
  bjvm_classdesc *c = bootstrap_lookup_class(thread, hslc(name_str));

  if (c && args[1].i) {
    bjvm_initialize_class_t ctx = {.args = {thread, c}};
    future_t f = bjvm_initialize_class(&ctx);
    CHECK(f.status == FUTURE_READY);

    if (ctx._result) {
      return value_null();
    }
  }

  free_heap_str(name_str);

  if (c) {
    return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, c)};
  }

  oom:
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, desiredAssertionStatus0, "(Ljava/lang/Class;)Z") {
  return (bjvm_stack_value){.i = 1}; // TODO add thread option
}

bool include_field(const bjvm_cp_field *field, bool public_only) {
  return !public_only || field->access_flags & BJVM_ACCESS_PUBLIC;
}

DECLARE_NATIVE("java/lang", Class, getDeclaredFields0, "(Z)[Ljava/lang/reflect/Field;") {
  bjvm_classdesc *class = bjvm_unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int fields = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->fields_count; ++i) {
    bjvm_cp_field *field = class->fields + i;
    if (include_field(field, public_only)) {
      bjvm_reflect_initialize_field(thread, class, field);
      ++fields;
    }
  }
  bjvm_classdesc *Field = thread->vm->cached_classdescs->field;
  bjvm_obj_header *result = CreateObjectArray1D(thread, Field, fields);
  if (!result)
    return value_null();
  struct bjvm_native_Field **data = ArrayData(result);
  for (int i = 0, j = 0; i < class->fields_count; ++i) {
    bjvm_cp_field *field = class->fields + i;
    if (include_field(field, public_only))
      data[j++] = field->reflection_field;
  }
  return (bjvm_stack_value){.obj = result};
}

bool include_ctor(const bjvm_cp_method *method, bool public_only) {
  return method->is_ctor && (!public_only || method->access_flags & BJVM_ACCESS_PUBLIC);
}

// Get a list of all constructors on a class, optionally filtering by
// public_only
DECLARE_NATIVE("java/lang", Class, getDeclaredConstructors0, "(Z)[Ljava/lang/reflect/Constructor;") {
  bjvm_classdesc *class = bjvm_unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int ctors = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->methods_count; ++i) {
    bjvm_cp_method *method = class->methods + i;
    if (include_ctor(method, public_only)) {
      bjvm_reflect_initialize_constructor(thread, class, method);
      ++ctors;
    }
  }
  // Then create the array
  bjvm_classdesc *Ctor = bootstrap_lookup_class(thread, STR("java/lang/reflect/Constructor"));
  bjvm_obj_header *result = CreateObjectArray1D(thread, Ctor, ctors);
  struct bjvm_native_Constructor **data = ArrayData(result);
  int j = 0;
  for (int i = 0; i < class->methods_count; ++i) {
    bjvm_cp_method *method = class->methods + i;
    if (include_ctor(method, public_only))
      data[j++] = method->reflection_ctor;
  }
  return (bjvm_stack_value){.obj = result};
}

bool include_method(const bjvm_cp_method *method, bool public_only) {
  return !method->is_ctor && !method->is_clinit &&
         (!public_only || method->access_flags & BJVM_ACCESS_PUBLIC);
}

// Get a list of all methods on a class, optionally filtering by public_only
DECLARE_NATIVE("java/lang", Class, getDeclaredMethods0, "(Z)[Ljava/lang/reflect/Method;") {
  DCHECK(argc == 1);
  bjvm_classdesc *class = bjvm_unmirror_class(obj->obj);
  bool public_only = args[0].i;
  int methods = 0;
  // First initialize the reflection objects
  for (int i = 0; i < class->methods_count; ++i) {
    bjvm_cp_method *method = class->methods + i;
    if (include_method(method, public_only)) {
      bjvm_reflect_initialize_method(thread, class, method);
      ++methods;
    }
  }
  // Then create the array
  bjvm_classdesc *Method = bootstrap_lookup_class(thread, STR("java/lang/reflect/Method"));
  bjvm_link_class(thread, Method);
  bjvm_obj_header *result = CreateObjectArray1D(thread, Method, methods);
  struct bjvm_native_Method **data = ArrayData(result);
  for (int i = 0, j = 0; i < class->methods_count; ++i) {
    bjvm_cp_method *method = class->methods + i;
    if (include_method(method, public_only))
      data[j++] = method->reflection_method;
  }
  return (bjvm_stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, getDeclaredClasses0, "()[Ljava/lang/Class;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj->obj);
  (void)classdesc;

  printf("Calling incompletely implemented getDeclaredClasses0\n");

  int count = 0;
  bjvm_stack_value ret;
  ret.obj = CreateObjectArray1D(thread, bootstrap_lookup_class(thread, STR("java/lang/Class")), count);
  // TODO parse inner classes and return them here
  return ret;
}

DECLARE_NATIVE("java/lang", Class, isPrimitive, "()Z") {
  return (bjvm_stack_value){.i = bjvm_unmirror_class(obj->obj)->kind == BJVM_CD_KIND_PRIMITIVE};
}

DECLARE_NATIVE("java/lang", Class, isInterface, "()Z") {
  return (bjvm_stack_value){.i = !!(bjvm_unmirror_class(obj->obj)->access_flags & BJVM_ACCESS_INTERFACE)};
}

DECLARE_NATIVE("java/lang", Class, isAssignableFrom, "(Ljava/lang/Class;)Z") {
  bjvm_classdesc *this_desc = bjvm_unmirror_class(obj->obj);
  if (!args[0].handle->obj) {
    raise_null_pointer_exception(thread);
    return value_null();
  }
  bjvm_classdesc *other_desc = bjvm_unmirror_class(args[0].handle->obj);
  return (bjvm_stack_value){.i = bjvm_instanceof(other_desc, this_desc)};
}

// Returns false when passed object is null
DECLARE_NATIVE("java/lang", Class, isInstance, "(Ljava/lang/Object;)Z") {
  bjvm_classdesc *this_desc = bjvm_unmirror_class(obj->obj);
  int result = 0;
  if (args[0].handle)
    result = bjvm_instanceof(args[0].handle->obj->descriptor, this_desc);
  return (bjvm_stack_value){.i = result};
}

DECLARE_NATIVE("java/lang", Class, isArray, "()Z") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  return (bjvm_stack_value){.i = desc->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
                                 desc->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY};
}

DECLARE_NATIVE("java/lang", Class, isHidden, "()Z") { // TODO
  return (bjvm_stack_value){.i = 0};
}

DECLARE_NATIVE("java/lang", Class, getNestHost0, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_cp_class_info *nest_host = desc->nest_host;
  if (!nest_host || !bjvm_resolve_class(thread, nest_host)) {
    return value_null();
  }

  return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, nest_host->classdesc)};
}

DECLARE_NATIVE("java/lang", Class, getConstantPool, "()Ljdk/internal/reflect/ConstantPool;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  return (bjvm_stack_value){.obj = (void *)bjvm_get_constant_pool_mirror(thread, desc)};
}

DECLARE_NATIVE("java/lang", Class, getRawAnnotations, "()[B") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_attribute *attr = find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS);
  if (attr) {
    bjvm_attribute_runtime_visible_annotations r = attr->annotations;
    bjvm_obj_header *array = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, r.length);
    memcpy(ArrayData(array), r.data, r.length);
    return (bjvm_stack_value){.obj = array};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getRawTypeAnnotations, "()[B") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_attribute *attr = find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS);
  if (attr) {
    bjvm_attribute_runtime_visible_type_annotations r = attr->type_annotations;
    bjvm_obj_header *array = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, r.length);
    memcpy(ArrayData(array), r.data, r.length);
    return (bjvm_stack_value){.obj = array};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getInterfaces0, "()[Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_handle *array =
      bjvm_make_handle(thread, CreateObjectArray1D(thread, bootstrap_lookup_class(thread, STR("java/lang/Class")),
                                                   desc->interfaces_count));

  for (int i = 0; i < desc->interfaces_count; ++i) {
    bjvm_cp_class_info *info = desc->interfaces[i];
    bjvm_classdesc *iface = info->classdesc;
    bjvm_obj_header *mirror = (void *)bjvm_get_class_mirror(thread, iface);
    *((bjvm_obj_header **)ArrayData(array->obj) + i) = mirror;
  }

  return (bjvm_stack_value){.obj = array->obj};
}

DECLARE_NATIVE("java/lang", Class, getGenericSignature0, "()Ljava/lang/String;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj->obj);
  bjvm_attribute *attr = find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_SIGNATURE);
  if (attr) {
    return (bjvm_stack_value){.obj = MakeJStringFromModifiedUTF8(thread, attr->signature.utf8, true)};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getProtectionDomain0, "()Ljava/security/ProtectionDomain;") {
  return value_null(); // TODO
}
