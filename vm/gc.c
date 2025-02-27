// Garbage collection

#include "analysis.h"
#include "arrays.h"
#include "bjvm.h"
#include <gc.h>
#include <roundrobin_scheduler.h>

typedef struct gc_ctx {
  vm *vm;

  // Vector of pointer to things to rewrite
  object **roots;

  // If lowest bit is a 1, it's a monitor_data*, otherwise it's an object.
  void **objs;
  object *new_location;
  object *worklist;  // should contain a reachable object exactly once over its lifetime

  object **relocations;
} gc_ctx;

int in_heap(const vm *vm, object field) {
  return (u8 *)field >= vm->heap && (u8 *)field < vm->heap + vm->true_heap_capacity;
}

#define lengthof(x) (sizeof(x) / sizeof(x[0]))
#define PUSH_ROOT(x)                                                                                                   \
  {                                                                                                                    \
    __typeof(x) v = (x);                                                                                               \
    if (*v && in_heap(ctx->vm, (void *)*v)) {                                                                          \
      check_duplicate_root(ctx, (object *)v);                                                                          \
      arrput(ctx->roots, (object *)v);                                                                                 \
    }                                                                                                                  \
  }

void check_duplicate_root([[maybe_unused]] gc_ctx *ctx, [[maybe_unused]] object *root) {
#if 0
  for (int i = 0; i < arrlen(ctx->roots); ++i) {
    if (ctx->roots[i] == root) {
      fprintf(stderr, "Duplicate root %p\n", root);
      CHECK(false);
    }
  }
#endif
}

static void enumerate_reflection_roots(gc_ctx *ctx, classdesc *desc) {
  // Push the mirrors of this base class and all of its array types
  PUSH_ROOT(&desc->classloader);
  PUSH_ROOT(&desc->linkage_error);

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
  PUSH_ROOT(&thr->thread_obj);
  PUSH_ROOT(&thr->current_exception);

  // To prevent double GC roots on shared stuff, we start from the highest address (innermost) frames and walk down,
  // keeping track of the minimum address we have scanned for references.

  uintptr_t min_frame_addr_scanned = UINTPTR_MAX;

  for (int frame_i = arrlen(thr->stack.frames) - 1; frame_i >= 0; --frame_i) {
    stack_frame *raw_frame = thr->stack.frames[frame_i];
    if (is_frame_native(raw_frame))
      continue;
    plain_frame *frame = get_plain_frame(raw_frame);
    code_analysis *analy = raw_frame->method->code_analysis;
    // List of stack and local values which are references
    // In particular, 0 through stack - 1 refer to the stack, and stack through stack + locals - 1
    // refer to the locals array
    stack_summary *ss = analy->stack_states[frame->program_counter];
    int i = 0;
    for (; i < ss->stack; ++i) {
      if (ss->entries[i] != TYPE_KIND_REFERENCE)
        continue;
      object *val = &frame->stack[i].obj;
      if ((uintptr_t)val >= min_frame_addr_scanned) {
        // We already processed this part of the stack as part of the inner frame's locals
        continue;
      }
      PUSH_ROOT(val);
    }

    // Scan the locals
    for (int local_i = 0; i < ss->locals + ss->stack; ++i, ++local_i) {
      if (ss->entries[i] != TYPE_KIND_REFERENCE)
        continue;
      PUSH_ROOT(&frame_locals(raw_frame)[local_i].obj);
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
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&desc)) {
    if (desc->static_references) {
      for (size_t i = 0; i < desc->static_references->count; ++i) {
        u16 offs = desc->static_references->slots_unscaled[i];
        object *root = ((object *)desc->static_fields) + offs;
        PUSH_ROOT(root);
      }
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

  // Interned strings (TODO remove)
  it = hash_table_get_iterator(&vm->interned_strings);
  object str;
  while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&str)) {
    PUSH_ROOT(&it.current->data);
    hash_table_iterator_next(&it);
  }

  // Scheduler roots
  if (vm->scheduler) {
    rr_scheduler_enumerate_gc_roots(vm->scheduler, ctx->roots);
  }
}

