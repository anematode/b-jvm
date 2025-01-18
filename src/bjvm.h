//
// Created by Cowpox on 12/10/24.
//

#ifndef BJVM_H
#define BJVM_H

#include <async.h>
#include <stdint.h>
#include <wchar.h>

#include "adt.h"
#include "classfile.h"
#include "classpath.h"
#include "util.h"

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bjvm_thread bjvm_thread;
typedef struct bjvm_obj_header bjvm_obj_header;

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE // used to allow access from JS
#endif

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

typedef bjvm_stack_value (*bjvm_sync_native_callback)(bjvm_thread *vm,
                                                      bjvm_handle *obj,
                                                      bjvm_value *args,
                                                      int argc);
typedef future_t (*bjvm_async_native_callback)(void *ctx,
    bjvm_thread *vm, bjvm_handle *obj, bjvm_value *args, int argc);

typedef struct {
  // Number of bytes needed for the context struct allocation (0 if sync)
  size_t async_ctx_bytes;
  // either bjvm_sync_native_callback or bjvm_async_native_callback
  void *ptr;
} bjvm_native_callback;

// represents a native method somewhere in this binary
typedef struct {
  bjvm_utf8 class_path;
  bjvm_utf8 method_name;
  bjvm_utf8 method_descriptor;
  bjvm_native_callback callback;
} bjvm_native_t;

typedef uint64_t bjvm_mark_word_t;

// Appears at the top of every object -- corresponds to HotSpot's oopDesc
typedef struct bjvm_obj_header {
#ifdef BJVM_MULTITHREADED

#endif
  volatile bjvm_mark_word_t mark_word;
  bjvm_classdesc *descriptor;
} bjvm_obj_header;

void read_string(bjvm_thread *thread, bjvm_obj_header *obj, short **buf,
                 size_t *len); // todo: get rid of
int read_string_to_utf8(bjvm_thread *thread,
                                              heap_string *result,
                                              bjvm_obj_header *obj);
typedef struct bjvm_vm bjvm_vm;

typedef void (*bjvm_write_byte)(int ch, void *param);

#define BJVM_CARD_BYTES 4096

typedef struct bjvm_plain_frame bjvm_plain_frame;
typedef struct bjvm_stack_frame bjvm_stack_frame;
typedef struct bjvm_native_frame bjvm_native_frame;


DECLARE_ASYNC(bjvm_stack_value, bjvm_run_native, void *native_struct;, bjvm_thread *thread,
              bjvm_native_frame *frame);

DECLARE_ASYNC(
    int, bjvm_initialize_class,
    union {
      bjvm_initialize_class_t *recursive_call_space;
      struct bjvm_interpret_s *interpret;
    };
    uint16_t i;, bjvm_thread *thread, bjvm_classdesc *classdesc);

DECLARE_ASYNC(struct bjvm_native_MethodType *, resolve_mh_mt,
              bjvm_initialize_class_t ic;
              , bjvm_thread *thread, bjvm_cp_method_handle_info *info);

DECLARE_ASYNC(
    struct bjvm_native_MethodHandle *, bjvm_resolve_method_handle,
    union {
      bjvm_initialize_class_t init_class_state;
      resolve_mh_mt_t resolve;
    };
    bjvm_classdesc * DirectMethodHandle; bjvm_cp_method * m;
    , bjvm_thread *thread, bjvm_cp_method_handle_info *info);

typedef struct bjvm_interpret_s bjvm_interpret_t;

DECLARE_ASYNC_VOID(bjvm_invokevirtual_signature_polymorphic,
bjvm_interpret_t *interpreter_ctx; bjvm_cp_method *method; int argc;,
bjvm_thread *thread, bjvm_plain_frame *frame, int *sd,
  bjvm_cp_method *method, struct bjvm_native_MethodType *provider_mt,
  bjvm_obj_header *target);

DECLARE_ASYNC(bjvm_value, bjvm_resolve_indy_static_argument,
              bjvm_resolve_method_handle_t resolve;
              , bjvm_thread *thread, bjvm_cp_entry *ent, bool *is_object);

