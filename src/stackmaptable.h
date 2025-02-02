//
// Created by Cowpox on 2/1/25.
//

#ifndef STACKMAPTABLE_H
#define STACKMAPTABLE_H

#include "classfile.h"

typedef enum : uint8_t {
  STACK_MAP_FRAME_VALIDATION_TYPE_TOP,
  STACK_MAP_FRAME_VALIDATION_TYPE_INTEGER,
  STACK_MAP_FRAME_VALIDATION_TYPE_FLOAT,
  STACK_MAP_FRAME_VALIDATION_TYPE_DOUBLE,
  STACK_MAP_FRAME_VALIDATION_TYPE_LONG,
  STACK_MAP_FRAME_VALIDATION_TYPE_NULL,
  STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT_THIS,
  STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT,
  STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT
} stack_map_frame_validation_type_kind;

static_assert(STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT == 8);

typedef struct {
  stack_map_frame_validation_type_kind kind;
  // for OBJECT and UNINIT only. internally used to detect whether a TOP entry is explicit or implicit
  slice *name;
} stack_map_frame_validation_type;

typedef struct {
  int pc;
  stack_map_frame_validation_type *stack;
  int stack_size;
  stack_map_frame_validation_type *locals; // in unswizzled indices
  int locals_size;

  // pimpl
  void *_impl;
} stack_map_frame_iterator;

void stack_map_frame_iterator_init(stack_map_frame_iterator *iter, const bjvm_cp_method *method);
bool stack_map_frame_iterator_has_next(const stack_map_frame_iterator *iter);
// Returns nonzero on error and fills in the heap_string
int stack_map_frame_iterator_next(stack_map_frame_iterator *iter, const char **error);
void stack_map_frame_iterator_uninit(stack_map_frame_iterator *iter);

#endif //STACKMAPTABLE_H
