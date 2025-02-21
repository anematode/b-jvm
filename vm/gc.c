// Garbage collection

#include "analysis.h"
#include "arrays.h"
#include "bjvm.h"
#include <gc.h>

typedef struct gc_ctx {
  vm *vm;

  // Vector of pointer to pointer to things to rewrite
  object **roots;
  object *objs;
  object *new_location;
} gc_ctx;

static int in_heap(gc_ctx *ctx, object field) {
  return (u8*)field >= ctx->vm->heap && (u8*)field < ctx->vm->heap + ctx->vm->true_heap_capacity;
}

#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#define PUSH_ROOT(x)                                                                                                   \
  {                                                                                                                    \
    __typeof(x) v = (x);                                                                                               \
    if (*v && in_heap(ctx, (void *)*v)) {                                                                              \
      arrput(ctx->roots, (object *)v);                                                                            \
    }                                                                                                                  \
  }

static void enumerate_reflection_roots(gc_ctx *ctx, classdesc *desc) {
  // Push the mirrors of this base class and all of its array types
  PUSH_ROOT(&desc->classloader);

  classdesc *array = desc;
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
      cp_entry *ent = desc->pool->entries + i;
      if (ent->kind == CP_KIND_METHOD_HANDLE) {
        PUSH_ROOT(&ent->method_handle.resolved_mt);
      } else if (ent->kind == CP_KIND_INVOKE_DYNAMIC) {
        PUSH_ROOT(&ent->indy_info.resolved_mt);
      } else if (ent->kind == CP_KIND_METHOD_TYPE) {
        PUSH_ROOT(&ent->method_type.resolved_mt);
      } else if (ent->kind == CP_KIND_STRING) {
        PUSH_ROOT(&ent->string.interned);
      } else if (ent->kind == CP_KIND_CLASS) {
        PUSH_ROOT(&ent->class_info.vm_object);
      }
    }
  }

  // Push all CallSite objects
  for (int i = 0; i < arrlen(desc->indy_insns); ++i) {
    bytecode_insn *insn = desc->indy_insns[i];
    PUSH_ROOT(&insn->ic);
  }

  // Push all ICed method type objects
  for (int i = 0; i < arrlen(desc->sigpoly_insns); ++i) {
    bytecode_insn *insn = desc->sigpoly_insns[i];
    PUSH_ROOT(&insn->ic2);
  }
}

static void push_thread_roots(gc_ctx *ctx, vm_thread *thr) {
  int *bitset_list = NULL;

  PUSH_ROOT(&thr->thread_obj);
  PUSH_ROOT(&thr->current_exception);

  // To prevent double GC roots on shared stuff, we start from the highest address (innermost) frames and walk down,
  // keeping track of the minimum address we have scanned for references.

  uintptr_t min_frame_addr_scanned = UINTPTR_MAX;

  for (int frame_i = arrlen(thr->frames) - 1; frame_i >= 0; --frame_i) {
    stack_frame *raw_frame = thr->frames[frame_i];
    if (is_frame_native(raw_frame))
      continue;
    plain_frame *frame = get_plain_frame(raw_frame);
    code_analysis *analy = raw_frame->method->code_analysis;
    compressed_bitset refs = analy->insn_index_to_references[frame->program_counter];
    // List of stack and local values which are references
    // In particular, 0 through max_stack - 1 refer to the stack, and max_stack through max_stack + max_locals - 1
    // refer to the locals array
    list_compressed_bitset_bits(refs, &bitset_list);
    // Scan the stack
    int i = 0;
    int max_stack = raw_frame->plain.max_stack;
    for (; i < arrlen(bitset_list) && bitset_list[i] < max_stack; ++i) {
      object *val = &frame->stack[bitset_list[i]].obj;
      if ((uintptr_t)val >= min_frame_addr_scanned) {
        // We already processed this part of the stack as part of the inner frame's locals
        continue;
      }
      PUSH_ROOT(val);
    }

    // Scan the locals
    for (; i < arrlen(bitset_list); ++i) {
      PUSH_ROOT(&frame_locals(raw_frame)[bitset_list[i] - max_stack].obj);
    }

    min_frame_addr_scanned = (uintptr_t)frame_locals(raw_frame);
  }

  // Non-null local handles
  for (int i = 0; i < thr->handles_capacity; ++i) {
    PUSH_ROOT(&thr->handles[i].obj);
  }

  // Pred exceptions
  PUSH_ROOT(&thr->out_of_mem_error);
  PUSH_ROOT(&thr->stack_overflow_error);

  arrfree(bitset_list);
}

