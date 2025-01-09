#define AGGRESSIVE_DEBUG 0

// If set, the interpreter will use a goto per instruction to jump to the next
// instruction. This messes up the debug dumps but can lead to slightly better
// performance because the branch predictor has more information about pairs
// of instructions which tend to follow another.
#define ONE_GOTO_PER_INSN 0
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
#include "natives.h"
#include "objects.h"
#include "strings.h"
#include "util.h"
#include "wasm_jit.h"

#define MAX_CF_NAME_LENGTH 1000

uint16_t stack_depth(const bjvm_plain_frame *frame) {
  assert(frame->method && "Can't get stack depth of fake frame");
  bjvm_code_analysis *analy = frame->method->code_analysis;
  int pc = frame->program_counter;
  assert(pc < analy->insn_count);
  return analy->insn_index_to_stack_depth[pc];
}

bool bjvm_is_frame_native(const bjvm_stack_frame *frame) {
  return !!frame->plain.is_native;
}

bjvm_native_frame *bjvm_get_native_frame(bjvm_stack_frame *frame) {
  assert(bjvm_is_frame_native(frame));
  return &frame->native;
}

bjvm_plain_frame *bjvm_get_plain_frame(bjvm_stack_frame *frame) {
  assert(!bjvm_is_frame_native(frame));
  return &frame->plain;
}

bjvm_cp_method *bjvm_get_frame_method(bjvm_stack_frame *frame) {
  return bjvm_is_frame_native(frame) ? frame->native.method
                                     : frame->plain.method;
}

bjvm_stack_value *bjvm_get_frame_result_of_next(bjvm_stack_frame *frame) {
  return bjvm_is_frame_native(frame) ? &frame->native.result_of_next
                                     : &frame->plain.result_of_next;
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
  if (handle == &thread->null_handle)
    return;
  assert(handle >= thread->handles &&
         handle < thread->handles + thread->handles_capacity);
  handle->obj = nullptr;
}

void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method);
void bjvm_abstract_method_error(bjvm_thread *thread,
                                const bjvm_cp_method *method);

// For each argument, if it's a reference, wrap it in a handle; otherwise
// just memcpy it over since the representations of primitives are the same
// between bjvm_stack_value and bjvm_value
static void make_handles_array(bjvm_thread *thread,
                               const bjvm_method_descriptor *descriptor,
                               bool is_static, bjvm_stack_value *stack_args,
                               bjvm_value *args) {
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

static void drop_handles_array(bjvm_thread *thread,
                               const bjvm_cp_method *method,
                               const bjvm_method_descriptor *desc,
                               bjvm_value *args) {
  bool is_static = method->access_flags & BJVM_ACCESS_STATIC;
  if (!is_static)
    bjvm_drop_handle(thread, args[0].handle);
  for (int i = 0; i < desc->args_count; ++i)
    if (field_to_kind(desc->args + i) == BJVM_TYPE_KIND_REFERENCE)
      bjvm_drop_handle(thread, args[i + !is_static].handle);
}

bjvm_stack_frame *
bjvm_push_native_frame(bjvm_thread *thread, bjvm_cp_method *method,
                       const bjvm_method_descriptor *descriptor,
                       bjvm_stack_value *args, int argc) {
  bjvm_native_callback *native = method->native_handle;
  if (!native) {
    bjvm_unsatisfied_link_error(thread, method);
    return nullptr;
  }

  const size_t header_bytes = sizeof(bjvm_native_frame);
  size_t args_bytes = argc * sizeof(bjvm_value);
  size_t total = header_bytes + args_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  assert((uintptr_t)frame % 8 == 0 && "Frame is aligned");
  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) =
      frame;

  frame->native.is_native = 1;
  frame->native.values_count = argc;
  frame->native.method = method;
  frame->native.method_shape = descriptor;
  frame->native.result_of_next = value_null();
  frame->native.state = 0;
  frame->native.async_native_data = nullptr;
  frame->native.free_async_native_data = nullptr;

  // Now wrap arguments in handles and copy them into the frame
  make_handles_array(thread, descriptor,
                     method->access_flags & BJVM_ACCESS_STATIC, args,
                     frame->native.values);
  return frame;
}

bjvm_stack_frame *bjvm_push_plain_frame(bjvm_thread *thread,
                                        bjvm_cp_method *method,
                                        bjvm_stack_value *args, int argc) {
  const bjvm_attribute_code *code = method->code;
  if (!code) {
    bjvm_abstract_method_error(thread, method);
    return nullptr;
  }

  assert(argc <= code->max_locals);

  const size_t header_bytes = sizeof(bjvm_plain_frame);
  size_t values_bytes =
      ((int)code->max_locals + code->max_stack) * sizeof(bjvm_stack_value);
  size_t total = header_bytes + values_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    return nullptr;
  }

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);

#if !SKIP_CLEARING_FRAME
  memset(&frame->plain, 0, total);
#endif

  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) =
      frame;
  frame->plain.is_native = 0;
  frame->plain.values_count = code->max_stack + code->max_locals;
  frame->plain.program_counter = 0;
  frame->plain.max_stack = code->max_stack;
  frame->plain.method = method;
  frame->plain.result_of_next = value_null();
  frame->plain.state = 0;

  // Copy in the arguments
  if (likely(argc)) {
    memcpy(frame->plain.values + code->max_stack, args,
           argc * sizeof(bjvm_stack_value));
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
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method,
                                  bjvm_stack_value *args, int argc) {
  assert(method != nullptr && "Method is null");
  bool argc_ok = argc == method->descriptor->args_count +
                             !(method->access_flags & BJVM_ACCESS_STATIC);
  assert(argc_ok && "Wrong argc");
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    return bjvm_push_native_frame(thread, method, method->descriptor, args,
                                  argc);
  }
  return bjvm_push_plain_frame(thread, method, args, argc);
}

const char *infer_type(bjvm_code_analysis *analysis, int insn, int index) {
  bjvm_compressed_bitset refs = analysis->insn_index_to_references[insn],
                         ints = analysis->insn_index_to_ints[insn],
                         floats = analysis->insn_index_to_floats[insn],
                         doubles = analysis->insn_index_to_doubles[insn],
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
    const char *is_ref =
        infer_type(frame->method->code_analysis, frame->program_counter, i);
    write +=
        snprintf(write, end - write, " stack[%d] = [ ref = %p, int = %d ] %s\n",
                 i, value.obj, value.i, is_ref);
  }

  for (int i = frame->max_stack; i < frame->values_count; ++i) {
    bjvm_stack_value value = frame->values[i];
    const char *is_ref =
        infer_type(frame->method->code_analysis, frame->program_counter, i);
    write +=
        snprintf(write, end - write, "locals[%d] = [ ref = %p, int = %d ] %s\n",
                 i - frame->max_stack, value.obj, value.i, is_ref);
  }

  fprintf(stream, "%s", buf);
}

void bjvm_pop_frame(bjvm_thread *thr,
                    [[maybe_unused]] const bjvm_stack_frame *reference) {
  assert(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  assert(reference == nullptr || reference == frame);
  if (bjvm_is_frame_native(frame)) {
    drop_handles_array(thr, frame->native.method, frame->native.method_shape,
                       frame->native.values);
  }
  thr->frames_count--;
  thr->frame_buffer_used =
      thr->frames_count == 0 ? 0 : (char *)frame - thr->frame_buffer;
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

void bjvm_register_native(bjvm_vm *vm, const bjvm_utf8 class,
                          const bjvm_utf8 method_name,
                          const bjvm_utf8 method_descriptor,
                          bjvm_native_callback callback) {
  native_entries *existing =
      bjvm_hash_table_lookup(&vm->natives, class.chars, class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)bjvm_hash_table_insert(&vm->natives, class.chars, class.len,
                                 existing);
  }

  native_entry *ent = VECTOR_PUSH(existing->entries, existing->entries_count,
                                  existing->entries_cap);
  ent->name = method_name;
  ent->descriptor = method_descriptor;
  ent->callback = callback;
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor, bjvm_utf8 name,
                                   bjvm_utf8 method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces);

// Raise an UnsatisfiedLinkError relating to the given method.
void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method) {
  printf("Unsatisfied link error %.*s on %.*s\n", fmt_slice(method->name),
         fmt_slice(method->my_class->name));
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Method %.*s on class %.*s with descriptor %.*s",
          fmt_slice(method->name), fmt_slice(method->my_class->name),
          fmt_slice(method->unparsed_descriptor));
  bjvm_raise_exception(thread, STR("java/lang/UnsatisfiedLinkError"), err);
}

// Raise an AbstractMethodError relating to the given method.
void bjvm_abstract_method_error(bjvm_thread *thread,
                                const bjvm_cp_method *method) {
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Found no concrete implementation of %.*s",
          fmt_slice(method->name), fmt_slice(method->my_class->name));
  bjvm_raise_exception(thread, STR("java/lang/AbstractMethodError"), err);
}

// Raise a NegativeArraySizeException with the given count value.
void bjvm_negative_array_size_exception(bjvm_thread *thread, int count) {
  INIT_STACK_STRING(err, 12);
  bprintf(err, "%d", count);
  bjvm_raise_exception(thread, STR("java/lang/NegativeArraySizeException"),
                       err);
}

// Raise a NullPointerException.
void bjvm_null_pointer_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, STR("java/lang/NullPointerException"),
                       null_str());
}

// Raise an ArrayStoreException.
void bjvm_array_store_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, STR("java/lang/ArrayStoreException"),
                       null_str());
}

// Raise an IncompatibleClassChangeError.
void bjvm_incompatible_class_change_error(bjvm_thread *thread,
                                          const bjvm_utf8 complaint) {
  bjvm_raise_exception(thread, STR("java/lang/IncompatibleClassChangeError"),
                       complaint);
}

// Raise an ArithmeticException.
void bjvm_arithmetic_exception(bjvm_thread *thread, const bjvm_utf8 complaint) {
  bjvm_raise_exception(thread, STR("java/lang/ArithmeticException"), complaint);
}

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
void bjvm_array_index_oob_exception(bjvm_thread *thread, int index,
                                    int length) {
  INIT_STACK_STRING(complaint, 80);
  bprintf(complaint, "Index %d out of bounds for array of length %d", index,
          length);
  bjvm_raise_exception(thread, STR("java/lang/ArrayIndexOutOfBoundsException"),
                       complaint);
}

