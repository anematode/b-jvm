#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <zlib.h>

#include "analysis.h"
#include "arrays.h"
#include "objects.h"
#include "util.h"
#include <config.h>
#include <exceptions.h>
#include <gc.h>
#include <reflection.h>

#include "cached_classdescs.h"
#include <errno.h>
#include <linkage.h>
#include <monitors.h>
#include <profiler.h>
#include <sys/mman.h>

/// Looks up a class and initializes it if it needs to be initialized.
/// This will abort() if the class's <clinit> either throws an excpetion or
/// tries to suspend -- so be VERY careful when calling this.
#define LoadClassSynchronous(thread, name)                                                                             \
  ({                                                                                                                   \
    obj_header *exception = thread->current_exception;                                                                 \
    thread->current_exception = nullptr;                                                                               \
    classdesc *c = bootstrap_lookup_class(thread, STR(name));                                                          \
    AWAIT_READY(initialize_class, thread, c);                                                                          \
    if (unlikely(thread->current_exception)) {                                                                         \
      UNREACHABLE("exception thrown in LoadClassSynchronous");                                                         \
    }                                                                                                                  \
    thread->current_exception = exception;                                                                             \
    c;                                                                                                                 \
  })

DECLARE_ASYNC(int, init_cached_classdescs,
  locals(
    classdesc * *cached_classdescs;
    u16 i;
  ),
  arguments(vm_thread *thread),
  invoked_methods(
    invoked_method(initialize_class)
  )
);

DEFINE_ASYNC(init_cached_classdescs) {
  DCHECK(!args->thread->vm->_cached_classdescs && "Cached classdescs already initialized");

  self->cached_classdescs = malloc(cached_classdesc_count * sizeof(classdesc *));
  if (!self->cached_classdescs) {
    out_of_memory(args->thread);
    ASYNC_RETURN(-1);
  }

  for (self->i = 0; self->i < cached_classdesc_count; self->i++) {
    char const *name = cached_classdesc_paths[self->i];
    self->cached_classdescs[self->i] = bootstrap_lookup_class(args->thread, str_to_utf8(name));

    AWAIT(initialize_class, args->thread, self->cached_classdescs[self->i]);
    int result = get_async_result(initialize_class);
    if (result != 0) {
      free(self->cached_classdescs); // TODO this is not at all safe
      ASYNC_RETURN(result);
    }
  }

  // Populate the field
  args->thread->vm->_cached_classdescs = (struct cached_classdescs *)self->cached_classdescs;
  ASYNC_END(0);
}

inline int set_unpark_permit(vm_thread *thread) {
  if (unlikely(!thread))
    return -1;
  thread->unpark_permit = true;
  return 0;
}

inline bool query_unpark_permit(vm_thread *thread) { return thread->unpark_permit; }

inline bool should_gc_pause(vm *vm) {
  return __atomic_load_n(&vm->should_gc_pause, __ATOMIC_ACQUIRE);
}

bool has_expanded_data(header_word *data) { return !((uintptr_t)data->expanded_data & IS_MARK_WORD); }

mark_word_t *get_mark_word(vm *vm, header_word *data) {
  mark_word_t *word = has_expanded_data(data) ? &data->expanded_data->mark_word : &data->mark_word;
#if DCHECKS_ENABLED
  if (!in_heap(vm, (void *)word)) {
    // Data got corrupted. Print out information
    fprintf(stderr, "Corrupted mark word: %p at %p (expanded data: %d)\n", word, data, has_expanded_data(data));
    fprintf(stderr, "Surrounding bytes (-16 to +16): ");
    for (int i = -16; i < 16; i++) {
      fprintf(stderr, "%02x ", ((u8 *)data)[i]);
    }
    fprintf(stderr, "\n");
    abort();
  }
#endif
  return word;
}

monitor_data *inspect_monitor(header_word *data) { return has_expanded_data(data) ? data->expanded_data : nullptr; }

monitor_data *allocate_monitor_for(vm_thread *thread, obj_header *obj) {
  CHECK(in_heap(thread->vm, obj)); // if you're synchronizing on staticFieldBase, you deserve the chair
  monitor_data *data = bump_allocate(thread, sizeof(monitor_data));
  return data;
}

u16 stack_depth(const stack_frame *frame) {
  DCHECK(!is_frame_native(frame), "Can't get stack depth of native frame");
  DCHECK(frame->method, "Can't get stack depth of fake frame");
  int pc = frame->program_counter;
  code_analysis *analy = frame->method->code_analysis;
  DCHECK(pc < analy->insn_count);
  return frame->insn_index_to_sd[pc];
}

value *get_native_args(const stack_frame *frame) {
  DCHECK(is_frame_native(frame));
  return ((value *)frame) - frame->num_locals;
}

stack_value *frame_stack(stack_frame *frame) {
  DCHECK(!is_frame_native(frame));
  return frame->stack;
}

native_frame_data *get_native_frame_data(stack_frame *frame) {
  DCHECK(is_frame_native(frame));
  return (native_frame_data *)(frame + 1);
}

obj_header *deref_js_handle(vm *vm, int index) {
  if (index < 0 || index >= arrlen(vm->js_handles)) {
    return nullptr;
  }
  object o = vm->js_handles[index];
  return o;
}

int make_js_handle(vm *vm, obj_header *obj) {
  DCHECK(obj);
  // Search for a null entry
  for (int i = 0; i < arrlen(vm->js_handles); i++) {
    if (!vm->js_handles[i]) {
      vm->js_handles[i] = obj;
      return i;
    }
  }
  // Push a new one
  int index = arrlen(vm->js_handles);
  arrput(vm->js_handles, obj);
  return index;
}

void drop_js_handle(vm *vm, int index) {
  if (index < 0 || index >= arrlen(vm->js_handles)) {
    return;
  }
  vm->js_handles[index] = nullptr;
}

handle *make_handle_impl(vm_thread *thread, obj_header *obj, [[maybe_unused]] const char *file_name,
                         [[maybe_unused]] int line_no) {
  if (!obj)
    return &thread->null_handle;
  for (int i = 0; i < thread->handles_capacity; ++i) {
    handle *h = &thread->handles[i];
    if (!h->obj) {
      h->obj = obj;
#if DCHECKS_ENABLED
      h->line = line_no;
      h->filename = file_name;
#endif
      return thread->handles + i;
    }
  }

#if DCHECKS_ENABLED
  // Print where the handles were allocated
  printf("Handle exhaustion: Locations ");
  for (int i = 0; i < thread->handles_capacity; ++i) {
    if (thread->handles[i].obj) {
      printf("%d (%s)", thread->handles[i].line, thread->handles[i].filename);
    }
  }
  printf("\n");
#else
  printf("Handle exhaustion! Build with DCHECKS_ENABLED to find out where...\n");
#endif
  UNREACHABLE(); // When we need more handles, rewrite to use a LL impl
}

void drop_handle(vm_thread *thread, handle *handle) {
  if (!handle || handle == &thread->null_handle)
    return;
  DCHECK(handle >= thread->handles && handle < thread->handles + thread->handles_capacity);
  handle->obj = nullptr;
}

// For each argument, if it's a reference, wrap it in a handle; otherwise
// just memcpy it over since the representations of primitives are the same
// between stack_value and value
static void make_handles_array(vm_thread *thread, const method_descriptor *descriptor, bool is_static,
                               stack_value *stack_args, value *args) {
  u8 argc = descriptor->args_count;
  int j = 0;
  if (!is_static) {
    args[j].handle = make_handle(thread, stack_args[j].obj);
    ++j;
  }
  for (int i = 0; i < argc; ++i, ++j) {
    if (descriptor->args[i].repr_kind == TYPE_KIND_REFERENCE) {
      args[j].handle = make_handle(thread, stack_args[j].obj);
    } else {
      memcpy(args + j, stack_args + j, sizeof(stack_value));
    }
  }
}

static void drop_handles_array(vm_thread *thread, const cp_method *method, const method_descriptor *desc, value *args) {
  bool is_static = method->access_flags & ACCESS_STATIC;
  if (!is_static)
    drop_handle(thread, args[0].handle);
  for (int i = 0; i < desc->args_count; ++i)
    if (desc->args[i].repr_kind == TYPE_KIND_REFERENCE)
      drop_handle(thread, args[i + !is_static].handle);
}

stack_frame *push_native_frame(vm_thread *thread, cp_method *method, const method_descriptor *descriptor,
                               stack_value *args, u8 argc) {
  native_callback *native = method->native_handle;
  if (!native) {
    raise_unsatisfied_link_error(thread, method);
    return nullptr;
  }

  value *locals = (value *)(args + argc); // reserve new memory on stack
  stack_frame *frame = (stack_frame *)(locals + argc);

  if ((uintptr_t)frame + sizeof(stack_frame) > (uintptr_t)thread->stack.frame_buffer_end) {
    raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  DCHECK((uintptr_t)frame % 8 == 0, "Frame must be aligned");

  frame->prev = thread->stack.top;
  thread->stack.top = frame;

  frame->kind = FRAME_KIND_NATIVE;
  frame->num_locals = argc;
  // this many additional slots are used for the native info
  frame->max_stack = align_up(sizeof(native_frame_data), sizeof(stack_value));
  frame->method = method;
  get_native_frame_data(frame)->method_shape = descriptor;
  frame->is_async_suspended = false;
  frame->synchronized_state = SYNCHRONIZE_NONE;

  // Now wrap arguments in handles and copy them into the frame
  make_handles_array(thread, descriptor, method->access_flags & ACCESS_STATIC, args, locals);
  return frame;
}

// See push_plain_frame
__attribute__((noinline)) static stack_frame *raise_abstract_method_error_tramp(vm_thread *thread, cp_method *method) {
  raise_abstract_method_error(thread, method);
  return nullptr;
}

// See push_plain_frame
__attribute__((noinline)) static stack_frame *raise_exception_object_tramp(vm_thread *thread,
                                                                           [[maybe_unused]] cp_method *method,
                                                                           [[maybe_unused]] stack_value *args,
                                                                           [[maybe_unused]] u8 argc) {
  raise_exception_object(thread, thread->stack_overflow_error);
  return nullptr;
}

// This function is extremely hot
stack_frame *push_plain_frame(vm_thread *thread, cp_method *method, stack_value *args, u8 argc) {
  const attribute_code *code = method->code;
  if (unlikely(!code)) {
    // allows the above check to become a tail call (jcc on x86, cbz on arm)
    return raise_abstract_method_error_tramp(thread, method);
  }

  DCHECK(argc <= code->max_locals);

  stack_frame *restrict frame = (stack_frame *)(args + code->max_locals);
  if ((uintptr_t)((char *)frame + code->frame_size) > (uintptr_t)thread->stack.frame_buffer_end) {
    // allows the above check to become a tail call
    return raise_exception_object_tramp(thread, method, args, argc);
  }

  // See linkage.c for how the template frame is generated

  frame->method = method;
  frame->prev = thread->stack.top;
  thread->stack.top = frame;

  constexpr size_t copy_sz = sizeof(stack_frame) - offsetof(stack_frame, kind);
  memcpy(&frame->kind /* member following prev */, &((stack_frame *)method->template_frame)->kind, copy_sz);

  // Not necessary, but possibly helpful when looking at debug dumps
  // memset(frame_stack(frame), 0x0, code->max_stack * sizeof(stack_value));

  return frame;
}

// Push a (native or plain) frame onto the execution stack, but do not execute
// any native code or bytecode.
//
// The number of arguments (argc) is an invariant of the method, but should be
// supplied as an argument for debug checking.
//
// This function must not be used for signature polymorphic methods -- use
// invokevirtual_signature_polymorphic for that.
//
// Exception behavior:
//   - If the stack has insufficient capacity, a StackOverflowError is raised.
//   - If the method is abstract, an AbstractMethodError is raised.
//   - If the method is native and has not been linked, an UnsatisfiedLinkError
//     is raised.
// In all the above cases, a null pointer is returned.
//
// Interrupt behavior:
//   - No interrupts will occur as a result of executing this function.
stack_frame *push_frame(vm_thread *thread, cp_method *method, stack_value *args, u8 argc) {
  DCHECK(method != nullptr, "Method is null");
  DCHECK(argc == method_argc(method), "Wrong argc");
  if (method->access_flags & ACCESS_NATIVE) {
    return push_native_frame(thread, method, method->descriptor, args, argc);
  }
  return push_plain_frame(thread, method, args, argc);
}

const char *infer_type(code_analysis *analysis, int insn, int index, bool is_local) {
  stack_summary *sum = analysis->stack_states[insn];
  return type_kind_to_string(sum->entries[index + (is_local ? sum->stack : 0)]);
}

void dump_frame(FILE *stream, const stack_frame *frame) {
  DCHECK(!is_frame_native(frame), "Can't dump native frame");

  char buf[5000] = {0}, *write = buf, *end = buf + sizeof(buf);
  int sd = stack_depth(frame);

  for (int i = 0; i < sd; ++i) {
    stack_value value = frame_stack((void *)frame)[i];
    const char *is_ref = infer_type(frame->method->code_analysis, frame->program_counter, i, false);
    write += snprintf(write, end - write, " stack[%d] = [ ref = %p, int = %d ] %s\n", i, value.obj, value.i, is_ref);
  }

  for (int i = 0; i < frame->num_locals; ++i) {
    stack_value value = frame_locals(frame)[i];
    const char *is_ref = infer_type(frame->method->code_analysis, frame->program_counter, i, true);
    write += snprintf(write, end - write, "locals[%d] = [ ref = %p, int = %d ] %s\n", i, value.obj, value.i, is_ref);
  }

  fprintf(stream, "%s", buf);
}

void pop_frame(vm_thread *thr, [[maybe_unused]] const stack_frame *reference) {
  stack_frame *frame = thr->stack.top;
  DCHECK(frame);
  DCHECK(reference == nullptr || reference == frame);
  if (is_frame_native(frame)) {
    drop_handles_array(thr, frame->method, get_native_frame_data(frame)->method_shape, get_native_args(frame));
  }
  thr->stack.top = frame->prev;
}

typedef struct {
  slice name;
  slice descriptor;

  native_callback callback;
} native_entry;

typedef struct {
  native_entry *entries;
} native_entries;

void free_native_entries(void *entries_) {
  if (!entries_)
    return;

  arrfree(((native_entries *)entries_)->entries);
  free(entries_);
}

extern size_t bjvm_natives_count;
extern native_t *bjvm_natives[];

void register_native(vm *vm, slice class, const slice method_name, const slice method_descriptor,
                     native_callback callback) {
  if (class.chars[0] == '/')
    class = subslice(class, 1);

  //  printf("Registering native: %.*s on class %.*s\n", fmt_slice(method_name), fmt_slice(class));

  heap_string heap_class = make_heap_str_from(class);
  for (size_t i = 0; i < class.len; i++) { // hacky way to avoid emscripten from complaining about symbol names
    if (heap_class.chars[i] == '_')
      heap_class.chars[i] = '$';
  }
  class = hslc(heap_class);

  // TODO look across all classloaders
  classdesc *cd = hash_table_lookup(&vm->bootstrap_classloader->loaded, class.chars, (int)class.len);
  CHECK(cd == nullptr, "%.*s: Natives must be registered before class is loaded", fmt_slice(class));

  native_entries *existing = hash_table_lookup(&vm->natives, class.chars, (int)class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)hash_table_insert(&vm->natives, class.chars, (int)class.len, existing);
  }

  native_entry ent = (native_entry){method_name, method_descriptor, callback};
  arrput(existing->entries, ent);
  free_heap_str(heap_class);
}