static u32 *get_flags(vm *vm, object o) {
  assert(o != nullptr);
  return &get_mark_word(vm, &o->header_word)->data[0];
}

static void mark_reachable(gc_ctx *ctx, object obj) {
  arrput(ctx->objs, obj);
  if (has_expanded_data(&obj->header_word)) {
    arrput(ctx->objs, (void*)((uintptr_t)obj->header_word.expanded_data | 1));
  }

  // Visit all instance fields
  classdesc *desc = obj->descriptor;
  if (desc->kind == CD_KIND_ORDINARY) {
    reference_list *refs = desc->instance_references;
    for (size_t i = 0; i < refs->count; ++i) {
      object field_obj = *((object *)obj + refs->slots_unscaled[i]);
      if (field_obj && in_heap(ctx->vm, field_obj) && !(*get_flags(ctx->vm, field_obj) & IS_REACHABLE)) {
        *get_flags(ctx->vm,field_obj) |= IS_REACHABLE;
        arrput(ctx->worklist, field_obj);
      }
    }
  } else if (desc->kind == CD_KIND_ORDINARY_ARRAY || (desc->kind == CD_KIND_PRIMITIVE_ARRAY && desc->dimensions > 1)) {
    // Visit all components
    int arr_len = ArrayLength(obj);
    for (size_t i = 0; i < (size_t)arr_len; ++i) {
      object arr_element = ReferenceArrayLoad(obj, i);
      if (arr_element && in_heap(ctx->vm, arr_element) && !(*get_flags(ctx->vm,arr_element) & IS_REACHABLE)) {
        *get_flags(ctx->vm,arr_element) |= IS_REACHABLE;
        arrput(ctx->worklist, arr_element);
      }
    }
  }
}

static int comparator(const void *a, const void *b) { return *(char **)a - *(char **)b; }

size_t size_of_object(object obj) {
  if (obj->descriptor->kind == CD_KIND_ORDINARY) {
    return obj->descriptor->instance_bytes;
  }
  if (obj->descriptor->kind == CD_KIND_ORDINARY_ARRAY) {
    return kArrayDataOffset + ArrayLength(obj) * sizeof(void *);
  }
  return kArrayDataOffset + ArrayLength(obj) * sizeof_type_kind(obj->descriptor->primitive_component);
}

static void relocate_object(gc_ctx *ctx, object *obj) {
  if (!*obj)
    return;

  DCHECK(((uintptr_t)obj & 1) == 0);

#if DCHECKS_ENABLED
  arrput(ctx->relocations, obj);
#endif

  // Binary search for obj in ctx->roots
  void **found = bsearch(obj, ctx->objs, arrlen(ctx->objs), sizeof(object), comparator);
  if (found) {
    object relocated = ctx->new_location[found - ctx->objs];
    DCHECK(relocated);
    DCHECK(!((uintptr_t)relocated & 1));
    *obj = relocated;
  } else {
    if (in_heap(ctx->vm, *obj)) {
      // Check for a double relocation
      for (int i = 0; i < arrlen(ctx->relocations); ++i) {
        if (ctx->relocations[i] == obj) {
          fprintf(stderr, "Double relocation.");
          break;
        }
      }
      fprintf(stderr, "Dangling reference! %p %p", obj, *obj);
      CHECK(false);
    }
  }
}