void read_string(bjvm_thread *, bjvm_obj_header *obj, short **buf,
                 size_t *len) {
  assert(utf8_equals(hslc(obj->descriptor->name), "java/lang/String"));
  bjvm_obj_header *array = ((struct bjvm_native_String *)obj)->value;
  *buf = ArrayData(array);
  *len = *ArrayLength(array);
}

heap_string read_string_to_utf8(bjvm_obj_header *obj) {
  short *buf;
  size_t len;
  read_string(nullptr, obj, &buf, &len);
  char *cbuf = malloc(len + 1);
  for (size_t i = 0; i < len; ++i) {
    cbuf[i] = buf[i];
  }
  cbuf[len] = 0;
  return (heap_string){.chars = cbuf, .len = len};
}

#if 0
void read_string(bjvm_thread *thread, bjvm_obj_header *obj, short **buf,
                 size_t *len) {
  assert(utf8_equals(hslc(obj->descriptor->name), "java/lang/String"));

  auto method = bjvm_easy_method_lookup(obj->descriptor, str("getBytes"),
                                        str("()[B"), false, false);
  bjvm_stack_value result;
  bjvm_thread_run(thread, method, (bjvm_stack_value[]){}, &result);

  bjvm_obj_header *byte_array = result.obj;
  *buf = ArrayData(byte_array);
  *len = *ArrayLength(byte_array);
}
#endif

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

void primitive_type_kind_to_array_info(bjvm_type_kind kind,
                                       const bjvm_utf8 *type, int *size);

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc);

#if 0
bjvm_obj_header *make_string(bjvm_thread *thread, const bjvm_utf8 chars) {
  bjvm_obj_header *byte_array = make_byte_array(thread, chars.len);

  bjvm_classdesc *string_class =
      bootstrap_class_create(thread, str("java/lang/String"));
  bjvm_initialize_class(thread, string_class);
  bjvm_obj_header *result = new_object(thread, string_class);
  bjvm_stack_value value;
  value.obj = result;
  bjvm_stack_value args[] = {value, {.obj = byte_array}};
  bjvm_cp_method *method = bjvm_easy_method_lookup(string_class, str("<init>"),
                                                   str("([B)V"), false, false);
  bjvm_thread_run(thread, method, args, nullptr);

  return result;
}
#endif

// TODO restore implementation calling <init> when we can figure it out
bjvm_obj_header *make_string(bjvm_thread *thread, bjvm_utf8 string) {
  bjvm_classdesc *java_lang_String =
      bootstrap_class_create(thread, STR("java/lang/String"));
  bjvm_initialize_class(thread, java_lang_String);
  bjvm_handle *str =
      bjvm_make_handle(thread, new_object(thread, java_lang_String));

#define S ((struct bjvm_native_String *)str->obj)

  short *chars;
  int len;
  convert_modified_utf8_to_chars(string.chars, string.len, &chars, &len, true);
  S->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_CHAR, len);
  memcpy(ArrayData(S->value), chars, len * sizeof(short));
  free(chars);

  bjvm_obj_header *result = (void *)S;
  bjvm_drop_handle(thread, str);
  return result;
}

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread,
                                               bjvm_utf8 name) {
  const char *chars = name.chars;
  if (chars[0] == 'L') {
    name = slice_to(name, 1, name.len - 1);
    return bootstrap_class_create(thread, name);
  }
  if (chars[0] == '[')
    return bootstrap_class_create(thread, name);
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
    return bjvm_primitive_class_mirror(thread, chars[0])->reflected_class;
  default:
    UNREACHABLE();
  }
}

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread,
                                    const bjvm_utf8 chars) {
  bjvm_obj_header *str = bjvm_hash_table_lookup(&thread->vm->interned_strings,
                                                chars.chars, chars.len);
  if (str)
    return str;
  bjvm_obj_header *new_str = make_string(thread, chars);
  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, chars.chars,
                               chars.len, new_str);
  return new_str;
}

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj) {
#if AGGRESSIVE_DEBUG
  printf("Raising exception of type %s\n", obj->descriptor->name);
#endif

  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int bjvm_raise_exception(bjvm_thread *thread, const bjvm_utf8 exception_name,
                         const bjvm_utf8 exception_string) {
  bjvm_classdesc *classdesc = bootstrap_class_create(thread, exception_name);
  bjvm_initialize_class(thread, classdesc);

  thread->lang_exception_frame = (int)thread->frames_count - 1;

  // Create the exception object
  bjvm_obj_header *obj = new_object(thread, classdesc);
  if (exception_string.chars) {
    bjvm_obj_header *str = make_string(thread, exception_string);
    bjvm_cp_method *method = bjvm_method_lookup(
        classdesc, STR("<init>"), STR("(Ljava/lang/String;)V"), true, false);
    bjvm_thread_run(thread, method,
                    (bjvm_stack_value[]){{.obj = obj}, {.obj = str}}, nullptr);
  } else {
    bjvm_cp_method *method =
        bjvm_method_lookup(classdesc, STR("<init>"), STR("()V"), true, false);
    bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}},
                    nullptr);
  }

  thread->lang_exception_frame = -1;

#ifndef EMSCRIPTEN
  fprintf(stderr, "Exception: %.*s: %.*s\n", fmt_slice(exception_name),
          fmt_slice(exception_string));
#endif
  bjvm_raise_exception_object(thread, obj);
  return 0;
}

bjvm_classdesc *bjvm_primitive_classdesc(bjvm_thread *thread,
                                         bjvm_type_kind prim_kind) {
  bjvm_vm *vm = thread->vm;
  return vm->primitive_classes[primitive_order(prim_kind)];
}

struct bjvm_native_Class *
bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  return bjvm_primitive_classdesc(thread, prim_kind)->mirror;
}

bjvm_classdesc *bjvm_make_primitive_classdesc(bjvm_thread *thread,
                                              bjvm_type_kind kind,
                                              const bjvm_utf8 name) {
  bjvm_classdesc *desc = calloc(1, sizeof(bjvm_classdesc));

  desc->kind = BJVM_CD_KIND_PRIMITIVE;
  desc->super_class = nullptr;
  desc->name = make_heap_str_from(name);
  desc->access_flags =
      BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL | BJVM_ACCESS_ABSTRACT;
  desc->array_type = nullptr;
  desc->primitive_component = kind;
  desc->dtor = free_primitive_classdesc;

  return desc;
}

void bjvm_vm_init_primitive_classes(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (vm->primitive_classes[0])
    return; // already initialized

  vm->primitive_classes[primitive_order(BJVM_TYPE_KIND_BOOLEAN)] =
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_BOOLEAN,
                                    STR("boolean"));
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
      bjvm_make_primitive_classdesc(thread, BJVM_TYPE_KIND_DOUBLE,
                                    STR("double"));
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
  options.runtime_classpath = STR("./jre/lib/rt.jar");
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

#define OOM_SLOP_BYTES (1 << 12)

bjvm_vm *bjvm_create_vm(const bjvm_vm_options options) {
  bjvm_vm *vm = calloc(1, sizeof(bjvm_vm));

  INIT_STACK_STRING(classpath, 1000);
  classpath =
      bprintf(classpath, "%.*s:%.*s", fmt_slice(options.runtime_classpath),
              fmt_slice(options.classpath));

  char *error = bjvm_init_classpath(&vm->classpath, classpath);
  if (error) {
    fprintf(stderr, "Classpath error: %s", error);
    return nullptr;
  }

  vm->classes = bjvm_make_hash_table(_free_classdesc, 0.75, 16);
  vm->inchoate_classes = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->natives = bjvm_make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->class_padding = bjvm_make_hash_table(nullptr, 0.75, 16);
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
    bjvm_register_native(vm, natives[i].class_path, natives[i].method_name,
                         natives[i].method_descriptor, natives[i].callback);
  }

  bjvm_register_native_padding(vm);

  return vm;
}

void bjvm_free_vm(bjvm_vm *vm) {
  bjvm_free_hash_table(vm->classes);
  bjvm_free_hash_table(vm->natives);
  bjvm_free_hash_table(vm->inchoate_classes);
  bjvm_free_hash_table(vm->interned_strings);
  bjvm_free_hash_table(vm->class_padding);

  if (vm->primitive_classes[0]) {
    for (int i = 0; i < 9; ++i) {
      vm->primitive_classes[i]->dtor(vm->primitive_classes[i]);
    }
  }

  bjvm_free_classpath(&vm->classpath);

  free(vm->active_threads);
  free(vm->heap);
  free(vm);
}

bjvm_thread_options bjvm_default_thread_options() {
  bjvm_thread_options options = {};
  options.stack_space = 1 << 19;
  options.js_jit_enabled = true;
  options.thread_group = nullptr;
  return options;
}

bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc,
                                 bjvm_utf8 const name,
                                 bjvm_utf8 const descriptor) {
  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_cp_field *field = classdesc->fields + i;
    if (utf8_equals_utf8(field->name, name) &&
        utf8_equals_utf8(field->descriptor, descriptor)) {
      return field;
    }
  }

  // Then look on superinterfaces
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    bjvm_cp_field *result = bjvm_field_lookup(
        classdesc->interfaces[i]->classdesc, name, descriptor);
    if (result)
      return result;
  }

  if (classdesc->super_class) {
    return bjvm_field_lookup(classdesc->super_class->classdesc, name,
                             descriptor);
  }

  return nullptr;
}

bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const bjvm_utf8 name,
                                      const bjvm_utf8 descriptor) {
  bjvm_cp_field *result = bjvm_field_lookup(classdesc, name, descriptor);
  return result;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value) {
  store_stack_value((void *)obj + field->byte_offset, bjvm_stack_value,
                    field_to_kind(&field->parsed_descriptor));
}

bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field) {
  return load_stack_value((void *)obj + field->byte_offset,
                          field_to_kind(&field->parsed_descriptor));
}

bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options) {
  bjvm_thread *thr = calloc(1, sizeof(bjvm_thread));
  *VECTOR_PUSH(vm->active_threads, vm->active_thread_count,
               vm->active_thread_cap) = thr;

  thr->vm = vm;
  thr->frame_buffer =
      calloc(1, thr->frame_buffer_capacity = options.stack_space);
  thr->js_jit_enabled = options.js_jit_enabled;
  const int HANDLES_CAPACITY = 100;
  thr->handles = calloc(1, sizeof(bjvm_handle) * HANDLES_CAPACITY);
  thr->handles_capacity = HANDLES_CAPACITY;
  thr->lang_exception_frame = -1;

  bjvm_classdesc *desc;

  desc = bootstrap_class_create(thr, STR("java/lang/Class"));
  bjvm_initialize_class(thr, desc);
  bjvm_vm_init_primitive_classes(thr);

  // Pre-allocate OOM and stack overflow errors
  desc = bootstrap_class_create(thr, STR("java/lang/OutOfMemoryError"));
  bjvm_initialize_class(thr, desc);
  thr->out_of_mem_error = new_object(thr, desc);

  desc = bootstrap_class_create(thr, STR("java/lang/StackOverflowError"));
  bjvm_initialize_class(thr, desc);
  thr->stack_overflow_error = new_object(thr, desc);

  desc = bootstrap_class_create(thr, STR("java/lang/reflect/Field"));
  desc = bootstrap_class_create(thr, STR("java/lang/reflect/Constructor"));

  // Initialize java.lang.Thread mirror
  desc = bootstrap_class_create(thr, STR("java/lang/Thread"));
  bjvm_initialize_class(thr, desc);

  struct bjvm_native_Thread *java_thr = (void *)new_object(thr, desc);
  thr->thread_obj = java_thr;

  java_thr->vm_thread = thr;
  java_thr->priority = 5;
  java_thr->name = bjvm_intern_string(thr, STR("main"));

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_method_lookup(
      desc, STR("<init>"), STR("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"),
      false, false);
  bjvm_obj_header *name = make_string(thr, STR("main"));
  bjvm_thread_run(thr, make_thread,
                  (bjvm_stack_value[]){{.obj = (void *)java_thr},
                                       {.obj = main_thread_group},
                                       {.obj = name}},
                  nullptr);

  // Call System.initializeSystemClass()
  desc = bootstrap_class_create(thr, STR("java/lang/System"));
  bjvm_initialize_class(thr, desc);

  bjvm_cp_method *method = bjvm_method_lookup(
      desc, STR("initializeSystemClass"), STR("()V"), false, false);
  bjvm_stack_value ret;
  bjvm_thread_run(thr, method, nullptr, &ret);

  thr->current_exception = nullptr;

  // Call setJavaLangAccess() since we crash before getting there
  method = bjvm_method_lookup(desc, STR("setJavaLangAccess"), STR("()V"), false,
                              false);
  bjvm_thread_run(thr, method, nullptr, &ret);

  return thr;
}

void bjvm_free_thread(bjvm_thread *thread) {
  // TODO remove from the VM

  free(thread->frames);
  free(thread->frame_buffer);
  free(thread->handles);
  free(thread);
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

bjvm_classdesc *load_class_of_field(bjvm_thread *thread,
                                    const bjvm_field_descriptor *field) {
  INIT_STACK_STRING(name, 1000);
  name = bjvm_unparse_field_descriptor(name, field);
  bjvm_classdesc *result = load_class_of_field_descriptor(thread, name);
  return result;
}

struct bjvm_native_MethodType *
bjvm_resolve_method_type(bjvm_thread *thread, bjvm_method_descriptor *method) {
  // Resolve each class in the arguments list, as well as the return type if it
  // exists
  assert(method);
  bjvm_classdesc *MethodHandleNatives = bootstrap_class_create(
                     thread, STR("java/lang/invoke/MethodHandleNatives")),
                 *Class =
                     bootstrap_class_create(thread, STR("java/lang/Class")),
                 *MethodType = bootstrap_class_create(
                     thread, STR("java/lang/invoke/MethodType"));

  assert(MethodHandleNatives);
  bjvm_initialize_class(thread, MethodHandleNatives);

  bjvm_handle *ptypes = bjvm_make_handle(
      thread, CreateObjectArray1D(thread, Class, method->args_count));

  for (int i = 0; i < method->args_count; ++i) {
    INIT_STACK_STRING(name, 1000);
    name = bjvm_unparse_field_descriptor(name, method->args + i);
    bjvm_classdesc *arg_desc = load_class_of_field_descriptor(thread, name);

    if (!arg_desc)
      return nullptr;
    *((struct bjvm_native_Class **)ArrayData(ptypes->obj) + i) =
        bjvm_get_class_mirror(thread, arg_desc);
  }

  INIT_STACK_STRING(return_name, 1000);
  return_name =
      bjvm_unparse_field_descriptor(return_name, &method->return_type);
  bjvm_classdesc *ret_desc =
      load_class_of_field_descriptor(thread, return_name);
  if (!ret_desc)
    return nullptr;
  struct bjvm_native_Class *rtype = bjvm_get_class_mirror(thread, ret_desc);
  // Call <init>(Ljava/lang/Class;[Ljava/lang/Class;Z)V
  bjvm_cp_method *init =
      bjvm_method_lookup(MethodType, STR("makeImpl"),
                         STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                             "lang/invoke/MethodType;"),
                         false, false);
  bjvm_stack_value result;
  bjvm_thread_run(thread, init,
                  (bjvm_stack_value[]){{.obj = (void *)rtype},
                                       {.obj = ptypes->obj},
                                       {.i = 1 /* trusted */}},
                  &result);
  bjvm_drop_handle(thread, ptypes);
  return (void *)result.obj;
}

struct bjvm_native_MethodType *resolve_mh_mt(bjvm_thread *thread,
                                             bjvm_cp_method_handle_info *info) {
  bjvm_classdesc *rtype = nullptr;
  bjvm_classdesc **ptypes = nullptr;
  int ptypes_count = 0;
  int ptypes_capacity = 0;
  bool is_static = false;

  switch (info->handle_kind) {
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
  case BJVM_MH_KIND_INVOKE_STATIC:
    is_static = true;
    [[fallthrough]];
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    // MT should be of the form (C,A*)T, where C is the class the method is
    // found on, A* is the list of argument types, and T is the return type
    bjvm_cp_method_info *method = &info->reference->methodref;
    bjvm_resolve_class(thread, method->class_info);
    bjvm_classdesc *C = method->class_info->classdesc;
    bjvm_initialize_class(thread, C);

    if (!is_static) {
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) = C;
    }

    for (int i = 0; i < method->descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) =
          load_class_of_field(thread, arg);
    }

    rtype = load_class_of_field(thread, &method->descriptor->return_type);
    break;
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
    UNREACHABLE();
    break;
  }

  // Call MethodType.makeImpl(rtype, ptypes, true)
  bjvm_classdesc *MethodType =
      bootstrap_class_create(thread, STR("java/lang/invoke/MethodType"));
  bjvm_initialize_class(thread, MethodType);
  bjvm_cp_method *make =
      bjvm_method_lookup(MethodType, STR("makeImpl"),
                         STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                             "lang/invoke/MethodType;"),
                         false, false);
  bjvm_stack_value result;

  bjvm_classdesc *Class =
      bootstrap_class_create(thread, STR("java/lang/Class"));
  bjvm_handle *ptypes_array = bjvm_make_handle(
      thread, CreateObjectArray1D(thread, Class, ptypes_count));
  for (int i = 0; i < ptypes_count; ++i) {
    *((bjvm_obj_header **)ArrayData(ptypes_array->obj) + i) =
        (void *)bjvm_get_class_mirror(thread, ptypes[i]);
  }
  free(ptypes);
  bjvm_thread_run(thread, make,
                  (bjvm_stack_value[]){
                      {.obj = (void *)bjvm_get_class_mirror(thread, rtype)},
                      {.obj = ptypes_array->obj},
                      {.i = 1}},
                  &result);
  return (void *)result.obj;
}

struct bjvm_native_MethodHandle *
bjvm_resolve_method_handle(bjvm_thread *thread,
                           bjvm_cp_method_handle_info *info) {
  info->resolved_mt = resolve_mh_mt(thread, info);

  switch (info->handle_kind) {
  case BJVM_MH_KIND_GET_FIELD:
  case BJVM_MH_KIND_GET_STATIC:
  case BJVM_MH_KIND_PUT_FIELD:
  case BJVM_MH_KIND_PUT_STATIC:
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_VIRTUAL:
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    bjvm_cp_method_info *method = &info->reference->methodref;
    bjvm_resolve_class(thread, method->class_info);
    bjvm_initialize_class(thread, method->class_info->classdesc);
    bjvm_cp_method *m =
        bjvm_method_lookup(method->class_info->classdesc, method->nat->name,
                           method->nat->descriptor, true, true);
    bjvm_reflect_initialize_method(thread, method->class_info->classdesc, m);

    // Call DirectMethodHandle.make(method, true)
    bjvm_classdesc *DirectMethodHandle = bootstrap_class_create(
        thread, STR("java/lang/invoke/DirectMethodHandle"));
    bjvm_initialize_class(thread, DirectMethodHandle);
    bjvm_cp_method *make =
        bjvm_method_lookup(DirectMethodHandle, STR("make"),
                           STR("(Ljava/lang/reflect/Method;)Ljava/lang/"
                               "invoke/DirectMethodHandle;"),
                           false, false);
    bjvm_stack_value result;
    bjvm_thread_run(thread, make,
                    (bjvm_stack_value[]){{.obj = (void *)m->reflection_method}},
                    &result);
    return (void *)result.obj;
  }

  // "Third, a reference to an instance of java.lang.invoke.MethodType is
  // obtained as if by resolution of an unresolved symbolic reference to a
  // method type that contains the method descriptor specified in
  // Table 5.4.3.5-B for the kind of MH."
  return 0;
}

static void free_ordinary_classdesc(bjvm_classdesc *cd) {
  if (cd->array_type)
    cd->array_type->dtor(cd->array_type);
  bjvm_free_classfile(*cd);
  bjvm_free_function_tables(cd);
  free(cd);
}

