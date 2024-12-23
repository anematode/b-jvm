#define AGGRESSIVE_DEBUG 0

#define CACHE_INVOKESTATIC 1
#define CACHE_INVOKENONSTATIC 1
#define ONE_GOTO_PER_INSN 0

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

struct file_entry {
  size_t len;
  uint8_t *data;
};

struct file_entry *make_cf_entry(const uint8_t *bytes, size_t len) {
  struct file_entry *result = malloc(sizeof(struct file_entry));
  result->data = malloc(result->len = len);
  memcpy(result->data, bytes, len);
  return result;
}

void free_cf_entry(void *entry_) {
  if (!entry_)
    return;
  struct file_entry *entry = entry_;
  free(entry->data);
  free(entry);
}

#define MAX_CF_NAME_LENGTH 1000

int add_classfile_bytes(bjvm_vm *vm, const bjvm_utf8 filename,
                        const uint8_t *bytes, size_t len) {
  if (filename.len > MAX_CF_NAME_LENGTH)
    return -1;

  struct file_entry *entry = make_cf_entry(bytes, len);
  free_cf_entry(bjvm_hash_table_insert(&vm->classfiles, filename.chars,
                                       filename.len, entry));
  return 0;
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

bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method) {
  assert(method != nullptr);

  const bjvm_attribute_code *code = method->code;
  assert(code);

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  assert(header_bytes % 8 == 0);

  size_t local_bytes =
      ((int)code->max_locals + code->max_stack) * sizeof(bjvm_stack_value);
  size_t total = header_bytes + local_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    // bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    UNREACHABLE();
  }

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  memset(frame, 0, total);
  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) =
      frame;
  frame->max_locals = code->max_locals;
  frame->max_stack = code->max_stack;
  frame->program_counter = 0;
  frame->stack_depth = 0;
  frame->method = method;

  return frame;
}

void dump_frame(FILE *stream, const bjvm_stack_frame *frame) {
  char buf[5000] = {0}, *write = buf, *end = buf + sizeof(buf);

  bjvm_compressed_bitset refs =
      frame->method ? ((bjvm_code_analysis *)frame->method->code_analysis)
                          ->insn_index_to_references[frame->program_counter]
                    : bjvm_empty_bitset();

  for (int i = 0; i < frame->stack_depth; ++i) {
    bjvm_stack_value value = frame->values[i];
    const char *is_ref = bjvm_test_compressed_bitset(refs, i) ? "<ref>" : "";
    write +=
        snprintf(write, end - write, " stack[%d] = [ ref = %p, int = %d ] %s\n",
                 i, value.obj, value.i, is_ref);
  }

  for (int i = 0; i < frame->max_locals; ++i) {
    bjvm_stack_value value = frame->values[i + frame->max_stack];
    const char *is_ref =
        bjvm_test_compressed_bitset(refs, i + frame->max_stack) ? "<ref>" : "";
    write +=
        snprintf(write, end - write, "locals[%d] = [ ref = %p, int = %d ] %s\n",
                 i, value.obj, value.i, is_ref);
  }

  fprintf(stream, "%s", buf);
}

void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference) {
  assert(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  assert(reference == nullptr || reference == frame);
  thr->frames_count--;
  thr->frame_buffer_used =
      thr->frames_count == 0 ? 0 : (uint8_t *)frame - thr->frame_buffer;
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

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 name,
                                   const bjvm_utf8 method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces);

bjvm_stack_value unimplemented_native(bjvm_thread *, bjvm_obj_header *,
                                      bjvm_stack_value *, int) {
  return value_null();
}

// Raise an UnsatisfiedLinkError relating to the given method.
void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method) {
  printf("Unsatisfied link error %.*s on %.*s\n", fmt_slice(method->name),
         fmt_slice(method->my_class->name));
  INIT_STACK_STRING(err, 1000);
  bprintf(err, "Method %.*s on class %.*s with descriptor %.*s",
          fmt_slice(method->name), fmt_slice(method->my_class->name),
          fmt_slice(method->descriptor));

  bjvm_raise_exception(thread, STR("java/lang/UnsatisfiedLinkError"), err);
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

// Raise an AbstractMethodError.
void bjvm_abstract_method_error(bjvm_thread *thread,
                                const bjvm_cp_method *method) {
  INIT_STACK_STRING(complaint, 1000);
  bprintf(complaint, "Abstract method '%.*s' on class %.*s",
          fmt_slice(method->name), fmt_slice(method->my_class->name));
  bjvm_raise_exception(thread, STR("java/lang/AbstractMethodError"), complaint);
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
  struct bjvm_native_String *str = (void *)new_object(thread, java_lang_String);

  short *chars;
  int len;
  convert_modified_utf8_to_chars(string.chars, string.len, &chars, &len, true);
  str->value = CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_CHAR, len);
  memcpy(ArrayData(str->value), chars, len * sizeof(short));
  free(chars);
  return (void *)str;
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
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_BOOLEAN)
        ->reflected_class;
  case 'B':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_BYTE)
        ->reflected_class;
  case 'C':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_CHAR)
        ->reflected_class;
  case 'S':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_SHORT)
        ->reflected_class;
  case 'I':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_INT)
        ->reflected_class;
  case 'J':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_LONG)
        ->reflected_class;
  case 'F':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_FLOAT)
        ->reflected_class;
  case 'D':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_DOUBLE)
        ->reflected_class;
  case 'V':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_VOID)
        ->reflected_class;
  }
  UNREACHABLE();
}

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field) {

  bjvm_classdesc *reflect_Field =
      bootstrap_class_create(thread, STR("java/lang/reflect/Field"));
  bjvm_initialize_class(thread, reflect_Field);

  bjvm_handle *field_mirror =
      bjvm_make_handle(thread, new_object(thread, reflect_Field));

#define F ((struct bjvm_native_Field *)field_mirror->obj)
  field->reflection_field = (void *)F;

  F->reflected_field = field;
  F->name = bjvm_intern_string(thread, field->name);
  F->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  F->type = (void *)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, field->descriptor));
  F->modifiers = field->access_flags;

  // Find runtimevisibleannotations attribute
  for (int i = 0; i < field->attributes_count; ++i) {
    if (field->attributes[i].kind ==
        BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS) {
      const bjvm_attribute_runtime_visible_annotations a =
          field->attributes[i].runtime_visible_annotations;
      F->annotations =
          CreatePrimitiveArray1D(thread, BJVM_TYPE_KIND_BYTE, a.length);
      memcpy(ArrayData(F->annotations), a.data, field->attributes[i].length);
      break;
    }
  }
#undef F

  bjvm_drop_handle(thread, field_mirror);
}

