// Garbage collection

#include "analysis.h"
#include "arrays.h"
#include "bjvm.h"
#include <gc.h>

typedef struct bjvm_gc_ctx {
  bjvm_vm *vm;

  // Vector of pointer to pointer to things to rewrite
  bjvm_obj_header ***roots;
  int roots_count;
  int roots_cap;

  bjvm_obj_header **objs;
  int objs_count;
  int objs_cap;

  bjvm_obj_header **new_location;
} bjvm_gc_ctx;

#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#define PUSH_ROOT(x)                                                           \
  {                                                                            \
    __typeof(x) v = (x);                                                       \
    if (*v) {                                                                  \
      printf("Pushing root %p %d\n", *v, __LINE__);                                         \
      *VECTOR_PUSH(ctx->roots, ctx->roots_count, ctx->roots_cap) =             \
          (bjvm_obj_header **)v;                                               \
    }                                                                          \
  }

static void enumerate_reflection_roots(bjvm_gc_ctx *ctx, bjvm_classdesc *desc) {
  // Push the mirrors of this base class and all of its array types
  bjvm_classdesc *array = desc;
  while (array) {
    PUSH_ROOT(&array->mirror);
    PUSH_ROOT(&array->cp_mirror);
    array = array->array_type;
  }

  // Push all the method mirrors
  for (int i = 0; i < desc->methods_count; ++i) {
    PUSH_ROOT(&desc->methods[i].reflection_method);
    PUSH_ROOT(&desc->methods[i].reflection_ctor);
    PUSH_ROOT(&desc->methods[i].method_type_obj);
  }

  // Push all the field mirrors
  for (int i = 0; i < desc->fields_count; ++i) {
    PUSH_ROOT(&desc->fields[i].reflection_field);
  }

  // Push all resolved MethodType objects
  if (desc->pool) {
    for (int i = 0; i < desc->pool->entries_len; ++i) {
      bjvm_cp_entry *ent = desc->pool->entries + i;
      if (ent->kind == BJVM_CP_KIND_METHOD_HANDLE) {
        PUSH_ROOT(&ent->method_handle.resolved_mt);
      } else if (ent->kind == BJVM_CP_KIND_INVOKE_DYNAMIC) {
        PUSH_ROOT(&ent->indy_info.resolved_mt);
      } else if (ent->kind == BJVM_CP_KIND_METHOD_TYPE) {
        PUSH_ROOT(&ent->method_type.resolved_mt);
      } else if (ent->kind == BJVM_CP_KIND_STRING) {
        PUSH_ROOT(&ent->string.interned);
      } else if (ent->kind == BJVM_CP_KIND_CLASS) {
        PUSH_ROOT(&ent->class_info.vm_object);
      }
    }
  }

  // Push all CallSite objects
  for (int i = 0; i < arrlen(desc->indy_insns); ++i) {
    bjvm_bytecode_insn *insn = desc->indy_insns[i];
    PUSH_ROOT(&insn->ic);
  }

  // Push all ICed method type objects
  for (int i = 0; i < arrlen(desc->sigpoly_insns); ++i) {
    bjvm_bytecode_insn *insn = desc->sigpoly_insns[i];
    PUSH_ROOT(&insn->ic2);
  }
}

