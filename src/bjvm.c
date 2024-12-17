#define AGGRESSIVE_DEBUG 0

#define CACHE_INVOKESTATIC 0
#define CACHE_INVOKENONSTATIC 0
#define ONE_GOTO_PER_INSN 0

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <stddef.h>

#include "util.h"

#include "analysis.h"
#include "bjvm.h"
#include <math.h>

struct classfile_entry {
  size_t len;
  uint8_t* data;
};

struct classfile_entry* make_cf_entry(const uint8_t* bytes, size_t len) {
  struct classfile_entry *result = malloc(sizeof(struct classfile_entry));
  result->data = malloc(result->len = len);
  memcpy(result->data, bytes, len);
  return result;
}

void free_cf_entry(void* entry_) {
  if (!entry_)
    return;
  struct classfile_entry *entry = entry_;
  free(entry->data);
  free(entry);
}

#define MAX_CF_NAME_LENGTH 1000

int add_classfile_bytes(bjvm_vm *vm,
                        const wchar_t *filename, size_t filename_length,
                        const uint8_t *bytes, size_t len) {
  if (filename_length > MAX_CF_NAME_LENGTH)
    return -1;

  struct classfile_entry *entry = make_cf_entry(bytes, len);
  free_cf_entry(bjvm_hash_table_insert(&vm->classfiles, filename, filename_length, entry));
  return 0;
}

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity) {
  bjvm_string_hash_table table;
  table.free_fn = free_fn;
  table.entries = calloc(initial_capacity, sizeof(bjvm_hash_table_entry));
  table.entries_count = 0;
  table.entries_cap = initial_capacity;
  table.load_factor = load_factor;
  return table;
}

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl) {
  bjvm_hash_table_iterator iter;
  iter.current_base = tbl->entries;
  bjvm_hash_table_entry *end = tbl->entries + tbl->entries_cap;
  // advance to first nonzero entry
  while (iter.current_base < end && iter.current_base->key == NULL)
    iter.current_base++;
  iter.current = iter.current_base;
  iter.end = end;
  return iter;
}

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       wchar_t **key, size_t *key_len,
                                       void **value) {
  if (iter.current != iter.end) {
    *key = iter.current->key;
    *key_len = iter.current->key_len;
    *value = iter.current->data;
    return true;
  }
  return false;
}

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter) {
  if (iter->current_base == iter->end)
    return false;
  if (iter->current->next) {
    iter->current = iter->current->next;
    return true;
  }
  // advance base until a non-null key
  while (++iter->current_base < iter->end && iter->current_base->key == NULL)
    ;
  iter->current = iter->current_base;
  return iter->current_base != iter->end;
}

uint32_t bjvm_hash_string(const wchar_t *key, size_t len) {
  uint64_t hash = 0;
  for (size_t i = 0; i < len; ++i) {
    hash = 31 * hash + key[i]; // yk what I mean ;)
  }
  return hash;
}

bjvm_hash_table_entry *
bjvm_find_hash_table_entry(bjvm_string_hash_table *tbl, const wchar_t *key,
                           size_t len, bool *equal, bool *on_chain,
                           bjvm_hash_table_entry **prev_entry) {
  uint32_t hash = bjvm_hash_string(key, len);
  size_t index = hash % tbl->entries_cap;
  bjvm_hash_table_entry *ent = &tbl->entries[index], *prev = NULL;
  while (ent) {
    *on_chain = prev != NULL;
    if (ent->key && ent->key_len == len && wmemcmp(ent->key, key, len) == 0) {
      *equal = true;
      *prev_entry = prev;
      return ent;
    }
    if (!ent->next) {
      *equal = false;
      *prev_entry = prev;
      return ent;
    }
    prev = ent;
    ent = ent->next;
  }
  *equal = false;
  *on_chain = true;
  if (prev_entry)
    *prev_entry = prev;
  return prev;
}

void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? wcslen(key) : len;
  bjvm_hash_table_entry *prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
  if (!equal)
    return NULL;
  tbl->entries_count--;
  void *ret_val = ent->data;
  free(ent->key);
  ent->key = NULL;
  if (prev) {
    prev->next = ent->next;
    free(ent);
  } else if (ent->next) {
    prev = ent->next;
    *ent = *ent->next;
    free(prev);
  }
  return ret_val;
}

void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, wchar_t *key,
                                  int len, void *value, bool copy_key) {
  len = len == -1 ? wcslen(key) : len;
  bool equal, on_chain;
  if (tbl->entries_count + 1 >= tbl->load_factor * tbl->entries_cap) {
    bjvm_hash_table_rehash(tbl, tbl->entries_cap * 2);
  }

  bjvm_hash_table_entry *_prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  if (equal) {
    void *ret_val = ent->data;
    ent->data = value;
    if (!copy_key)
      free(key);
    return ret_val;
  }
  if (on_chain || ent->key != NULL) {
    ent->next = malloc(sizeof(bjvm_hash_table_entry));
    ent = ent->next;
  }
  ent->next = NULL;
  ent->data = value;
  if (copy_key) {
    wchar_t *new_key = malloc(len * sizeof(wchar_t));
    wmemcpy(new_key, key, len);
    ent->key = new_key;
  } else {
    ent->key = key;
  }
  ent->key_len = len;
  tbl->entries_count++;
  return NULL;
}

void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len, void *value) {
  return bjvm_hash_table_insert_impl(tbl, (wchar_t *)key /* key copied */, len,
                                     value, true);
}

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity) {
  bjvm_string_hash_table new_table =
      bjvm_make_hash_table(tbl->free_fn, tbl->load_factor, new_capacity);
  bjvm_hash_table_iterator iter = bjvm_hash_table_get_iterator(tbl);
  wchar_t *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(iter, &key, &len, &value)) {
    bjvm_hash_table_insert_impl(&new_table, key, len, value,
                                false /* don't copy key */);
    bjvm_hash_table_entry *ent = iter.current;
    bool entry_on_chain = iter.current != iter.current_base;
    bjvm_hash_table_iterator_next(&iter);
    if (entry_on_chain)
      free(ent);
  }
  free(tbl->entries); // Don't need to free the linked lists, keys etc. as they
                      // were moved over
  *tbl = new_table;
}

void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? wcslen(key) : len;
  bjvm_hash_table_entry *_prev,
      *entry =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  return equal ? entry->data : NULL;
}

void bjvm_free_hash_table(bjvm_string_hash_table tbl) {
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&tbl);
  wchar_t *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(it, &key, &len, &value)) {
    bjvm_hash_table_entry *ent = it.current;
    bool needs_free = it.current != it.current_base;
    free(key);
    if (tbl.free_fn)
      tbl.free_fn(value);
    bjvm_hash_table_iterator_next(&it);
    if (needs_free)
      free(ent);
  }
  free(tbl.entries);
  tbl.entries_cap = tbl.entries_count = 0; // good form
}

bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method) {
  assert(method != NULL);

  const bjvm_attribute_code *code = method->code;
  assert(code);

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  assert(header_bytes % 8 == 0);

  size_t local_bytes =
      ((int)code->max_locals + code->max_stack) * sizeof(bjvm_stack_value);
  size_t total = header_bytes + local_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    //bjvm_raise_exception_object(thread, thread->stack_overflow_error);
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

char *stack_value_to_string(bjvm_stack_value value) {
  char buf[1000];
  sprintf(buf, "[ ref = %p, int = %d ]", value.obj, value.i);
  return strdup(buf);
}

char *dump_frame(const bjvm_stack_frame *frame) {
  char buf[2000] = {0}, *write = buf, *end = buf + sizeof(buf);

  for (int i = 0; i < frame->stack_depth; ++i) {
    char *stack_val = stack_value_to_string(frame->values[i]);
    write += snprintf(write, end - write, " stack[%d] = %s\n", i, stack_val);
    free(stack_val);
  }

  for (int i = 0; i < frame->max_locals; ++i) {
    char *stack_val =
        stack_value_to_string(frame->values[i + frame->max_stack]);
    write += snprintf(write, end - write, "locals[%d] = %s\n", i, stack_val);
    free(stack_val);
  }

  return strdup(buf);
}

void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference) {
  assert(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  assert(reference == NULL || reference == frame);
  thr->frames_count--;
  thr->frame_buffer_used =
      thr->frames_count == 0 ? 0 : (uint8_t *)frame - thr->frame_buffer;
}

void free_array_classdesc(bjvm_array_classdesc *classdesc) {
  if (classdesc->base.array_type) {
    classdesc->base.array_type = NULL;
    free_array_classdesc((bjvm_array_classdesc *)classdesc->base.array_type);
  }
  free(classdesc);
}

void free_primitive_arr_classdesc(bjvm_primitive_array_classdesc *classdesc);

void free_classdesc(void *classdesc_) {
  bjvm_classdesc *classdesc = classdesc_;
  if (classdesc->kind == BJVM_CD_KIND_ORDINARY) {
    bjvm_free_classfile(*classdesc);
    free(classdesc_);
  } else if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
    free_array_classdesc((bjvm_array_classdesc *)classdesc);
  } else {
    free_primitive_arr_classdesc((bjvm_primitive_array_classdesc *)classdesc);
  }
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

  native_entries *entries = entries_;
  for (int i = 0; i < entries->entries_count; i++) {
    free_utf8(entries->entries[i].name);
    free_utf8(entries->entries[i].descriptor);
  }
  free(entries);
}

void bjvm_register_native(bjvm_vm *vm, const char *class_name,
                          const char *method_name,
                          const char *method_descriptor,
                          bjvm_native_callback callback) {
  bjvm_utf8 class = bjvm_make_utf8_cstr(class_name);

  native_entries *existing =
      bjvm_hash_table_lookup(&vm->natives, class.chars, class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)bjvm_hash_table_insert(&vm->natives, class.chars, class.len,
                                 existing);
  }
  free_utf8(class);

  native_entry *ent = VECTOR_PUSH(existing->entries, existing->entries_count,
                                  existing->entries_cap);
  ent->name = bjvm_make_utf8_cstr(method_name);
  ent->descriptor = bjvm_make_utf8_cstr(method_descriptor);
  ent->callback = callback;
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 *name,
                                   const bjvm_utf8 *method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces);

// Generally, use this to indicate that a native function is returning void or null
bjvm_stack_value value_null() {
  return (bjvm_stack_value) { .obj = NULL };
}

bjvm_stack_value unimplemented_native(bjvm_thread *, bjvm_obj_header *, bjvm_stack_value *,
                         int) {
  return value_null();
}

// Raise an UnsatisfiedLinkError relating to the given method.
void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method) {
  wchar_t err[1000] = {0};
  swprintf(err, 1000, L"Method %S on class %S with descriptor %S",
           method->name->chars, method->my_class->name.chars,
           method->descriptor->chars);
  bjvm_raise_exception(thread, L"java/lang/UnsatisfiedLinkError", err);
}

// Raise a NegativeArraySizeException with the given count value.
void bjvm_negative_array_size_exception(bjvm_thread *thread, int count) {
  wchar_t err[12] = { 0 };
  swprintf(err, 12, L"%d", count);
  bjvm_raise_exception(thread, L"java/lang/NegativeArraySizeException", err);
}

// Raise a NullPointerException.
void bjvm_null_pointer_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, L"java/lang/NullPointerException", NULL);
}

// Raise an ArrayStoreException.
void bjvm_array_store_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, L"java/lang/ArrayStoreException", NULL);
}

// Raise an IncompatibleClassChangeError.
void bjvm_incompatible_class_change_error(bjvm_thread *thread, const wchar_t *complaint) {
  bjvm_raise_exception(thread, L"java/lang/IncompatibleClassChangeError", complaint);
}

// Raise an AbstractMethodError.
void bjvm_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method* method) {
  wchar_t complaint[1000];
  swprintf(complaint, 1000, L"Abstract method '%S' on class %S", method->name->chars, method->my_class->name.chars);
  bjvm_raise_exception(thread, L"java/lang/AbstractMethodError", complaint);
}

