//
// Created by alec on 12/20/24.
//

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

#include <assert.h>
#include <linkage.h>
#include <stdlib.h>

// Called for both primitive and object arrays
static void fill_array_classdesc(vm_thread *thread, classdesc *base) {
  base->access_flags = ACCESS_PUBLIC | ACCESS_FINAL | ACCESS_ABSTRACT;

  cp_class_info *info = arena_alloc(&base->arena, 1, sizeof(cp_class_info));
  info->classdesc = bootstrap_lookup_class(thread, STR("java/lang/Object"));
  base->super_class = info;

  cp_class_info *Cloneable = arena_alloc(&base->arena, 2, sizeof(cp_class_info)), *Serializable = Cloneable + 1;
  Cloneable->classdesc = bootstrap_lookup_class(thread, STR("java/lang/Cloneable"));
  Cloneable->name = STR("java/lang/Cloneable");
  Serializable->classdesc = bootstrap_lookup_class(thread, STR("java/io/Serializable"));
  Serializable->name = STR("java/io/Serializable");

  base->interfaces_count = 2;
  base->interfaces = arena_alloc(&base->arena, 2, sizeof(cp_class_info *));
  base->interfaces[0] = Cloneable;
  base->interfaces[1] = Serializable;
}

// Make a class descriptor corresponding to an array of primitives.
static classdesc *primitive_array_classdesc(vm_thread *thread, classdesc *component_type) {
  classdesc *result = calloc(1, sizeof(classdesc));
  result->state = CD_STATE_INITIALIZED;
  result->kind = CD_KIND_PRIMITIVE_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component_type->dimensions + 1;
  result->one_fewer_dim = component_type;
  result->primitive_component = component_type->primitive_component;
  INIT_STACK_STRING(name, 3);
  name = bprintf(name, "[%c", type_kind_to_char(component_type->primitive_component));
  result->name = arena_make_str(&result->arena, name.chars, (int)name.len);
  setup_super_hierarchy(result);
  set_up_function_tables(result);
  pthread_mutex_init(&result->lock, nullptr); // todo: check return values?

  return result;
}

// Make a class descriptor corresponding to an array of objects.
static classdesc *ordinary_array_classdesc(vm_thread *thread, classdesc *component) {
  classdesc *result = calloc(1, sizeof(classdesc));
  result->kind = CD_KIND_ORDINARY_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component->dimensions + 1;
  result->one_fewer_dim = component;

  INIT_STACK_STRING(name, MAX_CF_NAME_LENGTH + 256);
  if (component->kind == CD_KIND_ORDINARY) {
    result->base_component = component;
    name = bprintf(name, "[L%.*s;", fmt_slice(component->name));
  } else {
    result->base_component = component->base_component;
    name = bprintf(name, "[%.*s", fmt_slice(component->name));
    DCHECK(result->dimensions == component->dimensions + 1);
  }
  result->name = arena_make_str(&result->arena, name.chars, (int)name.len);

  // propagate to n-D primitive arrays
  result->primitive_component = component->primitive_component;

  // linkage state of array class is same as component class
  result->state = CD_STATE_LOADED;
  if (component->state >= CD_STATE_LINKED) {
    link_class_impl_super_impl(thread, result); // we own the mutex to component too
  }
  result->state = component->state;
  pthread_mutex_init(&result->lock, nullptr); // todo: check return values?

  return result;
}

// Fill in the array_type class descriptor, corresponding to an array of the
// given component. For example, J -> [J, [[J -> [[[J, Object -> [Object
classdesc *get_or_create_array_classdesc(vm_thread *thread, classdesc *classdesc) {
  DCHECK(classdesc);
  pthread_mutex_lock(&classdesc->lock); // todo: check return values? todo: never do this reentrantly
  if (!classdesc->array_type) {
    if (classdesc->kind == CD_KIND_PRIMITIVE) {
      classdesc->array_type = primitive_array_classdesc(thread, classdesc);
    } else {
      classdesc->array_type = ordinary_array_classdesc(thread, classdesc);
    }
    classdesc->array_type->classloader = classdesc->classloader;
  }
  pthread_mutex_unlock(&classdesc->lock);
  return classdesc->array_type;
}

// Create a 1D primitive array of the given type and size.
static object create_1d_primitive_array(vm_thread *thread, type_kind array_type, int count) {
  DCHECK(count >= 0);

  int size = sizeof_type_kind(array_type);
  classdesc *array_desc = get_or_create_array_classdesc(thread, primitive_classdesc(thread, array_type));
  DCHECK(array_desc);

  size_t allocation_size = kArrayDataOffset + count * size;
  obj_header *array = AllocateObject(thread, array_desc, allocation_size);
  if (array) {
    *(int *)((char *)array + kArrayLengthOffset) = count;
    memset(ArrayData(array), 0, count * size); // zero initialize
    DCHECK(size_of_object(array) == allocation_size);
  }

  return array;
}

// Create a 1D object array of the given type and size.
obj_header *CreateObjectArray1D(vm_thread *thread, classdesc *cd, int count) {
  DCHECK(cd);
  DCHECK(count >= 0);

  classdesc *array_desc = get_or_create_array_classdesc(thread, cd);
  DCHECK(array_desc);

  size_t allocation_size = kArrayDataOffset + count * sizeof(object);
  obj_header *array = AllocateObject(thread, array_desc, allocation_size);
  if (array) {
    *(int *)((char *)array + kArrayLengthOffset) = count;
    memset(ArrayData(array), 0, count * sizeof(object));
    DCHECK(size_of_object(array) == allocation_size);
  }

  return array;
}

// Create a multi-dimensional array of the given type and dimensions. total_dimensions may equal 1, in which case
// a one-dimensional array is created. The class descriptor may be a primitive class descriptor.
obj_header *CreateArray(vm_thread *thread, classdesc *desc, int const *dim_sizes, int total_dimensions) {
  DCHECK(total_dimensions > 0);

  if (total_dimensions == 1) {
    switch (desc->kind) {
    case CD_KIND_PRIMITIVE_ARRAY:
      return create_1d_primitive_array(thread, desc->primitive_component, dim_sizes[0]);
    case CD_KIND_ORDINARY_ARRAY:
      return CreateObjectArray1D(thread, desc->one_fewer_dim, dim_sizes[0]);
    default:
      UNREACHABLE();
    }
  }

  auto arr = make_handle(thread, CreateObjectArray1D(thread, desc->one_fewer_dim, dim_sizes[0]));
  obj_header *result = nullptr;

  for (int i = 0; i < dim_sizes[0]; i++) {
    obj_header *subarray = CreateArray(thread, desc->one_fewer_dim, dim_sizes + 1, total_dimensions - 1);
    if (!subarray)
      goto oom;
    ReferenceArrayStore(arr->obj, i, subarray);
  }

  result = arr->obj;
oom:
  drop_handle(thread, arr);
  return result;
}

// Create a 1D primitive array of the given type and size.
obj_header *CreatePrimitiveArray1D(vm_thread *thread, type_kind inner_type, int count) {
  auto desc = get_or_create_array_classdesc(thread, primitive_classdesc(thread, inner_type));
  return CreateArray(thread, desc, &count, 1);
}

// Create a byte array with the given data.
obj_header *CreateByteArray(vm_thread *thread, u8 *data, int length) {
  obj_header *result = CreatePrimitiveArray1D(thread, TYPE_KIND_BYTE, length);
  if (!result)
    return nullptr;
  memcpy(ArrayData(result), data, length);
  return result;
}