bjvm_utf8 unparse_field_descriptor(bjvm_utf8 str,
                                   const bjvm_field_descriptor *desc) {
  bjvm_utf8 write = str;
  // Print '[' repeatedly
  int dims = desc->dimensions;
  while (dims--) {
    write.chars[0] = '[';
    write = slice(write, 1);
  }
  switch (desc->base_kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_FLOAT:
  case BJVM_TYPE_KIND_DOUBLE:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_LONG:
  case BJVM_TYPE_KIND_VOID:
    write = slice(write, bprintf(write, "%c", desc->base_kind).len);
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    write =
        slice(write, bprintf(write, "L%.*s;", fmt_slice(desc->class_name)).len);
    break;
  default:
    UNREACHABLE();
  }
  str.len = write.chars - str.chars;
  return str;
}

void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method) {
  assert(utf8_equals(method->name, "<init>"));

  bjvm_classdesc *reflect_Constructor =
      bootstrap_class_create(thread, STR("java/lang/reflect/Constructor"));
  bjvm_initialize_class(thread, reflect_Constructor);

  method->reflection_ctor = (void *)new_object(thread, reflect_Constructor);

  bjvm_handle *result =
      bjvm_make_handle(thread, (void *)method->reflection_ctor);
#define C ((struct bjvm_native_Constructor *)result->obj)
  C->reflected_ctor = method;
  C->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  C->modifiers = method->access_flags;
  C->parameterTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")),
      method->parsed_descriptor->args_count);

  for (int i = 0; i < method->parsed_descriptor->args_count; ++i) {
    INIT_STACK_STRING(desc, 1000);
    desc = unparse_field_descriptor(desc, &method->parsed_descriptor->args[i]);
    struct bjvm_native_Class *type = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
    ((struct bjvm_native_Class **)ArrayData(C->parameterTypes))[i] = type;
  }

#undef C
}

void bjvm_reflect_initialize_method(bjvm_thread *thread,
                                    bjvm_classdesc *classdesc,
                                    bjvm_cp_method *method) {
  bjvm_classdesc *reflect_Method =
      bootstrap_class_create(thread, STR("java/lang/reflect/Method"));
  bjvm_initialize_class(thread, reflect_Method);

  struct bjvm_native_Method *result = method->reflection_method =
      (void *)new_object(thread, reflect_Method);
  result->reflected_method = method;
  result->name = bjvm_intern_string(thread, method->name);
  result->clazz = (void *)bjvm_get_class_mirror(thread, classdesc);
  result->modifiers = method->access_flags;
  result->signature = make_string(thread, method->descriptor);

  result->parameterTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")),
      method->parsed_descriptor->args_count);
  struct bjvm_native_Class **types = (void *)ArrayData(result->parameterTypes);
  INIT_STACK_STRING(str, 1000);
  for (int i = 0; i < method->parsed_descriptor->args_count; ++i) {
    bjvm_utf8 desc =
        unparse_field_descriptor(str, &method->parsed_descriptor->args[i]);
    types[i] = (void *)bjvm_get_class_mirror(
        thread, load_class_of_field_descriptor(thread, desc));
  }

  bjvm_utf8 ret_desc =
      unparse_field_descriptor(str, &method->parsed_descriptor->return_type);
  result->returnType = (void *)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, ret_desc));

  // TODO parse and fill these in
  result->exceptionTypes = CreateObjectArray1D(
      thread, bootstrap_class_create(thread, STR("java/lang/Class")), 0);
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

  // Create the exception object
  bjvm_obj_header *obj = new_object(thread, classdesc);
  if (exception_string.chars) {
    bjvm_obj_header *str = make_string(thread, exception_string);
    bjvm_cp_method *method = bjvm_easy_method_lookup(
        classdesc, STR("<init>"), STR("(Ljava/lang/String;)V"), true, false);
    bjvm_thread_run(thread, method,
                    (bjvm_stack_value[]){{.obj = obj}, {.obj = str}}, nullptr);
  } else {
    bjvm_cp_method *method = bjvm_easy_method_lookup(classdesc, STR("<init>"),
                                                     STR("()V"), true, false);
    bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}},
                    nullptr);
  }

#ifndef EMSCRIPTEN
  printf("Exception: %.*s: %.*s\n", fmt_slice(exception_name),
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
  bjvm_classdesc *mirror_desc =
      bootstrap_class_create(thread, STR("java/lang/Class"));
  assert(mirror_desc->state == BJVM_CD_STATE_INITIALIZED);

  bjvm_classdesc *desc = calloc(1, sizeof(bjvm_classdesc));
  struct bjvm_native_Class *mirror = (void *)new_object(thread, mirror_desc);

  mirror->reflected_class = desc;

  desc->kind = BJVM_CD_KIND_PRIMITIVE;
  desc->super_class = nullptr;
  desc->name = make_heap_str_from(name);
  desc->access_flags =
      BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL | BJVM_ACCESS_ABSTRACT;
  desc->array_type = nullptr;
  desc->mirror = mirror;
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
}

bjvm_vm_options bjvm_default_vm_options() {
  bjvm_vm_options options = {0};
  options.heap_size = 1 << 21;
  return options;
}

static void _free_classdesc(void *cd) {
  bjvm_classdesc *classdesc = cd;
  classdesc->dtor(classdesc);
}

bjvm_vm *bjvm_create_vm(bjvm_vm_options options) {
  bjvm_vm *vm = calloc(1, sizeof(bjvm_vm));

  vm->load_classfile = options.load_classfile;
  vm->load_classfile_param = options.load_classfile_param;

  vm->classfiles = bjvm_make_hash_table(free_cf_entry, 0.75, 16);
  vm->classes = bjvm_make_hash_table(_free_classdesc, 0.75, 16);
  vm->inchoate_classes = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->natives = bjvm_make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->class_padding = bjvm_make_hash_table(nullptr, 0.75, 16);
  vm->main_thread_group = nullptr;

  vm->heap = aligned_alloc(4096, options.heap_size);
  vm->heap_used = 0;
  vm->heap_capacity = options.heap_size;

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
  bjvm_free_hash_table(vm->classfiles);
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

  bjvm_classdesc *desc;

  // Link (but don't initialize) java.lang.Class immediately
  auto thing = STR("java/lang/Class");
  assert(thing.len != 0);
  desc = bootstrap_class_create(thr, STR("java/lang/Class"));
  bjvm_initialize_class(thr, desc);

  bjvm_vm_init_primitive_classes(thr);

  desc = bootstrap_class_create(thr, STR("java/lang/reflect/Field"));
  desc = bootstrap_class_create(thr, STR("java/lang/reflect/Constructor"));

  // Initialize java.lang.Thread mirror
  desc = bootstrap_class_create(thr, STR("java/lang/Thread"));
  bjvm_initialize_class(thr, desc);

  struct bjvm_native_Thread *thread_obj = (void *)new_object(thr, desc);
  thr->thread_obj = thread_obj;

  thread_obj->vm_thread = thr;
  thread_obj->priority = 5;
  thread_obj->name = bjvm_intern_string(thr, STR("main"));

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_easy_method_lookup(
      desc, STR("<init>"), STR("(Ljava/lang/ThreadGroup;Ljava/lang/String;)V"),
      false, false);
  bjvm_obj_header *name = make_string(thr, STR("main"));
  bjvm_thread_run(thr, make_thread,
                  (bjvm_stack_value[]){{.obj = (void *)thread_obj},
                                       {.obj = main_thread_group},
                                       {.obj = name}},
                  nullptr);

  // Call System.initializeSystemClass()
  desc = bootstrap_class_create(thr, STR("java/lang/System"));
  bjvm_initialize_class(thr, desc);

  bjvm_cp_method *method = bjvm_easy_method_lookup(
      desc, STR("initializeSystemClass"), STR("()V"), false, false);
  bjvm_stack_value ret;
  bjvm_thread_run(thr, method, nullptr, &ret);

  thr->current_exception = nullptr;

  // Call setJavaLangAccess() since we crash befroe getting there
  method = bjvm_easy_method_lookup(desc, STR("setJavaLangAccess"), STR("()V"),
                                   false, false);
  bjvm_thread_run(thr, method, nullptr, &ret);

  return thr;
}