// Raise an ArithmeticException.
void bjvm_arithmetic_exception(bjvm_thread *thread, const wchar_t* complaint) {
  bjvm_raise_exception(thread, L"java/lang/ArithmeticException", complaint);
}

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
void bjvm_array_index_oob_exception(bjvm_thread *thread, int index, int length) {
  wchar_t complaint[80];
  swprintf(complaint, 80, L"Index %d out of bounds for array of length %d", index, length);
  bjvm_raise_exception(thread, L"java/lang/ArrayIndexOutOfBoundsException", complaint);
}

void read_string(bjvm_obj_header *obj, short **buf, size_t *len) {
  assert(utf8_equals(&obj->descriptor->name, "java/lang/String"));
  bjvm_obj_header *array = ((struct bjvm_native_String*)obj)->value;
  *buf = array_data(array);
  *len = *array_length(array);
}

// TODO read the properties from the VM instead of hardcoding them
bjvm_stack_value bjvm_System_initProperties(bjvm_thread *thread, bjvm_obj_header *,
                               bjvm_stack_value *args, int) {
  bjvm_obj_header *props_obj = args[0].obj;
  const wchar_t *const props[][2] = {
      {L"file.encoding", L"UTF-8"},   {L"stdout.encoding", L"UTF-8"},
      {L"native.encoding", L"UTF-8"}, {L"stderr.encoding", L"UTF-8"},
      {L"line.separator", L"\n"},     {L"path.separator", L":"},
      {L"file.separator", L"/"}};
  bjvm_cp_method *put = bjvm_easy_method_lookup(
      props_obj->descriptor, "put",
      "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", true, false);
  for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); ++i) {
    bjvm_stack_value put_args[3] = {
        {.obj = props_obj},
        {.obj = bjvm_intern_string(thread, props[i][0], wcslen(props[i][0]))},
        {.obj = bjvm_intern_string(thread, props[i][1], wcslen(props[i][1]))}};
    bjvm_stack_value result;
    // call put() with String key and value
    bjvm_thread_run(thread, put, put_args, &result);
  }
  return value_null();
}

bjvm_stack_value bjvm_System_mapLibraryName(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int) {
  return args[0];
}

void *array_data(bjvm_obj_header *array);
bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *classdesc);

bool is_1d_primitive_array(bjvm_obj_header * src) {
  return src->descriptor->kind != BJVM_CD_KIND_ORDINARY_ARRAY &&
    ((bjvm_primitive_array_classdesc*) src->descriptor)->dimensions == 1;
}

bjvm_stack_value bjvm_System_arraycopy(bjvm_thread* thread, bjvm_obj_header*,
                          bjvm_stack_value *args, int argc) {
  assert(argc == 5);
  bjvm_obj_header *src = args[0].obj;
  bjvm_obj_header *dest = args[2].obj;
  if (src == NULL || dest == NULL) {
    bjvm_null_pointer_exception(thread);
    return value_null();
  }
  if (src->descriptor->kind == BJVM_CD_KIND_ORDINARY || dest->descriptor->kind == BJVM_CD_KIND_ORDINARY) {
    // Can't copy non-array objects to each other
    bjvm_array_store_exception(thread);
    return value_null();
  }
  bool src_is_1d_primitive = is_1d_primitive_array(src), dst_is_1d_primitive = is_1d_primitive_array(dest);
  if (src_is_1d_primitive != dst_is_1d_primitive) {
    bjvm_array_store_exception(thread);
    return value_null();
  }

  int src_pos = args[1].i;
  int dest_pos = args[3].i;
  int length = args[4].i;
  int src_length = *array_length(src);
  int dest_length = *array_length(dest);
  // Verify that everything is in bounds
  // TODO add more descriptive error messages
  if (src_pos < 0 || dest_pos < 0 || length < 0 || (int64_t)src_pos + length > src_length || (int64_t)dest_pos + length > dest_length) {
    bjvm_raise_exception(thread, L"java/lang/ArrayIndexOutOfBoundsException", NULL);
    return value_null();
  }

#define GEN_PRIMITIVE_IMPL(array_type, underlying)                                       \
  if (src->descriptor->kind == BJVM_CD_KIND_##array_type) {                    \
    underlying *src_data = array_data(src);                                    \
    underlying *dest_data = array_data(dest);                                  \
    for (int i = 0; i < length; ++i)                                           \
      dest_data[dest_pos + i] = src_data[src_pos + i];                         \
    return value_null();                                                               \
  }

  if (src_is_1d_primitive) {
    GEN_PRIMITIVE_IMPL(BYTE_ARRAY, int8_t)
    GEN_PRIMITIVE_IMPL(CHAR_ARRAY, uint16_t)
    GEN_PRIMITIVE_IMPL(DOUBLE_ARRAY, double)
    GEN_PRIMITIVE_IMPL(FLOAT_ARRAY, float)
    GEN_PRIMITIVE_IMPL(INT_ARRAY, int32_t)
    GEN_PRIMITIVE_IMPL(LONG_ARRAY, int64_t)
    GEN_PRIMITIVE_IMPL(SHORT_ARRAY, int16_t)
    GEN_PRIMITIVE_IMPL(BOOLEAN_ARRAY, uint8_t)
  }

  // If the component type of the src class is an instanceof the destination class, then we don't
  // need to perform any checks. Otherwise, we need to perform an instanceof check on each element
  // and raise an ArrayStoreException as appropriate.
  if (bjvm_instanceof(src->descriptor->one_fewer_dim, dest->descriptor->one_fewer_dim)) {
    memcpy(array_data(dest) + dest_pos, array_data(src) + src_pos, length * sizeof(void *));
    return value_null();
  }

  for (int i = 0; i < length; ++i) {
    bjvm_obj_header *src_elem = ((bjvm_obj_header **)array_data(src))[src_pos + i];
    if (src_elem && !bjvm_instanceof(src_elem->descriptor, dest->descriptor->one_fewer_dim)) {
      bjvm_array_store_exception(thread);
      return value_null();
    }
    ((bjvm_obj_header **)array_data(dest))[dest_pos + i] = src_elem;
  }

  return value_null();
}

bjvm_stack_value bjvm_System_setOut(bjvm_thread *thread, bjvm_obj_header *,
                       bjvm_stack_value *args, int) {
  // Look up the field System.out
  bjvm_classdesc *system_class =
      bootstrap_class_create(thread, L"java/lang/System");
  bjvm_cp_field *out_field =
      bjvm_easy_field_lookup(system_class, L"out", L"Ljava/io/PrintStream;");

  void *field = &system_class->static_fields[out_field->byte_offset];
  *(bjvm_obj_header **)field = args[0].obj;

  return value_null();
}

bjvm_stack_value bjvm_Object_clone(bjvm_thread *thread, bjvm_obj_header *obj,
                      bjvm_stack_value *, int) {
  switch (obj->descriptor->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY: {
    bjvm_array_classdesc *array_desc = (bjvm_array_classdesc *)obj->descriptor;
    bjvm_obj_header *new_array = create_object_array(
        thread, array_desc->base.base_component, *array_length(obj));
    if (new_array) {
      memcpy(array_data(new_array), array_data(obj),
             *array_length(obj) * sizeof(void *));
    }
    return (bjvm_stack_value) { .obj = new_array };
  }
  case BJVM_CD_KIND_BYTE_ARRAY:
    break;
  case BJVM_CD_KIND_CHAR_ARRAY:
    break;
  case BJVM_CD_KIND_DOUBLE_ARRAY:
    break;
  case BJVM_CD_KIND_FLOAT_ARRAY:
    break;
  case BJVM_CD_KIND_INT_ARRAY:
    break;
  case BJVM_CD_KIND_LONG_ARRAY:
    break;
  case BJVM_CD_KIND_SHORT_ARRAY:
    break;
  case BJVM_CD_KIND_BOOLEAN_ARRAY:
    break;
  case BJVM_CD_KIND_ORDINARY:
    break;
  }
  UNREACHABLE();  // TODO implement
  return value_null();
}

// Get the default hash code of an Object, which is stored in its mark word
bjvm_stack_value bjvm_Object_hashCode(bjvm_thread *, bjvm_obj_header *obj,
                         bjvm_stack_value *, int) {
  return (bjvm_stack_value) { .i = (int)obj->mark_word };
}

bjvm_stack_value bjvm_Class_getPrimitiveClass(bjvm_thread * thread, bjvm_obj_header *,
                                 bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  short *chars;
  size_t len;
  read_string(args[0].obj, &chars, &len);
  if (len > 10) {
    return value_null();
  }
  char as_cstr[11] = {0};
  for (size_t i = 0; i < len; ++i) {
    as_cstr[i] = chars[i];
  }
  bjvm_type_kind kind;
  if (strcmp(as_cstr, "boolean") == 0) {
    kind = BJVM_TYPE_KIND_BOOLEAN;
  } else if (strcmp(as_cstr, "byte") == 0) {
    kind = BJVM_TYPE_KIND_BYTE;
  } else if (strcmp(as_cstr, "char") == 0) {
    kind = BJVM_TYPE_KIND_CHAR;
  } else if (strcmp(as_cstr, "short") == 0) {
    kind = BJVM_TYPE_KIND_SHORT;
  } else if (strcmp(as_cstr, "int") == 0) {
    kind = BJVM_TYPE_KIND_INT;
  } else if (strcmp(as_cstr, "long") == 0) {
    kind = BJVM_TYPE_KIND_LONG;
  } else if (strcmp(as_cstr, "float") == 0) {
    kind = BJVM_TYPE_KIND_FLOAT;
  } else if (strcmp(as_cstr, "double") == 0) {
    kind = BJVM_TYPE_KIND_DOUBLE;
  } else if (strcmp(as_cstr, "void") == 0) {
    kind = BJVM_TYPE_KIND_VOID;
  } else {
    return value_null();
  }

  return (bjvm_stack_value) { .obj = (void*)bjvm_primitive_class_mirror(thread, kind) };
}

bjvm_stack_value bjvm_Class_getComponentType(bjvm_thread *, bjvm_obj_header * obj,
                                 bjvm_stack_value*, int) {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  if (desc->kind == BJVM_CD_KIND_ORDINARY) {
    return value_null();
  }
  return  (bjvm_stack_value) { .obj = (void*)desc->one_fewer_dim->mirror };
}

bjvm_stack_value bjvm_Class_getModifiers(bjvm_thread *, bjvm_obj_header *obj,
                            bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = classdesc->access_flags };
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                       bjvm_classdesc *classdesc);

bjvm_stack_value bjvm_Object_getClass(bjvm_thread *thread, bjvm_obj_header *obj,
                         bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .obj = (void*)bjvm_get_class_mirror(thread, obj->descriptor) };
}

bjvm_stack_value bjvm_Class_getSuperclass(bjvm_thread *, bjvm_obj_header *obj,
                             bjvm_stack_value *, int) {

  return value_null(); // TODO
}

bjvm_stack_value bjvm_Class_getClassLoader(bjvm_thread *, bjvm_obj_header *,
                              bjvm_stack_value *, int) {
  return value_null();  // TODO
}

bjvm_obj_header *make_string(bjvm_thread *thread, const wchar_t *chars, int len);

bjvm_stack_value bjvm_Class_getName(bjvm_thread *thread, bjvm_obj_header *obj,
                       bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .obj = bjvm_intern_string(thread, classdesc->name.chars, classdesc->name.len) };
}

// Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/Class; static
bjvm_stack_value bjvm_Class_forName(bjvm_thread *thread, bjvm_obj_header *,
                       bjvm_stack_value *args, int) {
  // Read args[0] as a string
  bjvm_obj_header *name_obj = args[0].obj;
  short *name;
  size_t len;
  read_string(name_obj, &name, &len);
  wchar_t *name_wchar = calloc(len + 1, sizeof(wchar_t));
  for (size_t i = 0; i < len; ++i) {
    name_wchar[i] = name[i] == '.' ? '/' : name[i];
  }
  bjvm_classdesc *c = bootstrap_class_create(thread, name_wchar);
  if (c) {
    return (bjvm_stack_value){.obj = (void*)bjvm_get_class_mirror(thread, c)};
  }
  return value_null();
}