void bjvm_class_circularity_error(bjvm_thread *thread,
                                  const bjvm_classdesc *class) {
  INIT_STACK_STRING(message, 1000);
  message =
      bprintf(message, "While loading class %.*s", fmt_slice(class->name));
  bjvm_raise_exception(thread, STR("java/lang/ClassCircularityError"), message);
}

bjvm_classdesc *bjvm_define_class(bjvm_thread *thread,
                                  bjvm_utf8 chars,
                                  const uint8_t *classfile_bytes,
                                  size_t classfile_len) {
  bjvm_vm *vm = thread->vm;
  bjvm_classdesc *class = calloc(1, sizeof(bjvm_classdesc));

  parse_result_t error = bjvm_parse_classfile(classfile_bytes, classfile_len, class);
  if (error != PARSE_SUCCESS) {
    // Raise VerifyError
    UNREACHABLE();
    goto error_1;
  }

  // 3. If C has a direct superclass, the symbolic reference from C to its
  // direct superclass is resolved using the algorithm of §5.4.3.1.
  bjvm_cp_class_info *super = class->super_class;
  if (super) {
    // If the superclass is currently being loaded -> circularity  error
    if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars,
                               super->name.len)) {
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
    if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars,
                               super->name.len)) {
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
  native_entries *entries =
      bjvm_hash_table_lookup(&vm->natives, chars.chars, chars.len);
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

  (void)bjvm_hash_table_insert(&vm->classes, chars.chars, chars.len, class);
  return class;

error_2:
  bjvm_free_classfile(*class);
error_1:
  free(class);
  return nullptr;
}

// name = "java/lang/Object" or "[[J" or "[Ljava/lang/String;"
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const bjvm_utf8 name) {
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
    (void)bjvm_hash_table_insert(&vm->inchoate_classes, chars.chars, chars.len,
                                 (void *)1);

    // e.g. "java/lang/Object.class"
    const bjvm_utf8 cf_ending = STR(".class");
    INIT_STACK_STRING(filename, MAX_CF_NAME_LENGTH + 6);
    memcpy(filename.chars, chars.chars, chars.len);
    memcpy(filename.chars + chars.len, cf_ending.chars, cf_ending.len);
    filename.len = chars.len + cf_ending.len;

    uint8_t *bytes;
    size_t cf_len;
    int read_status =
      bjvm_lookup_classpath(&vm->classpath, filename, &bytes, &cf_len);
    if (read_status) {
      // If the file is ClassNotFoundException, abort to avoid stack overflow
      if (utf8_equals(chars, "java/lang/ClassNotFoundException")) {
        printf("Could not find class %.*s\n", fmt_slice(chars));
        abort();
      }

      int i = 0;
      for (; i < chars.len; ++i)
        filename.chars[i] = filename.chars[i] == '/' ? '.' : filename.chars[i];
      // ClassNotFoundException: com.google.DontBeEvil
      bjvm_raise_exception(thread, STR("java/lang/ClassNotFoundException"),
                           filename);
      return nullptr;
    }

    class = bjvm_define_class(thread, chars, bytes, cf_len);
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
        printf("Error analyzing method %.*s: %.*s\n", method->name.len,
               method->name.chars, error_str.len, error_str.chars);
        // TODO raise VerifyError
        UNREACHABLE();
      }
    }
  }

  // Padding for VM fields (e.g., internal fields used for Reflection)
  int padding = (int)(uintptr_t)bjvm_hash_table_lookup(
      &thread->vm->class_padding, classdesc->name.chars, classdesc->name.len);

  // Assign memory locations to all static/non-static fields
  bjvm_classdesc *super =
      classdesc->super_class ? classdesc->super_class->classdesc : NULL;
  int static_offset = 0, nonstatic_offset = super ? super->instance_bytes
                                                  : sizeof(bjvm_obj_header);
  nonstatic_offset += padding;
  for (int field_i = 0; field_i < classdesc->fields_count; ++field_i) {
    bjvm_cp_field *field = classdesc->fields + field_i;
    bjvm_type_kind kind = field_to_kind(&field->parsed_descriptor);
    field->byte_offset = field->access_flags & BJVM_ACCESS_STATIC
                             ? allocate_field(&static_offset, kind)
                             : allocate_field(&nonstatic_offset, kind);

#if AGGRESSIVE_DEBUG
    printf("Allocating field %.*s for class %.*s at %d\n",
           fmt_slice(field->name), fmt_slice(classdesc->name),
           field->byte_offset);
#endif
  }

  bjvm_init_compressed_bitset(&classdesc->static_references,
                              static_offset / sizeof(void *));
  bjvm_init_compressed_bitset(&classdesc->instance_references,
                              nonstatic_offset / sizeof(void *));

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
      bjvm_compressed_bitset *bs = field->access_flags & BJVM_ACCESS_STATIC
                                       ? &classdesc->static_references
                                       : &classdesc->instance_references;
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
  printf("Allocating %zu bytes, %zu used, %zu capacity\n", bytes, vm->heap_used,
         vm->heap_capacity);
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
  return utf8_equals(hslc(d->name), "java/lang/Error") ||
         (d->super_class && is_error(d->super_class->classdesc));
}

bjvm_attribute *find_attribute(bjvm_attribute *attrs, int attrc,
                               bjvm_attribute_kind kind) {
  for (int i = 0; i < attrc; ++i)
    if (attrs[i].kind == kind)
      return attrs + i;
  return nullptr;
}

// During initialisation, we need to set the value of static final fields
// if they are provided in the class file.
//
// Returns true if an OOM occurred when initializing string fields.
bool initialize_constant_value_fields(bjvm_thread *thread,
                                      bjvm_classdesc *classdesc) {
  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_cp_field *field = classdesc->fields + i;
    if (field->access_flags & BJVM_ACCESS_STATIC &&
        field->access_flags & BJVM_ACCESS_FINAL) {
      bjvm_attribute *attr =
          find_attribute(field->attributes, field->attributes_count,
                         BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE);
      if (!attr)
        continue;
      void *p = classdesc->static_fields + field->byte_offset;
      bjvm_cp_entry *ent = attr->constant_value;
      switch (ent->kind) {
      case BJVM_CP_KIND_INTEGER:
        store_stack_value(p, (bjvm_stack_value){.i = (int)ent->integral.value},
                          BJVM_TYPE_KIND_INT);
        break;
      case BJVM_CP_KIND_FLOAT:
        store_stack_value(p,
                          (bjvm_stack_value){.f = (float)ent->floating.value},
                          BJVM_TYPE_KIND_FLOAT);
        break;
      case BJVM_CP_KIND_LONG:
        store_stack_value(p, (bjvm_stack_value){.l = ent->integral.value},
                          BJVM_TYPE_KIND_LONG);
        break;
      case BJVM_CP_KIND_DOUBLE:
        store_stack_value(p, (bjvm_stack_value){.d = ent->floating.value},
                          BJVM_TYPE_KIND_DOUBLE);
        break;
      case BJVM_CP_KIND_STRING:
        bjvm_obj_header *str = bjvm_intern_string(thread, ent->string.chars);
        if (!str)
          return true;
        store_stack_value(p, (bjvm_stack_value){.obj = str},
                          BJVM_TYPE_KIND_REFERENCE);
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
  bjvm_classdesc *EIIE = bootstrap_class_create(
      thread, STR("java/lang/ExceptionInInitializerError"));
  bjvm_initialize_class(thread, EIIE);
  bjvm_handle *eiie = bjvm_make_handle(thread, new_object(thread, EIIE));
  bjvm_cp_method *ctor = bjvm_method_lookup(
      EIIE, STR("<init>"), STR("(Ljava/lang/Throwable;)V"), false, false);
  thread->current_exception = nullptr; // clear exception
  int error = bjvm_thread_run(
      thread, ctor, (bjvm_stack_value[]){{.obj = eiie->obj}, {.obj = exc->obj}},
      nullptr);
  if (!error) {
    assert(eiie->obj);
    bjvm_raise_exception_object(thread, eiie->obj);
  }
  bjvm_drop_handle(thread, eiie);
  bjvm_drop_handle(thread, exc);
}

// Call <clinit> on the class, if it hasn't already been called.
bjvm_interpreter_result_t bjvm_initialize_class(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc) {
  assert(classdesc);
  if (classdesc->state >= BJVM_CD_STATE_INITIALIZING) {
    // Class is already initialized, or currently being initialized.
    // TODO In a multithreaded model, we would need to wait for the other
    // thread to finish initializing the class.
    return BJVM_INTERP_RESULT_OK;
  }

  if (classdesc->state != BJVM_CD_STATE_LINKED) {
    int error = bjvm_link_class(thread, classdesc);
    if (error) {
      assert(thread->current_exception);
      return BJVM_INTERP_RESULT_EXC;
    }
  }

  classdesc->state = BJVM_CD_STATE_INITIALIZING;
  int failed_to_init = 0;

  if (classdesc->super_class) {
    failed_to_init =
        bjvm_initialize_class(thread, classdesc->super_class->classdesc);
    if (failed_to_init)
      goto done;
  }

  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    failed_to_init =
        bjvm_initialize_class(thread, classdesc->interfaces[i]->classdesc);
    if (failed_to_init)
      goto done;
  }

  if (initialize_constant_value_fields(thread, classdesc)) {
    failed_to_init = -1;
    goto done;
  }

  bjvm_cp_method *clinit =
      bjvm_method_lookup(classdesc, STR("<clinit>"), STR("()V"), false, false);
  if (clinit) {
    failed_to_init = bjvm_thread_run(thread, clinit, nullptr, nullptr);
    if (failed_to_init && !is_error(thread->current_exception->descriptor)) {
      wrap_in_exception_in_initializer_error(thread);
    }
  }

done:
  classdesc->state =
      failed_to_init ? BJVM_CD_STATE_LINKAGE_ERROR : BJVM_CD_STATE_INITIALIZED;
  return failed_to_init;
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

bool method_candidate_matches(const bjvm_cp_method *candidate,
                              const bjvm_utf8 name,
                              const bjvm_utf8 method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor.chars ||
          utf8_equals_utf8(candidate->unparsed_descriptor, method_descriptor));
}

// TODO look at this more carefully
bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 name,
                                   const bjvm_utf8 method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces) {
  assert(descriptor->state >= BJVM_CD_STATE_LINKED);
  bjvm_classdesc *search = descriptor;
  // if the object is an array and we're looking for a superclass method, the
  // method must be on a superclass
  if (search->kind != BJVM_CD_KIND_ORDINARY && search_superclasses)
    search = search->super_class->classdesc;
  while (true) {
    for (int i = 0; i < search->methods_count; ++i)
      if (method_candidate_matches(search->methods + i, name,
                                   method_descriptor))
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
        bjvm_method_lookup(descriptor->interfaces[i]->classdesc, name,
                           method_descriptor, false, true);
    if (result)
      return result;
  }

  // Look in superinterfaces of superclasses
  if (search_superclasses && descriptor->super_class) {
    return bjvm_method_lookup(descriptor->super_class->classdesc, name,
                              method_descriptor, true, true);
  }

  return nullptr;
}

