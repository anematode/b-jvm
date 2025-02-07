#define AGGRESSIVE_DEBUG 0

// Skip the memset(...) call to clear each frame's locals/stack. This messes
// up the debug dumps, but makes setting up frames faster.
#define SKIP_CLEARING_FRAME 1

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "analysis.h"
#include "arrays.h"
#include "bjvm.h"
#include "classloader.h"
#include "objects.h"
#include "util.h"
#include <config.h>
#include <exceptions.h>
#include <gc.h>
#include <reflection.h>

#include "cached_classdescs.h"
#include <errno.h>
#include <linkage.h>
#include <sys/mman.h>

/// Looks up a class and initializes it if it needs to be initialized.
/// This will abort() if the class's <clinit> either throws an excpetion or
/// tries to suspend -- so be VERY careful when calling this.
#define LoadClassSynchronous(thread, name)                                                                             \
  ({                                                                                                                   \
    bjvm_obj_header *exception = thread->current_exception;                                                            \
    thread->current_exception = nullptr;                                                                               \
    bjvm_classdesc *c = bootstrap_lookup_class(thread, STR(name));                                                     \
    AWAIT_READY(bjvm_initialize_class, thread, c);                                                                     \
    if (unlikely(thread->current_exception)) {                                                                         \
      UNREACHABLE("exception thrown in LoadClassSynchronous");                                                         \
    }                                                                                                                  \
    thread->current_exception = exception;                                                                             \
    c;                                                                                                                 \
  })

DECLARE_ASYNC(int, init_cached_classdescs,
  locals(
    bjvm_classdesc * *cached_classdescs;
    u16 i;
  ),
  arguments(bjvm_thread *thread),
  invoked_methods(
    invoked_method(bjvm_initialize_class)
  )
);

DEFINE_ASYNC(init_cached_classdescs) {
  DCHECK(!args->thread->vm->cached_classdescs);

  self->cached_classdescs = malloc(cached_classdesc_count * sizeof(bjvm_classdesc *));
  args->thread->vm->cached_classdescs = (struct bjvm_cached_classdescs *)self->cached_classdescs;

  if (!self->cached_classdescs) {
    bjvm_out_of_memory(args->thread);
    ASYNC_RETURN(-1);
  }

  for (self->i = 0; self->i < cached_classdesc_count; self->i++) {
    char const *name = cached_classdesc_paths[self->i];
    self->cached_classdescs[self->i] = bootstrap_lookup_class(args->thread, str_to_utf8(name));

    AWAIT(bjvm_initialize_class, args->thread, self->cached_classdescs[self->i]);
    int result = get_async_result(bjvm_initialize_class);

    if (result != 0) {
      free(self->cached_classdescs);
      ASYNC_RETURN(result);
    }
  }
  ASYNC_END(0);
}

#define MAX_CF_NAME_LENGTH 1000

u16 stack_depth(const bjvm_stack_frame *frame) {
  DCHECK(!bjvm_is_frame_native(frame) && "Can't get stack depth of native frame");
  DCHECK(frame->method && "Can't get stack depth of fake frame");
  int pc = frame->plain.program_counter;
  if (pc == 0)
    return 0;
  bjvm_code_analysis *analy = frame->method->code_analysis;
  DCHECK(pc < analy->insn_count);
  return analy->insn_index_to_stack_depth[pc];
}

bjvm_value *bjvm_get_native_args(const bjvm_stack_frame *frame) {
  DCHECK(bjvm_is_frame_native(frame));
  return ((bjvm_value *)frame) - frame->num_locals;
}

bjvm_stack_value *frame_stack(bjvm_stack_frame *frame) {
  DCHECK(!bjvm_is_frame_native(frame));
  return frame->plain.stack;
}

bjvm_native_frame *bjvm_get_native_frame_data(bjvm_stack_frame *frame) {
  DCHECK(bjvm_is_frame_native(frame));
  return &frame->native;
}

bjvm_plain_frame *bjvm_get_plain_frame(bjvm_stack_frame *frame) {
  DCHECK(!bjvm_is_frame_native(frame));
  return &frame->plain;
}

bjvm_cp_method *bjvm_get_frame_method(bjvm_stack_frame *frame) { return frame->method; }

bjvm_obj_header *bjvm_deref_js_handle(bjvm_vm *vm, int index) {
  if (index < 0 || index >= arrlen(vm->js_handles)) {
    return nullptr;
  }
  return vm->js_handles[index];
}

int bjvm_make_js_handle(bjvm_vm *vm, bjvm_obj_header *obj) {
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

void bjvm_drop_js_handle(bjvm_vm *vm, int index) {
  if (index < 0 || index >= arrlen(vm->js_handles)) {
    return;
  }
  vm->js_handles[index] = nullptr;
}

[[maybe_unused]] static int handles_count(bjvm_thread * thread) {
  int count = 0;
  for (int i = 0; i < thread->handles_capacity; ++i) {
    if (thread->handles[i].obj) {
      count++;
    }
  }
  return count;
}

bjvm_handle *bjvm_make_handle_impl(bjvm_thread *thread, bjvm_obj_header *obj, int line_no) {
  if (!obj)
    return &thread->null_handle;
  for (int i = 0; i < thread->handles_capacity; ++i) {
    if (!thread->handles[i].obj) {
      thread->handles[i].obj = obj;
#ifndef NDEBUG
      thread->handles[i].line = line_no;
#endif
      return thread->handles + i;
    }
  }

#ifndef NDEBUG
  // Print where the handles were allocated
  fprintf(stderr, "Handle exhaustion: Lines ");
  for (int i = 0; i < thread->handles_capacity; ++i) {
    if (thread->handles[i].obj) {
      fprintf(stderr, "%d ", thread->handles[i].line);
    }
  }
  fprintf(stderr, "\n");
#endif
  UNREACHABLE(); // When we need more handles, rewrite to use a LL impl
}

void bjvm_drop_handle(bjvm_thread *thread, bjvm_handle *handle) {
  if (!handle || handle == &thread->null_handle)
    return;
  DCHECK(handle >= thread->handles && handle < thread->handles + thread->handles_capacity);
  handle->obj = nullptr;
}

// For each argument, if it's a reference, wrap it in a handle; otherwise
// just memcpy it over since the representations of primitives are the same
// between bjvm_stack_value and bjvm_value
static void make_handles_array(bjvm_thread *thread, const bjvm_method_descriptor *descriptor, bool is_static,
                               bjvm_stack_value *stack_args, bjvm_value *args) {
  u8 argc = descriptor->args_count;
  int j = 0;
  if (!is_static) {
    args[j].handle = bjvm_make_handle(thread, stack_args[j].obj);
    ++j;
  }
  for (int i = 0; i < argc; ++i, ++j) {
    if (field_to_kind(descriptor->args + i) == BJVM_TYPE_KIND_REFERENCE) {
      args[j].handle = bjvm_make_handle(thread, stack_args[j].obj);
    } else {
      memcpy(args + j, stack_args + j, sizeof(bjvm_stack_value));
    }
  }
}

static void drop_handles_array(bjvm_thread *thread, const bjvm_cp_method *method, const bjvm_method_descriptor *desc,
                               bjvm_value *args) {
  bool is_static = method->access_flags & BJVM_ACCESS_STATIC;
  if (!is_static)
    bjvm_drop_handle(thread, args[0].handle);
  for (int i = 0; i < desc->args_count; ++i)
    if (field_to_kind(desc->args + i) == BJVM_TYPE_KIND_REFERENCE)
      bjvm_drop_handle(thread, args[i + !is_static].handle);
}

bjvm_stack_frame *bjvm_push_native_frame(bjvm_thread *thread, bjvm_cp_method *method,
                                         const bjvm_method_descriptor *descriptor, bjvm_stack_value *args, u8 argc) {
  bjvm_native_callback *native = method->native_handle;
  if (!native) {
    raise_unsatisfied_link_error(thread, method);
    return nullptr;
  }

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  size_t args_bytes = argc * sizeof(bjvm_value);
  size_t total = header_bytes + args_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  bjvm_value *locals = (bjvm_value *)(args + argc); // reserve new memory on stack
  bjvm_stack_frame *frame = (bjvm_stack_frame *)(locals + argc);

  DCHECK((uintptr_t)frame % 8 == 0 && "Frame is aligned");

  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;

  thread->frame_buffer_used = (char *)frame + sizeof(*frame) - thread->frame_buffer;

  frame->is_native = 1;
  frame->num_locals = argc;
  frame->method = method;
  frame->native.method_shape = descriptor;
  frame->native.state = 0;
  frame->is_async_suspended = false;

  // Now wrap arguments in handles and copy them into the frame
  make_handles_array(thread, descriptor, method->access_flags & BJVM_ACCESS_STATIC, args, locals);
  return frame;
}

bjvm_stack_frame *bjvm_push_plain_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, u8 argc) {
  const bjvm_attribute_code *code = method->code;
  if (!code) {
    raise_abstract_method_error(thread, method);
    return nullptr;
  }

  DCHECK(argc <= code->max_locals);

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  size_t values_bytes = code->max_stack * sizeof(bjvm_stack_value);
  size_t total = header_bytes + values_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  //  bjvm_stack_frame *frame = (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  bjvm_stack_frame *frame = (bjvm_stack_frame *)(args + code->max_locals);

  thread->frame_buffer_used = (char *)(frame->plain.stack + code->max_stack) - thread->frame_buffer;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;
  frame->is_native = 0;
  frame->num_locals = code->max_locals;
  frame->plain.program_counter = 0;
  frame->plain.max_stack = code->max_stack;
  frame->method = method;
  frame->is_async_suspended = false;

  memset(frame_stack(frame), 0x0, code->max_stack * sizeof(bjvm_stack_value));

  return frame;
}

// Push a (native or plain) frame onto the execution stack, but do not execute
// any native code or bytecode.
//
// The number of arguments (argc) is an invariant of the method, but should be
// supplied as an argument for debug checking.
//
// This function must not be used for signature polymorphic methods -- use
// bjvm_invokevirtual_signature_polymorphic for that.
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
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, u8 argc) {
  DCHECK(method != nullptr && "Method is null");
  DCHECK(argc == bjvm_argc(method) && "Wrong argc");
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    return bjvm_push_native_frame(thread, method, method->descriptor, args, argc);
  }
  return bjvm_push_plain_frame(thread, method, args, argc);
}

