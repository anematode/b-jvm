//
// Created by Cowpox on 12/10/24.
//

#ifndef BJVM_H
#define BJVM_H

#include <async.h>
#include <stdint.h>
#include <wchar.h>

#include "classfile.h"
#include "classpath.h"

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
 * These typedefs are to be used for any values that a Java program might see.  These mimick JNI types.
 */
typedef int32_t jint;
typedef int64_t jlong;
typedef float jfloat;
typedef double jdouble;
typedef bool jboolean;
typedef int8_t jbyte;
typedef int16_t jshort;
typedef uint16_t jchar;
typedef bjvm_obj_header *object;

/**
 * For simplicity, we always store local variables/stack variables as 64 bits,
 * and only use part of them in the case of integer or float (or, in 32-bit
 * mode, reference) values.
 */
typedef union {
  jlong l;
  jint i; // used for all integer types except long, plus boolean
  jfloat f;
  jdouble d;
  object obj; // reference type
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
  int32_t i;
  float f;
  double d;
  bjvm_handle *handle;
} bjvm_value;

// Generally, use this to indicate that a native function is returning void or
// null
static inline bjvm_stack_value value_null() { return (bjvm_stack_value){.obj = nullptr}; }

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *target);

typedef struct {
  bjvm_thread *thread;
  bjvm_handle *obj;
  bjvm_value *args;
  uint8_t argc;
} async_natives_args_inner;

typedef struct {
  async_natives_args_inner args;
  uint32_t stage;
  bjvm_stack_value result;
} async_natives_args;

typedef bjvm_stack_value (*sync_native_callback)(bjvm_thread *vm, bjvm_handle *obj, bjvm_value *args, uint8_t argc);
typedef future_t (*async_native_callback)(void *args);

typedef struct {
  // Number of bytes needed for the context struct allocation (0 if sync)
  size_t async_ctx_bytes;
  // either sync_native_callback or async_native_callback
  union {
    sync_native_callback sync;
    async_native_callback async;
  };
} bjvm_native_callback;

