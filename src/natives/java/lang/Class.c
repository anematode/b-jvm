#include <natives.h>

static bjvm_attribute *find_attribute_by_kind(bjvm_classdesc *desc,
                                              bjvm_attribute_kind kind) {
  for (int i = 0; i < desc->attributes_count; ++i) {
    if (desc->attributes[i].kind == kind) {
      return desc->attributes + i;
    }
  }
  return nullptr;
}

DECLARE_NATIVE("java/lang", Class, registerNatives, "()V") {
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, getPrimitiveClass,
               "(Ljava/lang/String;)Ljava/lang/Class;") {
  assert(argc == 1);
  short *chars;
  size_t len;
  read_string(thread, args[0].obj, &chars, &len);
  if (len > 10) {
    return value_null();
  }
  char as_cstr[11] = {0};
  for (size_t i = 0; i < len; ++i) {
    as_cstr[i] = chars[i];
  }
  bjvm_type_kind kind;
  if (strcmp(as_cstr, "boolean") == 0) {
    kind = BJVM_TYPE_KIND_BOOLEAN;
  } else if (strcmp(as_cstr, "byte") == 0) {
    kind = BJVM_TYPE_KIND_BYTE;
  } else if (strcmp(as_cstr, "char") == 0) {
    kind = BJVM_TYPE_KIND_CHAR;
  } else if (strcmp(as_cstr, "short") == 0) {
    kind = BJVM_TYPE_KIND_SHORT;
  } else if (strcmp(as_cstr, "int") == 0) {
    kind = BJVM_TYPE_KIND_INT;
  } else if (strcmp(as_cstr, "long") == 0) {
    kind = BJVM_TYPE_KIND_LONG;
  } else if (strcmp(as_cstr, "float") == 0) {
    kind = BJVM_TYPE_KIND_FLOAT;
  } else if (strcmp(as_cstr, "double") == 0) {
    kind = BJVM_TYPE_KIND_DOUBLE;
  } else if (strcmp(as_cstr, "void") == 0) {
    kind = BJVM_TYPE_KIND_VOID;
  } else {
    return value_null();
  }

  return (bjvm_stack_value){
      .obj = (void *)bjvm_primitive_class_mirror(thread, kind)};
}

DECLARE_NATIVE("java/lang", Class, getEnclosingMethod0,
               "()[Ljava/lang/Object;") {
  // "The array is expected to have three elements: the immediately enclosing
  // class, the immediately enclosing method or constructor's name (can be
  // null). the immediately enclosing method or constructor's descriptor (null
  // iff name is).
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  // Search the class attributes for an EnclosingMethod attribute
  bjvm_attribute *attr =
      find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  bjvm_attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  bjvm_obj_header *array = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Object")), 3);
  bjvm_obj_header **data = ArrayData(array);
  int error = bjvm_resolve_class(thread, enclosing_method.class_info);
  assert(!error);
  data[0] = (void *)enclosing_method.class_info->classdesc->mirror;
  if (enclosing_method.nat != nullptr) {
    data[1] = bjvm_intern_string(thread, enclosing_method.nat->name);
    data[2] = bjvm_intern_string(thread, enclosing_method.nat->descriptor);
  }
  return (bjvm_stack_value){.obj = array};
}

DECLARE_NATIVE("java/lang", Class, getDeclaringClass0, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  bjvm_attribute *attr =
      find_attribute_by_kind(desc, BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD);
  if (!attr) {
    return value_null();
  }
  bjvm_attribute_enclosing_method enclosing_method = attr->enclosing_method;
  if (!enclosing_method.class_info) {
    return value_null();
  }
  int error = bjvm_resolve_class(thread, enclosing_method.class_info);
  assert(!error);
  return (bjvm_stack_value){
      .obj = (void *)enclosing_method.class_info->classdesc->mirror};
}

DECLARE_NATIVE("java/lang", Class, getComponentType, "()Ljava/lang/Class;") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  if (desc->kind == BJVM_CD_KIND_ORDINARY ||
      desc->kind == BJVM_CD_KIND_PRIMITIVE) {
    // ints and ordinary objects have no components
    return value_null();
  }
  void *result = bjvm_get_class_mirror(thread, desc->one_fewer_dim);
  assert(result);
  return (bjvm_stack_value){.obj = result};
}

DECLARE_NATIVE("java/lang", Class, getModifiers, "()I") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return (bjvm_stack_value){.i = classdesc->access_flags};
}

DECLARE_NATIVE("java/lang", Class, getSuperclass, "()Ljava/lang/Class;") {
  bjvm_cp_class_info *super = bjvm_unmirror_class(obj)->super_class;
  if (!super)
    return value_null();
  return (bjvm_stack_value) { .obj = (void*)bjvm_get_class_mirror(thread, super->classdesc) };
}

DECLARE_NATIVE("java/lang", Class, getClassLoader,
               "()Ljava/lang/ClassLoader;") {
  printf("TODO: getClassLoader\n");
  return value_null(); // TODO
}