bjvm_stack_value bjvm_Class_desiredAssertionStatus(bjvm_thread *, bjvm_obj_header *,
                                      bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 };  // TODO add thread option
}

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread,
                                               const wchar_t *chars) {
  if (chars[0] == 'L') {
    wchar_t *cow = wcsdup(chars);
    cow[wcslen(chars) - 1] = L'\0';
    return bootstrap_class_create(thread, cow + 1);
  }
  if (chars[0] == '[')
    return bootstrap_class_create(thread, chars);
  switch (chars[0]) {
  case 'Z':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_BOOLEAN)->reflected_class;
  case 'B':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_BYTE)->reflected_class;
  case 'C':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_CHAR)->reflected_class;
  case 'S':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_SHORT)->reflected_class;
  case 'I':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_INT)->reflected_class;
  case 'J':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_LONG)->reflected_class;
  case 'F':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_FLOAT)->reflected_class;
  case 'D':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_DOUBLE)->reflected_class;
  case 'V':
    return bjvm_primitive_class_mirror(thread, BJVM_TYPE_KIND_VOID)->reflected_class;
  }
  UNREACHABLE();
}

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field) {
  bjvm_classdesc *reflect_Field =
      bootstrap_class_create(thread, L"java/lang/reflect/Field");
  bjvm_initialize_class(thread, reflect_Field);
  struct bjvm_native_Field* result = field->reflection_field =
      (void*)new_object(thread, reflect_Field);

  result->reflected_field = field;
  result->name = bjvm_intern_string(thread, field->name->chars, field->name->len);
  result->clazz = (void*)bjvm_get_class_mirror(thread, classdesc);
  result->type = (void*)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, field->descriptor->chars));
  result->modifiers = field->access_flags;
}

void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method) {
  assert(utf8_equals(method->name, "<init>"));

  bjvm_classdesc *reflect_Constructor =
      bootstrap_class_create(thread, L"java/lang/reflect/Constructor");
  bjvm_initialize_class(thread, reflect_Constructor);

  struct bjvm_native_Constructor *result = method->reflection_ctor =
      (void*)new_object(thread, reflect_Constructor);
  result->reflected_ctor = method;
  result->clazz = (void*)bjvm_get_class_mirror(thread, classdesc);
  result->modifiers = method->access_flags;

  // TODO fill these in
  result->parameterTypes = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/Class"),
      method->parsed_descriptor->args_count);
}

bjvm_stack_value bjvm_Class_getDeclaredFields(bjvm_thread *thread, bjvm_obj_header *obj,
                                 bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  bjvm_stack_value ret;
  ret.obj = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/reflect/Field"),
      classdesc->fields_count);

  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_reflect_initialize_field(thread, classdesc, classdesc->fields + i);
    *((struct bjvm_native_Field**)array_data(ret.obj) + i) =
        classdesc->fields[i].reflection_field;
  }
  return ret;
}

bjvm_stack_value bjvm_Class_getDeclaredConstructors(bjvm_thread *thread,
                                       bjvm_obj_header *obj,
                                       bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);

  int count = 0;
  for (int i = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      bjvm_reflect_initialize_constructor(thread, classdesc,
                                          classdesc->methods + i);
      ++count;
    }
  }

  bjvm_stack_value ret;
  ret.obj = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/reflect/Constructor"),
      count);
  for (int i = 0, j = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      *((struct bjvm_native_Constructor **)array_data(ret.obj) + j++) =
          classdesc->methods[i].reflection_ctor;
    }
  }
  return ret;
}

bjvm_stack_value bjvm_Class_isAssignableFrom(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 }; // TODO
}

bjvm_stack_value bjvm_Class_isArray(bjvm_thread *, bjvm_obj_header * obj,
                                bjvm_stack_value *, int) {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = desc->kind != BJVM_CD_KIND_ORDINARY };
}

bjvm_stack_value bjvm_Float_floatToRawIntBits(bjvm_thread *, bjvm_obj_header *,
                                 bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .i = args[0].i };
}

bjvm_stack_value bjvm_Double_doubleToRawLongBits(bjvm_thread *, bjvm_obj_header *,
                                    bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .l = args[0].l };
}

bjvm_stack_value bjvm_Unsafe_arrayBaseOffset(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 24 };
}

// (Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z
bjvm_stack_value bjvm_Unsafe_compareAndSwapObject(bjvm_thread *, bjvm_obj_header *,
                                     bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 }; // TODO
}

bjvm_stack_value bjvm_Unsafe_objectFieldOffset(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  bjvm_cp_field *reflect_field = *bjvm_unmirror_field(args[0].obj);
  return  (bjvm_stack_value) { .l = reflect_field->byte_offset };
}

bjvm_stack_value bjvm_Unsafe_getIntVolatile(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int argc) {
  assert(argc == 2);
  return  (bjvm_stack_value) { .i = *(int *)((char *)args[0].obj + args[1].l) };
}

// (Ljava/lang/Object;JII)Z
bjvm_stack_value bjvm_Unsafe_compareAndSwapInt(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *args, int argc) {
  assert(argc == 4);
  bjvm_obj_header *obj = args[0].obj;
  int64_t offset = args[1].l;
  int expected = args[2].i, update = args[3].i;
  int ret = __sync_bool_compare_and_swap((int *)((char *)obj + offset), expected,
                                        update);
  return  (bjvm_stack_value) { .i = ret };
}

bjvm_stack_value bjvm_Unsafe_arrayIndexScale(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  bjvm_classdesc *desc = bjvm_unmirror_class(args[0].obj);
  switch (desc->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY:
    return  (bjvm_stack_value) { .i = sizeof(void *) };
  case BJVM_CD_KIND_BYTE_ARRAY:
  case BJVM_CD_KIND_CHAR_ARRAY:
  case BJVM_CD_KIND_FLOAT_ARRAY:
  case BJVM_CD_KIND_SHORT_ARRAY:
  case BJVM_CD_KIND_BOOLEAN_ARRAY:
  case BJVM_CD_KIND_INT_ARRAY:
    return  (bjvm_stack_value) { .i = 4 };
  case BJVM_CD_KIND_DOUBLE_ARRAY:
  case BJVM_CD_KIND_LONG_ARRAY:
    return  (bjvm_stack_value) { .i = 8 };
  case BJVM_CD_KIND_ORDINARY:
  default:  // invalid
    return  (bjvm_stack_value) { .i = 0 };
  }
}

bjvm_stack_value bjvm_Unsafe_addressSize(bjvm_thread *, bjvm_obj_header *,
                            bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = sizeof(void*) };
}

bjvm_stack_value bjvm_Unsafe_allocateMemory(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return  (bjvm_stack_value) { .l = (int64_t)malloc(args[0].l) };
}

bjvm_stack_value bjvm_Unsafe_freeMemory(bjvm_thread *, bjvm_obj_header *,
                           bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  free((void *)args[0].l);
  return value_null();
}

bjvm_stack_value bjvm_Unsafe_putLong(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *args, int argc) {
  assert(argc == 2);
  *(int64_t *)args[0].l = args[1].l;
  return value_null();
}

bjvm_stack_value bjvm_Unsafe_getByte(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return  (bjvm_stack_value) { .i = *(int8_t *)args[0].l };
}

bjvm_stack_value bjvm_AtomicLong_VMSupportsCS8(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 };
}

bjvm_stack_value bjvm_FileDescriptor_set(bjvm_thread *, bjvm_obj_header *,
                            bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .l = args[0].i };
}

bjvm_stack_value bjvm_Reflection_getCallerClass(bjvm_thread *thread, bjvm_obj_header *,
                                   bjvm_stack_value *, int) {
  // Look at frame before latest frame
  if (thread->frames_count < 2) {
    return value_null();
  }
  bjvm_stack_frame *frame = thread->frames[thread->frames_count - 2];
  return  (bjvm_stack_value) { .obj = (void*) bjvm_get_class_mirror(thread, frame->method->my_class) };
}

bjvm_stack_value bjvm_Reflection_getClassAccessFlags(bjvm_thread *, bjvm_obj_header *,
                                        bjvm_stack_value *args, int) {
  bjvm_obj_header *obj = args[0].obj;
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = classdesc->access_flags };
}

bjvm_stack_value bjvm_NativeConstructorAccessImpl_newInstance(bjvm_thread *thread,
                                                 bjvm_obj_header *,
                                                 bjvm_stack_value *args, int) {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].obj);
  bjvm_stack_value result;
  if (bjvm_initialize_class(thread, method->my_class))
    return value_null();
  bjvm_obj_header *obj = new_object(thread, method->my_class);

  bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}}, &result);
  return  (bjvm_stack_value) { .obj = obj };
}

bjvm_stack_value bjvm_Thread_currentThread(bjvm_thread *thread, bjvm_obj_header *,
                              bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .obj = (void*) thread->thread_obj };
}

bjvm_stack_value bjvm_Thread_isAlive(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 0 }; // TODO
}

bjvm_stack_value bjvm_Thread_start(bjvm_thread *, bjvm_obj_header *,
                      bjvm_stack_value *, int) {
  return value_null(); // TODO
}

bjvm_stack_value bjvm_Throwable_fillInStackTrace(bjvm_thread *, bjvm_obj_header *,
                                    bjvm_stack_value *args, int) {
  return args[0];  // TODO
}

bjvm_stack_value bjvm_AccessController_doPrivileged(bjvm_thread *thread, bjvm_obj_header *,
                                       bjvm_stack_value *args, int argc) {
  // Look up method "run" on obj
  assert(argc == 1);

  bjvm_obj_header *obj = args[0].obj;
  bjvm_classdesc *classdesc = obj->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_easy_method_lookup(classdesc, "run", NULL, true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = obj}};
  bjvm_stack_value ret;
  bjvm_thread_run(thread, method, method_args, &ret);
  return ret;
}

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const wchar_t *chars,
                                    size_t len) {
  bjvm_obj_header *str =
      bjvm_hash_table_lookup(&thread->vm->interned_strings, chars, len);
  if (str)
    return str;
  bjvm_obj_header *new_str = make_string(thread, chars, len);
  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, chars, len,
                               new_str);
  return new_str;
}

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj) {
  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int bjvm_raise_exception(bjvm_thread *thread, const wchar_t *exception_name,
                         const wchar_t *exception_string) {
  bjvm_classdesc *classdesc = bootstrap_class_create(thread, exception_name);
  bjvm_initialize_class(thread, classdesc);

  // Create the exception object
  bjvm_obj_header *obj = new_object(thread, classdesc);
  if (exception_string) {
    bjvm_obj_header *str = make_string(thread, exception_string, -1);
    bjvm_cp_method *method = bjvm_easy_method_lookup(
        classdesc, "<init>", "(Ljava/lang/String;)V", true, false);
    bjvm_thread_run(thread, method,
                    (bjvm_stack_value[]){{.obj = obj}, {.obj = str}}, NULL);
  } else {
    bjvm_cp_method *method =
        bjvm_easy_method_lookup(classdesc, "<init>", "()V", true, false);
    bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}}, NULL);
  }

#ifndef EMSCRIPTEN
  wprintf(L"Exception: %S: %S\n", exception_name, exception_string);
#endif
  bjvm_raise_exception_object(thread, obj);
  return 0;
}

struct bjvm_native_Class * bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  bjvm_vm *vm = thread->vm;
  return vm->primitive_classes[prim_kind - BJVM_TYPE_KIND_BOOLEAN];
}

bjvm_classdesc *bjvm_make_primitive_classdesc(const wchar_t* name, struct bjvm_native_Class* mirror) {
  bjvm_classdesc *desc = calloc(1, sizeof(bjvm_classdesc));

  desc->kind = BJVM_CD_KIND_ORDINARY;
  desc->super_class = NULL;
  desc->name = bjvm_make_utf8(name);
  desc->access_flags = 0x411;  // result of int.class.getModifiers()
  desc->array_type = NULL;  // TODO
  desc->mirror = mirror;

  return desc;
}

