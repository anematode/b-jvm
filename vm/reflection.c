// VM instantiations of java.lang.reflect.*

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

#include <cached_classdescs.h>
#include <reflection.h>

DEFINE_ASYNC(reflect_initialize_field) {
#define thread (self->args.thread)
#define cd (self->args.cd)
#define f (self->args.field)

  classdesc *reflect_Field = cached_classes(thread->vm)->field;
  self->field_mirror = make_handle(thread, new_object(thread, reflect_Field));
  if (!self->field_mirror->obj) { // out of memory
    ASYNC_RETURN_VOID();
  }

#define F ((struct native_Field *)self->field_mirror->obj)
  f->reflection_field = (void *)F;

  F->reflected_field = f;
  object name = MakeJStringFromModifiedUTF8(thread, f->name, true);
  if (!name)
    goto oom;
  F->name = name;
  object mirror = (void *)get_class_mirror(thread, cd);
  if (!mirror)
    goto oom;
  F->clazz = mirror;
  AWAIT(load_class_of_field_descriptor, thread, f->my_class->classloader, f->descriptor);
  mirror = (void *)get_class_mirror(thread, get_async_result(load_class_of_field_descriptor));
  F->type = mirror;
  F->modifiers = f->access_flags;

  // Find runtimevisibleannotations attribute and signature attribute
  for (int i = 0; i < f->attributes_count; ++i) {
    if (f->attributes[i].kind == ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS) {
      const attribute_runtime_visible_annotations a = f->attributes[i].annotations;
      object annotations = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, a.length);
      F->annotations = annotations;
      memcpy(ArrayData(F->annotations), a.data, f->attributes[i].length);
    } else if (f->attributes[i].kind == ATTRIBUTE_KIND_SIGNATURE) {
      const attribute_signature a = f->attributes[i].signature;
      object signature = MakeJStringFromModifiedUTF8(thread, a.utf8, true);
      F->signature = signature;
    }
  }
#undef F

oom:
  drop_handle(thread, self->field_mirror);

#undef thread
#undef cd
#undef f
  ASYNC_END_VOID()
}

DEFINE_ASYNC(reflect_initialize_constructor) {
#define method (self->args.method)
#define thread (self->args.thread)
#define cd (self->args.cd)

  DCHECK(method->is_ctor, "Method is not a constructor");
  classdesc *reflect_Constructor = cached_classes(thread->vm)->constructor;

  method->reflection_ctor = (void *)new_object(thread, reflect_Constructor);

  handle *result = make_handle(thread, (void *)method->reflection_ctor);
#define C ((struct native_Constructor *)result->obj)
  C->reflected_ctor = method;
  object mirror = (void *)get_class_mirror(thread, cd);
  C->clazz = mirror;
  C->modifiers = method->access_flags;
  object parameterTypes =
      CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, method->descriptor->args_count);
  C->parameterTypes = parameterTypes;
  object exceptionTypes = CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, 0);
  C->exceptionTypes = exceptionTypes;
  C->slot = (s32)method->my_index;
  // TODO parse these ^^

  for (int i = 0; i < method->descriptor->args_count; ++i) {
    slice desc = method->descriptor->args[i].unparsed;
    AWAIT(load_class_of_field_descriptor, thread, method->my_class->classloader, desc);
    struct native_Class *type = (void *)get_class_mirror(thread, get_async_result(load_class_of_field_descriptor));
    ((struct native_Class **)ArrayData(C->parameterTypes))[i] = type;
  }

#undef C
  drop_handle(thread, result);

#undef method
#undef thread
#undef cd
  ASYNC_END_VOID()
}