// represents a native method somewhere in this binary
typedef struct {
  slice class_path;
  slice method_name;
  slice method_descriptor;
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

void read_string(bjvm_thread *thread, bjvm_obj_header *obj, int8_t **buf,
                 size_t *len); // todo: get rid of
int read_string_to_utf8(bjvm_thread *thread, heap_string *result, bjvm_obj_header *obj);
typedef struct bjvm_vm bjvm_vm;

typedef void (*bjvm_write_byte)(int ch, void *param);

#define BJVM_CARD_BYTES 4096

typedef struct bjvm_plain_frame bjvm_plain_frame;
typedef struct bjvm_stack_frame bjvm_stack_frame;
typedef struct bjvm_native_frame bjvm_native_frame;

// Continue execution of a thread.
//
// When popping frames off the stack, if the passed frame "final_frame" is
// popped off, the result of that frame (if any) is placed in "result", and
// either INTERP_RESULT_OK or INTERP_RESULT_EXC is returned, depending on
// whether the frame completed abruptly.
DECLARE_ASYNC(
    bjvm_stack_value, bjvm_interpret,
    locals(
      uint16_t sd;
      uint16_t async_ctx; // offset within the secondary stack
    ),
    arguments(
      bjvm_thread *thread;
      bjvm_stack_frame *raw_frame;
    ),
);

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

// runs interpreter (async)
DECLARE_ASYNC(bjvm_stack_value, call_interpreter,
  locals(bjvm_async_run_ctx *ctx),
  arguments(
    bjvm_thread *thread;
    bjvm_cp_method *method;
    bjvm_stack_value *args;
  ),
  invoked_methods(invoked_method(bjvm_interpret))
);

bjvm_stack_value call_interpreter_synchronous(bjvm_thread *thread, bjvm_cp_method *method,
                                                     bjvm_stack_value *args);

DECLARE_ASYNC(bjvm_stack_value, bjvm_run_native,
  locals(async_natives_args *native_struct),
  arguments(bjvm_thread *thread; bjvm_stack_frame *frame),
  invoked_methods()
);

DECLARE_ASYNC(
    int, bjvm_initialize_class,
    locals(bjvm_initialize_class_t *recursive_call_space; uint16_t i),
    arguments(bjvm_thread *thread; bjvm_classdesc *classdesc),
    invoked_methods(
        invoked_method(call_interpreter)
      )
);

DECLARE_ASYNC(struct bjvm_native_MethodType *, resolve_mh_mt, locals(bjvm_handle *ptypes_array),
              arguments(bjvm_thread *thread; bjvm_cp_method_handle_info *info),
              invoked_methods(
                invoked_method(bjvm_initialize_class)
                invoked_method(call_interpreter)
                ));

DECLARE_ASYNC(bjvm_obj_header*, resolve_mh_vh,
  locals(),
  arguments(bjvm_thread *thread; bjvm_cp_method_handle_info *info),
  invoked_methods(
    invoked_method(bjvm_initialize_class)
    invoked_method(call_interpreter)
  )
);

DECLARE_ASYNC(bjvm_obj_header*, resolve_mh_invoke,
  locals(bjvm_handle *member; bjvm_cp_method *m),
  arguments(bjvm_thread *thread; bjvm_cp_method_handle_info *info),
  invoked_methods(
    invoked_method(bjvm_initialize_class)
    invoked_method(call_interpreter)
  )
);

DECLARE_ASYNC(
    struct bjvm_native_MethodHandle *, bjvm_resolve_method_handle,
    locals(bjvm_classdesc * DirectMethodHandle; bjvm_classdesc * MemberName; bjvm_cp_method * m; bjvm_cp_field *f),
    arguments(bjvm_thread *thread; bjvm_cp_method_handle_info *info),
    invoked_methods(
      invoked_method(bjvm_initialize_class)
      invoked_method(resolve_mh_mt)
      invoked_method(resolve_mh_vh)
      invoked_method(resolve_mh_invoke)
    )
);

typedef struct bjvm_interpret_s bjvm_interpret_t;

DECLARE_ASYNC_VOID(bjvm_invokevirtual_signature_polymorphic,
                  locals(
                    bjvm_interpret_t *interpreter_ctx;
                    bjvm_cp_method *method;
                    uint8_t argc;
                    bjvm_stack_frame *frame;
                  ),
                  arguments(
                    bjvm_thread *thread;
                    bjvm_stack_value *sp_;
                    bjvm_cp_method *method;
                    struct bjvm_native_MethodType *provider_mt;
                    bjvm_obj_header *target;
                  ),
                  invoked_methods(
                    invoked_method(call_interpreter)
                  )
);

DECLARE_ASYNC(bjvm_stack_value, bjvm_resolve_indy_static_argument,
              locals(),
              arguments(
                bjvm_thread *thread;
                bjvm_cp_entry *ent;
              ),
              invoked_methods(invoked_method(bjvm_resolve_method_handle))
);

DECLARE_ASYNC(
    int, indy_resolve,
    locals(
      bjvm_handle *bootstrap_handle;
      bjvm_handle *invoke_array;
      int static_i;
    ),
    arguments(bjvm_thread *thread; bjvm_bytecode_insn *insn; bjvm_cp_indy_info *indy),
    invoked_methods(
      invoked_method(bjvm_resolve_method_handle)
      invoked_method(bjvm_resolve_indy_static_argument)
      invoked_method(bjvm_invokevirtual_signature_polymorphic)
      invoked_method(call_interpreter)
    )
);

DECLARE_ASYNC(int, resolve_methodref,
              locals(
                bjvm_cp_class_info *klass;
                bjvm_initialize_class_t initializer_ctx;
              ),
              arguments(bjvm_thread *thread; bjvm_cp_method_info *info),
              invoked_method(bjvm_initialize_class)
);

typedef struct {
  void *ptr;
  size_t len;
} mmap_allocation;

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

  // Defined moules  (name -> bjvm_module *)
  bjvm_string_hash_table modules;

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

  int next_thread_id;

  // Vector of allocations done via Unsafe.allocateMemory0, to be freed in case
  // the finalizers aren't run
  void **unsafe_allocations;

  // Vector of allocations done via mmap, to be unmapped
  mmap_allocation *mmap_allocations;
} bjvm_vm;