void bjvm_vm_init_primitive_classes(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (vm->primitive_classes[0]) return; // already initialized

  bjvm_classdesc *desc = bootstrap_class_create(thread, L"java/lang/Class");
  assert(desc->state == BJVM_CD_STATE_INITIALIZED);

  int offs = BJVM_TYPE_KIND_BOOLEAN;
  struct bjvm_native_Class *Boolean, *Byte, *Char, *Short, *Int, *Long, *Float, *Double, *Void;
  Boolean = vm->primitive_classes[BJVM_TYPE_KIND_BOOLEAN - offs] = (void*)new_object(thread, desc);
  Byte = vm->primitive_classes[BJVM_TYPE_KIND_BYTE - offs] = (void*)new_object(thread, desc);
  Char = vm->primitive_classes[BJVM_TYPE_KIND_CHAR - offs] = (void*)new_object(thread, desc);
  Short = vm->primitive_classes[BJVM_TYPE_KIND_SHORT - offs] = (void*)new_object(thread, desc);
  Int = vm->primitive_classes[BJVM_TYPE_KIND_INT - offs] = (void*)new_object(thread, desc);
  Long = vm->primitive_classes[BJVM_TYPE_KIND_LONG - offs] = (void*)new_object(thread, desc);
  Float = vm->primitive_classes[BJVM_TYPE_KIND_FLOAT - offs] = (void*)new_object(thread, desc);
  Double = vm->primitive_classes[BJVM_TYPE_KIND_DOUBLE - offs] = (void*)new_object(thread, desc);
  Void = vm->primitive_classes[BJVM_TYPE_KIND_VOID - offs] = (void*)new_object(thread, desc);

  Boolean->reflected_class = bjvm_make_primitive_classdesc(L"boolean", Boolean);
  Byte->reflected_class = bjvm_make_primitive_classdesc(L"byte", Byte);
  Char->reflected_class = bjvm_make_primitive_classdesc(L"char", Char);
  Short->reflected_class = bjvm_make_primitive_classdesc(L"short", Short);
  Int->reflected_class = bjvm_make_primitive_classdesc(L"int", Int);
  Long->reflected_class = bjvm_make_primitive_classdesc(L"long", Long);
  Float->reflected_class = bjvm_make_primitive_classdesc(L"float", Float);
  Double->reflected_class = bjvm_make_primitive_classdesc(L"double", Double);
  Void->reflected_class = bjvm_make_primitive_classdesc(L"void", Void);
}

bjvm_stack_value bjvm_String_intern(bjvm_thread *thread, bjvm_obj_header *obj,
                       bjvm_stack_value *, int) {
  if (obj == NULL) {
    bjvm_raise_exception(thread, L"NullPointerException", NULL);
    return value_null();
  }
  short *buf;
  size_t len;
  read_string(obj, &buf, &len);
  wchar_t *data = malloc((len + 1) * sizeof(wchar_t));
  for (size_t i = 0; i < len; ++i)
    data[i] = buf[i];
  data[len] = 0;
  bjvm_stack_value result;
  result.obj = bjvm_intern_string(thread, data, len);
  free(data);
  return result;
}

bjvm_stack_value bjvm_FileOutputStream_writeBytes(bjvm_thread *thread, bjvm_obj_header *,
                                     bjvm_stack_value *args, int) {
  bjvm_obj_header *bytes = args[0].obj;
  int offset = args[1].i;
  int length = args[2].i;
  assert(bytes->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
  char *data = (char *)array_data(bytes);
  for (int i = 0; i < length; ++i) {
    if (thread->vm->write_stdout)
      thread->vm->write_stdout(data[offset + i], thread->vm->write_byte_param);
    else
      fprintf(stderr, "%c", data[offset + i]);
  }
  return (bjvm_stack_value) { .i = 0 };
}

/** Begin StrictMath natives */

bjvm_stack_value bjvm_StrictMath_log(bjvm_thread *, bjvm_obj_header*,
                       bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return (bjvm_stack_value) { .d = log(args[0].d) };
}

// Natives for the java.lang.StrictMath class.
void bjvm_register_natives_StrictMath(bjvm_vm *vm) {
  bjvm_register_native(vm, "java/lang/StrictMath", "log", "(D)D", bjvm_StrictMath_log);
}

/** End StrictMath natives, begin Class natives */

// Natives for the java.lang.Class class.
void bjvm_register_natives_Class(bjvm_vm *vm) {

}

bjvm_vm *bjvm_create_vm(bjvm_vm_options options) {
  bjvm_vm *vm = calloc(1, sizeof(bjvm_vm));

  vm->load_classfile = options.load_classfile;
  vm->load_classfile_param = options.load_classfile_param;

  vm->classfiles = bjvm_make_hash_table(free_cf_entry, 0.75, 16);
  vm->classes = bjvm_make_hash_table(free_classdesc, 0.75, 16);
  vm->inchoate_classes = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->natives = bjvm_make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->class_padding = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->main_thread_group = NULL;

  vm->write_stdout = options.write_stdout;
  vm->write_stderr = options.write_stderr;
  vm->write_byte_param = options.write_byte_param;

  bjvm_register_native_padding(vm);

  bjvm_register_natives_StrictMath(vm);

  bjvm_register_native(vm, "java/lang/System", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/System", "mapLibraryName",
                       "(Ljava/lang/String;)Ljava/lang/String;",
                       bjvm_System_mapLibraryName);
  bjvm_register_native(vm, "java/lang/System", "initProperties",
                       "(Ljava/util/Properties;)Ljava/util/Properties;",
                       bjvm_System_initProperties);
  bjvm_register_native(vm, "java/lang/System", "arraycopy",
                       "(Ljava/lang/Object;ILjava/lang/Object;II)V",
                       bjvm_System_arraycopy);
  bjvm_register_native(vm, "java/lang/System", "setIn0",
                       "(Ljava/io/InputStream;)V", unimplemented_native);
  bjvm_register_native(vm, "java/lang/System", "setOut0",
                       "(Ljava/io/PrintStream;)V", bjvm_System_setOut);
  bjvm_register_native(vm, "java/lang/System", "setErr0",
                       "(Ljava/io/PrintStream;)V", unimplemented_native);
  bjvm_register_native(vm, "java/util/concurrent/atomic/AtomicLong",
                       "VMSupportsCS8", "()Z", bjvm_AtomicLong_VMSupportsCS8);
  bjvm_register_native(vm, "java/lang/String", "intern", "()Ljava/lang/String;",
                       bjvm_String_intern);
  bjvm_register_native(vm, "java/lang/Object", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Object", "clone", "()Ljava/lang/Object;",
                       bjvm_Object_clone);
  bjvm_register_native(vm, "java/lang/Class", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "getPrimitiveClass",
                       "(Ljava/lang/String;)Ljava/lang/Class;",
                       bjvm_Class_getPrimitiveClass);
  bjvm_register_native(vm, "java/lang/Class", "getComponentType",
                       "()Ljava/lang/Class;",
                       bjvm_Class_getComponentType);
  bjvm_register_native(vm, "java/lang/Class", "getModifiers", "()I",
                       bjvm_Class_getModifiers);
  bjvm_register_native(vm, "java/lang/Object", "getClass",
                       "()Ljava/lang/Class;", bjvm_Object_getClass);
  bjvm_register_native(vm, "java/lang/Class", "getSuperclass",
                       "()Ljava/lang/Class;", bjvm_Class_getSuperclass);
  bjvm_register_native(vm, "java/lang/Class", "getClassLoader",
                       "()Ljava/lang/ClassLoader;", bjvm_Class_getClassLoader);
  bjvm_register_native(vm, "java/lang/Class", "getName0",
                       "()Ljava/lang/String;", bjvm_Class_getName);
  bjvm_register_native(vm, "java/lang/Class", "forName0",
                       "(Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/"
                       "Class;)Ljava/lang/Class;",
                       bjvm_Class_forName);
  bjvm_register_native(vm, "java/lang/Class", "desiredAssertionStatus0",
                       "(Ljava/lang/Class;)Z",
                       bjvm_Class_desiredAssertionStatus);
  bjvm_register_native(vm, "java/lang/Class", "getDeclaredFields0",
                       "(Z)[Ljava/lang/reflect/Field;",
                       bjvm_Class_getDeclaredFields);
  bjvm_register_native(vm, "java/lang/Class", "getDeclaredConstructors0",
                       "(Z)[Ljava/lang/reflect/Constructor;",
                       bjvm_Class_getDeclaredConstructors);
  bjvm_register_native(vm, "java/lang/Class", "isPrimitive", "()Z",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "isInterface", "()Z",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "isAssignableFrom",
  "(Ljava/lang/Class;)Z", bjvm_Class_isAssignableFrom);
  bjvm_register_native(vm, "java/lang/Class", "isArray",
                       "()Z", bjvm_Class_isArray);
  bjvm_register_native(vm, "java/lang/ClassLoader", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Float", "floatToRawIntBits", "(F)I",
                       bjvm_Float_floatToRawIntBits);
  bjvm_register_native(vm, "java/lang/Double", "doubleToRawLongBits", "(D)J",
                       bjvm_Double_doubleToRawLongBits);
  bjvm_register_native(vm, "java/lang/Double", "longBitsToDouble", "(J)D",
                       bjvm_Double_doubleToRawLongBits);
  bjvm_register_native(vm, "java/io/FileInputStream", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/WinNTFileSystem", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileOutputStream", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileOutputStream", "writeBytes", "([BIIZ)V",
                       bjvm_FileOutputStream_writeBytes);
  bjvm_register_native(vm, "java/io/FileDescriptor", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileDescriptor", "set", "(I)J",
                       bjvm_FileDescriptor_set);

  bjvm_register_native(
      vm, "java/security/AccessController", "doPrivileged",
      "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;",
      bjvm_AccessController_doPrivileged);
  bjvm_register_native(
      vm, "java/security/AccessController", "getStackAccessControlContext",
      "()Ljava/security/AccessControlContext;",
      unimplemented_native);
  bjvm_register_native(vm, "java/security/AccessController", "doPrivileged",
                       "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;",
                       bjvm_AccessController_doPrivileged);

  bjvm_register_native(vm, "sun/misc/VM", "initialize", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "sun/misc/Unsafe", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "sun/misc/Unsafe", "arrayBaseOffset",
                       "(Ljava/lang/Class;)I", bjvm_Unsafe_arrayBaseOffset);
  bjvm_register_native(
      vm, "sun/misc/Unsafe", "compareAndSwapObject",
      "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z",
      bjvm_Unsafe_compareAndSwapObject);
  bjvm_register_native(vm, "sun/misc/Unsafe", "objectFieldOffset",
                       "(Ljava/lang/reflect/Field;)J",
                       bjvm_Unsafe_objectFieldOffset);
  bjvm_register_native(vm, "sun/misc/Unsafe", "arrayIndexScale",
                       "(Ljava/lang/Class;)I", bjvm_Unsafe_arrayIndexScale);
  bjvm_register_native(vm, "sun/misc/Unsafe", "getIntVolatile",
                       "(Ljava/lang/Object;J)I", bjvm_Unsafe_getIntVolatile);
  bjvm_register_native(vm, "sun/misc/Unsafe", "compareAndSwapInt",
                       "(Ljava/lang/Object;JII)Z",
                       bjvm_Unsafe_compareAndSwapInt);
  bjvm_register_native(vm, "sun/misc/Unsafe", "addressSize", "()I",
                       bjvm_Unsafe_addressSize);
  bjvm_register_native(vm, "sun/misc/Unsafe", "allocateMemory", "(J)J",
                       bjvm_Unsafe_allocateMemory);
  bjvm_register_native(vm, "sun/misc/Unsafe", "freeMemory", "(J)V",
                       bjvm_Unsafe_freeMemory);
  bjvm_register_native(vm, "sun/misc/Unsafe", "putLong", "(JJ)V",
                       bjvm_Unsafe_putLong);
  bjvm_register_native(vm, "sun/misc/Unsafe", "getByte", "(J)B",
                       bjvm_Unsafe_getByte);
  bjvm_register_native(vm, "sun/reflect/Reflection", "getCallerClass",
                       "()Ljava/lang/Class;", bjvm_Reflection_getCallerClass);
  bjvm_register_native(vm, "sun/reflect/Reflection", "getClassAccessFlags",
                       "(Ljava/lang/Class;)I",
                       bjvm_Reflection_getClassAccessFlags);
  bjvm_register_native(
      vm, "sun/reflect/NativeConstructorAccessorImpl", "newInstance0",
      "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;",
      bjvm_NativeConstructorAccessImpl_newInstance);
  bjvm_register_native(vm, "java/lang/Object", "hashCode", "()I",
                       bjvm_Object_hashCode);

  bjvm_register_native(vm, "java/lang/Thread", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Thread", "currentThread",
                       "()Ljava/lang/Thread;", bjvm_Thread_currentThread);
  bjvm_register_native(vm, "java/lang/Thread", "setPriority0", "(I)V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Thread", "isAlive", "()Z",
                       bjvm_Thread_isAlive);
  bjvm_register_native(vm, "java/lang/Thread", "start0", "()V",
                       bjvm_Thread_start);
  bjvm_register_native(vm, "java/lang/Throwable", "fillInStackTrace",
                       "(I)Ljava/lang/Throwable;",
                       bjvm_Throwable_fillInStackTrace);

  return vm;
}

void bjvm_free_vm(bjvm_vm *vm) {
  bjvm_free_hash_table(vm->classfiles);
  bjvm_free_hash_table(vm->classes);
  bjvm_free_hash_table(vm->inchoate_classes);
  bjvm_free_hash_table(vm->interned_strings);
  free(vm);
}

bjvm_thread_options bjvm_default_thread_options() {
  bjvm_thread_options options = {};
  options.stack_space = 1 << 20;
  options.js_jit_enabled = true;
  options.thread_group = NULL;
  return options;
}

bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc, bjvm_utf8 *name,
                                 bjvm_utf8 *descriptor) {
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

  return NULL;
}

bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const wchar_t *name,
                                      const wchar_t *descriptor) {
  bjvm_utf8 name_utf8 = bjvm_make_utf8(name),
            descriptor_utf8 = bjvm_make_utf8(descriptor);
  bjvm_cp_field *result =
      bjvm_field_lookup(classdesc, &name_utf8, &descriptor_utf8);
  free_utf8(name_utf8);
  free_utf8(descriptor_utf8);
  return result;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread);