void read_string(vm_thread *, obj_header *obj, s8 **buf, size_t *len) {
  DCHECK(utf8_equals(obj->descriptor->name, "java/lang/String"));
  obj_header *array = ((struct native_String *)obj)->value;
  *buf = ArrayData(array);
  *len = ArrayLength(array);
}

int read_string_to_utf8(vm_thread *thread, heap_string *result, obj_header *obj) {
  DCHECK(obj, "String is null");

  s8 *buf;
  size_t len;
  read_string(nullptr, obj, &buf, &len);
  char *cbuf = malloc(len + 1);

  if (!cbuf) {
    out_of_memory(thread);
    return -1;
  }

  for (size_t i = 0; i < len; ++i) {
    cbuf[i] = buf[i];
  }
  cbuf[len] = 0;
  *result = (heap_string){.chars = cbuf, .len = len};

  return 0;
}

DEFINE_ASYNC(load_class_of_field_descriptor) {
#define name (self->args.name)
#define thread (self->args.thread)
  const char *chars = name.chars;
  if (chars[0] == 'L') {
    name = subslice_to(name, 1, name.len - 1);
    AWAIT(lookup_class, thread, name, self->args.cl);
    ASYNC_RETURN(get_async_result(lookup_class));
  }
  if (chars[0] == '[') {
    AWAIT(lookup_class, thread, name, self->args.cl);
    ASYNC_RETURN(get_async_result(lookup_class));
  }
  ASYNC_END(primitive_classdesc(thread, read_type_kind_char(chars[0])));
#undef thread
#undef name
}

classdesc *primitive_classdesc(vm_thread *thread, type_kind prim_kind) {
  vm *vm = thread->vm;
  return vm->primitive_classes[prim_kind];
}

struct native_Class *primitive_class_mirror(vm_thread *thread, type_kind prim_kind) {
  return primitive_classdesc(thread, prim_kind)->mirror;
}

classdesc *make_primitive_classdesc(vm *vm, type_kind kind, const slice name) {
  classdesc *desc = calloc(1, sizeof(classdesc));

  desc->kind = CD_KIND_PRIMITIVE;
  desc->state = CD_STATE_INITIALIZED;
  desc->super_class = nullptr;
  desc->name = arena_make_str(&desc->arena, name.chars, (int)name.len);
  desc->access_flags = ACCESS_PUBLIC | ACCESS_FINAL | ACCESS_ABSTRACT;
  desc->array_type = nullptr;
  desc->primitive_component = kind;
  desc->classloader = vm->bootstrap_classloader;
  pthread_mutex_init(&desc->lock, nullptr);

  return desc;
}

void vm_init_primitive_classes(vm_thread *thread) {
  vm *vm = thread->vm;
  if (vm->primitive_classes[0])
    return; // already initialized

  vm->primitive_classes[TYPE_KIND_BOOLEAN] = make_primitive_classdesc(vm, TYPE_KIND_BOOLEAN, STR("boolean"));
  vm->primitive_classes[TYPE_KIND_BYTE] = make_primitive_classdesc(vm, TYPE_KIND_BYTE, STR("byte"));
  vm->primitive_classes[TYPE_KIND_CHAR] = make_primitive_classdesc(vm, TYPE_KIND_CHAR, STR("char"));
  vm->primitive_classes[TYPE_KIND_SHORT] = make_primitive_classdesc(vm, TYPE_KIND_SHORT, STR("short"));
  vm->primitive_classes[TYPE_KIND_INT] = make_primitive_classdesc(vm, TYPE_KIND_INT, STR("int"));
  vm->primitive_classes[TYPE_KIND_LONG] = make_primitive_classdesc(vm, TYPE_KIND_LONG, STR("long"));
  vm->primitive_classes[TYPE_KIND_FLOAT] = make_primitive_classdesc(vm, TYPE_KIND_FLOAT, STR("float"));
  vm->primitive_classes[TYPE_KIND_DOUBLE] = make_primitive_classdesc(vm, TYPE_KIND_DOUBLE, STR("double"));
  vm->primitive_classes[TYPE_KIND_VOID] = make_primitive_classdesc(vm, TYPE_KIND_VOID, STR("void"));

  // Set up mirrors
  for (int i = 0; i < 9; ++i) {
    classdesc *desc = vm->primitive_classes[i];
    desc->mirror = get_class_mirror(thread, desc);
  }
}

static slice get_default_boot_cp() {
#if HAVE_GETENV
  char *boot_cp = getenv("BOOT_CLASSPATH");
  if (boot_cp != nullptr && strlen(boot_cp) > 0) {
    FILE *file = fopen(boot_cp, "r");
    if (file == nullptr) {
      fprintf(stderr, "Could not open BOOT_CLASSPATH %s: %s\n", boot_cp, strerror(errno));
      exit(1);
    }

    fclose(file);
    return (slice){.len = strlen(boot_cp), .chars = boot_cp};
  }
#endif

  return STR("./jdk23.jar");
}

vm_options default_vm_options() {
  vm_options options = {nullptr};
  options.heap_size = 1 << 26;
  options.runtime_classpath = get_default_boot_cp();

  return options;
}

// NOLINTNEXTLINE(misc-no-recursion)
void free_classdesc(void *cd_) {
  classdesc *cd = cd_;
  if (cd->array_type)
    free_classdesc(cd->array_type);
  free_classfile(*cd);
  free_function_tables(cd);
  free(cd);
}

void existing_classes_are_javabase(vm *vm, module *module) {
  // Iterate through all bootstrap-loaded classes and assign them to the module
  for (int classloader_i = 0; classloader_i < arrlen(vm->active_classloaders); ++classloader_i) {
    classloader *cl = vm->active_classloaders[classloader_i];
    hash_table_iterator it = hash_table_get_iterator(&cl->initiating);
    char *key;
    size_t key_len;
    classdesc *classdesc;
    while (hash_table_iterator_has_next(it, &key, &key_len, (void **)&classdesc)) {
      classdesc->module = module;
      if (classdesc->mirror) {
        classdesc->mirror->module = module->reflection_object;
      }
      hash_table_iterator_next(&it);
    }
  }
}

struct cached_classdescs *cached_classes(vm *vm) {
  CHECK(vm->_cached_classdescs && "Cached classdescs not yet initialized");
  return vm->_cached_classdescs;
}

int define_module(vm *vm, slice module_name, obj_header *module_) {
  module *mod = calloc(1, sizeof(module));
  mod->reflection_object = module_;
  (void)hash_table_insert(&vm->modules, module_name.chars, (int)module_name.len, mod);
  if (utf8_equals(module_name, "java.base")) {
    existing_classes_are_javabase(vm, mod);
  }
  return 0;
}

module *get_module(vm *vm, slice module_name) {
  return hash_table_lookup(&vm->modules, module_name.chars, (int)module_name.len);
}

#define OOM_SLOP_BYTES (1 << 12)

vm *create_vm(const vm_options options) {
  vm *vm = calloc(1, sizeof(*vm));

  INIT_STACK_STRING(classpath, 1000);
  classpath = bprintf(classpath, "%.*s:%.*s", fmt_slice(options.runtime_classpath), fmt_slice(options.classpath));

  char *error = init_classpath(&vm->bootstrap_classpath, classpath);
  vm->application_classpath = make_heap_str_from(options.classpath);
  if (error) {
    fprintf(stderr, "Classpath error: %s", error);
    free(error);
    return nullptr;
  }

  vm->inchoate_classes = make_hash_table(nullptr, 0.75, 16);
  vm->natives = make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = make_hash_table(nullptr, 0.75, 16);
  vm->class_padding = make_hash_table(nullptr, 0.75, 16);
  vm->modules = make_hash_table(free, 0.75, 16);
  vm->main_thread_group = nullptr;

  vm->heap.heap = aligned_alloc(4096, options.heap_size + OOM_SLOP_BYTES);
  vm->heap.heap_used = 0;
  vm->heap.heap_capacity = options.heap_size;
  vm->heap.true_heap_capacity = options.heap_size + OOM_SLOP_BYTES;
  vm->active_classloaders = nullptr;

  size_t bytecode_patch_queue_size = 1000; // todo: make a vm option
  vm->instruction_patch_queue.buffer = calloc(bytecode_patch_queue_size, sizeof(bytecode_patch_request));
  vm->instruction_patch_queue.num_used = 0;
  vm->instruction_patch_queue.capacity = bytecode_patch_queue_size;
  vm->permanent_gc_roots = nullptr;

  vm->bootstrap_classloader = calloc(1, sizeof(classloader));
  classloader_init(vm, vm->bootstrap_classloader, nullptr);
  arrput(vm->active_classloaders, vm->bootstrap_classloader);

  vm->active_threads = nullptr;

  vm->read_stdin = options.read_stdin;
  vm->poll_available_stdin = options.poll_available_stdin;
  vm->write_stdout = options.write_stdout;
  vm->write_stderr = options.write_stderr;
  vm->stdio_override_param = options.stdio_override_param;

  vm->next_tid = 0;
  vm->reference_pending_list = nullptr;
  vm->should_gc_pause = false;

  for (size_t i = 0; i < bjvm_natives_count; ++i) {
    native_t const *native_ptr = bjvm_natives[i];
    register_native(vm, native_ptr->class_path, native_ptr->method_name, native_ptr->method_descriptor,
                    native_ptr->callback);
  }

  register_native_padding(vm);

  return vm;
}

void remove_unsafe_allocation(vm *vm, void *allocation) {
  void **unsafe_allocations = vm->unsafe_allocations;
  for (int i = 0; i < arrlen(unsafe_allocations); ++i) {
    if (unsafe_allocations[i] == allocation) {
      arrdelswap(unsafe_allocations, i);
      return;
    }
  }
  CHECK(false);
}

void free_unsafe_allocations(vm *vm) {
  for (int i = 0; i < arrlen(vm->unsafe_allocations); ++i) {
    free(vm->unsafe_allocations[i]);
  }
  for (int i = 0; i < arrlen(vm->mmap_allocations); ++i) {
    mmap_allocation A = vm->mmap_allocations[i];
    munmap(A.ptr, A.len);
  }
  arrfree(vm->mmap_allocations);
  arrfree(vm->unsafe_allocations);
}

void free_zstreams(vm *vm) {
  for (int i = 0; i < arrlen(vm->z_streams); ++i) {
    inflateEnd(vm->z_streams[i]);
    free(vm->z_streams[i]);
  }
  arrfree(vm->z_streams);
}

void free_vm(vm *vm) {
  free_hash_table(vm->natives);
  free_hash_table(vm->inchoate_classes);
  free_hash_table(vm->interned_strings);
  free_hash_table(vm->class_padding);
  free_hash_table(vm->modules);

  if (vm->primitive_classes[0]) {
    for (int i = 0; i < 9; ++i) {
      free_classdesc(vm->primitive_classes[i]);
    }
  }

  for (int i = 0; i < arrlen(vm->active_classloaders); ++i) {
    classloader_uninit(vm->active_classloaders[i]);
    free(vm->active_classloaders[i]);
  }
  arrfree(vm->active_classloaders);

  arrfree(vm->permanent_gc_roots);

  free_classpath(&vm->bootstrap_classpath);
  free_heap_str(vm->application_classpath);
  free(cached_classes(vm));
  // Free all threads, iterate backwards because they remove themselves
  for (int i = arrlen(vm->active_threads) - 1; i >= 0; --i) {
    free_thread(vm->active_threads[i]);
  }
  arrfree(vm->active_threads);
  free(vm->heap.heap);
  free_unsafe_allocations(vm);
  free_zstreams(vm);

  free(vm);
}