bjvm_async_run_ctx *bjvm_thread_async_run(bjvm_thread *thread,
                                          bjvm_cp_method *method,
                                          bjvm_stack_value *args,
                                          bjvm_stack_value *result) {
  assert(method && "Method is null");
  bjvm_async_run_ctx *ctx = malloc(sizeof(bjvm_async_run_ctx));

  int nonstatic = !(method->access_flags & BJVM_ACCESS_STATIC);
  int argc = method->descriptor->args_count + nonstatic;

  ctx->thread = thread;
  ctx->frame = bjvm_push_frame(thread, method, args, argc);
  ctx->result = result;
  ctx->status = BJVM_INTERP_RESULT_EXC;
  if (!ctx->frame) // failed to allocate a frame
    return ctx;
  ctx->status = BJVM_INTERP_RESULT_INT;
  return ctx;
}

bool bjvm_async_run_step(bjvm_async_run_ctx *ctx) {
  if (ctx->status < BJVM_INTERP_RESULT_INT) {
    return true; // done
  }
  ctx->status = bjvm_interpret(ctx->thread, ctx->frame, ctx->result);
  return ctx->status < BJVM_INTERP_RESULT_INT;
}

void bjvm_free_async_run_ctx(bjvm_async_run_ctx *ctx) { free(ctx); }

int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                    bjvm_stack_value *args, bjvm_stack_value *result) {
  bjvm_async_run_ctx *ctx = bjvm_thread_async_run(thread, method, args, result);
  int ret;
  while (!bjvm_async_run_step(ctx)) {
    if (ctx->status == BJVM_INTERP_RESULT_MANDATORY_INT) {
      bjvm_raise_exception(thread, STR("java/lang/InternalError"),
                           STR("Attempted to execute an async function while "
                               "in bjvm_thread_run"));
      ret = -1;
      goto done;
    }
  }
  assert(ctx->status < BJVM_INTERP_RESULT_INT);
  ret = -(ctx->status == BJVM_INTERP_RESULT_EXC);

done:
  bjvm_free_async_run_ctx(ctx);
  return ret;
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
  info->classdesc = bootstrap_class_create(thread, info->name);
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
  bjvm_cp_field *field = bjvm_field_lookup(class->classdesc, info->nat->name,
                                           info->nat->descriptor);
  info->field = field;
  return field == nullptr;
}

