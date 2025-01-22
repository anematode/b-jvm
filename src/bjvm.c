#define AGGRESSIVE_DEBUG 0

// If set, the interpreter will use a goto per instruction to jump to the next
// instruction. This messes up the debug dumps but can lead to slightly better
// performance because the branch predictor has more information about pairs
// of instructions which tend to follow another.
#define ONE_GOTO_PER_INSN 0
// Skip the memset(...) call to clear each frame's locals/stack. This messes
// up the debug dumps, but makes setting up frames faster.
#define SKIP_CLEARING_FRAME 0

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
#include "monitor.h"
#include "natives.h"
#include "objects.h"
#include "strings.h"
#include "util.h"
#include "wasm_jit.h"

#include "cached_classdescs.h"

DECLARE_ASYNC(int, init_cached_classdescs,
  locals(
    bjvm_initialize_class_t ic;
    bjvm_classdesc * *cached_classdescs;
  ),
  arguments(bjvm_thread *thread),
  invoked_methods(
    invoked_method(bjvm_initialize_class)
  )
);

DEFINE_ASYNC(init_cached_classdescs) {
  assert(!args->thread->vm->cached_classdescs);

  self->cached_classdescs = malloc(cached_classdesc_count * sizeof(bjvm_classdesc *));
  args->thread->vm->cached_classdescs = (struct bjvm_cached_classdescs *)self->cached_classdescs;

  if (!self->cached_classdescs) {
    args->thread->current_exception = args->thread->out_of_mem_error;
    ASYNC_RETURN(-1);
  }

  for (int i = 0; i < cached_classdesc_count; i++) {
    char const *name = cached_classdesc_paths[i];
    self->cached_classdescs[i] = bootstrap_lookup_class(args->thread, str_to_utf8(name));

    AWAIT(bjvm_initialize_class, args->thread, self->cached_classdescs[i]);

    if (self->ic._result != 0) {
      free(self->cached_classdescs);
      ASYNC_RETURN(self->ic._result);
    }
  }
  ASYNC_END(0);
}

#define MAX_CF_NAME_LENGTH 1000

uint16_t stack_depth(const bjvm_plain_frame *frame) {
  assert(frame->method && "Can't get stack depth of fake frame");
  int pc = frame->program_counter;
  if (pc == 0) return 0;
  bjvm_code_analysis *analy = frame->method->code_analysis;
  assert(pc < analy->insn_count);
  return analy->insn_index_to_stack_depth[pc];
}

bool bjvm_is_frame_native(const bjvm_stack_frame *frame) { return !!frame->plain.is_native; }

bjvm_native_frame *bjvm_get_native_frame(bjvm_stack_frame *frame) {
  assert(bjvm_is_frame_native(frame));
  return &frame->native;
}

bjvm_plain_frame *bjvm_get_plain_frame(bjvm_stack_frame *frame) {
  assert(!bjvm_is_frame_native(frame));
  return &frame->plain;
}

bjvm_cp_method *bjvm_get_frame_method(bjvm_stack_frame *frame) {
  return bjvm_is_frame_native(frame) ? frame->native.method : frame->plain.method;
}

bjvm_obj_header *bjvm_deref_js_handle(bjvm_vm *vm, int index) {
  if (index < 0 || index >= arrlen(vm->js_handles)) {
    return nullptr;
  }
  return vm->js_handles[index];
}

int bjvm_make_js_handle(bjvm_vm *vm, bjvm_obj_header *obj) {
  assert(obj);
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

bjvm_handle *bjvm_make_handle(bjvm_thread *thread, bjvm_obj_header *obj) {
  if (!obj)
    return &thread->null_handle;
  for (int i = 0; i < thread->handles_capacity; ++i) {
    if (!thread->handles[i].obj) {
      thread->handles[i].obj = obj;
      return thread->handles + i;
    }
  }
  UNREACHABLE(); // When we need more handles, rewrite to use a LL impl
}

void bjvm_drop_handle(bjvm_thread *thread, bjvm_handle *handle) {
  if (!handle || handle == &thread->null_handle)
    return;
  assert(handle >= thread->handles && handle < thread->handles + thread->handles_capacity);
  handle->obj = nullptr;
}

void bjvm_unsatisfied_link_error(bjvm_thread *thread, const bjvm_cp_method *method);
void bjvm_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method *method);

// For each argument, if it's a reference, wrap it in a handle; otherwise
// just memcpy it over since the representations of primitives are the same
// between bjvm_stack_value and bjvm_value
static void make_handles_array(bjvm_thread *thread, const bjvm_method_descriptor *descriptor, bool is_static,
                               bjvm_stack_value *stack_args, bjvm_value *args) {
  int argc = descriptor->args_count;
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
                                         const bjvm_method_descriptor *descriptor, bjvm_stack_value *args, int argc) {
  bjvm_native_callback *native = method->native_handle;
  if (!native) {
    bjvm_unsatisfied_link_error(thread, method);
    return nullptr;
  }

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  size_t args_bytes = argc * sizeof(bjvm_value);
  size_t total = header_bytes + args_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  bjvm_stack_frame *frame = (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  assert((uintptr_t)frame % 8 == 0 && "Frame is aligned");
  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;

  frame->native.is_native = 1;
  frame->native.values_count = argc;
  frame->native.method = method;
  frame->native.method_shape = descriptor;
  frame->native.state = 0;

  // Now wrap arguments in handles and copy them into the frame
  make_handles_array(thread, descriptor, method->access_flags & BJVM_ACCESS_STATIC, args, frame->native.values);
  return frame;
}

bjvm_stack_frame *bjvm_push_plain_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  const bjvm_attribute_code *code = method->code;
  if (!code) {
    bjvm_abstract_method_error(thread, method);
    return nullptr;
  }

  assert(argc <= code->max_locals);

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  size_t values_bytes = ((int)code->max_locals + code->max_stack) * sizeof(bjvm_stack_value);
  size_t total = header_bytes + values_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  bjvm_stack_frame *frame = (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);

#if !SKIP_CLEARING_FRAME
  memset(frame, 0, total);
#endif

  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) = frame;
  frame->plain.is_native = 0;
  frame->plain.values_count = code->max_stack + code->max_locals;
  frame->plain.program_counter = 0;
  frame->plain.max_stack = code->max_stack;
  frame->plain.method = method;

  // Copy in the arguments
  if (likely(argc)) {
    memcpy(frame->plain.values + code->max_stack, args, argc * sizeof(bjvm_stack_value));
  }

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
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, int argc) {
  assert(method != nullptr && "Method is null");
  bool argc_ok = argc == method->descriptor->args_count + !(method->access_flags & BJVM_ACCESS_STATIC);
  assert(argc_ok && "Wrong argc");
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

void dump_frame(FILE *stream, const bjvm_plain_frame *frame) {
  char buf[5000] = {0}, *write = buf, *end = buf + sizeof(buf);
  int sd = stack_depth(frame);

  for (int i = 0; i < sd; ++i) {
    bjvm_stack_value value = frame->values[i];
    const char *is_ref = infer_type(frame->method->code_analysis, frame->program_counter, i);
    write += snprintf(write, end - write, " stack[%d] = [ ref = %p, int = %d ] %s\n", i, value.obj, value.i, is_ref);
  }

  for (int i = frame->max_stack; i < frame->values_count; ++i) {
    bjvm_stack_value value = frame->values[i];
    const char *is_ref = infer_type(frame->method->code_analysis, frame->program_counter, i);
    write += snprintf(write, end - write, "locals[%d] = [ ref = %p, int = %d ] %s\n", i - frame->max_stack, value.obj,
                      value.i, is_ref);
  }

  fprintf(stream, "%s", buf);
}

void bjvm_pop_frame(bjvm_thread *thr, [[maybe_unused]] const bjvm_stack_frame *reference) {
  assert(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  assert(reference == nullptr || reference == frame);
  if (bjvm_is_frame_native(frame)) {
    drop_handles_array(thr, frame->native.method, frame->native.method_shape, frame->native.values);
  }
  thr->frames_count--;
  thr->frame_buffer_used = thr->frames_count == 0 ? 0 : (char *)frame - thr->frame_buffer;
}

// Symmetry with make_primitive_classdesc
static void free_primitive_classdesc(bjvm_classdesc *classdesc) {
  assert(classdesc->kind == BJVM_CD_KIND_PRIMITIVE);
  if (classdesc->array_type)
    classdesc->array_type->dtor(classdesc->array_type);
  free_heap_str(classdesc->name);
  free(classdesc);
}

typedef struct {
  bjvm_utf8 name;
  bjvm_utf8 descriptor;

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

size_t bjvm_native_count = 0;
size_t bjvm_native_capacity = 0;
bjvm_native_t *bjvm_natives = nullptr;

size_t bjvm_get_natives_list(bjvm_native_t const *natives[]) {
  *natives = bjvm_natives;
  return bjvm_native_count;
}

void bjvm_register_native(bjvm_vm *vm, const bjvm_utf8 class, const bjvm_utf8 method_name,
                          const bjvm_utf8 method_descriptor, bjvm_native_callback callback) {
  native_entries *existing = bjvm_hash_table_lookup(&vm->natives, class.chars, class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)bjvm_hash_table_insert(&vm->natives, class.chars, class.len, existing);
  }

  native_entry *ent = VECTOR_PUSH(existing->entries, existing->entries_count, existing->entries_cap);
  ent->name = method_name;
  ent->descriptor = method_descriptor;
  ent->callback = callback;
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor, bjvm_utf8 name, bjvm_utf8 method_descriptor,
                                   bool search_superclasses, bool search_superinterfaces);

// Raise an UnsatisfiedLinkError relating to the given method.
void bjvm_unsatisfied_link_error(bjvm_thread *thread, const bjvm_cp_method *method) {
  printf("Unsatisfied link error %.*s on %.*s\n", fmt_slice(method->name), fmt_slice(method->my_class->name));
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Method %.*s on class %.*s with descriptor %.*s", fmt_slice(method->name),
          fmt_slice(method->my_class->name), fmt_slice(method->unparsed_descriptor));
  bjvm_raise_vm_exception(thread, STR("java/lang/UnsatisfiedLinkError"), err);
}

// Raise an AbstractMethodError relating to the given method.
void bjvm_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method *method) {
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Found no concrete implementation of %.*s", fmt_slice(method->name), fmt_slice(method->my_class->name));
  bjvm_raise_vm_exception(thread, STR("java/lang/AbstractMethodError"), err);
}

// Raise a NegativeArraySizeException with the given count value.
void bjvm_negative_array_size_exception(bjvm_thread *thread, int count) {
  INIT_STACK_STRING(err, 12);
  bprintf(err, "%d", count);
  bjvm_raise_vm_exception(thread, STR("java/lang/NegativeArraySizeException"), err);
}

// Raise a NullPointerException.
void bjvm_null_pointer_exception(bjvm_thread *thread) {
  bjvm_raise_vm_exception(thread, STR("java/lang/NullPointerException"), null_str());
}

// Raise an ArrayStoreException.
void bjvm_array_store_exception(bjvm_thread *thread, bjvm_utf8 class_name) {
  bjvm_raise_vm_exception(thread, STR("java/lang/ArrayStoreException"), class_name);
}

// Raise an IncompatibleClassChangeError.
void bjvm_incompatible_class_change_error(bjvm_thread *thread, const bjvm_utf8 complaint) {
  bjvm_raise_vm_exception(thread, STR("java/lang/IncompatibleClassChangeError"), complaint);
}

// Raise an ArithmeticException.
void bjvm_arithmetic_exception(bjvm_thread *thread, const bjvm_utf8 complaint) {
  bjvm_raise_vm_exception(thread, STR("java/lang/ArithmeticException"), complaint);
}

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
void bjvm_array_index_oob_exception(bjvm_thread *thread, int index, int length) {
  INIT_STACK_STRING(complaint, 80);
  bprintf(complaint, "Index %d out of bounds for array of length %d", index, length);
  bjvm_raise_vm_exception(thread, STR("java/lang/ArrayIndexOutOfBoundsException"), complaint);
}

void read_string(bjvm_thread *, bjvm_obj_header *obj, int8_t **buf, size_t *len) {
  assert(utf8_equals(hslc(obj->descriptor->name), "java/lang/String"));
  bjvm_obj_header *array = ((struct bjvm_native_String *)obj)->value;
  *buf = ArrayData(array);
  *len = *ArrayLength(array);
}

