//
// Created by Cowpox on 12/10/24.
//

#ifndef BJVM_H
#define BJVM_H

#include <async.h>
#include <types.h>
#include <wchar.h>

#include "classfile.h"
#include "classpath.h"

#ifdef EMSCRIPTEN
#include <emscripten.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct vm_thread vm_thread;
typedef struct obj_header obj_header;

#ifndef EMSCRIPTEN_KEEPALIVE
#define EMSCRIPTEN_KEEPALIVE // used to allow access from JS
#endif

/**
 * These typedefs are to be used for any values that a Java program might see.  These mimick JNI types.
 */
typedef s32 jint;
typedef s64 jlong;
typedef float jfloat;
typedef double jdouble;
typedef bool jboolean;
typedef s8 jbyte;
typedef s16 jshort;
typedef u16 jchar;
typedef obj_header *object;

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
} stack_value;

// A thread-local handle to an underlying object. Used in case the object is
// relocated.
//
// Implementation-wise, each thread contains an array of pointers to objects
// fixed in memory. This handle points into that array. During GC compaction/
// relocation, the array is updated to point to the new locations of the
// objects.
typedef struct {
  obj_header *obj;
#ifndef NDEBUG
  int line;
  const char *filename;
#endif
} handle;

// Value set as viewed by native functions (rather than by the VM itself,
// which deals in stack_value).
typedef union {
  s64 l;
  s32 i;
  float f;
  double d;
  handle *handle;
} value;

// Generally, use this to indicate that a native function is returning void or
// null
static inline stack_value value_null() { return (stack_value){.obj = nullptr}; }

__attribute__((noinline)) bool instanceof(const classdesc *o, const classdesc *target);

typedef struct {
  vm_thread *thread;
  handle *obj;
  value *args;
  u8 argc;
} async_natives_args_inner;

typedef struct {
  async_natives_args_inner args;
  u32 stage;
  stack_value result;
} async_natives_args;

typedef stack_value (*sync_native_callback)(vm_thread *vm, handle *obj, value *args, u8 argc);
typedef future_t (*async_native_callback)(void *args);

typedef struct {
  // Number of bytes needed for the context struct allocation (0 if sync)
  size_t async_ctx_bytes;
  // either sync_native_callback or async_native_callback
  union {
    sync_native_callback sync;
    async_native_callback async;
  };
} native_callback;

// represents a native method somewhere in this binary
typedef struct {
  slice class_path;
  slice method_name;
  slice method_descriptor;
  native_callback callback;
} native_t;

//      struct { flags, hash? }
typedef struct {
  u32 data[2];
} mark_word_t;

typedef struct {
  s32 tid;
  volatile u32 hold_count; // only changed by owner thread; therefore volatile is safe here
  mark_word_t mark_word;
} monitor_data;

static_assert(offsetof(monitor_data, mark_word) % 8 == 0);

typedef union {
  // least significant flag bit set if it's a mark_word, using the fact that expanded_data will be aligned
  mark_word_t mark_word;
  monitor_data *expanded_data;
} header_word;

// Appears at the top of every object -- corresponds to HotSpot's oopDesc
typedef struct obj_header {
  header_word header_word; // accessed atomically, except during a full GC pause
  classdesc *descriptor;
} obj_header;

typedef enum : u32 {
  IS_MARK_WORD = 1 << 0,
  IS_REACHABLE = 1 << 1,
} mark_word_flags;

bool has_expanded_data(header_word *data);
mark_word_t *get_mark_word(header_word *data);
// nullptr if the object has never been locked, otherwise a pointer to a lock_record.
monitor_data *inspect_monitor(header_word *data);
// only call this if inspect_monitor returns nullptr
// doesn't store this allocated data onto the object, because that should later be done atomically by someone else
monitor_data *allocate_monitor(vm_thread *thread); // doesn't initialize any monitor data

void read_string(vm_thread *thread, obj_header *obj, s8 **buf,
                 size_t *len); // todo: get rid of
int read_string_to_utf8(vm_thread *thread, heap_string *result, obj_header *obj);
typedef struct vm vm;

typedef void (*write_bytes)(char *buf, int len, void *param); // writes all the data in the buffer's length
typedef int (*read_bytes)(char *buf, int len,
                          void *param); // reads up to len bytes into the buffer, returns number of bytes actually read
typedef int (*poll_available_bytes)(void *param); // returns the number of bytes available to read

#define CARD_BYTES 4096

typedef struct plain_frame plain_frame;
typedef struct stack_frame stack_frame;
typedef struct native_frame native_frame;

