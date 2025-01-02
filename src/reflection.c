// VM instantiations of java.lang.reflect.*

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

bjvm_utf8 bjvm_unparse_field_descriptor(bjvm_utf8 str,
                                        const bjvm_field_descriptor *desc) {
  bjvm_utf8 write = str;
  // Print '[' repeatedly
  int dims = desc->dimensions;
  while (dims--) {
    write.chars[0] = '[';
    write = slice(write, 1);
  }
  if (desc->base_kind == BJVM_TYPE_KIND_REFERENCE) {
    write =
        slice(write, bprintf(write, "L%.*s;", fmt_slice(desc->class_name)).len);
  } else {
    write = slice(write, bprintf(write, "%c", desc->base_kind).len);
  }
  str.len = write.chars - str.chars;
  return str;
}

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field) {

  bjvm_classdesc *reflect_Field =
      bootstrap_class_create(thread, STR("java/lang/reflect/Field"));
  bjvm_initialize_class(thread, reflect_Field);
  bjvm_handle *field_mirror =
      bjvm_make_handle(thread, new_object(thread, reflect_Field));
  if (!field_mirror->obj) { // out of memory
    return;
  }

#define F ((struct bjvm_native_Field *)field_mirror->obj)
  field->reflection_field = (void *)F;

  F->reflected_field = field;
  F->name = bjvm_intern_string(thread, field->name);
  F->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  F->type = (void *)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, field->descriptor));
  F->modifiers = field->access_flags;

  // Find runtimevisibleannotations attribute
  for (int i = 0; i < field->attributes_count; ++i) {
    if (field->attributes[i].kind ==
        BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS) {
      const bjvm_attribute_runtime_visible_annotations a =
          field->attributes[i].annotations;
      F->annotations =
          CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, a.length, true);
      memcpy(ArrayData(F->annotations), a.data, field->attributes[i].length);
      break;
    }
  }
#undef F

  bjvm_drop_handle(thread, field_mirror);
}

void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method) {
  assert(utf8_equals(method->name, "<init>"));

  bjvm_classdesc *reflect_Constructor =
      bootstrap_class_create(thread, STR("java/lang/reflect/Constructor"));
  bjvm_initialize_class(thread, reflect_Constructor);

  method->reflection_ctor = (void *)new_object(thread, reflect_Constructor);

  bjvm_handle *result =
      bjvm_make_handle(thread, (void *)method->reflection_ctor);
#define C ((struct bjvm_native_Constructor *)result->obj)
  C->reflected_ctor = method;
  C->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  C->modifiers = method->access_flags;
  C->parameterTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")),
      method->descriptor->args_count, true);

  for (int i = 0; i < method->descriptor->args_count; ++i) {
    INIT_STACK_STRING(desc, 1000);
    desc = bjvm_unparse_field_descriptor(desc, &method->descriptor->args[i]);
    struct bjvm_native_Class *type = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
    ((struct bjvm_native_Class **)ArrayData(C->parameterTypes))[i] = type;
  }

#undef C
}

void bjvm_reflect_initialize_method(bjvm_thread *thread,
                                    bjvm_classdesc *classdesc,
                                    bjvm_cp_method *method) {
  bjvm_classdesc *reflect_Method =
      bootstrap_class_create(thread, STR("java/lang/reflect/Method"));
  bjvm_initialize_class(thread, reflect_Method);

  bjvm_handle *result =
      bjvm_make_handle(thread, new_object(thread, reflect_Method));

#define M ((struct bjvm_native_Method *)result->obj)

  M->reflected_method = method;
  M->modifiers = method->access_flags;

  if (!((M->name = bjvm_intern_string(thread, method->name))))
    goto oom;
  if (!((M->clazz = (void *)bjvm_get_class_mirror(thread, classdesc))))
    goto oom;
  if (!((M->signature = make_string(thread, method->unparsed_descriptor))))
    goto oom;

  for (int i = 0; i < method->attributes_count; ++i) {
    const bjvm_attribute *attr = method->attributes + i;
    switch (attr->kind) {
    case BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS:
      if (!((M->annotations = CreateByteArray(thread, attr->annotations.data,
                                              attr->annotations.length))))
        goto oom;
    case BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS:
      if (!((M->parameterAnnotations =
                 CreateByteArray(thread, attr->parameter_annotations.data,
                                 attr->parameter_annotations.length))))
        goto oom;
    case BJVM_ATTRIBUTE_KIND_ANNOTATION_DEFAULT:
      if (!((M->annotationDefault =
                 CreateByteArray(thread, attr->annotation_default.data,
                                 attr->annotation_default.length))))
        goto oom;
    default:
      break;
    }
  }

  M->parameterTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")),
      method->descriptor->args_count, true);
  INIT_STACK_STRING(str, 1000);
  for (int i = 0; i < method->descriptor->args_count; ++i) {
    bjvm_utf8 desc =
        bjvm_unparse_field_descriptor(str, &method->descriptor->args[i]);
    ((void **)ArrayData(M->parameterTypes))[i] = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
  }

  bjvm_utf8 ret_desc =
      bjvm_unparse_field_descriptor(str, &method->descriptor->return_type);
  M->returnType = (void *)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, ret_desc));
  M->exceptionTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")), 0, true);
  // TODO parse these ^^

  method->reflection_method = (void *)M;

oom: // OOM while creating the Method
  bjvm_drop_handle(thread, result);
}