int read_string_to_utf8(bjvm_thread *thread, heap_string *result, bjvm_obj_header *obj) {
  assert(obj && "String is null");

  int8_t *buf;
  size_t len;
  read_string(nullptr, obj, &buf, &len);
  char *cbuf = malloc(len + 1);

  if (!cbuf) {
    thread->current_exception = thread->out_of_mem_error;
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

// https://github.com/openjdk/jdk11u-dev/blob/be6956b15653f0d870efae89fc1b5df657cca45f/src/java.base/share/classes/java/lang/StringLatin1.java#L52
static bool do_latin1(short *chars, int len) {
  for (int i = 0; i < len; ++i) {
    if ((unsigned short)chars[i] >> 8 != 0)
      return false;
  }
  return true;
}

// TODO restore implementation calling <init> when we can figure it out
bjvm_obj_header *make_string(bjvm_thread *thread, bjvm_utf8 string) {
  bjvm_handle *str = bjvm_make_handle(thread, new_object(thread, thread->vm->cached_classdescs->string));

#define S ((struct bjvm_native_String *)str->obj)

  bjvm_obj_header *result = nullptr;

  short *chars;
  int len;
  convert_modified_utf8_to_chars(string.chars, string.len, &chars, &len, true);

  if (do_latin1(chars, len)) {
    S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, len);
    if (!S->value)
      goto oom;
    for (int i = 0; i < len; ++i) {
      ByteArrayStore(S->value, i, (int8_t)chars[i]);
    }
    S->coder = 0; // LATIN1
    result = (void *)S;
  } else {
    S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, 2 * len);
    if (!S->value)
      goto oom;
    memcpy(ArrayData(S->value), chars, len * sizeof(short));
    S->coder = 1; // UTF-16
    result = (void *)S;
  }

oom:
  free(chars);
  bjvm_drop_handle(thread, str);
  return result;
}

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread, bjvm_utf8 name) {
  const char *chars = name.chars;
  if (chars[0] == 'L') {
    name = slice_to(name, 1, name.len - 1);
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

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const bjvm_utf8 chars) {
  bjvm_obj_header *str = bjvm_hash_table_lookup(&thread->vm->interned_strings, chars.chars, chars.len);
  if (str) {
    return str;
  }
  bjvm_obj_header *new_str = make_string(thread, chars);
  if (!new_str)
    return nullptr; // OOM
  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, chars.chars, chars.len, new_str);
  return new_str;
}

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj) {
#if AGGRESSIVE_DEBUG
  printf("Raising exception of type %s\n", obj->descriptor->name);
#endif

#define T ((struct bjvm_native_Throwable *)obj)
  if (thread->frames_count > 0) {
    bjvm_stack_frame *frame = thread->frames[thread->frames_count - 1];
    if (!bjvm_is_frame_native(frame)) {
      T->faulting_insn = frame->plain.program_counter;
      T->method = frame->plain.method;
    }
  }
  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int bjvm_raise_vm_exception(bjvm_thread *thread, const bjvm_utf8 exception_name, const bjvm_utf8 exception_string) {
  bjvm_classdesc *classdesc = bootstrap_lookup_class(thread, exception_name);
  assert(classdesc->state == BJVM_CD_STATE_INITIALIZED && "VM-generated exceptions should be initialised at VM boot");

  thread->lang_exception_frame = (int)thread->frames_count - 1;

  // Create the exception object
  bjvm_obj_header *obj = new_object(thread, classdesc);
  if (exception_string.chars) {
    bjvm_obj_header *str = make_string(thread, exception_string);
    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("(Ljava/lang/String;)V"), true, false);
    bjvm_thread_run_leaf(thread, method, (bjvm_stack_value[]){{.obj = obj}, {.obj = str}}, nullptr);
  } else {
    bjvm_cp_method *method = bjvm_method_lookup(classdesc, STR("<init>"), STR("()V"), true, false);
    bjvm_thread_run_leaf(thread, method, (bjvm_stack_value[]){{.obj = obj}}, nullptr);
  }

  thread->lang_exception_frame = -1;

#ifndef EMSCRIPTEN
  // fprintf(stderr, "Exception: %.*s: %.*s\n", fmt_slice(exception_name), fmt_slice(exception_string));
#endif
  bjvm_raise_exception_object(thread, obj);
  return 0;
}

bjvm_classdesc *bjvm_primitive_classdesc(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  bjvm_vm *vm = thread->vm;
  return vm->primitive_classes[primitive_order(prim_kind)];
}

struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  return bjvm_primitive_classdesc(thread, prim_kind)->mirror;
}

bjvm_classdesc *bjvm_make_primitive_classdesc(bjvm_thread *thread, bjvm_type_kind kind, const bjvm_utf8 name) {
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
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_BOOLEAN, STR("boolean"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_BYTE)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_BYTE, STR("byte"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_CHAR)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_CHAR, STR("char"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_SHORT)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_SHORT, STR("short"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_INT)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_INT, STR("int"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_LONG)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_LONG, STR("long"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_FLOAT)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_FLOAT, STR("float"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_DOUBLE)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_DOUBLE, STR("double"));
  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_VOID)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_VOID, STR("void"));

  // Set up mirrors
  for (int i = 0; i < 9; ++i) {
    bjvm_classdesc *desc = vm->primitive_classes[i];
    desc->mirror = bjvm_get_class_mirror(thread, desc);
  }
}

bjvm_vm_options bjvm_default_vm_options() {
  bjvm_vm_options options = {0};
  options.heap_size = 1 << 23;
  options.runtime_classpath = STR("./jdk23.jar");
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

int bjvm_define_module(bjvm_vm *vm, bjvm_utf8 module_name, bjvm_obj_header *module) {
  bjvm_module *mod = calloc(1, sizeof(bjvm_module));
  mod->reflection_object = module;
  (void)bjvm_hash_table_insert(&vm->modules, module_name.chars, module_name.len, mod);
  if (utf8_equals(module_name, "java.base")) {
    existing_classes_are_javabase(vm, mod);
  }
  return 0;
}

bjvm_module *bjvm_get_module(bjvm_vm *vm, bjvm_utf8 module_name) {
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

  vm->write_stdout = options.write_stdout;
  vm->write_stderr = options.write_stderr;
  vm->write_byte_param = options.write_byte_param;

  bjvm_native_t const *natives;
  size_t natives_reg_count = bjvm_get_natives_list(&natives);
  for (size_t i = 0; i < natives_reg_count; ++i) {
    bjvm_register_native(vm, natives[i].class_path, natives[i].method_name, natives[i].method_descriptor,
                         natives[i].callback);
  }

  bjvm_register_native_padding(vm);

  return vm;
}

void free_unsafe_allocations(bjvm_vm *vm) {
  for (int i = 0; i < arrlen(vm->unsafe_allocations); ++i) {
    free(vm->unsafe_allocations[i]);
  }
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

bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc, bjvm_utf8 const name, bjvm_utf8 const descriptor) {
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

bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc, const bjvm_utf8 name, const bjvm_utf8 descriptor) {
  bjvm_cp_field *result = bjvm_field_lookup(classdesc, name, descriptor);
  return result;
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
  assert(UC && "UnsafeConstants class not found!");

  bjvm_initialize_class_t ctx = {.args = {thread, UC}};
  future_t init = bjvm_initialize_class(&ctx);
  assert(init.status == FUTURE_READY);

  bjvm_cp_field *address_size = bjvm_field_lookup(UC, STR("ADDRESS_SIZE0"), STR("I")),
                *page_size = bjvm_field_lookup(UC, STR("PAGE_SIZE"), STR("I")),
                *unaligned_access = bjvm_field_lookup(UC, STR("UNALIGNED_ACCESS"), STR("Z"));

  assert(address_size && page_size && unaligned_access && "UnsafeConstants fields not found!");
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
  const int HANDLES_CAPACITY = 100;
  thr->handles = calloc(1, sizeof(bjvm_handle) * HANDLES_CAPACITY);
  thr->handles_capacity = HANDLES_CAPACITY;
  thr->lang_exception_frame = -1;

  thr->async_stack = malloc(4096);
  thr->async_stack_used = 0;
  thr->async_stack_capacity = 4096;

  bjvm_vm_init_primitive_classes(thr);
  init_unsafe_constants(thr);

  init_cached_classdescs_t init = {.args = {thr}};
  future_t result = init_cached_classdescs(&init);
  assert(result.status == FUTURE_READY);

  // Pre-allocate OOM and stack overflow errors
  thr->out_of_mem_error = new_object(thr, vm->cached_classdescs->oom_error);
  thr->stack_overflow_error = new_object(thr, vm->cached_classdescs->stack_overflow_error);

  bootstrap_lookup_class(thr, STR("java/lang/reflect/Field"));
  bootstrap_lookup_class(thr, STR("java/lang/reflect/Constructor"));

  struct bjvm_native_Thread *java_thr = (void *)new_object(thr, vm->cached_classdescs->thread);
  thr->thread_obj = java_thr;

  java_thr->vm_thread = thr;
  java_thr->name = bjvm_intern_string(thr, STR("main"));

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_method_lookup(vm->cached_classdescs->thread, STR("<init>"),
                                                   STR("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"), false, false);
  bjvm_obj_header *name = make_string(thr, STR("main"));
  bjvm_thread_run_leaf(thr, make_thread,
                       (bjvm_stack_value[]){{.obj = (void *)java_thr}, {.obj = main_thread_group}, {.obj = name}},
                       nullptr);

  bjvm_utf8 const phases[3] = {STR("initPhase1"), STR("initPhase2"), STR("initPhase3")};
  bjvm_utf8 const signatures[3] = {STR("()V"), STR("(ZZ)I"), STR("()V")};

  bjvm_cp_method *method;
  bjvm_stack_value ret;
  for (int i = 0; i < sizeof(phases) / sizeof(*phases); ++i) {
    method = bjvm_method_lookup(vm->cached_classdescs->system, phases[i], signatures[i], false, false);
    assert(method);
    bjvm_stack_value args[2] = {{.i = 1}, {.i = 1}};
    bjvm_thread_run_leaf(thr, method, args, &ret);

    assert(!thr->current_exception);

    method = bjvm_method_lookup(vm->cached_classdescs->system, STR("getProperty"),
                                STR("(Ljava/lang/String;)Ljava/lang/String;"), false, false);
    assert(method);
    bjvm_stack_value args2[1] = {{.obj = (void *)bjvm_intern_string(thr, STR("java.home"))}};
    bjvm_thread_run_leaf(thr, method, args2, &ret);

    heap_string java_home;
    assert(read_string_to_utf8(thr, &java_home, ret.obj) == 0);
    free_heap_str(java_home);
  }

  thr->current_exception = nullptr;

  // Call setJavaLangAccess() since we crash before getting there
  method = bjvm_method_lookup(vm->cached_classdescs->system, STR("setJavaLangAccess"), STR("()V"), false, false);
  bjvm_thread_run_leaf(thr, method, nullptr, &ret);

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
  assert(method);

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
  bjvm_stack_value result;
  bjvm_thread_run_leaf(thread, init,
                       (bjvm_stack_value[]){{.obj = (void *)rtype}, {.obj = ptypes->obj}, {.i = 1 /* trusted */}},
                       &result);
  bjvm_drop_handle(thread, ptypes);
  return (void *)result.obj;
}

DEFINE_ASYNC_SL(resolve_mh_mt, BJVM_MH_KIND_LAST + 1) {
  bjvm_classdesc *rtype = nullptr;
  bjvm_classdesc **ptypes = nullptr;
  int ptypes_count = 0;
  int ptypes_capacity = 0;

  switch (args->info->handle_kind) {
  case BJVM_MH_KIND_GET_FIELD:
    // MT should be of the form (C)T, where C is the class the field is found on
    bjvm_cp_field_info *field = &args->info->reference->field;
    bjvm_resolve_class(args->thread, field->class_info);
    AWAIT(bjvm_initialize_class, args->thread, field->class_info->classdesc);
    // reload these because we awaited
    rtype = nullptr;
    ptypes = nullptr;
    ptypes_count = 0;
    ptypes_capacity = 0;
    *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = field->class_info->classdesc;
    rtype = load_class_of_field(args->thread, field->parsed_descriptor);
    break;
  case BJVM_MH_KIND_GET_STATIC:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_PUT_FIELD:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_PUT_STATIC:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE: {
    // MT should be of the form (C,A*)T, where C is the class the method is
    // found on, A* is the list of argument types, and T is the return type
    bjvm_cp_method_info *method = &args->info->reference->methodref;
    bjvm_resolve_class(args->thread, method->class_info);
    AWAIT(bjvm_initialize_class, args->thread, method->class_info->classdesc);

    // reload these because we awaited
    rtype = nullptr;
    ptypes = nullptr;
    ptypes_count = 0;
    ptypes_capacity = 0;

    if (args->info->handle_kind != BJVM_MH_KIND_INVOKE_STATIC) {
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = method->class_info->classdesc;
    }

    for (int i = 0; i < method->descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = load_class_of_field(args->thread, arg);
    }

    rtype = load_class_of_field(args->thread, &method->descriptor->return_type);
    break;
  }
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL: {
    // MT should be of the form (A*)T, where A* is the list of argument types,
    bjvm_cp_method_info *method = &args->info->reference->methodref;
    bjvm_resolve_class(args->thread, method->class_info);
    AWAIT(bjvm_initialize_class, args->thread, method->class_info->classdesc);

    // reload these because we awaited
    rtype = nullptr;
    ptypes = nullptr;
    ptypes_count = 0;
    ptypes_capacity = 0;

    for (int i = 0; i < method->descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = load_class_of_field(args->thread, arg);
    }

    rtype = method->class_info->classdesc;
    break;
  }
  }

  // Call MethodType.makeImpl(rtype, ptypes, true)
  bjvm_cp_method *make = bjvm_method_lookup(args->thread->vm->cached_classdescs->method_type, STR("makeImpl"),
                                            STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                                "lang/invoke/MethodType;"),
                                            false, false);
  bjvm_stack_value result;

  bjvm_handle *ptypes_array = bjvm_make_handle(
      args->thread, CreateObjectArray1D(args->thread, args->thread->vm->cached_classdescs->klass, ptypes_count));
  for (int i = 0; i < ptypes_count; ++i) {
    *((bjvm_obj_header **)ArrayData(ptypes_array->obj) + i) = (void *)bjvm_get_class_mirror(args->thread, ptypes[i]);
  }
  free(ptypes);
  bjvm_thread_run_leaf(args->thread, make,
                       (bjvm_stack_value[]){{.obj = (void *)bjvm_get_class_mirror(args->thread, rtype)},
                                            {.obj = ptypes_array->obj},
                                            {.i = 0}},
                       &result);
  bjvm_drop_handle(args->thread, ptypes_array);

  ASYNC_END((void *)result.obj);
}