const char *infer_type(bjvm_code_analysis *analysis, int insn, int index) {
  bjvm_compressed_bitset refs = analysis->insn_index_to_references[insn], ints = analysis->insn_index_to_ints[insn],
                         floats = analysis->insn_index_to_floats[insn], doubles = analysis->insn_index_to_doubles[insn],
                         longs = analysis->insn_index_to_longs[insn];
  if (bjvm_test_compressed_bitset(refs, index))
    return "ref";
  if (bjvm_test_compressed_bitset(ints, index))
    return "int";
  if (bjvm_test_compressed_bitset(floats, index))
    return "float";
  if (bjvm_test_compressed_bitset(doubles, index))
    return "double";
  if (bjvm_test_compressed_bitset(longs, index))
    return "long";
  return "void";
}

void dump_frame(FILE *stream, const bjvm_stack_frame *frame) {
  DCHECK(!bjvm_is_frame_native(frame) && "Can't dump native frame");

  char buf[5000] = {0}, *write = buf, *end = buf + sizeof(buf);
  int sd = stack_depth(frame);

  for (int i = 0; i < sd; ++i) {
    bjvm_stack_value value = frame_stack((void *)frame)[i];
    const char *is_ref = infer_type(frame->method->code_analysis, frame->plain.program_counter, i);
    write += snprintf(write, end - write, " stack[%d] = [ ref = %p, int = %d ] %s\n", i, value.obj, value.i, is_ref);
  }

  for (int i = 0; i < frame->num_locals; ++i) {
    bjvm_stack_value value = frame_locals(frame)[i];
    const char *is_ref =
        infer_type(frame->method->code_analysis, frame->plain.program_counter, i + frame->plain.max_stack);
    write += snprintf(write, end - write, "locals[%d] = [ ref = %p, int = %d ] %s\n", i, value.obj, value.i, is_ref);
  }

  fprintf(stream, "%s", buf);
}

void bjvm_pop_frame(bjvm_thread *thr, [[maybe_unused]] const bjvm_stack_frame *reference) {
  DCHECK(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  DCHECK(reference == nullptr || reference == frame);
  if (bjvm_is_frame_native(frame)) {
    drop_handles_array(thr, frame->method, frame->native.method_shape, bjvm_get_native_args(frame));
  }
  thr->frames_count--;
  thr->frame_buffer_used =
      thr->frames_count == 0 ? 0 : (char *)(frame->plain.stack + frame->plain.max_stack) - thr->frame_buffer;
}

// Symmetry with make_primitive_classdesc
static void free_primitive_classdesc(bjvm_classdesc *classdesc) {
  DCHECK(classdesc->kind == BJVM_CD_KIND_PRIMITIVE);
  if (classdesc->array_type)
    classdesc->array_type->dtor(classdesc->array_type);
  free_heap_str(classdesc->name);
  arena_uninit(&classdesc->arena);
  free(classdesc);
}

typedef struct {
  slice name;
  slice descriptor;

  bjvm_native_callback callback;
} native_entry;

typedef struct {
  native_entry *entries;
  int entries_count;
  int entries_cap;
} native_entries;

void free_native_entries(void *entries_) {
  if (!entries_)
    return;

  free(((native_entries *)entries_)->entries);
  free(entries_);
}

extern size_t bjvm_natives_count;
extern bjvm_native_t *bjvm_natives[];

void bjvm_register_native(bjvm_vm *vm, slice class, const slice method_name, const slice method_descriptor,
                          bjvm_native_callback callback) {
  if (class.chars[0] == '/') class = subslice(class, 1);

  heap_string heap_class = make_heap_str_from(class);
  for (size_t i = 0; i < class.len; i++) { // hacky way to avoid emscripten from complaining about symbol names
    if (heap_class.chars[i] == '_') heap_class.chars[i] = '$';
  }
  class = hslc(heap_class);

  bjvm_classdesc *cd = bjvm_hash_table_lookup(&vm->classes, class.chars, class.len);
  CHECK (cd == nullptr, "%.*s: Natives must be registered before class is loaded", fmt_slice(class));

  native_entries *existing = bjvm_hash_table_lookup(&vm->natives, class.chars, class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)bjvm_hash_table_insert(&vm->natives, class.chars, class.len, existing);
  }

  native_entry *ent = VECTOR_PUSH(existing->entries, existing->entries_count, existing->entries_cap);
  ent->name = method_name;
  ent->descriptor = method_descriptor;
  ent->callback = callback;

  free_heap_str(heap_class);
}

void read_string(bjvm_thread *, bjvm_obj_header *obj, s8 **buf, size_t *len) {
  DCHECK(utf8_equals(hslc(obj->descriptor->name), "java/lang/String"));
  bjvm_obj_header *array = ((struct bjvm_native_String *)obj)->value;
  *buf = ArrayData(array);
  *len = *ArrayLength(array);
}

int read_string_to_utf8(bjvm_thread *thread, heap_string *result, bjvm_obj_header *obj) {
  DCHECK(obj && "String is null");

  s8 *buf;
  size_t len;
  read_string(nullptr, obj, &buf, &len);
  char *cbuf = malloc(len + 1);

  if (!cbuf) {
    bjvm_out_of_memory(thread);
    return -1;
  }

  for (size_t i = 0; i < len; ++i) {
    cbuf[i] = buf[i];
  }
  cbuf[len] = 0;
  *result = (heap_string){.chars = cbuf, .len = len};

  return 0;
}

void *ArrayData(bjvm_obj_header *array);
bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target);

int primitive_order(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return 0;
  case BJVM_TYPE_KIND_CHAR:
    return 1;
  case BJVM_TYPE_KIND_FLOAT:
    return 2;
  case BJVM_TYPE_KIND_DOUBLE:
    return 3;
  case BJVM_TYPE_KIND_BYTE:
    return 4;
  case BJVM_TYPE_KIND_SHORT:
    return 5;
  case BJVM_TYPE_KIND_INT:
    return 6;
  case BJVM_TYPE_KIND_LONG:
    return 7;
  case BJVM_TYPE_KIND_VOID:
    return 8;
  default:
    UNREACHABLE();
  }
}

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread, slice name) {
  const char *chars = name.chars;
  if (chars[0] == 'L') {
    name = subslice_to(name, 1, name.len - 1);
    return bootstrap_lookup_class(thread, name);
  }
  if (chars[0] == '[')
    return bootstrap_lookup_class(thread, name);
  switch (chars[0]) {
  case 'Z':
  case 'B':
  case 'C':
  case 'S':
  case 'I':
  case 'J':
  case 'F':
  case 'D':
  case 'V':
    return bjvm_primitive_classdesc(thread, chars[0]);
  default:
    UNREACHABLE();
  }
}

bjvm_classdesc *bjvm_primitive_classdesc(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  bjvm_vm *vm = thread->vm;
  return vm->primitive_classes[primitive_order(prim_kind)];
}

struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  return bjvm_primitive_classdesc(thread, prim_kind)->mirror;
}

bjvm_classdesc *bjvm_make_primitive_classdesc(bjvm_type_kind kind, const slice name) {
  bjvm_classdesc *desc = calloc(1, sizeof(bjvm_classdesc));

  desc->kind = BJVM_CD_KIND_PRIMITIVE;
  desc->super_class = nullptr;
  desc->name = make_heap_str_from(name);
  desc->access_flags = BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL | BJVM_ACCESS_ABSTRACT;
  desc->array_type = nullptr;
  desc->primitive_component = kind;
  desc->dtor = free_primitive_classdesc;
  desc->classloader = &bjvm_bootstrap_classloader;

  return desc;
}

void bjvm_vm_init_primitive_classes(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (vm->primitive_classes[0])
    return; // already initialized

  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_BOOLEAN)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_BOOLEAN, STR("boolean"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_BYTE)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_BYTE, STR("byte"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_CHAR)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_CHAR, STR("char"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_SHORT)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_SHORT, STR("short"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_INT)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_INT, STR("int"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_LONG)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_LONG, STR("long"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_FLOAT)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_FLOAT, STR("float"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_DOUBLE)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_DOUBLE, STR("double"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_VOID)] =
      bjvm_make_primitive_classdesc(BJVM_TYPE_KIND_VOID, STR("void"));

  // Set up mirrors
  for (int i = 0; i < 9; ++i) {
    bjvm_classdesc *desc = vm->primitive_classes[i];
    desc->mirror = bjvm_get_class_mirror(thread, desc);
  }
}

static slice get_default_boot_cp() {
#if defined(HAVE_GETENV)
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

bjvm_vm_options bjvm_default_vm_options() {
  bjvm_vm_options options = {0};
  options.heap_size = 1 << 22;
  options.runtime_classpath = get_default_boot_cp();

  return options;
}

bjvm_vm_options *bjvm_default_vm_options_ptr() {
  bjvm_vm_options *options = malloc(sizeof(bjvm_vm_options));
  *options = bjvm_default_vm_options();
  return options;
}

static void _free_classdesc(void *cd) {
  bjvm_classdesc *classdesc = cd;
  classdesc->dtor(classdesc);
}

void existing_classes_are_javabase(bjvm_vm *vm, bjvm_module *module) {
  // Iterate through all bootstrap-loaded classes and assign them to the module
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&vm->classes);
  char *key;
  size_t key_len;
  bjvm_classdesc *classdesc;
  while (bjvm_hash_table_iterator_has_next(it, &key, &key_len, (void **)&classdesc)) {
    if (classdesc->classloader == &bjvm_bootstrap_classloader) {
      classdesc->module = module;
      if (classdesc->mirror) {
        classdesc->mirror->module = module->reflection_object;
      }
    }
    bjvm_hash_table_iterator_next(&it);
  }
}

int bjvm_define_module(bjvm_vm *vm, slice module_name, bjvm_obj_header *module) {
  bjvm_module *mod = calloc(1, sizeof(bjvm_module));
  mod->reflection_object = module;
  (void)bjvm_hash_table_insert(&vm->modules, module_name.chars, module_name.len, mod);
  if (utf8_equals(module_name, "java.base")) {
    existing_classes_are_javabase(vm, mod);
  }
  return 0;
}

bjvm_module *bjvm_get_module(bjvm_vm *vm, slice module_name) {
  return bjvm_hash_table_lookup(&vm->modules, module_name.chars, module_name.len);
}