void store_stack_value(void *field_location, bjvm_stack_value value,
                       bjvm_type_kind kind);
bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value) {
  store_stack_value((char *)obj + field->byte_offset, bjvm_stack_value,
                    field_to_representable_kind(&field->parsed_descriptor));
}

bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field) {
  return load_stack_value(
      (char *)obj + field->byte_offset,
      field_to_representable_kind(&field->parsed_descriptor));
}

bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options) {
  bjvm_thread *thr = calloc(1, sizeof(bjvm_thread));
  thr->vm = vm;
  thr->frame_buffer =
      calloc(1, thr->frame_buffer_capacity = options.stack_space);
  thr->js_jit_enabled = options.js_jit_enabled;

  bjvm_classdesc *desc;

  // Link (but don't initialize) java.lang.Class immediately
  desc = bootstrap_class_create(thr, L"java/lang/Class");
  bjvm_initialize_class(thr, desc);

  bjvm_vm_init_primitive_classes(thr);

  desc = bootstrap_class_create(thr, L"java/lang/reflect/Field");
  desc = bootstrap_class_create(thr, L"java/lang/reflect/Constructor");

  // Initialize java.lang.Thread mirror
  desc = bootstrap_class_create(thr, L"java/lang/Thread");
  bjvm_initialize_class(thr, desc);

  struct bjvm_native_Thread *thread_obj = (void*)new_object(thr, desc);
  thr->thread_obj = thread_obj;

  thread_obj->vm_thread = thr;
  thread_obj->priority = 5;
  thread_obj->name = bjvm_intern_string(thr, L"main", -1);

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_easy_method_lookup(
      desc, "<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/String;)V", false,
      false);
  bjvm_obj_header *name = make_string(thr, L"main", -1);
  bjvm_thread_run(thr, make_thread,
                  (bjvm_stack_value[]){{.obj = (void*)thread_obj},
                                       {.obj = main_thread_group},
                                       {.obj = name}},
                  NULL);

  // Call System.initializeSystemClass()
  desc = bootstrap_class_create(thr, L"java/lang/System");
  bjvm_initialize_class(thr, desc);

  bjvm_cp_method *method = bjvm_easy_method_lookup(
      desc, "initializeSystemClass", "()V", false, false);
  bjvm_stack_value ret;
  bjvm_thread_run(thr, method, NULL, &ret);

  thr->current_exception = NULL;

  return thr;
}

void bjvm_free_thread(bjvm_thread *thread) {
  // TODO what happens to ->current_exception etc.?
  free(thread->frame_buffer);
  free(thread);
}

int bjvm_vm_preregister_classfile(bjvm_vm *vm, const wchar_t *filename,
                                  const uint8_t *bytes, size_t len) {
  return add_classfile_bytes(vm, filename,
                             wcslen(filename), bytes, len);
}

int bjvm_vm_read_classfile(bjvm_vm *vm, const wchar_t *filename,
                           const uint8_t **bytes, size_t *len) {
  struct classfile_entry* entry = bjvm_hash_table_lookup(&vm->classfiles, filename, wcslen(filename));
  if (entry) {
    if (bytes)
      *bytes = entry->data;
    if (len)
      *len = entry->len;
    return 0;
  }

  // Otherwise, try to read it from the vm->load_classfile implementation
  if (vm->load_classfile) {
    uint8_t *loaded_bytes = NULL;

    size_t mbs_len = wcslen(filename) * 4 + 1;
    char *as_mbs = malloc(mbs_len);
    wcstombs(as_mbs, filename, mbs_len);

    int status = vm->load_classfile(as_mbs, vm->load_classfile_param,
                                    &loaded_bytes, len);
    free(as_mbs);

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

void bjvm_vm_list_classfiles(bjvm_vm *vm, wchar_t **strings, size_t *count) {
  *count = vm->classfiles.entries_count;
  if (strings) {
    bjvm_hash_table_iterator iter = bjvm_hash_table_get_iterator(&vm->classfiles);
    size_t key_len, i = 0;
    void* value;
    while (i < *count && bjvm_hash_table_iterator_has_next(iter, &strings[i], &key_len, &value)) {
      wchar_t* key = calloc(key_len + 1, sizeof(wchar_t));
      wmemcpy(key, strings[i], key_len);
      strings[i] = key;
      ++i;
      bjvm_hash_table_iterator_next(&iter);
    }
  }
}

// Called for both primitive and object arrays
void fill_array_classdesc(bjvm_thread *thread, bjvm_classdesc *base) {
  base->access_flags = BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL;

  bjvm_utf8 *java_lang_Object = malloc(sizeof(bjvm_utf8));
  *java_lang_Object = bjvm_make_utf8(L"java/lang/Object");
  bjvm_cp_class_info *info = calloc(1, sizeof(bjvm_cp_class_info));

  info->classdesc = bootstrap_class_create(thread, java_lang_Object->chars);
  info->name = java_lang_Object;
  base->super_class = info;
  base->fields_count = 1;

  bjvm_cp_field *fields = calloc(1, sizeof(bjvm_cp_field));
  base->fields = fields;
  fields->access_flags =
      BJVM_ACCESS_PUBLIC | BJVM_ACCESS_STATIC | BJVM_ACCESS_FINAL;

  bjvm_utf8 *length = malloc(sizeof(bjvm_utf8)), *I = malloc(sizeof(bjvm_utf8));
  *length = bjvm_make_utf8(L"length");
  *I = bjvm_make_utf8(L"I");
  fields->name = length;
  fields->descriptor = I;
}

void primitive_type_kind_to_array_info(bjvm_type_kind kind,
                                       const wchar_t **type, int *size) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    *type = L"[Z";
    *size = 1;
    break;
  case BJVM_TYPE_KIND_CHAR:
    *type = L"[C";
    *size = 2;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    *type = L"[F";
    *size = 4;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    *type = L"[D";
    *size = 8;
    break;
  case BJVM_TYPE_KIND_BYTE:
    *type = L"[B";
    *size = 1;
    break;
  case BJVM_TYPE_KIND_SHORT:
    *type = L"[S";
    *size = 2;
    break;
  case BJVM_TYPE_KIND_INT:
    *type = L"[I";
    *size = 4;
    break;
  case BJVM_TYPE_KIND_LONG:
    *type = L"[J";
    *size = 8;
    break;
  default:
    UNREACHABLE();
  }
}

bjvm_classdesc *primitive_arr_classdesc(bjvm_thread *thread, int dimensions,
                                        bjvm_classdesc_kind classdesc_kind,
                                        bjvm_type_kind prim_kind) {
  bjvm_primitive_array_classdesc *result =
      calloc(1, sizeof(bjvm_primitive_array_classdesc));
  bjvm_classdesc *base = &result->base;

  base->state = BJVM_CD_STATE_INITIALIZED;
  base->base_component = bjvm_primitive_class_mirror(thread, prim_kind)->reflected_class;

  base->kind = classdesc_kind;
  fill_array_classdesc(thread, base);

  result->dimensions = dimensions;
  result->element_type = prim_kind;

  if (dimensions > 1) {
    wchar_t one_fewer_name[MAX_CF_NAME_LENGTH] = { 0 };
    int i = 0;
    for (; i < dimensions - 1; ++i)
      one_fewer_name[i] = '[';
    const wchar_t* name;
    int size;
    primitive_type_kind_to_array_info(prim_kind, &name, &size);
    one_fewer_name[i] = name[1];
    result->base.one_fewer_dim = bootstrap_class_create(thread, one_fewer_name);
  } else {
    result->base.one_fewer_dim = base->base_component;
  }

  return (bjvm_classdesc *)result;
}

void free_base_arr_classdesc(bjvm_classdesc *base) {
  free_utf8(*base->fields->name);
  free_utf8(*base->fields->descriptor);
  free(base->fields->name);
  free(base->fields->descriptor);
  free(base->fields);
}

void free_primitive_arr_classdesc(bjvm_primitive_array_classdesc *desc) {
  bjvm_classdesc *base = &desc->base;
  free_base_arr_classdesc(base);
  free(desc);
}

bjvm_classdesc *ordinary_arr_classdesc(bjvm_thread *thread,
                                       bjvm_classdesc *base, int dimensions) {
  bjvm_array_classdesc *result = calloc(1, sizeof(bjvm_array_classdesc));
  result->base.kind = BJVM_CD_KIND_ORDINARY_ARRAY;
  result->base.state = base->state;
  fill_array_classdesc(thread, &result->base);

  result->dimensions = dimensions;
  if (base->kind == BJVM_CD_KIND_ORDINARY) {
    result->base.base_component = result->base.one_fewer_dim = base;
  } else {
    bjvm_array_classdesc *array_base = (bjvm_array_classdesc *)base;
    result->base.base_component = array_base->base.base_component;
    result->base.one_fewer_dim = (bjvm_classdesc*) array_base;
    assert(result->dimensions == array_base->dimensions + 1);
  }

  return (bjvm_classdesc *)result;
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

char primitive_type_kind_to_char(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN: return 'Z';
  case BJVM_TYPE_KIND_CHAR: return 'C';
  case BJVM_TYPE_KIND_FLOAT: return 'F';
  case BJVM_TYPE_KIND_DOUBLE: return 'D';
  case BJVM_TYPE_KIND_BYTE: return 'B';
  case BJVM_TYPE_KIND_SHORT: return 'S';
  case BJVM_TYPE_KIND_INT: return 'I';
  case BJVM_TYPE_KIND_LONG: return 'J';
  case BJVM_TYPE_KIND_VOID: return 'V';
  case BJVM_TYPE_KIND_REFERENCE: return 'L';
  default: UNREACHABLE();
  }
}

bjvm_utf8 field_descriptor_to_name(bjvm_field_descriptor desc) {
  wchar_t* result = calloc(desc.dimensions + 4 + desc.kind == BJVM_TYPE_KIND_REFERENCE ? desc.class_name.len : 0, sizeof(wchar_t)), *write = result;
  if (desc.dimensions) {
    wmemset(write, L'[', desc.dimensions);
    write += desc.dimensions;
  }
  if (desc.kind == BJVM_TYPE_KIND_REFERENCE) {
    *write++ = L'L';
    wmemcpy(write, desc.class_name.chars, desc.class_name.len);
    write += desc.class_name.len;
  } else {
    *write++ = primitive_type_kind_to_char(desc.kind);
  }
  return (bjvm_utf8) { .chars = result, .len = write - result };
}

