#include "exceptions.h"

#include <linkage.h>

#include <analysis.h>
#include <classfile.h>
#include <vtable.h>

#include <bjvm.h>

// Credit: https://stackoverflow.com/a/77159291/13458117
#define SIZEOF_POINTER (UINTPTR_MAX / 255 % 255)

static size_t allocate_field(size_t *current, type_kind kind) {
  size_t result;
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
  case TYPE_KIND_BYTE: {
    result = *current;
    (*current)++;
    break;
  }
  case TYPE_KIND_CHAR:
  case TYPE_KIND_SHORT: {
    *current = align_up(*current, 2);
    result = *current;
    *current += 2;
    break;
  }
  case TYPE_KIND_FLOAT:
  case TYPE_KIND_INT:
#if SIZEOF_POINTER == 4
  case TYPE_KIND_REFERENCE:
#endif
    *current = align_up(*current, 4);
    result = *current;
    *current += 4;
    break;
  case TYPE_KIND_DOUBLE:
  case TYPE_KIND_LONG:
#if SIZEOF_POINTER == 8
  case TYPE_KIND_REFERENCE:
#endif
    *current = align_up(*current, 8);
    result = *current;
    *current += 8;
    break;

  case TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
  return result;
}

// Construct superclass hierarchy, used for fast instanceof checks
void setup_super_hierarchy(classdesc *cd) {
  if (cd->super_class) {
    classdesc *super = cd->super_class->classdesc;
    assert(super && "Superclass is resolved");
    assert(super->hierarchy_len > 0 && "Invalid hierarchy length");

    cd->hierarchy_len = super->hierarchy_len + 1;
    cd->hierarchy = arena_alloc(&cd->arena, super->hierarchy_len + 1, sizeof(classdesc *));
    memcpy(cd->hierarchy, super->hierarchy, super->hierarchy_len * sizeof(classdesc *));
  } else {
    DCHECK(utf8_equals(cd->name, "java/lang/Object"));
    // java/lang/Object has a superclass hierarchy of length 1, containing only itself
    cd->hierarchy_len = 1;
    cd->hierarchy = arena_alloc(&cd->arena, 1, sizeof(classdesc *));
  }

  // Place ourselves into the hierarchy
  cd->hierarchy[cd->hierarchy_len - 1] = cd;
}

// assumes already holding lock
static int link_array_class_impl(vm_thread *thread, classdesc *cd) {
  if (cd->state >= CD_STATE_LINKED)
    return 0;

  classdesc_state st = CD_STATE_LINKED;
  cd->state = st;
  int status = 0;
  if (cd->base_component) {
    status = link_class_impl(thread, cd->base_component); // already holding lock
    if (status) {
      st = CD_STATE_LINKAGE_ERROR;
    }
    // Link all higher dimensional components
    classdesc *a = cd->base_component;
    while (a->array_type) {
      a = a->array_type;
      a->state = st;
    }
  }
  setup_super_hierarchy(cd);
  set_up_function_tables(cd);
  return status;
}

// Reorder fields in a class so that bigger fields are placed first.
static int *reorder_fields_for_compactness(cp_field *fields, int fields_count) {
  int *order = malloc(sizeof(int) * fields_count);
  // Because there's only a few values (1, 2, 4, 8), we can just use buckets
  int *buckets[4] = {nullptr};
  for (int i = 0; i < fields_count; ++i) {
    cp_field *field = fields + i;
    int size = sizeof_type_kind(field->parsed_descriptor.repr_kind);
    DCHECK(size == 1 || size == 2 || size == 4 || size == 8);
    arrput(buckets[__builtin_ctz(size)], i);
  }
  int j = 0;
  for (int i = 3; i >= 0; --i) {
    for (int k = 0; k < arrlen(buckets[i]); ++k)
      order[j++] = buckets[i][k];
    arrfree(buckets[i]);
  }
  DCHECK(j == fields_count);
  return order;
}

void create_template_interpreter_frame(cp_method *method) {
  stack_frame frame = {nullptr};
  frame.method = method;
  frame.code = method->code->code;
  DCHECK(frame.code);
  frame.insn_index_to_sd = method->code_analysis->insn_index_to_sd;
  DCHECK(frame.insn_index_to_sd);
  frame.kind = FRAME_KIND_INTERPRETER;
  frame.is_async_suspended = false;
  frame.synchronized_state = SYNCHRONIZE_NONE;
  frame.program_counter = 0;
  frame.max_stack = method->code->max_stack;
  frame.num_locals = method->code->max_locals;
  static_assert(sizeof(method->template_frame) >= sizeof(frame));
  memcpy(method->template_frame, &frame, sizeof(frame));
}