thread_options default_thread_options() {
  thread_options options = {};
  options.stack_space = 1 << 19;
  options.js_jit_enabled = true;
  options.thread_group = nullptr;
  return options;
}

/// Invokes a Java method.  Sets thread->current_exception if an exception is thrown and aborts if the method tries
/// to suspend.
stack_value call_interpreter_synchronous(vm_thread *thread, cp_method *method, stack_value *args) {
  if (args == nullptr) {
    assert(method->descriptor->args_count == 0 && "No arguments provided for method with arguments");
    args = (stack_value[]){};
  }

  thread->stack.synchronous_depth++;

  call_interpreter_t ctx = (call_interpreter_t){.args = {thread, method, args}};
  future_t fut = call_interpreter(&ctx);
  CHECK(fut.status == FUTURE_READY, "method tried to suspend");

  thread->stack.synchronous_depth--;

  return ctx._result;
}

// NOLINTNEXTLINE(misc-no-recursion)
__attribute__((noinline)) cp_field *field_lookup(classdesc *classdesc, slice const name, slice const descriptor) {
  if (!classdesc->super_class) // java/lang/Object has no fields (this is commonly called due to (*) below)
    return nullptr;

  for (int i = 0; i < classdesc->fields_count; ++i) {
    cp_field *field = classdesc->fields + i;
    if (utf8_equals_utf8(field->name, name) && utf8_equals_utf8(field->descriptor, descriptor)) {
      return field;
    }
  }

  // Then look on superinterfaces (*)
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    cp_field *result = field_lookup(classdesc->interfaces[i]->classdesc, name, descriptor);
    if (result)
      return result;
  }

  // Then look on the superclass
  return field_lookup(classdesc->super_class->classdesc, name, descriptor);
}

obj_header *get_main_thread_group(vm_thread *thread);

void set_field(obj_header *obj, cp_field *field, stack_value stack_value) {
  store_stack_value((void *)obj + field->byte_offset, stack_value, field->parsed_descriptor.repr_kind);
}

void set_static_field(cp_field *field, stack_value stack_value) {
  store_stack_value((void *)field->my_class->static_fields + field->byte_offset, stack_value,
                    field->parsed_descriptor.repr_kind);
}

stack_value get_field(obj_header *obj, cp_field *field) {
  return load_stack_value((void *)obj + field->byte_offset, field->parsed_descriptor.repr_kind);
}

// UnsafeConstants contains some important low-level data used by Unsafe:
//   - ADDRESS_SIZE0: The size in bytes of a pointer
//   - PAGE_SIZE: The size in bytes of a memory page
//   - UNALIGNED_ACCESS: Whether unaligned memory access is allowed
static void init_unsafe_constants(vm_thread *thread) {
  classdesc *UC = bootstrap_lookup_class(thread, STR("jdk/internal/misc/UnsafeConstants"));
  DCHECK(UC, "UnsafeConstants class not found!");

  initialize_class_t ctx = {.args = {thread, UC}};
  future_t init = initialize_class(&ctx);
  CHECK(init.status == FUTURE_READY);

  cp_field *address_size = field_lookup(UC, STR("ADDRESS_SIZE0"), STR("I")),
           *page_size = field_lookup(UC, STR("PAGE_SIZE"), STR("I")),
           *unaligned_access = field_lookup(UC, STR("UNALIGNED_ACCESS"), STR("Z"));

  DCHECK(address_size && page_size && unaligned_access, "UnsafeConstants fields not found!");
  set_static_field(address_size, (stack_value){.i = sizeof(void *)});
  set_static_field(page_size, (stack_value){.i = 4096});
  set_static_field(unaligned_access, (stack_value){.i = 1});
}

#define PROFILE_STARTUP 0 // if 1, prints out profiler data for startup

vm_thread *create_main_thread(vm *vm, thread_options options) {
  vm_thread *thr = calloc(1, sizeof(vm_thread));
  arrput(vm->active_threads, thr);

  thr->vm = vm;
  thr->stack.frame_buffer = calloc(1, thr->stack.frame_buffer_capacity = options.stack_space);
  thr->stack.frame_buffer_end = thr->stack.frame_buffer + options.stack_space;
  thr->js_jit_enabled = options.js_jit_enabled;
  const int HANDLES_CAPACITY = 200;
  thr->handles = calloc(1, sizeof(handle) * HANDLES_CAPACITY);
  thr->handles_capacity = HANDLES_CAPACITY;

  thr->stack.async_call_stack = calloc(1, 0x20);
  thr->tid = vm->next_tid++;

  bool initializing = !vm->vm_initialized;

#if PROFILE_STARTUP
  profiler *prof = launch_profiler(thr);
#endif

  if (initializing) {
    vm->vm_initialized = true;

    vm_init_primitive_classes(thr);
    init_unsafe_constants(thr);

    init_cached_classdescs_t init = {.args = {thr}};
    future_t result = init_cached_classdescs(&init);
    CHECK(result.status == FUTURE_READY);
  }

  // Pre-allocate OOM and stack overflow errors
  thr->out_of_mem_error = new_object(thr, cached_classes(vm)->oom_error);
  thr->stack_overflow_error = new_object(thr, cached_classes(vm)->stack_overflow_error);

  handle *java_thread = make_handle(thr, new_object(thr, cached_classes(vm)->thread));
#define java_thr ((struct native_Thread *)java_thread->obj)
  thr->thread_obj = java_thr;

  java_thr->eetop = (uintptr_t)thr;
  object name = MakeJStringFromCString(thr, "main", true);
  java_thr->name = name;

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  cp_method *make_thread = method_lookup(cached_classes(vm)->thread, STR("<init>"),
                                         STR("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"), false, false);

  obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  call_interpreter_synchronous(
      thr, make_thread,
      (stack_value[]){{.obj = (void *)java_thr}, {.obj = main_thread_group}, {.obj = java_thr->name}});

#undef java_thr
  drop_handle(thr, java_thread);

  if (initializing) {
    slice const phases[3] = {STR("initPhase1"), STR("initPhase2"), STR("initPhase3")};
    slice const signatures[3] = {STR("()V"), STR("(ZZ)I"), STR("()V")};

    cp_method *method;
    stack_value ret;
    for (unsigned i = 0; i < sizeof(phases) / sizeof(*phases); ++i) {
      method = method_lookup(cached_classes(vm)->system, phases[i], signatures[i], false, false);
      assert(method);
      stack_value args[2] = {{.i = 1}, {.i = 1}};
      call_interpreter_synchronous(thr, method, args); // void methods, no result

      if (thr->current_exception) {
        // Failed to initialize
        method = method_lookup(thr->current_exception->descriptor, STR("getMessage"), STR("()Ljava/lang/String;"), true,
                               true);
        assert(method);

        stack_value get_message_args[] = {{.obj = thr->current_exception}};
        stack_value obj = call_interpreter_synchronous(thr, method, get_message_args);
        heap_string message;
        if (obj.obj) {
          CHECK(read_string_to_utf8(thr, &message, obj.obj) == 0);
        }
        printf("Error in init phase %.*s: %.*s, %s\n", fmt_slice(thr->current_exception->descriptor->name),
               fmt_slice(phases[i]), obj.obj ? message.chars : "no message");
        abort();
      }

      method = method_lookup(cached_classes(vm)->system, STR("getProperty"),
                             STR("(Ljava/lang/String;)Ljava/lang/String;"), false, false);
      assert(method);
      stack_value args2[1] = {{.obj = (void *)MakeJStringFromCString(thr, "java.home", true)}};
      ret = call_interpreter_synchronous(thr, method, args2); // returns a String

      heap_string java_home;
      CHECK(read_string_to_utf8(thr, &java_home, ret.obj) == 0);
      free_heap_str(java_home);
    }

    vm->vm_initialized = true;
  }

#if PROFILE_STARTUP
  char *profiler_data = read_profiler(prof);
  if (profiler_data) {
    printf("Profiler data: %s\n", profiler_data);
    free(profiler_data);
  }
#endif

  thr->current_exception = nullptr;
  return thr;
}

vm_thread *create_vm_thread(vm *vm, vm_thread *creator_thread, struct native_Thread *thread_obj,
                            thread_options options) {
  handle *java_thread = make_handle(creator_thread, (obj_header *)thread_obj);

  vm_thread *thr = calloc(1, sizeof(vm_thread));
  arrput(vm->active_threads, thr);

  thr->vm = vm;
  thr->stack.frame_buffer = calloc(1, thr->stack.frame_buffer_capacity = options.stack_space);
  thr->stack.frame_buffer_end = thr->stack.frame_buffer + options.stack_space;
  thr->js_jit_enabled = options.js_jit_enabled;
  const int HANDLES_CAPACITY = 200;
  thr->handles = calloc(1, sizeof(handle) * HANDLES_CAPACITY);
  thr->handles_capacity = HANDLES_CAPACITY;

  thr->stack.async_call_stack = calloc(1, 0x20);
  thr->tid = vm->next_tid++;

  bool initializing = !vm->vm_initialized;

  if (initializing) {
    vm->vm_initialized = true;

    vm_init_primitive_classes(thr);
    init_unsafe_constants(thr);

    init_cached_classdescs_t init = {.args = {thr}};
    future_t result = init_cached_classdescs(&init);
    CHECK(result.status == FUTURE_READY);
  }

  // Pre-allocate OOM and stack overflow errors
  thr->out_of_mem_error = new_object(thr, cached_classes(vm)->oom_error);
  thr->stack_overflow_error = new_object(thr, cached_classes(vm)->stack_overflow_error);

  thr->thread_obj = (struct native_Thread *)java_thread->obj;
  drop_handle(creator_thread, java_thread);

  if (initializing) {
    slice const phases[3] = {STR("initPhase1"), STR("initPhase2"), STR("initPhase3")};
    slice const signatures[3] = {STR("()V"), STR("(ZZ)I"), STR("()V")};

    cp_method *method;
    stack_value ret;
    for (size_t i = 0; i < sizeof(phases) / sizeof(*phases); i++) {
      method = method_lookup(cached_classes(vm)->system, phases[i], signatures[i], false, false);
      assert(method);
      stack_value args[2] = {{.i = 1}, {.i = 1}};
      call_interpreter_synchronous(thr, method, args); // void methods, no result

      if (thr->current_exception) {
        // Failed to initialize
        method = method_lookup(thr->current_exception->descriptor, STR("getMessage"), STR("()Ljava/lang/String;"), true,
                               true);
        assert(method);

        stack_value get_message_args[] = {{.obj = thr->current_exception}};
        stack_value obj = call_interpreter_synchronous(thr, method, get_message_args);
        heap_string message;
        if (obj.obj) {
          CHECK(read_string_to_utf8(thr, &message, obj.obj) == 0);
        }
        fprintf(stderr, "Error in init phase %.*s: %.*s, %s\n", fmt_slice(thr->current_exception->descriptor->name),
                fmt_slice(phases[i]), obj.obj ? message.chars : "no message");
        abort();
      }

      method = method_lookup(cached_classes(vm)->system, STR("getProperty"),
                             STR("(Ljava/lang/String;)Ljava/lang/String;"), false, false);
      assert(method);
      stack_value args2[1] = {{.obj = (void *)MakeJStringFromCString(thr, "java.home", true)}};
      ret = call_interpreter_synchronous(thr, method, args2); // returns a String

      heap_string java_home;
      CHECK(read_string_to_utf8(thr, &java_home, ret.obj) == 0);
      free_heap_str(java_home);
    }
  }

  thr->current_exception = nullptr;
  return thr;
}

static void remove_thread_from_vm_list(vm_thread *thread) {
  for (int i = 0; i < arrlen(thread->vm->active_threads); ++i) {
    if (thread == thread->vm->active_threads[i]) {
      arrdelswap(thread->vm->active_threads, i);
      break;
    }
  }
}

void free_thread(vm_thread *thread) {
  // TODO remove from the VM

  finish_profiler(thread->profiler); // no-op if no profiler is active

  free(thread->stack.async_call_stack);
  free(thread->stack.frame_buffer);
  free(thread->handles);
  remove_thread_from_vm_list(thread);
  free(thread);
}

DEFINE_ASYNC(resolve_method_type) {
  // Resolve each class in the arguments list, as well as the return type if it
  // exists
#define thread (self->args.thread)
#define method (self->args.method)

  DCHECK(method);

  classdesc *Class = cached_classes(thread->vm)->klass;
  self->ptypes = make_handle(thread, CreateObjectArray1D(thread, Class, method->args_count));

  for (int i = 0; i < method->args_count; ++i) {
    AWAIT(load_class_of_field_descriptor, thread, self->args.cl, method->args[i].unparsed);

    classdesc *arg_desc = get_async_result(load_class_of_field_descriptor);
    if (!arg_desc)
      ASYNC_RETURN(nullptr);

    object mirror = (void *)get_class_mirror(thread, arg_desc);
    *((struct native_Class **)ArrayData(self->ptypes->obj) + i) = (void *)mirror;
  }

  AWAIT(load_class_of_field_descriptor, thread, self->args.cl, method->return_type.unparsed);
  classdesc *ret_desc = get_async_result(load_class_of_field_descriptor);
  if (!ret_desc)
    ASYNC_RETURN(nullptr);
  struct native_Class *rtype = get_class_mirror(thread, ret_desc);
  // Call <init>(Ljava/lang/Class;[Ljava/lang/Class;Z)V
  classdesc *MethodType = cached_classes(thread->vm)->method_type;
  cp_method *init = method_lookup(MethodType, STR("makeImpl"),
                                  STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                      "lang/invoke/MethodType;"),
                                  false, false);

  stack_value result = call_interpreter_synchronous(
      thread, init, (stack_value[]){{.obj = (void *)rtype}, {.obj = self->ptypes->obj}, {.i = 1 /* trusted */}});
  CHECK(!thread->current_exception);
  drop_handle(thread, self->ptypes);
  ASYNC_END((void *)result.obj);