struct bjvm_native_MethodType *bjvm_resolve_method_type(bjvm_thread *thread, bjvm_method_descriptor *method) {
  // Resolve each class in the arguments list, as well as the return type if it exists
  assert(method);
  bjvm_classdesc *MethodType = bootstrap_class_create(thread, L"java/lang/invoke/MethodType"),
                 *Class = bootstrap_class_create(thread, L"java/lang/Class");
  assert(MethodType);
  bjvm_initialize_class(thread, MethodType);

  bjvm_obj_header *ptypes = create_object_array(thread, Class, method->args_count);
  struct bjvm_native_Class *rtype;

  for (int i = 0; i < method->args_count; ++i) {
    bjvm_utf8 name = field_descriptor_to_name(method->args[i]);
    bjvm_classdesc* arg_desc = load_class_of_field_descriptor(thread, name.chars);
    free_utf8(name);
    if (!arg_desc) return NULL;
    *((struct bjvm_native_Class**)array_data(ptypes) + i) = arg_desc->mirror;
  }

  bjvm_utf8 name = field_descriptor_to_name(method->return_type);
  bjvm_classdesc* ret_desc = load_class_of_field_descriptor(thread, name.chars);
  free_utf8(name);
  if (!ret_desc) return NULL;
  rtype = ret_desc->mirror;
  // Call <init>(Ljava/lang/Class;[Ljava/lang/Class;Z)V
  bjvm_cp_method *init = bjvm_easy_method_lookup(MethodType, "<init>", "(Ljava/lang/Class;[Ljava/lang/Class;Z)V", false, false);
  struct bjvm_native_MethodType *result = (void*)new_object(thread, MethodType);
  bjvm_thread_run(thread, init, (bjvm_stack_value[]){{.obj = (void*)result}, {.obj = (void*)rtype}, {.obj = ptypes}, {.i = 1 /* trusted */}}, NULL);
  return result;
}
int bjvm_resolve_method_handle(bjvm_thread *thread, bjvm_cp_method_handle_info *info) {
  if (info->resolved_mt) return 0;  // already resolved
  // "Third, a reference to an instance of java.lang.invoke.MethodType is obtained as if by resolution of an
  // unresolved symbolic reference to a method type that contains the method descriptor specified in Table 5.4.3.5-B
  // for the kind of MH."
}

// name = "java/lang/Object" or "[[J" or "[Ljava/lang/String;"
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const wchar_t *name) {
  bjvm_vm *vm = thread->vm;

  int dimensions = 0;
  const wchar_t *chars = name;
  int len = wcslen(name), orig_len = len;
  while (len > 0 && *chars == '[') // munch '[' at beginning
    dimensions++, len--, chars++;

  assert(dimensions < 255);
  assert(len > 0);

  if (dimensions) { // array type
    if (*chars != 'L') {
      bjvm_classdesc *desc = bjvm_hash_table_lookup(&vm->classes, name, orig_len);
      if (desc)
        return desc;
      switch (*chars) {
      case 'J':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_LONG_ARRAY, BJVM_TYPE_KIND_LONG);
        break;
      case 'D':
        desc = primitive_arr_classdesc(thread, dimensions,
                                       BJVM_CD_KIND_DOUBLE_ARRAY,
                                       BJVM_TYPE_KIND_DOUBLE);
        break;
      case 'F':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_FLOAT_ARRAY, BJVM_TYPE_KIND_FLOAT);
        break;
      case 'I':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_INT_ARRAY, BJVM_TYPE_KIND_INT);
        break;
      case 'S':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_SHORT_ARRAY, BJVM_TYPE_KIND_SHORT);
        break;
      case 'B':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_BYTE_ARRAY, BJVM_TYPE_KIND_BYTE);
        break;
      case 'C':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_CHAR_ARRAY, BJVM_TYPE_KIND_CHAR);
        break;
      case 'Z':
        desc = primitive_arr_classdesc(thread, dimensions,
                                       BJVM_CD_KIND_BOOLEAN_ARRAY,
                                       BJVM_TYPE_KIND_BOOLEAN);
        break;
      default:
        UNREACHABLE();
      }
      (void)bjvm_hash_table_insert(&vm->classes, name, orig_len, desc);
      return desc;
    }

    chars++;
    assert(len >= 3);
    len -= 2;
  }

  // Check whether the class is already loaded
  bjvm_classdesc *base_class = bjvm_hash_table_lookup(&vm->classes, chars, len);

  if (!base_class) {
    // Add entry to inchoate_classes
    (void)bjvm_hash_table_insert(&vm->inchoate_classes, chars, len, (void *)1);

    // e.g. "java/lang/Object.class"
    const wchar_t *cf_ending = L".class";
    wchar_t filename[MAX_CF_NAME_LENGTH + 7];
    wcscpy(filename, chars);
    wcscpy(filename + len, cf_ending);

    uint8_t *bytes;
    size_t cf_len;
    int read_status =
        bjvm_vm_read_classfile(vm, filename, (const uint8_t **)&bytes, &cf_len);
    if (read_status) {
      int i = 0;
      for (; i < len; ++i)
        filename[i] = filename[i] == '/' ? '.' : filename[i];
      filename[i] = L'\0';
      // ClassNotFoundException: com.google.DontBeEvil
      bjvm_raise_exception(thread, L"java/lang/ClassNotFoundException",
                           filename);
      return NULL;
    }

    base_class = calloc(1, sizeof(bjvm_classdesc));
    char *error = bjvm_parse_classfile(bytes, cf_len, base_class);
    if (error) {
      free(base_class);
      free(error);
      // TODO raise VerifyError
      UNREACHABLE();
    }

    // 3. If C has a direct superclass, the symbolic reference from C to its
    // direct superclass is resolved using the algorithm of §5.4.3.1.
    bjvm_cp_class_info *super = base_class->super_class;
    if (super) {
      // If the superclass is currently being loaded -> circularity  error
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name->chars,
                                 super->name->len)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, base_class->super_class);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
      }
    }

    // 4. If C has any direct superinterfaces, the symbolic references from C to
    // its direct superinterfaces are resolved using the algorithm of §5.4.3.1.
    for (int i = 0; i < base_class->interfaces_count; ++i) {
      bjvm_cp_class_info *super = base_class->interfaces[i];
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name->chars,
                                 -1)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, base_class->interfaces[i]);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
      }
    }

    // Look up in the native methods list and add native handles as appropriate
    native_entries *entries = bjvm_hash_table_lookup(&vm->natives, chars, len);
    if (entries) {
      for (int i = 0; i < entries->entries_count; i++) {
        native_entry *entry = entries->entries + i;

        for (int j = 0; j < base_class->methods_count; ++j) {
          bjvm_cp_method *method = base_class->methods + j;

          if (utf8_equals_utf8(method->name, &entry->name) &&
              utf8_equals_utf8(method->descriptor, &entry->descriptor)) {
            method->native_handle = entry->callback;
            break;
          }
        }
      }
    }

    // Remove from inchoate_classes
    (void)bjvm_hash_table_delete(&vm->inchoate_classes, chars, len);
    (void)bjvm_hash_table_insert(&vm->classes, chars, len, base_class);
  }

  // Derive nth dimension
  bjvm_classdesc *result = base_class;
  result->kind = BJVM_CD_KIND_ORDINARY;
  for (int i = 1; i <= dimensions; ++i) {
    if (!result->array_type)
      result->array_type = ordinary_arr_classdesc(thread, result, i);
    result = result->array_type;
  }

  return result;
}

int bjvm_link_array_class(bjvm_thread *thread,
                          bjvm_array_classdesc *classdesc) {
  int status = bjvm_link_class(thread, classdesc->base.base_component);
  if (status) {
    // TODO mark all arrays of this class as fucked up
    UNREACHABLE();
  }
  classdesc->base.state = classdesc->base.base_component->state;
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
    return bjvm_link_array_class(thread, (bjvm_array_classdesc *)classdesc);
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
      char *error = bjvm_analyze_method_code_segment(method);
      if (error) {
        // TODO raise VerifyError
        classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
        printf("Error analyzing method %S: %s\n", method->name->chars, error);
        UNREACHABLE();
      }
    }
  }

  int imp_padding = (int)(uintptr_t)bjvm_hash_table_lookup(&thread->vm->class_padding,
                                                classdesc->name.chars,
                                                classdesc->name.len);

  // Assign memory locations to all static/non-static fields
  int static_offset = 0,
      nonstatic_offset = classdesc->super_class
                             ? classdesc->super_class->classdesc->data_bytes
                             : sizeof(bjvm_obj_header);
  nonstatic_offset += imp_padding;
  for (int field_i = 0; field_i < classdesc->fields_count; ++field_i) {
    bjvm_cp_field *field = classdesc->fields + field_i;
    bjvm_type_kind kind =
        field_to_representable_kind(&field->parsed_descriptor);
    field->byte_offset = field->access_flags & BJVM_ACCESS_STATIC
                             ? allocate_field(&static_offset, kind)
                             : allocate_field(&nonstatic_offset, kind);

#if AGGRESSIVE_DEBUG
    printf("Allocating nonstatic field %S for class %S at %d\n", field->name->chars, classdesc->name.chars,
           field->byte_offset);
#endif
  }

  // Create static field memory, initializing all to 0
  classdesc->static_fields = calloc(static_offset, 1);
  classdesc->data_bytes = nonstatic_offset;

  return 0;
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

  bjvm_cp_method *clinit =
      bjvm_easy_method_lookup(classdesc, "<clinit>", "()V", false, false);
  int error = 0;
  if (clinit) {
    bjvm_stack_frame *frame = bjvm_push_frame(thread, clinit);
    error = bjvm_bytecode_interpret(thread, frame, NULL);
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
                              const bjvm_utf8 *name,
                              const bjvm_utf8 *method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor ||
          utf8_equals_utf8(candidate->descriptor, method_descriptor));
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 *name,
                                   const bjvm_utf8 *method_descriptor,
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
    return NULL;

  for (int i = 0; i < descriptor->interfaces_count; ++i) {
    bjvm_cp_method *result =
        bjvm_method_lookup(descriptor->interfaces[i]->classdesc, name,
                           method_descriptor, false, true);
    if (result)
      return result;
  }

  return NULL;
}

bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const char *name,
                                        const char *descriptor,
                                        bool superclasses,
                                        bool superinterfaces) {
  if (!classdesc) return NULL;
  bjvm_utf8 name_wide = bjvm_make_utf8_cstr(name),
            descriptor_wide;
  if (descriptor)
    descriptor_wide = bjvm_make_utf8_cstr(descriptor);
  bjvm_cp_method *result = bjvm_method_lookup(
      classdesc, &name_wide, descriptor ? &descriptor_wide : NULL, superclasses, superinterfaces);
  free_utf8(name_wide);
  if (descriptor)
    free_utf8(descriptor_wide);
  return result;
}

int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                     bjvm_stack_value *args, bjvm_stack_value *result) {
  assert(method);

  bjvm_stack_frame *frame = bjvm_push_frame(thread, method);
  if (!frame) return -1;

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
  info->classdesc = bootstrap_class_create(thread, info->name->chars);
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
  return field == NULL;
}

void make_array_classdesc(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (classdesc->array_type)
    return;
  if (classdesc->kind == BJVM_CD_KIND_ORDINARY)
    classdesc->array_type = ordinary_arr_classdesc(thread, classdesc, 1);
  else if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
    bjvm_array_classdesc *arr = (bjvm_array_classdesc *)classdesc;
    classdesc->array_type = ordinary_arr_classdesc(thread, arr->base.base_component,
                                                   arr->dimensions + 1);
  } else {
    bjvm_primitive_array_classdesc *arr =
        (bjvm_primitive_array_classdesc *)classdesc;
    classdesc->array_type = primitive_arr_classdesc(
        thread, arr->dimensions + 1, classdesc->kind, arr->element_type);
  }
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

bjvm_obj_header *create_object_array(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc, int count) {
  assert(classdesc);
  if (count < 0) {
    bjvm_negative_array_size_exception(thread, count);
    return NULL;
  }
  bjvm_obj_header *array = calloc(1, 24 + count * sizeof(void *));
  make_array_classdesc(thread, classdesc);
  array->descriptor = classdesc->array_type;
  *array_length(array) = count;
  return array;
}