// Link the class.
// NOLINTNEXTLINE(misc-no-recursion)
int link_class(vm_thread *thread, classdesc *cd) {
  pthread_mutex_lock(&cd->lock); // todo: check result?
  int result = link_class_impl(thread, cd);
  pthread_mutex_unlock(&cd->lock);
  return result;
}

// Link the class.
// assumes the lock is held for the input classdesc.
// NOLINTNEXTLINE(misc-no-recursion)
int link_class_impl(vm_thread *thread, classdesc *cd) {
  if (cd->state != CD_STATE_LOADED) {
    return 0;
  }
  // Link superclasses
  if (cd->super_class) {
    classdesc *super = cd->super_class->classdesc;
    int status = link_class(thread, super);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      cd->linkage_error = thread->current_exception;
      return status;
    }
  }

  setup_super_hierarchy(cd);

  // Link superinterfaces
  for (int i = 0; i < cd->interfaces_count; ++i) {
    int status = link_class(thread, cd->interfaces[i]->classdesc);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      cd->linkage_error = thread->current_exception;
      return status;
    }
  }
  if (cd->kind != CD_KIND_ORDINARY) {
    DCHECK(cd->kind == CD_KIND_ORDINARY_ARRAY);
    int status = link_array_class_impl(thread, cd);
    return status;
  }
  cd->state = CD_STATE_LINKED;
  // Link the corresponding array type(s)
  if (cd->array_type) {
    link_array_class_impl(thread, cd->array_type); // for fun? idk why
  }
  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < cd->methods_count; ++method_i) {
    cp_method *method = cd->methods + method_i;
    if (method->code) {
      heap_string error_str;
      int result = analyze_method_code(method, &error_str);
      if (result != 0) {
        cd->state = CD_STATE_LINKAGE_ERROR;
        raise_verify_error(thread, hslc(error_str));
        cd->linkage_error = thread->current_exception;
        free_heap_str(error_str);
        return -1;
      }
      create_template_interpreter_frame(method);
    }
  }

  // Padding for VM fields (e.g., internal fields used for Reflection)
  int padding = (int)(uintptr_t)hash_table_lookup(&thread->vm->class_padding, cd->name.chars, cd->name.len);

  // Assign memory locations to all static/non-static fields
  classdesc *super = cd->super_class ? cd->super_class->classdesc : nullptr;
  size_t static_offset = 0, nonstatic_offset = super ? super->instance_bytes : sizeof(obj_header);
  nonstatic_offset += padding;

  bool must_have_C_layout = hash_table_contains(&thread->vm->class_padding, cd->name.chars, cd->name.len);
  int *order = nullptr;
  if (!must_have_C_layout)
    order = reorder_fields_for_compactness(cd->fields, cd->fields_count);
  u32 static_refs_c = 0, nonstatic_refs_c = super ? super->instance_references->count : 0;
  u32 super_refs_c = nonstatic_refs_c;
  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + (must_have_C_layout ? field_i : order[field_i]);
    type_kind kind = field->parsed_descriptor.repr_kind;
    field->byte_offset = field->access_flags & ACCESS_STATIC ? allocate_field(&static_offset, kind)
                                                             : allocate_field(&nonstatic_offset, kind);
    // printf("Allocating field %.*s for class %.*s at %zu\n", fmt_slice(field->name), fmt_slice(cd->name),
    //         field->byte_offset);
    if (kind == TYPE_KIND_REFERENCE) {
      bool is_static = field->access_flags & ACCESS_STATIC;
      static_refs_c += is_static;
      nonstatic_refs_c += !is_static;
    }
  }
  free(order);
  cd->static_references = arena_alloc(&cd->arena, 1, sizeof(reference_list) + static_refs_c * sizeof(u16));
  cd->instance_references = arena_alloc(&cd->arena, 1, sizeof(reference_list) + nonstatic_refs_c * sizeof(u16));

  cd->static_references->count = static_refs_c;
  cd->instance_references->count = nonstatic_refs_c;

  // Add superclass instance references
  if (super) {
    reference_list *super_refs = super->instance_references;
    memcpy(cd->instance_references->slots_unscaled, super_refs->slots_unscaled, super_refs->count * sizeof(u16));
  }

  static_refs_c = 0;
  nonstatic_refs_c = super_refs_c;
  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + field_i;
    if (field->parsed_descriptor.repr_kind == TYPE_KIND_REFERENCE) {
      bool is_static = field->access_flags & ACCESS_STATIC;
      u16 *slots = is_static ? cd->static_references->slots_unscaled : cd->instance_references->slots_unscaled;
      slots[is_static ? static_refs_c : nonstatic_refs_c] = field->byte_offset / sizeof(void *);
      nonstatic_refs_c += !is_static;
      static_refs_c += is_static;
    }
  }

  // Create static field memory, initializing all to 0
  cd->static_fields = arena_alloc(&cd->arena, static_offset, sizeof(u8));
  cd->instance_bytes = nonstatic_offset;

  // Set up vtable and itables
  set_up_function_tables(cd);

  return 0;
}

