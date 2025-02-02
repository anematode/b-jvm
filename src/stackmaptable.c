//
// Created by Cowpox on 2/1/25.
//

#include "stackmaptable.h"
#include "classfile.h"

typedef stack_map_frame_validation_type validation_type;
typedef stack_map_frame_iterator iterator;

typedef struct {
  bjvm_attribute_code *code;
  attribute_stack_map_table table_copy;
  bool has_next;
  bool is_first;  // is this the first stack map frame besides the 0th, implicit frame
} impl;

enum { EXPLICIT_TOP = 0 /* default */, IMPLICIT_TOP };

static validation_type *allocate_validation_buffer(size_t count) {
  validation_type *b = malloc((count + 1 /* to allow for some slop */) * sizeof(validation_type));
  while (count--)
    b[count] = (validation_type) { STACK_MAP_FRAME_VALIDATION_TYPE_TOP, (void*) EXPLICIT_TOP };
  return b;
}

static stack_map_frame_validation_type_kind lut[] = {
  [BJVM_TYPE_KIND_REFERENCE] = STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT,
  [BJVM_TYPE_KIND_INT] = STACK_MAP_FRAME_VALIDATION_TYPE_INTEGER,
  [BJVM_TYPE_KIND_LONG] = STACK_MAP_FRAME_VALIDATION_TYPE_LONG,
  [BJVM_TYPE_KIND_FLOAT] = STACK_MAP_FRAME_VALIDATION_TYPE_FLOAT,
  [BJVM_TYPE_KIND_DOUBLE] = STACK_MAP_FRAME_VALIDATION_TYPE_DOUBLE
};

static validation_type implicit_top = { STACK_MAP_FRAME_VALIDATION_TYPE_TOP, (void*) IMPLICIT_TOP };

// Create the initial stack frame for the method
static void init_locals(iterator *iter, const bjvm_cp_method *method) {
  iter->locals = allocate_validation_buffer(method->code->max_locals);
  int32_t i = 0;
  bool is_ctor = utf8_equals(method->name, "<init>");
  if (!(method->access_flags & BJVM_ACCESS_STATIC)) {
    // Write "this"
    iter->locals[i++] = (validation_type) {
      is_ctor ? STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT_THIS : STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT,
      // TODO this is probs UB
      (slice*)&method->my_class->name
    };
  }

  // Write the remaining objects
  auto *d = method->descriptor;
  assert(d && "Method has no descriptor");
  for (int j = 0; j < d->args_count; ++i, ++j) {
    bjvm_type_kind kind = field_to_kind(d->args + j);
    iter->locals[i].kind = lut[kind];
    if (kind == BJVM_TYPE_KIND_REFERENCE) {
      iter->locals[i].name = nullptr; // TODO
    } else if (kind == BJVM_TYPE_KIND_DOUBLE || kind == BJVM_TYPE_KIND_LONG) {
      iter->locals[i + 1] = implicit_top;
      i++;
    }
  }
  iter->locals_size = i;
}

void stack_map_frame_iterator_init(stack_map_frame_iterator *iter, const bjvm_cp_method *method) {
  impl *I = iter->_impl = malloc(sizeof(impl));
  auto *code = I->code = method->code;
  assert(code && "Method has no code");
  // Look for a StackMapTable, otherwise zero-init
  I->has_next = false;
  I->is_first = true;
  for (int i = 0; i < code->attributes_count; ++i) {
    if (BJVM_ATTRIBUTE_KIND_STACK_MAP_TABLE == code->attributes[i].kind) {
      I->table_copy = code->attributes[i].smt;
      I->has_next = true;
      goto found;
    }
  }
  memset(&I->table_copy, 0, sizeof(I->table_copy));
  found:
  iter->pc = 0;
  init_locals(iter, method);
  iter->stack_size = 0;
  iter->stack = allocate_validation_buffer(code->max_stack);

  if (I->table_copy.length && I->table_copy.data[0] == 0) {
    // Funny case where the first SMT entry is same_frame with offset 0; skip it
    stack_map_frame_iterator_next(iter, nullptr);
  }
}

bool stack_map_frame_iterator_has_next(const stack_map_frame_iterator *iter) {
  return ((impl*)iter->_impl)->has_next;
}

// Try to read a 16-bit integer, returning nonzero on failure
static int read_u8(impl *I, uint8_t *val, const char **error) {
  if (unlikely(I->table_copy.length == 0)) {
    *error = "Unexpected end of stackmaptable";
    return -1;
  }
  *val = *I->table_copy.data++;
  I->table_copy.length--;
  return 0;
}

static int read_u16(impl *I, uint16_t *val, const char **error) {
  if (unlikely(I->table_copy.length < 2)) {
    *error = "Unexpected end of stackmaptable";
    return -1;
  }
  *val = *I->table_copy.data++ << 8;  // big-endian
  *val |= *I->table_copy.data++;
  I->table_copy.length -= 2;
  return 0;
}

