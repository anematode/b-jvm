// StackMapTable parser

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
  // for OBJECT and UNINIT only.
  // Internally used to detect whether a TOP entry is explicit or implicit
  slice *name;
} stack_map_frame_validation_type;

typedef struct {
  // The formal program counter of the current frame
  int pc;
  // The state of the stack
  stack_map_frame_validation_type *stack;
  int stack_size;
  // in unswizzled indices
  stack_map_frame_validation_type *locals;
  int locals_size;

  // pointer to implementation
  void *_impl;
} stack_map_frame_iterator;

// Initializes the iterator with the given method
int stack_map_frame_iterator_init(stack_map_frame_iterator *iter, const bjvm_cp_method *method);
// Check if there is another frame to read
bool stack_map_frame_iterator_has_next(const stack_map_frame_iterator *iter);
// Returns nonzero on a failure to read and fills in the error with a constant (although not terribly helpful) message
int stack_map_frame_iterator_next(stack_map_frame_iterator *iter, const char **error);
// Frees the iterator
void stack_map_frame_iterator_uninit(stack_map_frame_iterator *iter);

#endif //STACKMAPTABLE_H