// Continue execution of a thread.
//
// When popping frames off the stack, if the passed frame "final_frame" is
// popped off, the result of that frame (if any) is placed in "result", and
// either INTERP_RESULT_OK or INTERP_RESULT_EXC is returned, depending on
// whether the frame completed abruptly.
DECLARE_ASYNC(
    stack_value, interpret,
    locals(
      u16 sd;
      u16 async_ctx; // offset within the secondary stack
    ),
    arguments(
      vm_thread *thread;
      stack_frame *raw_frame;
    ),
);

typedef struct {
  vm_thread *thread;
  stack_frame *frame;
  stack_value *result;
  interpret_t interpreter_state;
} async_run_ctx;

// runs interpreter (async)
DECLARE_ASYNC(stack_value, call_interpreter,
  locals(async_run_ctx ctx),
  arguments(
    vm_thread *thread;
    cp_method *method;
    stack_value *args;
  ),
  invoked_methods(invoked_method(interpret))
);

stack_value call_interpreter_synchronous(vm_thread *thread, cp_method *method, stack_value *args);

DECLARE_ASYNC(stack_value, run_native,
  locals(async_natives_args *native_struct),
  arguments(vm_thread *thread; stack_frame *frame),
  invoked_methods()
);

DECLARE_ASYNC(
    int, initialize_class,
    locals(initialize_class_t *recursive_call_space; void *wakeup_info; u16 i),
    arguments(vm_thread *thread; classdesc *classdesc),
    invoked_methods(
        invoked_method(call_interpreter)
      )
);

DECLARE_ASYNC(struct native_MethodType *, resolve_mh_mt, locals(handle *ptypes_array),
              arguments(vm_thread *thread; cp_method_handle_info *info),
              invoked_methods(
                invoked_method(initialize_class)
                invoked_method(call_interpreter)
                ));

DECLARE_ASYNC(obj_header*, resolve_mh_vh,
  locals(),
  arguments(vm_thread *thread; cp_method_handle_info *info),
  invoked_methods(
    invoked_method(initialize_class)
    invoked_method(call_interpreter)
  )
);

DECLARE_ASYNC(obj_header*, resolve_mh_invoke,
  locals(handle *member; cp_method *m),
  arguments(vm_thread *thread; cp_method_handle_info *info),
  invoked_methods(
    invoked_method(initialize_class)
    invoked_method(call_interpreter)
  )
);

DECLARE_ASYNC(
    struct native_MethodHandle *, resolve_method_handle,
    locals(classdesc * DirectMethodHandle; classdesc * MemberName; cp_method * m; cp_field *f),
    arguments(vm_thread *thread; cp_method_handle_info *info),
    invoked_methods(
      invoked_method(initialize_class)
      invoked_method(resolve_mh_mt)
      invoked_method(resolve_mh_vh)
      invoked_method(resolve_mh_invoke)
    )
);

typedef struct interpret_s interpret_t;

DECLARE_ASYNC_VOID(invokevirtual_signature_polymorphic,
                  locals(
                    interpret_t *interpreter_ctx;
                    cp_method *method;
                    u8 argc;
                    stack_frame *frame;
                  ),
                  arguments(
                    vm_thread *thread;
                    stack_value *sp_;
                    cp_method *method;
                    struct native_MethodType *provider_mt;
                    obj_header *target;
                  ),
                  invoked_methods(
                    invoked_method(call_interpreter)
                  )
);

DECLARE_ASYNC(stack_value, resolve_indy_static_argument,
              locals(),
              arguments(
                vm_thread *thread;
                cp_entry *ent;
              ),
              invoked_methods(invoked_method(resolve_method_handle))
);

DECLARE_ASYNC(
    int, indy_resolve,
    locals(
      handle *bootstrap_handle;
      handle *invoke_array;
      int static_i;
    ),
    arguments(vm_thread *thread; bytecode_insn *insn; cp_indy_info *indy),
    invoked_methods(
      invoked_method(resolve_method_handle)
      invoked_method(resolve_indy_static_argument)
      invoked_method(invokevirtual_signature_polymorphic)
      invoked_method(call_interpreter)
    )
);

DECLARE_ASYNC(int, resolve_methodref,
              locals(
                cp_class_info *klass;
                initialize_class_t initializer_ctx;
              ),
              arguments(vm_thread *thread; cp_method_info *info),
              invoked_method(initialize_class)
);

typedef struct {
  void *ptr;
  size_t len;
} mmap_allocation;