DECLARE_ASYNC(
    int, indy_resolve,
    int static_i;
    bjvm_plain_frame *fake_frame;
    union {
      bjvm_resolve_method_handle_t mh;
      bjvm_resolve_indy_static_argument_t static_arg;
      bjvm_invokevirtual_signature_polymorphic_t invoke;
    };
    , bjvm_thread *thread, bjvm_bytecode_insn *insn, bjvm_cp_indy_info *indy)
  ;

DECLARE_ASYNC(int, resolve_methodref,
  bjvm_cp_class_info *klass; bjvm_initialize_class_t initializer_ctx;,
  bjvm_thread *thread, bjvm_cp_method_info *info);

// Continue execution of a thread.
//
// When popping frames off the stack, if the passed frame "final_frame" is
// popped off, the result of that frame (if any) is placed in "result", and
// either INTERP_RESULT_OK or INTERP_RESULT_EXC is returned, depending on
// whether the frame completed abruptly.
EMSCRIPTEN_KEEPALIVE
DECLARE_ASYNC(
    bjvm_stack_value, bjvm_interpret, int sd; bjvm_stack_frame *invoked_frame; union {
      bjvm_initialize_class_t init_class;
      indy_resolve_t indy_resolve;
      bjvm_run_native_t native;
      resolve_methodref_t methodref_resolve;
      bjvm_invokevirtual_signature_polymorphic_t invoke_sigpoly;
    };
    , bjvm_thread *thread, bjvm_stack_frame *final_frame);

struct bjvm_cached_classdescs;
typedef struct bjvm_vm {
  // Map class name (e.g. "java/lang/String") to classdesc*
  bjvm_string_hash_table classes;
  // Classes currently under creation -- used to detect circularity
  bjvm_string_hash_table inchoate_classes;

  // Native methods in javah form
  bjvm_string_hash_table natives;

  // Bootstrap class loader will look for classes here
  bjvm_classpath classpath;

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

  // Primitive classes (int.class, etc.)
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

  // This capacity is used solely when handling an OutOfMemoryError, because
  // fillInStackTrace allocates stuff. The reserved space is a constant in
  // bjvm.c.
  size_t true_heap_capacity;

  // Handles referenced from JS
  bjvm_obj_header **js_handles;

  /// Struct containing cached classdescs
  struct bjvm_cached_classdescs *cached_classdescs;
} bjvm_vm;

typedef struct {
  // Write byte of stdout/stderr (if nullptr, uses the default implementation)
  bjvm_write_byte write_stdout;
  bjvm_write_byte write_stderr;
  // Passed to write_stdout/write_stderr
  void *write_byte_param;

  // Heap size (static for now)
  size_t heap_size;
  // Classpath for built-in files, e.g. rt.jar. Must have definitions for
  // Object.class, etc.
  bjvm_utf8 runtime_classpath;
  // Colon-separated custom classpath.
  bjvm_utf8 classpath;
} bjvm_vm_options;

// Stack frame associated with a Java method.
//
// Frames are aligned to 8 bytes, the natural alignment of a stack value.
// Layout:
//             -> stack grows this way
// ┌──────────┬───────────────────────────────┬───────────────────────────────────────────────┐
// │ metadata │ max_stack x bjvm_stack_value  │ (values_count - max_stack) x
// bjvm_stack_value │
// └──────────┴───────────────────────────────┴───────────────────────────────────────────────┘
//                      stack values                            local values
//
// The stack depth should be inferred from the program counter: In particular,
// the method contains an analysis of the stack depth at each instruction.
typedef struct bjvm_plain_frame {
  uint16_t is_native; // always 0
  uint16_t values_count;
  uint16_t program_counter; // in instruction indices
  uint16_t max_stack;

  // The method associated with this frame
  bjvm_cp_method *method;

  bjvm_stack_value values[];
} bjvm_plain_frame;

// Stack frame associated with a native method. Note that this is stored
// separately from the (inaccessible) WebAssembly stack, and merely contains
// data necessary for correct stack trace recovery and resumption after
// interrupts.
typedef struct bjvm_native_frame {
  uint16_t is_native;    // always 1
  uint16_t values_count; // number of args passed into the native method

  // Used by async native methods for their state machines
  int state;

  // see bjvm_plain_frame
  bjvm_cp_method *method;
  // Descriptor on the instruction itself. Unequal to method->descriptor only
  // in the situation of signature-polymorphic methods.
  const bjvm_method_descriptor *method_shape;

  // Arguments to the native method, including the class instance as the first
  // argument (if the method is not static)
  bjvm_value values[];
} bjvm_native_frame;