static void major_gc_enumerate_gc_roots(gc_ctx *ctx) {
  vm *vm = ctx->vm;
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
  hash_table_iterator it = hash_table_get_iterator(&vm->classes);
  char *key;
  size_t key_len;
  classdesc *desc;
  int *bitset_list = NULL;
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&desc)) {
    list_compressed_bitset_bits(desc->static_references, &bitset_list);
    for (int i = 0; i < arrlen(bitset_list); ++i) {
      object *root = ((object *)desc->static_fields) + bitset_list[i];
      PUSH_ROOT(root);
    }

    // Also, push things like Class, Method and Constructors
    enumerate_reflection_roots(ctx, desc);
    hash_table_iterator_next(&it);
  }

  // main thread group
  PUSH_ROOT(&vm->main_thread_group);

  // Modules
  it = hash_table_get_iterator(&vm->modules);
  module *module;
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&module)) {
    PUSH_ROOT(&module->reflection_object);
    hash_table_iterator_next(&it);
  }

  // Stack and local variables on active threads
  for (int thread_i = 0; thread_i < arrlen(vm->active_threads); ++thread_i) {
    vm_thread *thr = vm->active_threads[thread_i];
    push_thread_roots(ctx, thr);
  }

  arrfree(bitset_list);

  // Interned strings (TODO remove)
  it = hash_table_get_iterator(&vm->interned_strings);
  object str;
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&str)) {
    PUSH_ROOT(&it.current->data);
    hash_table_iterator_next(&it);
  }
}

static u32 *get_flags(object o) {
  assert(o != nullptr);
  return &get_mark_word(&o->header_word)->data[0];
}

static void mark_reachable(gc_ctx *ctx, object obj, int **bitsets, int depth) {
  *get_flags(obj) |= IS_REACHABLE;
  arrput(ctx->objs, obj);

  // Visit all instance fields
  classdesc *desc = obj->descriptor;
  if (desc->kind == CD_KIND_ORDINARY) {
    compressed_bitset bits = desc->instance_references;
    int **bitset = &bitsets[depth];
    list_compressed_bitset_bits(bits, bitset);
    for (int i = 0; i < arrlen(*bitset); ++i) {
      object field_obj = *((object *)obj + (*bitset)[i]);
      if (field_obj && !(*get_flags(field_obj) & IS_REACHABLE) && in_heap(ctx, field_obj)) {
        // Visiting instance field at offset on class
        mark_reachable(ctx, field_obj, bitsets, depth + 1);
      }
    }
  } else if (desc->kind == CD_KIND_ORDINARY_ARRAY || (desc->kind == CD_KIND_PRIMITIVE_ARRAY && desc->dimensions > 1)) {
    // Visit all components
    int arr_len = ArrayLength(obj);
    for (int i = 0; i < arr_len; ++i) {
      object arr_element = ReferenceArrayLoad(obj, i);
      if (arr_element && !(*get_flags(arr_element) & IS_REACHABLE) && in_heap(ctx, arr_element)) {
        mark_reachable(ctx, arr_element, bitsets, depth + 1);
      }
    }
  }
}

static int comparator(const void *a, const void *b) { return *(object *)a - *(object *)b; }

static size_t size_of_object(object obj) {
  if (obj->descriptor->kind == CD_KIND_ORDINARY) {
    return obj->descriptor->instance_bytes;
  }
  if (obj->descriptor->kind == CD_KIND_ORDINARY_ARRAY ||
      (obj->descriptor->kind == CD_KIND_PRIMITIVE_ARRAY && obj->descriptor->dimensions > 1)) {
    return kArrayDataOffset + ArrayLength(obj) * sizeof(void *);
  }
  return kArrayDataOffset + ArrayLength(obj) * sizeof_type_kind(obj->descriptor->primitive_component);
}

static void relocate_object(const gc_ctx *ctx, object *obj) {
  if (!*obj)
    return;

  // Binary search for obj in ctx->roots
  object *found = (object *)bsearch(obj, ctx->objs, arrlen(ctx->objs), sizeof(object ), comparator);
  if (found) {
    *obj = ctx->new_location[found - ctx->objs];
  }
}