DEFINE_ASYNC_SL(bjvm_resolve_method_handle, BJVM_MH_KIND_LAST + 1) {
#define info (args)->info
#define thread (args->thread)

  AWAIT(resolve_mh_mt, thread, info);
  info->resolved_mt = get_async_result(resolve_mh_mt);

  self->DirectMethodHandle = bootstrap_lookup_class(thread, STR("java/lang/invoke/DirectMethodHandle"));
  self->MemberName = bootstrap_lookup_class(thread, STR("java/lang/invoke/MemberName"));
  AWAIT(bjvm_initialize_class, thread, self->DirectMethodHandle);
  AWAIT(bjvm_initialize_class, thread, self->MemberName);

  switch (info->handle_kind) {
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_FIELD:
  case BJVM_MH_KIND_PUT_STATIC: {
    bjvm_cp_field_info *field = &info->reference->field;
    bjvm_resolve_class(thread, field->class_info);
    AWAIT(bjvm_initialize_class, thread, field->class_info->classdesc);
    field = &info->reference->field; // reload because of the await
    self->f = bjvm_field_lookup(field->class_info->classdesc, field->nat->name, field->nat->descriptor);
    bjvm_reflect_initialize_field(thread, field->class_info->classdesc, self->f);
    bjvm_handle *member = bjvm_make_handle(thread, new_object(thread, self->MemberName));
    bjvm_cp_method *make_member =
            bjvm_method_lookup(self->MemberName, STR("<init>"),
              STR("(Ljava/lang/reflect/Field;)V"), false, false);
    bjvm_stack_value result;
    bjvm_thread_run_leaf(
            thread, make_member,
            (bjvm_stack_value[]){
              {.obj = (void *)member->obj},
              {.obj = (void *)self->f->reflection_field}
            }, nullptr);
    bjvm_cp_method *make = bjvm_method_lookup(self->DirectMethodHandle, STR("make"),
                                              STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                                  "invoke/DirectMethodHandle;"),
                                              false, false);
    bjvm_thread_run_leaf(thread, make, (bjvm_stack_value[]){{.obj = member->obj}}, &result);
    bjvm_drop_handle(thread, member);
    ASYNC_RETURN((void *)result.obj);
    break;
  }
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    bjvm_cp_method_info *method = &info->reference->methodref;
    bjvm_resolve_class(thread, method->class_info);

    AWAIT(bjvm_initialize_class, thread, info->reference->methodref.class_info->classdesc);

    method = &info->reference->methodref; // reload because of the await

    self->m = bjvm_method_lookup(method->class_info->classdesc, method->nat->name, method->nat->descriptor, true, true);
    if (BJVM_MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind) {
      bjvm_reflect_initialize_constructor(thread, method->class_info->classdesc, self->m);
    } else {
      bjvm_reflect_initialize_method(thread, method->class_info->classdesc, self->m);
    }

    // Call DirectMethodHandle.make(method, true)
    bjvm_handle *member = bjvm_make_handle(thread, new_object(thread, self->MemberName));
    bool is_ctor = BJVM_MH_KIND_NEW_INVOKE_SPECIAL == info->handle_kind;
    bjvm_cp_method *make_member = bjvm_method_lookup(
        self->MemberName, STR("<init>"),
        is_ctor ? STR("(Ljava/lang/reflect/Constructor;)V") : STR("(Ljava/lang/reflect/Method;)V"), false, false);
    bjvm_stack_value result;
    bjvm_thread_run_leaf(
        thread, make_member,
        (bjvm_stack_value[]){{.obj = (void *)member->obj},
                             {.obj = is_ctor ? (void *)self->m->reflection_ctor : (void *)self->m->reflection_method}},
        nullptr);

    bjvm_cp_method *make = bjvm_method_lookup(self->DirectMethodHandle, STR("make"),
                                              STR("(Ljava/lang/invoke/MemberName;)Ljava/lang/"
                                                  "invoke/DirectMethodHandle;"),
                                              false, false);
    bjvm_thread_run_leaf(thread, make, (bjvm_stack_value[]){{.obj = member->obj}}, &result);
    bjvm_drop_handle(thread, member);

    // Now if the member is a variable arity constructor/method, call withVarargs(true) and return the resulting
    // MethodHandle.
    if (self->m->access_flags & BJVM_ACCESS_VARARGS) {
      bjvm_cp_method *with_varargs = bjvm_method_lookup(self->DirectMethodHandle, STR("withVarargs"),
                                                            STR("(Z)Ljava/lang/invoke/MethodHandle;"), true, false);
      bjvm_stack_value result2;
      bjvm_thread_run_leaf(thread, with_varargs, (bjvm_stack_value[]){{.obj = (void *)result.obj}, {.i = 1}}, &result2);
      ASYNC_RETURN((void *)result2.obj);
    }

    ASYNC_RETURN((void *)result.obj);
  }

  ASYNC_END(nullptr);

#undef info
#undef thread
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

bool is_builtin_class(bjvm_utf8 chars) {
  return strncmp(chars.chars, "java/", 5) == 0 || strncmp(chars.chars, "javax/", 6) == 0 ||
         strncmp(chars.chars, "jdk/", 4) == 0 || strncmp(chars.chars, "sun/", 4) == 0;
}

bjvm_classdesc *bjvm_define_bootstrap_class(bjvm_thread *thread, bjvm_utf8 chars, const uint8_t *classfile_bytes,
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

bjvm_classdesc *bootstrap_lookup_class_impl(bjvm_thread *thread, const bjvm_utf8 name, bool raise_class_not_found) {
  bjvm_vm *vm = thread->vm;

  int dimensions = 0;
  bjvm_utf8 chars = name;
  while (chars.len > 0 && *chars.chars == '[') // munch '[' at beginning
    dimensions++, chars = slice(chars, 1);

  assert(dimensions < 255);
  assert(chars.len > 0);

  bjvm_classdesc *class;

  if (dimensions && *chars.chars != 'L') {
    // Primitive array type
    bjvm_type_kind kind = *chars.chars;
    class = bjvm_primitive_classdesc(thread, kind);
  } else {
    if (dimensions) {
      // Strip L and ;
      chars = slice_to(chars, 1, chars.len - 1);
      assert(chars.len >= 1);
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
    const bjvm_utf8 cf_ending = STR(".class");
    INIT_STACK_STRING(filename, MAX_CF_NAME_LENGTH + 6);
    memcpy(filename.chars, chars.chars, chars.len);
    memcpy(filename.chars + chars.len, cf_ending.chars, cf_ending.len);
    filename.len = chars.len + cf_ending.len;

    uint8_t *bytes;
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

      int i = 0;
      for (; (i < chars.len) && (filename.chars[i] != '.'); ++i)
        filename.chars[i] = filename.chars[i] == '/' ? '.' : filename.chars[i];
      // ClassNotFoundException: com.google.DontBeEvil
      filename = slice_to(filename, 0, i);
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
bjvm_classdesc *bootstrap_lookup_class(bjvm_thread *thread, const bjvm_utf8 name) {
  return bootstrap_lookup_class_impl(thread, name, true);
}

int bjvm_link_array_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
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

int allocate_field(int *current, bjvm_type_kind kind) {
  int result;
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
    UNREACHABLE();
  }
  return result;
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
    assert(classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
    return bjvm_link_array_class(thread, classdesc);
  }
  classdesc->state = BJVM_CD_STATE_LINKED;
  // Link the corresponding array type(s)
  if (classdesc->array_type) {
    bjvm_link_array_class(thread, classdesc->array_type);
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
  int static_offset = 0, nonstatic_offset = super ? super->instance_bytes : sizeof(bjvm_obj_header);
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

void *bump_allocate(bjvm_thread *thread, size_t bytes) {
  // round up to multiple of 8
  bytes = (bytes + 7) & ~7;
  bjvm_vm *vm = thread->vm;
#if AGGRESSIVE_DEBUG
  printf("Allocating %zu bytes, %zu used, %zu capacity\n", bytes, vm->heap_used, vm->heap_capacity);
#endif
  if (vm->heap_used + bytes > vm->heap_capacity) {
    bjvm_major_gc(thread->vm);
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
      assert(ent);

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
        bjvm_obj_header *str = bjvm_intern_string(thread, ent->string.chars);
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
  int error = bjvm_thread_run_leaf(thread, ctor, (bjvm_stack_value[]){{.obj = eiie->obj}, {.obj = exc->obj}}, nullptr);
  if (!error) {
    assert(eiie->obj);
    bjvm_raise_exception_object(thread, eiie->obj);
  }
  bjvm_drop_handle(thread, eiie);
  bjvm_drop_handle(thread, exc);
}

// Call <clinit> on the class, if it hasn't already been called.
DEFINE_ASYNC(bjvm_initialize_class) {
#define thread (args->thread)

  bjvm_classdesc *classdesc = args->classdesc; // must be reloaded after await()
  bool error;                                  // this is a local, but it's ok because we don't use it between
                                               // awaits

  assert(classdesc);
  if (classdesc->state >= BJVM_CD_STATE_INITIALIZING) {
    // Class is already initialized, or currently being initialized.
    // TODO In a multithreaded model, we would need to wait for the other
    // thread to finish initializing the class.
    ASYNC_RETURN(0);
  }

  if (classdesc->state != BJVM_CD_STATE_LINKED) {
    error = bjvm_link_class(thread, classdesc);
    if (error) {
      assert(thread->current_exception);
      ASYNC_RETURN(0);
    }
  }

  classdesc->state = BJVM_CD_STATE_INITIALIZING;
  self->recursive_call_space = calloc(1, sizeof(bjvm_initialize_class_t));
  if (!self->recursive_call_space) {
    thread->current_exception = thread->out_of_mem_error;
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
    thread->current_exception = thread->out_of_mem_error;
    goto done;
  }

  bjvm_cp_method *clinit = bjvm_method_lookup(classdesc, STR("<clinit>"), STR("()V"), false, false);
  if (clinit) {
    error = bjvm_thread_run_root(thread, clinit, nullptr, nullptr);
    if (error && !is_error(thread->current_exception->descriptor)) {
      wrap_in_exception_in_initializer_error(thread);
      goto done;
    }
  }

  error = 0;
done:
  free(self->recursive_call_space);
  classdesc->state = error ? BJVM_CD_STATE_LINKAGE_ERROR : BJVM_CD_STATE_INITIALIZED;
  ASYNC_END(error);

#undef thread
}

int32_t java_idiv(int32_t a, int32_t b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return INT_MIN;
  return a / b;
}

int64_t java_irem(int32_t a, int32_t b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return 0;
  return a % b;
}

int64_t java_ldiv(int64_t a, int64_t b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return LONG_MIN;
  return a / b;
}

int64_t java_lrem(int64_t a, int64_t b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return 0;
  return a % b;
}

bool method_candidate_matches(const bjvm_cp_method *candidate, const bjvm_utf8 name,
                              const bjvm_utf8 method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor.chars ||
          utf8_equals_utf8(candidate->unparsed_descriptor, method_descriptor));
}

// TODO look at this more carefully
bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor, const bjvm_utf8 name, const bjvm_utf8 method_descriptor,
                                   bool search_superclasses, bool search_superinterfaces) {
  assert(descriptor->state >= BJVM_CD_STATE_LINKED);
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

bjvm_async_run_ctx *bjvm_thread_async_run(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args,
                                          bjvm_stack_value *result) {
  assert(method && "Method is null");
  bjvm_async_run_ctx *ctx = calloc(1, sizeof(bjvm_async_run_ctx));
  if (!ctx) {
    thread->current_exception = thread->out_of_mem_error;
    return nullptr;
  }

  int nonstatic = !(method->access_flags & BJVM_ACCESS_STATIC);
  int argc = method->descriptor->args_count + nonstatic;

  ctx->thread = thread;
  ctx->frame = bjvm_push_frame(thread, method, args, argc);
  ctx->result = result;
  ctx->status = BJVM_ASYNC_RUN_RESULT_EXC;
  if (!ctx->frame) // failed to allocate a frame
    return ctx;
  ctx->status = BJVM_ASYNC_RUN_RESULT_INT;
  return ctx;
}

bool bjvm_async_run_step(bjvm_async_run_ctx *ctx) {
  if (ctx->status != BJVM_ASYNC_RUN_RESULT_INT) {
    return true; // done
  }
  ctx->interpreter_state.args = (struct bjvm_interpret_args){ctx->thread, ctx->frame};
  future_t result = bjvm_interpret(&ctx->interpreter_state);
  if (ctx->thread->current_exception) {
    ctx->status = BJVM_ASYNC_RUN_RESULT_EXC;
  } else if (result.status == FUTURE_NOT_READY) {
    ctx->status = BJVM_ASYNC_RUN_RESULT_INT;
  } else {
    *ctx->result = ctx->interpreter_state._result;
    ctx->status = BJVM_ASYNC_RUN_RESULT_OK;
  }
  return ctx->status != BJVM_ASYNC_RUN_RESULT_INT;
}

void bjvm_free_async_run_ctx(bjvm_async_run_ctx *ctx) { free(ctx); }

static int _bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args,
                            bjvm_stack_value *result) {
  bjvm_async_run_ctx *ctx = bjvm_thread_async_run(thread, method, args, result);
  int ret;
  while (!bjvm_async_run_step(ctx)) {
    if (thread->must_unwind) {
      bjvm_raise_vm_exception(thread, STR("java/lang/InternalError"),
                              STR("Attempted to execute an async function while "
                                  "in bjvm_thread_run_root"));
      ret = -1;
      goto done;
    }
  }
  ret = -(ctx->status == BJVM_ASYNC_RUN_RESULT_EXC);

done:
  bjvm_free_async_run_ctx(ctx);
  return ret;
}

int bjvm_thread_run_root(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args,
                         bjvm_stack_value *result) {
  bjvm_stack_value blah;
  if (result == nullptr) {
    result = &blah;
  }
  return _bjvm_thread_run(thread, method, args, result);
}

int bjvm_thread_run_leaf(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args,
                         bjvm_stack_value *result) {
  bjvm_stack_value blah;
  if (result == nullptr) {
    result = &blah;
  }
  return _bjvm_thread_run(thread, method, args, result);
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info) {
  // TODO use current class loader
  // TODO synchronize on some object, probably the class which this info is a
  // part of

  if (info->classdesc)
    return 0; // already succeeded
  if (info->resolution_error) {
    bjvm_raise_exception_object(thread,
                                info->resolution_error); // already failed
    return -1;
  }
  info->classdesc = bootstrap_lookup_class(thread, info->name);
  if (!info->classdesc) {
    info->resolution_error = thread->current_exception;
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
  assert(class->classdesc->state >= BJVM_CD_STATE_LINKED);
  bjvm_cp_field *field = bjvm_field_lookup(class->classdesc, info->nat->name, info->nat->descriptor);
  info->field = field;
  return field == nullptr;
}

void store_stack_value(void *field_location, bjvm_stack_value value, bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    *(int8_t *)field_location = (int8_t)value.i;
    break;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    *(int16_t *)field_location = (int16_t)value.i;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    *(float *)field_location = value.f;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    *(double *)field_location = value.d;
    break;
  case BJVM_TYPE_KIND_INT:
    *(int *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_LONG:
    *(int64_t *)field_location = value.l;
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    *(void **)field_location = value.obj;
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
    result.i = (int)*(int8_t *)field_location;
    break;
  case BJVM_TYPE_KIND_CHAR:
    result.i = *(uint16_t *)field_location;
    break;
  case BJVM_TYPE_KIND_SHORT:
    result.i = *(int16_t *)field_location;
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
    result.l = *(int64_t *)field_location;
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

bool bjvm_is_instanceof_name(const bjvm_obj_header *mirror, const bjvm_utf8 name) {
  return mirror && utf8_equals_utf8(hslc(mirror->descriptor->name), name);
}

bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, STR("java/lang/Class")));
  return ((struct bjvm_native_Class *)mirror)->reflected_class;
}

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Field")));
  // Fields get copied around, but all reference the "root" created by the VM
  bjvm_obj_header *root = ((struct bjvm_native_Field *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Field *)mirror)->reflected_field;
}

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Constructor")));
  // Constructors get copied around, but all reference the "root" created by the
  // VM
  bjvm_obj_header *root = ((struct bjvm_native_Constructor *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Constructor *)mirror)->reflected_ctor;
}

bjvm_cp_method **bjvm_unmirror_method(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, STR("java/lang/reflect/Method")));
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
  assert(klass_init_state.status == FUTURE_READY);
  if (init._result) {
    // TODO raise exception
    UNREACHABLE();
  }

  struct bjvm_native_Class *class_mirror = classdesc->mirror = (void *)new_object(thread, java_lang_Class);
  if (class_mirror) {
    class_mirror->reflected_class = classdesc;
    if (classdesc->module)
      class_mirror->module = classdesc->module->reflection_object;
    class_mirror->componentType =
        classdesc->one_fewer_dim ? (void *)bjvm_get_class_mirror(thread, classdesc->one_fewer_dim) : nullptr;
  }

  return class_mirror;
}

