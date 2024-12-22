//
// Created by alec on 12/20/24.
//

#include "arrays.h"
#include "bjvm.h"
#include "objects.h"

#include <assert.h>
#include <stdlib.h>

// Symmetry with make_array_classdesc
static void free_array_classdesc(bjvm_classdesc *classdesc) {
  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
         classdesc->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY);
  free(classdesc->fields);
  free(classdesc);
}

// Called for both primitive and object arrays
static void fill_array_classdesc(bjvm_thread *thread, bjvm_classdesc *base) {
  base->access_flags = BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL;

  bjvm_utf8 name = str("java/lang/Object");

  bjvm_cp_class_info *info = calloc(1, sizeof(bjvm_cp_class_info));
  info->classdesc = bootstrap_class_create(thread, name);
  info->name = name;
  base->super_class = info;
  base->fields_count = 1;

  bjvm_cp_field *fields = calloc(1, sizeof(bjvm_cp_field));
  base->fields = fields;
  fields->access_flags =
      BJVM_ACCESS_PUBLIC | BJVM_ACCESS_STATIC | BJVM_ACCESS_FINAL;

  fields->name = str("length");
  fields->descriptor = str("I");
}

static bjvm_classdesc *
primitive_array_classdesc(bjvm_thread *thread, bjvm_classdesc *component_type) {
  bjvm_classdesc *result = calloc(1, sizeof(bjvm_classdesc));
  result->state = BJVM_CD_STATE_INITIALIZED;
  result->kind = BJVM_CD_KIND_PRIMITIVE_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component_type->dimensions + 1;
  result->one_fewer_dim = component_type;
  result->primitive_component = component_type->primitive_component;
  if (component_type->kind == BJVM_CD_KIND_PRIMITIVE) {
    result->name = make_heap_str(2);
    bprintf(hslc(result->name), "[%c",
            (char)component_type->primitive_component);
  } else {
    result->name = make_heap_str(component_type->name.len + 1);
    bprintf(hslc(result->name), "[%.*s", fmt_slice(component_type->name));
  }
  return result;
}

// Make a class descriptor corresponding to an array of components.
static bjvm_classdesc *ordinary_array_classdesc(bjvm_thread *thread,
                                                bjvm_classdesc *component) {
  bjvm_classdesc *result = calloc(1, sizeof(bjvm_classdesc));
  // linkage state of array class is same as component class
  result->state = component->state;
  result->kind = BJVM_CD_KIND_ORDINARY_ARRAY;
  fill_array_classdesc(thread, result);
  result->dimensions = component->dimensions + 1;
  result->one_fewer_dim = component;

  if (component->kind == BJVM_CD_KIND_ORDINARY) {
    result->base_component = component;
    result->name = make_heap_str(component->name.len + 3);
    bprintf(hslc(result->name), "[%.*s;", fmt_slice(component->name));
  } else {
    result->base_component = component->base_component;
    result->name = make_heap_str(component->name.len + 1);
    bprintf(hslc(result->name), "[%.*s", fmt_slice(component->name));
    assert(result->dimensions == component->dimensions + 1);
  }

  return result;
}

// Fill in the array_type class descriptor, corresponding to an array of the
// given component. For example, J -> [J, [[J -> [[[J, Object -> [Object
bjvm_classdesc *make_array_classdesc(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc) {
  if (!classdesc->array_type) {
    if (classdesc->kind == BJVM_CD_KIND_PRIMITIVE) {
      classdesc->array_type = primitive_array_classdesc(thread, classdesc);
    } else {
      classdesc->array_type = ordinary_array_classdesc(thread, classdesc);
    }

    classdesc->array_type->dtor = free_array_classdesc;
  }
  return classdesc->array_type;
}

static bjvm_obj_header *create_1d_primitive_array(bjvm_thread *thread,
                                                  bjvm_type_kind array_type,
                                                  int count) {
  assert(count >= 0);

  int size = sizeof_type_kind(array_type);
  bjvm_classdesc *array_desc = make_array_classdesc(
      thread, bjvm_primitive_classdesc(thread, array_type));
  assert(array_desc);

  bjvm_obj_header *array = AllocateObject(thread, array_desc, kArrayHeaderSize + count * size);
  *ArrayLength(array) = count;

  return array;
}

static bjvm_obj_header *create_1d_object_array(bjvm_thread *thread,
                                               bjvm_classdesc *classdesc,
                                               int count) {
  assert(classdesc);
  assert(count >= 0);

  bjvm_classdesc *array_desc = make_array_classdesc(thread, classdesc);
        assert(array_desc);

  bjvm_obj_header *array = AllocateObject(thread, array_desc, kArrayHeaderSize + count * sizeof(bjvm_obj_header *));
  *ArrayLength(array) = count;

  return array;
}

bjvm_obj_header *CreateArray(bjvm_thread *thread, bjvm_classdesc *desc,int const *dim_sizes,
                             int total_dimensions) {
  assert(total_dimensions > 0);

  if (total_dimensions == 1) {
    switch (desc->kind) {
        case BJVM_CD_KIND_PRIMITIVE_ARRAY:
          return create_1d_primitive_array(thread, desc->primitive_component,
                                           dim_sizes[0]);
        case BJVM_CD_KIND_ORDINARY_ARRAY:
                return create_1d_object_array(thread, desc->one_fewer_dim,
                                                dim_sizes[0]);
        default:
          UNREACHABLE();
    }
  }

  auto arr = create_1d_object_array(thread, desc, dim_sizes[0]);

  for (int i = 0; i < dim_sizes[0]; i++) {
    bjvm_obj_header *subarray = CreateArray(thread, desc->one_fewer_dim, dim_sizes + 1, total_dimensions - 1);
    ReferenceArrayStore(arr, i, subarray);
  }

  return arr;
}