#undef method
#undef thread
}

[[maybe_unused]] static bool mh_handle_supported(method_handle_kind kind) {
  switch (kind) {
  case MH_KIND_GET_FIELD:
  case MH_KIND_INVOKE_STATIC:
  case MH_KIND_INVOKE_VIRTUAL:
  case MH_KIND_INVOKE_SPECIAL:
  case MH_KIND_INVOKE_INTERFACE:
  case MH_KIND_NEW_INVOKE_SPECIAL:
    return true;
  default:
    return false;
  }
}

DEFINE_ASYNC(compute_mh_type_info) {
  self->rtype = nullptr;
  self->ptypes = nullptr;

#define info (self->args.info)
#define thread (self->args.thread)
  if (info->handle_kind == MH_KIND_GET_FIELD) {
    // MT should be of the form (C)T, where C is the class the field is found on
    cp_field_info *field = &info->reference->field;
    arrput(self->ptypes, field->class_info->classdesc);
    AWAIT(load_class_of_field_descriptor, thread, self->args.cl, field->parsed_descriptor->unparsed);
    self->rtype = get_async_result(load_class_of_field_descriptor);
  } else if (info->handle_kind == MH_KIND_INVOKE_STATIC || info->handle_kind == MH_KIND_INVOKE_VIRTUAL ||
             info->handle_kind == MH_KIND_INVOKE_SPECIAL || info->handle_kind == MH_KIND_INVOKE_INTERFACE) {
    // MT should be of the form (C,A*)T, where C is the class the method is
    // found on, A* is the list of argument types, and T is the return type
    self->method = &info->reference->methodref;

    if (info->handle_kind != MH_KIND_INVOKE_STATIC) {
      arrput(self->ptypes, self->method->class_info->classdesc);
    }

    for (int i = 0; i < self->method->descriptor->args_count; ++i) {
      field_descriptor *arg = self->method->descriptor->args + i;
      AWAIT(load_class_of_field_descriptor, thread, self->args.cl, arg->unparsed);
      arrput(self->ptypes, get_async_result(load_class_of_field_descriptor));
    }

    AWAIT(load_class_of_field_descriptor, thread, self->args.cl, self->method->descriptor->return_type.unparsed);
    self->rtype = get_async_result(load_class_of_field_descriptor);
  } else if (info->handle_kind == MH_KIND_NEW_INVOKE_SPECIAL) {
    // MT should be of the form (A*)T, where A* is the list of argument types,
    self->method = &info->reference->methodref;

    for (int i = 0; i < self->method->descriptor->args_count; ++i) {
      field_descriptor *arg = self->method->descriptor->args + i;
      AWAIT(load_class_of_field_descriptor, thread, self->args.cl, arg->unparsed);
      arrput(self->ptypes, get_async_result(load_class_of_field_descriptor));
    }

    self->rtype = self->method->class_info->classdesc;
  } else {
    UNREACHABLE();
  }

  *self->args.result =
      (mh_type_info_t){.rtype = self->rtype, .ptypes = self->ptypes, .ptypes_count = arrlen(self->ptypes)};
#undef thread
#undef info
  ASYNC_END(0);
}

DEFINE_ASYNC(resolve_mh_mt) {
  CHECK(mh_handle_supported(args->info->handle_kind), "Unsupported method handle kind");

  cp_class_info *required_type =
      args->info->handle_kind == MH_KIND_GET_FIELD || args->info->handle_kind == MH_KIND_PUT_FIELD
          ? args->info->reference->field.class_info
          : args->info->reference->methodref.class_info;

  resolve_class(args->thread, required_type);
  AWAIT(initialize_class, args->thread, required_type->classdesc);

  mh_type_info_t info = {};
  AWAIT(compute_mh_type_info, args->thread, args->cl, args->info, &info);
  CHECK(get_async_result(compute_mh_type_info) == 0);

  // Call MethodType.makeImpl(rtype, ptypes, true)
  cp_method *make = method_lookup(cached_classes(args->thread->vm)->method_type, STR("makeImpl"),
                                  STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                      "lang/invoke/MethodType;"),
                                  false, false);

  self->ptypes_array = make_handle(
      args->thread, CreateObjectArray1D(args->thread, cached_classes(args->thread->vm)->klass, info.ptypes_count));
  for (u32 i = 0; i < info.ptypes_count; ++i) {
    object mirror = (void *)get_class_mirror(args->thread, info.ptypes[i]);
    *((obj_header **)ArrayData(self->ptypes_array->obj) + i) = mirror;
  }
  arrfree(info.ptypes);

  object mirror = (void *)get_class_mirror(args->thread, info.rtype);
  AWAIT(call_interpreter, args->thread, make,
        (stack_value[]){{.obj = mirror}, {.obj = self->ptypes_array->obj}, {.i = 0}});
  stack_value result = get_async_result(call_interpreter);
  drop_handle(args->thread, self->ptypes_array);

  ASYNC_END((void *)result.obj);
}

DEFINE_ASYNC(resolve_mh_vh) {
#define info (args)->info
#define thread (args->thread)

  DCHECK(mh_is_vh(info->handle_kind));

  cp_field_info *field = &info->reference->field;
  resolve_class(thread, field->class_info);
  AWAIT(initialize_class, thread, field->class_info->classdesc);
  field = &info->reference->field; // reload because of the await

  classdesc *DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");

  classdesc *MemberName = LoadClassSynchronous(thread, "java/lang/invoke/MemberName");

  cp_field *f = field_lookup(field->class_info->classdesc, field->nat->name, field->nat->descriptor);
  AWAIT(reflect_initialize_field, thread, field->class_info->classdesc, f);
  handle *member = make_handle(thread, new_object(thread, MemberName));
  cp_method *make_member = method_lookup(MemberName, STR("<init>"), STR("(Ljava/lang/reflect/Field;)V"), false, false);

  call_interpreter_synchronous(thread, make_member,
                               (stack_value[]){{.obj = (void *)member->obj}, {.obj = (void *)f->reflection_field}});

  cp_method *make = method_lookup(DirectMethodHandle, STR("make"),
                                  STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                      "invoke/DirectMethodHandle;"),
                                  false, false);
  stack_value result = call_interpreter_synchronous(thread, make, (stack_value[]){{.obj = member->obj}});
  drop_handle(thread, member);
  ASYNC_END(result.obj);

#undef info
#undef thread
}

DEFINE_ASYNC(resolve_mh_invoke) {
#define info (args)->info
#define thread (args)->thread
#define member (self)->member
#define m (self->m)

  cp_method_info *method = &info->reference->methodref;
  resolve_class(thread, method->class_info);
  AWAIT(initialize_class, thread, info->reference->methodref.class_info->classdesc);
  method = &info->reference->methodref; // reload because of the await

  m = method_lookup(method->class_info->classdesc, method->nat->name, method->nat->descriptor, true, true);
  if (MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind) {
    AWAIT(reflect_initialize_constructor, thread, method->class_info->classdesc, m);
  } else {
    AWAIT(reflect_initialize_method, thread, method->class_info->classdesc, m);
  }

  classdesc *MemberName = LoadClassSynchronous(thread, "java/lang/invoke/MemberName");

  // Call DirectMethodHandle.make(method, true)
  member = make_handle(thread, new_object(thread, MemberName));
  bool is_ctor = MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind;
  cp_method *make_member = method_lookup(
      MemberName, STR("<init>"),
      is_ctor ? STR("(Ljava/lang/reflect/Constructor;)V") : STR("(Ljava/lang/reflect/Method;)V"), false, false);
  AWAIT(call_interpreter, thread, make_member,
        (stack_value[]){{.obj = (void *)member->obj},
                        {.obj = is_ctor ? (void *)m->reflection_ctor : (void *)m->reflection_method}});

  classdesc *DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");
  cp_method *make = method_lookup(DirectMethodHandle, STR("make"),
                                  STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                      "invoke/DirectMethodHandle;"),
                                  false, false);
  AWAIT(call_interpreter, thread, make, (stack_value[]){{.obj = member->obj}});
  stack_value result = get_async_result(call_interpreter);
  drop_handle(thread, member);

  // Now if the member is a variable arity constructor/method, call withVarargs(true) and return the resulting
  // MethodHandle.
  if (m->access_flags & ACCESS_VARARGS) {
    DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");
    cp_method *with_varargs =
        method_lookup(DirectMethodHandle, STR("withVarargs"), STR("(Z)Ljava/lang/invoke/MethodHandle;"), true, false);
    AWAIT(call_interpreter, thread, with_varargs, (stack_value[]){{.obj = (void *)result.obj}, {.i = 1}});
    stack_value result2 = get_async_result(call_interpreter);
    ASYNC_RETURN((void *)result2.obj);
  } else {
    ASYNC_RETURN((void *)result.obj);
  }

  UNREACHABLE();
  ASYNC_END_VOID();

#undef info
#undef thread
#undef member
#undef m
}

DEFINE_ASYNC(resolve_method_handle) {
#define info (args)->info
#define thread (args->thread)
  AWAIT(resolve_mh_mt, thread, info, args->cl);
  info->resolved_mt = get_async_result(resolve_mh_mt);

  if (mh_is_vh(info->handle_kind)) {
    AWAIT(resolve_mh_vh, thread, info);
    ASYNC_RETURN((void *)get_async_result(resolve_mh_vh));
  } else if (mh_is_invoke(info->handle_kind)) {
    AWAIT(resolve_mh_invoke, thread, info);
    ASYNC_RETURN((void *)get_async_result(resolve_mh_vh));
  } else {
    UNREACHABLE();
  }

#undef info
#undef thread

  ASYNC_END_VOID();
}

void class_circularity_error(vm_thread *thread, const classdesc *class) {
  INIT_STACK_STRING(message, 1000);
  message = bprintf(message, "While loading class %.*s", fmt_slice(class->name));
  raise_vm_exception(thread, STR("java/lang/ClassCircularityError"), message);
}

// NOLINTNEXTLINE(misc-no-recursion)
module *get_unnamed_module(vm_thread *thread) {
  vm *vm = thread->vm;
  module *result = get_module(vm, STR("<unnamed>"));
  if (result) {
    return result;
  }

  obj_header *module = new_object(thread, cached_classes(vm)->module);
  if (!module) {
    return nullptr;
  }
  define_module(vm, STR("<unnamed>"), module);
  return get_unnamed_module(thread);
}

bool is_builtin_class(slice chars) {
  return strncmp(chars.chars, "java/", 5) == 0 || strncmp(chars.chars, "javax/", 6) == 0 ||
         strncmp(chars.chars, "jdk/", 4) == 0 || strncmp(chars.chars, "sun/", 4) == 0;
}

// NOLINTNEXTLINE(misc-no-recursion)
classdesc *define_bootstrap_class(vm_thread *thread, slice chars, const u8 *classfile_bytes, size_t classfile_len) {
  return define_class(thread, thread->vm->bootstrap_classloader, chars, classfile_bytes, classfile_len);
}

int check_permitted_subclasses(vm_thread *thread, slice name, cp_class_info *super_class) {
  attribute *attr = find_attribute_by_kind(super_class->classdesc, ATTRIBUTE_KIND_PERMITTED_SUBCLASSES);
  if (attr) {
    // "Otherwise, if the class named as the direct superclass of C has a PermittedSubclasses attribute (§4.7.31) and
    // any of the following is true, derivation throws an IncompatibleClassChangeError:"

    // - The superclass is in a different run-time module than C TODO
    // "C does not have its ACC_PUBLIC flag set (§4.1) and the superclass is in a different run-time package than C
    // (§5.3)." TODO

    // "No entry in the classes array of the superclass's PermittedSubclasses attribute refers to a class or interface
    // with the name N."

    attribute_permitted_subclasses *ps = &attr->permitted_subclasses;
    for (int i = 0; i < ps->entries_count; ++i) {
      cp_class_info *candidate = ps->entries[i];
      if (utf8_equals_utf8(candidate->name, name)) {
        return 0;
      }
    }

    // "class C cannot implement sealed interface I"
    INIT_STACK_STRING(str, 2048);
    str = bprintf(str, "class %.*s cannot implement sealed interface %.*s", fmt_slice(name),
                  fmt_slice(super_class->name));
    raise_incompatible_class_change_error(thread, str);
    return 1;
  }
  return 0;
}