#define OOM_SLOP_BYTES (1 << 12)

bjvm_vm *bjvm_create_vm(const bjvm_vm_options options) {
  bjvm_vm *vm = calloc(1, sizeof(bjvm_vm));

  INIT_STACK_STRING(classpath, 1000);
  classpath = bprintf(classpath, "%.*s:%.*s", fmt_slice(options.runtime_classpath), fmt_slice(options.classpath));

  char *error = bjvm_init_classpath(&vm->classpath, classpath);
  if (error) {
    fprintf(stderr, "Classpath error: %s", error);
    free(error);
    return nullptr;
  }

  vm->classes = bjvm_make_hash_table(_free_classdesc, 0.75, 16);
  vm->inchoate_classes = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->natives = bjvm_make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->class_padding = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->modules = bjvm_make_hash_table(free, 0.75, 16);
  vm->main_thread_group = nullptr;

  vm->heap = aligned_alloc(4096, options.heap_size);
  vm->heap_used = 0;
  vm->heap_capacity = options.heap_size;
  vm->true_heap_capacity = vm->heap_capacity + OOM_SLOP_BYTES;
  vm->active_threads = nullptr;
  vm->active_thread_count = vm->active_thread_cap = 0;

  vm->read_stdin = options.read_stdin;
  vm->poll_available_stdin = options.poll_available_stdin;
  vm->write_stdout = options.write_stdout;
  vm->write_stderr = options.write_stderr;
  vm->stdio_override_param = options.stdio_override_param;

  vm->next_tid = 0;

  for (size_t i = 0; i < bjvm_natives_count; ++i) {
    bjvm_native_t const *native_ptr = bjvm_natives[i];
    bjvm_register_native(vm, native_ptr->class_path, native_ptr->method_name, native_ptr->method_descriptor,
                         native_ptr->callback);
  }

  bjvm_register_native_padding(vm);

  return vm;
}

void free_unsafe_allocations(bjvm_vm *vm) {
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

void bjvm_free_vm(bjvm_vm *vm) {
  bjvm_free_hash_table(vm->classes);
  bjvm_free_hash_table(vm->natives);
  bjvm_free_hash_table(vm->inchoate_classes);
  bjvm_free_hash_table(vm->interned_strings);
  bjvm_free_hash_table(vm->class_padding);
  bjvm_free_hash_table(vm->modules);

  if (vm->primitive_classes[0]) {
    for (int i = 0; i < 9; ++i) {
      vm->primitive_classes[i]->dtor(vm->primitive_classes[i]);
    }
  }

  bjvm_free_classpath(&vm->classpath);

  free(vm->cached_classdescs);
  free(vm->active_threads);
  free(vm->heap);
  free_unsafe_allocations(vm);

  free(vm);
}

bjvm_thread_options bjvm_default_thread_options() {
  bjvm_thread_options options = {};
  options.stack_space = 1 << 19;
  options.js_jit_enabled = true;
  options.thread_group = nullptr;
  return options;
}

/// Invokes a Java method.  Sets thread->current_exception if an exception is
/// thrown and causes UB if
bjvm_stack_value call_interpreter_synchronous(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args) {
  if (args == nullptr) {
    args = (bjvm_stack_value[]){};
  }

  call_interpreter_t ctx = (call_interpreter_t){.args = {thread, method, args}};
  future_t fut = call_interpreter(&ctx);
  CHECK(fut.status == FUTURE_READY, "method tried to suspend");

  return ctx._result;
}

__attribute__((noinline)) bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc, slice const name,
                                                           slice const descriptor) {
  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_cp_field *field = classdesc->fields + i;
    if (utf8_equals_utf8(field->name, name) && utf8_equals_utf8(field->descriptor, descriptor)) {
      return field;
    }
  }

  // Then look on superinterfaces
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    bjvm_cp_field *result = bjvm_field_lookup(classdesc->interfaces[i]->classdesc, name, descriptor);
    if (result)
      return result;
  }

  if (classdesc->super_class) {
    return bjvm_field_lookup(classdesc->super_class->classdesc, name, descriptor);
  }

  return nullptr;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field, bjvm_stack_value bjvm_stack_value) {
  store_stack_value((void *)obj + field->byte_offset, bjvm_stack_value, field_to_kind(&field->parsed_descriptor));
}

void bjvm_set_static_field(bjvm_cp_field *field, bjvm_stack_value bjvm_stack_value) {
  store_stack_value((void *)field->my_class->static_fields + field->byte_offset, bjvm_stack_value,
                    field_to_kind(&field->parsed_descriptor));
}

bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field) {
  return load_stack_value((void *)obj + field->byte_offset, field_to_kind(&field->parsed_descriptor));
}

// UnsafeConstants contains some important low-level data used by Unsafe:
//   - ADDRESS_SIZE0: The size in bytes of a pointer
//   - PAGE_SIZE: The size in bytes of a memory page
//   - UNALIGNED_ACCESS: Whether unaligned memory access is allowed
static void init_unsafe_constants(bjvm_thread *thread) {
  bjvm_classdesc *UC = bootstrap_lookup_class(thread, STR("jdk/internal/misc/UnsafeConstants"));
  DCHECK(UC && "UnsafeConstants class not found!");

  bjvm_initialize_class_t ctx = {.args = {thread, UC}};
  future_t init = bjvm_initialize_class(&ctx);
  CHECK(init.status == FUTURE_READY);

  bjvm_cp_field *address_size = bjvm_field_lookup(UC, STR("ADDRESS_SIZE0"), STR("I")),
                *page_size = bjvm_field_lookup(UC, STR("PAGE_SIZE"), STR("I")),
                *unaligned_access = bjvm_field_lookup(UC, STR("UNALIGNED_ACCESS"), STR("Z"));

  DCHECK(address_size && page_size && unaligned_access && "UnsafeConstants fields not found!");
  bjvm_set_static_field(address_size, (bjvm_stack_value){.i = sizeof(void *)});
  bjvm_set_static_field(page_size, (bjvm_stack_value){.i = 4096});
  bjvm_set_static_field(unaligned_access, (bjvm_stack_value){.i = 1});
}

bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options) {
  bjvm_thread *thr = calloc(1, sizeof(bjvm_thread));
  *VECTOR_PUSH(vm->active_threads, vm->active_thread_count, vm->active_thread_cap) = thr;

  thr->vm = vm;
  thr->frame_buffer = calloc(1, thr->frame_buffer_capacity = options.stack_space);
  thr->js_jit_enabled = options.js_jit_enabled;
  const int HANDLES_CAPACITY = 200;
  thr->handles = calloc(1, sizeof(bjvm_handle) * HANDLES_CAPACITY);
  thr->handles_capacity = HANDLES_CAPACITY;

  thr->async_stack = calloc(1, 0x20);

  thr->tid = vm->next_tid++;

  bjvm_vm_init_primitive_classes(thr);
  init_unsafe_constants(thr);

  init_cached_classdescs_t init = {.args = {thr}};
  future_t result = init_cached_classdescs(&init);
  CHECK(result.status == FUTURE_READY);

  // Pre-allocate OOM and stack overflow errors
  thr->out_of_mem_error = new_object(thr, vm->cached_classdescs->oom_error);
  thr->stack_overflow_error = new_object(thr, vm->cached_classdescs->stack_overflow_error);

  bjvm_handle *java_thread = bjvm_make_handle(thr, new_object(thr, vm->cached_classdescs->thread));
#define java_thr ((struct bjvm_native_Thread *)java_thread->obj)
  thr->thread_obj = java_thr;

  java_thr->vm_thread = thr;
  java_thr->name = MakeJStringFromCString(thr, "main", true);

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_method_lookup(vm->cached_classdescs->thread, STR("<init>"),
                                                   STR("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"), false, false);

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  call_interpreter_synchronous(
      thr, make_thread,
      (bjvm_stack_value[]){{.obj = (void *)java_thr}, {.obj = main_thread_group}, {.obj = java_thr->name}});

#undef java_thr
  bjvm_drop_handle(thr, java_thread);
  slice const phases[3] = {STR("initPhase1"), STR("initPhase2"), STR("initPhase3")};
  slice const signatures[3] = {STR("()V"), STR("(ZZ)I"), STR("()V")};

  bjvm_cp_method *method;
  bjvm_stack_value ret;
  for (uint_fast8_t i = 0; i < sizeof(phases) / sizeof(*phases);
       ++i) { // todo: these init phases should only be called once per VM
    method = bjvm_method_lookup(vm->cached_classdescs->system, phases[i], signatures[i], false, false);
    DCHECK(method);
    bjvm_stack_value args[2] = {{.i = 1}, {.i = 1}};
    call_interpreter_synchronous(thr, method, args); // void methods, no result

    if (thr->current_exception) {
      // Failed to initialize
      method = bjvm_method_lookup(thr->current_exception->descriptor, STR("getMessage"), STR("()Ljava/lang/String;"),
                                  true, true);
      DCHECK(method);

      bjvm_stack_value args[] = {{.obj = thr->current_exception}};
      bjvm_stack_value obj = call_interpreter_synchronous(thr, method, args);
      heap_string message;
      if (obj.obj) {
        CHECK(read_string_to_utf8(thr, &message, obj.obj) == 0);
      }
      fprintf(stderr, "Error in init phase %.*s: %.*s, %s\n", fmt_slice(thr->current_exception->descriptor->name),
              fmt_slice(phases[i]), obj.obj ? message.chars : "no message");
      abort();
    }

    method = bjvm_method_lookup(vm->cached_classdescs->system, STR("getProperty"),
                                STR("(Ljava/lang/String;)Ljava/lang/String;"), false, false);
    DCHECK(method);
    bjvm_stack_value args2[1] = {{.obj = (void *)MakeJStringFromCString(thr, "java.home", true)}};
    ret = call_interpreter_synchronous(thr, method, args2); // returns a String

    heap_string java_home;
    CHECK(read_string_to_utf8(thr, &java_home, ret.obj) == 0);
    free_heap_str(java_home);
  }

  thr->current_exception = nullptr;

  // Call setJavaLangAccess() since we crash before getting there
  method = bjvm_method_lookup(vm->cached_classdescs->system, STR("setJavaLangAccess"), STR("()V"), false, false);
  call_interpreter_synchronous(thr, method, nullptr); // void methods, no result

  return thr;
}