void bjvm_free_thread(bjvm_thread *thread) {
  // TODO remove from the VM

  // TODO what happens to ->current_exception etc.?
  free(thread->frames);
  free(thread->frame_buffer);
  free(thread->handles);
  free(thread);
}

int bjvm_vm_preregister_classfile(bjvm_vm *vm, const bjvm_utf8 filename,
                                  const uint8_t *bytes, size_t len) {
  return add_classfile_bytes(vm, filename, bytes, len);
}

int bjvm_vm_read_classfile(bjvm_vm *vm, const bjvm_utf8 filename,
                           const uint8_t **bytes, size_t *len) {
  struct file_entry *entry =
      bjvm_hash_table_lookup(&vm->classfiles, filename.chars, filename.len);
  if (entry) {
    if (bytes)
      *bytes = entry->data;
    if (len)
      *len = entry->len;
    return 0;
  }

  // Otherwise, try to read it from the vm->load_classfile implementation
  if (vm->load_classfile) {
    uint8_t *loaded_bytes = nullptr;

    int status = vm->load_classfile(filename, vm->load_classfile_param,
                                    &loaded_bytes, len);
    if (status) {
      free(loaded_bytes);
      return -1;
    }

    bjvm_vm_preregister_classfile(vm, filename, loaded_bytes, *len);
    free(loaded_bytes);
    return bjvm_vm_read_classfile(vm, filename, bytes, len);
  }

  return -1;
}

void bjvm_vm_list_classfiles(bjvm_vm *vm, heap_string *strings, size_t *count) {
  *count = vm->classfiles.entries_count;
  if (strings) {
    bjvm_hash_table_iterator iter =
        bjvm_hash_table_get_iterator(&vm->classfiles);
    size_t i = 0;
    void *value;

    bjvm_utf8 key;

    while (i < *count && bjvm_hash_table_iterator_has_next(
                             iter, &key.chars, (size_t *)&key.len, &value)) {
      heap_string heap_key = make_heap_str_from(key);
      strings[i] = heap_key;
      ++i;
      bjvm_hash_table_iterator_next(&iter);
    }
  }
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

heap_string field_descriptor_to_name(bjvm_field_descriptor desc) {
  heap_string result = make_heap_str(
      desc.dimensions + 6 +
      (desc.base_kind == BJVM_TYPE_KIND_REFERENCE ? desc.class_name.len : 0));

  bjvm_utf8 write = hslc(result);
  if (desc.dimensions) {
    memset(write.chars, '[', desc.dimensions);
    write = slice(write, desc.dimensions);
  }

  if (desc.base_kind == BJVM_TYPE_KIND_REFERENCE) {
    *write.chars = 'L';
    write = slice(write, 1);

    memcpy(write.chars, desc.class_name.chars, desc.class_name.len);
    write = slice(write, desc.class_name.len);

    *write.chars = ';';
    write = slice(write, 1);
  } else {
    *write.chars = desc.base_kind;
    write = slice(write, 1);
  }

  result.len = write.chars - result.chars;
  return result;
}

bjvm_classdesc *load_class_of_field(bjvm_thread *thread,
                                    const bjvm_field_descriptor *field) {
  heap_string name = field_descriptor_to_name(*field);
  bjvm_classdesc *result = load_class_of_field_descriptor(thread, hslc(name));
  free_heap_str(name);
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

  bjvm_obj_header *ptypes =
      CreateObjectArray1D(thread, Class, method->args_count);
  struct bjvm_native_Class *rtype;

  // TODO handlify

  for (int i = 0; i < method->args_count; ++i) {
    heap_string name = field_descriptor_to_name(method->args[i]);
    bjvm_classdesc *arg_desc =
        load_class_of_field_descriptor(thread, hslc(name));
    free_heap_str(name);

    if (!arg_desc)
      return nullptr;
    *((struct bjvm_native_Class **)ArrayData(ptypes) + i) =
        bjvm_get_class_mirror(thread, arg_desc);
  }

  heap_string name = field_descriptor_to_name(method->return_type);
  bjvm_classdesc *ret_desc = load_class_of_field_descriptor(thread, hslc(name));
  free_heap_str(name);
  if (!ret_desc)
    return nullptr;
  rtype = bjvm_get_class_mirror(thread, ret_desc);
  // Call <init>(Ljava/lang/Class;[Ljava/lang/Class;Z)V
  bjvm_cp_method *init =
      bjvm_easy_method_lookup(MethodType, STR("makeImpl"),
                              STR("(Ljava/lang/Class;[Ljava/lang/Class;Z)Ljava/"
                                  "lang/invoke/MethodType;"),
                              false, false);
  bjvm_stack_value result;
  bjvm_thread_run(thread, init,
                  (bjvm_stack_value[]){{.obj = (void *)rtype},
                                       {.obj = ptypes},
                                       {.i = 1 /* trusted */}},
                  &result);
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

    for (int i = 0; i < method->method_descriptor->args_count; ++i) {
      bjvm_field_descriptor *arg = method->method_descriptor->args + i;
      *VECTOR_PUSH(ptypes, ptypes_count, ptypes_capacity) =
          load_class_of_field(thread, arg);
    }

    rtype =
        load_class_of_field(thread, &method->method_descriptor->return_type);
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
      bjvm_easy_method_lookup(MethodType, STR("makeImpl"),
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
    UNREACHABLE();
    break;
  case BJVM_MH_KIND_INVOKE_STATIC:
  case BJVM_MH_KIND_INVOKE_SPECIAL:
  case BJVM_MH_KIND_NEW_INVOKE_SPECIAL:
  case BJVM_MH_KIND_INVOKE_INTERFACE:
    bjvm_cp_method_info *method = &info->reference->methodref;
    bjvm_resolve_class(thread, method->class_info);
    bjvm_initialize_class(thread, method->class_info->classdesc);
    bjvm_cp_method *m = bjvm_method_lookup(
        method->class_info->classdesc, method->name_and_type->name,
        method->name_and_type->descriptor, true, true);
    bjvm_reflect_initialize_method(thread, method->class_info->classdesc, m);

    // Call DirectMethodHandle.make(method, true)
    bjvm_classdesc *DirectMethodHandle = bootstrap_class_create(
        thread, STR("java/lang/invoke/DirectMethodHandle"));
    bjvm_initialize_class(thread, DirectMethodHandle);
    bjvm_cp_method *make =
        bjvm_easy_method_lookup(DirectMethodHandle, STR("make"),
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
  free(cd);
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
      bjvm_classdesc *d = thread->frames[i]->method->my_class;
      if (utf8_equals_utf8(hslc(d->name), chars)) {
        class = d;
        break;
      }
    }
  }

  if (!class) {
    // Add entry to inchoate_classes
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
        bjvm_vm_read_classfile(vm, filename, (const uint8_t **)&bytes, &cf_len);
    if (read_status) {
      int i = 0;
      for (; i < chars.len; ++i)
        filename.chars[i] = filename.chars[i] == '/' ? '.' : filename.chars[i];
      // ClassNotFoundException: com.google.DontBeEvil
      bjvm_raise_exception(thread, STR("java/lang/ClassNotFoundException"),
                           filename);
      return nullptr;
    }

    class = calloc(1, sizeof(bjvm_classdesc));
    parse_result_t error = bjvm_parse_classfile(bytes, cf_len, class);
    if (error != PARSE_SUCCESS) {
      free(class);
      // TODO raise VerifyError
      UNREACHABLE();
    }

    // 3. If C has a direct superclass, the symbolic reference from C to its
    // direct superclass is resolved using the algorithm of §5.4.3.1.
    bjvm_cp_class_info *super = class->super_class;
    if (super) {
      // If the superclass is currently being loaded -> circularity  error
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars,
                                 super->name.len)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, class->super_class);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
      }
    }

    // 4. If C has any direct superinterfaces, the symbolic references from C to
    // its direct superinterfaces are resolved using the algorithm of §5.4.3.1.
    for (int i = 0; i < class->interfaces_count; ++i) {
      bjvm_cp_class_info *super = class->interfaces[i];
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name.chars,
                                 super->name.len)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, class->interfaces[i]);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
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
              utf8_equals_utf8(method->descriptor, entry->descriptor)) {
            method->native_handle = entry->callback;
            break;
          }
        }
      }
    }

    // Remove from inchoate_classes
    (void)bjvm_hash_table_delete(&vm->inchoate_classes, chars.chars, chars.len);
    (void)bjvm_hash_table_insert(&vm->classes, chars.chars, chars.len, class);

    class->kind = BJVM_CD_KIND_ORDINARY;
    class->dtor = free_ordinary_classdesc;
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
  int status = bjvm_link_class(thread, classdesc->base_component);
  if (status) {
    // TODO mark all arrays of this class as fucked up
    UNREACHABLE();
  }
  classdesc->state = classdesc->base_component->state;
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
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
    UNREACHABLE();
  }
  return result;
}