static void push_thread_roots(bjvm_gc_ctx *ctx, bjvm_thread *thr) {
  int *bitset_list = NULL, bs_list_len = 0, bs_list_cap = 0;

  PUSH_ROOT(&thr->thread_obj);
  PUSH_ROOT(&thr->current_exception);

  // To prevent double GC roots on shared stuff, we start from the highest address (innermost) frames and walk down,
  // keeping track of the minimum address we have scanned for references.

  uintptr_t min_frame_addr_scanned = UINTPTR_MAX;

  for (int frame_i = thr->frames_count - 1; frame_i >= 0; --frame_i) {
    bjvm_stack_frame *raw_frame = thr->frames[frame_i];
    if (bjvm_is_frame_native(raw_frame))
      continue;
    bjvm_plain_frame *frame = bjvm_get_plain_frame(raw_frame);
    bjvm_code_analysis *analy = raw_frame->method->code_analysis;
    bjvm_compressed_bitset refs =
        analy->insn_index_to_references[frame->program_counter];
    // List of stack and local values which are references
    // In particular, 0 through max_stack - 1 refer to the stack, and max_stack through max_stack + max_locals - 1
    // refer to the locals array
    bitset_list = bjvm_list_compressed_bitset_bits(refs, bitset_list,
                                                   &bs_list_len, &bs_list_cap);
    // Scan the stack
    int i = 0;
    int max_stack = raw_frame->plain.max_stack;
    for (; i < bs_list_len && bitset_list[i] < max_stack; ++i) {
      bjvm_obj_header **val = &frame->stack[bitset_list[i]].obj;
      if ((uintptr_t)val >= min_frame_addr_scanned) {
        // We already processed this part of the stack as part of the inner frame's locals
        continue;
      }
      PUSH_ROOT(val);
    }

    // Scan the locals
    for (; i < bs_list_len; ++i) {
      PUSH_ROOT(&frame_locals(raw_frame)[bitset_list[i] - max_stack].obj);
    }

    min_frame_addr_scanned = (uintptr_t)frame_locals(raw_frame);
  }

  // Non-null local handles
  for (int i = 0; i < thr->handles_capacity; ++i) {
    PUSH_ROOT(&thr->handles[i].obj);
  }

  // Preallocated exceptions
  PUSH_ROOT(&thr->out_of_mem_error);
  PUSH_ROOT(&thr->stack_overflow_error);

  free(bitset_list);
}

static void bjvm_major_gc_enumerate_gc_roots(bjvm_gc_ctx *ctx) {
  bjvm_vm *vm = ctx->vm;
  if (vm->primitive_classes[0]) {
    for (size_t i = 0; i < lengthof(vm->primitive_classes); ++i) {
      enumerate_reflection_roots(ctx, vm->primitive_classes[i]);
    }
  }

  // JS Handles
  for (int i = 0; i < arrlen(vm->js_handles); ++i) {
    PUSH_ROOT(&vm->js_handles[i]);
  }

  // Static fields of bootstrap-loaded classes
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&vm->classes);
  char *key;
  size_t key_len;
  bjvm_classdesc *desc;
  int *bitset_list = NULL, bs_list_len = 0, bs_list_cap = 0;
  while (
      bjvm_hash_table_iterator_has_next(it, &key, &key_len, (void **)&desc)) {
    bitset_list = bjvm_list_compressed_bitset_bits(
        desc->static_references, bitset_list, &bs_list_len, &bs_list_cap);
    for (int i = 0; i < bs_list_len; ++i) {
      PUSH_ROOT(((bjvm_obj_header **)desc->static_fields) + bitset_list[i]);
    }

    // Also, push things like Class, Method and Constructors
    enumerate_reflection_roots(ctx, desc);
    bjvm_hash_table_iterator_next(&it);
  }

  // main thread group
  PUSH_ROOT(&vm->main_thread_group);

  // Modules
  it = bjvm_hash_table_get_iterator(&vm->modules);
  bjvm_module *module;
  while (bjvm_hash_table_iterator_has_next(it, &key, &key_len,
                                           (void **)&module)) {
    PUSH_ROOT(&module->reflection_object);
    bjvm_hash_table_iterator_next(&it);
  }

  // Stack and local variables on active threads
  for (int thread_i = 0; thread_i < vm->active_thread_count; ++thread_i) {
    bjvm_thread *thr = vm->active_threads[thread_i];
    push_thread_roots(ctx, thr);
  }

  free(bitset_list);

  // Interned strings (TODO remove)
  it = bjvm_hash_table_get_iterator(&vm->interned_strings);
  bjvm_obj_header *str;
  while (bjvm_hash_table_iterator_has_next(it, &key, &key_len, (void **)&str)) {
    PUSH_ROOT(&it.current->data);
    bjvm_hash_table_iterator_next(&it);
  }
}