void bjvm_free_thread(bjvm_thread *thread) {
  // TODO remove from the VM

  free(thread->async_stack);
  free(thread->frames);
  free(thread->frame_buffer);
  free(thread->handles);
  free(thread);
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

bjvm_classdesc *load_class_of_field(bjvm_thread *thread, const bjvm_field_descriptor *field) {
  INIT_STACK_STRING(name, 1000);
  name = bjvm_unparse_field_descriptor(name, field);
  bjvm_classdesc *result = load_class_of_field_descriptor(thread, name);
  return result;
}

struct bjvm_native_MethodType *bjvm_resolve_method_type(bjvm_thread *thread, bjvm_method_descriptor *method) {
  // Resolve each class in the arguments list, as well as the return type if it
  // exists
  DCHECK(method);

  bjvm_classdesc *Class = thread->vm->cached_classdescs->klass;
  bjvm_handle *ptypes = bjvm_make_handle(thread, CreateObjectArray1D(thread, Class, method->args_count));

  for (int i = 0; i < method->args_count; ++i) {
    INIT_STACK_STRING(name, 1000);
    name = bjvm_unparse_field_descriptor(name, method->args + i);
    bjvm_classdesc *arg_desc = load_class_of_field_descriptor(thread, name);

    if (!arg_desc)
      return nullptr;
    *((struct bjvm_native_Class **)ArrayData(ptypes->obj) + i) = bjvm_get_class_mirror(thread, arg_desc);
  }

  INIT_STACK_STRING(return_name, 1000);
  return_name = bjvm_unparse_field_descriptor(return_name, &method->return_type);
  bjvm_classdesc *ret_desc = load_class_of_field_descriptor(thread, return_name);
  if (!ret_desc)
    return nullptr;
  struct bjvm_native_Class *rtype = bjvm_get_class_mirror(thread, ret_desc);
  // Call <init>(Ljava/lang/Class;[Ljava/lang/Class;Z)V
  bjvm_classdesc *MethodType = thread->vm->cached_classdescs->method_type;
  bjvm_cp_method *init = bjvm_method_lookup(MethodType, STR("makeImpl"),
                                            STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                                "lang/invoke/MethodType;"),
                                            false, false);

  bjvm_stack_value result = call_interpreter_synchronous(
      thread, init, (bjvm_stack_value[]){{.obj = (void *)rtype}, {.obj = ptypes->obj}, {.i = 1 /* trusted */}});
  // todo: check for exceptions
  bjvm_drop_handle(thread, ptypes);
  return (void *)result.obj;
}

[[maybe_unused]] static bool mh_handle_supported(bjvm_method_handle_kind kind) {
  switch (kind) {
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
    return true;
  default:
    return false;
  }
}

typedef struct {
  bjvm_classdesc *rtype;
  bjvm_classdesc **ptypes;
  u32 ptypes_count;
  u32 ptypes_capacity;
} mh_type_info_t;

static int compute_mh_type_info(bjvm_thread *thread, bjvm_cp_method_handle_info *info, mh_type_info_t *result) {
  bjvm_classdesc *rtype = nullptr;
  bjvm_classdesc **ptypes = nullptr;
  int ptypes_count = 0;
  int ptypes_capacity = 0;

  switch (info->handle_kind) {
  case BJVM_MH_KIND_GET_FIELD: {
    // MT should be of the form (C)T, where C is the class the field is found on
    bjvm_cp_field_info *field = &info->reference->field;
    *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = field->class_info->classdesc;
    rtype = load_class_of_field(thread, field->parsed_descriptor);
    break;
  }

  case BJVM_MH_KIND_INVOKE_STATIC:
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_SPECIAL:
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_INTERFACE: {
    // MT should be of the form (C,A*)T, where C is the class the method is
    // found on, A* is the list of argument types, and T is the return type
    bjvm_cp_method_info *method = &info->reference->methodref;

    if (info->handle_kind != BJVM_MH_KIND_INVOKE_STATIC) {
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = method->class_info->classdesc;
    }

    for (int i = 0; i < method->descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = load_class_of_field(thread, arg);
    }

    rtype = load_class_of_field(thread, &method->descriptor->return_type);
    break;
  }
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL: {
    // MT should be of the form (A*)T, where A* is the list of argument types,
    bjvm_cp_method_info *method = &info->reference->methodref;

    for (int i = 0; i < method->descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = load_class_of_field(thread, arg);
    }

    rtype = method->class_info->classdesc;
    break;
  }

  default:
    UNREACHABLE();
  }

  *result = (mh_type_info_t){
      .rtype = rtype, .ptypes = ptypes, .ptypes_count = ptypes_count, .ptypes_capacity = ptypes_capacity};
  return 0;
}

DEFINE_ASYNC(resolve_mh_mt) {
  DCHECK(mh_handle_supported(args->info->handle_kind) && "Unsupported method handle kind");

  bjvm_cp_class_info *required_type = args->info->handle_kind == BJVM_MH_KIND_GET_FIELD
                                          ? args->info->reference->field.class_info
                                          : args->info->reference->methodref.class_info;

  bjvm_resolve_class(args->thread, required_type);
  AWAIT(bjvm_initialize_class, args->thread, required_type->classdesc);

  mh_type_info_t info = {};
  int err = compute_mh_type_info(args->thread, args->info, &info);
  CHECK(err == 0);

  // Call MethodType.makeImpl(rtype, ptypes, true)
  bjvm_cp_method *make = bjvm_method_lookup(args->thread->vm->cached_classdescs->method_type, STR("makeImpl"),
                                            STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                                "lang/invoke/MethodType;"),
                                            false, false);

  self->ptypes_array = bjvm_make_handle(
      args->thread, CreateObjectArray1D(args->thread, args->thread->vm->cached_classdescs->klass, info.ptypes_count));
  for (u32 i = 0; i < info.ptypes_count; ++i) {
    *((bjvm_obj_header **)ArrayData(self->ptypes_array->obj) + i) =
        (void *)bjvm_get_class_mirror(args->thread, info.ptypes[i]);
  }
  free(info.ptypes);

  AWAIT(call_interpreter, args->thread, make,
        (bjvm_stack_value[]){{.obj = (void *)bjvm_get_class_mirror(args->thread, info.rtype)},
                             {.obj = self->ptypes_array->obj},
                             {.i = 0}});
  bjvm_stack_value result = get_async_result(call_interpreter);
  bjvm_drop_handle(args->thread, self->ptypes_array);

  ASYNC_END((void *)result.obj);
}

static bjvm_handle *make_member_name(bjvm_thread *thread, bjvm_cp_field_info *field) {
  bjvm_classdesc *MemberName = LoadClassSynchronous(thread, "java/lang/invoke/MemberName");

  bjvm_cp_field *f = bjvm_field_lookup(field->class_info->classdesc, field->nat->name, field->nat->descriptor);
  bjvm_reflect_initialize_field(thread, field->class_info->classdesc, f);
  bjvm_handle *member = bjvm_make_handle(thread, new_object(thread, MemberName));
  bjvm_cp_method *make_member =
      bjvm_method_lookup(MemberName, STR("<init>"), STR("(Ljava/lang/reflect/Field;)V"), false, false);

  call_interpreter_synchronous(
      thread, make_member, (bjvm_stack_value[]){{.obj = (void *)member->obj}, {.obj = (void *)f->reflection_field}});

  return member;
}

DEFINE_ASYNC(resolve_mh_vh) {
#define info (args)->info
#define thread (args->thread)

  DCHECK(mh_is_vh(info->handle_kind));

  bjvm_cp_field_info *field = &info->reference->field;
  bjvm_resolve_class(thread, field->class_info);
  AWAIT(bjvm_initialize_class, thread, field->class_info->classdesc);
  field = &info->reference->field; // reload because of the await

  bjvm_classdesc *DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");

  bjvm_handle *member = make_member_name(thread, field);
  bjvm_cp_method *make = bjvm_method_lookup(DirectMethodHandle, STR("make"),
                                            STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                                "invoke/DirectMethodHandle;"),
                                            false, false);
  bjvm_stack_value result = call_interpreter_synchronous(thread, make, (bjvm_stack_value[]){{.obj = member->obj}});
  bjvm_drop_handle(thread, member);
  ASYNC_END(result.obj);

#undef info
#undef thread
}