bjvm_obj_header *create_primitive_array(bjvm_thread *thread,
                                        bjvm_type_kind array_type, int count) {
  const wchar_t *type;
  int size;

  if (count < 0) {
    bjvm_negative_array_size_exception(thread, count);
    return NULL;
  }

  primitive_type_kind_to_array_info(array_type, &type, &size);
  bjvm_classdesc *desc = bootstrap_class_create(thread, type);
  assert(desc);

  bjvm_obj_header *array = calloc(1, 24 + count * size);
  array->descriptor = desc;
  *array_length(array) = count;

  return array;
}

uint64_t hash_code_rng = 0;
uint64_t next_hash_code() {
  hash_code_rng = hash_code_rng * 0x5DEECE66D + 0xB;
  return hash_code_rng >> 16;
}

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  (void)thread;
  bjvm_obj_header *obj =
      calloc(1, classdesc->data_bytes + sizeof(bjvm_obj_header));
  obj->descriptor = classdesc;
  obj->mark_word = next_hash_code();
  return obj;
}

int *array_length(bjvm_obj_header *array) {
  return (int *)((char *)array + 16);
}

void *array_data(bjvm_obj_header *array) { return (char *)array + 24; }

bool bjvm_is_instanceof_name(const bjvm_obj_header *mirror, const char* name) {
  return utf8_equals(&mirror->descriptor->name, name);
}

// NULLABLE because bjvm_unmirror_class on int.class etc. will return NULL! (For now...)
bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/Class"));
  return ((struct bjvm_native_Class *)mirror)->reflected_class;
}

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/reflect/Field"));
  // Fields get copied around, but all reference the "root" created by the VM
  bjvm_obj_header* root = ((struct bjvm_native_Field *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Field *)mirror)->reflected_field;
}

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/reflect/Constructor"));
  // Constructors get copied around, but all reference the "root" created by the VM
  bjvm_obj_header* root = ((struct bjvm_native_Constructor *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Constructor *)mirror)->reflected_ctor;
}

bjvm_obj_header *make_string(bjvm_thread *thread, const wchar_t *chars, int len) {
  bjvm_classdesc *java_lang_String =
      bootstrap_class_create(thread, L"java/lang/String");
  bjvm_initialize_class(thread, java_lang_String);
  struct bjvm_native_String *str = (void*)new_object(thread, java_lang_String);
  len = len < 0 ? wcslen(chars) : len;
  str->value = create_primitive_array(thread, BJVM_TYPE_KIND_CHAR, len);
  for (int i = 0; i < len; ++i)
    *((uint16_t *)array_data(str->value) + i) = chars[i];
  return (void*)str;
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                       bjvm_classdesc *classdesc) {
  if (!classdesc)
    return NULL;
  if (classdesc->mirror)
    return classdesc->mirror;

  bjvm_classdesc *java_lang_Class =
      bootstrap_class_create(thread, L"java/lang/Class");
  struct bjvm_native_Class *class_mirror = classdesc->mirror = (void*)new_object(thread, java_lang_Class);
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

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *classdesc) {
  // Walk the superclasses/superinterfaces of bjvm_obj_header and see if any are
  // equal to classdesc
  // TODO compare class loaders too, superinterfaces
  // TODO arrays
  if (o == NULL) return true;

  if (classdesc->kind != BJVM_CD_KIND_ORDINARY) {
    if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
      const bjvm_array_classdesc *arr = (const bjvm_array_classdesc *)classdesc;
      if (o->kind != BJVM_CD_KIND_ORDINARY_ARRAY)
        return false;
      const bjvm_array_classdesc *o_arr = (const bjvm_array_classdesc *)o;
      return arr->dimensions == o_arr->dimensions &&
             bjvm_instanceof(o_arr->base.base_component, arr->base.base_component);
    }
    return classdesc->kind == o->kind;
  }

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  const bjvm_classdesc *desc = o;
  while (desc) {
    if (bjvm_instanceof_interface(desc, classdesc))
      return true;
    desc = desc->super_class ? desc->super_class->classdesc : NULL;
  }
  return false;
}

// Implementation of invokespecial/invokeinterface/invokevirtual
int bjvm_invokenonstatic(bjvm_thread *thread, bjvm_stack_frame *frame, bjvm_bytecode_insn *insn) {
  assert(insn->cp->kind == BJVM_CP_KIND_METHOD_REF || insn->cp->kind == BJVM_CP_KIND_INTERFACE_METHOD_REF);
  bjvm_cp_method *method = insn->ic;

  int args = insn->args;
  bjvm_obj_header *target = frame->values[frame->stack_depth - args].obj;

  if (!method || (insn->kind != bjvm_insn_invokespecial && target->descriptor != insn->ic2) || !CACHE_INVOKENONSTATIC) {
    const bjvm_cp_method_info *info = &insn->cp->methodref;
    args = insn->args = info->method_descriptor->args_count + 1;

    assert(args <= frame->stack_depth);
    target = frame->values[frame->stack_depth - args].obj;

    if (target == NULL) {
      bjvm_null_pointer_exception(thread);
      return -1;
    }
    if (insn->kind == bjvm_insn_invokespecial) {
      int error = bjvm_resolve_class(thread, info->class_info);
      if (error)
        return -1;
    }
    bjvm_classdesc *lookup_on = insn->ic2 = insn->kind == bjvm_insn_invokespecial
                                    ? info->class_info->classdesc
                                    : target->descriptor;
    if (lookup_on->state != BJVM_CD_STATE_INITIALIZED) {
      int error = bjvm_initialize_class(thread, lookup_on);
      if (error)
        return -1;
    }
    method = insn->ic = bjvm_method_lookup(
        lookup_on, info->name_and_type->name, info->name_and_type->descriptor,
        true, insn->kind == bjvm_insn_invokeinterface);
    insn->ic2 = lookup_on;
    if (!method) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Could not find method %S with descriptor %S on %s %S",
               info->name_and_type->name->chars, info->name_and_type->descriptor->chars,
               lookup_on->access_flags & BJVM_ACCESS_INTERFACE ? "interface" : "class",
               lookup_on->name.chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
    if (method->access_flags & BJVM_ACCESS_STATIC) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Method %S is static", method->name->chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }

    if (method->access_flags & BJVM_ACCESS_ABSTRACT)
      bjvm_abstract_method_error(thread, method);
  }

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    invoked_result = method->native_handle(thread, target,
                          frame->values + frame->stack_depth - args + 1,
                          args - 1);
    frame->stack_depth -= args;
    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    for (int i = 0, j = 0; i < args; ++i, ++j) {
      invoked_frame->values[invoked_frame->max_stack + j] = frame->values[frame->stack_depth - args + i];
      if (i >= 1)
        j += bjvm_is_field_wide(method->parsed_descriptor->args[i - 1]);
    }
    frame->stack_depth -= args;

    int err =
        bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }
  if (method->parsed_descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);

  return 0;
}

int bjvm_invokestatic(bjvm_thread * thread, bjvm_stack_frame * frame, bjvm_bytecode_insn * insn) {
  bjvm_cp_method *method = insn->ic;
  if (!method || !CACHE_INVOKESTATIC) {
    const bjvm_cp_method_info *info = &insn->cp->methodref;
    bjvm_cp_class_info *class = info->class_info;

    int error = bjvm_resolve_class(thread, class);
    if (error)
      return -1;
    error = bjvm_initialize_class(thread, class->classdesc);
    if (error)
      return -1;

    method = insn->ic =
        bjvm_method_lookup(class->classdesc, info->name_and_type->name,
                           info->name_and_type->descriptor, false, false);
    if (!method) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Could not find method %S with descriptor %S on class %S",
              info->name_and_type->name->chars, info->name_and_type->descriptor->chars,
              class->name->chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
  }

  int args = method->parsed_descriptor->args_count;
  assert(args <= frame->stack_depth);

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    invoked_result = method->native_handle(thread, NULL,
                          frame->values + frame->stack_depth - args, args);
    frame->stack_depth -= args;
    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    for (int i = 0, j = 0; i < args; ++i, ++j) {
      invoked_frame->values[invoked_frame->max_stack + j] = frame->values[frame->stack_depth - args + i];
      j += bjvm_is_field_wide(method->parsed_descriptor->args[i]);
    }
    frame->stack_depth -= args;
    int err = bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }

  if (method->parsed_descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);
  return 0;
}

bjvm_obj_header *bjvm_multianewarray_impl(bjvm_thread *thread, bjvm_classdesc *desc, bjvm_stack_value *value, int dims) {
  int this_dim = (value + dims - 1)->i;
  if (dims == 1 && desc->kind != BJVM_CD_KIND_ORDINARY_ARRAY) {
    return create_primitive_array(thread, ((bjvm_primitive_array_classdesc*) desc)->element_type, this_dim);
  }
  bjvm_obj_header* arr = create_object_array(thread, desc, this_dim);
  for (int i = 0; i < this_dim; ++i) {
    bjvm_obj_header* next = bjvm_multianewarray_impl(thread, desc->array_type, value, dims - 1);
    *((bjvm_obj_header**) array_data(arr) + i) = next;
  }
  return arr;
}

int bjvm_multianewarray(bjvm_thread *thread, bjvm_stack_frame *frame, struct bjvm_multianewarray_data *multianewarray) {
  int dims = multianewarray->dimensions;
  assert(frame->stack_depth >= dims);
  assert(dims >= 1);

  int error = bjvm_resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  for (int i = 0; i < dims; ++i) {
    int dim = frame->values[frame->stack_depth - dims + i].i;
    if (dim < 0) {
      bjvm_negative_array_size_exception(thread, dim);
      return -1;
    }
  }

  bjvm_obj_header *result = bjvm_multianewarray_impl(thread,
    multianewarray->entry->classdesc, &frame->values[frame->stack_depth - dims], dims);
  frame->stack_depth -= dims;
  checked_push(frame, (bjvm_stack_value){.obj = result});
  return 0;
}