void store_stack_value(void *field_location, bjvm_stack_value value,
                       bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    *(int8_t *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    *(int16_t *)field_location = value.i;
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

bool bjvm_is_instanceof_name(const bjvm_obj_header *mirror,
                             const bjvm_utf8 name) {
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

struct bjvm_native_ConstantPool *
bjvm_get_constant_pool_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (!classdesc)
    return nullptr;
  if (classdesc->cp_mirror)
    return classdesc->cp_mirror;
  bjvm_classdesc *java_lang_ConstantPool =
      bootstrap_class_create(thread, STR("sun/reflect/ConstantPool"));
  bjvm_initialize_class(thread, java_lang_ConstantPool);
  struct bjvm_native_ConstantPool *cp_mirror = classdesc->cp_mirror =
      (void *)new_object(thread, java_lang_ConstantPool);
  cp_mirror->reflected_class = classdesc;
  return cp_mirror;
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc) {
  if (!classdesc)
    return nullptr;
  if (classdesc->mirror)
    return classdesc->mirror;

  bjvm_classdesc *java_lang_Class =
      bootstrap_class_create(thread, STR("java/lang/Class"));
  struct bjvm_native_Class *class_mirror = classdesc->mirror =
      (void *)new_object(thread, java_lang_Class);
  if (class_mirror)
    class_mirror->reflected_class = classdesc;

  return class_mirror;
}

bool bjvm_instanceof_interface(const bjvm_classdesc *o,
                               const bjvm_classdesc *classdesc) {
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
      return target->dimensions == o->dimensions &&
             o->primitive_component == target->primitive_component &&
             (!o->base_component || !target->base_component ||
              bjvm_instanceof(o->base_component, target->base_component));
    }
    // o is 1D primitive array, equality check suffices
    return target->dimensions == o->dimensions &&
           target->primitive_component == o->primitive_component;
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

bool method_types_compatible(struct bjvm_native_MethodType *provider_mt,
                             struct bjvm_native_MethodType *targ) {
  // Compare ptypes
  if (provider_mt == targ)
    return true;
  if (*ArrayLength(provider_mt->ptypes) != *ArrayLength(targ->ptypes)) {
    printf("Different lengths!\n");
    return false;
  }
  for (int i = 0; i < *ArrayLength(provider_mt->ptypes); ++i) {
    bjvm_classdesc *left = bjvm_unmirror_class(
        ((bjvm_obj_header **)ArrayData(provider_mt->ptypes))[i]);
    bjvm_classdesc *right =
        bjvm_unmirror_class(((bjvm_obj_header **)ArrayData(targ->ptypes))[i]);

    if (left != right) {
      printf("Different ptypes!\n");
      return false;
    }
  }
  return true;
}

heap_string debug_dump_string(bjvm_thread *thread, bjvm_obj_header *header) {
  bjvm_cp_method *toString =
      bjvm_method_lookup(header->descriptor, STR("toString"),
                         STR("()Ljava/lang/String;"), true, true);
  bjvm_stack_value result;
  bjvm_thread_run(thread, toString, (bjvm_stack_value[]){{.obj = header}},
                  &result);
  return read_string_to_utf8(result.obj);
}

void bjvm_wrong_method_type_error(bjvm_thread *thread,
                                  struct bjvm_native_MethodType *provider_mt,
                                  struct bjvm_native_MethodType *targ) {
  UNREACHABLE(); // TODO
}

#define checked_pop(frame)                                                     \
  ({                                                                           \
    assert(sd > 0);                                                            \
    frame->values[--sd];                                                       \
  })

#define checked_push(frame, value)                                             \
  {                                                                            \
    bjvm_stack_value _value = value;                                           \
    assert(sd < frame->max_stack);                                             \
    frame->values[sd++] = _value;                                              \
  }

enum {
  INVOKE_STATE_ENTRY = 0,
  // The invoked frame was already created and the result has been written
  INVOKE_STATE_MADE_FRAME = 2,
};

bjvm_interpreter_result_t bjvm_invokevirtual_signature_polymorphic(
    bjvm_thread *thread, bjvm_plain_frame *frame, int *sd,
    bjvm_cp_method *method, struct bjvm_native_MethodType *provider_mt,
    bjvm_obj_header *target) {
  struct bjvm_native_MethodHandle *mh = (void *)target;
  struct bjvm_native_MethodType *targ = (void *)mh->type;

  // varargs iff mh is of class AsVarargsCollector
  bool is_varargs =
      utf8_equals(hslc(mh->base.descriptor->name),
                  "java/lang/invoke/MethodHandleImpl$AsVarargsCollector");
  if (is_varargs) {
    // To-implement
    UNREACHABLE();
  }

  bool mts_are_same = method_types_compatible(provider_mt, targ);
  bool is_invoke_exact = utf8_equals_utf8(method->name, STR("invokeExact"));
  bool is_invoke_basic = utf8_equals_utf8(method->name, STR("invokeBasic"));

  if (is_invoke_exact) {
    if (!mts_are_same) {
      bjvm_wrong_method_type_error(thread, provider_mt, targ);
      return BJVM_INTERP_RESULT_EXC;
    }
  }

  if (!mts_are_same && !is_invoke_basic) {
    // Call asType to get an adapter handle
    bjvm_cp_method *asType = bjvm_method_lookup(
        mh->base.descriptor, STR("asType"),
        STR("(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;"),
        true, false);
    if (!asType)
      UNREACHABLE();

    bjvm_stack_value result = value_null();
    int status = bjvm_thread_run(
        thread, asType,
        (bjvm_stack_value[]){{.obj = (void *)mh}, {.obj = (void *)provider_mt}},
        &result);
    if (status != BJVM_INTERP_RESULT_OK) // asType failed
      return status;
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
    bjvm_cp_method *method = name->vmtarget;
    assert(method);
    bjvm_stack_value result;
    if (frame->state < INVOKE_STATE_MADE_FRAME) {
      bjvm_stack_frame *new_frame = bjvm_push_frame(
          thread, method, frame->values + *sd - method->descriptor->args_count,
          method->descriptor->args_count);
      bjvm_interpreter_result_t status =
          bjvm_interpret(thread, new_frame, &result);
      if (status != BJVM_INTERP_RESULT_OK) {
        if (status == BJVM_INTERP_RESULT_INT)
          frame->state = INVOKE_STATE_MADE_FRAME;
        return status;
      }
    }
    frame->state = INVOKE_STATE_ENTRY;
    if (method->descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID) {
      frame->values[*sd - method->descriptor->args_count] = result;
      (*sd)++;
    }
    *sd -= method->descriptor->args_count;
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
  return BJVM_INTERP_RESULT_OK;
}

static bjvm_interpreter_result_t resolve_methodref(bjvm_thread *thread,
                                                   bjvm_cp_method_info *info) {
  if (info->resolved) {
    return BJVM_INTERP_RESULT_OK;
  }

  bjvm_cp_class_info *class = info->class_info;
  int status = bjvm_resolve_class(thread, class);
  if (status) {
    // Failed to resolve the class in question
    return BJVM_INTERP_RESULT_EXC;
  }

  status = bjvm_initialize_class(thread, class->classdesc);
  if (status) {
    // <clinit> might get interrupted, that's ok -- steps until here are
    // idempotent
    return status;
  }

  info->resolved = bjvm_method_lookup(class->classdesc, info->nat->name,
                                      info->nat->descriptor, true, true);
  if (!info->resolved) {
    INIT_STACK_STRING(complaint, 1000);
    complaint =
        bprintf(complaint,
                "Could not find method %.*s with descriptor %.*s on class %.*s",
                fmt_slice(info->nat->name), fmt_slice(info->nat->descriptor),
                fmt_slice(class->name));
    bjvm_incompatible_class_change_error(thread, complaint);
    return -1;
  }

  return BJVM_INTERP_RESULT_OK;
}

int bjvm_multianewarray(bjvm_thread *thread, bjvm_plain_frame *frame,
                        struct bjvm_multianewarray_data *multianewarray,
                        int *sd) {
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

  bjvm_obj_header *result =
      CreateArray(thread, multianewarray->entry->classdesc, dim_sizes, dims);
  frame->values[*sd - dims] = (bjvm_stack_value){.obj = result};
  *sd -= dims - 1;
  return 0;
}

bjvm_value bjvm_resolve_indy_static_argument(bjvm_thread *thread,
                                             bjvm_cp_entry *ent,
                                             bool *is_object) {
  *is_object = false;
  switch (ent->kind) {
  case BJVM_CP_KIND_INTEGER:
    return (bjvm_value){.i = ent->integral.value};
  case BJVM_CP_KIND_FLOAT:
    return (bjvm_value){.f = ent->floating.value};
  case BJVM_CP_KIND_LONG:
    return (bjvm_value){.l = ent->integral.value};
  case BJVM_CP_KIND_DOUBLE:
    return (bjvm_value){.d = ent->floating.value};
  case BJVM_CP_KIND_STRING:
    *is_object = true;
    bjvm_obj_header *string = bjvm_intern_string(thread, ent->string.chars);
    return (bjvm_value){.handle = bjvm_make_handle(thread, string)};
  case BJVM_CP_KIND_CLASS:
    *is_object = true;
    bjvm_resolve_class(thread, &ent->class_info);
    return (bjvm_value){.handle = bjvm_make_handle(
                            thread, (void *)bjvm_get_class_mirror(
                                        thread, ent->class_info.classdesc))};
  case BJVM_CP_KIND_METHOD_TYPE:
    *is_object = true;
    if (!ent->method_type.resolved_mt) {
      ent->method_type.resolved_mt =
          bjvm_resolve_method_type(thread, ent->method_type.parsed_descriptor);
    }
    return (bjvm_value){.handle = bjvm_make_handle(
                            thread, (void *)ent->method_type.resolved_mt)};
  case BJVM_CP_KIND_METHOD_HANDLE:
    *is_object = true;
    void *result = bjvm_resolve_method_handle(thread, &ent->method_handle);
    return (bjvm_value){.handle = bjvm_make_handle(thread, result)};
  default: {
    UNREACHABLE();
  }
  }
}

int indy_resolve(bjvm_thread *thread, bjvm_bytecode_insn *insn,
                 bjvm_cp_indy_info *indy) {
  int result = 0;
  bjvm_bootstrap_method *m = indy->method;

  // e.g. LambdaMetafactory.metafactory
  bjvm_handle *bootstrap_handle = bjvm_make_handle(
      thread, (void *)bjvm_resolve_method_handle(thread, m->ref));

  bjvm_stack_value lookup_obj;
  // MethodHandles class
  bjvm_classdesc *lookup_class =
      bootstrap_class_create(thread, STR("java/lang/invoke/MethodHandles"));
  bjvm_initialize_class(thread, lookup_class);
  bjvm_cp_method *lookup_factory = bjvm_method_lookup(
      lookup_class, STR("lookup"),
      STR("()Ljava/lang/invoke/MethodHandles$Lookup;"), true, false);

  bjvm_thread_run(thread, lookup_factory, (bjvm_stack_value[]){}, &lookup_obj);
  bjvm_handle *lookup_handle = bjvm_make_handle(thread, lookup_obj.obj);

  bjvm_plain_frame *fake_frame =
      calloc(1, sizeof(bjvm_plain_frame) +
                    sizeof(bjvm_stack_value) * (m->args_count + 4));
  fake_frame->max_stack = fake_frame->values_count = m->args_count + 4;

  bjvm_value handles[256];
  bool is_handle[256];
  int static_i = 0;
  for (; static_i < m->args_count; ++static_i) {
    bjvm_cp_entry *arg = m->args[static_i];
    handles[static_i] =
        bjvm_resolve_indy_static_argument(thread, arg, &is_handle[static_i]);
  }

  bjvm_handle *name = bjvm_make_handle(
      thread, bjvm_intern_string(thread, indy->name_and_type->name));
  indy->resolved_mt = bjvm_resolve_method_type(thread, indy->method_descriptor);

  fake_frame->values[0] = (bjvm_stack_value){.obj = bootstrap_handle->obj};
  fake_frame->values[1] = (bjvm_stack_value){.obj = lookup_handle->obj};
  fake_frame->values[2] = (bjvm_stack_value){.obj = name->obj};
  fake_frame->values[3] = (bjvm_stack_value){.obj = (void *)indy->resolved_mt};

  for (int i = 0; i < static_i; ++i) {
    if (is_handle[i]) {
      fake_frame->values[i + 4] =
          (bjvm_stack_value){.obj = handles[i].handle->obj};
      bjvm_drop_handle(thread, handles[i].handle);
    } else {
      memcpy(fake_frame->values + i + 4, handles + i, sizeof(bjvm_stack_value));
    }
  }

  // Invoke the bootstrap method using invokeExact
  bjvm_cp_method *invokeExact = bjvm_method_lookup(
      bootstrap_handle->obj->descriptor, STR("invokeExact"),
      STR("(Ljava/lang/invoke/MethodHandle;[Ljava/lang/Object;)Ljava/lang/"
          "Object;"),
      true, false);

  int sd = static_i + 4;
  bjvm_invokevirtual_signature_polymorphic(thread, fake_frame, &sd, invokeExact,
                                           m->ref->resolved_mt,
                                           (void *)bootstrap_handle->obj);

  if (thread->current_exception) {
    result = -1;
    goto fail;
  }
  insn->ic = fake_frame->values[0].obj;

fail:
  free(fake_frame);

  bjvm_drop_handle(thread, bootstrap_handle);
  bjvm_drop_handle(thread, lookup_handle);
  bjvm_drop_handle(thread, name);

  return result;
}

int max_calls = 4251;

EMSCRIPTEN_KEEPALIVE
int set_max_calls(int calls) {
  max_calls = calls;
  return 0;
}

int bjvm_run_as_wasm(bjvm_thread *thread, bjvm_plain_frame *final_frame,
                     bjvm_stack_value *result,
                     bjvm_interpreter_result_t *interp_result) {
  bjvm_cp_method *m = final_frame->method;
  if (!m->compiled_method) {
    m->compiled_method = bjvm_wasm_jit_compile(thread, m, max_calls == 0);
    if (!m->compiled_method ||
        ((bjvm_wasm_instantiation_result *)m->compiled_method)->status !=
            BJVM_WASM_INSTANTIATION_SUCCESS) {
      m->failed_jit = true;
      return 1;
    }
  }

  // printf("%d", max_calls);

  if (max_calls-- < 0) {
    // printf("Skipping call to method: %.*s\n", fmt_slice(m->name));
    return 1;
  }

  int (*run)(bjvm_thread *, bjvm_plain_frame *, bjvm_stack_value *) =
      ((bjvm_wasm_instantiation_result *)m->compiled_method)->run;
  *interp_result = run(thread, final_frame, result);
  if (*interp_result != BJVM_INTERP_RESULT_INT) {
    bjvm_pop_frame(thread, (void *)final_frame);
  }
  return 0;
}

int should_attempt_to_jit(bjvm_cp_method *method) {
  return method->call_count > 50 && method->code->insn_count > 10 &&
         !method->failed_jit; // && utf8_equals(method->name, "testOperation");
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

bjvm_interpreter_result_t bjvm_run_native(bjvm_thread *thread,
                                          bjvm_native_frame *frame,
                                          bjvm_stack_value *result) {
  assert(frame && "frame is null");

  bjvm_native_callback *handle = frame->method->native_handle;
  bool is_static = frame->method->access_flags & BJVM_ACCESS_STATIC;
  bjvm_handle *target_handle = is_static ? nullptr : frame->values[0].handle;
  bjvm_value *native_args = frame->values + (is_static ? 0 : 1);

  if (!handle->is_async) {
    *result = ((bjvm_sync_native_callback)handle->ptr)(
        thread, target_handle, native_args, frame->values_count - !is_static);
    return thread->current_exception ? BJVM_INTERP_RESULT_EXC
                                     : BJVM_INTERP_RESULT_OK;
  }

  bjvm_interpreter_result_t status = ((bjvm_async_native_callback)handle->ptr)(
      thread, target_handle, native_args, frame->values_count - is_static,
      result, &frame->async_native_data);
  return status;
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

// Main interpreter
bjvm_interpreter_result_t bjvm_interpret(bjvm_thread *thread,
                                         bjvm_stack_frame *final_frame,
                                         bjvm_stack_value *result) {

#if ENABLE_JIT
  if (final_frame == thread->frames[thread->frames_count - 1] &&
      final_frame->program_counter == 0 &&
      should_attempt_to_jit(final_frame->method)) {
    bjvm_interpreter_result_t interp_result;
#if AGGRESSIVE_DEBUG
    printf("Running method %.*s with signature %.*s on %.*s as WASM\n",
           fmt_slice(final_frame->method->name),
           fmt_slice(final_frame->method->descriptor),
           fmt_slice(final_frame->method->my_class->name));
    dump_frame(stdout, final_frame);
#endif
    int failed_to_compile =
        bjvm_run_as_wasm(thread, final_frame, result, &interp_result);
    if (!failed_to_compile) {
      return interp_result;
    }
#if AGGRESSIVE_DEBUG
    printf("Continuing...\n");
#endif
  }
#endif

  bjvm_interpreter_result_t status = BJVM_INTERP_RESULT_OK;
  bjvm_cp_method *method = nullptr;

  // Go to the top-most frame on the execution stack and set things up.
get_top_frame:
  if (thread->frames_count == 0)
    return status;

  bjvm_stack_frame *raw_frame = *(thread->frames + thread->frames_count - 1);
  if (bjvm_is_frame_native(raw_frame)) {
    status = bjvm_run_native(thread, bjvm_get_native_frame(raw_frame), result);
    if (status != BJVM_INTERP_RESULT_INT) {
      goto done;
    }
    return status;
  }

  bjvm_plain_frame *frame = bjvm_get_plain_frame(raw_frame);
  assert(frame && "frame is null");

  // Current stack depth
  int sd = stack_depth(frame);

  method = frame->method;
  method->call_count++;
  bjvm_bytecode_insn *code = method->code->code;

  if (thread->current_exception)
    goto done;

#if AGGRESSIVE_DEBUG
  printf("Calling method %.*s, descriptor %.*s, on class %.*s\n",
         fmt_slice(method->name), fmt_slice(method->unparsed_descriptor),
         fmt_slice(method->my_class->name));
#endif



  // Interpret the current frame.
interpret_frame:
  while (true) {
    bjvm_bytecode_insn *insn = &code[frame->program_counter];
    assert(sd == stack_depth(frame));

#if AGGRESSIVE_DEBUG
    heap_string insn_dump = insn_to_string(insn, frame->program_counter);
    printf("Insn: %.*s\n", fmt_slice(insn_dump));
    printf("Method: %.*s in class %.*s (%.*s:%d)\n", fmt_slice(method->name),
           fmt_slice(method->my_class->name),
           fmt_slice(method->my_class->source_file
                         ? method->my_class->source_file->name
                         : null_str()),
           bjvm_get_line_number(method->code, frame->program_counter));
    printf("FRAME:\n");
    dump_frame(stdout, frame);

    free_heap_str(insn_dump);
#endif

    static const void *insn_jump_table[] = {
        &&bjvm_insn_nop,
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
#define NEXT_INSN                                                              \
  {                                                                            \
    insn = &code[++frame->program_counter];                                    \
    goto *insn_jump_table[insn->kind];                                         \
  }
#define JMP_INSN                                                               \
  {                                                                            \
    insn = &code[frame->program_counter];                                      \
    goto *insn_jump_table[insn->kind];                                         \
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
      if (value && !bjvm_instanceof(value->descriptor,
                                    array->descriptor->one_fewer_dim)) {
        bjvm_array_store_exception(thread);
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
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int8_t *)ArrayData(array) + index)});
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
      checked_push(frame, (bjvm_stack_value){
                              .i = *((uint16_t *)ArrayData(array) + index)});
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
      checked_push(
          frame, (bjvm_stack_value){.i = double_to_int(checked_pop(frame).d)});
      NEXT_INSN;
    }
    bjvm_insn_d2l: {
      checked_push(
          frame, (bjvm_stack_value){.l = double_to_long(checked_pop(frame).d)});
      NEXT_INSN;
    }
    bjvm_insn_dadd: {
      checked_push(frame, (bjvm_stack_value){.d = checked_pop(frame).d +
                                                  checked_pop(frame).d});
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
      checked_push(frame,
                   (bjvm_stack_value){.d = (double)checked_pop(frame).f});
      NEXT_INSN;
    }
    bjvm_insn_f2i: {
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.i = double_to_int(a)});
      NEXT_INSN;
    }
    bjvm_insn_f2l:
      checked_push(
          frame, (bjvm_stack_value){.l = double_to_long(checked_pop(frame).f)});
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
      checked_push(frame,
                   (bjvm_stack_value){.i = (int8_t)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_i2c: {
      checked_push(frame,
                   (bjvm_stack_value){.i = checked_pop(frame).i & 0xffff});
      NEXT_INSN;
    }
    bjvm_insn_i2d: {
      checked_push(frame,
                   (bjvm_stack_value){.d = (double)checked_pop(frame).i});
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
      checked_push(frame,
                   (bjvm_stack_value){.i = (int16_t)checked_pop(frame).i});
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
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int32_t *)ArrayData(array) + index)});
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
      bjvm_stack_value r = checked_pop(frame);
      if (thread->frames_count > 1) {
        bjvm_stack_frame *prev_frame = thread->frames[thread->frames_count - 2];
        *bjvm_get_frame_result_of_next(prev_frame) = r;
      }
      if (result) {
        *result = r;
      }
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
      checked_push(frame,
                   (bjvm_stack_value){.d = (double)checked_pop(frame).l});
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
      checked_push(frame, (bjvm_stack_value){
                              .l = *((int64_t *)ArrayData(array) + index)});
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
      checked_push(frame,
                   (bjvm_stack_value){.l = a >> (b & 0x3f)}); // fuck u UB
      NEXT_INSN;
    }
    bjvm_insn_lsub: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a - b});
      NEXT_INSN;
    }
    bjvm_insn_lushr: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame,
                   (bjvm_stack_value){.l = a >> (b & 0x3f)}); // fuck u UB
      NEXT_INSN;
    }
    bjvm_insn_lxor: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a ^ b});
      NEXT_INSN;
    }
    bjvm_insn_monitorenter:
    bjvm_insn_monitorexit: {
      // TODO
      checked_pop(frame);
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
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int16_t *)ArrayData(array) + index)});
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
      bjvm_obj_header *array =
          CreateObjectArray1D(thread, insn->classdesc, count);
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
        complaint = bprintf(complaint, "Expected instance of %.*s, got %.*s",
                            fmt_slice(insn->classdesc->name),
                            fmt_slice(obj->descriptor->name));
        bjvm_raise_exception(thread, STR("java/lang/ClassCastException"),
                             complaint);
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
      checked_push(
          frame, (bjvm_stack_value){.i = obj ? bjvm_instanceof(obj->descriptor,
                                                               insn->classdesc)
                                             : 0});
      NEXT_INSN;
    }
    bjvm_insn_invokedynamic: {
      bjvm_cp_indy_info *indy = &insn->cp->indy_info;
      if (indy_resolve(thread, insn, indy)) {
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
      switch (kind) {
      case BJVM_MH_KIND_INVOKE_STATIC: {
        bool returns = form->result != -1;

        // Invoke name->vmtarget with arguments mh, args
        bjvm_cp_method *invoke = name->vmtarget;
        bjvm_stack_value *args = alloca(insn->args * sizeof(bjvm_stack_value));
        args[0] = (bjvm_stack_value){.obj = (void *)mh}; // MethodHandle
        memcpy(args + 1, frame->values + sd - insn->args + 1,
               (insn->args - 1) * sizeof(bjvm_stack_value));
        bjvm_stack_frame *invoked =
            bjvm_push_frame(thread, invoke, args, insn->args);
        if (!invoked) {
          goto done;
        }
        status = bjvm_interpret(thread, invoked,
                                returns ? &frame->values[sd - insn->args + 1]
                                        : nullptr);
        if (status != BJVM_INTERP_RESULT_OK) {
          goto done;
        }
        sd -= insn->args - 1;
        sd += returns;
        break;
      }
      default:
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_new: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      error = bjvm_initialize_class(thread, info->classdesc);
      if (error)
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
        bprintf(complaint, "Expected nonstatic field %.*s on class %.*s",
                fmt_slice(field_info->nat->name),
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

      error = bjvm_initialize_class(thread, class->classdesc);
      if (error)
        goto done;
      bjvm_cp_field *field = bjvm_field_lookup(
          class->classdesc, field_info->nat->name, field_info->nat->descriptor);
      field_info->field = field;
      if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
        INIT_STACK_STRING(complaint, 1000);
        bprintf(complaint, "Expected static field %.*s on class %.*s",
                fmt_slice(field_info->nat->name),
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
      status = resolve_methodref(thread, method_info);
      if (status != BJVM_INTERP_RESULT_OK)
        goto done;

      bjvm_classdesc *lookup_on = method_info->resolved->my_class;

      // "If all of the following are true, let C [the class to look up the
      // method upon] be the direct superclass of the current class:
      //
      // The resolved method is not an instance initialization method;
      // If the symbolic reference names a class (not an interface), then that
      // class is a superclass of the current class."
      if (!utf8_equals(method_info->resolved->name, "<init>") &&
          (lookup_on->access_flags & BJVM_ACCESS_INTERFACE ||
           (method->my_class != lookup_on &&
            bjvm_instanceof(method->my_class, lookup_on)))) {
        lookup_on = method->my_class->super_class->classdesc;
      }

      // Look at the class and its superclasses first
      bjvm_cp_method *candidate = bjvm_method_lookup(
          lookup_on, method_info->resolved->name,
          method_info->resolved->unparsed_descriptor, true, false);
      if (!candidate) {
        // Then perform an itable lookup (I rly don't think this is correct...)
        if (method_info->resolved->my_class->access_flags &
            BJVM_ACCESS_INTERFACE) {
          candidate =
              bjvm_itable_lookup(lookup_on, method_info->resolved->my_class,
                                 method_info->resolved->itable_index);
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
      status = resolve_methodref(thread, method_info);
      if (status != BJVM_INTERP_RESULT_OK)
        goto done;
      if (!(method_info->resolved->my_class->access_flags &
            BJVM_ACCESS_INTERFACE)) {
        insn->kind = bjvm_insn_invokevirtual;
        JMP_INSN;
      }
      insn->kind = bjvm_insn_invokeitable_monomorphic;
      insn->ic = bjvm_itable_lookup(target->descriptor,
                                    method_info->resolved->my_class,
                                    method_info->resolved->itable_index);
      insn->ic2 = target->descriptor;
      if (!insn->ic) {
        bjvm_abstract_method_error(thread, method_info->resolved);
        goto done;
      }
      JMP_INSN;
    }
    bjvm_insn_invokevirtual: {
      bjvm_cp_method_info *method_info = &insn->cp->methodref;
      int argc = insn->args = method_info->descriptor->args_count + 1;
      assert(argc <= sd);
      bjvm_obj_header *target = frame->values[sd - argc].obj;
      if (!target) {
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      status = resolve_methodref(thread, method_info);
      if (status != BJVM_INTERP_RESULT_OK)
        goto done;
      if (method_info->resolved->is_signature_polymorphic) {
        status = bjvm_invokevirtual_signature_polymorphic(
            thread, frame, &sd, method,
            bjvm_resolve_method_type(thread, method_info->descriptor), target);
        if (status != BJVM_INTERP_RESULT_OK)
          goto done;
        NEXT_INSN;
      }

      // If we found an interface method, transmogrify into a invokeinterface
      if (method_info->resolved->my_class->access_flags &
          BJVM_ACCESS_INTERFACE) {
        insn->kind = bjvm_insn_invokeinterface;
        JMP_INSN;
      }

      insn->kind = bjvm_insn_invokevtable_monomorphic;
      insn->ic = bjvm_vtable_lookup(target->descriptor,
                                    method_info->resolved->vtable_index);
      assert(method);
      insn->ic2 = target->descriptor;
      JMP_INSN;
    }
    bjvm_insn_invokestatic: {
      bjvm_cp_method_info *info = &insn->cp->methodref;
      bjvm_interpreter_result_t status = resolve_methodref(thread, info);
      if (status != BJVM_INTERP_RESULT_OK) {
        return status;
      }
      insn->kind = bjvm_insn_invokestatic_resolved;
      insn->ic = info->resolved;
      insn->args = info->descriptor->args_count;
      JMP_INSN;
    }
    bjvm_insn_invokestatic_resolved: {
      bjvm_cp_method *method = insn->ic;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind !=
                     BJVM_TYPE_KIND_VOID;
      if (frame->state < INVOKE_STATE_MADE_FRAME) {
        bjvm_stack_frame *invoked_frame;
        if (method->is_signature_polymorphic) {
          invoked_frame = bjvm_push_native_frame(
              thread, method, insn->cp->methodref.descriptor,
              frame->values + sd - insn->args, insn->args);
        } else {
          invoked_frame = bjvm_push_frame(
              thread, method, frame->values + sd - insn->args, insn->args);
        }
        status = bjvm_interpret(thread, invoked_frame,
                                &frame->values[sd - insn->args]);
        if (status != BJVM_INTERP_RESULT_OK) {
          if (status == BJVM_INTERP_RESULT_INT)
            frame->state = INVOKE_STATE_MADE_FRAME;
          goto done;
        }
      } else {
        if (returns) {
          frame->values[sd - insn->args] = frame->result_of_next;
        }
      }

      frame->state = INVOKE_STATE_ENTRY;
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
        bjvm_obj_header *obj =
            (void *)bjvm_get_class_mirror(thread, ent->class_info.classdesc);
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

#define MAKE_INT_COMPARE(op)                                                   \
  {                                                                            \
    int b = checked_pop(frame).i, a = checked_pop(frame).i;                    \
    if (a op b) {                                                              \
      frame->program_counter = insn->index;                                    \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                 \
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
#define MAKE_INT_COMPARE(op)                                                   \
  {                                                                            \
    int a = checked_pop(frame).i;                                              \
    if (a op 0) {                                                              \
      frame->program_counter = insn->index;                                    \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                 \
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

      bjvm_obj_header *array =
          CreatePrimitiveArray1D(thread, insn->array_type, count);
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
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind !=
                     BJVM_TYPE_KIND_VOID;
      if (likely(frame->state < INVOKE_STATE_MADE_FRAME)) {
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
        bjvm_stack_frame *new_frame = bjvm_push_frame(
            thread, insn->ic, frame->values + sd - insn->args, insn->args);
        if (!new_frame)
          goto done;
        frame->state = INVOKE_STATE_MADE_FRAME;
        status =
            bjvm_interpret(thread, new_frame, &frame->values[sd - insn->args]);
        if (status < BJVM_INTERP_RESULT_INT)
          frame->state = INVOKE_STATE_ENTRY;
        if (status != BJVM_INTERP_RESULT_OK)
          goto done;
        sd -= insn->args;
        sd += returns;
      } else {
        sd -= insn->args;
        if (returns) {
          frame->values[sd] = frame->result_of_next;
          sd++;
        }
      }
      NEXT_INSN;
    }
    bjvm_insn_invokevtable_polymorphic:
    bjvm_insn_invokeitable_polymorphic: {
      bjvm_obj_header *target = frame->values[sd - insn->args].obj;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind !=
                     BJVM_TYPE_KIND_VOID;
      if (likely(frame->state < INVOKE_STATE_MADE_FRAME)) {
        if (target == nullptr) {
          bjvm_null_pointer_exception(thread);
          goto done;
        }
        bjvm_cp_method *target_method;
        if (insn->kind == bjvm_insn_invokevtable_polymorphic)
          target_method =
              bjvm_vtable_lookup(target->descriptor, (int)insn->ic2);
        else {
          target_method =
              bjvm_itable_lookup(target->descriptor, insn->ic, (int)insn->ic2);
          if (!target_method) {
            bjvm_abstract_method_error(thread, insn->cp->methodref.resolved);
            goto done;
          }
        }
        assert(target_method);
        bjvm_stack_frame *new_frame = bjvm_push_frame(
            thread, target_method, frame->values + sd - insn->args, insn->args);
        if (!new_frame)
          goto done;
        frame->state = INVOKE_STATE_MADE_FRAME;
        status =
            bjvm_interpret(thread, new_frame, &frame->values[sd - insn->args]);
        if (status < BJVM_INTERP_RESULT_INT)
          frame->state = INVOKE_STATE_ENTRY;
        if (status != BJVM_INTERP_RESULT_OK)
          goto done;
        frame->state = INVOKE_STATE_ENTRY;
        sd -= insn->args;
        sd += returns;
      } else {
        sd -= insn->args;
        if (returns) {
          frame->values[sd] = frame->result_of_next;
          sd++;
        }
      }
      NEXT_INSN;
    }
    bjvm_insn_invokespecial_resolved: {
      bjvm_obj_header *target = frame->values[sd - insn->args].obj;
      bool returns = insn->cp->methodref.descriptor->return_type.base_kind !=
                     BJVM_TYPE_KIND_VOID;
      if (likely(frame->state < INVOKE_STATE_MADE_FRAME)) {
        if (target == nullptr) {
          bjvm_null_pointer_exception(thread);
          goto done;
        }
        bjvm_cp_method *target_method = insn->ic;
        bjvm_stack_frame *new_frame = bjvm_push_frame(
            thread, target_method, frame->values + sd - insn->args, insn->args);
        if (!new_frame)
          goto done;
        frame->state = INVOKE_STATE_MADE_FRAME;
        status =
            bjvm_interpret(thread, new_frame, &frame->values[sd - insn->args]);
        if (status < BJVM_INTERP_RESULT_INT)
          frame->state = INVOKE_STATE_ENTRY;
        if (status != BJVM_INTERP_RESULT_OK)
          goto done;
        frame->state = INVOKE_STATE_ENTRY;
        sd -= insn->args;
        sd += returns;
      } else {
        sd -= insn->args;
        if (returns) {
          frame->values[sd] = frame->result_of_next;
          sd++;
        }
      }
      NEXT_INSN;
    }

#define GETFIELD_IMPL(kind)                                                    \
  {                                                                            \
    bjvm_obj_header *obj = checked_pop(frame).obj;                             \
    if (!obj) {                                                                \
      bjvm_null_pointer_exception(thread);                                     \
      goto done;                                                               \
    }                                                                          \
    checked_push(frame, load_stack_value((void *)obj + (int)insn->ic2, kind)); \
    NEXT_INSN;                                                                 \
  }

#define PUTFIELD_IMPL(kind)                                                    \
  {                                                                            \
    bjvm_stack_value value = checked_pop(frame);                               \
    bjvm_obj_header *obj = checked_pop(frame).obj;                             \
    if (!obj) {                                                                \
      bjvm_null_pointer_exception(thread);                                     \
      goto done;                                                               \
    }                                                                          \
    store_stack_value((void *)obj + (int)insn->ic2, value, kind);              \
    NEXT_INSN;                                                                 \
  }

#define PUTSTATIC_IMPL(kind)                                                   \
  {                                                                            \
    bjvm_stack_value value = checked_pop(frame);                               \
    store_stack_value(insn->ic, value, kind);                                  \
    NEXT_INSN;                                                                 \
  }

#define GETSTATIC_IMPL(kind)                                                   \
  {                                                                            \
    checked_push(frame, load_stack_value(insn->ic, kind));                     \
    NEXT_INSN;                                                                 \
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
  if (status >= BJVM_INTERP_RESULT_INT)
    return status;

  if (thread->current_exception != nullptr && method) {
    bjvm_attribute_exception_table *table = method->code->exception_table;
    status = BJVM_INTERP_RESULT_EXC;
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
          if (!ent.catch_type ||
              bjvm_instanceof(thread->current_exception->descriptor,
                              ent.catch_type->classdesc)) {
            frame->program_counter = ent.handler_insn;
            sd = 1;
            frame->values[0] =
                (bjvm_stack_value){.obj = thread->current_exception};
            thread->current_exception = nullptr;
            status = BJVM_INTERP_RESULT_OK;

            goto interpret_frame;
          }
        }
      }
    }
  }

  if (raw_frame == final_frame) {
    bjvm_pop_frame(thread, raw_frame);
    return thread->current_exception != nullptr ? BJVM_INTERP_RESULT_EXC
                                                : status;
  }

  // If there is a previous frame to go to, go to it
  bjvm_pop_frame(thread, raw_frame);
  goto get_top_frame;
}

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
        (mid == table->entry_count - 1 ||
         table->entries[mid + 1].start_pc > original_pc)) {
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
    bjvm_classdesc *ThreadGroup =
        bootstrap_class_create(thread, STR("java/lang/ThreadGroup"));
    int error = bjvm_initialize_class(thread, ThreadGroup);
    assert(!error);

    bjvm_cp_method *init = bjvm_method_lookup(ThreadGroup, STR("<init>"),
                                              STR("()V"), false, false);

    assert(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    bjvm_thread_run(thread, init, args, nullptr);
  }
  return vm->main_thread_group;
}