DEFINE_ASYNC(resolve_mh_invoke) {
#define info (args)->info
#define thread (args)->thread
#define member (self)->member
#define m (self->m)

  bjvm_cp_method_info *method = &info->reference->methodref;
  bjvm_resolve_class(thread, method->class_info);
  AWAIT(bjvm_initialize_class, thread, info->reference->methodref.class_info->classdesc);
  method = &info->reference->methodref; // reload because of the await

  m = bjvm_method_lookup(method->class_info->classdesc, method->nat->name, method->nat->descriptor, true, true);
  if (BJVM_MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind) {
    bjvm_reflect_initialize_constructor(thread, method->class_info->classdesc, m);
  } else {
    bjvm_reflect_initialize_method(thread, method->class_info->classdesc, m);
  }

  bjvm_classdesc *MemberName = LoadClassSynchronous(thread, "java/lang/invoke/MemberName");

  // Call DirectMethodHandle.make(method, true)
  member = bjvm_make_handle(thread, new_object(thread, MemberName));
  bool is_ctor = BJVM_MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind;
  bjvm_cp_method *make_member = bjvm_method_lookup(
      MemberName, STR("<init>"),
      is_ctor ? STR("(Ljava/lang/reflect/Constructor;)V") : STR("(Ljava/lang/reflect/Method;)V"), false, false);
  AWAIT(call_interpreter, thread, make_member,
        (bjvm_stack_value[]){{.obj = (void *)member->obj},
                             {.obj = is_ctor ? (void *)m->reflection_ctor : (void *)m->reflection_method}});

  bjvm_classdesc *DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");
  bjvm_cp_method *make = bjvm_method_lookup(DirectMethodHandle, STR("make"),
                                            STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                                "invoke/DirectMethodHandle;"),
                                            false, false);
  AWAIT(call_interpreter, thread, make, (bjvm_stack_value[]){{.obj = member->obj}});
  bjvm_stack_value result = get_async_result(call_interpreter);
  bjvm_drop_handle(thread, member);

  // Now if the member is a variable arity constructor/method, call withVarargs(true) and return the resulting
  // MethodHandle.
  if (m->access_flags & BJVM_ACCESS_VARARGS) {
    DirectMethodHandle = LoadClassSynchronous(thread, "java/lang/invoke/DirectMethodHandle");
    bjvm_cp_method *with_varargs = bjvm_method_lookup(DirectMethodHandle, STR("withVarargs"),
                                                      STR("(Z)Ljava/lang/invoke/MethodHandle;"), true, false);
    AWAIT(call_interpreter, thread, with_varargs, (bjvm_stack_value[]){{.obj = (void *)result.obj}, {.i = 1}});
    bjvm_stack_value result2 = get_async_result(call_interpreter);
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

DEFINE_ASYNC(bjvm_resolve_method_handle) {
#define info (args)->info
#define thread (args->thread)

  AWAIT(resolve_mh_mt, thread, info);
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

static void free_ordinary_classdesc(bjvm_classdesc *cd) {
  if (cd->array_type)
    cd->array_type->dtor(cd->array_type);
  bjvm_free_classfile(*cd);
  bjvm_free_function_tables(cd);
  free(cd);
}

void bjvm_class_circularity_error(bjvm_thread *thread, const bjvm_classdesc *class) {
  INIT_STACK_STRING(message, 1000);
  message = bprintf(message, "While loading class %.*s", fmt_slice(class->name));
  bjvm_raise_vm_exception(thread, STR("java/lang/ClassCircularityError"), message);
}

bjvm_module *bjvm_get_unnamed_module(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  bjvm_module *result = bjvm_get_module(vm, STR("<unnamed>"));
  if (result) {
    return result;
  }

  bjvm_obj_header *module = new_object(thread, vm->cached_classdescs->module);
  if (!module) {
    return nullptr;
  }
  bjvm_define_module(vm, STR("<unnamed>"), module);
  return bjvm_get_unnamed_module(thread);
}

bool is_builtin_class(slice chars) {
  return strncmp(chars.chars, "java/", 5) == 0 || strncmp(chars.chars, "javax/", 6) == 0 ||
         strncmp(chars.chars, "jdk/", 4) == 0 || strncmp(chars.chars, "sun/", 4) == 0;
}

bjvm_classdesc *bjvm_define_bootstrap_class(bjvm_thread *thread, slice chars, const u8 *classfile_bytes,
                                            size_t classfile_len) {
  bjvm_vm *vm = thread->vm;
  bjvm_classdesc *class = calloc(1, sizeof(bjvm_classdesc));

  heap_string format_error;
  parse_result_t error = bjvm_parse_classfile(classfile_bytes, classfile_len, class, &format_error);
  if (error != PARSE_SUCCESS) {
    bjvm_raise_vm_exception(thread, STR("java/lang/ClassFormatError"), hslc(format_error));
    free_heap_str(format_error);

    goto error_1;
  }

  // 3. If C has a direct superclass, the symbolic reference from C to its
  // direct superclass is resolved using the algorithm of §5.4.3.1.
  bjvm_cp_class_info *super = class->super_class;
  if (super) {
    // If the superclass is currently being loaded -> circularity  error
    if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars, super->name.len)) {
      bjvm_class_circularity_error(thread, class);
      goto error_2;
    }

    int status = bjvm_resolve_class(thread, class->super_class);
    if (status) {
      // Check whether the current exception is a ClassNotFoundException and
      // if so, raise a NoClassDefFoundError TODO
      goto error_2;
    }
  }

  // 4. If C has any direct superinterfaces, the symbolic references from C to
  // its direct superinterfaces are resolved using the algorithm of §5.4.3.1.
  for (int i = 0; i < class->interfaces_count; ++i) {
    bjvm_cp_class_info *super = class->interfaces[i];
    if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars, super->name.len)) {
      bjvm_class_circularity_error(thread, class);
      goto error_2;
    }

    int status = bjvm_resolve_class(thread, class->interfaces[i]);
    if (status) {
      // Check whether the current exception is a ClassNotFoundException and
      // if so, raise a NoClassDefFoundError TODO
      goto error_2;
    }
  }

  // Look up in the native methods list and add native handles as appropriate
  native_entries *entries = bjvm_hash_table_lookup(&vm->natives, chars.chars, chars.len);
  if (entries) {
    for (int i = 0; i < entries->entries_count; i++) {
      native_entry *entry = entries->entries + i;

      for (int j = 0; j < class->methods_count; ++j) {
        bjvm_cp_method *method = class->methods + j;

        if (utf8_equals_utf8(method->name, entry->name) &&
            utf8_equals_utf8(method->unparsed_descriptor, entry->descriptor)) {
          method->native_handle = &entry->callback;
          break;
        }
      }
    }
  }

  class->kind = BJVM_CD_KIND_ORDINARY;
  class->dtor = free_ordinary_classdesc;
  class->classloader = &bjvm_bootstrap_classloader;

  if (is_builtin_class(chars)) {
    class->module = bjvm_get_module(vm, STR("java.base"));
  } else {
    class->module = bjvm_get_unnamed_module(thread);
  }

  (void)bjvm_hash_table_insert(&vm->classes, chars.chars, chars.len, class);
  return class;

error_2:
  bjvm_free_classfile(*class);
error_1:
  free(class);
  return nullptr;
}

bjvm_classdesc *bootstrap_lookup_class_impl(bjvm_thread *thread, const slice name, bool raise_class_not_found) {
  bjvm_vm *vm = thread->vm;

  int dimensions = 0;
  slice chars = name;
  while (chars.len > 0 && *chars.chars == '[') // munch '[' at beginning
    dimensions++, chars = subslice(chars, 1);

  DCHECK(dimensions < 255);
  DCHECK(chars.len > 0);

  bjvm_classdesc *class;

  if (dimensions && *chars.chars != 'L') {
    // Primitive array type
    bjvm_type_kind kind = *chars.chars;
    class = bjvm_primitive_classdesc(thread, kind);
  } else {
    if (dimensions) {
      // Strip L and ;
      chars = subslice_to(chars, 1, chars.len - 1);
      DCHECK(chars.len >= 1);
    }
    class = bjvm_hash_table_lookup(&vm->classes, chars.chars, chars.len);
  }

  if (!class) {
    // Maybe it's an anonymous class, read the thread stack looking for a
    // class with a matching name
    for (int i = thread->frames_count - 1; i >= 0; --i) {
      bjvm_classdesc *d = bjvm_get_frame_method(thread->frames[i])->my_class;
      if (utf8_equals_utf8(hslc(d->name), chars)) {
        class = d;
        break;
      }
    }
  }

  if (!class) {
    (void)bjvm_hash_table_insert(&vm->inchoate_classes, chars.chars, chars.len, (void *)1);

    // e.g. "java/lang/Object.class"
    const slice cf_ending = STR(".class");
    INIT_STACK_STRING(filename, MAX_CF_NAME_LENGTH + 6);
    memcpy(filename.chars, chars.chars, chars.len);
    memcpy(filename.chars + chars.len, cf_ending.chars, cf_ending.len);
    filename.len = chars.len + cf_ending.len;

    u8 *bytes;
    size_t cf_len;
    int read_status = bjvm_lookup_classpath(&vm->classpath, filename, &bytes, &cf_len);
    if (read_status) {
      if (!raise_class_not_found) {
        return nullptr;
      }

      // If the file is ClassNotFoundException, abort to avoid stack overflow
      if (utf8_equals(chars, "java/lang/ClassNotFoundException")) {
        printf("Could not find class %.*s\n", fmt_slice(chars));
        abort();
      }

      u32 i = 0;
      for (; (i < chars.len) && (filename.chars[i] != '.'); ++i)
        filename.chars[i] = filename.chars[i] == '/' ? '.' : filename.chars[i];
      // ClassNotFoundException: com.google.DontBeEvil
      filename = subslice_to(filename, 0, i);
      bjvm_raise_vm_exception(thread, STR("java/lang/ClassNotFoundException"), filename);
      return nullptr;
    }

    class = bjvm_define_bootstrap_class(thread, chars, bytes, cf_len);
    free(bytes);
    (void)bjvm_hash_table_delete(&vm->inchoate_classes, chars.chars, chars.len);
    if (!class)
      return nullptr;
  }

  // Derive nth dimension
  bjvm_classdesc *result = class;
  for (int i = 1; i <= dimensions; ++i) {
    make_array_classdesc(thread, result);
    result = result->array_type;
  }

  return result;
}

// name = "java/lang/Object" or "[[J" or "[Ljava/lang/String;"
bjvm_classdesc *bootstrap_lookup_class(bjvm_thread *thread, const slice name) {
  return bootstrap_lookup_class_impl(thread, name, true);
}

void bjvm_out_of_memory(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  thread->current_exception = nullptr;
  if (vm->heap_capacity == vm->true_heap_capacity) {
    // We're currently calling fillInStackTrace on the OOM instance, just
    // shut up
    return;
  }

  // temporarily expand the valid heap so that we can allocate the OOM error and
  // its constituents
  size_t original_capacity = vm->heap_capacity;
  vm->heap_capacity = vm->true_heap_capacity;

  bjvm_obj_header *oom = thread->out_of_mem_error;
  // TODO call fillInStackTrace
  bjvm_raise_exception_object(thread, oom);

  vm->heap_capacity = original_capacity;
}

#define GC_EVERY_ALLOCATION 0

void *bump_allocate(bjvm_thread *thread, size_t bytes) {
  // round up to multiple of 8
  bytes = align_up(bytes, 8);
  bjvm_vm *vm = thread->vm;
#if AGGRESSIVE_DEBUG
  printf("Allocating %zu bytes, %zu used, %zu capacity\n", bytes, vm->heap_used, vm->heap_capacity);
#endif
  if (vm->heap_used + bytes > vm->heap_capacity || (GC_EVERY_ALLOCATION && thread->allocations_so_far++ > 69770)) {
    bjvm_major_gc(thread->vm);
    // printf("GC!\n");
    if (vm->heap_used + bytes > vm->heap_capacity) {
      bjvm_out_of_memory(thread);
      return nullptr;
    }
  }
  void *result = vm->heap + vm->heap_used;
  memset(result, 0, bytes);
  vm->heap_used += bytes;
  return result;
}