bool bjvm_instanceof_interface(const bjvm_classdesc *o, const bjvm_classdesc *classdesc) {
  if (o == classdesc)
    return true;
  for (int i = 0; i < o->interfaces_count; ++i) {
    if (bjvm_instanceof_interface(o->interfaces[i]->classdesc, classdesc)) {
      return true;
    }
  }
  return false;
}

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target) {
  // TODO compare class loaders too
  if (o == nullptr || o == target)
    return true;

  if (target->kind != BJVM_CD_KIND_ORDINARY) {
    if (o->kind == BJVM_CD_KIND_ORDINARY)
      return false;
    if (o->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
      return target->dimensions == o->dimensions && o->primitive_component == target->primitive_component &&
             (!o->base_component || !target->base_component ||
              bjvm_instanceof(o->base_component, target->base_component));
    }
    // o is 1D primitive array, equality check suffices
    return target->dimensions == o->dimensions && target->primitive_component == o->primitive_component;
  }

  // o is normal object
  const bjvm_classdesc *desc = o;
  while (desc) {
    if (bjvm_instanceof_interface(desc, target))
      return true;
    desc = desc->super_class ? desc->super_class->classdesc : nullptr;
  }
  return false;
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
static void dump_method_type(FILE* stream, struct bjvm_native_MethodType *type) {
  fprintf(stream, "(");
  for (int i = 0; i < *ArrayLength(type->ptypes); ++i) {
    bjvm_classdesc *desc = bjvm_unmirror_class(((bjvm_obj_header **)ArrayData(type->ptypes))[i]);
    fprintf(stream, "%.*s", fmt_slice(desc->name));
    if (i + 1 < *ArrayLength(type->ptypes))
      fprintf(stream, ", ");
  }
  fprintf(stream, ") -> %.*s\n", fmt_slice(bjvm_unmirror_class(type->rtype)->name));
}

heap_string debug_dump_string(bjvm_thread *thread, bjvm_obj_header *header) {
  bjvm_cp_method *toString =
      bjvm_method_lookup(header->descriptor, STR("toString"), STR("()Ljava/lang/String;"), true, true);
  bjvm_stack_value result;
  bjvm_thread_run_root(thread, toString, (bjvm_stack_value[]){{.obj = header}}, &result);
  return AsHeapString(result.obj, on_oom);

on_oom:
  UNREACHABLE();
}

void bjvm_wrong_method_type_error(bjvm_thread *thread, struct bjvm_native_MethodType *provider_mt,
                                  struct bjvm_native_MethodType *targ) {
  UNREACHABLE(); // TODO
}

static inline bjvm_stack_value __checked_pop(bjvm_plain_frame *frame, uint16_t *sd) {
  assert(*sd > 0);
  return frame->values[--*sd];
}

static inline void __checked_push(bjvm_plain_frame *frame, uint16_t *sd, bjvm_stack_value value) {
  assert(*sd < frame->max_stack);
  frame->values[(*sd)++] = value;
}

#define checked_pop(frame) __checked_pop(frame, &sd)
#define checked_push(frame, value) __checked_push(frame, &sd, value)

DEFINE_ASYNC_SL(bjvm_invokevirtual_signature_polymorphic, 100) {
#define target (args->target)
#define provider_mt (args->provider_mt)
#define thread (args->thread)
#define sd (args->sd)

  struct bjvm_native_MethodHandle *mh = (void *)target;
  struct bjvm_native_MethodType *targ = (void *)mh->type;

  // varargs iff mh is of class AsVarargsCollector
  /*bool is_varargs =
      utf8_equals(hslc(mh->base.descriptor->name), "java/lang/invoke/MethodHandleImpl$AsVarargsCollector");
  if (is_varargs) {
    printf("Varargs: %.*s\n", fmt_slice(args->method->name));
    dump_method_type(stderr, targ);
    dump_method_type(stderr, provider_mt);
    // To-implement
    UNREACHABLE();
  }*/

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

    bjvm_stack_value result = value_null();
    int status = bjvm_thread_run_root(thread, asType,
                                      (bjvm_stack_value[]){{.obj = (void *)mh}, {.obj = (void *)provider_mt}}, &result);
    if (status != 0) // asType failed
      ASYNC_RETURN_VOID();
    mh = (void *)result.obj;
  }

  struct bjvm_native_LambdaForm *form = (void *)mh->form;
  struct bjvm_native_MemberName *name = (void *)form->vmentry;

  bjvm_method_handle_kind kind = (name->flags >> 24) & 0xf;

  switch (kind) {
  case BJVM_MH_KIND_GET_FIELD:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_GET_STATIC:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_PUT_FIELD:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_PUT_STATIC:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_STATIC: {
    self->method = name->vmtarget;
    assert(self->method);
    self->argc = self->method->descriptor->args_count;
    self->frame = bjvm_push_frame(thread, self->method, args->frame->values + *sd - self->argc, self->argc);
    self->frame->plain.values[self->frame->plain.max_stack].obj = (void*) mh;
    // TODO arena allocate this in the VM so that it gets freed appropriately
    // if the VM is deleted
    self->interpreter_ctx = calloc(1, sizeof(bjvm_interpret_t));
    AWAIT_INNER(self->interpreter_ctx, bjvm_interpret, thread, self->frame);
    // This many arguments were consumed
    *sd -= self->argc;
    if (self->method->descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID) {
      // Store the result in the frame and increment the stack pointer
      args->frame->values[*sd] = self->interpreter_ctx->_result;
      (*sd)++;
    }
    free(self->interpreter_ctx);
    break;
  }
  case BJVM_MH_KIND_INVOKE_SPECIAL:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    UNREACHABLE();
    break;
  default:
    UNREACHABLE();
  }
  ASYNC_END_VOID();

#undef target
#undef provider_mt
#undef thread
#undef sd
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
  if (self->initializer_ctx._result != 0) {
    ASYNC_RETURN(1);
  }

  info->resolved = bjvm_method_lookup(self->klass->classdesc, info->nat->name, info->nat->descriptor, true, true);
  if (!info->resolved) {
    INIT_STACK_STRING(complaint, 1000);
    complaint = bprintf(complaint, "Could not find method %.*s with descriptor %.*s on class %.*s",
                        fmt_slice(info->nat->name), fmt_slice(info->nat->descriptor), fmt_slice(self->klass->name));
    bjvm_incompatible_class_change_error(thread, complaint);
    ASYNC_RETURN(1);
  }
  ASYNC_END(0);

#undef info
#undef thread
}

int bjvm_multianewarray(bjvm_thread *thread, bjvm_plain_frame *frame, struct bjvm_multianewarray_data *multianewarray,
                        uint16_t *sd) {
  int dims = multianewarray->dimensions;
  assert(*sd >= dims);
  assert(stack_depth(frame) >= dims);
  assert(dims >= 1);

  int error = bjvm_resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  bjvm_link_class(thread, multianewarray->entry->classdesc);

  int dim_sizes[kArrayMaxDimensions];
  for (int i = 0; i < dims; ++i) {
    int dim = frame->values[*sd - dims + i].i;
    if (dim < 0) {
      bjvm_negative_array_size_exception(thread, dim);
      return -1;
    }

    dim_sizes[i] = dim;
  }

  bjvm_obj_header *result = CreateArray(thread, multianewarray->entry->classdesc, dim_sizes, dims);
  frame->values[*sd - dims] = (bjvm_stack_value){.obj = result};
  *sd -= dims - 1;
  return 0;
}