struct cached_classdescs;
typedef struct vm {
  // Map class name (e.g. "java/lang/String") to classdesc*
  string_hash_table classes;
  // Classes currently under creation -- used to detect circularity
  string_hash_table inchoate_classes;

  // Native methods in javah form
  string_hash_table natives;

  // Bootstrap class loader will look for classes here
  classpath classpath;

  // Main thread group
  obj_header *main_thread_group;

  // Interned strings (string -> instance of java/lang/String)
  string_hash_table interned_strings;

  // Classes with implementation-required padding before other fields (map class
  // name -> padding bytes)
  string_hash_table class_padding;

  // Defined moules  (name -> module *)
  string_hash_table modules;

  // Overrides for stdio (if nullptr, uses the default implementation)
  read_bytes read_stdin;
  poll_available_bytes poll_available_stdin;
  write_bytes write_stdout;
  write_bytes write_stderr;

  // Passed to write_stdout/write_stderr/read_stdin
  void *stdio_override_param;

  // Primitive classes (int.class, etc.)
  classdesc *primitive_classes[9];

  vm_thread **active_threads;

  // The first thread created, which will always be kept around so there as at least one thread
  // available (even if it isn't running anything)
  vm_thread *primordial_thread;

  u8 *heap;
  // Next object should be allocated here. Should always be 8-byte aligned
  // which is the alignment of BJVM objects.
  size_t heap_used;
  size_t heap_capacity;

  // This capacity is used solely when handling an OutOfMemoryError, because
  // fillInStackTrace allocates stuff. The reserved space is a constant in
  // bjvm.c.
  size_t true_heap_capacity;

  // Handles referenced from JS
  obj_header **js_handles;

  /// Struct containing cached classdescs
  void *_cached_classdescs;  // struct cached_classdescs* -- type erased to discourage unsafe accesses

  s64 next_thread_id; // MUST BE 64 BITS

  // Vector of allocations done via Unsafe.allocateMemory0, to be freed in case
  // the finalizers aren't run
  void **unsafe_allocations;

  // Vector of allocations done via mmap, to be unmapped
  mmap_allocation *mmap_allocations;

  // Vector of z_streams, to be freed
  void **z_streams;  // z_stream **

  // Latest TID
  s32 next_tid;

  bool vm_initialized;
  void *scheduler; // rr_scheduler or null
  void *debugger;  // standard_debugger or null
} vm;

struct cached_classdescs *cached_classes(vm *vm);

// Java Module
typedef struct module {
  // Pointer to the java.lang.Module object corresponding to this Module
  obj_header *reflection_object;

  // TODO add exports, imports, yada yada
} module;

int define_module(vm *vm, slice module_name, obj_header *module);

typedef struct {
  // Overrides for stdio (if nullptr, uses the default implementation)
  read_bytes read_stdin;
  poll_available_bytes poll_available_stdin;
  write_bytes write_stdout;
  write_bytes write_stderr;

  // Passed to write_stdout/write_stderr/read_stdin
  void *stdio_override_param;

  // Heap size (static for now)
  size_t heap_size;
  // Classpath for built-in files, e.g. rt.jar. Must have definitions for
  // Object.class, etc.
  slice runtime_classpath;
  // Colon-separated custom classpath.
  slice classpath;
} vm_options;

// Stack frame associated with a Java method.
//
// Frames are aligned to 8 bytes, the natural alignment of a stack value.
// in native frames, locals are of type value
// in plain frames,  locals are of type stack_value
// Layout:
//                                       -> stack grows this way
// ┌────────────────┬───────────────────┬───────────────────────────────────┐
// │    array of    │      metadata     │          space until
// │     locals     │     num_locals    │           max_stack
// └────────────────┴───────────────────┴───────────────────────────────────┘
//    locals array  | stack_frame  |    more stack space
//
// The stack depth should be inferred from the program counter: In particular,
// the method contains an analysis of the stack depth at each instruction.
typedef struct plain_frame {
  u16 values_count;
  u16 program_counter; // in instruction indices
  u16 max_stack;

  stack_value stack[];
} plain_frame;

// Stack frame associated with a native method. Note that this is stored
// separately from the (inaccessible) WebAssembly stack, and merely contains
// data necessary for correct stack trace recovery and resumption after
// interrupts.
typedef struct native_frame {
  u16 values_count; // number of args passed into the native method

  // Used by async native methods for their state machines
  int state;

  // Descriptor on the instruction itself. Unequal to method->descriptor only
  // in the situation of signature-polymorphic methods.
  const method_descriptor *method_shape;
} native_frame;

typedef struct {
  int pc;
  object oops[];
} compiled_frame;