void relocate_instance_fields(gc_ctx *ctx) {
  for (int i = 0; i < arrlen(ctx->objs); ++i) {
    if ((uintptr_t) ctx->objs[i] & 1) {  // monitor
      continue;
    }
    object obj = ctx->new_location[i];

    // Re-map the monitor, if any
    if (has_expanded_data(&obj->header_word)) {
      monitor_data *monitor = obj->header_word.expanded_data;
      void *key = (void*)((uintptr_t)monitor | 1);
      void **found = bsearch(&key, ctx->objs, arrlen(ctx->objs), sizeof(object), comparator);
      if (!found) {
        fprintf(stderr, "Can't find monitor %p!\n", monitor);
        abort();
      }
      obj->header_word.expanded_data = (void*)ctx->new_location[found - ctx->objs];
    }

    if (obj->descriptor->kind == CD_KIND_ORDINARY) {
      classdesc *desc = obj->descriptor;
      reference_list *refs = desc->instance_references;
      for (size_t j = 0; j < refs->count; ++j) {
        object *field = (object *)obj + refs->slots_unscaled[j];
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
}

#if DCHECKS_ENABLED
#define NEW_HEAP_EACH_GC 1
#else
#define NEW_HEAP_EACH_GC 0
#endif

void major_gc(vm *vm) {
  // TODO wait for all threads to get ready (for now we'll just call this from
  // an already-running thread)
  gc_ctx ctx = {.vm = vm};
  major_gc_enumerate_gc_roots(&ctx);

  // Mark phase
  for (int i = 0; i < arrlen(ctx.roots); ++i) {
    object root = *ctx.roots[i];
    if (!in_heap(vm, root) || *get_flags(vm,root) & IS_REACHABLE) // already visited
      continue;
    *get_flags(vm,root) |= IS_REACHABLE;
    arrput(ctx.worklist, root);
  }

  int *bitset[1] = {nullptr};
  while (arrlen(ctx.worklist) > 0) {
    object obj = arrpop(ctx.worklist);
    *get_flags(vm,obj) |= IS_REACHABLE;
    mark_reachable(&ctx, obj);
  }
  arrfree(ctx.worklist);
  arrfree(bitset[0]);

  // Sort roots by address
  qsort(ctx.objs, arrlen(ctx.objs), sizeof(object), comparator);
  object *new_location = ctx.new_location = malloc(arrlen(ctx.objs) * sizeof(object));

  // Create a new heap of the same size so ASAN can enjoy itself
#if NEW_HEAP_EACH_GC
  u8 *new_heap = aligned_alloc(4096, vm->true_heap_capacity), *end = new_heap + vm->true_heap_capacity;
#else
  u8 *new_heap = vm->heap, *end = vm->heap + vm->true_heap_capacity;
#endif

  u8 *write_ptr = new_heap;

  // Copy object by object. Monitors are "objects" with a low bit of 1 to differentiate them.
  for (size_t i = 0; i < arrlenu(ctx.objs); ++i) {
    // Align to 8 bytes
    write_ptr = (u8 *)align_up((uintptr_t)write_ptr, 8);
    object obj = ctx.objs[i];

#if !NEW_HEAP_EACH_GC
    DCHECK((uintptr_t)obj >= (uintptr_t)write_ptr);
#endif

    bool is_monitor = (uintptr_t)obj & 1;
    size_t sz = is_monitor ? sizeof(monitor_data) : size_of_object(obj);
    sz = align_up(sz, 8);
    obj = (void*)((uintptr_t) obj & ~1ULL);  // get the actual underlying pointer
    DCHECK(write_ptr + sz <= end);
    if (!is_monitor) {
      *get_flags(vm, obj) &= ~IS_REACHABLE; // clear the reachable flag
    }
    memmove(write_ptr, obj, sz);      // not memcpy because the heap is the same; overlap is possible
    object new_obj = (object)write_ptr;
    new_location[i] = new_obj;
    write_ptr += sz;
  }

  // Go through all static and instance fields and rewrite in place
  relocate_instance_fields(&ctx);
  for (int i = 0; i < arrlen(ctx.roots); ++i) {
    object *obj = ctx.roots[i];
    relocate_object(&ctx, obj);
  }

  arrfree(ctx.objs);
  free(ctx.new_location);
  arrfree(ctx.roots);
  arrfree(ctx.relocations);

#if NEW_HEAP_EACH_GC
  free(vm->heap);
#endif

  vm->heap = new_heap;
  vm->heap_used = align_up(write_ptr - new_heap, 8);
}