// Java Module
typedef struct bjvm_module {
  // Pointer to the java.lang.Module object corresponding to this Module
  bjvm_obj_header *reflection_object;

  // TODO add exports, imports, yada yada
} bjvm_module;

int bjvm_define_module(bjvm_vm *vm, slice module_name, bjvm_obj_header *module);

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
  slice runtime_classpath;
  // Colon-separated custom classpath.
  slice classpath;
} bjvm_vm_options;

// Stack frame associated with a Java method.
//
// Frames are aligned to 8 bytes, the natural alignment of a stack value.
// in native frames, locals are of type bjvm_value
// in plain frames,  locals are of type bjvm_stack_value
// Layout:
//                                       -> stack grows this way
// ┌────────────────┬───────────────────┬───────────────────────────────────┐
// │    array of    │      metadata     │          space until
// │     locals     │     num_locals    │           max_stack
// └────────────────┴───────────────────┴───────────────────────────────────┘
//    locals array  | bjvm_stack_frame  |    more stack space
//
// The stack depth should be inferred from the program counter: In particular,
// the method contains an analysis of the stack depth at each instruction.
typedef struct bjvm_plain_frame {
  uint16_t values_count;
  uint16_t program_counter; // in instruction indices
  uint16_t max_stack;

  bjvm_stack_value stack[];
} bjvm_plain_frame;

// Stack frame associated with a native method. Note that this is stored
// separately from the (inaccessible) WebAssembly stack, and merely contains
// data necessary for correct stack trace recovery and resumption after
// interrupts.
typedef struct bjvm_native_frame {
  uint16_t values_count; // number of args passed into the native method

  // Used by async native methods for their state machines
  int state;

  // Descriptor on the instruction itself. Unequal to method->descriptor only
  // in the situation of signature-polymorphic methods.
  const bjvm_method_descriptor *method_shape;
} bjvm_native_frame;

// A frame is either a native frame or a plain frame. They may be distinguished
// with is_native.
//
// Native frames may be consecutive: for example, a native method might invoke
// another native method, which itself raises an interrupt.
typedef struct bjvm_stack_frame {
  uint8_t is_native;
  uint8_t is_async_suspended;
  uint16_t num_locals;
  bjvm_interpret_t async_frame;

  // The method associated with this frame
  bjvm_cp_method *method;

  union {
    bjvm_plain_frame plain;
    bjvm_native_frame native;
  };
} bjvm_stack_frame;

// Get the current stack depth of the interpreted frame, based on the program
// counter.
uint16_t stack_depth(const bjvm_stack_frame *frame);

static inline bool bjvm_is_frame_native(const bjvm_stack_frame *frame) { return frame->is_native != 0; }
bjvm_value *bjvm_get_native_args(const bjvm_stack_frame *frame); // same as locals, just called args for native

bjvm_stack_value *frame_stack(bjvm_stack_frame *frame);
bjvm_stack_value bjvm_interpret_2(future_t *fut, bjvm_thread *thread, bjvm_stack_frame *frame);

bjvm_native_frame *bjvm_get_native_frame_data(bjvm_stack_frame *frame);
bjvm_plain_frame *bjvm_get_plain_frame(bjvm_stack_frame *frame);
bjvm_cp_method *bjvm_get_frame_method(bjvm_stack_frame *frame);