classdesc *define_class(vm_thread *thread, classloader *cl, slice chars, const u8 *classfile_bytes,
                        size_t classfile_len) {
  // "First, the Java Virtual Machine determines whether it has already recorded that L is an initiating loader of a
  // class or interface denoted by N. If so, this creation attempt is invalid and loading throws a LinkageError."
  classdesc *existing = hash_table_lookup(&cl->initiating, chars.chars, (int)chars.len);
  if (existing) {
    raise_vm_exception(thread, STR("java/lang/LinkageError"), STR("Class already defined"));
    return nullptr;
  }

  vm *vm = thread->vm;
  classdesc *class = calloc(1, sizeof(classdesc));

  heap_string format_error;

  parse_result_t error = parse_classfile(classfile_bytes, classfile_len, class, &format_error);
  if (error != PARSE_SUCCESS) {
    raise_vm_exception(thread, STR("java/lang/ClassFormatError"), hslc(format_error));
    class->linkage_error = thread->current_exception;
    free_heap_str(format_error);

    goto error_1;
  }

  // 3. If C has a direct superclass, the symbolic reference from C to its
  // direct superclass is resolved using the algorithm of §5.4.3.1.
  cp_class_info *super = class->super_class;
  if (super) {
    // If the superclass is currently being loaded -> circularity  error
    if (hash_table_lookup(&vm->inchoate_classes, super->name.chars, (int)super->name.len)) {
      class_circularity_error(thread, class);
      goto error_2;
    }

    int status = resolve_class_impl(thread, class->super_class, cl);
    if (status) {
      // Check whether the current exception is a ClassNotFoundException and
      // if so, raise a NoClassDefFoundError TODO
      goto error_2;
    }

    status = check_permitted_subclasses(thread, class->name, class->super_class);
    if (status)
      goto error_2;
  }

  // 4. If C has any direct superinterfaces, the symbolic references from C to
  // its direct superinterfaces are resolved using the algorithm of §5.4.3.1.
  for (int i = 0; i < class->interfaces_count; ++i) {
    cp_class_info *sup = class->interfaces[i];
    if (hash_table_lookup(&vm->inchoate_classes, sup->name.chars, (int)sup->name.len)) {
      class_circularity_error(thread, class);
      goto error_2;
    }

    int status = resolve_class_impl(thread, class->interfaces[i], cl);
    if (status) {
      // Check whether the current exception is a ClassNotFoundException and
      // if so, raise a NoClassDefFoundError TODO
      goto error_2;
    }

    status = check_permitted_subclasses(thread, class->name, sup);
    if (status)
      goto error_2;
  }

  // Look up in the native methods list and add native handles as appropriate
  native_entries *entries = hash_table_lookup(&vm->natives, chars.chars, (int)chars.len);
  if (entries) {
    for (int i = 0; i < arrlen(entries->entries); i++) {
      native_entry *entry = entries->entries + i;
      //      printf("Trying to bind method %.*s on class %.*s\n", fmt_slice(entry->name), fmt_slice(chars));

      for (int j = 0; j < class->methods_count; ++j) {
        cp_method *method = class->methods + j;

        if (utf8_equals_utf8(method->name, entry->name) &&
            utf8_equals_utf8(method->unparsed_descriptor, entry->descriptor)) {
          //          printf("Successfully bound method %.*s on class %.*s\n", fmt_slice(entry->name),
          //          fmt_slice(chars));
          method->native_handle = &entry->callback;
          goto done;
        }
      }

      //      printf("Failed to bind method %.*s on class %.*s\n", fmt_slice(entry->name), fmt_slice(chars));
    done:
    }
  }

  class->kind = CD_KIND_ORDINARY;
  class->classloader = cl;
  class->self->classdesc = class;

  if (is_builtin_class(chars)) {
    class->module = get_module(vm, STR("java.base"));
  } else {
    class->module = get_unnamed_module(thread);
  }

  // "The Java Virtual Machine marks C as having L as its defining class loader and records that L is an initiating
  // loader of C."
  (void)hash_table_insert(&cl->loaded, chars.chars, (int)chars.len, class);
  (void)hash_table_insert(&cl->initiating, chars.chars, (int)chars.len, class);

  return class;

error_2:
  free_classfile(*class);
error_1:
  free(class);
  return nullptr;
}

void dump_trace(vm_thread *thread) {
  // Walk frames and print the method/line number
  stack_frame *frame = thread->stack.top;
  fprintf(stderr, "Start trace:\n");
  while (frame) {
    cp_method *method = frame->method;
    if (is_frame_native(frame)) {
      fprintf(stderr, "  at %.*s.%.*s(Native Method)\n", fmt_slice(method->my_class->name), fmt_slice(method->name));
    } else {
      int line = get_line_number(method->code, frame->program_counter);
      fprintf(stderr, "  at %.*s.%.*s(%.*s:%d)\n", fmt_slice(method->my_class->name), fmt_slice(method->name),
              fmt_slice(method->my_class->source_file ? method->my_class->source_file->name : null_str()), line);
    }
    frame = frame->prev;
  }
}

static classdesc *bootstrap_load_class_from_fs(vm_thread *thread, slice chars) {
  // e.g. "java/lang/Object.class"
  const slice cf_ending = STR(".class");
  INIT_STACK_STRING(filename, MAX_CF_NAME_LENGTH + 6);
  memcpy(filename.chars, chars.chars, chars.len);
  memcpy(filename.chars + chars.len, cf_ending.chars, cf_ending.len);
  filename.len = chars.len + cf_ending.len;

  u8 *bytes;
  size_t cf_len;
  int read_status = lookup_classpath(&thread->vm->bootstrap_classpath, filename, &bytes, &cf_len);
  if (read_status) {
    return nullptr;
  }

  classdesc *class = define_bootstrap_class(thread, chars, bytes, cf_len);
  free(bytes);
  return class;
}

DEFINE_ASYNC(lookup_class) {
#define cl (self->args.cl)
#define thread (self->args.thread)
#define classname (self->chars)

  int dimensions = 0;
  classname = self->args.name;
  while (classname.len > 0 && *classname.chars == '[') // munch '[' at beginning
    dimensions++, classname = subslice(classname, 1);

  DCHECK(dimensions < 255);
  DCHECK(classname.len > 0);

  classdesc *class;
  if (dimensions && *classname.chars != 'L') {
    // Primitive array type
    type_kind kind = read_type_kind_char(*classname.chars);
    class = primitive_classdesc(thread, kind);
  } else {
    if (dimensions) {
      // Strip L and ;
      DCHECK(classname.len >= 2);
      classname = subslice_to(classname, 1, classname.len - 1);
    }
    // "First, the Java Virtual Machine determines whether L has already been recorded as an initiating loader of a
    // class or interface denoted by N. If so, this class or interface is C, and no class creation is necessary."
    class = hash_table_lookup(&cl->initiating, classname.chars, (int)classname.len);
  }

  if (class == nullptr) {
    // Could not find the existing class, so we need to call loadClass on the user-defined class loader, or perform
    // a bootstrap class loader lookup.

    // Detect class circularity errors
    (void)hash_table_insert(&thread->vm->inchoate_classes, classname.chars, (int)classname.len, (void *)1);

    if (cl->is_bootstrap) {
      class = bootstrap_load_class_from_fs(thread, classname);
    } else {
      // (user-defined class loader) "Otherwise, the Java Virtual Machine invokes loadClass(N) on L."
      // (This is why this method has to be async.)

      object java_mirror = cl->java_mirror;
      DCHECK(java_mirror);
      cp_method *method = method_lookup(java_mirror->descriptor, STR("loadClass"),
                                        STR("(Ljava/lang/String;)Ljava/lang/Class;"), true, false);

      INIT_STACK_STRING(with_dots, 1024);
      exchange_slashes_and_dots(&with_dots, classname);
      object string = MakeJStringFromModifiedUTF8(thread, with_dots, false);
      if (string == nullptr) {
        ASYNC_RETURN(nullptr); // oom
      }

      stack_value method_args[2] = {{.obj = cl->java_mirror}, {.obj = string}};
      AWAIT(call_interpreter, thread, method, method_args);
      object class_mirror = get_async_result(call_interpreter).obj;
      // TODO deal with adversarial loadClass implementations
      if (thread->current_exception == nullptr) {
        class = class_mirror ? unmirror_class(class_mirror) : nullptr;
      }
    }

    (void)hash_table_delete(&thread->vm->inchoate_classes, classname.chars, (int)classname.len);

    if (class) {
      // Mark all class loaders in the chain from the current class loader to the defining class loader as being
      // an initiating loader of C
      classloader *curr = cl;
      DCHECK(class->classloader);
      while (curr != class->classloader) {
        classdesc *existing = hash_table_insert(&curr->initiating, classname.chars, (int)classname.len, class);
        DCHECK(existing == nullptr || existing == class);
        curr = curr->parent;
      }
    }
  }

  if (class == nullptr) { // Still couldn't load the class
    if (!self->args.raise_class_not_found) {
      ASYNC_RETURN(nullptr);
    }

    // If the file is ClassNotFoundException, abort to avoid stack overflow
    if (utf8_equals(classname, "java/lang/ClassNotFoundException")) {
      printf("Could not find class %.*s\n", fmt_slice(classname));
      abort();
    }

    if (thread->current_exception == nullptr) { // let any existing exception propagate
      // ClassNotFoundException: com/google/DontBeEvil
      // (For some reason the JVM uses slashes instead of dots)
      raise_vm_exception(thread, STR("java/lang/ClassNotFoundException"), classname);
    }
    ASYNC_RETURN(nullptr);
  }

  // Derive nth dimension
  classdesc *result = class;
  for (int i = 1; i <= dimensions; ++i) {
    get_or_create_array_classdesc(thread, result);
    result = result->array_type;
  }

#undef thread
#undef cl
#undef classname
  ASYNC_END(result);
}

// NOLINTNEXTLINE(misc-no-recursion)
classdesc *bootstrap_lookup_class_impl(vm_thread *thread, const slice name, bool raise_class_not_found) {
  lookup_class_t init = {.args = {thread, name, thread->vm->bootstrap_classloader, raise_class_not_found}};
  future_t result = lookup_class(&init);
  CHECK(result.status == FUTURE_READY);
  return (classdesc *)init._result;
}

// name = "java/lang/Object" or "[[J" or "[Ljava/lang/String;"
// NOLINTNEXTLINE(misc-no-recursion)
classdesc *bootstrap_lookup_class(vm_thread *thread, const slice name) {
  return bootstrap_lookup_class_impl(thread, name, true);
}

void out_of_memory(vm_thread *thread) {
  thread->current_exception = nullptr;

  obj_header *oom = thread->out_of_mem_error;
  // OOM does not need a stack trace
  raise_exception_object(thread, oom);
}

// does atomic access to be thread-safe
void *bump_allocate(vm_thread *thread, size_t bytes) {
  // round up to multiple of 8
  bytes = align_up(bytes, 8);
  vm *vm = thread->vm;

  static_assert(sizeof(vm->heap.heap_used) == sizeof(uintptr_t), "data size cannot be used in atomic addition operation!");

  // bump allocate a slice optimistically
  size_t heap_used = __atomic_fetch_add(&vm->heap.heap_used, bytes, __ATOMIC_SEQ_CST);
  DCHECK(heap_used % 8 == 0);
  DCHECK(heap_used < __atomic_load_n(&vm->heap.heap_used, __ATOMIC_SEQ_CST));
  size_t end;
  [[maybe_unused]] bool overflow = __builtin_add_overflow(heap_used, bytes, &end);
  DCHECK(!overflow); // todo: more strict overflow checking?
  if (end > vm->heap.heap_capacity) {
    scheduled_gc_pause(thread->vm);

    // try again; bump allocate a slice
    heap_used = __atomic_fetch_add(&vm->heap.heap_used, bytes, __ATOMIC_SEQ_CST);
    DCHECK(heap_used % 8 == 0);
    overflow = __builtin_add_overflow(heap_used, bytes, &end);
    DCHECK(!overflow); // todo: more strict overflow checking?
    if (end > vm->heap.heap_capacity) {
      out_of_memory(thread);
      return nullptr;
    }
  }

  void *result = vm->heap.heap + heap_used;
  memset(result, 0, bytes);
  return result;
}

void suggest_bytecode_patch(vm *vm, bytecode_patch_request request) {
  static_assert(sizeof(vm->instruction_patch_queue.num_used) == sizeof(uintptr_t), "data size cannot be used in atomic addition operation!");
  size_t reserved_slice_index;
  for (;;) {
    // bump allocate a slice optimistically
    reserved_slice_index = __atomic_fetch_add(&vm->instruction_patch_queue.num_used, 1, __ATOMIC_SEQ_CST);
    if (reserved_slice_index + 1 >= vm->instruction_patch_queue.capacity) {
      // we need to trigger a GC
      scheduled_gc_pause_patch_request(vm, request);
      return;
    }
    break;
  }

  vm->instruction_patch_queue.buffer[reserved_slice_index] = request;
}

void atomically_patch_instruction_info(bytecode_insn *insn, const bytecode_insn *source) {
  static_assert(sizeof(struct { insn_code_kind kind; void *ic; }) <= sizeof(insn->raw_patch_data_), "raw_patch_data_ is too small");

  // insn->extra_data = source->extra_data;
  __atomic_store_n(&insn->args, source->args, __ATOMIC_SEQ_CST);
  __atomic_store_n(&insn->tos_before, source->tos_before, __ATOMIC_SEQ_CST);
  __atomic_store_n(&insn->tos_after, source->tos_after, __ATOMIC_SEQ_CST);
  __atomic_store_n(&insn->ic2, source->ic2, __ATOMIC_SEQ_CST);
  __atomic_store_n(&insn->raw_patch_data_, source->raw_patch_data_, __ATOMIC_SEQ_CST);
}

inline void gc_pause_if_requested(vm *vm) {
  if (unlikely(should_gc_pause(vm))) {
    scheduled_gc_pause(vm);
  }
}

// Returns true if the class descriptor is a subclass of java.lang.Error.
// NOLINTNEXTLINE(misc-no-recursion)
bool is_error(classdesc *d) {
  return utf8_equals(d->name, "java/lang/Error") || (d->super_class && is_error(d->super_class->classdesc));
}

attribute *find_attribute(attribute *attrs, int attrc, attribute_kind kind) {
  for (int i = 0; i < attrc; ++i)
    if (attrs[i].kind == kind)
      return attrs + i;
  return nullptr;
}