DEFINE_ASYNC(reflect_initialize_method) {
  classdesc *reflect_Method = cached_classes(self->args.thread->vm)->method;

#define method (self->args.method)
#define thread (self->args.thread)
#define cd (self->args.cd)

  DCHECK(!method->is_ctor && !method->is_clinit, "Method is a constructor or <clinit>");

  self->result = make_handle(thread, new_object(thread, reflect_Method));

#define M ((struct native_Method *)self->result->obj)

  M->reflected_method = method;
  M->modifiers = method->access_flags;

  object name = MakeJStringFromModifiedUTF8(thread, method->name, true);
  if (!name)
    goto oom;
  M->name = name;
  object mirror = (void *)get_class_mirror(thread, cd);
  if (!mirror)
    goto oom;
  M->clazz = mirror;

  for (int i = 0; i < method->attributes_count; ++i) {
    const attribute *attr = method->attributes + i;
    object obj;
    if (attr->kind == ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS) {
      obj = CreateByteArray(thread, attr->annotations.data, attr->annotations.length);
      M->annotations = obj;
    } else if (attr->kind == ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS) {
      obj = CreateByteArray(thread, attr->parameter_annotations.data, attr->parameter_annotations.length);
      M->parameterAnnotations = obj;
    } else if (attr->kind == ATTRIBUTE_KIND_ANNOTATION_DEFAULT) {
      obj = CreateByteArray(thread, attr->annotation_default.data, attr->annotation_default.length);
      M->annotationDefault = obj;
    } else if (attr->kind == ATTRIBUTE_KIND_SIGNATURE) {
      obj = MakeJStringFromModifiedUTF8(thread, attr->signature.utf8, true);
      M->signature = obj;
    } else {
      continue;
    }
    if (!obj)
      goto oom;
  }

  object parameterTypes =
      CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, method->descriptor->args_count);
  if (!parameterTypes)
    goto oom;
  M->parameterTypes = parameterTypes;
  for (int i = 0; i < method->descriptor->args_count; ++i) {
    slice desc = method->descriptor->args[i].unparsed;
    AWAIT(load_class_of_field_descriptor, thread, method->my_class->classloader, desc);
    object mirror = (void *)get_class_mirror(thread, get_async_result(load_class_of_field_descriptor));
    if (!mirror)
      goto oom;
    ((void **)ArrayData(M->parameterTypes))[i] = mirror;
  }

  slice ret_desc = method->descriptor->return_type.unparsed;
  AWAIT(load_class_of_field_descriptor, thread, method->my_class->classloader, ret_desc);
  mirror = (void *)get_class_mirror(thread, get_async_result(load_class_of_field_descriptor));
  if (!mirror)
    goto oom;
  M->returnType = mirror;
  object exceptionTypes = CreateObjectArray1D(thread, cached_classes(thread->vm)->klass, 0);
  M->exceptionTypes = exceptionTypes;
  M->slot = (s32)method->my_index;
  // TODO parse these ^^

  method->reflection_method = (void *)M;

oom: // OOM while creating the Method
  drop_handle(thread, self->result);

#undef M
#undef thread
#undef method
#undef cd
  ASYNC_END_VOID()
}

static obj_header *get_method_parameters(vm_thread *thread, cp_method *method,
                                              attribute_method_parameters mparams) {
  classdesc *Parameter = cached_classes(thread->vm)->parameter;
  handle *params = make_handle(thread, CreateObjectArray1D(thread, Parameter, mparams.count));
  if (!params->obj)
    return nullptr;

  handle *parameter = nullptr;
  obj_header *result = nullptr;
  for (int j = 0; j < mparams.count; ++j) {
    drop_handle(thread, parameter);
    parameter = make_handle(thread, new_object(thread, Parameter));

#define P ((struct native_Parameter *)parameter->obj)

    if (!P)
      goto oom;
    object name = MakeJStringFromModifiedUTF8(thread, mparams.params[j].name, true);
    P->name = name;
    if (!P->name)
      goto oom;
    P->executable = method->reflection_method ? (void *)method->reflection_method : (void *)method->reflection_ctor;
    DCHECK(P->executable); // we should have already initialised the method

    P->index = j;
    P->modifiers = mparams.params[j].access_flags;

    ReferenceArrayStore(params->obj, j, parameter->obj);
  }

  result = params->obj; // Success!

oom:
  drop_handle(thread, params);
  drop_handle(thread, parameter);
  return result;
}

obj_header *reflect_get_method_parameters(vm_thread *thread, cp_method *method) {
  for (int i = 0; i < method->attributes_count; ++i) {
    const attribute *attr = method->attributes + i;
    if (attr->kind == ATTRIBUTE_KIND_METHOD_PARAMETERS) {
      return get_method_parameters(thread, method, attr->method_parameters);
    }
  }
  return nullptr; // none to be found :(
}