uint64_t REACHABLE_BIT = 1ULL << 33;

static int in_heap(bjvm_gc_ctx *ctx, bjvm_obj_header *field) {
  return (uintptr_t)field - (uintptr_t)ctx->vm->heap <
         ctx->vm->true_heap_capacity;
}

static void bjvm_mark_reachable(bjvm_gc_ctx *ctx, bjvm_obj_header *obj, int **bitsets,
                         int *capacities, int depth) {
  obj->mark_word |= REACHABLE_BIT;
  *VECTOR_PUSH(ctx->objs, ctx->objs_count, ctx->objs_cap) = obj;

  // Visit all instance fields
  bjvm_classdesc *desc = obj->descriptor;
  int len = 0;
  if (desc->kind == BJVM_CD_KIND_ORDINARY) {
    bjvm_compressed_bitset bits = desc->instance_references;
    int **bitset = &bitsets[depth];
    *bitset = bjvm_list_compressed_bitset_bits(bits, *bitset, &len,
                                               &capacities[depth]);
    for (int i = 0; i < len; ++i) {
      bjvm_obj_header *field = *((bjvm_obj_header **)obj + (*bitset)[i]);
      if (field && !(field->mark_word & REACHABLE_BIT) && in_heap(ctx, field)) {
        // Visiting instance field at offset on class
        bjvm_mark_reachable(ctx, field, bitsets, capacities, depth + 1);
      }
    }
  } else if (desc->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
             (desc->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY &&
              desc->dimensions > 1)) {
    // Visit all components
    int len = *ArrayLength(obj);
    for (int i = 0; i < len; ++i) {
      bjvm_obj_header *field = *((bjvm_obj_header **)ArrayData(obj) + i);
      if (field && !(field->mark_word & REACHABLE_BIT) && in_heap(ctx, field)) {
        bjvm_mark_reachable(ctx, field, bitsets, capacities, depth + 1);
      }
    }
  }
}

static int comparator(const void *a, const void *b) {
  return *(bjvm_obj_header **)a - *(bjvm_obj_header **)b;
}

static size_t size_of_object(bjvm_obj_header *obj) {
  if (obj->descriptor->kind == BJVM_CD_KIND_ORDINARY) {
    return obj->descriptor->instance_bytes;
  }
  if (obj->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
      (obj->descriptor->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY &&
       obj->descriptor->dimensions > 1)) {
    return kArrayDataOffset + *ArrayLength(obj) * sizeof(void *);
  }
  return kArrayDataOffset +
         *ArrayLength(obj) *
             sizeof_type_kind(obj->descriptor->primitive_component);
}

static void relocate_object(const bjvm_gc_ctx *ctx, bjvm_obj_header **obj) {
  if (!*obj)
    return;

  // Binary search for obj in ctx->roots
  bjvm_obj_header **found = (bjvm_obj_header **)bsearch(
      obj, ctx->objs, ctx->objs_count, sizeof(bjvm_obj_header *), comparator);
  if (found) {
    *obj = ctx->new_location[found - ctx->objs];
  }
}

static void relocate_static_fields(bjvm_gc_ctx *ctx) {
  bjvm_vm *vm = ctx->vm;

  // Static fields of bootstrap-loaded classes
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&vm->classes);
  char *key;
  size_t key_len;
  bjvm_classdesc *desc;
  int *bitset_list = NULL, bs_list_len = 0, bs_list_cap = 0;
  while (
      bjvm_hash_table_iterator_has_next(it, &key, &key_len, (void **)&desc)) {
    bitset_list = bjvm_list_compressed_bitset_bits(
        desc->static_references, bitset_list, &bs_list_len, &bs_list_cap);
    for (int i = 0; i < bs_list_len; ++i) {
      relocate_object(ctx, ((bjvm_obj_header **)desc->static_fields) +
                               bitset_list[i]);
    }
    // Push the mirrors of this base class and all of its array types
    bjvm_classdesc *array = desc;
    while (array) {
      relocate_object(ctx, (bjvm_obj_header **)&array->mirror);
      array = array->array_type;
    }
    bjvm_hash_table_iterator_next(&it);
  }
  free(bitset_list);
}