typedef enum : u8 {
  FRAME_KIND_INTERPRETER,
  FRAME_KIND_NATIVE,
  FRAME_KIND_COMPILED
} frame_kind;

// A frame is either a native frame or a plain frame. They may be distinguished
// with is_native.
//
// Native frames may be consecutive: for example, a native method might invoke
// another native method, which itself raises an interrupt.
typedef struct stack_frame {
  frame_kind is_native;
  u8 is_async_suspended;
  // todo: enum 0- non 1- synchronize in progress, 2- synchronized, and done
  u8 attempted_synchronize; // whether this frame method has been synchronized
  u16 num_locals;

  // The method associated with this frame
  cp_method *method;

  union {
    plain_frame plain;
    native_frame native;
    compiled_frame compiled;
  };
} stack_frame;

// Get the current stack depth of the interpreted frame, based on the program
// counter.
u16 stack_depth(const stack_frame *frame);

static inline bool is_frame_native(const stack_frame *frame) { return frame->is_native == FRAME_KIND_NATIVE; }
static inline bool is_interpreter_frame(const stack_frame *frame) { return frame->is_native == FRAME_KIND_INTERPRETER; }
value *get_native_args(const stack_frame *frame); // same as locals, just called args for native

stack_value *frame_stack(stack_frame *frame);
stack_value interpret_2(future_t *fut, vm_thread *thread, stack_frame *frame);

native_frame *get_native_frame_data(stack_frame *frame);
plain_frame *get_plain_frame(stack_frame *frame);
cp_method *get_frame_method(stack_frame *frame);

EMSCRIPTEN_KEEPALIVE
obj_header *deref_js_handle(vm *vm, int index);
EMSCRIPTEN_KEEPALIVE
int make_js_handle(vm *vm, obj_header *obj);
EMSCRIPTEN_KEEPALIVE
void drop_js_handle(vm *vm, int index);

struct async_stack;
typedef struct async_stack async_stack_t;

#define MONITOR_ACQUIRE_CONTINUATION_SIZE 56
typedef struct vm_thread {
  // Global VM corresponding to this thread
  vm *vm;

  struct {
    // a contiguous buffer which stores stack_frames and intermediate data
    char *frame_buffer;
    u32 frame_buffer_capacity;
    // index of one past the end of the last stack frame
    u32 frame_buffer_used;

    // Pointers into the frame_buffer
    stack_frame **frames;

    /// Secondary stack for async calls from the interpreter
    async_stack_t *async_call_stack;

    // Current number of active synchronous calls
    u32 synchronous_depth;

    // If true, the call stack must fully unwind because we are e.g. waiting for
    // a JS Promise. This should only be cleared by the top-level scheduler.
    // This function is used in thread_run, which attempts to run code in
    // a synchronous manner.
    bool must_unwind;

    char synchronize_acquire_continuation[MONITOR_ACQUIRE_CONTINUATION_SIZE];
  } stack;

  // Pre-allocated out-of-memory and stack overflow errors
  obj_header *out_of_mem_error;
  obj_header *stack_overflow_error;

  // Currently propagating exception
  obj_header *current_exception;

  bool js_jit_enabled;

  // Instance of java.lang.Thread
  struct native_Thread *thread_obj;

  // Array of handles, see handle (null entries are free for use)
  // TODO make this a linked list to accommodate arbitrary # of handles
  handle *handles;
  int handles_capacity;

  // Handle for null
  handle null_handle;

  int allocations_so_far;
  // This value is used to periodically check whether we should yield back to the scheduler ...
  u32 fuel;
  // ... if the current time is past this value
  u64 yield_at_time;

  s32 tid;

  // Allocation for the refuel_check wakeup info
  void *refuel_wakeup_info;

  // Thread-local allocation buffer (objects are first created here)

  // Whether this thread is currently being debugged, AND the debugger should be consulted after the execution of
  // every bytecode instruction.
  bool is_single_stepping;
  // Whether this thread is currently paused in the debugger
  bool paused_in_debugger;
} vm_thread;

handle *make_handle_impl(vm_thread *thread, obj_header *obj, const char* file, int line_no);
// Create a handle to the given object. Should always be paired with drop_handle.
#define make_handle(thread, obj) make_handle_impl(thread, obj, __FILE__, __LINE__)

void drop_handle(vm_thread *thread, handle *handle);
bool is_builtin_class(slice chars);

/**
 * Create an uninitialized frame with space sufficient for the given method.
 * Raises a StackOverflowError if the frames are exhausted.
 */
__attribute__((noinline)) stack_frame *push_frame(vm_thread *thread, cp_method *method, stack_value *args, u8 argc);