DEFINE_ASYNC_SL(bjvm_resolve_indy_static_argument, BJVM_CP_KIND_LAST + 1) {
#define thread args->thread
#define ent args->ent

  switch (ent->kind) {
  case BJVM_CP_KIND_INTEGER:
    UNREACHABLE("BOX THIS");
    ASYNC_RETURN((bjvm_stack_value){.i = ent->integral.value});
  case BJVM_CP_KIND_FLOAT:
    UNREACHABLE("BOX THIS");
    ASYNC_RETURN((bjvm_stack_value){.f = ent->floating.value});
  case BJVM_CP_KIND_LONG:
    UNREACHABLE("BOX THIS");
    ASYNC_RETURN((bjvm_stack_value){.l = ent->integral.value});
  case BJVM_CP_KIND_DOUBLE:
    UNREACHABLE("BOX THIS");
    ASYNC_RETURN((bjvm_stack_value){.d = ent->floating.value});
  case BJVM_CP_KIND_STRING:
    bjvm_obj_header *string = bjvm_intern_string(thread, ent->string.chars);
    ASYNC_RETURN((bjvm_stack_value){.obj = string});
  case BJVM_CP_KIND_CLASS:
    bjvm_resolve_class(thread, &ent->class_info);
    ASYNC_RETURN((bjvm_stack_value){ .obj = (void*)bjvm_get_class_mirror(thread, ent->class_info.classdesc)});
  case BJVM_CP_KIND_METHOD_TYPE:
    if (!ent->method_type.resolved_mt) {
      ent->method_type.resolved_mt = bjvm_resolve_method_type(thread, ent->method_type.parsed_descriptor);
    }
    ASYNC_RETURN((bjvm_stack_value){.obj = (void *)ent->method_type.resolved_mt});
  case BJVM_CP_KIND_METHOD_HANDLE:
    AWAIT(bjvm_resolve_method_handle, thread, &ent->method_handle);
    ASYNC_RETURN(
        (bjvm_stack_value){.obj = (void *)get_async_result(bjvm_resolve_method_handle)});
  default: {
    UNREACHABLE();
  }
  }

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
  bjvm_handle *bootstrap_handle = self->bootstrap_handle =
      bjvm_make_handle(thread, (void *)get_async_result(bjvm_resolve_method_handle));

  bjvm_stack_value lookup_obj;
  // MethodHandles class
  bjvm_classdesc *lookup_class = thread->vm->cached_classdescs->method_handles;
  bjvm_cp_method *lookup_factory =
      bjvm_method_lookup(lookup_class, STR("lookup"), STR("()Ljava/lang/invoke/MethodHandles$Lookup;"), true, false);

  bjvm_thread_run_root(thread, lookup_factory, (bjvm_stack_value[]){}, &lookup_obj);
  bjvm_handle *lookup_handle = bjvm_make_handle(thread, lookup_obj.obj);

  self->invoke_array = bjvm_make_handle(thread,
    CreateObjectArray1D(thread, thread->vm->cached_classdescs->object, m->args_count + 3));

  self->static_i = 0;
  for (; self->static_i < m->args_count; ++self->static_i) {
    bjvm_cp_entry *arg = m->args[self->static_i];
    AWAIT(bjvm_resolve_indy_static_argument, thread, arg);
    ReferenceArrayStore(self->invoke_array->obj, self->static_i + 3, get_async_result(bjvm_resolve_indy_static_argument).obj);
  }

  bjvm_handle *name = bjvm_make_handle(thread, bjvm_intern_string(thread, indy->name_and_type->name));
  indy->resolved_mt = bjvm_resolve_method_type(thread, indy->method_descriptor);

  ReferenceArrayStore(self->invoke_array->obj, 0, lookup_handle->obj);
  bjvm_drop_handle(thread, lookup_handle);
  ReferenceArrayStore(self->invoke_array->obj, 1, name->obj);
  bjvm_drop_handle(thread, name);
  ReferenceArrayStore(self->invoke_array->obj, 2, (void *)indy->resolved_mt);

  // Invoke the bootstrap method using invokeWithArguments
  bjvm_cp_method *invokeWithArguments = bjvm_method_lookup(self->bootstrap_handle->obj->descriptor, STR("invokeWithArguments"),
                                                   STR("([Ljava/lang/Object;)Ljava/lang/Object;"),
                                                   true, false);

  bjvm_stack_value res;
  bjvm_thread_run_root(thread, invokeWithArguments, (bjvm_stack_value[]){
    { .obj = self->bootstrap_handle->obj },
    { .obj = self->invoke_array->obj }
  }, &res);

  int result;
  if (thread->current_exception) {
    result = -1;
  } else {
    assert(res.obj);
    args->insn->ic = res.obj;
    result = 0;
  }

  bjvm_drop_handle(thread, self->bootstrap_handle);

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

// Java saturates the conversion
static int double_to_int(double x) {
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  if (isnan(x))
    return 0;
  return (int)x;
}

// Java saturates the conversion
static int64_t double_to_long(double x) {
  if (x >= (double)(ULLONG_MAX / 2))
    return LLONG_MAX;
  if (x < (double)LLONG_MIN)
    return LLONG_MIN;
  if (isnan(x))
    return 0;
  return (int64_t)x;
}

DEFINE_ASYNC(bjvm_run_native) {
#define frame args->frame
#define thread args->thread

  assert(frame && "frame is null");

  bjvm_native_callback *handle = frame->method->native_handle;
  bool is_static = frame->method->access_flags & BJVM_ACCESS_STATIC;
  bjvm_handle *target_handle = is_static ? nullptr : frame->values[0].handle;
  bjvm_value *native_args = frame->values + (is_static ? 0 : 1);

  if (!handle->async_ctx_bytes) {
    bjvm_stack_value result =
        ((bjvm_sync_native_callback)handle->ptr)(thread, target_handle, native_args, frame->values_count - !is_static);
    ASYNC_RETURN(result);
  }

  self->native_struct = malloc(handle->async_ctx_bytes);
  AWAIT_FUTURE_EXPR(((bjvm_async_native_callback)handle->ptr)(self->native_struct, thread, target_handle, native_args,
                                                              frame->values_count - !is_static));
  // We've laid out the context struct so that the result is always at offset 0
  bjvm_stack_value result = *(bjvm_stack_value *)self->native_struct;
  free(self->native_struct);

  ASYNC_END(result);

#undef thread
#undef frame
}

void make_invokevtable_polymorphic(bjvm_bytecode_insn *insn) {
  assert(insn->kind == bjvm_insn_invokevtable_monomorphic);
  bjvm_cp_method *method = insn->ic;
  assert(method);
  insn->kind = bjvm_insn_invokevtable_polymorphic;
  insn->ic2 = (void *)method->vtable_index;
}

void make_invokeitable_polymorphic(bjvm_bytecode_insn *insn) {
  assert(insn->kind == bjvm_insn_invokeitable_monomorphic);
  insn->kind = bjvm_insn_invokeitable_polymorphic;
  insn->ic = (void *)insn->cp->methodref.resolved->my_class;
  insn->ic2 = (void *)insn->cp->methodref.resolved->itable_index;
}

static int32_t async_stack_push(bjvm_thread *thread, size_t size) {
  if (unlikely(thread->async_stack_used + size > thread->async_stack_capacity)) {
    size_t new_capacity = thread->async_stack_used + size;
    uint8_t *new_stack = realloc(thread->async_stack, thread->async_stack_capacity);
    if (unlikely(!new_stack)) {
      thread->current_exception = thread->stack_overflow_error;
      return -1;
    }

    thread->async_stack = new_stack;
    thread->async_stack_capacity = new_capacity;
  }

  uint16_t result = thread->async_stack_used;
  thread->async_stack_used += size;
  return result;
}

static void async_stack_pop(bjvm_thread *thread, size_t size) {
  assert(thread->async_stack_used >= size);
  thread->async_stack_used -= size;
}

// Main interpreter
DEFINE_ASYNC(bjvm_interpret) {
#define thread args->thread
#define raw_frame args->raw_frame
  assert(*(thread->frames + thread->frames_count - 1) == raw_frame && "Frame is not last frame on stack");

  bjvm_stack_value the_result = bjvm_interpret_2(thread, raw_frame);
  ASYNC_END(the_result);

#if 0

  // these should be cleaned up at some point

#define async_attempt_allocate(ty)                                                                                     \
  ({                                                                                                                   \
    int32_t offset = async_stack_push(thread, sizeof(ty));                                                             \
    if (offset < 0)                                                                                                    \
      goto done;                                                                                                       \
    (uint16_t)offset;                                                                                                  \
  })

#define async_ctx_ptr(ty, offset) ((ty *)(thread->async_stack + (offset)))

#define _INTERPRETER_AWAIT(result_bind, method_name, ...)                                                              \
  self->async_ctx = async_attempt_allocate(method_name##_t);                                                           \
                                                                                                                       \
  INTERPRETER_AWAIT_INNER(async_ctx_ptr(method_name##_t, self->async_ctx), method_name, ##__VA_ARGS__);                \
  result_bind;                                                                                                         \
  async_stack_pop(thread, sizeof(method_name##_t));                                                                    \
  RELOAD_STACK_LOCALS();

#define bind_result(result_name, method_name)                                                                          \
  method_name##_return_t result_name = async_ctx_ptr(method_name##_t, self->async_ctx)->_result;

#define INTERPRETER_AWAIT(result_name, method_name, ...)                                                               \
  _INTERPRETER_AWAIT(bind_result(result_name, method_name), method_name, ##__VA_ARGS__)

#define INTERPRETER_AWAIT_VOID(method_name, ...) _INTERPRETER_AWAIT(;, method_name, ##__VA_ARGS__)

#define INTERPRETER_AWAIT_INNER(context, method_name, ...)                                                             \
  AWAIT_INNER(context, method_name, ##__VA_ARGS__);                                                                    \
  RELOAD_STACK_LOCALS();

#define RELOAD_STACK_LOCALS()
  // Handle native frames
  if (bjvm_is_frame_native(raw_frame)) {
    INTERPRETER_AWAIT(native_result, bjvm_run_native, thread, bjvm_get_native_frame(raw_frame));
    bjvm_pop_frame(thread, raw_frame);
    ASYNC_RETURN(native_result);
  }
#undef RELOAD_STACK_LOCALS()

  // these are worth caching, so reload them after every await
  bjvm_bytecode_insn *insn;
  bjvm_plain_frame *frame;
  bjvm_bytecode_insn *code;

  // Not used across awaits
  bjvm_stack_value result;

#define RELOAD_STACK_LOCALS()                                                                                          \
  frame = bjvm_get_plain_frame(raw_frame);                                                                             \
  code = frame->method->code->code;                                                                                    \
  insn = &code[frame->program_counter];

  RELOAD_STACK_LOCALS();

  // Current stack depth
  self->sd = stack_depth(frame);
  frame->method->call_count++;

#define sd (self->sd)

#if AGGRESSIVE_DEBUG
  bjvm_cp_method *m = frame->method;
  printf("Calling method %.*s, descriptor %.*s, on class %.*s\n", fmt_slice(m->name), fmt_slice(m->unparsed_descriptor),
         fmt_slice(m->my_class->name));
#endif

  // Interpret the current frame.
interpret_frame:
  while (true) {
    insn = &code[frame->program_counter];
    assert(sd == stack_depth(frame));

#if AGGRESSIVE_DEBUG
    heap_string insn_dump = insn_to_string(insn, frame->program_counter);
    printf("Insn: %.*s\n", fmt_slice(insn_dump));
    printf("Method: %.*s in class %.*s (%.*s:%d)\n", fmt_slice(m->name), fmt_slice(m->my_class->name),
           fmt_slice(m->my_class->source_file ? m->my_class->source_file->name : null_str()),
           bjvm_get_line_number(m->code, frame->program_counter));
    printf("FRAME:\n");
    dump_frame(stdout, frame);

    free_heap_str(insn_dump);
#endif

    static const void *insn_jump_table[] = {&&bjvm_insn_nop,
                                            &&bjvm_insn_aaload,
                                            &&bjvm_insn_aastore,
                                            &&bjvm_insn_aconst_null,
                                            &&bjvm_insn_areturn,
                                            &&bjvm_insn_arraylength,
                                            &&bjvm_insn_athrow,
                                            &&bjvm_insn_baload,
                                            &&bjvm_insn_bastore,
                                            &&bjvm_insn_caload,
                                            &&bjvm_insn_castore,
                                            &&bjvm_insn_d2f,
                                            &&bjvm_insn_d2i,
                                            &&bjvm_insn_d2l,
                                            &&bjvm_insn_dadd,
                                            &&bjvm_insn_daload,
                                            &&bjvm_insn_dastore,
                                            &&bjvm_insn_dcmpg,
                                            &&bjvm_insn_dcmpl,
                                            &&bjvm_insn_ddiv,
                                            &&bjvm_insn_dmul,
                                            &&bjvm_insn_dneg,
                                            &&bjvm_insn_drem,
                                            &&bjvm_insn_dreturn,
                                            &&bjvm_insn_dsub,
                                            &&bjvm_insn_dup,
                                            &&bjvm_insn_dup_x1,
                                            &&bjvm_insn_dup_x2,
                                            &&bjvm_insn_dup2,
                                            &&bjvm_insn_dup2_x1,
                                            &&bjvm_insn_dup2_x2,
                                            &&bjvm_insn_f2d,
                                            &&bjvm_insn_f2i,
                                            &&bjvm_insn_f2l,
                                            &&bjvm_insn_fadd,
                                            &&bjvm_insn_faload,
                                            &&bjvm_insn_fastore,
                                            &&bjvm_insn_fcmpg,
                                            &&bjvm_insn_fcmpl,
                                            &&bjvm_insn_fdiv,
                                            &&bjvm_insn_fmul,
                                            &&bjvm_insn_fneg,
                                            &&bjvm_insn_frem,
                                            &&bjvm_insn_freturn,
                                            &&bjvm_insn_fsub,
                                            &&bjvm_insn_i2b,
                                            &&bjvm_insn_i2c,
                                            &&bjvm_insn_i2d,
                                            &&bjvm_insn_i2f,
                                            &&bjvm_insn_i2l,
                                            &&bjvm_insn_i2s,
                                            &&bjvm_insn_iadd,
                                            &&bjvm_insn_iaload,
                                            &&bjvm_insn_iand,
                                            &&bjvm_insn_iastore,
                                            &&bjvm_insn_idiv,
                                            &&bjvm_insn_imul,
                                            &&bjvm_insn_ineg,
                                            &&bjvm_insn_ior,
                                            &&bjvm_insn_irem,
                                            &&bjvm_insn_ireturn,
                                            &&bjvm_insn_ishl,
                                            &&bjvm_insn_ishr,
                                            &&bjvm_insn_isub,
                                            &&bjvm_insn_iushr,
                                            &&bjvm_insn_ixor,
                                            &&bjvm_insn_l2d,
                                            &&bjvm_insn_l2f,
                                            &&bjvm_insn_l2i,
                                            &&bjvm_insn_ladd,
                                            &&bjvm_insn_laload,
                                            &&bjvm_insn_land,
                                            &&bjvm_insn_lastore,
                                            &&bjvm_insn_lcmp,
                                            &&bjvm_insn_ldiv,
                                            &&bjvm_insn_lmul,
                                            &&bjvm_insn_lneg,
                                            &&bjvm_insn_lor,
                                            &&bjvm_insn_lrem,
                                            &&bjvm_insn_lreturn,
                                            &&bjvm_insn_lshl,
                                            &&bjvm_insn_lshr,
                                            &&bjvm_insn_lsub,
                                            &&bjvm_insn_lushr,
                                            &&bjvm_insn_lxor,
                                            &&bjvm_insn_monitorenter,
                                            &&bjvm_insn_monitorexit,
                                            &&bjvm_insn_pop,
                                            &&bjvm_insn_pop2,
                                            &&bjvm_insn_return,
                                            &&bjvm_insn_saload,
                                            &&bjvm_insn_sastore,
                                            &&bjvm_insn_swap,
                                            &&bjvm_insn_anewarray,
                                            &&bjvm_insn_checkcast,
                                            &&bjvm_insn_getfield,
                                            &&bjvm_insn_getstatic,
                                            &&bjvm_insn_instanceof,
                                            &&bjvm_insn_invokedynamic,
                                            &&bjvm_insn_new,
                                            &&bjvm_insn_putfield,
                                            &&bjvm_insn_putstatic,
                                            &&bjvm_insn_invokevirtual,
                                            &&bjvm_insn_invokespecial,
                                            &&bjvm_insn_invokestatic,
                                            &&bjvm_insn_ldc,
                                            &&bjvm_insn_ldc2_w,
                                            &&bjvm_insn_dload,
                                            &&bjvm_insn_fload,
                                            &&bjvm_insn_iload,
                                            &&bjvm_insn_lload,
                                            &&bjvm_insn_dstore,
                                            &&bjvm_insn_fstore,
                                            &&bjvm_insn_istore,
                                            &&bjvm_insn_lstore,
                                            &&bjvm_insn_aload,
                                            &&bjvm_insn_astore,
                                            &&bjvm_insn_goto,
                                            &&bjvm_insn_jsr,
                                            &&bjvm_insn_if_acmpeq,
                                            &&bjvm_insn_if_acmpne,
                                            &&bjvm_insn_if_icmpeq,
                                            &&bjvm_insn_if_icmpne,
                                            &&bjvm_insn_if_icmplt,
                                            &&bjvm_insn_if_icmpge,
                                            &&bjvm_insn_if_icmpgt,
                                            &&bjvm_insn_if_icmple,
                                            &&bjvm_insn_ifeq,
                                            &&bjvm_insn_ifne,
                                            &&bjvm_insn_iflt,
                                            &&bjvm_insn_ifge,
                                            &&bjvm_insn_ifgt,
                                            &&bjvm_insn_ifle,
                                            &&bjvm_insn_ifnonnull,
                                            &&bjvm_insn_ifnull,
                                            &&bjvm_insn_iconst,
                                            &&bjvm_insn_dconst,
                                            &&bjvm_insn_fconst,
                                            &&bjvm_insn_lconst,
                                            &&bjvm_insn_iinc,
                                            &&bjvm_insn_invokeinterface,
                                            &&bjvm_insn_multianewarray,
                                            &&bjvm_insn_newarray,
                                            &&bjvm_insn_tableswitch,
                                            &&bjvm_insn_lookupswitch,
                                            &&bjvm_insn_ret,
                                            &&bjvm_insn_anewarray_resolved,
                                            &&bjvm_insn_checkcast_resolved,
                                            &&bjvm_insn_instanceof_resolved,
                                            &&bjvm_insn_new_resolved,
                                            &&bjvm_insn_invokevtable_monomorphic,
                                            &&bjvm_insn_invokevtable_polymorphic,
                                            &&bjvm_insn_invokeitable_monomorphic,
                                            &&bjvm_insn_invokeitable_polymorphic,
                                            &&bjvm_insn_invokespecial_resolved,
                                            &&bjvm_insn_invokestatic_resolved,
                                            &&bjvm_insn_invokecallsite,
                                            &&bjvm_insn_getfield_B,
                                            &&bjvm_insn_getfield_C,
                                            &&bjvm_insn_getfield_S,
                                            &&bjvm_insn_getfield_I,
                                            &&bjvm_insn_getfield_J,
                                            &&bjvm_insn_getfield_F,
                                            &&bjvm_insn_getfield_D,
                                            &&bjvm_insn_getfield_Z,
                                            &&bjvm_insn_getfield_L,
                                            &&bjvm_insn_putfield_B,
                                            &&bjvm_insn_putfield_C,
                                            &&bjvm_insn_putfield_S,
                                            &&bjvm_insn_putfield_I,
                                            &&bjvm_insn_putfield_J,
                                            &&bjvm_insn_putfield_F,
                                            &&bjvm_insn_putfield_D,
                                            &&bjvm_insn_putfield_Z,
                                            &&bjvm_insn_putfield_L,
                                            &&bjvm_insn_getstatic_B,
                                            &&bjvm_insn_getstatic_C,
                                            &&bjvm_insn_getstatic_S,
                                            &&bjvm_insn_getstatic_I,
                                            &&bjvm_insn_getstatic_J,
                                            &&bjvm_insn_getstatic_F,
                                            &&bjvm_insn_getstatic_D,
                                            &&bjvm_insn_getstatic_Z,
                                            &&bjvm_insn_getstatic_L,
                                            &&bjvm_insn_putstatic_B,
                                            &&bjvm_insn_putstatic_C,
                                            &&bjvm_insn_putstatic_S,
                                            &&bjvm_insn_putstatic_I,
                                            &&bjvm_insn_putstatic_J,
                                            &&bjvm_insn_putstatic_F,
                                            &&bjvm_insn_putstatic_D,
                                            &&bjvm_insn_putstatic_Z,
                                            &&bjvm_insn_putstatic_L};
    goto *insn_jump_table[insn->kind];

    // Macros to go to the next instruction in the frame
#if ONE_GOTO_PER_INSN
#define NEXT_INSN                                                                                                      \
  {                                                                                                                    \
    insn = &code[++frame->program_counter];                                                                            \
    goto *insn_jump_table[insn->kind];                                                                                 \
  }
#define JMP_INSN                                                                                                       \
  {                                                                                                                    \
    insn = &code[frame->program_counter];                                                                              \
    goto *insn_jump_table[insn->kind];                                                                                 \
  }
#else
#define NEXT_INSN break
#define JMP_INSN continue
#endif

    switch (0) {
      /** BEGIN INSTRUCTION HANDLERS */

    bjvm_insn_nop:
      NEXT_INSN;
    bjvm_insn_aaload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      assert(array->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      bjvm_obj_header *obj = *((bjvm_obj_header **)ArrayData(array) + index);
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_aastore: {
      bjvm_obj_header *value = checked_pop(frame).obj;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      // Instanceof check against the component type
      if (value && !bjvm_instanceof(value->descriptor, array->descriptor->one_fewer_dim)) {
        bjvm_array_store_exception(thread, hslc(value->descriptor->name));
        goto done;
      }
      bjvm_obj_header **obj = (bjvm_obj_header **)ArrayData(array) + index;
      *obj = value;
      NEXT_INSN;
    }
    bjvm_insn_aconst_null:
      checked_push(frame, (bjvm_stack_value){.obj = nullptr});
      NEXT_INSN;
    bjvm_insn_arraylength: {
      bjvm_obj_header *obj = checked_pop(frame).obj;
      if (!obj) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      assert(obj->descriptor->kind != BJVM_CD_KIND_ORDINARY);
      checked_push(frame, (bjvm_stack_value){.i = *ArrayLength(obj)});
      NEXT_INSN;
    }
    bjvm_insn_athrow: {
      bjvm_raise_exception_object(thread, checked_pop(frame).obj);
      goto done;
    }
    bjvm_insn_baload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = *((int8_t *)ArrayData(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_bastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int8_t *)ArrayData(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_caload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = *((uint16_t *)ArrayData(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_sastore:
    bjvm_insn_castore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((uint16_t *)ArrayData(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_d2f: {
      checked_push(frame, (bjvm_stack_value){.f = (float)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_d2i: {
      checked_push(frame, (bjvm_stack_value){.i = double_to_int(checked_pop(frame).d)});
      NEXT_INSN;
    }
    bjvm_insn_d2l: {
      checked_push(frame, (bjvm_stack_value){.l = double_to_long(checked_pop(frame).d)});
      NEXT_INSN;
    }
    bjvm_insn_dadd: {
      checked_push(frame, (bjvm_stack_value){.d = checked_pop(frame).d + checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_dcmpg: {
      double value2 = checked_pop(frame).d, value1 = checked_pop(frame).d;
      if (value1 < value2) {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      }
      NEXT_INSN;
    }
    bjvm_insn_dcmpl: {
      double value2 = checked_pop(frame).d, value1 = checked_pop(frame).d;
      if (value1 > value2) {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      }
      NEXT_INSN;
    }
    bjvm_insn_ddiv: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value){.d = a / b});
      NEXT_INSN;
    }
    bjvm_insn_dmul: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value){.d = a * b});
      NEXT_INSN;
    }
    bjvm_insn_dneg: {
      double a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value){.d = -a});
      NEXT_INSN;
    }
    bjvm_insn_drem: { // deprecated
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value){.d = fmod(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_dsub: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value){.d = a - b});
      NEXT_INSN;
    }
    bjvm_insn_dup: {
      bjvm_stack_value val = checked_pop(frame);
      checked_push(frame, val);
      checked_push(frame, val);
      NEXT_INSN;
    }
    bjvm_insn_dup_x1: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      checked_push(frame, val1);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup_x2: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      checked_push(frame, val1);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2: {
      bjvm_stack_value val1 = checked_pop(frame), val2 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2_x1: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2_x2: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      bjvm_stack_value val4 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val4);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_f2d: {
      checked_push(frame, (bjvm_stack_value){.d = (double)checked_pop(frame).f});
      NEXT_INSN;
    }
    bjvm_insn_f2i: {
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.i = double_to_int(a)});
      NEXT_INSN;
    }
    bjvm_insn_f2l:
      checked_push(frame, (bjvm_stack_value){.l = double_to_long(checked_pop(frame).f)});
      NEXT_INSN;
    bjvm_insn_fadd: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a + b});
      NEXT_INSN;
    }
    bjvm_insn_fcmpg: {
      float value2 = checked_pop(frame).f, value1 = checked_pop(frame).f;

      if (value1 < value2) {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      }

      NEXT_INSN;
    }
    bjvm_insn_fcmpl: {
      float value2 = checked_pop(frame).f, value1 = checked_pop(frame).f;

      if (value1 > value2) {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      }

      NEXT_INSN;
    }
    bjvm_insn_fdiv: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a / b});
      NEXT_INSN;
    }
    bjvm_insn_fmul: {
      float a = checked_pop(frame).f, b = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a * b});
      NEXT_INSN;
    }
    bjvm_insn_fneg: {
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = -a});
      NEXT_INSN;
    }
    bjvm_insn_frem: { // deprecated
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = fmodf(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_fsub: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a - b});
      NEXT_INSN;
    }
    bjvm_insn_i2b: {
      checked_push(frame, (bjvm_stack_value){.i = (int8_t)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_i2c: {
      checked_push(frame, (bjvm_stack_value){.i = checked_pop(frame).i & 0xffff});
      NEXT_INSN;
    }
    bjvm_insn_i2d: {
      checked_push(frame, (bjvm_stack_value){.d = (double)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_i2f: {
      int a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.f = (float)a});
      NEXT_INSN;
    }
    bjvm_insn_i2l: {
      int a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)a});
      NEXT_INSN;
    }
    bjvm_insn_i2s: {
      checked_push(frame, (bjvm_stack_value){.i = (int16_t)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_iadd: {
      uint32_t a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a + b});
      NEXT_INSN;
    }
    bjvm_insn_faload:
    bjvm_insn_iaload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = *((int32_t *)ArrayData(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_iand: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a & b});
      NEXT_INSN;
    }
    bjvm_insn_fastore:
    bjvm_insn_iastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int32_t *)ArrayData(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_idiv: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, STR("/ by zero"));
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = java_idiv(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_imul: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i, c;
      __builtin_mul_overflow(a, b, &c);
      checked_push(frame, (bjvm_stack_value){.i = c});
      NEXT_INSN;
    }
    bjvm_insn_ineg: {
      uint32_t a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = -a});
      NEXT_INSN;
    }
    bjvm_insn_ior: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a | b});
      NEXT_INSN;
    }
    bjvm_insn_irem: {
      int32_t b = checked_pop(frame).i, a = checked_pop(frame).i;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, STR("/ by zero"));
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = java_irem(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_dreturn:
    bjvm_insn_areturn:
    bjvm_insn_lreturn:
    bjvm_insn_freturn:
    bjvm_insn_ireturn: {
      // Look at the frame before and store it there
      result = checked_pop(frame);
      goto done;
    }
    bjvm_insn_ishl: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      uint32_t c = (uint32_t)a << (b & 0x1f); // fuck u UB
      checked_push(frame, (bjvm_stack_value){.i = (int)c});
      NEXT_INSN;
    }
    bjvm_insn_ishr: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a >> b});
      NEXT_INSN;
    }
    bjvm_insn_isub: {
      uint32_t b = checked_pop(frame).i, a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a - b});
      NEXT_INSN;
    }
    bjvm_insn_iushr: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      uint32_t c = (uint32_t)a >> (b & 0x1f);
      checked_push(frame, (bjvm_stack_value){.i = (int)c});
      NEXT_INSN;
    }
    bjvm_insn_ixor: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a ^ b});
      NEXT_INSN;
    }
    bjvm_insn_l2d: {
      checked_push(frame, (bjvm_stack_value){.d = (double)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_l2f: {
      checked_push(frame, (bjvm_stack_value){.f = (float)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_l2i: {
      checked_push(frame, (bjvm_stack_value){.i = (int)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_ladd: {
      uint64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a + b});
      NEXT_INSN;
    }
    bjvm_insn_laload:
    bjvm_insn_daload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.l = *((int64_t *)ArrayData(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_land: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a & b});
      NEXT_INSN;
    }
    bjvm_insn_lastore:
    bjvm_insn_dastore: {
      int64_t value = checked_pop(frame).l;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int64_t *)ArrayData(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_lcmp: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      int sign = a > b ? 1 : a == b ? 0 : -1;
      checked_push(frame, (bjvm_stack_value){.i = sign});
      NEXT_INSN;
    }
    bjvm_insn_ldiv: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, STR("/ by zero"));
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.l = java_ldiv(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_lmul: {
      uint64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a * b});
      NEXT_INSN;
    }
    bjvm_insn_lneg: {
      uint64_t a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = -a});
      NEXT_INSN;
    }
    bjvm_insn_lor: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a | b});
      NEXT_INSN;
    }
    bjvm_insn_lrem: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, STR("/ by zero"));
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.l = java_lrem(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_lshl: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      uint64_t c = (uint64_t)a << (b & 0x3f); // fuck u UB
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)c});
      NEXT_INSN;
    }
    bjvm_insn_lshr: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a >> (b & 0x3f)}); // fuck u UB
      NEXT_INSN;
    }
    bjvm_insn_lsub: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a - b});
      NEXT_INSN;
    }
    bjvm_insn_lushr: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a >> (b & 0x3f)}); // fuck u UB
      NEXT_INSN;
    }
    bjvm_insn_lxor: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a ^ b});
      NEXT_INSN;
    }
    bjvm_insn_monitorenter: {
      bjvm_monitor_state state = bjvm_acquire_monitor(thread, checked_pop(frame).obj);
      [[likely]] if (state == BJVM_MONITOR_STATE_ACQUIRED) {
        NEXT_INSN;
      } else if (state == BJVM_MONITOR_STATE_WAITING) {
        UNREACHABLE(); // todo
        goto done;
      } else {
        UNREACHABLE("unexpected monitor state after acquire");
      }
    }
    bjvm_insn_monitorexit: {
      // TODO
      bjvm_release_monitor(thread, checked_pop(frame).obj);
      NEXT_INSN;
    }
    bjvm_insn_pop: {
      checked_pop(frame);
      NEXT_INSN;
    }
    bjvm_insn_pop2: {
      checked_pop(frame);
      checked_pop(frame);
      NEXT_INSN;
    }
    bjvm_insn_return: { goto done; }
    bjvm_insn_saload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      if (!array) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = *((int16_t *)ArrayData(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_swap: {
      bjvm_stack_value a = checked_pop(frame), b = checked_pop(frame);
      checked_push(frame, a);
      checked_push(frame, b);
      NEXT_INSN;
    }
    bjvm_insn_anewarray: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      if (bjvm_resolve_class(thread, info))
        goto done;
      assert(info->classdesc);
      if (bjvm_link_class(thread, info->classdesc))
        goto done;
      insn->classdesc = info->classdesc;
      insn->kind = bjvm_insn_anewarray_resolved;
      JMP_INSN;
    }
    bjvm_insn_anewarray_resolved: {
      int count = checked_pop(frame).i;
      if (count < 0) {
        bjvm_negative_array_size_exception(thread, count);
        goto done;
      }
      bjvm_obj_header *array = CreateObjectArray1D(thread, insn->classdesc, count);
      if (array) {
        checked_push(frame, (bjvm_stack_value){.obj = array});
      } else {
        goto done; // failed to create array
      }
      NEXT_INSN;
    }
    bjvm_insn_checkcast: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      insn->classdesc = info->classdesc;
      insn->kind = bjvm_insn_checkcast_resolved;
      JMP_INSN;
    }
    bjvm_insn_checkcast_resolved: {
      bjvm_obj_header *obj = frame->values[sd - 1].obj;
      if (obj && unlikely(!bjvm_instanceof(obj->descriptor, insn->classdesc))) {
        INIT_STACK_STRING(complaint, 1000);
        complaint = bprintf(complaint, "Expected instance of %.*s, got %.*s", fmt_slice(insn->classdesc->name),
                            fmt_slice(obj->descriptor->name));
        bjvm_raise_vm_exception(thread, STR("java/lang/ClassCastException"), complaint);
        goto done;
      }
      NEXT_INSN;
    }
    bjvm_insn_instanceof: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      insn->classdesc = info->classdesc;
      insn->kind = bjvm_insn_instanceof_resolved;
      JMP_INSN;
    }
    bjvm_insn_instanceof_resolved: {
      bjvm_obj_header *obj = checked_pop(frame).obj;
      checked_push(frame, (bjvm_stack_value){.i = obj ? bjvm_instanceof(obj->descriptor, insn->classdesc) : 0});
      NEXT_INSN;
    }
    bjvm_insn_invokedynamic: {
      bjvm_cp_indy_info *indy = &insn->cp->indy_info;
      INTERPRETER_AWAIT(resolve_result, indy_resolve, thread, insn, indy);

      if (resolve_result) {
        goto done;
      }

      assert(insn->ic);
      insn->kind = bjvm_insn_invokecallsite;
      struct bjvm_native_CallSite *cs = insn->ic;
      struct bjvm_native_MethodHandle *mh = (void *)cs->target;
      struct bjvm_native_LambdaForm *form = (void *)mh->form;
      struct bjvm_native_MemberName *name = (void *)form->vmentry;
      insn->args = form->arity;
      JMP_INSN;
    }
    bjvm_insn_invokecallsite: {
      // Call the "vmtarget" method with the correct number of arguments
      struct bjvm_native_CallSite *cs = insn->ic;
      struct bjvm_native_MethodHandle *mh = (void *)cs->target;
      struct bjvm_native_LambdaForm *form = (void *)mh->form;
      struct bjvm_native_MemberName *name = (void *)form->vmentry;

      bjvm_method_handle_kind kind = (name->flags >> 24) & 0xf;
      if (kind == BJVM_MH_KIND_INVOKE_STATIC) {
        bool returns = form->result != -1;

        // Invoke name->vmtarget with arguments mh, args
        bjvm_cp_method *invoke = name->vmtarget;
        bjvm_stack_value *arguments = alloca(insn->args * sizeof(bjvm_stack_value));
        arguments[0] = (bjvm_stack_value){.obj = (void *)mh}; // MethodHandle
        memcpy(arguments + 1, frame->values + sd - insn->args + 1, (insn->args - 1) * sizeof(bjvm_stack_value));
        bjvm_stack_frame *invoked = bjvm_push_frame(thread, invoke, arguments, insn->args);
        if (!invoked) {
          goto done;
        }

        memset(&raw_frame->async_frame, 0, sizeof(raw_frame->async_frame));
        INTERPRETER_AWAIT_INNER(&raw_frame->async_frame, bjvm_interpret, thread, invoked);
        if (returns) {
          frame->values[sd - insn->args + 1] = raw_frame->async_frame._result;
        }
        if (thread->current_exception) {
          goto done;
        }
        sd -= insn->args - 1;
        sd += returns;
      } else {
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_new: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;

      if (insn->cp->class_info.classdesc->state < BJVM_CD_STATE_INITIALIZED) {
        INTERPRETER_AWAIT(init_success, bjvm_initialize_class, thread, insn->cp->class_info.classdesc);
      }
      if (thread->current_exception)
        goto done;
      insn->kind = bjvm_insn_new_resolved;
      insn->classdesc = info->classdesc;
      JMP_INSN;
    }
    bjvm_insn_new_resolved: {
      // Create an instance of the class
      bjvm_obj_header *obj = new_object(thread, insn->classdesc);
      if (!obj)
        goto done;
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_getfield:
    bjvm_insn_putfield: {
      bool putfield = insn->kind == bjvm_insn_putfield;

      bjvm_obj_header *obj = frame->values[sd - 1 - putfield].obj;
      if (!obj) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      bjvm_cp_field_info *field_info = &insn->cp->field;
      int error = bjvm_resolve_field(thread, field_info);
      if (error || field_info->field->access_flags & BJVM_ACCESS_STATIC) {
        INIT_STACK_STRING(complaint, 1000);
        bprintf(complaint, "Expected nonstatic field %.*s on class %.*s", fmt_slice(field_info->nat->name),
                fmt_slice(field_info->class_info->name));

        bjvm_incompatible_class_change_error(thread, complaint);
        goto done;
      }
      switch (field_to_kind(field_info->parsed_descriptor)) {
      case BJVM_TYPE_KIND_BOOLEAN:
        insn->kind = putfield ? bjvm_insn_putfield_B : bjvm_insn_getfield_B;
        break;
      case BJVM_TYPE_KIND_CHAR:
        insn->kind = putfield ? bjvm_insn_putfield_C : bjvm_insn_getfield_C;
        break;
      case BJVM_TYPE_KIND_FLOAT:
        insn->kind = putfield ? bjvm_insn_putfield_F : bjvm_insn_getfield_F;
        break;
      case BJVM_TYPE_KIND_DOUBLE:
        insn->kind = putfield ? bjvm_insn_putfield_D : bjvm_insn_getfield_D;
        break;
      case BJVM_TYPE_KIND_BYTE:
        insn->kind = putfield ? bjvm_insn_putfield_B : bjvm_insn_getfield_B;
        break;
      case BJVM_TYPE_KIND_SHORT:
        insn->kind = putfield ? bjvm_insn_putfield_S : bjvm_insn_getfield_S;
        break;
      case BJVM_TYPE_KIND_INT:
        insn->kind = putfield ? bjvm_insn_putfield_I : bjvm_insn_getfield_I;
        break;
      case BJVM_TYPE_KIND_LONG:
        insn->kind = putfield ? bjvm_insn_putfield_J : bjvm_insn_getfield_J;
        break;
      case BJVM_TYPE_KIND_VOID:
        UNREACHABLE();
        break;
      case BJVM_TYPE_KIND_REFERENCE:
        insn->kind = putfield ? bjvm_insn_putfield_L : bjvm_insn_getfield_L;
        break;
      }
      insn->ic = field_info->field;
      insn->ic2 = (void *)field_info->field->byte_offset;
      JMP_INSN;
    }
    bjvm_insn_getstatic:
    bjvm_insn_putstatic: {
      bjvm_cp_field_info *field_info = &insn->cp->field;
      bool putstatic = insn->kind == bjvm_insn_putstatic;

      bjvm_cp_class_info *class = field_info->class_info;
      int error = bjvm_resolve_class(thread, class);
      if (error)
        goto done;

      INTERPRETER_AWAIT(init_result, bjvm_initialize_class, thread, class->classdesc);
      if (init_result)
        goto done;

      bjvm_cp_field *field = bjvm_field_lookup(class->classdesc, field_info->nat->name, field_info->nat->descriptor);
      field_info->field = field;
      if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
        INIT_STACK_STRING(complaint, 1000);
        bprintf(complaint, "Expected static field %.*s on class %.*s", fmt_slice(field_info->nat->name),
                fmt_slice(field_info->class_info->name));
        bjvm_incompatible_class_change_error(thread, complaint);
        goto done;
      }

      switch (field_to_kind(field_info->parsed_descriptor)) {
      case BJVM_TYPE_KIND_BOOLEAN:
        insn->kind = putstatic ? bjvm_insn_putstatic_B : bjvm_insn_getstatic_B;
        break;
      case BJVM_TYPE_KIND_CHAR:
        insn->kind = putstatic ? bjvm_insn_putstatic_C : bjvm_insn_getstatic_C;
        break;
      case BJVM_TYPE_KIND_FLOAT:
        insn->kind = putstatic ? bjvm_insn_putstatic_F : bjvm_insn_getstatic_F;
        break;
      case BJVM_TYPE_KIND_DOUBLE:
        insn->kind = putstatic ? bjvm_insn_putstatic_D : bjvm_insn_getstatic_D;
        break;
      case BJVM_TYPE_KIND_BYTE:
        insn->kind = putstatic ? bjvm_insn_putstatic_B : bjvm_insn_getstatic_B;
        break;
      case BJVM_TYPE_KIND_SHORT:
        insn->kind = putstatic ? bjvm_insn_putstatic_S : bjvm_insn_getstatic_S;
        break;
      case BJVM_TYPE_KIND_INT:
        insn->kind = putstatic ? bjvm_insn_putstatic_I : bjvm_insn_getstatic_I;
        break;
      case BJVM_TYPE_KIND_LONG:
        insn->kind = putstatic ? bjvm_insn_putstatic_J : bjvm_insn_getstatic_J;
        break;
      case BJVM_TYPE_KIND_VOID:
        UNREACHABLE();
        break;
      case BJVM_TYPE_KIND_REFERENCE:
        insn->kind = putstatic ? bjvm_insn_putstatic_L : bjvm_insn_getstatic_L;
        break;
      }
      insn->ic = (void *)field->my_class->static_fields + field->byte_offset;
      JMP_INSN;
    }
    bjvm_insn_invokespecial: {
      bjvm_cp_method_info *method_info = &insn->cp->methodref;
      int argc = insn->args = method_info->descriptor->args_count + 1;
      assert(argc <= sd);
      bjvm_obj_header *target = frame->values[sd - argc].obj;
      if (!target) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }

      INTERPRETER_AWAIT_VOID(resolve_methodref, thread, &insn->cp->methodref);
      if (thread->current_exception)
        goto done;

      method_info = &insn->cp->methodref;
      bjvm_classdesc *lookup_on = method_info->resolved->my_class;
      bjvm_cp_method *method = frame->method;

      // "If all of the following are true, let C [the class to look up the
      // method upon] be the direct superclass of the current class:
      //
      // The resolved method is not an instance initialization method;
      // If the symbolic reference names a class (not an interface), then that
      // class is a superclass of the current class."
      if (!utf8_equals(method_info->resolved->name, "<init>") &&
          (!(lookup_on->access_flags & BJVM_ACCESS_INTERFACE) &&
           (method->my_class != lookup_on && bjvm_instanceof(method->my_class, lookup_on)))) {
        lookup_on = method->my_class->super_class->classdesc;
      }

      // Look at the class and its superclasses first
      bjvm_cp_method *candidate = bjvm_method_lookup(lookup_on, method_info->resolved->name,
                                                     method_info->resolved->unparsed_descriptor, true, false);
      if (!candidate) {
        // Then perform an itable lookup (I rly don't think this is correct...)
        if (method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE) {
          candidate =
              bjvm_itable_lookup(lookup_on, method_info->resolved->my_class, method_info->resolved->itable_index);
        }
        if (!candidate) {
          bjvm_abstract_method_error(thread, method_info->resolved);
          goto done;
        }
      } else if (candidate->access_flags & BJVM_ACCESS_ABSTRACT) {
        bjvm_abstract_method_error(thread, candidate);
        goto done;
      }

      insn->kind = bjvm_insn_invokespecial_resolved;
      insn->ic = candidate;
      JMP_INSN;
    }
    bjvm_insn_invokeinterface: {
      bjvm_cp_method_info *method_info = &insn->cp->methodref;
      int argc = insn->args = method_info->descriptor->args_count + 1;
      assert(argc <= sd);
      bjvm_obj_header *target = frame->values[sd - argc].obj;
      if (!target) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }

      INTERPRETER_AWAIT_VOID(resolve_methodref, thread, &insn->cp->methodref);
      if (thread->current_exception)
        goto done;
      method_info = &insn->cp->methodref;
      if (!(method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE)) {
        insn->kind = bjvm_insn_invokevirtual;
        JMP_INSN;
      }
      insn->kind = bjvm_insn_invokeitable_monomorphic;
      insn->ic =
          bjvm_itable_lookup(target->descriptor, method_info->resolved->my_class, method_info->resolved->itable_index);
      insn->ic2 = target->descriptor;
      if (!insn->ic) {
        bjvm_abstract_method_error(thread, method_info->resolved);
        goto done;
      }
      JMP_INSN;
    }
    bjvm_insn_invokevirtual: {
      bjvm_cp_method *method = frame->method;
      bjvm_cp_method_info *method_info = &insn->cp->methodref;
      int argc = insn->args = method_info->descriptor->args_count + 1;
      assert(argc <= sd);
      bjvm_obj_header *target = frame->values[sd - argc].obj;
      if (!target) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }

      INTERPRETER_AWAIT_VOID(resolve_methodref, thread, &insn->cp->methodref);
      if (thread->current_exception)
        goto done;
      method_info = &insn->cp->methodref;
      if (method_info->resolved->is_signature_polymorphic) {
        INTERPRETER_AWAIT_VOID(bjvm_invokevirtual_signature_polymorphic, thread, frame, &sd, method_info->resolved,
                               bjvm_resolve_method_type(thread, method_info->descriptor), target);
        if (thread->current_exception)
          goto done;
        NEXT_INSN;
      }

      // If we found an interface method, transmogrify into a invokeinterface
      if (method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE) {
        insn->kind = bjvm_insn_invokeinterface;
        JMP_INSN;
      }

      insn->kind = bjvm_insn_invokevtable_monomorphic;
      insn->ic = bjvm_vtable_lookup(target->descriptor, method_info->resolved->vtable_index);
      assert(method);
      insn->ic2 = target->descriptor;
      JMP_INSN;
    }
    bjvm_insn_invokestatic: {
      bjvm_cp_method_info *info = &insn->cp->methodref;

      INTERPRETER_AWAIT(_, resolve_methodref, thread, &insn->cp->methodref);
      if (thread->current_exception)
        goto done;
      info = &insn->cp->methodref;
      insn->kind = bjvm_insn_invokestatic_resolved;
      insn->ic = info->resolved;
      insn->args = info->descriptor->args_count;
      JMP_INSN;
    }
    bjvm_insn_invokestatic_resolved: {
      bjvm_cp_method *method = insn->ic;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
      bjvm_stack_frame *invoked_frame;
      if (method->is_signature_polymorphic) {
        invoked_frame = bjvm_push_native_frame(thread, method, insn->cp->methodref.descriptor,
                                               frame->values + sd - insn->args, insn->args);
      } else {
        invoked_frame = bjvm_push_frame(thread, method, frame->values + sd - insn->args, insn->args);
      }
      if (unlikely(!invoked_frame)) {
        goto done;
      }

      INTERPRETER_AWAIT_INNER(&raw_frame->async_frame, bjvm_interpret, thread, invoked_frame);
      if (thread->current_exception) {
        goto done;
      }
      if (returns) {
        frame->values[sd - insn->args] = raw_frame->async_frame._result;
      }

      sd -= insn->args;
      sd += returns;
      NEXT_INSN;
    }
    bjvm_insn_ldc: {
      bjvm_cp_entry *ent = insn->cp;
      switch (ent->kind) {
      case BJVM_CP_KIND_INTEGER:
        checked_push(frame, (bjvm_stack_value){.i = ent->integral.value});
        break;
      case BJVM_CP_KIND_FLOAT:
        checked_push(frame, (bjvm_stack_value){.f = ent->floating.value});
        break;
      case BJVM_CP_KIND_CLASS: {
        // Initialize the class, then get its Java mirror
        int error = bjvm_resolve_class(thread, &ent->class_info);
        if (error)
          goto done;
        error = bjvm_link_class(thread, ent->class_info.classdesc);
        if (error)
          goto done;
        bjvm_obj_header *obj = (void *)bjvm_get_class_mirror(thread, ent->class_info.classdesc);
        checked_push(frame, (bjvm_stack_value){.obj = obj});
        break;
      }
      case BJVM_CP_KIND_STRING: {
        bjvm_utf8 s = ent->string.chars;
        bjvm_obj_header *obj = bjvm_intern_string(thread, s);
        checked_push(frame, (bjvm_stack_value){.obj = obj});
        break;
      }
      default:
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_ldc2_w: {
      const bjvm_cp_entry *ent = insn->cp;
      switch (ent->kind) {
      case BJVM_CP_KIND_LONG:
        checked_push(frame, (bjvm_stack_value){.l = ent->integral.value});
        break;
      case BJVM_CP_KIND_DOUBLE:
        checked_push(frame, (bjvm_stack_value){.d = ent->floating.value});
        break;
      default:
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_aload:
    bjvm_insn_lload:
    bjvm_insn_iload:
    bjvm_insn_fload:
    bjvm_insn_dload: {
      checked_push(frame, frame->values[frame->max_stack + insn->index]);
      NEXT_INSN;
    }
    bjvm_insn_dstore:
    bjvm_insn_fstore:
    bjvm_insn_astore:
    bjvm_insn_lstore:
    bjvm_insn_istore: {
      frame->values[frame->max_stack + insn->index] = checked_pop(frame);
      NEXT_INSN;
    }
    bjvm_insn_goto: {
      frame->program_counter = insn->index;
      JMP_INSN;
    }
    bjvm_insn_jsr:
      UNREACHABLE("bjvm_insn_jsr");
      NEXT_INSN;
    bjvm_insn_if_acmpeq: {
      bjvm_obj_header *a = checked_pop(frame).obj, *b = checked_pop(frame).obj;
      if (a == b) {
        frame->program_counter = insn->index;
        JMP_INSN;
      }
      NEXT_INSN;
    }
    bjvm_insn_if_acmpne: {
      bjvm_obj_header *a = checked_pop(frame).obj, *b = checked_pop(frame).obj;
      if (a != b) {
        frame->program_counter = insn->index;
        JMP_INSN;
      }
      NEXT_INSN;
    }

#define MAKE_INT_COMPARE(op)                                                                                           \
  {                                                                                                                    \
    int b = checked_pop(frame).i, a = checked_pop(frame).i;                                                            \
    if (a op b) {                                                                                                      \
      frame->program_counter = insn->index;                                                                            \
      JMP_INSN;                                                                                                        \
    }                                                                                                                  \
    NEXT_INSN;                                                                                                         \
  }
    bjvm_insn_if_icmpeq:
      MAKE_INT_COMPARE(==);
    bjvm_insn_if_icmpne:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_if_icmplt:
      MAKE_INT_COMPARE(<);
    bjvm_insn_if_icmpge:
      MAKE_INT_COMPARE(>=);
    bjvm_insn_if_icmpgt:
      MAKE_INT_COMPARE(>);
    bjvm_insn_if_icmple:
      MAKE_INT_COMPARE(<=);

#undef MAKE_INT_COMPARE
#define MAKE_INT_COMPARE(op)                                                                                           \
  {                                                                                                                    \
    int a = checked_pop(frame).i;                                                                                      \
    if (a op 0) {                                                                                                      \
      frame->program_counter = insn->index;                                                                            \
      JMP_INSN;                                                                                                        \
    }                                                                                                                  \
    NEXT_INSN;                                                                                                         \
  }

    bjvm_insn_ifeq:
      MAKE_INT_COMPARE(==);
    bjvm_insn_ifne:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_iflt:
      MAKE_INT_COMPARE(<);
    bjvm_insn_ifge:
      MAKE_INT_COMPARE(>=);
    bjvm_insn_ifgt:
      MAKE_INT_COMPARE(>);
    bjvm_insn_ifle:
      MAKE_INT_COMPARE(<=);
    bjvm_insn_ifnonnull:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_ifnull:
      MAKE_INT_COMPARE(==)

#undef MAKE_INT_COMPARE
    bjvm_insn_iconst: {
      checked_push(frame, (bjvm_stack_value){.i = (int)insn->integer_imm});
      NEXT_INSN;
    }
    bjvm_insn_dconst: {
      checked_push(frame, (bjvm_stack_value){.d = (int)insn->d_imm});
      NEXT_INSN;
    }
    bjvm_insn_fconst: {
      checked_push(frame, (bjvm_stack_value){.f = (int)insn->f_imm});
      NEXT_INSN;
    }
    bjvm_insn_lconst: {
      checked_push(frame, (bjvm_stack_value){.l = insn->integer_imm});
      NEXT_INSN;
    }
    bjvm_insn_iinc: {
      frame->values[frame->max_stack + insn->iinc.index].i += insn->iinc.const_;
      NEXT_INSN;
    }
    bjvm_insn_multianewarray: {
      if (bjvm_multianewarray(thread, frame, &insn->multianewarray, &sd))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_newarray: {
      int count = checked_pop(frame).i;

      if (count < 0) {
        bjvm_negative_array_size_exception(thread, count);
        goto done;
      }

      bjvm_obj_header *array = CreatePrimitiveArray1D(thread, insn->array_type, count);
      if (array) {
        checked_push(frame, (bjvm_stack_value){.obj = array});
      } else {
        goto done; // OOM
      }
      NEXT_INSN;
    }
    bjvm_insn_tableswitch: {
      int value = checked_pop(frame).i;
      struct bjvm_bc_tableswitch_data data = insn->tableswitch;
      if (value < data.low || value > data.high) {
        frame->program_counter = data.default_target;
        JMP_INSN;
      }
      frame->program_counter = data.targets[value - data.low];
      JMP_INSN;
    }
    bjvm_insn_lookupswitch: {
      int value = checked_pop(frame).i;
      struct bjvm_bc_lookupswitch_data data = insn->lookupswitch;
      bool found = false;
      for (int i = 0; i < data.keys_count; ++i) {
        if (data.keys[i] == value) {
          frame->program_counter = data.targets[i];
          found = true;
          break;
        }
      }

      if (!found)
        frame->program_counter = data.default_target;
      JMP_INSN;
    }
    bjvm_insn_ret:
      UNREACHABLE("bjvm_insn_ret");
      NEXT_INSN;
    bjvm_insn_invokevtable_monomorphic:
    bjvm_insn_invokeitable_monomorphic: {
      bjvm_obj_header *target = frame->values[sd - insn->args].obj;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
      if (target == nullptr) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      if (target->descriptor != insn->ic2) {
        if (insn->kind == bjvm_insn_invokevtable_monomorphic)
          make_invokevtable_polymorphic(insn);
        else
          make_invokeitable_polymorphic(insn);
        JMP_INSN;
      }
      bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, insn->ic, frame->values + sd - insn->args, insn->args);
      if (!invoked_frame)
        goto done;

      INTERPRETER_AWAIT_INNER(&raw_frame->async_frame, bjvm_interpret, thread, invoked_frame);
      if (thread->current_exception)
        goto done;
      if (returns) {
        frame->values[sd - insn->args] = raw_frame->async_frame._result;
      }
      sd -= insn->args;
      sd += returns;
      NEXT_INSN;
    }
    bjvm_insn_invokevtable_polymorphic:
    bjvm_insn_invokeitable_polymorphic: {
      bjvm_obj_header *target = frame->values[sd - insn->args].obj;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
      if (target == nullptr) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      bjvm_cp_method *target_method;
      if (insn->kind == bjvm_insn_invokevtable_polymorphic)
        target_method = bjvm_vtable_lookup(target->descriptor, (int)insn->ic2);
      else {
        target_method = bjvm_itable_lookup(target->descriptor, insn->ic, (int)insn->ic2);
        if (!target_method) {
          bjvm_abstract_method_error(thread, insn->cp->methodref.resolved);
          goto done;
        }
      }
      assert(target_method);
      bjvm_stack_frame *invoked_frame =
          bjvm_push_frame(thread, target_method, frame->values + sd - insn->args, insn->args);
      if (!invoked_frame)
        goto done;

      INTERPRETER_AWAIT_INNER(&raw_frame->async_frame, bjvm_interpret, thread, invoked_frame);
      if (thread->current_exception)
        goto done;
      if (returns) {
        frame->values[sd - insn->args] = raw_frame->async_frame._result;
      }
      sd -= insn->args;
      sd += returns;
      NEXT_INSN;
    }
    bjvm_insn_invokespecial_resolved: {
      bjvm_obj_header *target = frame->values[sd - insn->args].obj;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
      if (target == nullptr) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      bjvm_cp_method *target_method = insn->ic;
      bjvm_stack_frame *invoked_frame =
          bjvm_push_frame(thread, target_method, frame->values + sd - insn->args, insn->args);
      if (!invoked_frame)
        goto done;

      INTERPRETER_AWAIT_INNER(&raw_frame->async_frame, bjvm_interpret, thread, invoked_frame);
      if (thread->current_exception)
        goto done;

      if (returns) {
        frame->values[sd - insn->args] = raw_frame->async_frame._result;
      }

      sd -= insn->args;
      sd += returns;
      NEXT_INSN;
    }

#define GETFIELD_IMPL(kind)                                                                                            \
  {                                                                                                                    \
    bjvm_obj_header *obj = checked_pop(frame).obj;                                                                     \
    if (!obj) {                                                                                                        \
      bjvm_null_pointer_exception(thread);                                                                             \
      goto done;                                                                                                       \
    }                                                                                                                  \
    checked_push(frame, load_stack_value((void *)obj + (int)(uintptr_t)insn->ic2, kind));                              \
    NEXT_INSN;                                                                                                         \
  }

#define PUTFIELD_IMPL(kind)                                                                                            \
  {                                                                                                                    \
    bjvm_stack_value value = checked_pop(frame);                                                                       \
    bjvm_obj_header *obj = checked_pop(frame).obj;                                                                     \
    if (!obj) {                                                                                                        \
      bjvm_null_pointer_exception(thread);                                                                             \
      goto done;                                                                                                       \
    }                                                                                                                  \
    store_stack_value((void *)obj + (int)(uintptr_t)insn->ic2, value, kind);                                           \
    NEXT_INSN;                                                                                                         \
  }

#define PUTSTATIC_IMPL(kind)                                                                                           \
  {                                                                                                                    \
    bjvm_stack_value value = checked_pop(frame);                                                                       \
    store_stack_value(insn->ic, value, kind);                                                                          \
    NEXT_INSN;                                                                                                         \
  }

#define GETSTATIC_IMPL(kind)                                                                                           \
  {                                                                                                                    \
    checked_push(frame, load_stack_value(insn->ic, kind));                                                             \
    NEXT_INSN;                                                                                                         \
  }

    bjvm_insn_getfield_B:
      GETFIELD_IMPL(BJVM_TYPE_KIND_BYTE);
    bjvm_insn_getfield_C:
      GETFIELD_IMPL(BJVM_TYPE_KIND_CHAR);
    bjvm_insn_getfield_F:
      GETFIELD_IMPL(BJVM_TYPE_KIND_FLOAT);
    bjvm_insn_getfield_D:
      GETFIELD_IMPL(BJVM_TYPE_KIND_DOUBLE);
    bjvm_insn_getfield_I:
      GETFIELD_IMPL(BJVM_TYPE_KIND_INT);
    bjvm_insn_getfield_J:
      GETFIELD_IMPL(BJVM_TYPE_KIND_LONG);
    bjvm_insn_getfield_S:
      GETFIELD_IMPL(BJVM_TYPE_KIND_SHORT);
    bjvm_insn_getfield_L:
      GETFIELD_IMPL(BJVM_TYPE_KIND_REFERENCE);
    bjvm_insn_getfield_Z:
      GETFIELD_IMPL(BJVM_TYPE_KIND_BOOLEAN);
    bjvm_insn_putfield_B:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_BYTE);
    bjvm_insn_putfield_C:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_CHAR);
    bjvm_insn_putfield_F:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_FLOAT);
    bjvm_insn_putfield_D:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_DOUBLE);
    bjvm_insn_putfield_I:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_INT);
    bjvm_insn_putfield_J:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_LONG);
    bjvm_insn_putfield_S:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_SHORT);
    bjvm_insn_putfield_L:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_REFERENCE);
    bjvm_insn_putfield_Z:
      PUTFIELD_IMPL(BJVM_TYPE_KIND_BOOLEAN);
    bjvm_insn_getstatic_B:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_BYTE);
    bjvm_insn_getstatic_C:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_CHAR);
    bjvm_insn_getstatic_F:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_FLOAT);
    bjvm_insn_getstatic_D:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_DOUBLE);
    bjvm_insn_getstatic_I:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_INT);
    bjvm_insn_getstatic_J:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_LONG);
    bjvm_insn_getstatic_S:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_SHORT);
    bjvm_insn_getstatic_L:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_REFERENCE);
    bjvm_insn_getstatic_Z:
      GETSTATIC_IMPL(BJVM_TYPE_KIND_BOOLEAN);
    bjvm_insn_putstatic_B:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_BYTE);
    bjvm_insn_putstatic_C:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_CHAR);
    bjvm_insn_putstatic_F:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_FLOAT);
    bjvm_insn_putstatic_D:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_DOUBLE);
    bjvm_insn_putstatic_I:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_INT);
    bjvm_insn_putstatic_J:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_LONG);
    bjvm_insn_putstatic_S:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_SHORT);
    bjvm_insn_putstatic_L:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_REFERENCE);
    bjvm_insn_putstatic_Z:
      PUTSTATIC_IMPL(BJVM_TYPE_KIND_BOOLEAN);
    }

    frame->program_counter++;
  }

