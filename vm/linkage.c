#include <linkage.h>

#include <analysis.h>
#include <classfile.h>
#include <vtable.h>

#include <bjvm.h>

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
    *current = (*current + 1) & ~1;
    result = *current;
    *current += 2;
    break;
  }
  case TYPE_KIND_FLOAT:
  case TYPE_KIND_INT:
#ifdef EMSCRIPTEN
  case TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 3) & ~3;
    result = *current;
    *current += 4;
    break;
  case TYPE_KIND_DOUBLE:
  case TYPE_KIND_LONG:
#ifndef EMSCRIPTEN
  case TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 7) & ~7;
    result = *current;
    *current += 8;
    break;

  case TYPE_KIND_VOID:
    [[fallthrough]];
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
    // java/lang/Object has a superclass hierarchy of length 1, containing only itself
    cd->hierarchy_len = 1;
    cd->hierarchy = arena_alloc(&cd->arena, 1, sizeof(classdesc *));
  }

  // Place ourselves into the hierarchy
  cd->hierarchy[cd->hierarchy_len - 1] = cd;
}

static int link_array_class(vm_thread *thread, classdesc *cd) {
  if (cd->state >= CD_STATE_LINKED)
    return 0;

  classdesc_state st = CD_STATE_LINKED;
  cd->state = st;
  int status = 0;
  if (cd->base_component) {
    status = link_class(thread, cd->base_component);
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
    int size = sizeof_type_kind(field_to_kind(&field->parsed_descriptor));
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

// Link the class.
int link_class(vm_thread *thread, classdesc *cd) {
  if (cd->state != CD_STATE_LOADED) {
    return 0;
  }
  // Link superclasses
  if (cd->super_class) {
    classdesc *super = cd->super_class->classdesc;
    int status = link_class(thread, super);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      return status;
    }
  } else {
    assert(utf8_equals(cd->name, "java/lang/Object"));
  }

  setup_super_hierarchy(cd);

  // Link superinterfaces
  for (int i = 0; i < cd->interfaces_count; ++i) {
    int status = link_class(thread, cd->interfaces[i]->classdesc);
    if (status) {
      cd->state = CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }
  if (cd->kind != CD_KIND_ORDINARY) {
    DCHECK(cd->kind == CD_KIND_ORDINARY_ARRAY);
    return link_array_class(thread, cd);
  }
  cd->state = CD_STATE_LINKED;
  // Link the corresponding array type(s)
  if (cd->array_type) {
    link_array_class(thread, cd->array_type);
  }
  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < cd->methods_count; ++method_i) {
    cp_method *method = cd->methods + method_i;
    if (method->code) {
      heap_string error_str;
      int result = analyze_method_code(method, &error_str);
      if (result != 0) {
        cd->state = CD_STATE_LINKAGE_ERROR;
        printf("Error analyzing method %.*s: %.*s\n", method->name.len, method->name.chars, error_str.len,
               error_str.chars);
        // TODO raise VerifyError
        UNREACHABLE();
      }
    }
  }

  // Padding for VM fields (e.g., internal fields used for Reflection)
  int padding = (int)(uintptr_t)hash_table_lookup(&thread->vm->class_padding, cd->name.chars, cd->name.len);

  // Assign memory locations to all static/non-static fields
  classdesc *super = cd->super_class ? cd->super_class->classdesc : NULL;
  size_t static_offset = 0, nonstatic_offset = super ? super->instance_bytes : sizeof(obj_header);
  nonstatic_offset += padding;

  bool must_have_C_layout = hash_table_contains(&thread->vm->class_padding, cd->name.chars, cd->name.len);
  int *order = reorder_fields_for_compactness(cd->fields, cd->fields_count);

  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + (must_have_C_layout ? field_i : order[field_i]);
    type_kind kind = field_to_kind(&field->parsed_descriptor);
    field->byte_offset = field->access_flags & ACCESS_STATIC ? allocate_field(&static_offset, kind)
                                                             : allocate_field(&nonstatic_offset, kind);

#if AGGRESSIVE_DEBUG
    printf("Allocating field %.*s for class %.*s at %zu\n", fmt_slice(field->name), fmt_slice(cd->name),
           field->byte_offset);
#endif
  }
  free(order);

  init_compressed_bitset(&cd->static_references, static_offset / sizeof(void *));
  init_compressed_bitset(&cd->instance_references, nonstatic_offset / sizeof(void *));

  // Add superclass instance references
  if (super) {
    compressed_bitset bs = super->instance_references;
    for (size_t i = 0; i < super->instance_bytes / sizeof(void *); ++i) {
      if (test_compressed_bitset(bs, i)) {
        test_set_compressed_bitset(&cd->instance_references, i);
      }
    }
  }
  for (int field_i = 0; field_i < cd->fields_count; ++field_i) {
    cp_field *field = cd->fields + field_i;
    if (field_to_kind(&field->parsed_descriptor) == TYPE_KIND_REFERENCE) {
      compressed_bitset *bs = field->access_flags & ACCESS_STATIC ? &cd->static_references : &cd->instance_references;
      test_set_compressed_bitset(bs, field->byte_offset / sizeof(void *));
    }
  }

  // Create static field memory, initializing all to 0
  cd->static_fields = calloc(static_offset, 1);
  cd->instance_bytes = nonstatic_offset;

  // Set up vtable and itables
  set_up_function_tables(cd);

  return 0;
}