int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  assert(classdesc);
  if (classdesc->state != BJVM_CD_STATE_LOADED) // already linked
    return 0;

  if (classdesc->kind != BJVM_CD_KIND_ORDINARY) {
    assert(classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
    return bjvm_link_array_class(thread, classdesc);
  }

  // Link superclasses
  if (classdesc->super_class) {
    int status = bjvm_link_class(thread, classdesc->super_class->classdesc);
    if (status) {
      // TODO raise VerifyError
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }

  // Link superinterfaces
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    int status = bjvm_link_class(thread, classdesc->interfaces[i]->classdesc);
    if (status) {
      // TODO raise VerifyError
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }

  classdesc->state = BJVM_CD_STATE_LINKED;

  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < classdesc->methods_count; ++method_i) {
    bjvm_cp_method *method = classdesc->methods + method_i;
    if (method->code) {
      heap_string error_str;
      int result = bjvm_analyze_method_code_segment(method, &error_str);
      if (result != 0) {
        // TODO raise VerifyError
        classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
        printf("Error analyzing method %.*s: %.*s\n", method->name.len,
               method->name.chars, error_str.len, error_str.chars);
        UNREACHABLE();
      }
    }
  }

  int imp_padding = (int)(uintptr_t)bjvm_hash_table_lookup(
      &thread->vm->class_padding, classdesc->name.chars, classdesc->name.len);

  // Assign memory locations to all static/non-static fields
  bjvm_classdesc *super =
      classdesc->super_class ? classdesc->super_class->classdesc : NULL;
  int static_offset = 0,
      nonstatic_offset = super ? super->data_bytes : sizeof(bjvm_obj_header);
  nonstatic_offset += imp_padding;
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

  classdesc->static_references =
      bjvm_init_compressed_bitset(static_offset / sizeof(void *));
  classdesc->instance_references =
      bjvm_init_compressed_bitset(nonstatic_offset / sizeof(void *));
  // Add superclass instance references
  if (super) {
    bjvm_compressed_bitset bs = super->instance_references;
    for (int i = 0; i < super->data_bytes / sizeof(void *); ++i) {
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
  classdesc->data_bytes = nonstatic_offset;

  return 0;
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
    bjvm_major_gc(thread->vm); // LOL
    if (vm->heap_used + bytes > vm->heap_capacity) {
      // Out of memory
      UNREACHABLE();
    }
  }
  void *result = vm->heap + vm->heap_used;
  memset(result, 0, bytes);
  vm->heap_used += bytes;
  return result;
}

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result);

int bjvm_initialize_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  assert(classdesc);
  if (classdesc->state == BJVM_CD_STATE_INITIALIZED)
    return 0; // already initialized
  if (classdesc->state != BJVM_CD_STATE_LINKED) {
    int error = bjvm_link_class(thread, classdesc);
    if (error)
      return error;
  }

  classdesc->state = BJVM_CD_STATE_INITIALIZED;

  bjvm_cp_method *clinit = bjvm_easy_method_lookup(classdesc, STR("<clinit>"),
                                                   STR("()V"), false, false);
  int error = 0;
  if (clinit) {
    bjvm_stack_frame *frame = bjvm_push_frame(thread, clinit);
    error = bjvm_bytecode_interpret(thread, frame, nullptr);
    bjvm_pop_frame(thread, frame);
  }
  classdesc->state =
      error ? BJVM_CD_STATE_LINKAGE_ERROR : BJVM_CD_STATE_INITIALIZED;
  return error;
}

#define checked_pop(frame)                                                     \
  ({                                                                           \
    assert(frame->stack_depth > 0);                                            \
    frame->values[--frame->stack_depth];                                       \
  })

void checked_push(bjvm_stack_frame *frame, bjvm_stack_value value) {
  assert(frame->stack_depth < frame->max_stack);
  frame->values[frame->stack_depth++] = value;
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
          utf8_equals_utf8(candidate->descriptor, method_descriptor));
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

bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const bjvm_utf8 name,
                                        const bjvm_utf8 descriptor,
                                        bool superclasses,
                                        bool superinterfaces) {
  if (!classdesc)
    return nullptr;

  bjvm_cp_method *result = bjvm_method_lookup(classdesc, name, descriptor,
                                              superclasses, superinterfaces);

  return result;
}