done:;
  bjvm_cp_method *method = frame->method;

  if (thread->current_exception != nullptr && method) {
    bjvm_attribute_exception_table *table = method->code->exception_table;
    if (table) {
      int pc = frame->program_counter;
      for (int i = 0; i < table->entries_count; ++i) {
        bjvm_exception_table_entry ent = table->entries[i];
        if (ent.start_insn <= pc && pc < ent.end_insn) {
          if (ent.catch_type) {
            int error = bjvm_resolve_class(thread, ent.catch_type);
            if (error)
              goto done; // O dear
          }
          if (!ent.catch_type || bjvm_instanceof(thread->current_exception->descriptor, ent.catch_type->classdesc)) {
            frame->program_counter = ent.handler_insn;
            sd = 1;
            frame->values[0] = (bjvm_stack_value){.obj = thread->current_exception};
            thread->current_exception = nullptr;

            goto interpret_frame;
          }
        }
      }
    }
  }

  bjvm_pop_frame(thread, raw_frame);
  ASYNC_END(result);
#endif
}
// #pragma GCC diagnostic pop

int bjvm_get_line_number(const bjvm_attribute_code *code, uint16_t pc) {
  assert(code && "code is null");
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
#undef thread
#undef raw_frame
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (!vm->main_thread_group) {
    bjvm_classdesc *ThreadGroup = vm->cached_classdescs->thread_group;
    bjvm_cp_method *init = bjvm_method_lookup(ThreadGroup, STR("<init>"), STR("()V"), false, false);

    assert(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    bjvm_thread_run_root(thread, init, args, nullptr);
  }
  return vm->main_thread_group;
}