__attribute__((noinline)) stack_frame *push_plain_frame(vm_thread *thread, cp_method *method, stack_value *args,
                                                        u8 argc);
__attribute__((noinline)) stack_frame *push_native_frame(vm_thread *thread, cp_method *method,
                                                         const method_descriptor *descriptor, stack_value *args,
                                                         u8 argc);
struct native_MethodType *resolve_method_type(vm_thread *thread, method_descriptor *method);

/**
 * Pop the topmost frame from the stack, optionally passing a pointer as a debug
 * check.
 */
void pop_frame(vm_thread *thr, [[maybe_unused]] const stack_frame *reference);

vm_options default_vm_options();

vm *create_vm(vm_options options);

vm_options *default_vm_options_ptr();

typedef struct {
  u32 stack_space;
  // Whether to enable JavaScript JIT compilation
  bool js_jit_enabled;
  // What thread group to construct the thread in (nullptr = default thread
  // group)
  obj_header *thread_group;
} thread_options;

thread_options default_thread_options();
vm_thread *create_main_thread(vm *vm, thread_options options);
vm_thread *create_vm_thread(vm *vm, vm_thread *creator_thread, struct native_Thread *thread_obj, thread_options options); // wraps a Thread obj
void free_thread(vm_thread *thread);

/**
 * Free the classfile.
 */
void free_classfile(classdesc cf);

void free_vm(vm *vm);

classdesc *bootstrap_lookup_class(vm_thread *thread, slice name);
classdesc *bootstrap_lookup_class_impl(vm_thread *thread, slice name, bool raise_class_not_found);
classdesc *define_bootstrap_class(vm_thread *thread, slice chars, const u8 *classfile_bytes, size_t classfile_len);
cp_method *method_lookup(classdesc *classdesc, const slice name, const slice descriptor, bool superclasses,
                         bool superinterfaces);

void register_native(vm *vm, const slice class_name, const slice method_name, const slice method_descriptor,
                     native_callback callback);

obj_header *new_object(vm_thread *thread, classdesc *classdesc);
classdesc *unmirror_class(obj_header *mirror);

cp_field **unmirror_field(obj_header *mirror);

cp_method **unmirror_method(obj_header *mirror);

cp_method **unmirror_ctor(obj_header *mirror);

void set_field(obj_header *obj, cp_field *field, stack_value stack_value);
int resolve_field(vm_thread *thread, cp_field_info *info);
stack_value get_field(obj_header *obj, cp_field *field);
// Look up a (possibly inherited) field on the class.
cp_field *field_lookup(classdesc *classdesc, slice name, slice descriptor);
int multianewarray(vm_thread *thread, plain_frame *frame, struct multianewarray_data *multianewarray, u16 *sd);
void dump_frame(FILE *stream, const stack_frame *frame);

// e.g. int.class
struct native_Class *primitive_class_mirror(vm_thread *thread, type_kind prim_kind);

int resolve_class(vm_thread *thread, cp_class_info *info);

#include "natives_gen.h"

struct native_Class *get_class_mirror(vm_thread *thread, classdesc *classdesc);
struct native_ConstantPool *get_constant_pool_mirror(vm_thread *thread, classdesc *classdesc);

classdesc *load_class_of_field_descriptor(vm_thread *thread, slice name);
int get_line_number(const attribute_code *method, u16 pc);
void store_stack_value(void *field_location, stack_value value, type_kind kind);
stack_value load_stack_value(void *field_location, type_kind kind);

static inline int sizeof_type_kind(type_kind kind) {
  switch (kind) {
  case TYPE_KIND_BYTE:
  case TYPE_KIND_BOOLEAN:
    return 1;
  case TYPE_KIND_CHAR:
  case TYPE_KIND_SHORT:
    return 2;
  case TYPE_KIND_FLOAT:
  case TYPE_KIND_INT:
    return 4;
  case TYPE_KIND_REFERENCE:
    return sizeof(void *);
  case TYPE_KIND_DOUBLE:
  case TYPE_KIND_LONG:
    return 8;
  default:
    UNREACHABLE();
  }
}

static inline stack_value *frame_locals(const stack_frame *frame) {
  DCHECK(!is_frame_native(frame));
  return ((stack_value *)frame) - frame->num_locals;
}

classdesc *primitive_classdesc(vm_thread *thread, type_kind prim_kind);
void out_of_memory(vm_thread *thread);
void *bump_allocate(vm_thread *thread, size_t bytes);

#ifdef __cplusplus
}
#endif

#endif // BJVM_H