// A frame is either a native frame or a plain frame. They may be distinguished
// with bjvm_is_frame_native.
//
// Native frames may be consecutive: for example, a native method might invoke
// another native method, which itself raises an interrupt.
typedef struct bjvm_stack_frame {
  bjvm_interpret_t async_frame;

  union {
    bjvm_plain_frame plain;
    bjvm_native_frame native;
  };
} bjvm_stack_frame;

// Get the current stack depth of the interpreted frame, based on the program
// counter.
uint16_t stack_depth(const bjvm_plain_frame *frame);

bool bjvm_is_frame_native(const bjvm_stack_frame *frame);

bjvm_native_frame *bjvm_get_native_frame(bjvm_stack_frame *frame);
bjvm_plain_frame *bjvm_get_plain_frame(bjvm_stack_frame *frame);
bjvm_cp_method *bjvm_get_frame_method(bjvm_stack_frame *frame);

EMSCRIPTEN_KEEPALIVE
bjvm_obj_header *bjvm_deref_js_handle(bjvm_vm *vm, int index);
EMSCRIPTEN_KEEPALIVE
int bjvm_make_js_handle(bjvm_vm *vm, bjvm_obj_header *obj);
EMSCRIPTEN_KEEPALIVE
void bjvm_drop_js_handle(bjvm_vm *vm, int index);

typedef struct bjvm_thread {
  // Global VM corresponding to this thread
  bjvm_vm *vm;

  // Essentially the stack of the thread -- a contiguous buffer which stores
  // stack_frames
  char *frame_buffer;
  uint32_t frame_buffer_capacity;
  // Also pointer one past the end of the last stack frame
  uint32_t frame_buffer_used;

  // Pre-allocated out-of-memory and stack overflow errors
  bjvm_obj_header *out_of_mem_error;
  bjvm_obj_header *stack_overflow_error;

  // Pointers into the frame_buffer
  bjvm_stack_frame **frames;
  int frames_count;
  uint32_t frames_cap;

  // Currently propagating exception
  bjvm_obj_header *current_exception;
  // Frame in which we are raising a language Throwable (as opposed to a
  // "manually" generated Throwable). Used so that the stack trace in such
  // cases doesn't include the <init> frame. -1 if not applicable.
  // See Throwable:fillInStackTrace for more information.
  int lang_exception_frame;

  bool js_jit_enabled;

  // Instance of java.lang.Thread
  struct bjvm_native_Thread *thread_obj;

  // Array of handles, see bjvm_handle (null entries are free for use)
  // TODO make this a linked list to accommodate arbitrary # of handles
  bjvm_handle *handles;
  int handles_capacity;

  // Handle for null
  bjvm_handle null_handle;

  // If true, the call stack must fully unwind because we are e.g. waiting for
  // a JS Promise. This should only be cleared by the top-level scheduler.
  // This function is used in bjvm_thread_run, which attempts to run code in
  // a synchronous manner.
  bool must_unwind;

  // Thread-local allocation buffer (objects are first created here)
} bjvm_thread;

bjvm_handle *bjvm_make_handle(bjvm_thread *thread, bjvm_obj_header *obj);

void bjvm_drop_handle(bjvm_thread *thread, bjvm_handle *handle);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method,
                                  bjvm_stack_value *args, int argc);

bjvm_stack_frame *bjvm_push_plain_frame(bjvm_thread *thread,
                                        bjvm_cp_method *method,
                                        bjvm_stack_value *args, int argc);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference);

bjvm_vm_options bjvm_default_vm_options();

bjvm_vm *bjvm_create_vm(bjvm_vm_options options);

bjvm_vm_options *bjvm_default_vm_options_ptr();

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
 * Free the classfile.
 */
void bjvm_free_classfile(bjvm_classdesc cf);

