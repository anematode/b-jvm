#include <linkage.h>

#include <vtable.h>
#include <analysis.h>
#include <classfile.h>

#include <bjvm.h>

static size_t allocate_field(size_t *current, bjvm_type_kind kind) {
  size_t result;
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE: {
    result = *current;
    (*current)++;
    break;
  }
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT: {
    *current = (*current + 1) & ~1;
    result = *current;
    *current += 2;
    break;
  }
  case BJVM_TYPE_KIND_FLOAT:
  case BJVM_TYPE_KIND_INT:
#ifdef EMSCRIPTEN
  case BJVM_TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 3) & ~3;
    result = *current;
    *current += 4;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
  case BJVM_TYPE_KIND_LONG:
#ifndef EMSCRIPTEN
  case BJVM_TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 7) & ~7;
    result = *current;
    *current += 8;
    break;

  case BJVM_TYPE_KIND_VOID:
    [[fallthrough]];
  default:
    UNREACHABLE();
  }
  return result;
}

static int link_array_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (classdesc->state >= BJVM_CD_STATE_LINKED)
    return 0;

  bjvm_classdesc_state st = BJVM_CD_STATE_LINKED;
  classdesc->state = st;
  int status = 0;
  if (classdesc->base_component) {
    status = bjvm_link_class(thread, classdesc->base_component);
    if (status) {
      st = BJVM_CD_STATE_LINKAGE_ERROR;
    }
    // Link all higher dimensional components
    bjvm_classdesc *a = classdesc->base_component;
    while (a->array_type) {
      a = a->array_type;
      a->state = st;
    }
  }
  bjvm_set_up_function_tables(classdesc);
  return status;
}

// Link the class.
int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (classdesc->state != BJVM_CD_STATE_LOADED) {
    return 0;
  }
  // Link superclasses
  if (classdesc->super_class) {
    int status = bjvm_link_class(thread, classdesc->super_class->classdesc);
    if (status) {
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }
  // Link superinterfaces
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    int status = bjvm_link_class(thread, classdesc->interfaces[i]->classdesc);
    if (status) {
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }
  if (classdesc->kind != BJVM_CD_KIND_ORDINARY) {
    DCHECK(classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
    return link_array_class(thread, classdesc);
  }
  classdesc->state = BJVM_CD_STATE_LINKED;
  // Link the corresponding array type(s)
  if (classdesc->array_type) {
    link_array_class(thread, classdesc->array_type);
  }
  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < classdesc->methods_count; ++method_i) {
    bjvm_cp_method *method = classdesc->methods + method_i;
    if (method->code) {
      heap_string error_str;
      int result = bjvm_analyze_method_code(method, &error_str);
      if (result != 0) {
        classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
        printf("Error analyzing method %.*s: %.*s\n", method->name.len, method->name.chars, error_str.len,
               error_str.chars);
        // TODO raise VerifyError
        UNREACHABLE();
      }
    }
  }

  // Padding for VM fields (e.g., internal fields used for Reflection)
  int padding =
      (int)(uintptr_t)bjvm_hash_table_lookup(&thread->vm->class_padding, classdesc->name.chars, classdesc->name.len);

  // Assign memory locations to all static/non-static fields
  bjvm_classdesc *super = classdesc->super_class ? classdesc->super_class->classdesc : NULL;
  size_t static_offset = 0, nonstatic_offset = super ? super->instance_bytes : sizeof(bjvm_obj_header);
  nonstatic_offset += padding;
  for (int field_i = 0; field_i < classdesc->fields_count; ++field_i) {
    bjvm_cp_field *field = classdesc->fields + field_i;
    bjvm_type_kind kind = field_to_kind(&field->parsed_descriptor);
    field->byte_offset = field->access_flags & BJVM_ACCESS_STATIC ? allocate_field(&static_offset, kind)
                                                                  : allocate_field(&nonstatic_offset, kind);

#if AGGRESSIVE_DEBUG
    printf("Allocating field %.*s for class %.*s at %d\n", fmt_slice(field->name), fmt_slice(classdesc->name),
           field->byte_offset);
#endif
  }

  bjvm_init_compressed_bitset(&classdesc->static_references, static_offset / sizeof(void *));
  bjvm_init_compressed_bitset(&classdesc->instance_references, nonstatic_offset / sizeof(void *));

  // Add superclass instance references
  if (super) {
    bjvm_compressed_bitset bs = super->instance_references;
    for (size_t i = 0; i < super->instance_bytes / sizeof(void *); ++i) {
      if (bjvm_test_compressed_bitset(bs, i)) {
        bjvm_test_set_compressed_bitset(&classdesc->instance_references, i);
      }
    }
  }
  for (int field_i = 0; field_i < classdesc->fields_count; ++field_i) {
    bjvm_cp_field *field = classdesc->fields + field_i;
    if (field_to_kind(&field->parsed_descriptor) == BJVM_TYPE_KIND_REFERENCE) {
      bjvm_compressed_bitset *bs =
          field->access_flags & BJVM_ACCESS_STATIC ? &classdesc->static_references : &classdesc->instance_references;
      bjvm_test_set_compressed_bitset(bs, field->byte_offset / sizeof(void *));
    }
  }

  // Create static field memory, initializing all to 0
  classdesc->static_fields = calloc(static_offset, 1);
  classdesc->instance_bytes = nonstatic_offset;

  // Set up vtable and itables
  bjvm_set_up_function_tables(classdesc);

  return 0;
}