int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                    bjvm_stack_value *args, bjvm_stack_value *result) {
  assert(method);

  bjvm_stack_frame *frame = bjvm_push_frame(thread, method);
  if (!frame)
    return -1;

  int object_argument = !(method->access_flags & BJVM_ACCESS_STATIC);
  for (int i = 0; i < method->parsed_descriptor->args_count + object_argument;
       ++i) {
    frame->values[frame->max_stack + i] = args[i];
  }
  bjvm_bytecode_interpret(thread, frame, result);
  bjvm_pop_frame(thread, frame);
  return 0;
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
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
  default:
    UNREACHABLE();
  }
}

bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind) {
  bjvm_stack_value result;
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    result.i = *(int8_t *)field_location;
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
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
  default:
    UNREACHABLE();
  }
  return result;
}

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  assert(classdesc->state >= BJVM_CD_STATE_LINKED);
  bjvm_obj_header *obj =
      bump_allocate(thread, classdesc->data_bytes + sizeof(bjvm_obj_header));
  obj->descriptor = classdesc;
  obj->mark_word = ObjNextHashCode();
  return obj;
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
  printf("Mirror, desc: %p %p\n", cp_mirror, cp_mirror->reflected_class);
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
  // Walk the superclasses/superinterfaces of bjvm_obj_header and see if any are
  // equal to classdesc
  // TODO compare class loaders too, superinterfaces
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