static int read_verification_type(iterator *iter, validation_type *result, bool *is_wide, const char **error) {
  uint8_t type_kind;
  if (read_u8(iter->_impl, &type_kind, error))
    return -1;
  if (type_kind > STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT) {
    *error = "Invalid verification type";
    return -1;
  }
  result->kind = type_kind;
  if (type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT || type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT) {
    // Read the name
    uint16_t index;
    if (read_u16(iter->_impl, &index, error))
      return -1;
    result->name = nullptr;  // TODO
  } else {
    result->name = nullptr;
  }
  *is_wide = type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_DOUBLE || type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_LONG;
  return 0;
}

static void increment_pc(iterator *iter, int delta) {
  impl *I = iter->_impl;
  if (I->is_first) {
    iter->pc += delta;
    I->is_first = false;
  } else {
    iter->pc += delta + 1;
  }
}

bool is_implicit_top(stack_map_frame_validation_type local) {
  return local.kind == STACK_MAP_FRAME_VALIDATION_TYPE_TOP && local.name == (void*) IMPLICIT_TOP;
}

// Returns nonzero on error and fills in the heap_string
int stack_map_frame_iterator_next(stack_map_frame_iterator *iter, const char **error) {
  impl *I =iter->_impl;
  int max_locals = I->code->max_locals;
  int max_stack = I->code->max_stack;

  uint8_t frame_kind = 0;
  if (read_u8(iter->_impl, &frame_kind, error))
    return -1;
  uint16_t offset_delta;
  bool is_wide;
  if (frame_kind <= 63) {
    // same_frame, just set stack to zero
    offset_delta = frame_kind;
    iter->stack_size = 0;
  } else if ((64 <= frame_kind && frame_kind <= 127) || frame_kind == 247) {
    // same_locals_1_stack_item_frame
    iter->stack_size = 1;
    bool is_extended = frame_kind == 247;
    if (is_extended && read_u16(iter->_impl, &offset_delta, error))
      return -1;
    if (read_verification_type(iter, iter->stack, &is_wide, error))
      return -1;
    if (!is_extended)
      offset_delta = frame_kind - 64;
  } else if (128 <= frame_kind && frame_kind <= 246) {
    *error = "Reserved frame kind";
    return -1;
  } else if (248 <= frame_kind && frame_kind <= 250) {
    // chop_frame
    for (int pop_i = 0; pop_i < 251 - frame_kind; ++pop_i) {
      if (is_implicit_top(iter->locals[iter->locals_size-- - 1])) {
        iter->locals_size--;
      }
    }

    if (iter->locals_size < 0) {
      *error = "chop_frame underflow";
      return -1;
    }
    iter->stack_size = 0;
    if (read_u16(iter->_impl, &offset_delta, error))
      return -1;
  } else if (frame_kind == 251) {
    // same_frame_extended
    if (read_u16(iter->_impl, &offset_delta, error))
      return -1;
    iter->stack_size = 0;
  } else if (252 <= frame_kind && frame_kind <= 254) {
    // append_frame
    if (read_u16(iter->_impl, &offset_delta, error))
      return -1;
    iter->stack_size = 0;
    int additional = frame_kind - 251;
    for (int j = 0; j < additional; ++j) {
      validation_type *local = iter->locals + iter->locals_size++;
      if (read_verification_type(iter, local, &is_wide, error))
        return -1;
      if (is_wide)
        *(iter->locals + iter->locals_size++) = implicit_top;
      if (iter->locals_size > max_locals) {
        *error = "append_frame locals overflow";
        return -1;
      }
    }
  } else {
    // full_frame
    assert(frame_kind == 255);
    if (read_u16(iter->_impl, &offset_delta, error))
      return -1;
    uint16_t num_locals, num_stack;
    if (read_u16(iter->_impl, &num_locals, error))
      return -1;
    iter->locals_size = 0;
    iter->stack_size = 0;
    for (int i = 0; i < num_locals; ++i) {
      validation_type *local = iter->locals + iter->locals_size++;
      if (read_verification_type(iter, local, &is_wide, error))
        return -1;
      if (is_wide)
        *(iter->locals + iter->locals_size++) = implicit_top;
      if (iter->locals_size > max_locals) {
        *error = "full_frame locals overflow";
        return -1;
      }
    }
    if (read_u16(iter->_impl, &num_stack, error))
      return -1;
    for (int i = 0; i < num_stack; ++i) {
      validation_type *stack = iter->stack + iter->stack_size++;
      if (read_verification_type(iter, stack, &is_wide, error))
        return -1;
      if (iter->stack_size > max_stack) {
        *error = "full_frame stack overflow";
        return -1;
      }
    }
  }

  if (iter->stack_size > max_stack || iter->locals_size > max_locals) {
    *error = "Stack map frame overflow";
    return -1;
  }

  I->has_next = I->table_copy.length > 0;
  increment_pc(iter, offset_delta);
  return 0;
}

void stack_map_frame_iterator_uninit(stack_map_frame_iterator *iter) {
  free(iter->_impl);
  free(iter->locals);
  free(iter->stack);
}