bool bjvm_invokedynamic(bjvm_thread * thread, bjvm_stack_frame * frame, bjvm_bytecode_insn * insn) {

  bjvm_cp_indy_info *indy = &insn->cp->indy_info;
  // "The call site specifier is resolved (§5.4.3.6) for this specific dynamic call site to obtain a reference to a
  // java.lang.invoke.MethodHandle instance that will serve as the bootstrap method, a reference to a
  // java.lang.invoke.MethodType instance, and references to static arguments."

  UNREACHABLE("bjvm_invokedynamic");

  (void) thread;
  (void) frame;
  (void) insn;
}

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result) {
  bjvm_cp_method *method = frame->method;

#if AGGRESSIVE_DEBUG
  printf("Calling method %S, descriptor %S, on class %S\n", method->name->chars,
         method->descriptor->chars, method->my_class->name.chars);
#endif

start:
  while (true) {
    bjvm_bytecode_insn* insn = &method->code->code[frame->program_counter];

#if AGGRESSIVE_DEBUG
    char *dump = dump_frame(frame),
         *insn_dump = insn_to_string(insn, frame->program_counter);
    printf("Insn: %s\n", insn_to_string(insn, frame->program_counter));
    printf("Method: %S in class %S\n", method->name->chars,
           method->my_class->name.chars);
    printf("FRAME:\n%s\n", dump_frame(frame));

    free(dump);
    free(insn_dump);
#endif

    static const void* insn_jump_table[] = { &&bjvm_insn_nop, &&bjvm_insn_aaload, &&bjvm_insn_aastore, &&bjvm_insn_aconst_null,   &&bjvm_insn_areturn,   &&bjvm_insn_arraylength,   &&bjvm_insn_athrow,   &&bjvm_insn_baload,   &&bjvm_insn_bastore,   &&bjvm_insn_caload,   &&bjvm_insn_castore,   &&bjvm_insn_d2f,   &&bjvm_insn_d2i,   &&bjvm_insn_d2l,   &&bjvm_insn_dadd,   &&bjvm_insn_daload,   &&bjvm_insn_dastore,   &&bjvm_insn_dcmpg,   &&bjvm_insn_dcmpl,   &&bjvm_insn_ddiv,   &&bjvm_insn_dmul,   &&bjvm_insn_dneg,   &&bjvm_insn_drem,   &&bjvm_insn_dreturn,   &&bjvm_insn_dsub,   &&bjvm_insn_dup,   &&bjvm_insn_dup_x1,   &&bjvm_insn_dup_x2,   &&bjvm_insn_dup2,   &&bjvm_insn_dup2_x1,   &&bjvm_insn_dup2_x2,   &&bjvm_insn_f2d,   &&bjvm_insn_f2i,   &&bjvm_insn_f2l,   &&bjvm_insn_fadd,   &&bjvm_insn_faload,   &&bjvm_insn_fastore,   &&bjvm_insn_fcmpg,   &&bjvm_insn_fcmpl,   &&bjvm_insn_fdiv,   &&bjvm_insn_fmul,   &&bjvm_insn_fneg,   &&bjvm_insn_frem,   &&bjvm_insn_freturn,   &&bjvm_insn_fsub,   &&bjvm_insn_i2b,   &&bjvm_insn_i2c,   &&bjvm_insn_i2d,   &&bjvm_insn_i2f,   &&bjvm_insn_i2l,   &&bjvm_insn_i2s,   &&bjvm_insn_iadd,   &&bjvm_insn_iaload,   &&bjvm_insn_iand,   &&bjvm_insn_iastore,   &&bjvm_insn_idiv,   &&bjvm_insn_imul,   &&bjvm_insn_ineg,   &&bjvm_insn_ior,   &&bjvm_insn_irem,   &&bjvm_insn_ireturn,   &&bjvm_insn_ishl,   &&bjvm_insn_ishr,   &&bjvm_insn_isub,   &&bjvm_insn_iushr,   &&bjvm_insn_ixor,   &&bjvm_insn_l2d,   &&bjvm_insn_l2f,   &&bjvm_insn_l2i,   &&bjvm_insn_ladd,   &&bjvm_insn_laload,   &&bjvm_insn_land,   &&bjvm_insn_lastore,   &&bjvm_insn_lcmp,   &&bjvm_insn_ldiv,   &&bjvm_insn_lmul,   &&bjvm_insn_lneg,   &&bjvm_insn_lor,   &&bjvm_insn_lrem,   &&bjvm_insn_lreturn,   &&bjvm_insn_lshl,   &&bjvm_insn_lshr,   &&bjvm_insn_lsub,   &&bjvm_insn_lushr,   &&bjvm_insn_lxor,   &&bjvm_insn_monitorenter,   &&bjvm_insn_monitorexit,   &&bjvm_insn_pop,   &&bjvm_insn_pop2,   &&bjvm_insn_return,   &&bjvm_insn_saload,   &&bjvm_insn_sastore,   &&bjvm_insn_swap,   &&bjvm_insn_anewarray,   &&bjvm_insn_checkcast,   &&bjvm_insn_getfield,   &&bjvm_insn_getstatic,   &&bjvm_insn_instanceof,   &&bjvm_insn_invokedynamic,   &&bjvm_insn_new,   &&bjvm_insn_putfield,   &&bjvm_insn_putstatic,   &&bjvm_insn_invokevirtual,   &&bjvm_insn_invokespecial,   &&bjvm_insn_invokestatic,   &&bjvm_insn_ldc,   &&bjvm_insn_ldc2_w,   &&bjvm_insn_dload,   &&bjvm_insn_fload,   &&bjvm_insn_iload,   &&bjvm_insn_lload,   &&bjvm_insn_dstore,   &&bjvm_insn_fstore,   &&bjvm_insn_istore,   &&bjvm_insn_lstore,   &&bjvm_insn_aload,   &&bjvm_insn_astore,   &&bjvm_insn_goto,   &&bjvm_insn_jsr,   &&bjvm_insn_if_acmpeq,   &&bjvm_insn_if_acmpne,   &&bjvm_insn_if_icmpeq,   &&bjvm_insn_if_icmpne,   &&bjvm_insn_if_icmplt,   &&bjvm_insn_if_icmpge,   &&bjvm_insn_if_icmpgt,   &&bjvm_insn_if_icmple,   &&bjvm_insn_ifeq,   &&bjvm_insn_ifne,   &&bjvm_insn_iflt,   &&bjvm_insn_ifge,   &&bjvm_insn_ifgt,   &&bjvm_insn_ifle,   &&bjvm_insn_ifnonnull,   &&bjvm_insn_ifnull,   &&bjvm_insn_iconst,   &&bjvm_insn_dconst,   &&bjvm_insn_fconst,   &&bjvm_insn_lconst,   &&bjvm_insn_iinc,   &&bjvm_insn_invokeinterface,   &&bjvm_insn_multianewarray,   &&bjvm_insn_newarray,   &&bjvm_insn_tableswitch,   &&bjvm_insn_lookupswitch,   &&bjvm_insn_ret };
    goto *insn_jump_table[insn->kind];

#if ONE_GOTO_PER_INSN
#define NEXT_INSN { insn = &method->code->code[++frame->program_counter]; goto *insn_jump_table[insn->kind]; }
#define JMP_INSN { insn = &method->code->code[frame->program_counter]; goto *insn_jump_table[insn->kind]; }
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
      assert(array->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      bjvm_obj_header *obj = *((bjvm_obj_header **)array_data(array) + index);
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_aastore: {
      bjvm_obj_header *value = checked_pop(frame).obj;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      bjvm_obj_header **obj = (bjvm_obj_header **)array_data(array) + index;
      *obj = value;
      NEXT_INSN;
    }
    bjvm_insn_aconst_null:
      checked_push(frame, (bjvm_stack_value){.obj = NULL});
      NEXT_INSN;
    bjvm_insn_arraylength: {
      bjvm_obj_header *obj = checked_pop(frame).obj;
      if (!obj) {
        // NullPointerException
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      assert(obj->descriptor->kind != BJVM_CD_KIND_ORDINARY);
      checked_push(frame, (bjvm_stack_value){.i = *array_length(obj)});
      NEXT_INSN;
    }
    bjvm_insn_athrow: {
      bjvm_raise_exception_object(thread, checked_pop(frame).obj);
      goto done;
    }
    bjvm_insn_baload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int8_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_bastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int8_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_caload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_CHAR_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((uint16_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_sastore:
    bjvm_insn_castore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_CHAR_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((uint16_t *)array_data(array) + index) = value;
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
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_dadd: {
      checked_push(frame, (bjvm_stack_value){
                              .d = checked_pop(frame).d + checked_pop(frame).d});
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
      checked_push(frame, (bjvm_stack_value) { .d = a / b });
      NEXT_INSN;
    }
    bjvm_insn_dmul: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = a * b });
      NEXT_INSN;
    }
    bjvm_insn_dneg: {
      double a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = -a });
      NEXT_INSN;
    }
    bjvm_insn_drem: { // deprecated
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = fmod(a, b) });
      NEXT_INSN;
    }
    bjvm_insn_dsub: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = a - b });
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
      checked_push(frame, (bjvm_stack_value){.i = (int)a});
      NEXT_INSN;
    }
    bjvm_insn_f2l:
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)checked_pop(frame).f});
      NEXT_INSN;
    bjvm_insn_fadd: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a + b });
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
    bjvm_insn_fdiv:{
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a / b });
      NEXT_INSN;
    }
    bjvm_insn_fmul: {
      float a = checked_pop(frame).f, b = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a * b});
      NEXT_INSN;
    }
    bjvm_insn_fneg:{
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = -a });
      NEXT_INSN;
    }
    bjvm_insn_frem: { // deprecated
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value) { .f = fmodf(a, b) });
      NEXT_INSN;
    }
    bjvm_insn_fsub: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a - b });
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
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int32_t *)array_data(array) + index)});
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
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int32_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_idiv: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, L"/ by zero");
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
        bjvm_arithmetic_exception(thread, L"/ by zero");
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
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int64_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_land: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a & b});
      NEXT_INSN;
    }
    bjvm_insn_lastore:
    bjvm_insn_dastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int64_t *)array_data(array) + index) = value;
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
        bjvm_arithmetic_exception(thread, L"/ by zero");
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
        bjvm_arithmetic_exception(thread, L"/ by zero");
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
      checked_push(frame, (bjvm_stack_value){.l = a >> b});
      NEXT_INSN;
    }
    bjvm_insn_lsub: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a - b});
      NEXT_INSN;
    }
    bjvm_insn_lushr: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a >> b});
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
    bjvm_insn_return: {
      goto done;
    }
    bjvm_insn_saload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_SHORT_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int16_t *)array_data(array) + index)});
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

      bjvm_obj_header *array =
          create_object_array(thread, info->classdesc, count);
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
          bjvm_raise_exception(thread, L"java/lang/ClassCastException", NULL);
          goto done;
        }
      } else {
        checked_push(frame, (bjvm_stack_value){.obj = NULL});
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
        wchar_t complaint[1000];
        swprintf(complaint, 1000, L"Expected nonstatic field %S on class %S", field_info->nat->name, field_info->class_info->name->chars);
        bjvm_incompatible_class_change_error(thread, complaint);
        goto done;
      }
      bjvm_stack_value val;
      if (insn->kind == bjvm_insn_putfield)
        val = checked_pop(frame);
      bjvm_obj_header *obj = checked_pop(frame).obj;

      void *addr = (char *)obj + field_info->field->byte_offset;

      bjvm_type_kind kind =
          field_to_representable_kind(field_info->parsed_descriptor);
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

        bjvm_initialize_class(thread, class->classdesc);
        bjvm_cp_field *field = bjvm_field_lookup(
            class->classdesc, field_info->nat->name, field_info->nat->descriptor);
        field_info->field = field;
        if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
          wchar_t complaint[1000];
          swprintf(complaint, 1000, L"Expected static field %S on class %S", field_info->nat->name, field_info->class_info->name->chars);
          bjvm_incompatible_class_change_error(thread, complaint);
          goto done;
        }
      }

      bjvm_cp_field *field = field_info->field;
      void *field_location =
          &field->my_class->static_fields[field->byte_offset];
      bjvm_type_kind kind =
          field_to_representable_kind(field_info->parsed_descriptor);
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
        bjvm_obj_header *obj = (void*) bjvm_get_class_mirror(thread, ent->class_info.classdesc);
        checked_push(frame, (bjvm_stack_value){.obj = obj});
        break;
      }
      case BJVM_CP_KIND_STRING: {
        bjvm_utf8* s = ent->string.chars;
        bjvm_obj_header *obj = bjvm_intern_string(thread, s->chars, s->len);
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
      frame->program_counter = insn->index;                                     \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                     \
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
      frame->program_counter = insn->index;                                     \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                     \
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
      bjvm_obj_header *array =
          create_primitive_array(thread, insn->array_type, count);
      if (array) {
        checked_push(frame, (bjvm_stack_value){.obj = array});
      } else {  // failed to create array
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
  if (thread->current_exception != NULL) {
    bjvm_attribute_exception_table *table = method->code->exception_table;
    if (table) {
      int pc = frame->program_counter;
      for (int i = 0; i < table->entries_count; ++i) {
        bjvm_exception_table_entry ent = table->entries[i];
        if (ent.start_insn <= pc && pc < ent.end_insn) {
          if (ent.catch_type) {
            int error = bjvm_resolve_class(thread, ent.catch_type);
            if (error) goto done;  // O dear
          }
          if (!ent.catch_type ||
              bjvm_instanceof(thread->current_exception->descriptor,
                              ent.catch_type->classdesc)) {
            frame->program_counter = ent.handler_pc;
            frame->stack_depth = 1;
            frame->values[0] =
                (bjvm_stack_value){.obj = thread->current_exception};
            thread->current_exception = NULL;

            goto start;
          }
        }
      }
    }

    return -1;
  }

  return 0;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (!vm->main_thread_group) {
    bjvm_classdesc *ThreadGroup =
        bootstrap_class_create(thread, L"java/lang/ThreadGroup");
    int error = bjvm_initialize_class(thread, ThreadGroup);
    assert(!error);

    bjvm_cp_method *init =
        bjvm_easy_method_lookup(ThreadGroup, "<init>", "()V", false, false);

    assert(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    bjvm_thread_run(thread, init, args, NULL);
  }
  return vm->main_thread_group;
}