bool compare_method_types(struct bjvm_native_MethodType *provider_mt,
                          struct bjvm_native_MethodType *targ) {
  // Compare ptypes
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
      bjvm_easy_method_lookup(header->descriptor, STR("toString"),
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

void pass_args_to_frame(bjvm_stack_frame *new_frame,
                        bjvm_stack_frame *old_frame, int args,
                        const bjvm_method_descriptor *descriptor,
                        bool is_static) {
  for (int i = 0, j = 0; i < args; ++i, ++j) {
    new_frame->values[new_frame->max_stack + j] =
        old_frame->values[old_frame->stack_depth - args + i];
  }
}

void bjvm_invokevirtual_signature_polymorphic(
    bjvm_thread *thread, bjvm_stack_frame *frame, bjvm_cp_method *method,
    struct bjvm_native_MethodType *provider_mt, bjvm_obj_header *target,
    int args) {
  // TODO attach this to the descriptor
  // TODO check invokeExact, invoke
  struct bjvm_native_MethodHandle *mh = (void *)target;
  struct bjvm_native_MethodType *targ = (void *)mh->type;

  // varargs if mh is of class AsVarargsCollector
  bool is_varargs =
      utf8_equals(hslc(mh->base.descriptor->name),
                  "java/lang/invoke/MethodHandleImpl$AsVarargsCollector");
  if (is_varargs) {
    // To-implement
    UNREACHABLE();
  }

  bool mts_are_same = compare_method_types(provider_mt, targ);
  bool is_invoke_exact = utf8_equals_utf8(method->name, STR("invokeExact"));
  bool is_invoke_basic = utf8_equals_utf8(method->name, STR("invokeBasic"));
  if (is_invoke_exact) {
    if (!mts_are_same) {
      bjvm_wrong_method_type_error(thread, provider_mt, targ);
    }
  }

  if (!mts_are_same && !is_invoke_basic) {
    // Call asType
    bjvm_cp_method *asType = bjvm_easy_method_lookup(
        mh->base.descriptor, STR("asType"),
        STR("(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;"),
        true, false);
    if (!asType)
      UNREACHABLE();

    bjvm_stack_value result = value_null();
    bjvm_thread_run(
        thread, asType,
        (bjvm_stack_value[]){{.obj = (void *)mh}, {.obj = (void *)provider_mt}},
        &result);
    if (thread->current_exception)
      return;
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
    bjvm_stack_frame *new_frame = bjvm_push_frame(thread, method);
    pass_args_to_frame(new_frame, frame, method->parsed_descriptor->args_count,
                       method->parsed_descriptor, true);
    bjvm_bytecode_interpret(thread, new_frame, &result);
    bjvm_pop_frame(thread, new_frame);
    frame->stack_depth -= args;
    if (method->parsed_descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID)
      checked_push(frame, result);

    // UNREACHABLE();
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
}

// Used when passing arguments into a native function:
bjvm_value *make_handles_array(bjvm_thread *thread,
                               const bjvm_method_descriptor *calling,
                               bjvm_stack_value *stack_args) {
  int argc = calling->args_count;
  bjvm_value *result = malloc(argc * sizeof(bjvm_value));
  for (int i = 0; i < argc; ++i) {
    // For each argument, if it's a reference, wrap it in a handle; otherwise
    // just memcpy it over since the representations of primitives are the same
    if (field_to_kind(calling->args + i) == BJVM_TYPE_KIND_REFERENCE) {
      result[i].handle = bjvm_make_handle(thread, stack_args[i].obj);
    } else {
      memcpy(result + i, stack_args + i, sizeof(bjvm_stack_value));
    }
  }
  return result;
}

void drop_handles_array(bjvm_thread *thread, bjvm_method_descriptor *called,
                        bjvm_value *array) {
  int argc = called->args_count;
  for (int i = 0; i < argc; ++i) {
    if (field_to_kind(called->args + i) == BJVM_TYPE_KIND_REFERENCE) {
      bjvm_drop_handle(thread, array[i].handle);
    }
  }
  free(array);
}

// Implementation of invokespecial/invokeinterface/invokevirtual
int bjvm_invokenonstatic(bjvm_thread *thread, bjvm_stack_frame *frame,
                         bjvm_bytecode_insn *insn) {
  assert(insn->cp->kind == BJVM_CP_KIND_METHOD_REF ||
         insn->cp->kind == BJVM_CP_KIND_INTERFACE_METHOD_REF);
  bjvm_cp_method *method = insn->ic;

  int args = insn->args;
  bjvm_obj_header *target = frame->values[frame->stack_depth - args].obj;
  const bjvm_cp_method_info *info = &insn->cp->methodref;

  if (!method || !target ||
      (insn->kind != bjvm_insn_invokespecial &&
       target->descriptor != insn->ic2) ||
      !CACHE_INVOKENONSTATIC) {
    args = insn->args = info->method_descriptor->args_count + 1;

    assert(args <= frame->stack_depth);
    target = frame->values[frame->stack_depth - args].obj;

    if (target == nullptr) {
      bjvm_null_pointer_exception(thread);
      return -1;
    }
    if (insn->kind == bjvm_insn_invokespecial) {
      int error = bjvm_resolve_class(thread, info->class_info);
      if (error)
        return -1;
    }
    bjvm_classdesc *lookup_on = insn->ic2 =
        insn->kind == bjvm_insn_invokespecial ? info->class_info->classdesc
                                              : target->descriptor;
    if (lookup_on->state != BJVM_CD_STATE_INITIALIZED) {
      int error = bjvm_initialize_class(thread, lookup_on);
      if (error)
        return -1;
    }
    method = insn->ic =
        bjvm_method_lookup(lookup_on, info->name_and_type->name,
                           info->name_and_type->descriptor, true, true);
    insn->ic2 = lookup_on;
    if (!method) {
      INIT_STACK_STRING(complaint, 1000);
      bprintf(complaint,
              "Could not find method %.*s with descriptor %.*s on %s %.*s",
              fmt_slice(info->name_and_type->name),
              fmt_slice(info->name_and_type->descriptor),
              lookup_on->access_flags & BJVM_ACCESS_INTERFACE ? "interface"
                                                              : "class",
              fmt_slice(lookup_on->name));
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
    if (method->access_flags & BJVM_ACCESS_STATIC) {
      INIT_STACK_STRING(complaint, 1000);
      bprintf(complaint, "Method %.*s is static", fmt_slice(method->name));
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }

    if (method->access_flags & BJVM_ACCESS_ABSTRACT)
      bjvm_abstract_method_error(thread, method);
  }

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (method->is_signature_polymorphic) {
      bjvm_invokevirtual_signature_polymorphic(
          thread, frame, method,
          bjvm_resolve_method_type(thread, info->method_descriptor), target,
          args);
      return thread->current_exception != NULL;
    }

    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    // Need to wrap arguments in handles

    bjvm_handle *target_handle = bjvm_make_handle(thread, target);
    bjvm_value *native_args =
        make_handles_array(thread, info->method_descriptor,
                           frame->values + frame->stack_depth - args + 1);

    invoked_result = ((bjvm_native_callback)method->native_handle)(
        thread, target_handle, native_args, args - 1);
    frame->stack_depth -= args;

    bjvm_drop_handle(thread, target_handle);
    drop_handles_array(thread, info->method_descriptor, native_args);

    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    for (int i = 0, j = 0; i < args; ++i, ++j) {
      invoked_frame->values[invoked_frame->max_stack + j] =
          frame->values[frame->stack_depth - args + i];
    }
    frame->stack_depth -= args;

    int err = bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }
  if (method->parsed_descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);

  return 0;
}

int method_handle_linker(bjvm_thread *thread, bjvm_stack_frame *frame,
                         bjvm_cp_method *method) {
  return 0;
}

int bjvm_invokestatic(bjvm_thread *thread, bjvm_stack_frame *frame,
                      bjvm_bytecode_insn *insn) {
  bjvm_cp_method *method = insn->ic;
  const bjvm_cp_method_info *info = &insn->cp->methodref;
  if (!method || !CACHE_INVOKESTATIC) {
    bjvm_cp_class_info *class = info->class_info;

    int error = bjvm_resolve_class(thread, class);
    if (error)
      return -1;
    error = bjvm_initialize_class(thread, class->classdesc);
    if (error)
      return -1;

    method = insn->ic =
        bjvm_method_lookup(class->classdesc, info->name_and_type->name,
                           info->name_and_type->descriptor, true, false);
    if (!method) {
      INIT_STACK_STRING(complaint, 1000);
      bprintf(complaint,
              "Could not find method %.*s with descriptor %.*s on class %.*s",
              fmt_slice(info->name_and_type->name),
              fmt_slice(info->name_and_type->descriptor),
              fmt_slice(class->name));
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
  }

  int args = info->method_descriptor->args_count;
  assert(args <= frame->stack_depth);

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    bjvm_value *native_args =
        make_handles_array(thread, info->method_descriptor,
                           frame->values + frame->stack_depth - args);

    invoked_result = ((bjvm_native_callback)method->native_handle)(
        thread, nullptr, native_args, args);

    drop_handles_array(thread, info->method_descriptor, native_args);

    frame->stack_depth -= args;
    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    pass_args_to_frame(invoked_frame, frame, args, method->parsed_descriptor,
                       true);
    frame->stack_depth -= args;
    int err = bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }

  if (info->method_descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);
  return 0;
}

int bjvm_multianewarray(bjvm_thread *thread, bjvm_stack_frame *frame,
                        struct bjvm_multianewarray_data *multianewarray) {
  int dims = multianewarray->dimensions;
  assert(frame->stack_depth >= dims);
  assert(dims >= 1);

  int error = bjvm_resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  int dim_sizes[kArrayMaxDimensions];
  for (int i = 0; i < dims; ++i) {
    int dim = frame->values[frame->stack_depth - dims + i].i;
    if (dim < 0) {
      bjvm_negative_array_size_exception(thread, dim);
      return -1;
    }

    dim_sizes[i] = dim;
  }

  bjvm_obj_header *result =
      CreateArray(thread, multianewarray->entry->classdesc, dim_sizes, dims);
  frame->stack_depth -= dims;
  checked_push(frame, (bjvm_stack_value){.obj = result});
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
  bjvm_cp_method *lookup_factory = bjvm_easy_method_lookup(
      lookup_class, STR("lookup"),
      STR("()Ljava/lang/invoke/MethodHandles$Lookup;"), true, false);

  bjvm_thread_run(thread, lookup_factory, (bjvm_stack_value[]){}, &lookup_obj);
  bjvm_handle *lookup_handle = bjvm_make_handle(thread, lookup_obj.obj);

  bjvm_stack_frame *fake_frame =
      calloc(1, sizeof(bjvm_stack_frame) +
                    sizeof(bjvm_stack_value) * (m->args_count + 4));
  fake_frame->max_stack = fake_frame->stack_depth = m->args_count + 4;

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
    } else {
      memcpy(fake_frame->values + i + 4, handles + i, sizeof(bjvm_stack_value));
    }
  }

  // Invoke the bootstrap method using invokeExact
  bjvm_cp_method *invokeExact = bjvm_easy_method_lookup(
      bootstrap_handle->obj->descriptor, STR("invokeExact"),
      STR("(Ljava/lang/invoke/MethodHandle;[Ljava/lang/Object;)Ljava/lang/"
          "Object;"),
      true, false);

  bjvm_invokevirtual_signature_polymorphic(
      thread, fake_frame, invokeExact, m->ref->resolved_mt,
      (void *)bootstrap_handle->obj, static_i + 4);

  if (thread->current_exception) {
    result = -1;
    goto fail;
  }
  insn->ic = fake_frame->values[0].obj;

fail:
  free(fake_frame);

  bjvm_drop_handle(thread, bootstrap_handle);
  bjvm_drop_handle(thread, lookup_handle);

  return result;
}

int bjvm_invokedynamic(bjvm_thread *thread, bjvm_stack_frame *frame,
                       bjvm_bytecode_insn *insn) {

  bjvm_cp_indy_info *indy = &insn->cp->indy_info;
  if (!insn->ic) {
    if (indy_resolve(thread, insn, indy)) {
      return -1;
    }
  }
  assert(insn->ic);
  struct bjvm_native_CallSite *cs = insn->ic;
  struct bjvm_native_MethodHandle *mh = (void *)cs->target;

  bjvm_method_descriptor *d = indy->method_descriptor;
  bjvm_stack_frame *fake_frame =
      calloc(1, sizeof(bjvm_stack_frame) +
                    sizeof(bjvm_stack_value) * (d->args_count + 1));
  fake_frame->values[0].obj = (void *)mh;
  memcpy(fake_frame->values + 1,
         frame->values + frame->stack_depth - d->args_count,
         d->args_count * sizeof(bjvm_stack_value));
  fake_frame->max_stack = fake_frame->stack_depth = d->args_count + 1;

  bjvm_cp_method *invokeExact =
      bjvm_easy_method_lookup(mh->base.descriptor, STR("invokeExact"),
                              STR("(Ljava/lang/invoke/MethodHandle;[Ljava/lang/"
                                  "Object;)Ljava/lang/Object;"),
                              true, false);
  assert(invokeExact);
  bjvm_invokevirtual_signature_polymorphic(thread, fake_frame, invokeExact,
                                           (void *)mh->type, (void *)mh,
                                           d->args_count + 1);
  frame->stack_depth -= d->args_count;
  if (d->return_type.base_kind != BJVM_TYPE_KIND_VOID) {
    checked_push(frame, fake_frame->values[0]);
  }
  free(fake_frame);
  return thread->current_exception != nullptr;
}

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result) {
  bjvm_cp_method *method = frame->method;
  bjvm_bytecode_insn *code = method->code->code;

#if AGGRESSIVE_DEBUG
  printf("Calling method %.*s, descriptor %.*s, on class %.*s\n",
         fmt_slice(method->name), fmt_slice(method->descriptor),
         fmt_slice(method->my_class->name));
#endif

start:
  while (true) {
    bjvm_bytecode_insn *insn = &code[frame->program_counter];

#if AGGRESSIVE_DEBUG
    char *insn_dump = insn_to_string(insn, frame->program_counter);
    printf("Insn: %s\n", insn_to_string(insn, frame->program_counter));
    printf("Method: %.*s in class %.*s (%.*s:%d)\n", fmt_slice(method->name),
           fmt_slice(method->my_class->name),
           fmt_slice(method->my_class->source_file->name),
           bjvm_get_line_number(method->code, frame->program_counter));
    printf("FRAME:\n");
    dump_frame(stdout, frame);

    free(insn_dump);
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
                                            &&bjvm_insn_ret};
    goto *insn_jump_table[insn->kind];

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
      int len = *ArrayLength(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
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
      checked_push(frame, (bjvm_stack_value){.i = (int)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_d2l: {
      checked_push(frame,
                   (bjvm_stack_value){.l = (int64_t)checked_pop(frame).d});
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
      checked_push(frame, (bjvm_stack_value){.i = (int)a});
      NEXT_INSN;
    }
    bjvm_insn_f2l:
      checked_push(frame,
                   (bjvm_stack_value){.l = (int64_t)checked_pop(frame).f});
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
      *result = checked_pop(frame);
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
    bjvm_insn_pop:
      checked_pop(frame);
      NEXT_INSN;
    bjvm_insn_pop2:
      checked_pop(frame);
      checked_pop(frame);
      NEXT_INSN;
    bjvm_insn_return: { goto done; }
    bjvm_insn_saload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
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
      int count = checked_pop(frame).i;
      bjvm_cp_class_info *info = &insn->cp->class_info;

      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      assert(info->classdesc);

      if (count < 0) {
        bjvm_negative_array_size_exception(thread, count);
        goto done;
      }

      bjvm_obj_header *array =
          CreateObjectArray1D(thread, info->classdesc, count);
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
      bjvm_obj_header *obj = checked_pop(frame).obj;
      if (obj) {
        if (bjvm_instanceof(obj->descriptor, info->classdesc)) {
          checked_push(frame, (bjvm_stack_value){.obj = obj});
        } else {
          INIT_STACK_STRING(complaint, 1000);
          complaint = bprintf(complaint, "Expected instance of %.*s, got %.*s",
                              fmt_slice(info->classdesc->name),
                              fmt_slice(obj->descriptor->name));
          bjvm_raise_exception(thread, STR("java/lang/ClassCastException"),
                               complaint);
          goto done;
        }
      } else {
        checked_push(frame, (bjvm_stack_value){.obj = nullptr});
      }
      NEXT_INSN;
    }
    bjvm_insn_instanceof: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;

      bjvm_obj_header *obj = checked_pop(frame).obj;
      checked_push(
          frame, (bjvm_stack_value){.i = obj ? bjvm_instanceof(obj->descriptor,
                                                               info->classdesc)
                                             : 0});
      NEXT_INSN;
    }
    bjvm_insn_invokedynamic: {
      if (bjvm_invokedynamic(thread, frame, insn))
        goto done;
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
      // Create an instance of the class
      bjvm_obj_header *obj = new_object(thread, info->classdesc);
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_getfield:
    bjvm_insn_putfield: {
      bjvm_cp_field_info *field_info = &insn->cp->fieldref_info;
      int error = bjvm_resolve_field(thread, field_info);
      if (error || field_info->field->access_flags & BJVM_ACCESS_STATIC) {
        INIT_STACK_STRING(complaint, 1000);
        bprintf(complaint, "Expected nonstatic field %.*s on class %.*s",
                fmt_slice(field_info->nat->name),
                fmt_slice(field_info->class_info->name));

        bjvm_incompatible_class_change_error(thread, complaint);
        goto done;
      }
      bjvm_stack_value val;
      if (insn->kind == bjvm_insn_putfield)
        val = checked_pop(frame);
      bjvm_obj_header *obj = checked_pop(frame).obj;

      // TODO raise NPE

      void *addr = (void *)obj + field_info->field->byte_offset;

      bjvm_type_kind kind = field_to_kind(field_info->parsed_descriptor);
      if (insn->kind == bjvm_insn_getfield) {
        checked_push(frame, load_stack_value(addr, kind));
      } else {
        store_stack_value(addr, val, kind);
      }

      NEXT_INSN;
    }
    bjvm_insn_getstatic:
    bjvm_insn_putstatic: {
      bjvm_cp_field_info *field_info = &insn->cp->fieldref_info;
      if (!field_info->field) {
        bjvm_cp_class_info *class = field_info->class_info;

        int error = bjvm_resolve_class(thread, class);
        if (error)
          goto done;

        error = bjvm_initialize_class(thread, class->classdesc);
        if (error)
          goto done;
        bjvm_cp_field *field =
            bjvm_field_lookup(class->classdesc, field_info->nat->name,
                              field_info->nat->descriptor);
        field_info->field = field;
        if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
          INIT_STACK_STRING(complaint, 1000);
          bprintf(complaint, "Expected static field %.*s on class %.*s",
                  fmt_slice(field_info->nat->name),
                  fmt_slice(field_info->class_info->name));
          bjvm_incompatible_class_change_error(thread, complaint);
          goto done;
        }
      }

      bjvm_cp_field *field = field_info->field;
      void *field_location =
          &field->my_class->static_fields[field->byte_offset];
      bjvm_type_kind kind = field_to_kind(field_info->parsed_descriptor);
      if (insn->kind == bjvm_insn_putstatic) {
        store_stack_value(field_location, checked_pop(frame), kind);
      } else {
        checked_push(frame, load_stack_value(field_location, kind));
      }

      NEXT_INSN;
    }
    bjvm_insn_invokespecial:
    bjvm_insn_invokeinterface:
    bjvm_insn_invokevirtual: {
      if (bjvm_invokenonstatic(thread, frame, insn))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_invokestatic: {
      if (bjvm_invokestatic(thread, frame, insn))
        goto done;
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
      if (bjvm_multianewarray(thread, frame, &insn->multianewarray))
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
      } else { // failed to create array
        /// TODO: throw OOM
        goto done;
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
    }

    frame->program_counter++;
  }

done:;
  if (thread->current_exception != nullptr) {
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
          if (!ent.catch_type ||
              bjvm_instanceof(thread->current_exception->descriptor,
                              ent.catch_type->classdesc)) {
            frame->program_counter = ent.handler_pc;
            frame->stack_depth = 1;
            frame->values[0] =
                (bjvm_stack_value){.obj = thread->current_exception};
            thread->current_exception = nullptr;

            goto start;
          }
        }
      }
    }

    return -1;
  }

  return 0;
}