// Returns true if the class descriptor is a subclass of java.lang.Error.
bool is_error(bjvm_classdesc *d) {
  return utf8_equals(hslc(d->name), "java/lang/Error") || (d->super_class && is_error(d->super_class->classdesc));
}

bjvm_attribute *find_attribute(bjvm_attribute *attrs, int attrc, bjvm_attribute_kind kind) {
  for (int i = 0; i < attrc; ++i)
    if (attrs[i].kind == kind)
      return attrs + i;
  return nullptr;
}

// During initialisation, we need to set the value of static final fields
// if they are provided in the class file.
//
// Returns true if an OOM occurred when initializing string fields.
bool initialize_constant_value_fields(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_cp_field *field = classdesc->fields + i;
    if (field->access_flags & BJVM_ACCESS_STATIC && field->access_flags & BJVM_ACCESS_FINAL) {
      bjvm_attribute *attr =
          find_attribute(field->attributes, field->attributes_count, BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE);
      if (!attr)
        continue;
      void *p = classdesc->static_fields + field->byte_offset;
      bjvm_cp_entry *ent = attr->constant_value;
      DCHECK(ent);

      switch (ent->kind) {
      case BJVM_CP_KIND_INTEGER:
        store_stack_value(p, (bjvm_stack_value){.i = (int)ent->integral.value}, BJVM_TYPE_KIND_INT);
        break;
      case BJVM_CP_KIND_FLOAT:
        store_stack_value(p, (bjvm_stack_value){.f = (float)ent->floating.value}, BJVM_TYPE_KIND_FLOAT);
        break;
      case BJVM_CP_KIND_LONG:
        store_stack_value(p, (bjvm_stack_value){.l = ent->integral.value}, BJVM_TYPE_KIND_LONG);
        break;
      case BJVM_CP_KIND_DOUBLE:
        store_stack_value(p, (bjvm_stack_value){.d = ent->floating.value}, BJVM_TYPE_KIND_DOUBLE);
        break;
      case BJVM_CP_KIND_STRING:
        bjvm_obj_header *str = MakeJStringFromModifiedUTF8(thread, ent->string.chars, true);
        if (!str)
          return true;
        store_stack_value(p, (bjvm_stack_value){.obj = str}, BJVM_TYPE_KIND_REFERENCE);
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
void wrap_in_exception_in_initializer_error(bjvm_thread *thread) {
  bjvm_handle *exc = bjvm_make_handle(thread, thread->current_exception);
  bjvm_classdesc *EIIE = thread->vm->cached_classdescs->exception_in_initializer_error;
  bjvm_handle *eiie = bjvm_make_handle(thread, new_object(thread, EIIE));
  bjvm_cp_method *ctor = bjvm_method_lookup(EIIE, STR("<init>"), STR("(Ljava/lang/Throwable;)V"), false, false);
  thread->current_exception = nullptr; // clear exception
  call_interpreter_synchronous(thread, ctor, (bjvm_stack_value[]){{.obj = eiie->obj}, {.obj = exc->obj}});
  DCHECK(!thread->current_exception);
  bjvm_raise_exception_object(thread, eiie->obj);

  bjvm_drop_handle(thread, eiie);
  bjvm_drop_handle(thread, exc);
}

// Call <clinit> on the class, if it hasn't already been called.
DEFINE_ASYNC(bjvm_initialize_class) {
#define thread (args->thread)

  bjvm_classdesc *classdesc = args->classdesc; // must be reloaded after await()
  bool error;                                  // this is a local, but it's ok because we don't use it between
                                               // awaits

  DCHECK(classdesc);
  if (classdesc->state >= BJVM_CD_STATE_INITIALIZING) {
    // Class is already initialized, or currently being initialized.
    // TODO In a multithreaded model, we would need to wait for the other
    // thread to finish initializing the class.
    ASYNC_RETURN(0);
  }

  if (classdesc->state != BJVM_CD_STATE_LINKED) {
    error = bjvm_link_class(thread, classdesc);
    if (error) {
      DCHECK(thread->current_exception);
      ASYNC_RETURN(0);
    }
  }

  classdesc->state = BJVM_CD_STATE_INITIALIZING;
  self->recursive_call_space = calloc(1, sizeof(bjvm_initialize_class_t));
  if (!self->recursive_call_space) {
    bjvm_out_of_memory(thread);
    ASYNC_RETURN(-1);
  }

  if (classdesc->super_class) {
    AWAIT_INNER(self->recursive_call_space, bjvm_initialize_class, thread, classdesc->super_class->classdesc);
    classdesc = args->classdesc;

    if ((error = self->recursive_call_space->_result))
      goto done;
  }

  for (self->i = 0; self->i < classdesc->interfaces_count; ++self->i) {
    AWAIT_INNER(self->recursive_call_space, bjvm_initialize_class, thread, classdesc->interfaces[self->i]->classdesc);
    classdesc = args->classdesc;

    if ((error = self->recursive_call_space->_result))
      goto done;
  }
  free(self->recursive_call_space);
  self->recursive_call_space = nullptr;

  if ((error = initialize_constant_value_fields(thread, classdesc))) {
    bjvm_out_of_memory(thread);
    goto done;
  }

  bjvm_cp_method *clinit = bjvm_method_lookup(classdesc, STR("<clinit>"), STR("()V"), false, false);
  if (clinit) {
    AWAIT(call_interpreter, thread, clinit, nullptr);
    if (thread->current_exception && !is_error(thread->current_exception->descriptor)) {
      wrap_in_exception_in_initializer_error(thread);
      goto done;
    }
  }

  error = 0;
done:
  free(self->recursive_call_space);
  args->classdesc->state = error ? BJVM_CD_STATE_LINKAGE_ERROR : BJVM_CD_STATE_INITIALIZED;
  ASYNC_END(error);

#undef thread
}

bool method_candidate_matches(const bjvm_cp_method *candidate, const slice name, const slice method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor.chars ||
          utf8_equals_utf8(candidate->unparsed_descriptor, method_descriptor));
}

// TODO look at this more carefully
bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor, const slice name, const slice method_descriptor,
                                   bool search_superclasses, bool search_superinterfaces) {
  DCHECK(descriptor->state >= BJVM_CD_STATE_LINKED);
  bjvm_classdesc *search = descriptor;
  // if the object is an array and we're looking for a superclass method, the
  // method must be on a superclass
  if (search->kind != BJVM_CD_KIND_ORDINARY && search_superclasses)
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
    bjvm_cp_method *result =
        bjvm_method_lookup(descriptor->interfaces[i]->classdesc, name, method_descriptor, false, true);
    if (result)
      return result;
  }

  // Look in superinterfaces of superclasses
  if (search_superclasses && descriptor->super_class) {
    return bjvm_method_lookup(descriptor->super_class->classdesc, name, method_descriptor, true, true);
  }

  return nullptr;
}

// Returns true on failure to initialize the context
static bool initialize_async_ctx(bjvm_async_run_ctx *ctx, bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args) {
  assert(method && "Method is null");
  memset(ctx, 0, sizeof(*ctx));

  u8 argc = bjvm_argc(method);
  bjvm_stack_value *stack_top = (bjvm_stack_value *)(thread->frame_buffer + thread->frame_buffer_used);
  size_t args_size = sizeof(bjvm_stack_value) * argc;

  if (args_size + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return true;
  }

  memcpy(stack_top, args, args_size);
  thread->frame_buffer_used += args_size;

  ctx->thread = thread;
  ctx->frame = bjvm_push_frame(thread, method, stack_top, argc);
  return !ctx->frame;  // true on failure to allocate
}

DEFINE_ASYNC(call_interpreter) {
  if (initialize_async_ctx(&self->ctx, args->thread, args->method, args->args))
    ASYNC_RETURN((bjvm_stack_value){.l = 0});

  self->ctx.interpreter_state.args = (struct bjvm_interpret_args){ args->thread, self->ctx.frame};
  AWAIT_FUTURE_EXPR(bjvm_interpret(&self->ctx.interpreter_state));

  bjvm_stack_value result = self->ctx.interpreter_state._result;
  ASYNC_END(result);
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info) {
  // TODO use current class loader
  // TODO synchronize on some object, probably the class which this info is a
  // part of

  if (info->classdesc)
    return 0; // already succeeded
  if (info->vm_object) {
    bjvm_raise_exception_object(thread,
                                info->vm_object); // already failed
    return -1;
  }
  info->classdesc = bootstrap_lookup_class(thread, info->name);
  if (!info->classdesc) {
    info->vm_object = thread->current_exception;
    return -1;
  }

  // TODO check that the class is accessible

  return 0;
}

int bjvm_resolve_field(bjvm_thread *thread, bjvm_cp_field_info *info) {
  if (info->field)
    return 0;
  bjvm_cp_class_info *class = info->class_info;
  int error = bjvm_resolve_class(thread, class);
  if (error)
    return error;
  error = bjvm_link_class(thread, class->classdesc);
  if (error)
    return error;

  // Get offset of field
  DCHECK(class->classdesc->state >= BJVM_CD_STATE_LINKED);
  bjvm_cp_field *field = bjvm_field_lookup(class->classdesc, info->nat->name, info->nat->descriptor);
  info->field = field;
  return field == nullptr;
}

void store_stack_value(void *field_location, bjvm_stack_value value, bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    DCHECK((value.i == 0) || (value.i == 1));
    [[fallthrough]];
  case BJVM_TYPE_KIND_BYTE:
    *(jbyte *)field_location = (jbyte)value.i;
    break;
  case BJVM_TYPE_KIND_CHAR:
    [[fallthrough]];
  case BJVM_TYPE_KIND_SHORT:
    *(jshort *)field_location = (s16)value.i;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    *(jfloat *)field_location = value.f;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    *(jdouble *)field_location = value.d;
    break;
  case BJVM_TYPE_KIND_INT:
    *(jint *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_LONG:
    *(jlong *)field_location = value.l;
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    *(object *)field_location = value.obj;
    break;
  case BJVM_TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
}

bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind) {
  bjvm_stack_value result;
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE: // sign-extend the byte
    result.i = (int)*(s8 *)field_location;
    break;
  case BJVM_TYPE_KIND_CHAR:
    result.i = *(u16 *)field_location;
    break;
  case BJVM_TYPE_KIND_SHORT:
    result.i = *(s16 *)field_location;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    result.f = *(float *)field_location;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    result.d = *(double *)field_location;
    break;
  case BJVM_TYPE_KIND_INT:
    result.i = *(int *)field_location;
    break;
  case BJVM_TYPE_KIND_LONG:
    result.l = *(s64 *)field_location;
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    result.obj = *(void **)field_location;
    break;
  case BJVM_TYPE_KIND_VOID:
  default:
    UNREACHABLE();
  }
  return result;
}

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  return AllocateObject(thread, classdesc, classdesc->instance_bytes);
}