DECLARE_NATIVE("java/lang", Class, getName0, "()Ljava/lang/String;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  INIT_STACK_STRING(name, 1000);
  bprintf(name, "%.*s", fmt_slice(classdesc->name));
  for (int i = 0; i < classdesc->name.len; ++i) {
    name.chars[i] = name.chars[i] == '/' ? '.' : name.chars[i];
  }
  name.len = classdesc->name.len;
  return (bjvm_stack_value){.obj = bjvm_intern_string(thread, name)};
}

DECLARE_NATIVE("java/lang", Class, forName0,
               "(Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/"
               "Class;)Ljava/lang/Class;") {
  // Read args[0] as a string
  bjvm_obj_header *name_obj = args[0].obj;
  short *name;
  size_t len;
  read_string(thread, name_obj, &name, &len);

  heap_string name_str = make_heap_str(len);
  for (size_t i = 0; i < len; ++i) {
    name_str.chars[i] = name[i] == '.' ? '/' : name[i];
  }
  bjvm_classdesc *c = bootstrap_class_create(thread, hslc(name_str));
  free_heap_str(name_str);
  if (c) {
    return (bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, c)};
  }
  return value_null();
}

DECLARE_NATIVE("java/lang", Class, desiredAssertionStatus0,
               "(Ljava/lang/Class;)Z") {
  return (bjvm_stack_value){.i = 1}; // TODO add thread option
}

DECLARE_NATIVE("java/lang", Class, getDeclaredFields0,
               "(Z)[Ljava/lang/reflect/Field;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  bjvm_stack_value ret;
  ret.obj = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/reflect/Field")),
      classdesc->fields_count);

  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_reflect_initialize_field(thread, classdesc, classdesc->fields + i);
    *((struct bjvm_native_Field **)ArrayData(ret.obj) + i) =
        classdesc->fields[i].reflection_field;
  }
  return ret;
}

DECLARE_NATIVE("java/lang", Class, getDeclaredConstructors0,
               "(Z)[Ljava/lang/reflect/Constructor;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);

  int count = 0;
  for (int i = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      bjvm_reflect_initialize_constructor(thread, classdesc,
                                          classdesc->methods + i);
      ++count;
    }
  }

  bjvm_stack_value ret;
  ret.obj = CreateObjectArray1D(
      thread,
      bootstrap_class_create(thread, STR("java/lang/reflect/Constructor")),
      count);
  for (int i = 0, j = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      *((struct bjvm_native_Constructor **)ArrayData(ret.obj) + j++) =
          classdesc->methods[i].reflection_ctor;
    }
  }
  return ret;
}

DECLARE_NATIVE("java/lang", Class, getDeclaredMethods0,
               "(Z)[Ljava/lang/reflect/Method;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);

  int count = 0;
  for (int i = 0; i < classdesc->methods_count; ++i) {
    if (!utf8_equals(classdesc->methods[i].name, "<init>")) {
      bjvm_reflect_initialize_method(thread, classdesc, classdesc->methods + i);
      ++count;
    }
  }

  bjvm_stack_value ret;
  ret.obj = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/reflect/Method")),
      count);
  for (int i = 0, j = 0; i < classdesc->methods_count; ++i) {
    if (!utf8_equals(classdesc->methods[i].name, "<init>")) {
      *((struct bjvm_native_Method **)ArrayData(ret.obj) + j++) =
          classdesc->methods[i].reflection_method;
    }
  }
  return ret;
}

DECLARE_NATIVE("java/lang", Class, getDeclaredClasses0,
               "()[Ljava/lang/Class;") {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);

  printf("Calling incompletely implemented getDeclaredClasses0\n");

  int count = 0;
  bjvm_stack_value ret;
  ret.obj = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")),
      count);
  // TODO parse inner classes and return them here
  return ret;
}

DECLARE_NATIVE("java/lang", Class, isPrimitive, "()Z") {
  return (bjvm_stack_value){.i = bjvm_unmirror_class(obj)->kind ==
                                 BJVM_CD_KIND_PRIMITIVE};
}

DECLARE_NATIVE("java/lang", Class, isInterface, "()Z") {
  return (bjvm_stack_value){.i = bjvm_unmirror_class(obj)->access_flags &
                                 BJVM_ACCESS_INTERFACE};
}

DECLARE_NATIVE("java/lang", Class, isAssignableFrom, "(Ljava/lang/Class;)Z") {
  bjvm_classdesc *this_desc = bjvm_unmirror_class(obj);
  if (!args[0].obj)
    UNREACHABLE(); // TODO check reference for what to do here
  bjvm_classdesc *other_desc = bjvm_unmirror_class(args[0].obj);
  return (bjvm_stack_value){.i = bjvm_instanceof(other_desc, this_desc)};
}

DECLARE_NATIVE("java/lang", Class, isArray, "()Z") {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  return (bjvm_stack_value){.i = desc->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
                                 desc->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY};
}
