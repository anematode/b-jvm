//
// Created by alec on 12/20/24.
//

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

#include <assert.h>
#include <linkage.h>
#include <stdlib.h>

// Symmetry with make_array_classdesc
static void free_array_classdesc(classdesc *classdesc) {
  DCHECK(classdesc->kind == CD_KIND_ORDINARY_ARRAY ||
         classdesc->kind == CD_KIND_PRIMITIVE_ARRAY);
  if (classdesc->array_type)
    free_array_classdesc(classdesc->array_type);
  free_heap_str(classdesc->name);
  free(classdesc->super_class);
  free(classdesc->interfaces[0]); // Cloneable and Serializable together
  free(classdesc->interfaces);
  free_function_tables(classdesc);
  arena_uninit(&classdesc->arena);
  free(classdesc);
}

// Called for both primitive and object arrays
static void fill_array_classdesc(vm_thread *thread, classdesc *base) {
  base->access_flags =
      ACCESS_PUBLIC | ACCESS_FINAL | ACCESS_ABSTRACT;

  slice name = STR("java/lang/Object");
  cp_class_info *info = calloc(1, sizeof(cp_class_info));
  info->classdesc = bootstrap_lookup_class(thread, name);
  info->name = name;
  base->super_class = info;

  cp_class_info *Cloneable = calloc(2, sizeof(cp_class_info)),
                     *Serializable = Cloneable + 1;
  Cloneable->classdesc = bootstrap_lookup_class(thread, STR("java/lang/Cloneable"));
  Cloneable->name = STR("java/lang/Cloneable");
  Serializable->classdesc =
      bootstrap_lookup_class(thread, STR("java/io/Serializable"));
  Serializable->name = STR("java/io/Serializable");

  base->interfaces_count = 2;
  base->interfaces = calloc(2, sizeof(cp_class_info *));
  base->interfaces[0] = Cloneable;
  base->interfaces[1] = Serializable;
}

static classdesc *
primitive_array_classdesc(vm_thread *thread, classdesc *component_type) {
  classdesc *result = calloc(1, sizeof(classdesc));
  result->state = CD_STATE_INITIALIZED;
  result->kind = CD_KIND_PRIMITIVE_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component_type->dimensions + 1;
  result->one_fewer_dim = component_type;
  result->primitive_component = component_type->primitive_component;
  result->name = make_heap_str(2);
  bprintf(hslc(result->name), "[%c", (char)component_type->primitive_component);
  setup_super_hierarchy(result);
  set_up_function_tables(result);
  return result;
}

// Make a class descriptor corresponding to an array of components.
static classdesc *ordinary_array_classdesc(vm_thread *thread,
                                                classdesc *component) {
  classdesc *result = calloc(1, sizeof(classdesc));
  result->kind = CD_KIND_ORDINARY_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component->dimensions + 1;
  result->one_fewer_dim = component;

  if (component->kind == CD_KIND_ORDINARY) {
    result->base_component = component;
    result->name = make_heap_str(component->name.len + 3);
    bprintf(hslc(result->name), "[L%.*s;", fmt_slice(component->name));
  } else {
    result->base_component = component->base_component;
    result->name = make_heap_str(component->name.len + 1);
    bprintf(hslc(result->name), "[%.*s", fmt_slice(component->name));
    DCHECK(result->dimensions == component->dimensions + 1);
  }

  // propagate to n-D primitive arrays
  result->primitive_component = component->primitive_component;

  // linkage state of array class is same as component class
  result->state = CD_STATE_LOADED;
  if (component->state >= CD_STATE_LINKED) {
    link_class(thread, result);
  }
  result->state = component->state;

  return result;
}

// Fill in the array_type class descriptor, corresponding to an array of the
// given component. For example, J -> [J, [[J -> [[[J, Object -> [Object
classdesc *make_array_classdesc(vm_thread *thread,
                                     classdesc *classdesc) {
  DCHECK(classdesc);
  if (!classdesc->array_type) {
    if (classdesc->kind == CD_KIND_PRIMITIVE) {
      classdesc->array_type = primitive_array_classdesc(thread, classdesc);
    } else {
      classdesc->array_type = ordinary_array_classdesc(thread, classdesc);
    }
    classdesc->array_type->dtor = free_array_classdesc;
  }
  return classdesc->array_type;
}

static obj_header *create_1d_primitive_array(vm_thread *thread,
                                                  type_kind array_type,
                                                  int count) {
  DCHECK(count >= 0);

  int size = sizeof_type_kind(array_type);
  classdesc *array_desc = make_array_classdesc(
      thread, primitive_classdesc(thread, array_type));
  DCHECK(array_desc);

  obj_header *array =
      AllocateObject(thread, array_desc, kArrayHeaderSize + count * size);
  if (array)
    *ArrayLength(array) = count;

  return array;
}

static obj_header *create_1d_object_array(vm_thread *thread,
                                               classdesc *cd,
                                               int count) {
  DCHECK(cd);
  DCHECK(count >= 0);

  classdesc *array_desc = make_array_classdesc(thread, cd);
  DCHECK(array_desc);

  obj_header *array = AllocateObject(
      thread, array_desc, kArrayHeaderSize + count * sizeof(obj_header *));
  if (array)
    *ArrayLength(array) = count;

  return array;
}

obj_header *CreateArray(vm_thread *thread, classdesc *desc,
                             int const *dim_sizes, int total_dimensions) {
  DCHECK(total_dimensions > 0);

  if (total_dimensions == 1) {
    switch (desc->kind) {
    case CD_KIND_PRIMITIVE_ARRAY:
      return create_1d_primitive_array(thread, desc->primitive_component,
                                       dim_sizes[0]);
    case CD_KIND_ORDINARY_ARRAY:
      return create_1d_object_array(thread, desc->one_fewer_dim, dim_sizes[0]);
    default:
      UNREACHABLE();
    }
  }

  auto arr = make_handle(thread, create_1d_object_array(thread, desc->one_fewer_dim, dim_sizes[0]));
  obj_header *result = nullptr;

  for (int i = 0; i < dim_sizes[0]; i++) {
    obj_header *subarray = CreateArray(
        thread, desc->one_fewer_dim, dim_sizes + 1, total_dimensions - 1);
    if (!subarray)
      goto oom;
    ReferenceArrayStore(arr->obj, i, subarray);
  }

  result = arr->obj;
  oom:
  drop_handle(thread, arr);
  return result;
}