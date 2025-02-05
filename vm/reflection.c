// VM instantiations of java.lang.reflect.*

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

#include <cached_classdescs.h>
#include <reflection.h>

slice bjvm_unparse_field_descriptor(slice str,
                                        const bjvm_field_descriptor *desc) {
  slice write = str;
  // Print '[' repeatedly
  int dims = desc->dimensions;
  while (dims--) {
    write.chars[0] = '[';
    write = subslice(write, 1);
  }
  if (desc->base_kind == BJVM_TYPE_KIND_REFERENCE) {
    write =
        subslice(write, bprintf(write, "L%.*s;", fmt_slice(desc->class_name)).len);
  } else {
    write = subslice(write, bprintf(write, "%c", desc->base_kind).len);
  }
  str.len = write.chars - str.chars;
  return str;
}

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field) {

  bjvm_classdesc *reflect_Field = thread->vm->cached_classdescs->field;
  bjvm_handle *field_mirror =
      bjvm_make_handle(thread, new_object(thread, reflect_Field));
  if (!field_mirror->obj) { // out of memory
    return;
  }

#define F ((struct bjvm_native_Field *)field_mirror->obj)
  field->reflection_field = (void *)F;

  F->reflected_field = field;
  F->name = MakeJStringFromModifiedUTF8(thread, field->name, true);
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
          CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, a.length);
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
  assert(method->is_ctor && "Method is not a constructor");
  bjvm_classdesc *reflect_Constructor = thread->vm->cached_classdescs->constructor;

  method->reflection_ctor = (void *)new_object(thread, reflect_Constructor);

  bjvm_handle *result =
      bjvm_make_handle(thread, (void *)method->reflection_ctor);
#define C ((struct bjvm_native_Constructor *)result->obj)
  C->reflected_ctor = method;
  C->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  C->modifiers = method->access_flags;
  C->parameterTypes = CreateObjectArray1D(
      thread, thread->vm->cached_classdescs->klass,
      method->descriptor->args_count);

  for (int i = 0; i < method->descriptor->args_count; ++i) {
    INIT_STACK_STRING(desc, 1000);
    desc = bjvm_unparse_field_descriptor(desc, &method->descriptor->args[i]);
    struct bjvm_native_Class *type = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
    ((struct bjvm_native_Class **)ArrayData(C->parameterTypes))[i] = type;
  }

#undef C
  bjvm_drop_handle(thread, result);
}

void bjvm_reflect_initialize_method(bjvm_thread *thread,
                                    bjvm_classdesc *classdesc,
                                    bjvm_cp_method *method) {
  assert(!method->is_ctor && !method->is_clinit && "Method is a constructor or <clinit>");
  bjvm_classdesc *reflect_Method = thread->vm->cached_classdescs->method;

  bjvm_handle *result =
      bjvm_make_handle(thread, new_object(thread, reflect_Method));

#define M ((struct bjvm_native_Method *)result->obj)

  M->reflected_method = method;
  M->modifiers = method->access_flags;

  if (!((M->name = MakeJStringFromModifiedUTF8(thread, method->name, true))))
    goto oom;
  if (!((M->clazz = (void *)bjvm_get_class_mirror(thread, classdesc))))
    goto oom;
  if (!((M->signature = MakeJStringFromModifiedUTF8(thread, method->unparsed_descriptor, false))))
    goto oom;

  for (int i = 0; i < method->attributes_count; ++i) {
    const bjvm_attribute *attr = method->attributes + i;
    switch (attr->kind) {
    case BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS:
      if (!((M->annotations = CreateByteArray(thread, attr->annotations.data,
                                              attr->annotations.length))))
        goto oom;
      break;
    case BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS:
      if (!((M->parameterAnnotations =
                 CreateByteArray(thread, attr->parameter_annotations.data,
                                 attr->parameter_annotations.length))))
        goto oom;
      break;
    case BJVM_ATTRIBUTE_KIND_ANNOTATION_DEFAULT:
      if (!((M->annotationDefault =
                 CreateByteArray(thread, attr->annotation_default.data,
                                 attr->annotation_default.length))))
        goto oom;
      break;
    default:
      break;
    }
  }

  M->parameterTypes = CreateObjectArray1D(
      thread, bootstrap_lookup_class(thread, STR("java/lang/Class")),
      method->descriptor->args_count);
  INIT_STACK_STRING(str, 1000);
  for (int i = 0; i < method->descriptor->args_count; ++i) {
    slice desc =
        bjvm_unparse_field_descriptor(str, &method->descriptor->args[i]);
    ((void **)ArrayData(M->parameterTypes))[i] = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
  }

  slice ret_desc =
      bjvm_unparse_field_descriptor(str, &method->descriptor->return_type);
  M->returnType = (void *)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, ret_desc));
  M->exceptionTypes = CreateObjectArray1D(
      thread, bootstrap_lookup_class(thread, STR("java/lang/Class")), 0);
  // TODO parse these ^^

  method->reflection_method = (void *)M;

oom: // OOM while creating the Method
  bjvm_drop_handle(thread, result);
}

static bjvm_obj_header * get_method_parameters_impl(bjvm_thread * thread, bjvm_cp_method * method, bjvm_attribute_method_parameters mparams) {
  bjvm_classdesc *Parameter = thread->vm->cached_classdescs->parameter;
  bjvm_handle *params = bjvm_make_handle(thread, CreateObjectArray1D(thread, Parameter, mparams.count));
  if (!params->obj)
    return nullptr;

  bjvm_handle *parameter = nullptr;
  bjvm_obj_header *result = nullptr;
  for (int j = 0; j < mparams.count; ++j) {
    bjvm_drop_handle(thread, parameter);
    parameter = bjvm_make_handle(thread, new_object(thread, Parameter));

#define P ((struct bjvm_native_Parameter *)parameter->obj)

    if (!P)
      goto oom;
    P->name = MakeJStringFromModifiedUTF8(thread, mparams.params[j].name, true);
    if (!P->name)
      goto oom;
    P->executable = method->reflection_method ? (void*)method->reflection_method : (void*)method->reflection_ctor;
    assert(P->executable);  // we should have already initialised the method

    P->index = j;
    P->modifiers = mparams.params[j].access_flags;

    ReferenceArrayStore(params->obj, j, parameter->obj);
  }

  result = params->obj;  // Success!

  oom:
  bjvm_drop_handle(thread, params);
  bjvm_drop_handle(thread, parameter);
  return result;
}

bjvm_obj_header *bjvm_reflect_get_method_parameters(bjvm_thread *thread, bjvm_cp_method *method) {
  for (int i = 0; i < method->attributes_count; ++i) {
    const bjvm_attribute *attr = method->attributes + i;
    if (attr->kind == BJVM_ATTRIBUTE_KIND_METHOD_PARAMETERS) {
      bjvm_attribute_method_parameters mparams = attr->method_parameters;
      return get_method_parameters_impl(thread, method, mparams);
    }
  }
  return nullptr;  // none to be found :(
}