// During initialisation, we need to set the value of static final fields
// if they are provided in the class file.
//
// Returns true if an OOM occurred when initializing string fields.
// only called when classdesc lock is held
bool initialize_constant_value_fields(vm_thread *thread, classdesc *classdesc) {
  CHECK(classdesc->state >= CD_STATE_LINKED, "Class must be linked");
  for (int i = 0; i < classdesc->fields_count; ++i) {
    cp_field *field = classdesc->fields + i;
    if (field->access_flags & ACCESS_STATIC && field->access_flags & ACCESS_FINAL) {
      attribute *attr = find_attribute(field->attributes, field->attributes_count, ATTRIBUTE_KIND_CONSTANT_VALUE);
      if (!attr)
        continue;
      void *p = classdesc->static_fields + field->byte_offset;
      cp_entry *ent = attr->constant_value;
      DCHECK(ent);

      switch (ent->kind) {
      case CP_KIND_INTEGER:
        store_stack_value(p, (stack_value){.i = (int)ent->integral.value}, TYPE_KIND_INT);
        break;
      case CP_KIND_FLOAT:
        store_stack_value(p, (stack_value){.f = (float)ent->floating.value}, TYPE_KIND_FLOAT);
        break;
      case CP_KIND_LONG:
        store_stack_value(p, (stack_value){.l = ent->integral.value}, TYPE_KIND_LONG);
        break;
      case CP_KIND_DOUBLE:
        store_stack_value(p, (stack_value){.d = ent->floating.value}, TYPE_KIND_DOUBLE);
        break;
      case CP_KIND_STRING:
        obj_header *str = MakeJStringFromModifiedUTF8(thread, ent->string.chars, true);
        if (!str)
          return true;
        store_stack_value(p, (stack_value){.obj = str}, TYPE_KIND_REFERENCE);
        break;
      default:
        UNREACHABLE(); // should have been audited at parse time
      }
    }
  }
  return false;
}

// Wrap the currently propagating exception in an ExceptionInInitializerError
// and raise it.
void wrap_in_exception_in_initializer_error(vm_thread *thread) {
  handle *exc = make_handle(thread, thread->current_exception);
  classdesc *EIIE = cached_classes(thread->vm)->exception_in_initializer_error;
  handle *eiie = make_handle(thread, new_object(thread, EIIE));
  cp_method *ctor = method_lookup(EIIE, STR("<init>"), STR("(Ljava/lang/Throwable;)V"), false, false);
  thread->current_exception = nullptr; // clear exception
  call_interpreter_synchronous(thread, ctor, (stack_value[]){{.obj = eiie->obj}, {.obj = exc->obj}});
  DCHECK(!thread->current_exception);
  raise_exception_object(thread, eiie->obj);

  drop_handle(thread, eiie);
  drop_handle(thread, exc);
}

// Call <clinit> on the class, if it hasn't already been called.
// NOLINTNEXTLINE(misc-no-recursion)
DEFINE_ASYNC(initialize_class) {
#define thread (args->thread)
  classdesc *cd = args->classdesc; // must be reloaded after await()
  pthread_mutex_lock(&cd->lock);
  if (cd->kind == CD_KIND_ORDINARY_ARRAY) {
    // Initialize
    pthread_mutex_unlock(&cd->lock);
    ASYNC_RETURN(0);
  }

  bool error; // this is a local, but it's ok because we don't use it between
              // awaits

  DCHECK(cd);
  if (likely(cd->state == CD_STATE_INITIALIZED || cd->state == CD_STATE_LINKAGE_ERROR)) {
    // Class is already initialized
    pthread_mutex_unlock(&cd->lock);
    ASYNC_RETURN(0);
  }

  if (cd->state < CD_STATE_LINKED) {
    error = link_class(thread, cd);
    if (error) {
      DCHECK(thread->current_exception);
      pthread_mutex_unlock(&cd->lock);
      ASYNC_RETURN(0);
    }
  }

  if (cd->state == CD_STATE_INITIALIZING) {
    if (cd->initializing_thread == thread->tid) {
      // The class is currently being initialized by this thread, so we can just return
      pthread_mutex_unlock(&cd->lock);
      ASYNC_RETURN(0);
    }

    // The class is being initialized by a different thread, and we need to wait for it to finish.
    // This effectively busy waits, but this is an edge case so it doesn't matter for now.
    while (cd->state == CD_STATE_INITIALIZING) {
      ((rr_wakeup_info *)self->wakeup_info)->kind = RR_WAKEUP_YIELDING;
      pthread_mutex_unlock(&cd->lock);
      ASYNC_YIELD(self->wakeup_info);
      DEBUG_PEDANTIC_YIELD(*((rr_wakeup_info *)self->wakeup_info));
      cd = args->classdesc; // reload!
      pthread_mutex_lock(&cd->lock); // reäcquire
    }

    pthread_mutex_unlock(&cd->lock);
    ASYNC_RETURN(0); // the class is done
  }

  // TODO handle linkage error
  if (cd->state == CD_STATE_LINKAGE_ERROR) {
    thread->current_exception = nullptr;
    raise_exception_object(thread, cd->linkage_error);
    pthread_mutex_unlock(&cd->lock);
    ASYNC_RETURN(-1);
  }

  cd->state = CD_STATE_INITIALIZING;
  cd->initializing_thread = thread->tid; // mark this thread as initializing the class

  self->recursive_call_space = calloc(1, sizeof(initialize_class_t));
  if (!self->recursive_call_space) {
    out_of_memory(thread);
    pthread_mutex_unlock(&cd->lock);
    ASYNC_RETURN(-1);
  }

  if (cd->super_class) {
    pthread_mutex_unlock(&cd->lock);
    AWAIT_INNER(self->recursive_call_space, initialize_class, thread, cd->super_class->classdesc);
    cd = args->classdesc;
    pthread_mutex_lock(&cd->lock); // reäcquire

    if ((error = self->recursive_call_space->_result))
      goto done;
  }

  for (self->i = 0; (int)self->i < cd->interfaces_count; ++self->i) {
    pthread_mutex_unlock(&cd->lock);
    AWAIT_INNER(self->recursive_call_space, initialize_class, thread, cd->interfaces[self->i]->classdesc);
    cd = args->classdesc;
    pthread_mutex_lock(&cd->lock); // reäcquire

    if ((error = self->recursive_call_space->_result))
      goto done;
  }
  free(self->recursive_call_space);
  self->recursive_call_space = nullptr;

  if ((error = initialize_constant_value_fields(thread, cd))) {
    out_of_memory(thread);
    goto done;
  }

  cp_method *clinit = method_lookup(cd, STR("<clinit>"), STR("()V"), false, false);

  if (clinit) {
    pthread_mutex_unlock(&cd->lock);
    AWAIT(call_interpreter, thread, clinit, nullptr);
    cd = args->classdesc;
    pthread_mutex_lock(&cd->lock); // reäcquire
    if (thread->current_exception && !is_error(thread->current_exception->descriptor)) {
      wrap_in_exception_in_initializer_error(thread);
      goto done;
    }
  }

  error = 0;
done:
  free(self->recursive_call_space);
  args->classdesc->state = error ? CD_STATE_LINKAGE_ERROR : CD_STATE_INITIALIZED;
  args->classdesc->linkage_error = thread->current_exception;
  // Mark all array classes with the same state
  classdesc *arr = args->classdesc;
  while ((arr = arr->array_type)) {
    arr->state = args->classdesc->state;
  }
  pthread_mutex_unlock(&cd->lock);
  ASYNC_END(error);

#undef thread
}

bool method_candidate_matches(const cp_method *candidate, const slice name, const slice method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor.chars ||
          utf8_equals_utf8(candidate->unparsed_descriptor, method_descriptor));
}

// NOLINTNEXTLINE(misc-no-recursion)
cp_method *method_lookup(classdesc *descriptor, const slice name, const slice method_descriptor,
                         bool search_superclasses, bool search_superinterfaces) {
  DCHECK(descriptor->state >= CD_STATE_LINKED);
  classdesc *search = descriptor;
  // if the object is an array and we're looking for a superclass method, the
  // method must be on a superclass
  if (search->kind != CD_KIND_ORDINARY && search_superclasses)
    search = search->super_class->classdesc;
  while (true) {
    for (int i = 0; i < search->methods_count; ++i)
      if (method_candidate_matches(search->methods + i, name, method_descriptor))
        return search->methods + i;
    if (search_superclasses && search->super_class) {
      search = search->super_class->classdesc;
    } else {
      break;
    }
  }
  if (!search_superinterfaces)
    return nullptr;

  for (int i = 0; i < descriptor->interfaces_count; ++i) {
    cp_method *result = method_lookup(descriptor->interfaces[i]->classdesc, name, method_descriptor, false, true);
    if (result)
      return result;
  }

  // Look in superinterfaces of superclasses
  if (search_superclasses && descriptor->super_class) {
    return method_lookup(descriptor->super_class->classdesc, name, method_descriptor, true, true);
  }

  return nullptr;
}

static char *get_next_frame_start(vm_thread *thread) {
  if (thread->stack.top) {
    return (char *)thread->stack.top + sizeof(stack_frame) + thread->stack.top->max_stack * sizeof(stack_value);
  }
  return thread->stack.frame_buffer;
}

// Returns true on failure to initialize the context
static bool initialize_async_ctx(async_run_ctx *ctx, vm_thread *thread, cp_method *method, stack_value *args) {
  assert(method && "Method is null");
  memset(ctx, 0, sizeof(*ctx));

  u8 argc = method_argc(method);
  stack_value *stack_top = (stack_value *)get_next_frame_start(thread);

  if ((uintptr_t)(stack_top + argc) > (uintptr_t)thread->stack.frame_buffer_end) {
    raise_exception_object(thread, thread->stack_overflow_error);
    return true;
  }

  size_t args_size = argc * sizeof(stack_value);
  memcpy(stack_top, args, args_size);

  ctx->thread = thread;
  ctx->frame = push_frame(thread, method, stack_top, argc);
  return !ctx->frame; // true on failure to allocate
}

DEFINE_ASYNC(call_interpreter) {
  if (initialize_async_ctx(&self->ctx, args->thread, args->method, args->args))
    ASYNC_RETURN((stack_value){.l = 0});

  self->ctx.interpreter_state.args = (struct interpret_args){args->thread, self->ctx.frame};
  AWAIT_FUTURE_EXPR(interpret(&self->ctx.interpreter_state));

  stack_value result = self->ctx.interpreter_state._result;
  ASYNC_END(result);
}

int resolve_class_impl(vm_thread *thread, cp_class_info *info, classloader *loader) {
  if (info->classdesc)
    return 0; // already succeeded
  if (info->vm_object) {
    raise_exception_object(thread,
                           info->vm_object); // already failed
    return -1;
  }

  if (loader) {
    lookup_class_t lookup_class_args = {.args = {thread, info->name, loader, true}};
    thread->stack.synchronous_depth++;
    future_t fut = lookup_class(&lookup_class_args);
    thread->stack.synchronous_depth--;
    CHECK(fut.status == FUTURE_READY);
    info->classdesc = (classdesc *)lookup_class_args._result;
  }

  if (!info->classdesc) {
    DCHECK(thread->current_exception);
    info->vm_object = thread->current_exception;
    return -1;
  }

  // TODO check that the class is accessible

  return 0;
}

classloader *get_current_classloader(vm_thread *thread) {
  stack_frame *frame = thread->stack.top;
  if (frame) {
    return frame->method->my_class->classloader;
  }
  return thread->vm->bootstrap_classloader;
}

// TODO make this async
// NOLINTNEXTLINE(misc-no-recursion)
int resolve_class(vm_thread *thread, cp_class_info *info) {
  classloader *loader = get_current_classloader(thread);
  DCHECK(loader); // Need to find
  return resolve_class_impl(thread, info, loader);
}

int resolve_field(vm_thread *thread, cp_field_info *info) {
  if (info->field)
    return 0;
  cp_class_info *class = info->class_info;
  int error = resolve_class(thread, class);
  if (error)
    return error;
  error = link_class(thread, class->classdesc);
  if (error)
    return error;

  // Get offset of field
  DCHECK(class->classdesc->state >= CD_STATE_LINKED);
  cp_field *field = field_lookup(class->classdesc, info->nat->name, info->nat->descriptor);
  info->field = field;
  return field == nullptr;
}

void store_stack_value(void *field_location, stack_value value, type_kind kind) {
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
    DCHECK((value.i == 0) || (value.i == 1));
    [[fallthrough]];
  case TYPE_KIND_BYTE:
    *(jbyte *)field_location = (jbyte)value.i;
    break;
  case TYPE_KIND_CHAR:
    [[fallthrough]];
  case TYPE_KIND_SHORT:
    *(jshort *)field_location = (s16)value.i;
    break;
  case TYPE_KIND_FLOAT:
    *(jfloat *)field_location = value.f;
    break;
  case TYPE_KIND_DOUBLE:
    *(jdouble *)field_location = value.d;
    break;
  case TYPE_KIND_INT:
    *(jint *)field_location = value.i;
    break;
  case TYPE_KIND_LONG:
    *(jlong *)field_location = value.l;
    break;
  case TYPE_KIND_REFERENCE:
    *(object *)field_location = value.obj;
    break;
  case TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
}

stack_value load_stack_value(void *field_location, type_kind kind) {
  stack_value result;
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
  case TYPE_KIND_BYTE: // sign-extend the byte
    result.i = (int)*(s8 *)field_location;
    break;
  case TYPE_KIND_CHAR:
    result.i = *(u16 *)field_location;
    break;
  case TYPE_KIND_SHORT:
    result.i = *(s16 *)field_location;
    break;
  case TYPE_KIND_FLOAT:
    result.f = *(float *)field_location;
    break;
  case TYPE_KIND_DOUBLE:
    result.d = *(double *)field_location;
    break;
  case TYPE_KIND_INT:
    result.i = *(int *)field_location;
    break;
  case TYPE_KIND_LONG:
    result.l = *(s64 *)field_location;
    break;
  case TYPE_KIND_REFERENCE:
    result.obj = *(void **)field_location;
    break;
  case TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
  return result;
}