void bjvm_free_vm(bjvm_vm *vm);

bjvm_classdesc *bootstrap_lookup_class(bjvm_thread *thread, bjvm_utf8 name);
bjvm_classdesc *bjvm_define_bootstrap_class(bjvm_thread *thread,
                                            bjvm_utf8 chars,
                                            const uint8_t *classfile_bytes,
                                            size_t classfile_len);
int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *classdesc,
                                   const bjvm_utf8 name,
                                   const bjvm_utf8 descriptor,
                                   bool superclasses, bool superinterfaces);

// Run the root-level interpreter, getting stuck if we hit an asynchronous
// function. This should only be used when you know that you're not going to be
// calling any asynchronous functions. (e.g., initializing most JDK classes).
// Cannot be called if an interpreter is already running.
int bjvm_thread_run_root(bjvm_thread *thread, bjvm_cp_method *method,
                         bjvm_stack_value *args, bjvm_stack_value *result);

// Run a short-lived, second-level interpreter.  The provided method may NEVER
// block.
int bjvm_thread_run_leaf(bjvm_thread *thread, bjvm_cp_method *method,
                         bjvm_stack_value *args, bjvm_stack_value *result);

typedef enum {
  BJVM_ASYNC_RUN_RESULT_OK,
  BJVM_ASYNC_RUN_RESULT_EXC,
  BJVM_ASYNC_RUN_RESULT_INT,
} bjvm_async_run_result;

typedef struct {
  bjvm_thread *thread;
  bjvm_stack_frame *frame;
  bjvm_stack_value *result;
  bjvm_interpret_t interpreter_state;
  bjvm_async_run_result status;
} bjvm_async_run_ctx;

// Get an asynchronous running context. The caller should repeatedly call
// bjvm_async_run_step() until it returns true.
EMSCRIPTEN_KEEPALIVE
bjvm_async_run_ctx *bjvm_thread_async_run(bjvm_thread *thread,
                                          bjvm_cp_method *method,
                                          bjvm_stack_value *args,
                                          bjvm_stack_value *result);

EMSCRIPTEN_KEEPALIVE
bool bjvm_async_run_step(bjvm_async_run_ctx *ctx);

void bjvm_free_async_run_ctx(bjvm_async_run_ctx *ctx);

void bjvm_register_native(bjvm_vm *vm, const bjvm_utf8 class_name,
                          const bjvm_utf8 method_name,
                          const bjvm_utf8 method_descriptor,
                          bjvm_native_callback callback);

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
int bjvm_raise_vm_exception(bjvm_thread *thread, const bjvm_utf8 exception_name,
                            const bjvm_utf8 exception_string);
void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj);
void bjvm_null_pointer_exception(bjvm_thread *thread);
void bjvm_array_index_oob_exception(bjvm_thread *thread, int index, int length);
void bjvm_negative_array_size_exception(bjvm_thread *thread, int count);
void bjvm_incompatible_class_change_error(bjvm_thread *thread,
                                          bjvm_utf8 complaint);
void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method);
void bjvm_abstract_method_error(bjvm_thread *thread,
                                const bjvm_cp_method *method);
int bjvm_invokestatic(bjvm_thread *thread,
                                            bjvm_plain_frame *frame,
                                            bjvm_bytecode_insn *insn, int *sd);
void dump_frame(FILE *stream, const bjvm_plain_frame *frame);

// e.g. int.class
struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread,
                                                      bjvm_type_kind prim_kind);

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const bjvm_utf8 chars);
bjvm_obj_header *make_string(bjvm_thread *thread, bjvm_utf8 string);
int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

#include "natives_gen.h"

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                                bjvm_classdesc *classdesc);
struct bjvm_native_ConstantPool *
bjvm_get_constant_pool_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc);

bjvm_utf8 bjvm_unparse_field_descriptor(bjvm_utf8 str,
                                        const bjvm_field_descriptor *desc);
void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field);
void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method);
void bjvm_reflect_initialize_method(bjvm_thread *thread,
                                    bjvm_classdesc *classdesc,
                                    bjvm_cp_method *method);
bjvm_obj_header *bjvm_reflect_get_method_parameters(bjvm_thread *thread,
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