int bjvm_get_line_number(const bjvm_attribute_code *code, uint16_t pc) {
  bjvm_attribute_line_number_table *table = code->line_number_table;
  if (!table || pc >= code->insn_count)
    return -1;
  // Look up original PC (the instruction is tagged with it)
  int original_pc = code->code[pc].original_pc;
  int low = 0, high = table->entry_count - 1;
  while (low <= high) { // binary search for first entry with start_pc <= pc
    int mid = (low + high) / 2;
    bjvm_line_number_table_entry *entry = &table->entries[mid];
    if (entry->start_pc <= original_pc &&
        (mid == table->entry_count - 1 ||
         table->entries[mid + 1].start_pc > original_pc)) {
      return entry->line;
    }
    if (entry->start_pc < original_pc) {
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

    bjvm_cp_method *init = bjvm_easy_method_lookup(ThreadGroup, STR("<init>"),
                                                   STR("()V"), false, false);

    assert(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    bjvm_thread_run(thread, init, args, nullptr);
  }
  return vm->main_thread_group;
}

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
      *VECTOR_PUSH(ctx->roots, ctx->roots_count, ctx->roots_cap) =             \
          (bjvm_obj_header **)v;                                               \
    }                                                                          \
  }

void enumerate_reflection_roots(bjvm_gc_ctx *ctx, bjvm_classdesc *desc) {
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
      if (desc->pool->entries[i].kind == BJVM_CP_KIND_METHOD_HANDLE) {
        PUSH_ROOT(&desc->pool->entries[i].method_handle.resolved_mt);
      }
    }
  }

  // Push all CallSite objects
  for (int i = 0; i < desc->indy_insns_count; ++i) {
    bjvm_bytecode_insn *insn = *(desc->indy_insns + i);
    PUSH_ROOT(&insn->ic);
  }
}

