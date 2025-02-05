#include "stackmaptable.h"
#include "classfile.h"

typedef stack_map_frame_validation_type validation_type;
typedef stack_map_frame_iterator iterator;

typedef struct {
  bjvm_attribute_code *code;
  attribute_stack_map_table table_copy;  // the pointers here indicate how much is left to read
  bool has_next;  // whether there is another frame to read
  bool is_first;  // are we about to compute the first stack map frame besides the 0th, implicit frame
} impl;

enum { EXPLICIT_TOP = 0 /* default */, IMPLICIT_TOP };

static validation_type *allocate_validation_buffer(size_t count) {
  validation_type *b = calloc(count + 1 /* to allow for some slop in wide kinds */, sizeof(validation_type));
  static_assert(STACK_MAP_FRAME_VALIDATION_TYPE_TOP == 0);
  static_assert(EXPLICIT_TOP == 0);
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
static int init_locals(iterator *iter, const bjvm_cp_method *method) {
  iter->locals = allocate_validation_buffer(method->code->max_locals);
  if (!iter->locals)
    return -1; // oom
  s32 i = 0;
  if (!(method->access_flags & BJVM_ACCESS_STATIC)) {
    // Write "this"
    iter->locals[i++] = (validation_type) {
      method->is_ctor ? STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT_THIS : STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT,
      (slice*)&method->my_class->name
    };
  }

  // Write the remaining objects
  bjvm_method_descriptor *d = method->descriptor;
  assert(d && "Method has no descriptor");
  for (int j = 0; j < d->args_count; ++i, ++j) {
    bjvm_type_kind kind = field_to_kind(d->args + j);
    iter->locals[i].kind = lut[kind];
    if (kind == BJVM_TYPE_KIND_REFERENCE) {
      iter->locals[i].name = nullptr; // TODO
    } else if (kind == BJVM_TYPE_KIND_DOUBLE || kind == BJVM_TYPE_KIND_LONG) {
      iter->locals[++i] = implicit_top;
    }
  }
  iter->locals_size = i;
  return 0;
}

int stack_map_frame_iterator_init(stack_map_frame_iterator *iter, const bjvm_cp_method *method) {
  memset(iter, 0, sizeof(*iter));  // clear memory
  impl *I = iter->_impl = calloc(1, sizeof(impl));
  if (!I)
    return -1;
  bjvm_attribute_code *code = I->code = method->code;
  assert(code && "Method has no code");
  // Look for a StackMapTable, otherwise zero-init
  I->is_first = true;
  for (int i = 0; i < code->attributes_count; ++i) {
    if (BJVM_ATTRIBUTE_KIND_STACK_MAP_TABLE == code->attributes[i].kind) {
      I->table_copy = code->attributes[i].smt;
      I->has_next = true;
      break;
    }
  }
  iter->stack = allocate_validation_buffer(code->max_stack);
  if (!iter->stack || init_locals(iter, method))
    goto oom;

  if (I->table_copy.length && I->table_copy.data[0] == 0) {
    // Funny case where the first SMT entry is same_frame with offset 0; skip it
    stack_map_frame_iterator_next(iter, nullptr);
  }
  return 0;

  oom:
  stack_map_frame_iterator_uninit(iter);
  return -1;
}

bool stack_map_frame_iterator_has_next(const stack_map_frame_iterator *iter) {
  return ((impl*)iter->_impl)->has_next;
}

// Try to read a 16-bit integer, returning nonzero on failure
static int read_u8(impl *I, u8 *val, const char **error) {
  if (unlikely(I->table_copy.length == 0)) {
    *error = "Unexpected end of stackmaptable";
    return -1;
  }
  *val = *I->table_copy.data++;
  I->table_copy.length--;
  return 0;
}

static int read_u16(impl *I, u16 *val, const char **error) {
  if (unlikely(I->table_copy.length < 2)) {
    *error = "Unexpected end of stackmaptable";
    return -1;
  }
  *val = *I->table_copy.data++ << 8;  // big-endian
  *val |= *I->table_copy.data++;
  I->table_copy.length -= 2;
  return 0;
}

static int read_verification_type(const iterator *iter, validation_type *result, bool *is_wide, const char **error) {
  u8 type_kind;
  if (read_u8(iter->_impl, &type_kind, error))
    return -1;
  if (type_kind > STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT) {
    *error = "Invalid verification type";
    return -1;
  }
  result->kind = type_kind;
  if (type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT || type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT) {
    // Read the name
    u16 index;
    if (read_u16(iter->_impl, &index, error))
      return -1;
    result->name = nullptr;  // TODO
  } else {
    result->name = nullptr;
  }
  *is_wide = type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_DOUBLE || type_kind == STACK_MAP_FRAME_VALIDATION_TYPE_LONG;
  return 0;
}

static void increment_pc(iterator *iter, int offset_delta) {
  impl *I = iter->_impl;
  if (I->is_first) {  // the first stack map frame is only offset_delta away from 0
    iter->pc += offset_delta;
    I->is_first = false;
  } else {
    iter->pc += offset_delta + 1;
  }
}

static bool is_implicit_top(stack_map_frame_validation_type local) {
  return local.kind == STACK_MAP_FRAME_VALIDATION_TYPE_TOP && local.name == (void*) IMPLICIT_TOP;
}

#define MAY_FAIL(cond) if (cond) { return -1; }
#define MAY_FAIL_MSG(cond, msg) if (cond) { *error = msg; return -1; }

// Returns nonzero on error and fills in the heap_string
int stack_map_frame_iterator_next(stack_map_frame_iterator *iter, const char **error) {
  impl *I =iter->_impl;
  int max_locals = I->code->max_locals;
  int max_stack = I->code->max_stack;

  u8 frame_kind = 0;
  MAY_FAIL(read_u8(iter->_impl, &frame_kind, error))
  u16 offset_delta;
  bool is_wide;
  if (frame_kind <= 63) {
    // same_frame, just set stack to zero
    offset_delta = frame_kind;
    iter->stack_size = 0;
  } else if ((64 <= frame_kind && frame_kind <= 127) || frame_kind == 247) {
    // same_locals_1_stack_item_frame
    iter->stack_size = 1;
    bool is_extended = frame_kind == 247;
    if (is_extended) {
      MAY_FAIL(read_u16(iter->_impl, &offset_delta, error))
    } else {
      offset_delta = frame_kind - 64;
    }
    MAY_FAIL(read_verification_type(iter, iter->stack, &is_wide, error))
  } else if (128 <= frame_kind && frame_kind <= 246) {
    *error = "Reserved frame kind";
    return -1;
  } else if (248 <= frame_kind && frame_kind <= 250) {
    // chop_frame
    int pop_count = 251 - frame_kind;
    int locals_size = iter->locals_size;
    for (int pop_i = 0; pop_i < pop_count; ++pop_i) {
      if (is_implicit_top(iter->locals[locals_size-- - 1])) {
        locals_size--;
      }
      MAY_FAIL_MSG(locals_size < 0, "chop_frame underflow");
    }
    iter->locals_size = locals_size;
    iter->stack_size = 0;
    MAY_FAIL(read_u16(iter->_impl, &offset_delta, error));
  } else if (frame_kind == 251) {
    // same_frame_extended
    MAY_FAIL(read_u16(iter->_impl, &offset_delta, error));
    iter->stack_size = 0;
  } else if (252 <= frame_kind && frame_kind <= 254) {
    // append_frame
    MAY_FAIL(read_u16(iter->_impl, &offset_delta, error));
    iter->stack_size = 0;
    int additional = frame_kind - 251;
    for (int j = 0; j < additional; ++j) {
      validation_type *local = iter->locals + iter->locals_size++;
      MAY_FAIL(read_verification_type(iter, local, &is_wide, error));
      if (is_wide)
        *(iter->locals + iter->locals_size++) = implicit_top;
      MAY_FAIL_MSG(iter->locals_size > max_locals, "append_frame locals overflow");
    }
  } else {
    // full_frame
    assert(frame_kind == 255);
    MAY_FAIL(read_u16(iter->_impl, &offset_delta, error));
    u16 num_locals, num_stack;
    MAY_FAIL(read_u16(iter->_impl, &num_locals, error));
    iter->locals_size = 0;
    iter->stack_size = 0;
    for (int i = 0; i < num_locals; ++i) {
      validation_type *local = iter->locals + iter->locals_size++;
      MAY_FAIL(read_verification_type(iter, local, &is_wide, error))
      if (is_wide)
        *(iter->locals + iter->locals_size++) = implicit_top;
      MAY_FAIL_MSG(iter->locals_size > max_locals, "full_frame locals overflow");
    }
    MAY_FAIL(read_u16(iter->_impl, &num_stack, error))
    for (int i = 0; i < num_stack; ++i) {
      validation_type *stack = iter->stack + iter->stack_size++;
      MAY_FAIL(read_verification_type(iter, stack, &is_wide, error))
      MAY_FAIL_MSG(iter->stack_size > max_stack, "full_frame stack overflow");
    }
  }

  MAY_FAIL_MSG(iter->stack_size > max_stack || iter->locals_size > max_locals, "Stack map frame overflow");
  I->has_next = I->table_copy.length > 0;
  increment_pc(iter, offset_delta);
  return 0;
}

void stack_map_frame_iterator_uninit(stack_map_frame_iterator *iter) {
  free(iter->_impl);
  free(iter->locals);
  free(iter->stack);
}