// assumes the class and the super class are locked
// NOLINTNEXTLINE(misc-no-recursion)
int link_class_impl_super_impl(vm_thread *thread, classdesc *cd) {
  if (cd->state != CD_STATE_LOADED) {
    return 0;
  }
  // Link superclasses
  if (cd->super_class) {
    classdesc *super = cd->super_class->classdesc;
    int status = link_class_impl(thread, super);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      cd->linkage_error = thread->current_exception;
      return status;
    }
  }

  setup_super_hierarchy(cd);

  // Link superinterfaces
  for (int i = 0; i < cd->interfaces_count; ++i) {
    int status = link_class_impl(thread, cd->interfaces[i]->classdesc);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      cd->linkage_error = thread->current_exception;
      return status;
    }
  }
  if (cd->kind != CD_KIND_ORDINARY) {
    DCHECK(cd->kind == CD_KIND_ORDINARY_ARRAY);
    int status = link_array_class_impl(thread, cd);
    return status;
  }
  cd->state = CD_STATE_LINKED;
  // Link the corresponding array type(s)
  if (cd->array_type) {
    link_array_class_impl(thread, cd->array_type); // for fun? idk why
  }
  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < cd->methods_count; ++method_i) {
    cp_method *method = cd->methods + method_i;
    if (method->code) {
      heap_string error_str;
      int result = analyze_method_code(method, &error_str);
      if (result != 0) {
        cd->state = CD_STATE_LINKAGE_ERROR;
        raise_verify_error(thread, hslc(error_str));
        cd->linkage_error = thread->current_exception;
        free_heap_str(error_str);
        return -1;
      }
      create_template_interpreter_frame(method);
    }
  }

  // Padding for VM fields (e.g., internal fields used for Reflection)
  int padding = (int)(uintptr_t)hash_table_lookup(&thread->vm->class_padding, cd->name.chars, cd->name.len);

  // Assign memory locations to all static/non-static fields
  classdesc *super = cd->super_class ? cd->super_class->classdesc : nullptr;
  size_t static_offset = 0, nonstatic_offset = super ? super->instance_bytes : sizeof(obj_header);
  nonstatic_offset += padding;

  bool must_have_C_layout = hash_table_contains(&thread->vm->class_padding, cd->name.chars, cd->name.len);
  int *order = nullptr;
  if (!must_have_C_layout)
    order = reorder_fields_for_compactness(cd->fields, cd->fields_count);
  u32 static_refs_c = 0, nonstatic_refs_c = super ? super->instance_references->count : 0;
  u32 super_refs_c = nonstatic_refs_c;
  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + (must_have_C_layout ? field_i : order[field_i]);
    type_kind kind = field->parsed_descriptor.repr_kind;
    field->byte_offset = field->access_flags & ACCESS_STATIC ? allocate_field(&static_offset, kind)
                                                             : allocate_field(&nonstatic_offset, kind);
    // printf("Allocating field %.*s for class %.*s at %zu\n", fmt_slice(field->name), fmt_slice(cd->name),
    //         field->byte_offset);
    if (kind == TYPE_KIND_REFERENCE) {
      bool is_static = field->access_flags & ACCESS_STATIC;
      static_refs_c += is_static;
      nonstatic_refs_c += !is_static;
    }
  }
  free(order);
  cd->static_references = arena_alloc(&cd->arena, 1, sizeof(reference_list) + static_refs_c * sizeof(u16));
  cd->instance_references = arena_alloc(&cd->arena, 1, sizeof(reference_list) + nonstatic_refs_c * sizeof(u16));

  cd->static_references->count = static_refs_c;
  cd->instance_references->count = nonstatic_refs_c;

  // Add superclass instance references
  if (super) {
    reference_list *super_refs = super->instance_references;
    memcpy(cd->instance_references->slots_unscaled, super_refs->slots_unscaled, super_refs->count * sizeof(u16));
  }

  static_refs_c = 0;
  nonstatic_refs_c = super_refs_c;
  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + field_i;
    if (field->parsed_descriptor.repr_kind == TYPE_KIND_REFERENCE) {
      bool is_static = field->access_flags & ACCESS_STATIC;
      u16 *slots = is_static ? cd->static_references->slots_unscaled : cd->instance_references->slots_unscaled;
      slots[is_static ? static_refs_c : nonstatic_refs_c] = field->byte_offset / sizeof(void *);
      nonstatic_refs_c += !is_static;
      static_refs_c += is_static;
    }
  }

  // Create static field memory, initializing all to 0
  cd->static_fields = arena_alloc(&cd->arena, static_offset, sizeof(u8));
  cd->instance_bytes = nonstatic_offset;

  // Set up vtable and itables
  set_up_function_tables(cd);

  return 0;
}