void bjvm_major_gc_enumerate_gc_roots(bjvm_gc_ctx *ctx) {
  bjvm_vm *vm = ctx->vm;
  if (vm->primitive_classes[0]) {
    for (int i = 0; i < lengthof(vm->primitive_classes); ++i) {
      enumerate_reflection_roots(ctx, vm->primitive_classes[i]);
    }
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

  // Stack and local variables on active threads
  for (int thread_i = 0; thread_i < vm->active_thread_count; ++thread_i) {
    bjvm_thread *thr = vm->active_threads[thread_i];
    PUSH_ROOT(&thr->thread_obj);
    PUSH_ROOT(&thr->current_exception);

    for (int frame_i = 0; frame_i < thr->frames_count; ++frame_i) {
      bjvm_stack_frame *frame = thr->frames[frame_i];
      bjvm_code_analysis *analy = frame->method->code_analysis;
      bjvm_compressed_bitset refs =
          analy->insn_index_to_references[frame->program_counter];
      bitset_list = bjvm_list_compressed_bitset_bits(
          refs, bitset_list, &bs_list_len, &bs_list_cap);
      for (int i = 0; i < bs_list_len; ++i) {
        PUSH_ROOT(&frame->values[bitset_list[i]].obj);
      }
    }

    // Non-null local handles
    for (int i = 0; i < thr->handles_capacity; ++i) {
      PUSH_ROOT(&thr->handles[i].obj);
    }
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

int in_heap(bjvm_gc_ctx *ctx, bjvm_obj_header *field) {
  return (uintptr_t)field - (uintptr_t)ctx->vm->heap < ctx->vm->heap_capacity;
}

void bjvm_mark_reachable(bjvm_gc_ctx *ctx, bjvm_obj_header *obj, int **bitsets,
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

int comparator(const void *a, const void *b) {
  return *(bjvm_obj_header **)a - *(bjvm_obj_header **)b;
}

size_t size_of_object(bjvm_obj_header *obj) {
  if (obj->descriptor->kind == BJVM_CD_KIND_ORDINARY) {
    return obj->descriptor->data_bytes;
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

void relocate_object(const bjvm_gc_ctx *ctx, bjvm_obj_header **obj) {
  if (!*obj)
    return;

  // Binary search for obj in ctx->roots
  bjvm_obj_header **found = (bjvm_obj_header **)bsearch(
      obj, ctx->objs, ctx->objs_count, sizeof(bjvm_obj_header *), comparator);
  if (found) {
    // printf("RELOCATING %p to %p\n", *obj, ctx->new_location[found -
    // ctx->roots]);
    *obj = ctx->new_location[found - ctx->objs];
  } else {
  }
}

void relocate_static_fields(bjvm_gc_ctx *ctx) {
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
  printf("GCing!\n");
  // TODO wait for all threads to get ready (for now we'll just call this from
  // an already-running thread)
  bjvm_gc_ctx ctx = {.vm = vm};
  bjvm_major_gc_enumerate_gc_roots(&ctx);

  // Mark phase
  int *bitset_list[1000] = {nullptr}, capacity[1000] = {0};
  for (int i = 0; i < ctx.roots_count; ++i) {
    bjvm_obj_header *root = *ctx.roots[i];
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
  uint8_t *new_heap = aligned_alloc(4096, vm->heap_capacity),
          *end = new_heap + vm->heap_capacity;
  uint8_t *write_ptr = new_heap;

  // Copy object by object
  for (int i = 0; i < ctx.objs_count; ++i) {
    // Align to 8 bytes
    write_ptr = (uint8_t *)((uintptr_t)write_ptr + 7 & ~7);
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
  vm->heap_used = write_ptr - new_heap;
  vm->heap_used = (vm->heap_used + 7) & ~7;
}