obj_header *new_object(vm_thread *thread, classdesc *classdesc) {
  return AllocateObject(thread, classdesc, classdesc->instance_bytes);
}

bool is_instanceof_name(const obj_header *mirror, const slice name) {
  return mirror && utf8_equals_utf8(mirror->descriptor->name, name);
}

classdesc *unmirror_class(obj_header *mirror) {
  DCHECK(is_instanceof_name(mirror, STR("java/lang/Class")));
  return ((struct native_Class *)mirror)->reflected_class;
}

cp_field **unmirror_field(obj_header *mirror) {
  DCHECK(is_instanceof_name(mirror, STR("java/lang/reflect/Field")));
  // Fields get copied around, but all reference the "root" created by the VM
  obj_header *root = ((struct native_Field *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct native_Field *)mirror)->reflected_field;
}

cp_method *unmirror_ctor(obj_header *mirror) {
  DCHECK(is_instanceof_name(mirror, STR("java/lang/reflect/Constructor")));
  // Unmirror the class, then get the ->slot method
  struct native_Constructor *m = (struct native_Constructor *)mirror;
  classdesc *class = unmirror_class(m->clazz);
  return class->methods + m->slot;
}

cp_method *unmirror_method(obj_header *mirror) {
  DCHECK(is_instanceof_name(mirror, STR("java/lang/reflect/Method")));
  // Unmirror the class, then get the ->slot method
  struct native_Method *m = (struct native_Method *)mirror;
  classdesc *class = unmirror_class(m->clazz);
  return class->methods + m->slot;
}

struct native_ConstantPool *get_constant_pool_mirror(vm_thread *thread, classdesc *cd) {
  if (!cd)
    return nullptr;
  pthread_mutex_lock(&cd->lock); // todo: check result?
  if (cd->cp_mirror) {
    pthread_mutex_unlock(&cd->lock);
    return cd->cp_mirror;
  }
  classdesc *java_lang_ConstantPool = cached_classes(thread->vm)->constant_pool;
  struct native_ConstantPool *cp_mirror = cd->cp_mirror = (void *)new_object(thread, java_lang_ConstantPool);
  cp_mirror->reflected_class = cd;
  pthread_mutex_unlock(&cd->lock);
  return cp_mirror;
}

// NOLINTNEXTLINE(misc-no-recursion)
struct native_Class *get_class_mirror(vm_thread *thread, classdesc *cd) {
  if (!cd)
    return nullptr;
  pthread_mutex_lock(&cd->lock); // todo: check result?
  if (cd->mirror) {
    pthread_mutex_unlock(&cd->lock);
    return cd->mirror;
  }

  classdesc *java_lang_Class = bootstrap_lookup_class(thread, STR("java/lang/Class"));
  initialize_class_t init = {.args = {thread, java_lang_Class}};
  future_t klass_init_state = initialize_class(&init);
  CHECK(klass_init_state.status == FUTURE_READY);
  if (init._result) {
    // TODO raise exception
    UNREACHABLE();
  }

  cd->mirror = (void *)new_object(thread, java_lang_Class);
  handle *cm_handle = make_handle(thread, (void *)cd->mirror);

#define class_mirror ((struct native_Class *)cm_handle->obj)

  if (class_mirror) {
    class_mirror->reflected_class = cd;
    if (cd->module) {
      class_mirror->module = cd->module->reflection_object;
    }
    object componentType = cd->one_fewer_dim ? (void *)get_class_mirror(thread, cd->one_fewer_dim) : nullptr;
    class_mirror->componentType = componentType;
    class_mirror->classLoader = cd->classloader->java_mirror;
  }
  struct native_Class *result = class_mirror;
  drop_handle(thread, cm_handle);
  pthread_mutex_unlock(&cd->lock);
  return result;
#undef class_mirror
}

bool instanceof_interface(const classdesc *o, const classdesc *target) {
  if (o == target)
    return true;
  for (int i = 0; i < arrlen(o->itables.interfaces); ++i)
    if (o->itables.interfaces[i] == target)
      return true;
  return false;
}

bool instanceof_super(const classdesc *o, const classdesc *target) {
  assert(target->hierarchy_len > 0 && "Invalid hierarchy length");
  assert(target->state >= CD_STATE_LINKED && "Target class not linked");
  assert(o->state >= CD_STATE_LINKED && "Source class not linked");

  return target->hierarchy_len <= o->hierarchy_len             // target is at or higher than o in its chain
         && o->hierarchy[target->hierarchy_len - 1] == target; // it is a superclass!
}

// Returns true if o is an instance of target
// NOLINTNEXTLINE(misc-no-recursion)
bool instanceof(const classdesc *o, const classdesc *target) {
  assert(o->kind != CD_KIND_PRIMITIVE && "instanceof not intended for primitives");
  assert(target->kind != CD_KIND_PRIMITIVE && "instanceof not intended for primitives");

  // TODO compare class loaders too
  if (o == target)
    return true;

  if (unlikely(target->kind != CD_KIND_ORDINARY)) { // target is an array
    if (o->kind == CD_KIND_ORDINARY)
      return false;
    if (o->kind == CD_KIND_ORDINARY_ARRAY) {
      // First remove target_dims from o. If we run out of dimensions, then it's not compatible. Then we perform a
      // normal check.
      int target_dims = target->dimensions;
      while (target_dims--) {
        o = o->one_fewer_dim;
        target = target->one_fewer_dim;
        DCHECK(target);
        if (!o) {
          return false;
        }
      }
      if (o->kind == CD_KIND_PRIMITIVE || target->kind == CD_KIND_PRIMITIVE) {
        return false; // handled earlier
      }
    } else {
      // o is 1D primitive array, equality check suffices
      return target->dimensions == o->dimensions && target->primitive_component == o->primitive_component;
    }
  }

  // o can be compared as a normal object
  const classdesc *desc = o;
  return target->access_flags & ACCESS_INTERFACE ? instanceof_interface(desc, target) : instanceof_super(desc, target);
}

bool method_types_compatible(struct native_MethodType *provider_mt, struct native_MethodType *targ) {
  // Compare ptypes
  if (provider_mt == targ)
    return true;
  if (ArrayLength(provider_mt->ptypes) != ArrayLength(targ->ptypes)) {
    return false;
  }
  for (int i = 0; i < ArrayLength(provider_mt->ptypes); ++i) {
    classdesc *left = unmirror_class(((obj_header **)ArrayData(provider_mt->ptypes))[i]);
    classdesc *right = unmirror_class(((obj_header **)ArrayData(targ->ptypes))[i]);

    if (left != right) {
      return false;
    }
  }
  return true;
}

void wrong_method_type_error([[maybe_unused]] vm_thread *thread, [[maybe_unused]] struct native_MethodType *provider_mt,
                             [[maybe_unused]] struct native_MethodType *targ) {
  UNREACHABLE(); // TODO
}

enum {
  VH_GET,
  VH_SET,
  VH_GET_VOLATILE,
  VH_SET_VOLATILE,
  VH_GET_ACQUIRE,
  VH_SET_RELEASE,
  VH_GET_OPAQUE,
  VH_SET_OPAQUE,
  VH_COMPARE_AND_SET,
  VH_COMPARE_AND_EXCHANGE,
  VH_COMPARE_AND_EXCHANGE_ACQUIRE,
  VH_COMPARE_AND_EXCHANGE_RELEASE,
  VH_WEAK_COMPARE_AND_SET,
  VH_WEAK_COMPARE_AND_SET_PLAIN,
  VH_WEAK_COMPARE_AND_SET_ACQUIRE,
  VH_WEAK_COMPARE_AND_SET_RELEASE,
  VH_GET_AND_SET,
  VH_GET_AND_SET_ACQUIRE,
  VH_GET_AND_SET_RELEASE,
  VH_GET_AND_ADD,
  VH_GET_AND_ADD_ACQUIRE,
  VH_GET_AND_ADD_RELEASE,
  VH_GET_AND_BITWISE_OR,
  VH_GET_AND_BITWISE_OR_RELEASE,
  VH_GET_AND_BITWISE_OR_ACQUIRE,
  VH_GET_AND_BITWISE_AND,
  VH_GET_AND_BITWISE_AND_RELEASE,
  VH_GET_AND_BITWISE_AND_ACQUIRE,
  VH_GET_AND_BITWISE_XOR,
  VH_GET_AND_BITWISE_XOR_RELEASE,
  VH_GET_AND_BITWISE_XOR_ACQUIRE,
};

DEFINE_ASYNC(invokevirtual_signature_polymorphic) {
  self->provider_mt_handle = make_handle(args->thread, (obj_header *) args->provider_mt);
#define target (args->target)
#define provider_methodtype ((struct native_MethodType *) self->provider_mt_handle->obj)
#define thread (args->thread)
  DCHECK(args->method);

  struct native_MethodHandle *mh = (void *)target;
  self->doing_var_handle = false;

doit:
  if (!mh->reflected_mh) {
    // MethodHandle!
    struct native_MethodType *targ = (void *)mh->type;
    assert(targ && "Method type must be non-null");

    bool mts_are_same = method_types_compatible(provider_methodtype, targ);
    bool is_invoke_exact = utf8_equals_utf8(args->method->name, STR("invokeExact"));
    // only raw calls to MethodHandle.invoke involve "asType" conversions
    bool is_invoke = utf8_equals_utf8(args->method->name, STR("invoke")) &&
                     utf8_equals(args->method->my_class->name, "java/lang/invoke/MethodHandle");

    if (is_invoke_exact) {
      if (!mts_are_same) {
        wrong_method_type_error(thread, provider_methodtype, targ);
        drop_handle(thread, self->provider_mt_handle);
        ASYNC_RETURN_VOID();
      }
    }

    if (!mts_are_same && is_invoke) {
      // Call asType to get an adapter handle
      cp_method *asType =
          method_lookup(mh->base.descriptor, STR("asType"),
                        STR("(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;"), true, false);
      if (!asType)
        UNREACHABLE();

      AWAIT(call_interpreter, thread, asType, (stack_value[]){{.obj = (void *)mh}, {.obj = (void *)provider_methodtype}});
      stack_value result = get_async_result(call_interpreter);
      if (thread->current_exception) { // asType failed
        drop_handle(thread, self->provider_mt_handle);
        ASYNC_RETURN_VOID();
      }
      mh = (void *)result.obj;
    }

    struct native_LambdaForm *form = (void *)mh->form;
    struct native_MemberName *name = (void *)form->vmentry;

    method_handle_kind kind = (name->flags >> 24) & 0xf;
    if (kind == MH_KIND_INVOKE_STATIC) {
      self->method = name->vmtarget;

      u8 argc = self->argc = self->method->descriptor->args_count;
      if (self->doing_var_handle) {
        memmove(args->sp_ + 1, args->sp_, sizeof(stack_value) * argc); // includes objectref
      }
      args->sp_->obj = (void *)mh;
      self->frame = push_frame(thread, self->method, args->sp_, argc);

      // TODO arena allocate this in the VM so that it gets freed appropriately
      // if the VM is deleted
      self->interpreter_ctx = calloc(1, sizeof(interpret_t));
      AWAIT_INNER(self->interpreter_ctx, interpret, thread, self->frame);
      if (self->method->descriptor->return_type.base_kind != TYPE_KIND_VOID) {
        // Store the result in the frame
        *args->sp_ = self->interpreter_ctx->_result;
      }
      free(self->interpreter_ctx);
    } else {
      UNREACHABLE();
    }
  } else {
    self->vh = make_handle(thread, target);
    // Call valueFromMethodName on java.lang.invoke.VarHandle.AccessMode with the method name
    // to get the method handle
    classdesc *AccessMode = bootstrap_lookup_class(thread, STR("java/lang/invoke/VarHandle$AccessMode"));
    initialize_class_t init = {.args = {thread, AccessMode}};
    future_t fut = initialize_class(&init);
    CHECK(fut.status == FUTURE_READY);

    cp_method *valueFromMethodName =
        method_lookup(AccessMode, STR("valueFromMethodName"),
                      STR("(Ljava/lang/String;)Ljava/lang/invoke/VarHandle$AccessMode;"), true, false);
    DCHECK(valueFromMethodName);

    // Now invoke it with the name of the method
    void *str = MakeJStringFromData(thread, self->args.method->name, false);
    // TODO oom
    stack_value arg[] = {{.obj = (void *)str}};
    AWAIT(call_interpreter, thread, valueFromMethodName, arg);
    if (thread->current_exception) {
      drop_handle(thread, self->vh);
      drop_handle(thread, self->provider_mt_handle);
      ASYNC_RETURN_VOID();
    }

    self->result = make_handle(thread, get_async_result(call_interpreter).obj);

    // Second, a reference to an instance of java.lang.invoke.MethodType is obtained as if by invocation of the
    // accessModeType method of java.lang.invoke.VarHandle on the instance objectref, with the instance of
    // java.lang.invoke.VarHandle.AccessMode as the argument.
    cp_method *accessModeType =
        method_lookup(self->vh->obj->descriptor, STR("accessModeType"),
                      STR("(Ljava/lang/invoke/VarHandle$AccessMode;)Ljava/lang/invoke/MethodType;"), true, false);
    DCHECK(accessModeType);

    // Now invoke it with the result of the previous call
    stack_value arg2[] = {{.obj = (void *)self->vh->obj}, {.obj = self->result->obj}};
    AWAIT(call_interpreter, thread, accessModeType, arg2);
    if (thread->current_exception) {
      drop_handle(thread, self->vh);
      drop_handle(thread, self->result);
      drop_handle(thread, self->provider_mt_handle);
      ASYNC_RETURN_VOID();
    }

    // Third, a reference to an instance of java.lang.invoke.MethodHandle is obtained as if by invocation of the
    // varHandleExactInvoker method of java.lang.invoke.MethodHandles with the instance of
    // java.lang.invoke.VarHandle.AccessMode as the first argument and the instance of java.lang.invoke.MethodType
    // as the second argument. The resulting instance is called the invoker method handle.
    classdesc *MethodHandles = cached_classes(thread->vm)->method_handles;
    CHECK(MethodHandles);
    stack_value arg3[] = {{.obj = self->result->obj}, {.obj = get_async_result(call_interpreter).obj}};
    cp_method *varHandleExactInvoker =
        method_lookup(MethodHandles, STR("varHandleExactInvoker"),
                      STR("(Ljava/lang/invoke/VarHandle$AccessMode;Ljava/lang/invoke/MethodType;)"
                          "Ljava/lang/invoke/MethodHandle;"),
                      true, false);
    DCHECK(varHandleExactInvoker);
    AWAIT(call_interpreter, thread, varHandleExactInvoker, arg3);
    if (thread->current_exception) {
      drop_handle(thread, self->vh);
      drop_handle(thread, self->result);
      drop_handle(thread, self->provider_mt_handle);
      ASYNC_RETURN_VOID();
    }

    mh = (void *)get_async_result(call_interpreter).obj;
    drop_handle(thread, self->vh);
    drop_handle(thread, self->result);
    self->doing_var_handle = true;

    goto doit;
  }

  drop_handle(thread, self->provider_mt_handle);

  ASYNC_END_VOID();

#undef target
#undef provider_methodtype
#undef thread
}

