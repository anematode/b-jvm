//
// Created by Cowpox on 12/10/24.
//

#ifndef BJVM_H
#define BJVM_H

#include <stdint.h>
#include <wchar.h>

#include "adt.h"
#include "classfile.h"
#include "util.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bjvm_thread bjvm_thread;
typedef struct bjvm_obj_header bjvm_obj_header;

/**
 * For simplicity, we always store local variables/stack variables as 64 bits,
 * and only use part of them in the case of integer or float (or, in 32-bit
 * mode, reference) values.
 */
typedef union {
  int64_t l;
  int i; // used for all integer types except long, plus boolean
  float f;
  double d;
  bjvm_obj_header *obj; // reference type
} bjvm_stack_value;

// Generally, use this to indicate that a native function is returning void or
// null
static inline bjvm_stack_value value_null() {
  return (bjvm_stack_value){.obj = nullptr};
}

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target);

typedef bjvm_stack_value (*bjvm_native_callback)(bjvm_thread *vm,
                                                 bjvm_obj_header *obj,
                                                 bjvm_stack_value *args,
                                                 int argc);

// represents a native method somewhere in this binary
typedef struct {
  char const *class_path;
  char const *method_name;
  char const *method_descriptor;
  bjvm_native_callback callback;
} bjvm_native_t;

typedef struct bjvm_array_classdesc bjvm_array_classdesc;

typedef uint64_t bjvm_mark_word_t;

typedef struct bjvm_ordinary_class bjvm_ordinary_class;

// Equivalent to HotSpot's InstanceKlass
typedef struct bjvm_ordinary_class {
  bjvm_classdesc base;
  bjvm_classdesc classfile;
} bjvm_ordinary_classdesc;

// Appears at the top of every object -- corresponds to HotSpot's oopDesc
typedef struct bjvm_obj_header {
  volatile bjvm_mark_word_t mark_word;
  bjvm_classdesc *descriptor;
} bjvm_obj_header;

struct bjvm_class_loader;
typedef struct bjvm_vm bjvm_vm;

typedef void (*bjvm_write_byte)(int ch, void *param);

typedef struct bjvm_vm {
  // Map class file name -> cf bytes
  bjvm_string_hash_table classfiles;
  // Map class name (e.g. "java/lang/String") to classdesc*
  bjvm_string_hash_table classes;
  // Classes currently under creation -- used to detect circularity
  bjvm_string_hash_table inchoate_classes;

  // Native methods in javah form
  bjvm_string_hash_table natives;

  int (*load_classfile)(const char *filename, void *param, uint8_t **bytes,
                        size_t *len);
  void *load_classfile_param;

  // Main thread group
  bjvm_obj_header *main_thread_group;

  // Interned strings (string -> instance of java/lang/String)
  bjvm_string_hash_table interned_strings;

  // Classes with implementation-required padding before other fields (map class
  // name -> padding bytes)
  bjvm_string_hash_table class_padding;

  // Write byte of stdout/stderr (if nullptr, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;

  // Passed to write_stdout/write_stderr
  void *write_byte_param;

  // Primitive classes (int.class, etc., boolean (4 -> 0) through void (12 -> 8)
  // )
  bjvm_classdesc *primitive_classes[9];
} bjvm_vm;

typedef struct {
  bool unused;

  // Callback to load a classfile from the classpath. Returns 0 on success,
  // nonzero on failure. Pointer passed to bytes will be free()-d by the VM.
  int (*load_classfile)(const char *filename, void *param, uint8_t **bytes,
                        size_t *len);
  void *load_classfile_param;

  // Write byte of stdout/stderr (if nullptr, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
  // Passed to write_stdout/write_stderr
  void *write_byte_param;
} bjvm_vm_options;

typedef struct {
  int program_counter; // in instruction indices
  int max_stack;
  int max_locals;
  int stack_depth;

  bjvm_cp_method *method;

  // First max_locals bjvm_stack_values, then max_stack more
  bjvm_stack_value values[];
} bjvm_stack_frame;