void relocate_instance_fields(bjvm_gc_ctx *ctx) {
  int len = 0, cap = 0;
  int *bitset = NULL;
  for (int i = 0; i < ctx->objs_count; ++i) {
    bjvm_obj_header *obj = ctx->new_location[i];
    if (obj->descriptor->kind == BJVM_CD_KIND_ORDINARY) {
      bjvm_classdesc *desc = obj->descriptor;
      bjvm_compressed_bitset bits = desc->instance_references;
      len = 0;
      bitset = bjvm_list_compressed_bitset_bits(bits, bitset, &len, &cap);
      for (int i = 0; i < len; ++i) {
        bjvm_obj_header **field = (bjvm_obj_header **)obj + bitset[i];
        relocate_object(ctx, field);
      }
    } else if (obj->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY ||
               (obj->descriptor->kind == BJVM_CD_KIND_PRIMITIVE_ARRAY &&
                obj->descriptor->dimensions > 1)) {
      int len = *ArrayLength(obj);
      for (int i = 0; i < len; ++i) {
        bjvm_obj_header **field = (bjvm_obj_header **)ArrayData(obj) + i;
        relocate_object(ctx, field);
      }
    }
  }
  free(bitset);
}

void bjvm_major_gc(bjvm_vm *vm) {
  // TODO wait for all threads to get ready (for now we'll just call this from
  // an already-running thread)
  bjvm_gc_ctx ctx = {.vm = vm};
  bjvm_major_gc_enumerate_gc_roots(&ctx);

  // Mark phase
  int *bitset_list[1000] = {nullptr}, capacity[1000] = {0};
  for (int i = 0; i < ctx.roots_count; ++i) {
    bjvm_obj_header *root = *ctx.roots[i];
    printf("Root: %p\n", root);
    if (!(root->mark_word & REACHABLE_BIT))
      bjvm_mark_reachable(&ctx, root, bitset_list, capacity, 0);
  }
  for (int i = 0; i < 1000; ++i) {
    free(bitset_list[i]);
  }

  // Sort roots by address
  qsort(ctx.objs, ctx.objs_count, sizeof(bjvm_obj_header *), comparator);
  bjvm_obj_header **new_location = ctx.new_location =
      malloc(ctx.objs_count * sizeof(bjvm_obj_header *));

  // For now, create a new heap of the same size
  [[maybe_unused]] uint8_t *new_heap = aligned_alloc(4096, vm->true_heap_capacity),
          *end = new_heap + vm->true_heap_capacity;
  uint8_t *write_ptr = new_heap;

  // Copy object by object
  for (int i = 0; i < ctx.objs_count; ++i) {
    // Align to 8 bytes
    write_ptr = (uint8_t *)(align_up((uintptr_t)write_ptr, 8));
    bjvm_obj_header *obj = ctx.objs[i];
    size_t sz = size_of_object(obj);

    assert(write_ptr + sz <= end);

    obj->mark_word &= ~REACHABLE_BIT;
    memcpy(write_ptr, obj, sz);

    new_location[i] = (bjvm_obj_header *)write_ptr;
    write_ptr += sz;
  }

  // Go through all static and instance fields and rewrite in place
  relocate_instance_fields(&ctx);
  relocate_static_fields(&ctx);
  for (int i = 0; i < ctx.roots_count; ++i) {
    bjvm_obj_header **obj = ctx.roots[i];
    relocate_object(&ctx, obj);
  }

  free(ctx.objs);
  free(ctx.new_location);
  free(ctx.roots);

  free(vm->heap);

  vm->heap = new_heap;
  vm->heap_used = align_up(write_ptr - new_heap, 8);
}