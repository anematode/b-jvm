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

// A thread-local handle to an underlying object. Used in case the object is
// relocated.
//
// Implementation-wise, each thread contains an array of pointers to objects
// fixed in memory. This handle points into that array. During GC compaction/
// relocation, the array is updated to point to the new locations of the
// objects.
typedef struct {
  bjvm_obj_header *obj;
} bjvm_handle;

// Value set as viewed by native functions (rather than by the VM itself,
// which deals in bjvm_stack_value).
typedef union {
  int64_t l;
  int i;
  float f;
  double d;
  bjvm_handle *handle;
} bjvm_value;

// Generally, use this to indicate that a native function is returning void or
// null
static inline bjvm_stack_value value_null() {
  return (bjvm_stack_value){.obj = nullptr};
}

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target);

typedef bjvm_stack_value (*bjvm_native_callback)(bjvm_thread *vm,
                                                 bjvm_handle *obj,
                                                 bjvm_value *args, int argc);

// represents a native method somewhere in this binary
typedef struct {
  bjvm_utf8 class_path;
  bjvm_utf8 method_name;
  bjvm_utf8 method_descriptor;
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

void read_string(bjvm_thread *thread, bjvm_obj_header *obj, short **buf,
                 size_t *len); // todo: get rid of
heap_string read_string_to_utf8(bjvm_obj_header *obj);

struct bjvm_class_loader;
typedef struct bjvm_vm bjvm_vm;

typedef void (*bjvm_write_byte)(int ch, void *param);

#define BJVM_CARD_BYTES 4096

typedef struct bjvm_vm {
  // Map class file name -> cf bytes
  bjvm_string_hash_table classfiles;
  // Map class name (e.g. "java/lang/String") to classdesc*
  bjvm_string_hash_table classes;
  // Classes currently under creation -- used to detect circularity
  bjvm_string_hash_table inchoate_classes;

  // Native methods in javah form
  bjvm_string_hash_table natives;

  int (*load_classfile)(const bjvm_utf8 filename, void *param, uint8_t **bytes,
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

  // Active threads (unused for now)
  bjvm_thread **active_threads;
  int active_thread_count;
  int active_thread_cap;

  uint8_t *heap;
  // Next object should be allocated here. Should always be 8-byte aligned
  // which is the alignment of BJVM objects.
  size_t heap_used;
  size_t heap_capacity;
} bjvm_vm;

typedef struct {
  // Callback to load a classfile from the classpath. Returns 0 on success,
  // nonzero on failure. Pointer passed to bytes will be free()-d by the VM.
  int (*load_classfile)(const bjvm_utf8 filename, void *param, uint8_t **bytes,
                        size_t *len);
  void *load_classfile_param;

  // Write byte of stdout/stderr (if nullptr, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
  // Passed to write_stdout/write_stderr
  void *write_byte_param;

  // Heap size (static for now)
  size_t heap_size;
} bjvm_vm_options;

typedef struct {
  uint16_t program_counter; // in instruction indices
  uint16_t max_stack;
  uint16_t max_locals;
  uint16_t stack_depth;

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

  // Array of handles, see bjvm_handle (null entries are free for use)
  // TODO make this a linked list to accommodate arbitrary # of handles
  bjvm_handle *handles;
  int handles_capacity;

  // Null handle, for convenience
  bjvm_handle null_handle;

  // Thread-local allocation buffer (objects are first created here)
} bjvm_thread;

bjvm_handle *bjvm_make_handle(bjvm_thread *thread, bjvm_obj_header *obj);

void bjvm_drop_handle(bjvm_thread *thread, bjvm_handle *handle);

bjvm_array_classdesc *
bjvm_checked_to_array_classdesc(bjvm_classdesc *classdesc);
bjvm_classdesc *
bjvm_checked_to_primitive_array_classdesc(bjvm_classdesc *classdesc);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method);

void pass_args_to_frame(bjvm_stack_frame *new_frame,
                        bjvm_stack_frame *old_frame, int args,
                        const bjvm_method_descriptor *descriptor,
                        bool is_static);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference);

bjvm_vm_options bjvm_default_vm_options();
bjvm_vm *bjvm_create_vm(bjvm_vm_options options);
void bjvm_major_gc(bjvm_vm *vm);

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
int bjvm_vm_preregister_classfile(bjvm_vm *vm, const bjvm_utf8 filename,
                                  const uint8_t *bytes, size_t len);

/**
 * Read the classfile in the class path. Returns -1 on failure to find the
 * classfile. Writes a pointer to the classfile bytes and the length of the
 * classfile to the given pointers.
 */
int bjvm_vm_read_classfile(bjvm_vm *vm, const bjvm_utf8 filename,
                           const uint8_t **bytes, size_t *len);

void bjvm_vm_list_classfiles(bjvm_vm *vm, heap_string *strings, size_t *count);

/**
 * Free the classfile.
 */
void bjvm_free_classfile(bjvm_classdesc cf);

void bjvm_free_vm(bjvm_vm *vm);

/**
 * Implementation details, but exposed for testing...
 */
void free_field_descriptor(bjvm_field_descriptor descriptor);
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const bjvm_utf8 name);
int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const bjvm_utf8 name,
                                        const bjvm_utf8 descriptor,
                                        bool superclasses,
                                        bool superinterfaces);
bjvm_utf8 bjvm_make_utf8_cstr(const bjvm_utf8 c_literal);
int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                    bjvm_stack_value *args, bjvm_stack_value *result);
int bjvm_initialize_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
void bjvm_register_native(bjvm_vm *vm, const bjvm_utf8 class_name,
                          const bjvm_utf8 method_name,
                          const bjvm_utf8 method_descriptor,
                          bjvm_native_callback callback);

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result);

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror);

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_method(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value);
bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field);
bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const bjvm_utf8 name,
                                      const bjvm_utf8 descriptor);
bjvm_type_kind field_to_kind(const bjvm_field_descriptor *field);
int bjvm_raise_exception(bjvm_thread *thread, const bjvm_utf8 exception_name,
                         const bjvm_utf8 exception_string);
void bjvm_null_pointer_exception(bjvm_thread *thread);

// e.g. int.class
struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread,
                                                      bjvm_type_kind prim_kind);

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const bjvm_utf8 chars);
int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

#include "natives_gen.h"

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc);
struct bjvm_native_ConstantPool *bjvm_get_constant_pool_mirror(
  bjvm_thread *thread, bjvm_classdesc *classdesc);

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field);
void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method);
void bjvm_reflect_initialize_method(bjvm_thread *thread,
                                    bjvm_classdesc *classdesc,
                                    bjvm_cp_method *method);
bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread,
                                               bjvm_utf8 name);
int bjvm_get_line_number(const bjvm_attribute_code *method, uint16_t pc);
void store_stack_value(void *field_location, bjvm_stack_value value,
                       bjvm_type_kind kind);
bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind);

static inline int sizeof_type_kind(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_BOOLEAN:
    return 1;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    return 2;
  case BJVM_TYPE_KIND_FLOAT:
  case BJVM_TYPE_KIND_INT:
    return 4;
  case BJVM_TYPE_KIND_REFERENCE:
    return sizeof(void *);
  case BJVM_TYPE_KIND_DOUBLE:
  case BJVM_TYPE_KIND_LONG:
    return 8;
  default:
    UNREACHABLE();
  }
}

bjvm_classdesc *bjvm_primitive_classdesc(bjvm_thread *thread,
                                         bjvm_type_kind prim_kind);
void *bump_allocate(bjvm_thread *thread, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif // BJVM_H