bool bjvm_is_instanceof_name(const bjvm_obj_header *mirror, const slice name) {
  return mirror && utf8_equals_utf8(hslc(mirror->descriptor->name), name);
}

bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror) {
  DCHECK(bjvm_is_instanceof_name(mirror, STR("java/lang/Class")));
  return ((struct bjvm_native_Class *)mirror)->reflected_class;
}

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror) {
  DCHECK(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Field")));
  // Fields get copied around, but all reference the "root" created by the VM
  bjvm_obj_header *root = ((struct bjvm_native_Field *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Field *)mirror)->reflected_field;
}

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror) {
  DCHECK(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Constructor")));
  // Constructors get copied around, but all reference the "root" created by the
  // VM
  bjvm_obj_header *root = ((struct bjvm_native_Constructor *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Constructor *)mirror)->reflected_ctor;
}

bjvm_cp_method **bjvm_unmirror_method(bjvm_obj_header *mirror) {
  DCHECK(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Method")));
  // Methods get copied around, but all reference the "root" created by the VM
  bjvm_obj_header *root = ((struct bjvm_native_Method *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Method *)mirror)->reflected_method;
}

struct bjvm_native_ConstantPool *bjvm_get_constant_pool_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (!classdesc)
    return nullptr;
  if (classdesc->cp_mirror)
    return classdesc->cp_mirror;
  bjvm_classdesc *java_lang_ConstantPool = thread->vm->cached_classdescs->constant_pool;
  struct bjvm_native_ConstantPool *cp_mirror = classdesc->cp_mirror =
      (void *)new_object(thread, java_lang_ConstantPool);
  cp_mirror->reflected_class = classdesc;
  return cp_mirror;
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (!classdesc)
    return nullptr;
  if (classdesc->mirror)
    return classdesc->mirror;

  bjvm_classdesc *java_lang_Class = bootstrap_lookup_class(thread, STR("java/lang/Class"));
  bjvm_initialize_class_t init = {.args = {thread, java_lang_Class}};
  future_t klass_init_state = bjvm_initialize_class(&init);
  CHECK(klass_init_state.status == FUTURE_READY);
  if (init._result) {
    // TODO raise exception
    UNREACHABLE();
  }

  classdesc->mirror = (void *)new_object(thread, java_lang_Class);
  bjvm_handle *cm_handle = bjvm_make_handle(thread, (void *)classdesc->mirror);

#define class_mirror ((struct bjvm_native_Class *)cm_handle->obj)

  if (class_mirror) {
    class_mirror->reflected_class = classdesc;
    if (classdesc->module)
      class_mirror->module = classdesc->module->reflection_object;
    class_mirror->componentType =
        classdesc->one_fewer_dim ? (void *)bjvm_get_class_mirror(thread, classdesc->one_fewer_dim) : nullptr;
  }
  struct bjvm_native_Class *result = class_mirror;
  bjvm_drop_handle(thread, cm_handle);
  return result;
#undef class_mirror
}

bool bjvm_instanceof_interface(const bjvm_classdesc *o, const bjvm_classdesc *target) {
  if (o == target)
    return true;
  for (int i = 0; i < arrlen(o->itables.interfaces); ++i)
    if (o->itables.interfaces[i] == target)
      return true;
  return false;
}

bool bjvm_instanceof_super(const bjvm_classdesc *o, const bjvm_classdesc *target) {
  assert(target->hierarchy_len > 0 && "Invalid hierarchy length");
  assert(target->state >= BJVM_CD_STATE_LINKED && "Target class not linked");
  assert(o->state >= BJVM_CD_STATE_LINKED && "Source class not linked");

  return target->hierarchy_len <= o->hierarchy_len  // target is at or higher than o in its chain
    && o->hierarchy[target->hierarchy_len - 1] == target; // it is a superclass!
}

// Returns true if o is an instance of target
bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target) {
  assert(o->kind != BJVM_CD_KIND_PRIMITIVE && "bjvm_instanceof not intended for primitives");
  assert(target->kind != BJVM_CD_KIND_PRIMITIVE && "bjvm_instanceof not intended for primitives");

  // TODO compare class loaders too
  if (o == target)
    return true;

  if (unlikely(target->kind != BJVM_CD_KIND_ORDINARY)) { // target is an array
    if (o->kind == BJVM_CD_KIND_ORDINARY)
      return false;
    if (o->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
      return target->dimensions == o->dimensions &&
        o->primitive_component == target->primitive_component &&
             (!o->base_component || !target->base_component ||
              bjvm_instanceof(o->base_component, target->base_component));
    }
    // o is 1D primitive array, equality check suffices
    return target->dimensions == o->dimensions && target->primitive_component == o->primitive_component;
  }

  // o is normal object
  const bjvm_classdesc *desc = o;
  return target->access_flags & BJVM_ACCESS_INTERFACE ?
    bjvm_instanceof_interface(desc, target) :
    bjvm_instanceof_super(desc, target);
}

bool method_types_compatible(struct bjvm_native_MethodType *provider_mt, struct bjvm_native_MethodType *targ) {
  // Compare ptypes
  if (provider_mt == targ)
    return true;
  if (*ArrayLength(provider_mt->ptypes) != *ArrayLength(targ->ptypes)) {
    return false;
  }
  for (int i = 0; i < *ArrayLength(provider_mt->ptypes); ++i) {
    bjvm_classdesc *left = bjvm_unmirror_class(((bjvm_obj_header **)ArrayData(provider_mt->ptypes))[i]);
    bjvm_classdesc *right = bjvm_unmirror_class(((bjvm_obj_header **)ArrayData(targ->ptypes))[i]);

    if (left != right) {
      return false;
    }
  }
  return true;
}

// Dump the contents of the method type to the specified stream.
[[maybe_unused]] static void dump_method_type(FILE *stream, struct bjvm_native_MethodType *type) {
  fprintf(stream, "(");
  for (int i = 0; i < *ArrayLength(type->ptypes); ++i) {
    bjvm_classdesc *desc = bjvm_unmirror_class(((bjvm_obj_header **)ArrayData(type->ptypes))[i]);
    fprintf(stream, "%.*s", fmt_slice(desc->name));
    if (i + 1 < *ArrayLength(type->ptypes))
      fprintf(stream, ", ");
  }
  fprintf(stream, ") -> %.*s\n", fmt_slice(bjvm_unmirror_class(type->rtype)->name));
}

[[maybe_unused]] static heap_string debug_dump_string(bjvm_thread *thread, bjvm_obj_header *header) {
  bjvm_cp_method *toString =
      bjvm_method_lookup(header->descriptor, STR("toString"), STR("()Ljava/lang/String;"), true, true);
  bjvm_stack_value result = call_interpreter_synchronous(thread, toString, (bjvm_stack_value[]){{.obj = header}});

  heap_string str;
  if (read_string_to_utf8(thread, &str, result.obj)) {
    UNREACHABLE();
  }
  return str;
}

void bjvm_wrong_method_type_error([[maybe_unused]] bjvm_thread *thread,
                                  [[maybe_unused]] struct bjvm_native_MethodType *provider_mt,
                                  [[maybe_unused]] struct bjvm_native_MethodType *targ) {
  UNREACHABLE(); // TODO
}

DEFINE_ASYNC(bjvm_invokevirtual_signature_polymorphic) {
#define target (args->target)
#define provider_mt (args->provider_mt)
#define thread (args->thread)

  struct bjvm_native_MethodHandle *mh = (void *)target;
  struct bjvm_native_MethodType *targ = (void *)mh->type;

  bool mts_are_same = method_types_compatible(provider_mt, targ);
  bool is_invoke_exact = utf8_equals_utf8(args->method->name, STR("invokeExact"));
  // only raw calls to MethodHandle.invoke involve "asType" conversions
  bool is_invoke = utf8_equals_utf8(args->method->name, STR("invoke")) &&
                   utf8_equals(hslc(args->method->my_class->name), "java/lang/invoke/MethodHandle");

  if (is_invoke_exact) {
    if (!mts_are_same) {
      bjvm_wrong_method_type_error(thread, provider_mt, targ);
      ASYNC_RETURN_VOID();
    }
  }

  if (!mts_are_same && is_invoke) {
    // Call asType to get an adapter handle
    bjvm_cp_method *asType =
        bjvm_method_lookup(mh->base.descriptor, STR("asType"),
                           STR("(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;"), true, false);
    if (!asType)
      UNREACHABLE();

    AWAIT(call_interpreter, thread, asType, (bjvm_stack_value[]){{.obj = (void *)mh}, {.obj = (void *)provider_mt}});
    bjvm_stack_value result = get_async_result(call_interpreter);
    if (thread->current_exception != 0) // asType failed
      ASYNC_RETURN_VOID();
    mh = (void *)result.obj;
  }

  struct bjvm_native_LambdaForm *form = (void *)mh->form;
  struct bjvm_native_MemberName *name = (void *)form->vmentry;

  bjvm_method_handle_kind kind = (name->flags >> 24) & 0xf;
  if (kind == BJVM_MH_KIND_INVOKE_STATIC) {
    self->method = name->vmtarget;
    DCHECK(self->method);

    args->sp_->obj = (void *)mh;
    u8 argc = self->argc = self->method->descriptor->args_count;
    self->frame = bjvm_push_frame(thread, self->method, args->sp_, argc);

    // TODO arena allocate this in the VM so that it gets freed appropriately
    // if the VM is deleted
    self->interpreter_ctx = calloc(1, sizeof(bjvm_interpret_t));
    AWAIT_INNER(self->interpreter_ctx, bjvm_interpret, thread, self->frame);
    if (self->method->descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID) {
      // Store the result in the frame
      *args->sp_ = self->interpreter_ctx->_result;
    }
    free(self->interpreter_ctx);
  } else {
    UNREACHABLE();
  }

  ASYNC_END_VOID();

#undef target
#undef provider_mt
#undef thread
}