EMSCRIPTEN_KEEPALIVE
bjvm_obj_header *bjvm_deref_js_handle(bjvm_vm *vm, int index);
EMSCRIPTEN_KEEPALIVE
int bjvm_make_js_handle(bjvm_vm *vm, bjvm_obj_header *obj);
EMSCRIPTEN_KEEPALIVE
void bjvm_drop_js_handle(bjvm_vm *vm, int index);

struct async_stack;
typedef struct async_stack async_stack_t;

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

  /// Secondary stack for async calls from the interpreter
  async_stack_t *async_stack;

  // Thread-local allocation buffer (objects are first created here)
} bjvm_thread;

bjvm_handle *bjvm_make_handle(bjvm_thread *thread, bjvm_obj_header *obj);

void bjvm_drop_handle(bjvm_thread *thread, bjvm_handle *handle);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args, uint8_t argc);

bjvm_stack_frame *bjvm_push_plain_frame(bjvm_thread *thread, bjvm_cp_method *method, bjvm_stack_value *args,
                                        uint8_t argc);
bjvm_stack_frame *bjvm_push_native_frame(bjvm_thread *thread, bjvm_cp_method *method,
                                         const bjvm_method_descriptor *descriptor, bjvm_stack_value *args,
                                         uint8_t argc);
struct bjvm_native_MethodType *bjvm_resolve_method_type(bjvm_thread *thread, bjvm_method_descriptor *method);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void bjvm_pop_frame(bjvm_thread *thr, [[maybe_unused]] const bjvm_stack_frame *reference);

bjvm_vm_options bjvm_default_vm_options();

bjvm_vm *bjvm_create_vm(bjvm_vm_options options);

bjvm_vm_options *bjvm_default_vm_options_ptr();

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

bjvm_classdesc *bootstrap_lookup_class(bjvm_thread *thread, slice name);
bjvm_classdesc *bootstrap_lookup_class_impl(bjvm_thread *thread, slice name, bool raise_class_not_found);
bjvm_classdesc *bjvm_define_bootstrap_class(bjvm_thread *thread, slice chars, const uint8_t *classfile_bytes,
                                            size_t classfile_len);
bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *classdesc, const slice name, const slice descriptor,
                                   bool superclasses, bool superinterfaces);

void bjvm_register_native(bjvm_vm *vm, const slice class_name, const slice method_name,
                          const slice method_descriptor, bjvm_native_callback callback);

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc);
bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror);

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_method(bjvm_obj_header *mirror);

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field, bjvm_stack_value bjvm_stack_value);
int bjvm_resolve_field(bjvm_thread *thread, bjvm_cp_field_info *info);
bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field);
bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc, slice const name, slice const descriptor);
bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc, const slice name, const slice descriptor);
int bjvm_multianewarray(bjvm_thread *thread, bjvm_plain_frame *frame, struct bjvm_multianewarray_data *multianewarray,
                        uint16_t *sd);
void dump_frame(FILE *stream, const bjvm_stack_frame *frame);

// e.g. int.class
struct bjvm_native_Class *bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind);

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

#include "natives_gen.h"

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc);
struct bjvm_native_ConstantPool *bjvm_get_constant_pool_mirror(bjvm_thread *thread, bjvm_classdesc *classdesc);

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread, slice name);
int bjvm_get_line_number(const bjvm_attribute_code *method, uint16_t pc);
void store_stack_value(void *field_location, bjvm_stack_value value, bjvm_type_kind kind);
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

static inline bjvm_stack_value *frame_locals(const bjvm_stack_frame *frame) {
  assert(!bjvm_is_frame_native(frame));
  return ((bjvm_stack_value *)frame) - frame->num_locals;
}

bjvm_classdesc *bjvm_primitive_classdesc(bjvm_thread *thread, bjvm_type_kind prim_kind);
void bjvm_out_of_memory(bjvm_thread *thread);
void *bump_allocate(bjvm_thread *thread, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif // BJVM_H