DEFINE_ASYNC(resolve_methodref) {
#define info args->info
#define thread args->thread

  if (info->resolved) {
    ASYNC_RETURN(0);
  }
  self->klass = info->class_info;
  int status = resolve_class(thread, self->klass);
  if (status) {
    // Failed to resolve the class in question
    ASYNC_RETURN(status);
  }

  AWAIT(initialize_class, thread, self->klass->classdesc);
  if (thread->current_exception) {
    ASYNC_RETURN(1);
  }

  info->resolved = method_lookup(self->klass->classdesc, info->nat->name, info->nat->descriptor, true, true);
  if (!info->resolved) {
    INIT_STACK_STRING(complaint, 1000);
    complaint = bprintf(complaint, "Could not find method %.*s with descriptor %.*s on class %.*s",
                        fmt_slice(info->nat->name), fmt_slice(info->nat->descriptor), fmt_slice(self->klass->name));
    raise_incompatible_class_change_error(thread, complaint);
    ASYNC_RETURN(1);
  }
  ASYNC_END(0);

#undef info
#undef thread
}

int multianewarray(vm_thread *thread, stack_frame *frame, struct multianewarray_data *multianewarray, u16 *sd) {
  int dims = multianewarray->dimensions;
  DCHECK(*sd >= dims);
  // DCHECK(stack_depth(frame) >= dims);
  DCHECK(dims >= 1);

  int error = resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  link_class(thread, multianewarray->entry->classdesc);

  int dim_sizes[kArrayMaxDimensions];
  for (int i = 0; i < dims; ++i) {
    int dim = frame->stack[*sd - dims + i].i;
    if (dim < 0) {
      raise_negative_array_size_exception(thread, dim);
      return -1;
    }

    dim_sizes[i] = dim;
  }

  obj_header *result = CreateArray(thread, multianewarray->entry->classdesc, dim_sizes, dims);
  frame->stack[*sd - dims] = (stack_value){.obj = result};
  *sd -= dims - 1;
  return 0;
}

static stack_value box_cp_integral(vm_thread *thread, cp_kind kind, cp_entry *ent) {
  switch (kind) {
  case CP_KIND_INTEGER: {
    stack_value args[1] = {{.i = (jint)ent->integral.value}};
    // Call Integer.valueOf
    cp_method *valueOf =
        method_lookup(cached_classes(thread->vm)->integer, STR("valueOf"), STR("(I)Ljava/lang/Integer;"), true, false);
    return call_interpreter_synchronous(thread, valueOf, args);
  }
  case CP_KIND_FLOAT: {
    stack_value args[1] = {{.f = (jfloat)ent->floating.value}};
    cp_method *valueOf =
        method_lookup(cached_classes(thread->vm)->float_, STR("valueOf"), STR("(F)Ljava/lang/Float;"), true, false);
    return call_interpreter_synchronous(thread, valueOf, args);
  }
  case CP_KIND_LONG: {
    stack_value args[1] = {{.l = (jlong)ent->integral.value}};
    cp_method *valueOf =
        method_lookup(cached_classes(thread->vm)->long_, STR("valueOf"), STR("(J)Ljava/lang/Long;"), true, false);
    return call_interpreter_synchronous(thread, valueOf, args);
  }
  case CP_KIND_DOUBLE: {
    stack_value args[1] = {{.d = (jdouble)ent->floating.value}};
    cp_method *valueOf =
        method_lookup(cached_classes(thread->vm)->double_, STR("valueOf"), STR("(D)Ljava/lang/Double;"), true, false);
    return call_interpreter_synchronous(thread, valueOf, args);
  }
  default:
    UNREACHABLE();
  }
}

DEFINE_ASYNC(resolve_indy_static_argument) {
#define thread args->thread
#define ent args->ent

  if (cp_kind_is_primitive(ent->kind)) {
    ASYNC_RETURN(box_cp_integral(thread, ent->kind, ent));
  }

  if (ent->kind == CP_KIND_CLASS) {
    resolve_class(thread, &ent->class_info);
    ASYNC_RETURN((stack_value){.obj = (void *)get_class_mirror(thread, ent->class_info.classdesc)});
  }

  if (ent->kind == CP_KIND_STRING) {
    obj_header *string = MakeJStringFromModifiedUTF8(thread, ent->string.chars, true);
    ASYNC_RETURN((stack_value){.obj = string});
  }

  if (ent->kind == CP_KIND_METHOD_TYPE) {
    if (!ent->method_type.resolved_mt) {
      AWAIT(resolve_method_type, thread, self->args.cl, ent->method_type.parsed_descriptor);
      ent->method_type.resolved_mt = get_async_result(resolve_method_type);
    }
    ASYNC_RETURN((stack_value){.obj = (void *)ent->method_type.resolved_mt});
  }

  if (ent->kind == CP_KIND_METHOD_HANDLE) {
    AWAIT(resolve_method_handle, thread, &ent->method_handle, args->cl);
    ASYNC_RETURN((stack_value){.obj = (void *)get_async_result(resolve_method_handle)});
  }

  UNREACHABLE();
  ASYNC_END_VOID();

#undef is_object
#undef thread
#undef ent
}

_Thread_local value handles[256];
_Thread_local bool is_handle[256];

DEFINE_ASYNC(indy_resolve) {
#define thread args->thread
#define indy args->indy
#define m (indy->method)

  // e.g. LambdaMetafactory.metafactory
  AWAIT(resolve_method_handle, thread, indy->method->ref, get_current_classloader(thread));
  if (thread->current_exception) {
    ASYNC_RETURN(1);
  }
  self->bootstrap_handle = make_handle(thread, (void *)get_async_result(resolve_method_handle));

  // MethodHandles class
  classdesc *lookup_class = cached_classes(thread->vm)->method_handles;
  cp_method *lookup_factory =
      method_lookup(lookup_class, STR("lookup"), STR("()Ljava/lang/invoke/MethodHandles$Lookup;"), true, false);

  handle *lookup_handle = make_handle(thread, call_interpreter_synchronous(thread, lookup_factory, nullptr).obj);

  self->invoke_array =
      make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->object, m->args_count + 3));

  self->static_i = 0;
  for (; self->static_i < m->args_count; ++self->static_i) {
    cp_entry *arg = m->args[self->static_i];
    AWAIT(resolve_indy_static_argument, thread, arg, get_current_classloader(thread));
    ReferenceArrayStore(self->invoke_array->obj, self->static_i + 3,
                        get_async_result(resolve_indy_static_argument).obj);
  }

  handle *name = make_handle(thread, MakeJStringFromModifiedUTF8(thread, indy->name_and_type->name, true));
  AWAIT(resolve_method_type, thread, get_current_classloader(thread), indy->method_descriptor);
  indy->resolved_mt = get_async_result(resolve_method_type);
  ReferenceArrayStore(self->invoke_array->obj, 0, lookup_handle->obj);
  drop_handle(thread, lookup_handle);
  ReferenceArrayStore(self->invoke_array->obj, 1, name->obj);
  drop_handle(thread, name);
  ReferenceArrayStore(self->invoke_array->obj, 2, (void *)indy->resolved_mt);

  // Invoke the bootstrap method using invokeWithArguments
  cp_method *invokeWithArguments = method_lookup(self->bootstrap_handle->obj->descriptor, STR("invokeWithArguments"),
                                                 STR("([Ljava/lang/Object;)Ljava/lang/Object;"), true, false);

  object bh = self->bootstrap_handle->obj, invoke_array = self->invoke_array->obj;
  drop_handle(thread, self->bootstrap_handle);
  drop_handle(thread, self->invoke_array);

  AWAIT(call_interpreter, thread, invokeWithArguments, (stack_value[]){{.obj = bh}, {.obj = invoke_array}});
  stack_value res = get_async_result(call_interpreter);

  int result;
  if (thread->current_exception) {
    result = -1;
  } else {
    DCHECK(res.obj);
    args->insn->ic = res.obj;
    result = 0;
  }

  ASYNC_END(result);
#undef m
#undef thread
#undef indy
}

int max_calls = 4251;

EMSCRIPTEN_KEEPALIVE
int set_max_calls(int calls) {
  max_calls = calls;
  return 0;
}

DEFINE_ASYNC(run_native) {
#define frame args->frame
#define thread args->thread

  DCHECK(frame, "frame is null");

  native_callback *hand = frame->method->native_handle;
  bool is_static = frame->method->access_flags & ACCESS_STATIC;
  handle *target_handle = is_static ? nullptr : get_native_args(frame)[0].handle;
  value *native_args = get_native_args(frame) + (is_static ? 0 : 1);
  u16 argc = frame->num_locals - !is_static;

  if (!hand->async_ctx_bytes) {
    stack_value result = hand->sync(thread, target_handle, native_args, argc);
    ASYNC_RETURN(result);
  }

  self->native_struct = malloc(hand->async_ctx_bytes);
  // todo: commenting out the unsafe allocation stuff causes mallocations to not be tracked down
//  arrput(thread->vm->unsafe_allocations, self->native_struct);

  *self->native_struct = (async_natives_args){{thread, target_handle, native_args, argc}, 0};
  AWAIT_FUTURE_EXPR(((native_callback *)frame->method->native_handle)->async(self->native_struct));
  // We've laid out the context struct so that the result is always at offset 0
  stack_value result = ((async_natives_args *)self->native_struct)->result;
  free(self->native_struct);
//  remove_unsafe_allocation(thread->vm, self->native_struct);

  ASYNC_END(result);

#undef thread
#undef frame
}

// Main interpreter
DEFINE_ASYNC(interpret) {
#define thread args->thread
#define raw_frame args->raw_frame
  DCHECK(thread->stack.top == raw_frame, "Frame is not last frame on stack");

  for (;;) {
    future_t f;
    stack_value the_result = interpret_2(&f, thread, raw_frame);
    if (f.status == FUTURE_READY) {
      ASYNC_RETURN(the_result);
    }
    ASYNC_YIELD(f.wakeup);
  }

  ASYNC_END_VOID();
#undef thread
#undef raw_frame
}
// #pragma GCC diagnostic pop

int get_line_number(const attribute_code *code, u16 pc) {
  DCHECK(code, "code is null");
  attribute_line_number_table *table = code->line_number_table;
  if (!table || pc >= code->insn_count)
    return -1;
  // Look up original PC (the instruction is tagged with it)
  int original_pc = code->code[pc].original_pc;
  int low = 0, high = table->entry_count - 1;
  while (low <= high) { // binary search for first entry with start_pc <= pc
    int mid = (low + high) / 2;
    line_number_table_entry entry = table->entries[mid];
    if (entry.start_pc <= original_pc &&
        (mid == table->entry_count - 1 || table->entries[mid + 1].start_pc > original_pc)) {
      return entry.line;
    }
    if (entry.start_pc < original_pc) {
      low = mid + 1;
    } else {
      high = mid - 1;
    }
  }
  return -1;
}

obj_header *get_main_thread_group(vm_thread *thread) {
  vm *vm = thread->vm;
  if (!vm->main_thread_group) {
    classdesc *ThreadGroup = cached_classes(vm)->thread_group;
    cp_method *init = method_lookup(ThreadGroup, STR("<init>"), STR("()V"), false, false);

    DCHECK(init);

    obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    stack_value args[1] = {(stack_value){.obj = thread_group}};
    call_interpreter_synchronous(thread, init, args); // ThreadGroup constructor doesn't do much
  }
  return vm->main_thread_group;
}

bool thread_is_daemon(vm_thread *thread) {
  struct native_Thread *thread_obj = thread->thread_obj;
  DCHECK(thread_obj);

  // The daemon field is stored in the "holder" of the Java Thread object.
  object holder = thread_obj->holder;
  return LoadFieldBoolean(holder, "daemon");
}