DEFINE_ASYNC(resolve_methodref) {
#define info args->info
#define thread args->thread

  if (info->resolved) {
    ASYNC_RETURN(0);
  }
  self->klass = info->class_info;
  int status = bjvm_resolve_class(thread, self->klass);
  if (status) {
    // Failed to resolve the class in question
    ASYNC_RETURN(status);
  }

  AWAIT(bjvm_initialize_class, thread, self->klass->classdesc);
  if (thread->current_exception) {
    ASYNC_RETURN(1);
  }

  info->resolved = bjvm_method_lookup(self->klass->classdesc, info->nat->name, info->nat->descriptor, true, true);
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

int bjvm_multianewarray(bjvm_thread *thread, bjvm_plain_frame *frame, struct bjvm_multianewarray_data *multianewarray,
                        u16 *sd) {
  int dims = multianewarray->dimensions;
  DCHECK(*sd >= dims);
  // DCHECK(stack_depth(frame) >= dims);
  DCHECK(dims >= 1);

  int error = bjvm_resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  bjvm_link_class(thread, multianewarray->entry->classdesc);

  int dim_sizes[kArrayMaxDimensions];
  for (int i = 0; i < dims; ++i) {
    int dim = frame->stack[*sd - dims + i].i;
    if (dim < 0) {
      raise_negative_array_size_exception(thread, dim);
      return -1;
    }

    dim_sizes[i] = dim;
  }

  bjvm_obj_header *result = CreateArray(thread, multianewarray->entry->classdesc, dim_sizes, dims);
  frame->stack[*sd - dims] = (bjvm_stack_value){.obj = result};
  *sd -= dims - 1;
  return 0;
}

static bjvm_stack_value box_cp_integral(bjvm_cp_kind kind, bjvm_cp_entry *ent) {
  switch (kind) {
  case BJVM_CP_KIND_INTEGER:
    UNREACHABLE("BOX THIS");
    return (bjvm_stack_value){.i = (jint)ent->integral.value};
  case BJVM_CP_KIND_FLOAT:
    UNREACHABLE("BOX THIS");
    return (bjvm_stack_value){.f = (jfloat)ent->floating.value};
  case BJVM_CP_KIND_LONG:
    UNREACHABLE("BOX THIS");
    return (bjvm_stack_value){.l = ent->integral.value};
  case BJVM_CP_KIND_DOUBLE:
    UNREACHABLE("BOX THIS");
    return (bjvm_stack_value){.d = ent->floating.value};

  default:
    UNREACHABLE();
  }
}

DEFINE_ASYNC(bjvm_resolve_indy_static_argument) {
#define thread args->thread
#define ent args->ent

  if (cp_kind_is_primitive(ent->kind)) {
    ASYNC_RETURN(box_cp_integral(ent->kind, ent));
  }

  if (ent->kind == BJVM_CP_KIND_CLASS) {
    bjvm_resolve_class(thread, &ent->class_info);
    ASYNC_RETURN((bjvm_stack_value){.obj = (void *)bjvm_get_class_mirror(thread, ent->class_info.classdesc)});
  }

  if (ent->kind == BJVM_CP_KIND_STRING) {
    bjvm_obj_header *string = MakeJStringFromModifiedUTF8(thread, ent->string.chars, true);
    ASYNC_RETURN((bjvm_stack_value){.obj = string});
  }

  if (ent->kind == BJVM_CP_KIND_METHOD_TYPE) {
    if (!ent->method_type.resolved_mt) {
      ent->method_type.resolved_mt = bjvm_resolve_method_type(thread, ent->method_type.parsed_descriptor);
    }
    ASYNC_RETURN((bjvm_stack_value){.obj = (void *)ent->method_type.resolved_mt});
  }

  if (ent->kind == BJVM_CP_KIND_METHOD_HANDLE) {
    AWAIT(bjvm_resolve_method_handle, thread, &ent->method_handle);
    ASYNC_RETURN((bjvm_stack_value){.obj = (void *)get_async_result(bjvm_resolve_method_handle)});
  }

  UNREACHABLE();
  ASYNC_END_VOID();

#undef is_object
#undef thread
#undef ent
}

_Thread_local bjvm_value handles[256];
_Thread_local bool is_handle[256];

DEFINE_ASYNC(indy_resolve) {
#define thread args->thread
#define indy args->indy
#define m (indy->method)

  // e.g. LambdaMetafactory.metafactory
  AWAIT(bjvm_resolve_method_handle, thread, indy->method->ref);
  if (thread->current_exception) {
    ASYNC_RETURN(1);
  }
  self->bootstrap_handle = bjvm_make_handle(thread, (void *)get_async_result(bjvm_resolve_method_handle));

  // MethodHandles class
  bjvm_classdesc *lookup_class = thread->vm->cached_classdescs->method_handles;
  bjvm_cp_method *lookup_factory =
      bjvm_method_lookup(lookup_class, STR("lookup"), STR("()Ljava/lang/invoke/MethodHandles$Lookup;"), true, false);

  bjvm_handle *lookup_handle =
      bjvm_make_handle(thread, call_interpreter_synchronous(thread, lookup_factory, nullptr).obj);

  self->invoke_array =
      bjvm_make_handle(thread, CreateObjectArray1D(thread, thread->vm->cached_classdescs->object, m->args_count + 3));

  self->static_i = 0;
  for (; self->static_i < m->args_count; ++self->static_i) {
    bjvm_cp_entry *arg = m->args[self->static_i];
    AWAIT(bjvm_resolve_indy_static_argument, thread, arg);
    ReferenceArrayStore(self->invoke_array->obj, self->static_i + 3,
                        get_async_result(bjvm_resolve_indy_static_argument).obj);
  }

  bjvm_handle *name = bjvm_make_handle(thread, MakeJStringFromModifiedUTF8(thread, indy->name_and_type->name, true));
  indy->resolved_mt = bjvm_resolve_method_type(thread, indy->method_descriptor);

  ReferenceArrayStore(self->invoke_array->obj, 0, lookup_handle->obj);
  bjvm_drop_handle(thread, lookup_handle);
  ReferenceArrayStore(self->invoke_array->obj, 1, name->obj);
  bjvm_drop_handle(thread, name);
  ReferenceArrayStore(self->invoke_array->obj, 2, (void *)indy->resolved_mt);

  // Invoke the bootstrap method using invokeWithArguments
  bjvm_cp_method *invokeWithArguments =
      bjvm_method_lookup(self->bootstrap_handle->obj->descriptor, STR("invokeWithArguments"),
                         STR("([Ljava/lang/Object;)Ljava/lang/Object;"), true, false);

  object bh = self->bootstrap_handle->obj, invoke_array = self->invoke_array->obj;
  bjvm_drop_handle(thread, self->bootstrap_handle);
  bjvm_drop_handle(thread, self->invoke_array);

  AWAIT(call_interpreter, thread, invokeWithArguments,
                       (bjvm_stack_value[]){{.obj = bh}, {.obj = invoke_array}});
  bjvm_stack_value res = get_async_result(call_interpreter);

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

DEFINE_ASYNC(bjvm_run_native) {
#define frame args->frame
#define thread args->thread

  DCHECK(frame && "frame is null");

  bjvm_native_callback *handle = frame->method->native_handle;
  bool is_static = frame->method->access_flags & BJVM_ACCESS_STATIC;
  bjvm_handle *target_handle = is_static ? nullptr : bjvm_get_native_args(frame)[0].handle;
  bjvm_value *native_args = bjvm_get_native_args(frame) + (is_static ? 0 : 1);
  u16 argc = frame->num_locals - !is_static;

  if (!handle->async_ctx_bytes) {
    bjvm_stack_value result = handle->sync(thread, target_handle, native_args, argc);
    ASYNC_RETURN(result);
  }

  self->native_struct = malloc(handle->async_ctx_bytes);
  *self->native_struct = (async_natives_args){{thread, target_handle, native_args, argc}, 0};
  AWAIT_FUTURE_EXPR(((bjvm_native_callback *)frame->method->native_handle)->async(self->native_struct));
  // We've laid out the context struct so that the result is always at offset 0
  bjvm_stack_value result = ((async_natives_args *)self->native_struct)->result;
  free(self->native_struct);

  ASYNC_END(result);

#undef thread
#undef frame
}

// Main interpreter
DEFINE_ASYNC(bjvm_interpret) {
#define thread args->thread
#define raw_frame args->raw_frame
  DCHECK(*(thread->frames + thread->frames_count - 1) == raw_frame && "Frame is not last frame on stack");

  for (;;) {
    future_t f;
    bjvm_stack_value the_result = bjvm_interpret_2(&f, thread, raw_frame);
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

int bjvm_get_line_number(const bjvm_attribute_code *code, u16 pc) {
  DCHECK(code && "code is null");
  bjvm_attribute_line_number_table *table = code->line_number_table;
  if (!table || pc >= code->insn_count)
    return -1;
  // Look up original PC (the instruction is tagged with it)
  int original_pc = code->code[pc].original_pc;
  int low = 0, high = table->entry_count - 1;
  while (low <= high) { // binary search for first entry with start_pc <= pc
    int mid = (low + high) / 2;
    bjvm_line_number_table_entry entry = table->entries[mid];
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

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (!vm->main_thread_group) {
    bjvm_classdesc *ThreadGroup = vm->cached_classdescs->thread_group;
    bjvm_cp_method *init = bjvm_method_lookup(ThreadGroup, STR("<init>"), STR("()V"), false, false);

    DCHECK(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    call_interpreter_synchronous(thread, init, args); // ThreadGroup constructor doesn't do much
  }
  return vm->main_thread_group;
}

#ifdef EMSCRIPTEN
EMSCRIPTEN_KEEPALIVE
__attribute__((constructor)) static void nodejs_bootloader() {
  MAIN_THREAD_EM_ASM_INT({
    if (ENVIRONMENT_IS_NODE) {
      if (!FS.initialized)
        FS.init();
      const fs = require('fs');
      const needed = ([ 'jdk23.jar', 'jdk23/lib/modules' ]);

      // Read each of these from disk and put them in the filesystem
      for (let i = 0; i < needed.length; ++i) {
        const path = needed[i];
        const data = fs.readFileSync("./" + path); // Buffer
        FS.createPath('/', path.substring(0, path.lastIndexOf('/')));
        FS.writeFile(path, data);
      }
    }
  });
}
#endif