typedef struct bjvm_thread {
  // Global VM corresponding to this thread
  bjvm_vm *vm;

  // Essentially the stack of the thread -- a contiguous buffer which stores
  // stack_frames
  uint8_t *frame_buffer;
  uint32_t frame_buffer_capacity;
  // Also pointer one past the end of the last stack frame
  uint32_t frame_buffer_used;

  // Pointers into the frame_buffer
  bjvm_stack_frame **frames;
  uint32_t frames_count;
  uint32_t frames_cap;

  // Currently propagating exception
  bjvm_obj_header *current_exception;

  bool js_jit_enabled;

  // Instance of java.lang.Thread
  struct bjvm_native_Thread *thread_obj;
} bjvm_thread;

bjvm_array_classdesc *
bjvm_checked_to_array_classdesc(bjvm_classdesc *classdesc);
bjvm_classdesc *
bjvm_checked_to_primitive_array_classdesc(bjvm_classdesc *classdesc);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference);

bjvm_vm *bjvm_create_vm(bjvm_vm_options options);

typedef struct {
  uint32_t stack_space;
  // Whether to enable JavaScript JIT compilation
  bool js_jit_enabled;
  // What thread group to construct the thread in (nullptr = default thread
  // group)
  bjvm_obj_header *thread_group;
  // Write byte of stdout/stderr (if nullptr, prints directly to stdout/stderr)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
} bjvm_thread_options;

bjvm_thread_options bjvm_default_thread_options();
bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options);
void bjvm_free_thread(bjvm_thread *thread);

/**
 * Directly add the given classfile as accessible to the VM, bypassing the
 * callback to load_classfile.
 */
int bjvm_vm_preregister_classfile(bjvm_vm *vm, const wchar_t *filename,
                                  const uint8_t *bytes, size_t len);

/**
 * Read the classfile in the class path. Returns -1 on failure to find the
 * classfile. Writes a pointer to the classfile bytes and the length of the
 * classfile to the given pointers.
 */
int bjvm_vm_read_classfile(bjvm_vm *vm, const wchar_t *filename,
                           const uint8_t **bytes, size_t *len);

void bjvm_vm_list_classfiles(bjvm_vm *vm, wchar_t **strings, size_t *count);

/**
 * Parse a Java class file.
 *
 * The error message corresponds to a ClassFormatError in Java land.
 * (UnsupportedClassVersionErrors and VerifyErrors should be raised elsewhere.)
 *
 * @param bytes Start byte of the classfile.
 * @param len Length of the classfile in bytes.
 * @param result Where to write the result.
 * @return nullptr on success, otherwise an error message (which is the caller's
 * responsibility to free).
 */
char *bjvm_parse_classfile(uint8_t *bytes, size_t len, bjvm_classdesc *result);

/**
 * Free the classfile.
 */
void bjvm_free_classfile(bjvm_classdesc cf);

void bjvm_free_vm(bjvm_vm *vm);

/**
 * Implementation details, but exposed for testing...
 */
void free_utf8(bjvm_utf8 entry);
void free_field_descriptor(bjvm_field_descriptor descriptor);
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const wchar_t *name);
int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const char *name,
                                        const char *descriptor,
                                        bool superclasses,
                                        bool superinterfaces);
bjvm_utf8 bjvm_make_utf8_cstr(const char *c_literal);
int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                    bjvm_stack_value *args, bjvm_stack_value *result);
int bjvm_initialize_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
void bjvm_register_native(bjvm_vm *vm, const char *class_name,
                          const char *method_name,
                          const char *method_descriptor,
                          bjvm_native_callback callback);

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc);

int *array_length(bjvm_obj_header *array);

void *array_data(bjvm_obj_header *array);

bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror);

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror);

bjvm_obj_header *create_object_array(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc, int count);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value);
bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field);
bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const wchar_t *name,
                                      const wchar_t *descriptor);
bjvm_type_kind field_to_representable_kind(const bjvm_field_descriptor *field);
int bjvm_raise_exception(bjvm_thread *thread, const wchar_t *exception_name,
                         const wchar_t *exception_string);

// e.g. int.class
struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread,
                                                      bjvm_type_kind prim_kind);

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const wchar_t *chars,
                                    size_t len);
int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

#include "natives_gen.h"

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc);

#ifdef __cplusplus
}
#endif

#endif // BJVM_H