static void relocate_static_fields(gc_ctx *ctx) {
  vm *vm = ctx->vm;

  // Static fields of bootstrap-loaded classes
  hash_table_iterator it = hash_table_get_iterator(&vm->classes);
  char *key;
  size_t key_len;
  classdesc *desc;
  int *bitset_list = NULL;
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&desc)) {
    list_compressed_bitset_bits(desc->static_references, &bitset_list);
    for (int i = 0; i < arrlen(bitset_list); ++i) {
      relocate_object(ctx, ((object *)desc->static_fields) + bitset_list[i]);
    }
    // Push the mirrors of this base class and all of its array types
    classdesc *array = desc;
    while (array) {
      relocate_object(ctx, (object *)&array->mirror);
      array = array->array_type;
    }
    hash_table_iterator_next(&it);
  }
  arrfree(bitset_list);
}

void relocate_instance_fields(gc_ctx *ctx) {
  int *bitset = nullptr;
  for (int i = 0; i < arrlen(ctx->objs); ++i) {
    object obj = ctx->new_location[i];
    if (obj->descriptor->kind == CD_KIND_ORDINARY) {
      classdesc *desc = obj->descriptor;
      compressed_bitset bits = desc->instance_references;
      list_compressed_bitset_bits(bits, &bitset);
      for (int j = 0; j < arrlen(bitset); ++j) {
        object *field = (object *)obj + bitset[j];
        relocate_object(ctx, field);
      }
    } else if (obj->descriptor->kind == CD_KIND_ORDINARY_ARRAY ||
               (obj->descriptor->kind == CD_KIND_PRIMITIVE_ARRAY && obj->descriptor->dimensions > 1)) {
      int arr_len = ArrayLength(obj);
      for (int j = 0; j < arr_len; ++j) {
        object *field = (object *)ArrayData(obj) + j;
        relocate_object(ctx, field);
      }
    }
  }
  arrfree(bitset);
}

void major_gc(vm *vm) {
  // TODO wait for all threads to get ready (for now we'll just call this from
  // an already-running thread)
  gc_ctx ctx = {.vm = vm};
  major_gc_enumerate_gc_roots(&ctx);

  // Mark phase
  int *bitset_list[1000] = {nullptr};
  for (int i = 0; i < arrlen(ctx.roots); ++i) {
    object root = *ctx.roots[i];
    if (!(*get_flags(root) & IS_REACHABLE))
      mark_reachable(&ctx, root, bitset_list, 0);
  }
  for (int i = 0; i < 1000; ++i) {
    arrfree(bitset_list[i]);
  }

  // Sort roots by address
  qsort(ctx.objs, arrlen(ctx.objs), sizeof(object ), comparator);
  object *new_location = ctx.new_location = malloc(arrlen(ctx.objs) * sizeof(object ));

  // For now, create a new heap of the same size
  [[maybe_unused]] u8 *new_heap = aligned_alloc(4096, vm->true_heap_capacity), *end = new_heap + vm->true_heap_capacity;
  u8 *write_ptr = new_heap;

  // Copy object by object
  for (int i = 0; i < arrlen(ctx.objs); ++i) {
    // Align to 8 bytes
    write_ptr = (u8 *)(align_up((uintptr_t)write_ptr, 8));
    object obj = ctx.objs[i];
    size_t sz = size_of_object(obj);

    DCHECK(write_ptr + sz <= end);

    *get_flags(obj) &= ~IS_REACHABLE; // clear the reachable flag
    memcpy(write_ptr, obj, sz);

    object new_obj = (object )write_ptr;
    new_location[i] = new_obj;
    write_ptr += sz;

    if (has_expanded_data(&obj->header_word)) {
      // Copy the expanded data (align to 8 bytes for atomic ops to be happy)
      write_ptr = (u8 *)align_up((uintptr_t)write_ptr, 8);
      constexpr size_t monitor_data_size = sizeof(*obj->header_word.expanded_data);
      DCHECK(write_ptr + monitor_data_size <= end);

      memcpy(write_ptr, obj->header_word.expanded_data, monitor_data_size);
      new_obj->header_word.expanded_data = (monitor_data *)write_ptr;
      write_ptr += monitor_data_size;
    }
  }

  // Go through all static and instance fields and rewrite in place
  relocate_instance_fields(&ctx);
  relocate_static_fields(&ctx);
  for (int i = 0; i < arrlen(ctx.roots); ++i) {
    object *obj = ctx.roots[i];
    relocate_object(&ctx, obj);
  }

  arrfree(ctx.objs);
  free(ctx.new_location);
  arrfree(ctx.roots);

  free(vm->heap);

  vm->heap = new_heap;
  vm->heap_used = align_up(write_ptr - new_heap, 8);
}