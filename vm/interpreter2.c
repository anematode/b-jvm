// Interpreter v2, a significant improvement over v1.
//
// There are implementations for TOS types of int/long/reference, float, and double. Some instruction/TOS
// combinations are impossible (rejected by the verifier) and are thus omitted. The execution of "stack polymorphic"
// instructions like pop will consult the TOS stack type that the instruction is annotated with (during analysis),
// before loading the value from the stack and using the appropriate jump table.
//
// The general signature is (thread, frame, insns, fuel, sp, tos). At appropriate points (whenever the frame might be
// read back, e.g. for GC purposes, or when interrupting), the TOS value and program counter are written to the stack,
// and the fuel counter is written back to the thread struct.
//
// In tail call mode, JMP_VOID etc. macros select the correct bytecode to jump to. In asm this will compile down
// to a single indirect branch. Otherwise, they return the appropriate jump table value (kind * 4 + tos_type) for
// the switch-loop to jump to. (See [1] below.)
//
// RETURNING CONTROL TO THE INTERPRETER LOOP
//
// There are five cases in which we might return to the main interpreter loop:
//
// 1. async suspension of an instruction like monitorenter, resolution of an instruction, etc.;
// 2. refuel checks, which are treated like optional async suspensions;
// 3. invoking a method;
// 4. throwing an exception;
// 5. and returning a result from a function.
//
// Each one is detected and handled in a slightly different way, mostly for efficiency purposes. Check out the
// interpreter flowchart later in the file for more info.
//
// MORE ABOUT THE NO-TAILS CASE [1]
//
// In non-tail-call mode, used when targeting the browser, the generator loosely controls inlining by assuming
// that inlining will occur, then preventing the inlining of certain functions through a funcref
// (__interpreter_intrinsic_force_outline). This is done because the WASM runtime often aggressively inlines functions,
// shooting itself in the foot by spilling the most important values (e.g. "insns", "sp") out of registers. The live
// ranges of these variables are simply too large for JIT register allocation algorithms to work well.
//
// Failure to inline can be verified by checking that the C stack usage of entry_notco_impl is equal to 32, which
// is the size of the struct which is used in the force-outlined case. If it's not, then at least one function didn't
// get inlined.
//
// The handler return value is the index of the handler of the next bytecode instruction, encoded as
// 4 * instruction kind + tos type (which is fast to compute and lets all handlers be packed into one giant br_table
// instruction). return and throw instructions are special-cased to return the appropriate value. Also, a return value
// of 0, RETVAL_ASYNC_SUSPEND, RETVAL_EXCEPTION, or RETVAL_FUEL_CHECK is used to indicate that control should be
// passed back to the main interpreter loop. (0 is unused in practice because it corresponds to a nop instruction, which
// javac never emits -- we may fix this at some point.)
//
// Codegen on x86 is probably suboptimal for no-tails, because of the fewer registers. More effort is needed to
// determine which bytecodes can be profitably inlined on this architecture.
//
// JS POSTPROCESSING
//
// The force outline intrinsic converts the calls to force-outlined bytecodes into calls to a typed funcref table,
// which avoids various safety checks. The postprocessor may be found in codegen/wasm_opt.js and is compiled from
// https://github.com/anematode/binaryen.

#include "arrays.h"
#include "bjvm.h"
#include "classfile.h"
#include "dumb_jit.h"
#include "util.h"
#include "wasm_trampolines.h"

#include <analysis.h>
#include <debugger.h>
#include <math.h>
#include <tgmath.h>

#include <exceptions.h>
#include <linkage.h>
#include <monitors.h>

#include <instrumentation.h>
#include <objects.h>
#include <roundrobin_scheduler.h>
#include <sys/time.h>

[[maybe_unused]] s64 tick = 0; // for debugging

// Define this macro to print debug dumps upon the execution of every interpreter instruction. Useful for debugging.
#define DEBUG_CHECK() ;
#if 0
#undef DEBUG_CHECK
#define DEBUG_CHECK()                                                                                                  \
  if (1) {                                                                                                             \
    SPILL_VOID                                                                                                         \
    printf("Frame method: %p\n", frame->method);                                                                       \
    cp_method *m = frame->method;                                                                                      \
    printf("Calling method %.*s, descriptor %.*s, on class %.*s; sp = %ld; %d, %lld\n", fmt_slice(m->name),            \
           fmt_slice(m->unparsed_descriptor), fmt_slice(m->my_class->name), sp - frame->stack, __LINE__, tick);        \
    heap_string s = insn_to_string(insn, pc);                                                                          \
    printf("Insn kind: %.*s\n", fmt_slice(s));                                                                         \
    free_heap_str(s);                                                                                                  \
    dump_frame(stdout, frame);                                                                                         \
  }
#endif

// If DO_TAILS is true, use a sequence of tail calls rather than computed goto. We try to make the code reasonably
// generic to handle both cases efficiently. The macros necessary for the code to be semi-readable are defined here.
#ifdef EMSCRIPTEN
#define DO_TAILS 0 // not profitable on web
#else
#define DO_TAILS 1
#endif

#define MUSTTAIL // defined as empty here to allow compilation without -mtail-call

#if DO_TAILS
#define sp sp_
#define pc (insns - frame->code)
#define FUEL fuel_
#define tos tos_
#define insns_ insns

// Arguments common to all TOS kinds.
#define ARGS_BASE                                                                                                      \
  [[maybe_unused]] vm_thread *thread, [[maybe_unused]] stack_frame *frame, [[maybe_unused]] bytecode_insn *insns,      \
      [[maybe_unused]] s32 fuel_, [[maybe_unused]] stack_value *sp_

// Arguments wherein the special TOS is called "tos", and the other arguments are named arg_1, arg_2, arg_3, where
// arg_1 is the integer argument, arg_2 is the float argument, and arg_3 is the double argument.
#define ARGS_VOID ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_INT ARGS_BASE, [[maybe_unused]] s64 tos_, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_DOUBLE ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double tos_
#define ARGS_FLOAT ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float tos_, [[maybe_unused]] double arg_3

// Indicates that the following return must be a tail call. Supported by modern clang and GCC (but GCC support seems
// somewhat buggy..., so we use -foptimize-sibling-calls and disable ASAN in CMake and pray.)
#ifdef __clang__
#undef MUSTTAIL
#define MUSTTAIL [[clang::musttail]]
#endif

#ifndef __OPTIMIZE__
#pragma GCC optimize("O1")
#endif
#pragma GCC optimize("optimize-sibling-calls")

// TODO consider what to do here so that it's efficient but not UB
#ifdef EMSCRIPTEN
#define WITH_UNDEF(expr)                                                                                               \
  do {                                                                                                                 \
    s64 a_undef = 0;                                                                                                   \
    float b_undef = 0;                                                                                                 \
    double c_undef = 0;                                                                                                \
    MUSTTAIL return (expr);                                                                                            \
  } while (0);
#else

#ifdef __aarch64__
#define CREATE_UNDEFS asm volatile("" : "=r"(a_undef), "=w"(b_undef), "=w"(c_undef));
#else
#define CREATE_UNDEFS asm volatile("" : "=r"(a_undef), "=x"(b_undef), "=x"(c_undef));
#endif

#define WITH_UNDEF(expr)                                                                                               \
  do {                                                                                                                 \
    s64 a_undef;                                                                                                       \
    float b_undef;                                                                                                     \
    double c_undef;                                                                                                    \
    CREATE_UNDEFS                                                                                                      \
    MUSTTAIL return (expr);                                                                                            \
  } while (0);
#endif

#ifdef EMSCRIPTEN
#error "TCO mode unsupported in the browser"
#else // !ifdef EMSCRIPTEN
#define ADVANCE_INT_(tos, insn_off)                                                                                    \
  int k = insns[insn_off].kind;                                                                                        \
  s64 __tos = (s64)(tos);                                                                                              \
  WITH_UNDEF(jmp_table_int[k](thread, frame, insns + insn_off, FUEL, sp, (s64)__tos, b_undef, c_undef));
#define ADVANCE_FLOAT_(tos, insn_off)                                                                                  \
  int k = insns[insn_off].kind;                                                                                        \
  float __tos = (tos);                                                                                                 \
  WITH_UNDEF(jmp_table_float[k](thread, frame, insns + insn_off, FUEL, sp, a_undef, __tos, c_undef));
#define ADVANCE_DOUBLE_(tos, insn_off)                                                                                 \
  int k = insns[insn_off].kind;                                                                                        \
  double __tos = (tos);                                                                                                \
  WITH_UNDEF(jmp_table_double[k](thread, frame, insns + insn_off, FUEL, sp, a_undef, b_undef, __tos));

#define JMP_INT(tos) ADVANCE_INT_(tos, 0)
#define JMP_FLOAT(tos) ADVANCE_FLOAT_(tos, 0)
#define JMP_DOUBLE(tos) ADVANCE_DOUBLE_(tos, 0)

#define NEXT_INT(tos) ADVANCE_INT_(tos, 1)
#define NEXT_FLOAT(tos) ADVANCE_FLOAT_(tos, 1)
#define NEXT_DOUBLE(tos) ADVANCE_DOUBLE_(tos, 1)

// Jump to the instruction at pc, with nothing in the top of the stack. This does NOT imply that sp = 0, only that
// all stack values are in memory (rather than in a register)
#define JMP_VOID WITH_UNDEF(jmp_table_void[insns[0].kind](thread, frame, insns, FUEL, sp, a_undef, b_undef, c_undef));
// Jump to the instruction at pc + 1, with nothing in the top of the stack.
#define NEXT_VOID                                                                                                      \
  WITH_UNDEF(jmp_table_void[insns[1].kind](thread, frame, insns + 1, FUEL, sp, a_undef, b_undef, c_undef));

#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  MUSTTAIL return bytecode_tables[insns[1].tos_before][insns[1].kind](thread, frame, insns + 1, FUEL, sp, \
    (sp - 1)->l, (sp - 1)->f, (sp - 1)->d);

#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  MUSTTAIL return bytecode_tables[insns[0].tos_before][insns[0].kind](thread, frame, insns, FUEL, sp, \
    (sp - 1)->l, (sp - 1)->f, (sp - 1)->d);
#endif // ifdef EMSCRIPTEN
#else  // !DO_TAILS

#define sp (*sp_)
#define pc (insns - frame->code)
#define tos (*tos_)
#define insns (*insns_)
#define FUEL (*fuel_)

#define ARGS_BASE                                                                                                      \
  [[maybe_unused]] vm_thread *thread, [[maybe_unused]] stack_frame *frame, [[maybe_unused]] bytecode_insn **insns_,    \
      [[maybe_unused]] s32 *fuel_, [[maybe_unused]] stack_value **sp_

#define ARGS_VOID ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *arg_3
#define ARGS_INT ARGS_BASE, [[maybe_unused]] s64 *tos_, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *arg_3
#define ARGS_DOUBLE ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *tos_
#define ARGS_FLOAT ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *tos_, [[maybe_unused]] double *arg_3

static bool *tos_; // used in conjunction with _Generic by JMP_* and NEXT_* to detect what type of function we're in.

static s64 *arg_1; // not actually used, just to silence compiler errors in branches not taken at compile time
static float *arg_2;
static double *arg_3;

#define JMP_VOID return 4 * insn->kind;
#define NEXT_VOID                                                                                                      \
  insns++;                                                                                                             \
  return 4 * insn->kind;

#define SET_INT(tos) *(_Generic((*tos_), s64: true, default: false) ? (s64 *)tos_ : arg_1) = (s64)tos;
#define SET_FLOAT(tos) *(_Generic((*tos_), float: true, default: false) ? (float *)tos_ : arg_2) = tos;
#define SET_DOUBLE(tos) *(_Generic((*tos_), double: true, default: false) ? (double *)tos_ : arg_3) = tos;

#define JMP_INT(tos)                                                                                                   \
  SET_INT(tos);                                                                                                        \
  return 4 * insn->kind + TOS_INT;
#define NEXT_INT(tos)                                                                                                  \
  SET_INT(tos);                                                                                                        \
  insns++;                                                                                                             \
  return 4 * insn->kind + TOS_INT;

#define JMP_FLOAT(tos)                                                                                                 \
  SET_FLOAT(tos);                                                                                                      \
  return 4 * insn->kind + TOS_FLOAT;
#define NEXT_FLOAT(tos)                                                                                                \
  SET_FLOAT(tos);                                                                                                      \
  insns++;                                                                                                             \
  return 4 * insn->kind + TOS_FLOAT;

#define JMP_DOUBLE(tos)                                                                                                \
  SET_DOUBLE(tos);                                                                                                     \
  return 4 * insn->kind + TOS_DOUBLE;
#define NEXT_DOUBLE(tos)                                                                                               \
  SET_DOUBLE(tos);                                                                                                     \
  insns++;                                                                                                             \
  return 4 * insn->kind + TOS_DOUBLE;

#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  SET_FLOAT((tos).f);                                                                                                  \
  SET_INT((tos).l);                                                                                                    \
  SET_DOUBLE((tos).d);                                                                                                 \
  return 4 * insn->kind + insn->tos_before;
#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  SET_FLOAT((tos).f);                                                                                                  \
  SET_INT((tos).l);                                                                                                    \
  SET_DOUBLE((tos).d);                                                                                                 \
  insns++;                                                                                                             \
  return 4 * insn->kind + insn->tos_before;

#endif // DO_TAILS

#ifdef __clang__
// TODO see if this is worth it
#define UNPREDICTABLE(x) x // __builtin_unpredictable(x)
#else
#define UNPREDICTABLE(x) x
#endif

// The current instruction
#define insn (&insns[0])

typedef s64 (*bytecode_handler_t)(ARGS_VOID);

// Used when the TOS is int (i.e., the stack is empty)
static bytecode_handler_t jmp_table_void[MAX_INSN_KIND];
// Used when the TOS is int, long, or a reference (wasm signature: i64). In the int case, the result is sign-extended;
// in the reference case, the result is zero-extended.
static bytecode_handler_t jmp_table_int[MAX_INSN_KIND];
// Used when the TOS is float (wasm signature: f32)
static bytecode_handler_t jmp_table_float[MAX_INSN_KIND];
// Used when the TOS is double (wasm signature: f64)
static bytecode_handler_t jmp_table_double[MAX_INSN_KIND];

[[maybe_unused]] const static bytecode_handler_t *bytecode_tables[4] = {
    [TOS_VOID] = jmp_table_void,
    [TOS_INT] = jmp_table_int,
    [TOS_FLOAT] = jmp_table_float,
    [TOS_DOUBLE] = jmp_table_double,
};

// Call the bytecode implementation through a funcref table. This prevents inlining by the runtime and regalloc from
// dying.
extern int64_t __interpreter_intrinsic_notco_call_outlined(ARGS_VOID, int index);

// These point into .rodata and are used to recover the function pointers for the funcref jump tables. They should not
// be used by the interpreter itself and are deleted by the postprocessor.
EMSCRIPTEN_KEEPALIVE
void *__interpreter_intrinsic_void_table_base() { return jmp_table_void; }

EMSCRIPTEN_KEEPALIVE
void *__interpreter_intrinsic_int_table_base() { return jmp_table_int; }

EMSCRIPTEN_KEEPALIVE
void *__interpreter_intrinsic_float_table_base() { return jmp_table_float; }

EMSCRIPTEN_KEEPALIVE
void *__interpreter_intrinsic_double_table_base() { return jmp_table_double; }

EMSCRIPTEN_KEEPALIVE
int32_t __interpreter_intrinsic_max_insn() { return MAX_INSN_KIND; }

// Same as SPILL(tos), but when no top-of-stack value is available
#define SPILL_VOID                                                                                                     \
  frame->program_counter = pc;                                                                                         \
  thread->fuel = (u32)FUEL;

// Spill all the information currently in locals/registers to the frame (required at safepoints and when interrupting)
#define SPILL(tos)                                                                                                     \
  SPILL_VOID                                                                                                           \
  *(sp - 1) = _Generic((tos),                                                                                          \
      s64: (stack_value){.l = (s64)tos},                                                                               \
      float: (stack_value){.f = (float)tos},                                                                           \
      double: (stack_value){.d = (double)tos},                                                                         \
      obj_header *: (stack_value){.obj = (obj_header *)(uintptr_t)tos} /* shut up float branch */                      \
  );

// Reload the top of stack type -- used after an instruction which may have instigated a GC. RELOAD_VOID is not
// required.
#define RELOAD(tos)                                                                                                    \
  tos = _Generic(tos, s64: (*(sp - 1)).l, float: (*(sp - 1)).f, double: (*(sp - 1)).d, obj_header *: (*(sp - 1)).obj);

// For a bytecode that takes no arguments, given an implementation for the int TOS type, generate adapter funcsptions
// which push the current TOS value onto the stack and then call the void TOS implementation.
#define FORWARD_TO_NULLARY(which)                                                                                      \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    *(sp - 1) = (stack_value){.l = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns_, fuel_, sp_, tos_, arg_2, arg_3);                          \
  }                                                                                                                    \
                                                                                                                       \
  static s64 which##_impl_float(ARGS_FLOAT) {                                                                          \
    *(sp - 1) = (stack_value){.f = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns_, fuel_, sp_, arg_1, tos_, arg_3);                          \
  }                                                                                                                    \
                                                                                                                       \
  static s64 which##_impl_double(ARGS_DOUBLE) {                                                                        \
    *(sp - 1) = (stack_value){.d = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns_, fuel_, sp_, arg_1, arg_2, tos_);                          \
  }

// Emit a null pointer exception when the given expression is null.
#define NPE_ON_NULL(expr)                                                                                              \
  if (unlikely(!expr)) {                                                                                               \
    SPILL_VOID                                                                                                         \
    raise_null_pointer_exception(thread);                                                                              \
    return RETVAL_EXCEPTION_THROWN;                                                                                    \
  }

/** Helper functions */

static s32 java_idiv(s32 const a, s32 const b) {
  DCHECK(b != 0);
  if (a == INT_MIN && b == -1)
    return INT_MIN;
  return a / b;
}

static s64 java_irem(s32 const a, s32 const b) {
  DCHECK(b != 0);
  if (a == INT_MIN && b == -1)
    return 0;
  return a % b;
}

static s64 java_ldiv(s64 const a, s64 const b) {
  DCHECK(b != 0);
  if (a == LONG_MIN && b == -1)
    return LONG_MIN;
  return a / b;
}

static s64 java_lrem(s64 const a, s64 const b) {
  DCHECK(b != 0);
  if (a == LONG_MIN && b == -1)
    return 0;
  return a % b;
}

// Java saturates the conversion. For Emscripten we use the builtins for saturating conversions.
static int double_to_int(double const x) {
#ifdef EMSCRIPTEN
  return __builtin_wasm_trunc_saturate_s_i32_f64(x);
#else
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  if (isnan(x))
    return 0;
  return (int)x;
#endif
}

static int float_to_int(float const x) {
#ifdef EMSCRIPTEN
  return __builtin_wasm_trunc_saturate_s_i32_f32(x);
#else
  return double_to_int(x);
#endif
}

// Java saturates the conversion
static s64 double_to_long(double const x) {
#ifdef EMSCRIPTEN
  return __builtin_wasm_trunc_saturate_s_i64_f64(x);
#else
  if (x >= (double)(ULLONG_MAX / 2))
    return LLONG_MAX;
  if (x < (double)LLONG_MIN)
    return LLONG_MIN;
  if (isnan(x))
    return 0;
  return (s64)x;
#endif
}

static s64 float_to_long(float const x) {
#ifdef EMSCRIPTEN
  return __builtin_wasm_trunc_saturate_s_i64_f32(x);
#else
  return double_to_long(x);
#endif
}

// Convert getstatic and putstatic instructions into one of the resolved forms -- or throw a linkage error if
// appropriate. The stack should be made consistent before this function is called, as it may interrupt.
DECLARE_ASYNC(int, resolve_getstatic_putstatic,
  locals(cp_field_info *field_info; cp_class_info *class),
  arguments(vm_thread *thread; bytecode_insn *inst;),
  invoked_methods(invoked_method(initialize_class)));

// Convert getfield and putfield instructions into one of the resolved forms -- or throw a linkage error if
// appropriate. The stack should be made consistent before this function is called, as it may interrupt.
DECLARE_ASYNC(int, resolve_getfield_putfield,
  locals(cp_field_info *field_info; cp_class_info *class),
  arguments(vm_thread *thread; bytecode_insn *inst; stack_frame *frame; stack_value *sp_;),
  invoked_methods(invoked_method(initialize_class)));

DECLARE_ASYNC(int, resolve_invokestatic,
              locals(),
              arguments(vm_thread *thread; bytecode_insn *insn_),
              invoked_method(resolve_methodref)
);

DECLARE_ASYNC(int, resolve_new_inst,
  locals(classdesc *classdesc),
  arguments(vm_thread *thread; bytecode_insn *inst;),
  invoked_methods(invoked_method(initialize_class)));

DECLARE_ASYNC(int, resolve_insn,
              locals(),
              arguments(vm_thread *thread; bytecode_insn *inst; stack_frame *frame; stack_value *sp_;),
              invoked_methods(
                invoked_method(resolve_getstatic_putstatic)
                invoked_method(resolve_getfield_putfield)
                invoked_method(resolve_invokestatic)
                invoked_method(resolve_new_inst)
              )
);

DEFINE_ASYNC(resolve_insn) {
  u16 kind = args->inst->kind;
  if (kind == insn_getstatic || kind == insn_putstatic) {
    AWAIT(resolve_getstatic_putstatic, args->thread, args->inst);
    ASYNC_RETURN(get_async_result(resolve_getstatic_putstatic));
  }
  if (kind == insn_getfield || kind == insn_putfield) {
    AWAIT(resolve_getfield_putfield, args->thread, args->inst, args->frame, args->sp_);
    ASYNC_RETURN(get_async_result(resolve_getfield_putfield));
  }
  if (kind == insn_new) {
    AWAIT(resolve_new_inst, args->thread, args->inst);
    ASYNC_RETURN(get_async_result(resolve_new_inst));
  }
  if (kind == insn_invokestatic) {
    AWAIT(resolve_invokestatic, args->thread, args->inst);
    ASYNC_RETURN(get_async_result(resolve_invokestatic));
  }
  UNREACHABLE();
  ASYNC_END_VOID();
}

/// In the interpreter, we don't use the DECLARE_ASYNC/DEFINE_ASYNC macros;  instead, we manually
/// define the different positions an async continuation may return to.  When an async function yields,
/// the top of the async stack contains (a) the state index and (b) a pointer to a malloc'd
/// context struct.
start_counter(state_index, 1);
typedef enum { STATE_DONE, STATE_FAILED, STATE_YIELD } async_task_status;

typedef enum {
  CONT_RESOLVE,
  CONT_RUN_NATIVE,
  CONT_INVOKESIGPOLY,
  CONT_MONITOR_ENTER,
  CONT_RESUME_INSN,
  CONT_DEBUGGER_PAUSE,
  CONT_SYNCHRONIZED_METHOD
} continuation_point;

typedef struct {
  void *wakeup;
  continuation_point pnt;
  stack_frame *frame; // frame associated with the continuation

  union {
    resolve_insn_t resolve_insn;
    invokevirtual_signature_polymorphic_t sigpoly;
    run_native_t run_native;
    monitor_acquire_t acquire_monitor;
    struct {
      stack_frame *frame;
      u8 argc;
      bool returns;
    } interp_call;
  } ctx;
} continuation_frame;

struct async_stack {
  u16 max_height;
  u16 height;
  continuation_frame frames[];
};

static s32 grow_async_stack(vm_thread *thread) {
  struct async_stack *stk = thread->stack.async_call_stack;

  size_t new_capacity = stk->max_height + stk->max_height / 2;
  if (new_capacity == 0)
    new_capacity = 4;

  stk = realloc(thread->stack.async_call_stack, sizeof(struct async_stack) + new_capacity * sizeof(continuation_frame));
  if (unlikely(!stk)) {
    thread->current_exception = thread->stack_overflow_error;
    return -1;
  }
  thread->stack.async_call_stack = stk;

  stk->max_height = new_capacity;
  return 0;
}

static continuation_frame *async_stack_push(vm_thread *thread) {
#define stk thread->stack.async_call_stack

  if (unlikely(stk->height == stk->max_height))
    if (grow_async_stack(thread) != 0)
      return nullptr;

  return &thread->stack.async_call_stack->frames[thread->stack.async_call_stack->height++];

#undef stk
}

static continuation_frame *async_stack_pop(vm_thread *thread) {
  DCHECK(thread->stack.async_call_stack->height > 0);
  return &thread->stack.async_call_stack->frames[--thread->stack.async_call_stack->height];
}

static continuation_frame *async_stack_peek(vm_thread *thread) {
  assert(thread->stack.async_call_stack->height > 0);
  return &thread->stack.async_call_stack->frames[thread->stack.async_call_stack->height - 1];
}

/** FUEL CHECKING */

[[maybe_unused]]
static bool refuel_check(vm_thread *thread) {
  const int REFUEL = 200000;
  thread->fuel = REFUEL;

  if (thread->stack.synchronous_depth) // we're in a synchronous call, don't try to yield
    return false;

  // Get the current time in milliseconds since 1970
  u64 now = get_unix_us();
  if (thread->yield_at_time != 0 && now >= thread->yield_at_time) {
    thread->stack.top->is_async_suspended = true;

    continuation_frame *cont = async_stack_push(thread);
    // Provide a way for us to free the wakeup info. There will never be multiple refuel checks in flight within a
    // single thread.
    rr_wakeup_info *wakeup = (rr_wakeup_info *)&thread->refuel_wakeup_info;
    static_assert(sizeof(*wakeup) <= sizeof(thread->refuel_wakeup_info),
                  "wakeup info can not be stored within thread cache");
    wakeup->kind = RR_WAKEUP_YIELDING;
    *cont = (continuation_frame){.pnt = CONT_RESUME_INSN, .frame = thread->stack.top, .wakeup = (void *)wakeup};
    return true;
  }
  return false;
}

enum {
  RETVAL_ASYNC_SUSPEND = 4 * MAX_INSN_KIND,
  RETVAL_FUEL_CHECK = 4 * MAX_INSN_KIND + 1,
  RETVAL_EXCEPTION_THROWN = 4 * MAX_INSN_KIND + 2
};

#define FUEL_CHECK                                                                                                     \
  gc_pause_if_requested(thread->vm);                                                                     \
  if (unlikely(FUEL-- == 0)) {                                                                                         \
    SPILL(tos);                                                                                                        \
    frame->is_async_suspended = true;                                                                                  \
    return RETVAL_FUEL_CHECK;                                                                                          \
  }
#define FUEL_CHECK_VOID                                                                                                \
  gc_pause_if_requested(thread->vm);                                                                     \
  if (unlikely(FUEL-- == 0)) {                                                                                         \
    frame->is_async_suspended = true;                                                                                  \
    SPILL_VOID return RETVAL_FUEL_CHECK;                                                                               \
  }

#define DO_FUEL_CHECKS 1

#if !DO_FUEL_CHECKS
#undef FUEL_CHECK
#undef FUEL_CHECK_VOID

#define FUEL_CHECK
#define FUEL_CHECK_VOID
#endif

static void mark_insn_returns(bytecode_insn *inst) {
  inst->returns = inst->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
}

/** BYTECODE IMPLEMENTATIONS */

/**
 * getstatic/putstatic/getfield/putfield
 *
 * getstatic:
 *   ... -> ..., value
 * putstatic:
 *   ..., value -> ...
 * getfield:
 *   ..., obj -> ..., value
 * putfield:
 *   ..., obj, value -> ...
 */

// For a given field kind, return the correct instruction kind for a resolved getstatic or putstatic instruction.
insn_code_kind getstatic_putstatic_resolved_kind(bool putstatic, type_kind field_kind) {
  switch (field_kind) {
  case TYPE_KIND_BOOLEAN:
    return putstatic ? insn_putstatic_B : insn_getstatic_B;
  case TYPE_KIND_CHAR:
    return putstatic ? insn_putstatic_C : insn_getstatic_C;
  case TYPE_KIND_FLOAT:
    return putstatic ? insn_putstatic_F : insn_getstatic_F;
  case TYPE_KIND_DOUBLE:
    return putstatic ? insn_putstatic_D : insn_getstatic_D;
  case TYPE_KIND_BYTE:
    return putstatic ? insn_putstatic_B : insn_getstatic_B;
  case TYPE_KIND_SHORT:
    return putstatic ? insn_putstatic_S : insn_getstatic_S;
  case TYPE_KIND_INT:
    return putstatic ? insn_putstatic_I : insn_getstatic_I;
  case TYPE_KIND_LONG:
    return putstatic ? insn_putstatic_J : insn_getstatic_J;
  case TYPE_KIND_REFERENCE:
    return putstatic ? insn_putstatic_L : insn_getstatic_L;
  default:
    UNREACHABLE();
    break;
  }
}

DEFINE_ASYNC(resolve_getstatic_putstatic) {
  // For brevity
#define inst self->args.inst
#define thread self->args.thread
  self->field_info = &inst->cp->field;
  self->class = self->field_info->class_info;

  // First, attempt to resolve the class that the instruction is referring to.
  if (resolve_class(thread, self->class))
    ASYNC_RETURN(-1);

  // Then, attempt to initialize the class.
  AWAIT(initialize_class, thread, self->class->classdesc);
  if (thread->current_exception)
    ASYNC_RETURN(-1);

#define field self->field_info->field

  // Look up the field on the class.
  field = field_lookup(self->class->classdesc, self->field_info->nat->name, self->field_info->nat->descriptor);

  // Check that the field exists on the class and is static. (TODO access checks)
  if (!field || !(field->access_flags & ACCESS_STATIC)) {
    INIT_STACK_STRING(complaint, 1000);
    bprintf(complaint, "Expected static field %.*s on class %.*s", fmt_slice(self->field_info->nat->name),
            fmt_slice(self->field_info->class_info->name));
    raise_incompatible_class_change_error(thread, complaint);
    ASYNC_RETURN(-1);
  }

  // Select the appropriate resolved instruction kind.
  bool putstatic = inst->kind == insn_putstatic;
  inst->kind = getstatic_putstatic_resolved_kind(putstatic, self->field_info->parsed_descriptor->repr_kind);

  // Store the static address of the field in the instruction.
  inst->ic = (void *)field->my_class->static_fields + field->byte_offset;

#undef thread
#undef inst
#undef field

  ASYNC_END(0);
}

#define TryResolve(thread_, insn_, frame_, sp_)                                                                        \
  do {                                                                                                                 \
    resolve_insn_t ctx = {.args = {thread_, insn_, frame_, sp_}};                                                      \
    future_t fut = resolve_insn(&ctx);                                                                                 \
    if (unlikely(fut.status == FUTURE_NOT_READY)) {                                                                    \
      continuation_frame *cont = async_stack_push(thread);                                                             \
      frame->is_async_suspended = true;                                                                                \
      *cont =                                                                                                          \
          (continuation_frame){.pnt = CONT_RESOLVE, .frame = frame_, .wakeup = fut.wakeup, .ctx.resolve_insn = ctx};   \
      return RETVAL_ASYNC_SUSPEND;                                                                                     \
    }                                                                                                                  \
    if (thread->current_exception)                                                                                     \
      return RETVAL_EXCEPTION_THROWN;                                                                                  \
  } while (0)

__attribute__((noinline)) static s64 getstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, frame, sp);
  JMP_VOID // we rewrote this instruction to a resolved form, so jump to that implementation
}
FORWARD_TO_NULLARY(getstatic)

// Never actually directly called -- we just do it this way because it's easier and we might as well merge code paths
// for different TOS types.
__attribute__((noinline)) static s64 putstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, frame, sp);
  STACK_POLYMORPHIC_JMP(*(sp - 1));
}
FORWARD_TO_NULLARY(putstatic)

static s64 getstatic_L_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(obj_header **)insn->ic);
}
FORWARD_TO_NULLARY(getstatic_L)

static s64 getstatic_F_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_FLOAT(*(float *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_F)

static s64 getstatic_D_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_DOUBLE(*(double *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_D)

static s64 getstatic_J_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(s64 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_J)

static s64 getstatic_I_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(int *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_I)

static s64 getstatic_S_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s16 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_S)

static s64 getstatic_C_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (u16 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_C)

static s64 getstatic_B_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s8 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_B)

static s64 getstatic_Z_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s8 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_Z)

static s64 putstatic_B_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(s8 *)insn->ic = (s8)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_C_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(u16 *)insn->ic = (u16)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_S_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(s16 *)insn->ic = (s16)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_I_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(int *)insn->ic = (int)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_J_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(s64 *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_F_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(float *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_D_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(double *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_L_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  *(obj_header **)insn->ic = (obj_header *)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_Z_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  DCHECK(tos == (bool)tos, "Illegal boolean value");
  *(s8 *)insn->ic = (s8)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** getfield/putfield */

insn_code_kind getfield_putfield_resolved_kind(bool putfield, type_kind field_kind) {
  switch (field_kind) {
  case TYPE_KIND_BOOLEAN:
    return putfield ? insn_putfield_B : insn_getfield_B;
  case TYPE_KIND_CHAR:
    return putfield ? insn_putfield_C : insn_getfield_C;
  case TYPE_KIND_FLOAT:
    return putfield ? insn_putfield_F : insn_getfield_F;
  case TYPE_KIND_DOUBLE:
    return putfield ? insn_putfield_D : insn_getfield_D;
  case TYPE_KIND_BYTE:
    return putfield ? insn_putfield_B : insn_getfield_B;
  case TYPE_KIND_SHORT:
    return putfield ? insn_putfield_S : insn_getfield_S;
  case TYPE_KIND_INT:
    return putfield ? insn_putfield_I : insn_getfield_I;
  case TYPE_KIND_LONG:
    return putfield ? insn_putfield_J : insn_getfield_J;
  case TYPE_KIND_REFERENCE:
    return putfield ? insn_putfield_L : insn_getfield_L;
  default:
    UNREACHABLE();
  }
}

DEFINE_ASYNC(resolve_getfield_putfield) {
  // For brevity
#define inst self->args.inst
#define thread self->args.thread
#define frame self->args.frame
#define sp_ self->args.sp_

  bool putfield = inst->kind == insn_putfield;

  obj_header *obj = (*(sp_ - 1 - putfield)).obj;
  if (!obj) {
    raise_null_pointer_exception(thread);
    ASYNC_RETURN(-1);
  }
  cp_field_info *field_info = &inst->cp->field;
  if (resolve_field(thread, field_info)) {
    ASYNC_RETURN(-1);
  }
  if (field_info->field->access_flags & ACCESS_STATIC) {
    INIT_STACK_STRING(complaint, 1000);
    bprintf(complaint, "Expected nonstatic field %.*s on class %.*s", fmt_slice(field_info->nat->name),
            fmt_slice(field_info->class_info->name));
    raise_incompatible_class_change_error(thread, complaint);
    ASYNC_RETURN(-1);
  }

  inst->kind = getfield_putfield_resolved_kind(putfield, field_info->parsed_descriptor->repr_kind);
  inst->ic = field_info->field;
  inst->ic2 = (void *)field_info->field->byte_offset;

  ASYNC_END(0);

#undef frame
#undef thread
#undef inst
#undef sp_
}

static s64 getfield_impl_int(ARGS_INT) {

  SPILL(tos)
  DEBUG_CHECK();

  TryResolve(thread, insn, frame, sp);

  RELOAD(tos)
  JMP_INT(tos)
}

static s64 putfield_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, frame, sp);
  STACK_POLYMORPHIC_JMP(*(sp - 1))
}
FORWARD_TO_NULLARY(putfield)

static s64 getfield_B_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  s8 *field = (s8 *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT((s64)*field)
}

static s64 getfield_C_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  u16 *field = (u16 *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT((s64)*field)
}

static s64 getfield_S_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  s16 *field = (s16 *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT((s64)*field)
}

static s64 getfield_I_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  int *field = (int *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT((s64)*field)
}

static s64 getfield_J_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  s64 *field = (s64 *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT(*field)
}

static s64 getfield_F_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  float *field = (float *)((char *)tos + (size_t)insn->ic2);
  NEXT_FLOAT(*field)
}

static s64 getfield_D_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  double *field = (double *)((char *)tos + (size_t)insn->ic2);
  NEXT_DOUBLE(*field)
}

static s64 getfield_L_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  obj_header **field = (obj_header **)((char *)tos + (size_t)insn->ic2);
  NEXT_INT(*field)
}

static s64 getfield_Z_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  s8 *field = (s8 *)((char *)tos + (size_t)insn->ic2);
  NEXT_INT((s64)*field)
}

static s64 putfield_B_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  s8 *field = (s8 *)((char *)obj + (size_t)insn->ic2);
  *field = (s8)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_C_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  u16 *field = (u16 *)((char *)obj + (size_t)insn->ic2);
  *field = (u16)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_S_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  s16 *field = (s16 *)((char *)obj + (size_t)insn->ic2);
  *field = (s16)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_I_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  int *field = (int *)((char *)obj + (size_t)insn->ic2);
  *field = (int)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_J_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  s64 *field = (s64 *)((char *)obj + (size_t)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_L_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  obj_header **field = (obj_header **)((char *)obj + (size_t)insn->ic2);
  *field = (obj_header *)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_Z_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  DCHECK(tos == (bool)tos, "Illegal boolean value");
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  s8 *field = (s8 *)((char *)obj + (size_t)insn->ic2);
  *field = (s8)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_F_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  float *field = (float *)((char *)obj + (size_t)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putfield_D_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  obj_header *obj = (sp - 2)->obj;
  NPE_ON_NULL(obj);
  double *field = (double *)((char *)obj + (size_t)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** Arithmetic operations */

// Binary operation on two integers (ints or longs)
#define INTEGER_BIN_OP(which, eval)                                                                                    \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    s64 a = (sp - 2)->l, b = tos;                                                                                      \
    s64 result = eval;                                                                                                 \
    sp--;                                                                                                              \
    NEXT_INT(result)                                                                                                   \
  }

INTEGER_BIN_OP(iadd, (s32)((u32)a + (u32)b))
INTEGER_BIN_OP(ladd, (s64)((u64)a + (u64)b))
INTEGER_BIN_OP(isub, (s32)((u32)a - (u32)b))
INTEGER_BIN_OP(lsub, (s64)((u64)a - (u64)b))
INTEGER_BIN_OP(imul, (s32)((u32)a *(u32)b))
INTEGER_BIN_OP(lmul, (s64)((u64)a *(u64)b))
INTEGER_BIN_OP(iand, (s32)((u32)a &(u32)b))
INTEGER_BIN_OP(land, (s64)((u64)a &(u64)b))
INTEGER_BIN_OP(ior, (s32)((u32)a | (u32)b))
INTEGER_BIN_OP(lor, (s64)((u64)a | (u64)b))
INTEGER_BIN_OP(ixor, (s32)((u32)a ^ (u32)b))
INTEGER_BIN_OP(lxor, (s64)((u64)a ^ (u64)b))
INTEGER_BIN_OP(ishl, (s32)((u32)a << (b & 0x1f)))
INTEGER_BIN_OP(lshl, (s64)((u64)a << (b & 0x3f)))
INTEGER_BIN_OP(ishr, (u32)((s32)a >> (b & 0x1f)))
INTEGER_BIN_OP(lshr, (s64)a >> (b & 0x3f))
INTEGER_BIN_OP(iushr, (s32)((u32)a >> (b & 0x1f)))
INTEGER_BIN_OP(lushr, (s64)((u64)a >> (b & 0x3f)))

#undef INTEGER_BIN_OP

#define INTEGER_UN_OP(which, eval, NEXT)                                                                               \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    s64 a = tos;                                                                                                       \
    NEXT(eval)                                                                                                         \
  }

INTEGER_UN_OP(ineg, (s32)(-(u32)a), NEXT_INT)
INTEGER_UN_OP(lneg, (s64)(-(u64)a), NEXT_INT)
INTEGER_UN_OP(i2l, (s64)a, NEXT_INT)
INTEGER_UN_OP(i2s, (s64)(s16)a, NEXT_INT)
INTEGER_UN_OP(i2b, (s64)(s8)a, NEXT_INT)
INTEGER_UN_OP(i2c, (s64)(u16)a, NEXT_INT)
INTEGER_UN_OP(i2f, (float)a, NEXT_FLOAT)
INTEGER_UN_OP(i2d, (double)a, NEXT_DOUBLE)
INTEGER_UN_OP(l2i, (int)a, NEXT_INT)
INTEGER_UN_OP(l2f, (float)a, NEXT_FLOAT)
INTEGER_UN_OP(l2d, (double)a, NEXT_DOUBLE)

#undef INTEGER_UN_OP

#define FLOAT_BIN_OP(which, eval, out_float, out_double, NEXT1, NEXT2)                                                 \
  static s64 f##which##_impl_float(ARGS_FLOAT) {                                                                       \
    DEBUG_CHECK();                                                                                                     \
    float a = (sp - 2)->f, b = tos;                                                                                    \
    out_float result = eval;                                                                                           \
    sp--;                                                                                                              \
    NEXT1(result)                                                                                                      \
  }                                                                                                                    \
  static s64 d##which##_impl_double(ARGS_DOUBLE) {                                                                     \
    DEBUG_CHECK();                                                                                                     \
    double a = (sp - 2)->d, b = tos;                                                                                   \
    out_double result = eval;                                                                                          \
    sp--;                                                                                                              \
    NEXT2(result)                                                                                                      \
  }

#define FLOAT_UN_OP(which, eval, out, NEXT)                                                                            \
  static s64 which##_impl_float(ARGS_FLOAT) {                                                                          \
    DEBUG_CHECK();                                                                                                     \
    float a = tos;                                                                                                     \
    out result = eval;                                                                                                 \
    NEXT(result)                                                                                                       \
  }

#define DOUBLE_UN_OP(which, eval, out, NEXT)                                                                           \
  static s64 which##_impl_double(ARGS_DOUBLE) {                                                                        \
    DEBUG_CHECK();                                                                                                     \
    double a = tos;                                                                                                    \
    out result = eval;                                                                                                 \
    NEXT(result)                                                                                                       \
  }

FLOAT_BIN_OP(add, a + b, float, double, NEXT_FLOAT, NEXT_DOUBLE)
FLOAT_BIN_OP(sub, a - b, float, double, NEXT_FLOAT, NEXT_DOUBLE)
FLOAT_BIN_OP(mul, a *b, float, double, NEXT_FLOAT, NEXT_DOUBLE)
FLOAT_BIN_OP(div, a / b, float, double, NEXT_FLOAT, NEXT_DOUBLE)
FLOAT_BIN_OP(cmpg, a > b ? 1 : (a < b ? -1 : (a == b ? 0 : 1)), int, int, NEXT_INT, NEXT_INT)
FLOAT_BIN_OP(cmpl, a > b ? 1 : (a < b ? -1 : (a == b ? 0 : -1)), int, int, NEXT_INT, NEXT_INT)

FLOAT_UN_OP(fneg, -a, float, NEXT_FLOAT)
FLOAT_UN_OP(f2i, float_to_int(a), int, NEXT_INT)
FLOAT_UN_OP(f2l, float_to_long(a), s64, NEXT_INT)
FLOAT_UN_OP(f2d, (double)a, double, NEXT_DOUBLE)

DOUBLE_UN_OP(dneg, -a, double, NEXT_DOUBLE)
DOUBLE_UN_OP(d2i, double_to_int(a), int, NEXT_INT)
DOUBLE_UN_OP(d2l, double_to_long(a), s64, NEXT_INT)
DOUBLE_UN_OP(d2f, (float)a, float, NEXT_FLOAT)

static s64 idiv_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  int a = (sp - 2)->i, b = (int)tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    raise_div0_arithmetic_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }
  sp--;
  NEXT_INT(java_idiv(a, b));
}

static s64 ldiv_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  s64 a = (sp - 2)->l, b = tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    raise_div0_arithmetic_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }
  sp--;
  NEXT_INT(java_ldiv(a, b));
}

static s64 lcmp_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  s64 a = (sp - 2)->l, b = tos;
  sp--;
  NEXT_INT(a > b ? 1 : (a < b ? -1 : 0));
}

static s64 irem_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  int a = (sp - 2)->i, b = (int)tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    raise_div0_arithmetic_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }
  sp--;
  NEXT_INT(java_irem(a, b));
}

static s64 lrem_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  s64 a = (sp - 2)->l, b = tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    raise_div0_arithmetic_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }
  sp--;
  NEXT_INT(java_lrem(a, b));
}

/** Array instructions (arraylength, array loads, array stores) */

static s64 arraylength_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *array = (obj_header *)tos;
  NPE_ON_NULL(array);
  NEXT_INT(ArrayLength(array))
}

#define ARRAY_LOAD(which, load, type, NEXT)                                                                            \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    obj_header *array = (obj_header *)(sp - 2)->obj;                                                                   \
    int index = (int)tos;                                                                                              \
    NPE_ON_NULL(array);                                                                                                \
    int length = ArrayLength(array);                                                                                   \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL_VOID;                                                                                                      \
      raise_array_index_oob_exception(thread, index, length);                                                          \
      return RETVAL_EXCEPTION_THROWN;                                                                                  \
    }                                                                                                                  \
    sp--;                                                                                                              \
    type cow = (type)load(array, index);                                                                               \
    NEXT(cow)                                                                                                          \
  }

ARRAY_LOAD(iaload, IntArrayLoad, int, NEXT_INT)
ARRAY_LOAD(laload, LongArrayLoad, s64, NEXT_INT)
ARRAY_LOAD(faload, FloatArrayLoad, float, NEXT_FLOAT)
ARRAY_LOAD(daload, DoubleArrayLoad, double, NEXT_DOUBLE)
ARRAY_LOAD(aaload, ReferenceArrayLoad, obj_header *, NEXT_INT)
ARRAY_LOAD(baload, ByteArrayLoad, s64, NEXT_INT)
ARRAY_LOAD(saload, ShortArrayLoad, s64, NEXT_INT)
ARRAY_LOAD(caload, CharArrayLoad, s64, NEXT_INT)

#define ARRAY_STORE(which, tt1, args, tt3, store)                                                                      \
  static s64 which##_impl_##tt1(args) {                                                                                \
    DEBUG_CHECK();                                                                                                     \
    obj_header *array = (obj_header *)(sp - 3)->obj;                                                                   \
    int index = (int)(sp - 2)->i;                                                                                      \
    NPE_ON_NULL(array);                                                                                                \
    int length = ArrayLength(array);                                                                                   \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL_VOID;                                                                                                      \
      raise_array_index_oob_exception(thread, index, length);                                                          \
      return RETVAL_EXCEPTION_THROWN;                                                                                  \
    }                                                                                                                  \
    store(array, index, (tt3)tos);                                                                                     \
    sp -= 3;                                                                                                           \
    STACK_POLYMORPHIC_NEXT(*(sp - 1));                                                                                 \
  }

ARRAY_STORE(iastore, int, ARGS_INT, int, IntArrayStore)
ARRAY_STORE(lastore, int, ARGS_INT, s64, LongArrayStore)
ARRAY_STORE(fastore, float, ARGS_FLOAT, float, FloatArrayStore)
ARRAY_STORE(dastore, double, ARGS_DOUBLE, double, DoubleArrayStore)
ARRAY_STORE(bastore, int, ARGS_INT, s8, ByteArrayStore)
ARRAY_STORE(sastore, int, ARGS_INT, s16, ShortArrayStore)
ARRAY_STORE(castore, int, ARGS_INT, u16, CharArrayStore)

// We implement aastore separately because it needs an additional instanceof check for ArrayStoreExceptions.
// <array> <index> <value>  ->  <void>
static s64 aastore_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *array = (obj_header *)(sp - 3)->obj;
  obj_header *value = (obj_header *)tos;
  int index = (int)(sp - 2)->i;
  NPE_ON_NULL(array);
  int length = ArrayLength(array);
  if (unlikely(index < 0 || index >= length)) {
    SPILL(tos);
    raise_array_index_oob_exception(thread, index, length);
    return RETVAL_EXCEPTION_THROWN;
  }
  // Instanceof check against the component type
  if (value && !instanceof(value->descriptor, array->descriptor->one_fewer_dim)) {
    SPILL(tos);
    raise_array_store_exception(thread, value->descriptor->name);
    return RETVAL_EXCEPTION_THROWN;
  }
  ReferenceArrayStore(array, index, value);
  sp -= 3;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** Control-flow instructions (returns, jumps, branches) */

static s64 return_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  return 0;
}
FORWARD_TO_NULLARY(return)

static s64 areturn_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos);
  return (s64)(uintptr_t)tos;
}

static s64 ireturn_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos);
  return (int)tos;
}

static s64 lreturn_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos);
  return tos;
}

static s64 freturn_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  SPILL(tos);
  s64 a = 0;
  memcpy(&a, &tos, sizeof(float));
  return a;
}

static s64 dreturn_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  SPILL(tos);
  s64 a = 0;
  memcpy(&a, &tos, sizeof(double));
  return a;
}

static s64 goto_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  FUEL_CHECK_VOID
  insns = (bytecode_insn*)((char*)insns + insn->delta);
  JMP_VOID
}

static s64 goto_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  FUEL_CHECK
  insns = (bytecode_insn*)((char*)insns + insn->delta);
  JMP_DOUBLE(tos)
}

static s64 goto_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  FUEL_CHECK
  insns = (bytecode_insn*)((char*)insns + insn->delta);
  JMP_FLOAT(tos)
}

static s64 goto_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  FUEL_CHECK
  insns = (bytecode_insn*)((char*)insns + insn->delta);
  JMP_INT(tos)
}

static s64 tableswitch_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  s32 index = (s32)tos;
  s32 low = insn->tableswitch->low;
  s32 high = insn->tableswitch->high;
  s32 *offsets = insn->tableswitch->targets;
  if (index < low || index > high) {
    int delta = (s32)(insn->tableswitch->default_target - 1) - (s32)pc;

    insns += delta;
  } else {
    int delta = (offsets[index - low] - 1) - pc;

    insns += delta;
  }
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 lookupswitch_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  struct lookupswitch_data data = *insn->lookupswitch;

  s32 key = (s32)tos;
  s32 *keys = data.keys;
  s32 *offsets = data.targets;
  s32 n = data.keys_count;
  s32 default_target = data.default_target;
  // Look through keys one by one
  for (int i = 0; i < n; i++) {
    if (keys[i] == key) {
      int delta = (offsets[i] - 1) - pc;

      insns += delta;
      sp--;
      STACK_POLYMORPHIC_NEXT(*(sp - 1));
    }
  }
  // No key found, jump to default
  int delta = (default_target - 1) - pc;

  insns += delta;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

#define MAKE_INT_BRANCH_AGAINST_0(which, op)                                                                           \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    FUEL_CHECK                                                                                                         \
    s32 offset = UNPREDICTABLE((s32)tos op 0) ? insn->delta : (s32)sizeof(bytecode_insn);                                   \
    insns = (bytecode_insn*)((char*)insns + offset);                                                                   \
    sp--;                                                                                                              \
    STACK_POLYMORPHIC_JMP(*(sp - 1));                                                                                  \
  }

MAKE_INT_BRANCH_AGAINST_0(ifeq, ==)
MAKE_INT_BRANCH_AGAINST_0(ifne, !=)
MAKE_INT_BRANCH_AGAINST_0(iflt, <)
MAKE_INT_BRANCH_AGAINST_0(ifge, >=)
MAKE_INT_BRANCH_AGAINST_0(ifgt, >)
MAKE_INT_BRANCH_AGAINST_0(ifle, <=)
MAKE_INT_BRANCH_AGAINST_0(ifnull, ==)
MAKE_INT_BRANCH_AGAINST_0(ifnonnull, !=)

#define MAKE_INT_BRANCH(which, op)                                                                                     \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    FUEL_CHECK                                                                                                         \
    s64 a = (sp - 2)->i, b = (int)tos;                                                                                 \
    s32 offset = UNPREDICTABLE((s32)a op(s32) b) ? insn->delta : (s32)sizeof(bytecode_insn);                                \
    insns = (bytecode_insn*)((char*)insns + offset);                                                                   \
    sp -= 2;                                                                                                           \
    STACK_POLYMORPHIC_JMP(*(sp - 1));                                                                                  \
  }

MAKE_INT_BRANCH(if_icmpeq, ==)
MAKE_INT_BRANCH(if_icmpne, !=)
MAKE_INT_BRANCH(if_icmplt, <)
MAKE_INT_BRANCH(if_icmpge, >=)
MAKE_INT_BRANCH(if_icmpgt, >)
MAKE_INT_BRANCH(if_icmple, <=)

static s64 if_acmpeq_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  FUEL_CHECK
  obj_header *a = (sp - 2)->obj, *b = (obj_header *)tos;
  s32 offset = UNPREDICTABLE(a == b) ? insn->delta : (s32)sizeof(bytecode_insn);
  insns = (bytecode_insn*)((char*)insns + offset);
  sp -= 2;
  STACK_POLYMORPHIC_JMP(*(sp - 1))
}

static s64 if_acmpne_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  FUEL_CHECK
  obj_header *a = (sp - 2)->obj, *b = (obj_header *)tos;
  s32 offset = UNPREDICTABLE(a != b) ? insn->delta : (s32)sizeof(bytecode_insn);
  insns = (bytecode_insn*)((char*)insns + offset);
  sp -= 2;
  STACK_POLYMORPHIC_JMP(*(sp - 1))
}

/** Monitors */

static s64 monitorenter_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);
  SPILL(tos);

  do {
    monitor_acquire_t ctx = {.args = {thread, (obj_header *)tos}};
    future_t fut = monitor_acquire(&ctx);
    if (unlikely(thread->current_exception)) { // oom
      return RETVAL_EXCEPTION_THROWN;
    }
    if (fut.status == FUTURE_NOT_READY) { // monitor is contended
      continuation_frame *cont = async_stack_push(thread);
      frame->is_async_suspended = true;
      *cont = (continuation_frame){
          .pnt = CONT_MONITOR_ENTER, .frame = frame, .wakeup = fut.wakeup, .ctx.acquire_monitor = ctx};
      return RETVAL_ASYNC_SUSPEND;
    }
  } while (0);

  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 monitorexit_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);

  obj_header *obj = (obj_header *)tos;
  int result = monitor_release(thread, obj);

  if (unlikely(result)) {
    SPILL_VOID
    raise_illegal_monitor_state_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }

  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** New object creation */

DEFINE_ASYNC(resolve_new_inst) {
  classdesc *classdesc = self->classdesc = args->inst->cp->class_info.classdesc;
  AWAIT(initialize_class, args->thread, classdesc);
  if (self->classdesc->state < CD_STATE_INITIALIZING) { // linkage error, etc.
    ASYNC_RETURN(-1);
  }
  args->inst->kind = insn_new_resolved;
  args->inst->classdesc = self->classdesc;
  ASYNC_END(0);
}

static s64 new_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID

  cp_class_info *info = &insn->cp->class_info;
  int error = resolve_class(thread, info);
  if (error)
    return RETVAL_EXCEPTION_THROWN;

  TryResolve(thread, insn, frame, sp);
  JMP_VOID
}
FORWARD_TO_NULLARY(new)

static s64 new_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  obj_header *obj = new_object(thread, insn->classdesc);
  if (!obj)
    return RETVAL_EXCEPTION_THROWN;

  sp++;
  NEXT_INT(obj)
}
FORWARD_TO_NULLARY(new_resolved)

static s64 newarray_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  int count = tos;
  SPILL(tos)
  if (unlikely(count < 0)) {
    raise_negative_array_size_exception(thread, count);
    return RETVAL_EXCEPTION_THROWN;
  }
  obj_header *array = CreatePrimitiveArray1D(thread, insn->array_type, count);
  if (unlikely(!array)) {
    return RETVAL_EXCEPTION_THROWN; // oom
  }
  NEXT_INT(array)
}

static s64 anewarray_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL_VOID
  cp_class_info *info = &insn->cp->class_info;
  if (resolve_class(thread, info)) {
    return RETVAL_EXCEPTION_THROWN;
  }
  DCHECK(info->classdesc);
  if (link_class(thread, info->classdesc)) {
    return RETVAL_EXCEPTION_THROWN;
  }
  insn->classdesc = info->classdesc;
  insn->kind = insn_anewarray_resolved;
  JMP_INT(tos)
}

// <length> -> <object>
static s64 anewarray_resolved_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL_VOID
  int count = tos;
  if (count < 0) {
    raise_negative_array_size_exception(thread, count);
    return RETVAL_EXCEPTION_THROWN;
    ;
  }
  obj_header *array = CreateObjectArray1D(thread, insn->classdesc, count);
  if (likely(array)) {
    NEXT_INT(array)
  }
  return RETVAL_EXCEPTION_THROWN;
  ; // oom
}

static s64 multianewarray_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos)
  u16 temp_sp = sp - frame_stack(frame);
  if (multianewarray(thread, frame, insn->multianewarray, &temp_sp))
    return RETVAL_EXCEPTION_THROWN;

  sp = frame_stack(frame) + temp_sp;
  NEXT_INT(frame_stack(frame)[temp_sp - 1].obj)
}

/** Method invocations */

static int intrinsify(bytecode_insn *inst) {
  cp_method *method = inst->ic;
  if (utf8_equals(method->my_class->name, "java/lang/Math")) {
    if (utf8_equals(method->name, "sqrt")) {
      inst->kind = insn_sqrt;
      return 1;
    }
    if (utf8_equals(method->name, "pow")) {
      inst->kind = insn_pow;
      return 1;
    }
  }
  return 0;
}

DEFINE_ASYNC(resolve_invokestatic) {
  AWAIT(resolve_methodref, self->args.thread, &self->args.insn_->cp->methodref);
  if (self->args.thread->current_exception) {
    ASYNC_RETURN(-1);
  }

  cp_method_info *info = &self->args.insn_->cp->methodref;
  self->args.insn_->kind = insn_invokestatic_resolved;
  self->args.insn_->ic = info->resolved;
  self->args.insn_->args = info->descriptor->args_count;

  mark_insn_returns(self->args.insn_);

  ASYNC_END(0);
}

__attribute__((noinline)) static s64 invokestatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, frame, sp);
  if (intrinsify(insn)) {
    STACK_POLYMORPHIC_JMP(*(sp - 1));
  }

  JMP_VOID
}
FORWARD_TO_NULLARY(invokestatic)

#define AttemptInvoke(thread, invoked_frame, argc, returns) return 0;

#define JIT_THRESHOLD 500

void attempt_jit(cp_method *method) {
  method->call_count = INT_MIN;
  return;

  CHECK(!method->jit_entry);
  dumb_jit_options options = {};
  dumb_jit_result *result = dumb_jit_compile(method, options);
  printf("Attempted JIT for %.*s.%.*s\n", fmt_slice(method->my_class->name), fmt_slice(method->name));
  if (result && method->trampoline) {
    printf("Result: %p\n", result->entry);
    method->jit_entry = result->entry;
  } else {
    method->call_count = INT_MIN;
  }
}

// Expects sp and insn->args to be in scope
#define ConsiderJitEntry(thread, method, argz)                                                                         \
  retry:                                                                                                               \
  if (method->jit_entry) {                                                                                             \
    ((jit_trampoline)(method)->trampoline)((method)->jit_entry, thread, method, argz);                                 \
    if (thread->current_exception) {                                                                                   \
      return RETVAL_EXCEPTION_THROWN;                                                                                  \
    }                                                                                                                  \
    sp -= insn->args;                                                                                                  \
    sp += returns;                                                                                                     \
    return 0;                                                                                                          \
  } else if (method->call_count > JIT_THRESHOLD) {                                                                     \
    attempt_jit(method);                                                                                               \
    goto retry;                                                                                                        \
  } else {                                                                                                             \
    method->call_count += 15;                                                                                          \
  }

static s64 invokestatic_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method *method = insn->ic;
  bool returns = insn->returns;
  stack_frame *invoked_frame;
  SPILL_VOID
  if (method->is_signature_polymorphic) {
    invoked_frame = push_native_frame(thread, method, insn->cp->methodref.descriptor, sp - insn->args, insn->args);
  } else {
    ConsiderJitEntry(thread, method, sp - insn->args);
    invoked_frame = push_frame(thread, method, sp - insn->args, insn->args);
  }
  if (unlikely(!invoked_frame)) {
    return RETVAL_EXCEPTION_THROWN;
  }

  AttemptInvoke(thread, invoked_frame, insn->args, returns);
}
FORWARD_TO_NULLARY(invokestatic_resolved)

__attribute__((noinline)) static s64 invokevirtual_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *receiver = (sp - argc)->obj;

  SPILL_VOID
  if (!receiver) {
    raise_null_pointer_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;

  ctx.args.info = &insn->cp->methodref;
  thread->stack.synchronous_depth++; // TODO remove
  future_t fut = resolve_methodref(&ctx);
  CHECK(fut.status == FUTURE_READY);
  thread->stack.synchronous_depth--;
  if (thread->current_exception) {
    return RETVAL_EXCEPTION_THROWN;
  }
  method_info = &insn->cp->methodref;
  mark_insn_returns(insn);

  // If we found a signature-polymorphic method, transmogrify into a insn_invokesigpoly
  if (method_info->resolved->is_signature_polymorphic) {
    insn->kind = insn_invokesigpoly;
    insn->ic = method_info->resolved;
    insn->ic2 = resolve_method_type(thread, method_info->descriptor);

    arrput(frame->method->my_class->sigpoly_insns, insn); // so GC can move around ic2
    JMP_VOID
  }

  // If we found an interface method, transmogrify into a invokeinterface
  if (method_info->resolved->my_class->access_flags & ACCESS_INTERFACE) {
    insn->kind = insn_invokeinterface;
    JMP_VOID
  }

  if (method_info->resolved->access_flags & ACCESS_FINAL) { // if the method is FINAL, we can make it an invokespecial
    insn->kind = insn_invokespecial_resolved;
    insn->ic = method_info->resolved;
    JMP_VOID
  }

  insn->kind = insn_invokevtable_monomorphic;
  insn->ic = vtable_lookup(receiver->descriptor, method_info->resolved->vtable_index);
  insn->ic2 = receiver->descriptor;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokevirtual)

__attribute__((noinline)) static s64 invokespecial_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *receiver = (sp - argc)->obj;
  SPILL_VOID
  if (!receiver) {
    raise_null_pointer_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  CHECK(fut.status == FUTURE_READY);
  if (thread->current_exception) {
    return RETVAL_EXCEPTION_THROWN;
  }

  method_info = &insn->cp->methodref;
  classdesc *lookup_on = method_info->resolved->my_class;
  cp_method *method = frame->method;

  // "If all of the following are true, let C [the class to look up the
  // method upon] be the direct superclass of the current class:
  //
  // The resolved method is not an instance initialization method;
  // If the symbolic reference names a class (not an interface), then that
  // class is a superclass of the current class."
  if (!method->is_ctor && (!(lookup_on->access_flags & ACCESS_INTERFACE) &&
                           (method->my_class != lookup_on && instanceof(method->my_class, lookup_on)))) {
    lookup_on = method->my_class->super_class->classdesc;
  }

  // Look at the class and its superclasses first
  cp_method *candidate =
      method_lookup(lookup_on, method_info->resolved->name, method_info->resolved->unparsed_descriptor, true, false);
  if (!candidate) {
    // Then perform an itable lookup (I rly don't think this is correct...)
    if (method_info->resolved->my_class->access_flags & ACCESS_INTERFACE) {
      candidate = itable_lookup(lookup_on, method_info->resolved->my_class, method_info->resolved->itable_index);
    }
    if (!candidate) {
      raise_abstract_method_error(thread, method_info->resolved);
      return RETVAL_EXCEPTION_THROWN;
    }
  } else if (candidate->access_flags & ACCESS_ABSTRACT) {
    raise_abstract_method_error(thread, candidate);
    return RETVAL_EXCEPTION_THROWN;
  }

  // If this is the <init> method of Object, make it a nop
  if (utf8_equals(candidate->my_class->name, "java/lang/Object") && utf8_equals(candidate->name, "<init>")) {
    insn->kind = insn_pop;
  } else {
    insn->kind = insn_invokespecial_resolved;
    insn->ic = candidate;
  }
  mark_insn_returns(insn);
  JMP_VOID
}
FORWARD_TO_NULLARY(invokespecial)

static s64 invokespecial_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *receiver = (sp - insn->args)->obj;
  bool returns = insn->returns;
  SPILL_VOID
  NPE_ON_NULL(receiver);

  cp_method *receiver_method = insn->ic;
  ConsiderJitEntry(thread, receiver_method, sp - insn->args);

  stack_frame *invoked_frame = push_frame(thread, receiver_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return RETVAL_EXCEPTION_THROWN;

  AttemptInvoke(thread, invoked_frame, insn->args, returns);
}
FORWARD_TO_NULLARY(invokespecial_resolved)

__attribute__((noinline)) static s64 invokeinterface_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *receiver = (sp - argc)->obj;
  SPILL_VOID
  if (!receiver) {
    raise_null_pointer_exception(thread);
    return RETVAL_EXCEPTION_THROWN;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  CHECK(fut.status == FUTURE_READY);
  if (thread->current_exception)
    return RETVAL_EXCEPTION_THROWN;

  method_info = &insn->cp->methodref;
  if (!(method_info->resolved->my_class->access_flags & ACCESS_INTERFACE)) {
    insn->kind = insn_invokevirtual;
    JMP_VOID
  }
  mark_insn_returns(insn);

  cp_method *method =
      itable_lookup(receiver->descriptor, method_info->resolved->my_class, method_info->resolved->itable_index);
  if (!method) {
    raise_abstract_method_error(thread, method_info->resolved);
    return RETVAL_EXCEPTION_THROWN;
  }

  if (method_argc(method) != method_argc(method_info->resolved)) {
    printf("Looking for method %.*s.%.*s, receiver is %.*s; found %.*s.%.*s\n",
           fmt_slice(method_info->resolved->my_class->name), fmt_slice(method_info->resolved->name),
           fmt_slice(receiver->descriptor->name), fmt_slice(method->my_class->name), fmt_slice(method->name));
  }

  if (method_info->resolved->access_flags & ACCESS_FINAL) { // if the method is FINAL, we can make it an invokespecial
    insn->kind = insn_invokespecial_resolved;
    insn->ic = method_info->resolved;
    JMP_VOID
  }

  insn->ic = method;
  insn->ic2 = receiver->descriptor;
  insn->kind = insn_invokeitable_monomorphic;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokeinterface)

__attribute__((noinline)) void make_invokevtable_polymorphic_(bytecode_insn *inst) {
  DCHECK(inst->kind == insn_invokevtable_monomorphic);
  cp_method *method = inst->ic;
  DCHECK(method);
  inst->kind = insn_invokevtable_polymorphic;
  inst->ic2 = (void *)method->vtable_index;
}

__attribute__((noinline)) void make_invokeitable_polymorphic_(bytecode_insn *inst) {
  DCHECK(inst->kind == insn_invokeitable_monomorphic);
  inst->kind = insn_invokeitable_polymorphic;
  inst->ic = (void *)inst->cp->methodref.resolved->my_class;
  inst->ic2 = (void *)inst->cp->methodref.resolved->itable_index;
}

static s64 invokeitable_vtable_monomorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *receiver = (sp - insn->args)->obj;
  bool returns = insn->returns;
  SPILL_VOID
  NPE_ON_NULL(receiver);
  if (unlikely(receiver->descriptor != insn->ic2)) {
    if (insn->kind == insn_invokevtable_monomorphic)
      make_invokevtable_polymorphic_(insn);
    else
      make_invokeitable_polymorphic_(insn);
    JMP_VOID
  }

  ConsiderJitEntry(thread, ((cp_method *)insn->ic), sp - insn->args);
  stack_frame *invoked_frame = push_frame(thread, insn->ic, sp - insn->args, insn->args);
  if (!invoked_frame)
    return RETVAL_EXCEPTION_THROWN;

  AttemptInvoke(thread, invoked_frame, insn->args, returns);
}
FORWARD_TO_NULLARY(invokeitable_vtable_monomorphic)

__attribute__((noinline)) static s64 invokesigpoly_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *receiver = (sp - insn->args)->obj;
  bool returns = insn->returns;
  SPILL_VOID
  NPE_ON_NULL(receiver);

  invokevirtual_signature_polymorphic_t ctx = {
      .args = {.thread = thread,
               .method = insn->ic,
               .sp_ = sp - insn->args,
               .provider_mt = (struct native_MethodType **)&insn->ic2, // GC root
               .target = receiver}};

  future_t fut = invokevirtual_signature_polymorphic(&ctx);
  if (unlikely(fut.status == FUTURE_NOT_READY)) {
    continuation_frame *cont = async_stack_push(thread);
    frame->is_async_suspended = true;
    *cont = (continuation_frame){.pnt = CONT_INVOKESIGPOLY, .frame = frame, .wakeup = fut.wakeup, .ctx.sigpoly = ctx};
    return RETVAL_ASYNC_SUSPEND;
  }

  if (thread->current_exception)
    return RETVAL_EXCEPTION_THROWN;

  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokesigpoly)

static s64 invokeitable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *receiver = (sp - insn->args)->obj;
  bool returns = insn->returns;
  SPILL_VOID
  NPE_ON_NULL(receiver);
  cp_method *receiver_method = itable_lookup(receiver->descriptor, insn->ic, (size_t)insn->ic2);
  if (unlikely(!receiver_method)) {
    raise_abstract_method_error(thread, insn->cp->methodref.resolved);
    return RETVAL_EXCEPTION_THROWN;
  }
  DCHECK(receiver_method);

  ConsiderJitEntry(thread, receiver_method, sp - insn->args);

  stack_frame *invoked_frame = push_frame(thread, receiver_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return RETVAL_EXCEPTION_THROWN;

  AttemptInvoke(thread, invoked_frame, insn->args, returns);
}
FORWARD_TO_NULLARY(invokeitable_polymorphic)

static s64 invokevtable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *receiver = (sp - insn->args)->obj;
  bool returns = insn->returns;
  SPILL_VOID
  NPE_ON_NULL(receiver);
  cp_method *receiver_method = vtable_lookup(receiver->descriptor, (size_t)insn->ic2);
  DCHECK(receiver_method);

  ConsiderJitEntry(thread, receiver_method, sp - insn->args);

  stack_frame *invoked_frame = push_frame(thread, receiver_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return RETVAL_EXCEPTION_THROWN;

  AttemptInvoke(thread, invoked_frame, insn->args, returns);
}
FORWARD_TO_NULLARY(invokevtable_polymorphic)

__attribute__((noinline)) static s64 invokedynamic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID

  cp_indy_info *indy = &insn->cp->indy_info;
  indy_resolve_t ctx = {};
  ctx.args.thread = thread;
#undef insn
  ctx.args.insn =
#define insn (&insns[0])
      insn;
  ctx.args.indy = indy;
  thread->stack.synchronous_depth++;
  future_t fut = indy_resolve(&ctx);
  thread->stack.synchronous_depth--;
  CHECK(fut.status == FUTURE_READY);

  if (thread->current_exception) {
    return RETVAL_EXCEPTION_THROWN;
  }

  DCHECK(insn->ic);
  insn->kind = insn_invokecallsite;
  struct native_CallSite *cs = insn->ic;
  struct native_MethodHandle *mh = (void *)cs->target;
  struct native_LambdaForm *form = (void *)mh->form;
  insn->args = form->arity;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokedynamic)

static s64 invokecallsite_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  // Call the "vmtarget" method with the correct number of arguments
  struct native_CallSite *cs = insn->ic;
  struct native_MethodHandle *mh = (void *)cs->target;
  struct native_LambdaForm *form = (void *)mh->form;
  struct native_MemberName *name = (void *)form->vmentry;

  method_handle_kind kind = (name->flags >> 24) & 0xf;
  SPILL_VOID
  if (kind == MH_KIND_INVOKE_STATIC) {
    // Invoke name->vmtarget with arguments mh, args
    cp_method *invoke = name->vmtarget;
    [[maybe_unused]] bool returns = form->result != -1;

    // State of the stack:
    // ... args
    //   ^------^ length insn->args - 1
    // Want prepend an mh:
    // [mh] ... args
    // ^------------^ length insn->args
    stack_value *arguments = sp - insn->args + 1;

    memmove(arguments + 1, arguments, (insn->args - 1) * sizeof(stack_value));
    arguments[0] = (stack_value){.obj = (void *)mh}; // MethodHandle

    stack_frame *invoked_frame = push_frame(thread, invoke, arguments, insn->args);
    if (!invoked_frame) {
      return RETVAL_EXCEPTION_THROWN;
    }

    AttemptInvoke(thread, invoked_frame, insn->args, returns);
  }
  UNREACHABLE(); // not yet handled
}
FORWARD_TO_NULLARY(invokecallsite)

static stack_value *get_local(stack_frame *frame, bytecode_insn *inst) { return (stack_value *)frame - inst->delta; }

/** Local variable accessors */
static s64 iload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(get_local(frame, insn)->i)
}
FORWARD_TO_NULLARY(iload)

static s64 fload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_FLOAT(get_local(frame, insn)->f)
}
FORWARD_TO_NULLARY(fload)

static s64 dload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_DOUBLE(get_local(frame, insn)->d)
}
FORWARD_TO_NULLARY(dload)

static s64 lload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(get_local(frame, insn)->l)
}
FORWARD_TO_NULLARY(lload)

static s64 aload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(get_local(frame, insn)->obj)
}
FORWARD_TO_NULLARY(aload)

static s64 astore_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  get_local(frame, insn)->obj = (obj_header *)tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 istore_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  get_local(frame, insn)->i = (int)tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 fstore_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  get_local(frame, insn)->f = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 dstore_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  get_local(frame, insn)->d = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 lstore_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  get_local(frame, insn)->l = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 iinc_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_VOID
}

static s64 iinc_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_DOUBLE(tos)
}

static s64 iinc_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_FLOAT(tos)
}

static s64 iinc_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_INT(tos)
}

/** Constant-pushing instructions */

static s64 aconst_null_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT((s64)0)
}
FORWARD_TO_NULLARY(aconst_null)

static s64 ldc_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  cp_entry *ent = insn->cp;
  switch (ent->kind) {
  case CP_KIND_INTEGER:
  case CP_KIND_FLOAT: {
    UNREACHABLE();
  }
  case CP_KIND_CLASS: {
    // Initialize the class, then get its Java mirror
    if (!ent->class_info.vm_object) {
      SPILL_VOID
      if (resolve_class(thread, &ent->class_info) || link_class(thread, ent->class_info.classdesc)) {
        DCHECK(thread->current_exception);
        return RETVAL_EXCEPTION_THROWN;
      }
      obj_header *obj = (void *)get_class_mirror(thread, ent->class_info.classdesc);
      ent->class_info.vm_object = obj;
    }
    NEXT_INT(ent->class_info.vm_object);
  }
  case CP_KIND_STRING: {
    if (likely(ent->string.interned)) {
      NEXT_INT(ent->string.interned);
    }
    slice s = ent->string.chars;
    SPILL_VOID
    obj_header *obj = MakeJStringFromModifiedUTF8(thread, s, true);
    if (!obj) { // oom
      DCHECK(thread->current_exception);
      return RETVAL_EXCEPTION_THROWN;
    }
    ent->string.interned = obj;
    NEXT_INT(obj);
  }
  case CP_KIND_DYNAMIC_CONSTANT: {
    UNREACHABLE("unimplemented");
  }
  default:
    UNREACHABLE();
  }
}
FORWARD_TO_NULLARY(ldc)

static s64 ldc2_w_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  cp_entry *ent = insn->cp;
  switch (ent->kind) {
  case CP_KIND_DOUBLE: {
    NEXT_DOUBLE(ent->floating.value);
  }
  case CP_KIND_LONG: {
    NEXT_INT(ent->integral.value);
  }
  default:
    UNREACHABLE();
  }
}
FORWARD_TO_NULLARY(ldc2_w)

static s64 iconst_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(insn->integer_imm)
}
FORWARD_TO_NULLARY(iconst)

static s64 fconst_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_FLOAT(insn->f_imm);
}
FORWARD_TO_NULLARY(fconst)

static s64 dconst_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_DOUBLE(insn->d_imm);
}
FORWARD_TO_NULLARY(dconst)

static s64 lconst_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(insn->integer_imm);
}
FORWARD_TO_NULLARY(lconst)

/** Stack manipulation instructions */

static s64 pop_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(pop)

static s64 pop2_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(pop2)

// Never directly called
static s64 swap_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value tmp = *(sp - 1);
  *(sp - 1) = *(sp - 2);
  *(sp - 2) = tmp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(swap)

EMSCRIPTEN_KEEPALIVE // read by the intrinsic converter
    s64
    nop_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  NEXT_VOID
}

s64 nop_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  NEXT_DOUBLE(tos)
}

s64 nop_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  NEXT_FLOAT(tos)
}

s64 nop_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NEXT_INT(tos)
}

static s64 dup_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  (sp++ - 1)->l = tos;
  NEXT_INT(tos)
}

static s64 dup_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  (sp++ - 1)->f = tos;
  NEXT_FLOAT(tos)
}

static s64 dup_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  (sp++ - 1)->d = tos;
  NEXT_DOUBLE(tos)
}

// dup2 is fairly common, so this is worth it (I think)
static s64 dup2_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  stack_value val2 = *(sp - 2);
  (sp - 1)->l = tos;
  *sp = val2;
  sp += 2;
  NEXT_INT(tos)
}

static s64 dup2_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  stack_value val2 = *(sp - 2);
  (sp - 1)->f = tos;
  *sp = val2;
  sp += 2;
  NEXT_INT(tos)
}

static s64 dup2_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  stack_value val2 = *(sp - 2);
  (sp - 1)->d = tos;
  *sp = val2;
  sp += 2;
  NEXT_INT(tos)
}

// Rare enough (especially after analysis phase) that we might as well just have the one implementation and forward
// every TOS type to them.

// ..., val2, val1 -> ..., val1, val2, val1
static s64 dup_x1_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value val1 = *(sp - 1), val2 = *(sp - 2);
  *(sp - 2) = val1;
  *(sp - 1) = val2;
  *(sp) = val1;
  sp++;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup_x1)

// ..., val3, val2, val1 -> val1, val3, val2, val1
static s64 dup_x2_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3);
  *(sp - 3) = val1;
  *(sp - 2) = val3;
  *(sp - 1) = val2;
  *(sp) = val1;
  sp++;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup_x2)

// ..., val3, val2, val1 -> ..., val2, val1, val3, val2, val1
static s64 dup2_x1_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3);
  *(sp - 3) = val2;
  *(sp - 2) = val1;
  *(sp - 1) = val3;
  *(sp) = val2;
  *(sp + 1) = val1;
  sp += 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup2_x1)

// ..., val4, val3, val2, val1 -> ..., val2, val1, val4, val3, val2, val1
static s64 dup2_x2_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3), val4 = *(sp - 4);
  *(sp - 4) = val2;
  *(sp - 3) = val1;
  *(sp - 2) = val4;
  *(sp - 1) = val3;
  *(sp) = val2;
  *(sp + 1) = val1;
  sp += 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup2_x2)

/** Misc. */
static s64 athrow_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos)
  NPE_ON_NULL(tos);
  thread->current_exception = (obj_header *)tos;
  return RETVAL_EXCEPTION_THROWN;
}

static s64 checkcast_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = resolve_class(thread, info) || link_class(thread, info->classdesc);
  if (error)
    return RETVAL_EXCEPTION_THROWN;

  RELOAD(tos)
  insn->classdesc = info->classdesc;
  insn->kind = insn_checkcast_resolved;
  JMP_INT(tos)
}

static s64 checkcast_resolved_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (obj_header *)tos;
  if (obj && unlikely(!instanceof(obj->descriptor, insn->classdesc))) {
    SPILL(tos)
    raise_class_cast_exception(thread, obj->descriptor, insn->classdesc);
    return RETVAL_EXCEPTION_THROWN;
  }
  NEXT_INT(tos)
}

static s64 instanceof_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = resolve_class(thread, info) || link_class(thread, info->classdesc);
  if (error)
    return RETVAL_EXCEPTION_THROWN;

  RELOAD(tos)
  insn->classdesc = info->classdesc;
  insn->kind = insn_instanceof_resolved;
  JMP_INT(tos)
}

static s64 instanceof_resolved_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  obj_header *obj = (obj_header *)tos;
  int result = obj ? instanceof(obj->descriptor, insn->classdesc) : 0;
  NEXT_INT(result)
}

static s64 sqrt_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  NEXT_DOUBLE(sqrt(tos))
}

static s64 sqrt_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  NEXT_FLOAT(sqrt(tos))
}

static s64 pow_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  double result = pow((sp - 2)->d, tos);
  sp -= 1;
  NEXT_DOUBLE(result)
}

static s64 pow_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  float result = pow((sp - 2)->f, tos);
  sp -= 1;
  NEXT_DOUBLE(result)
}

static s64 frem_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  float a = (sp - 2)->f, b = tos;
  sp -= 1;
  NEXT_FLOAT(fmodf(a, b))
}

static s64 drem_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  double a = (sp - 2)->d, b = tos;
  sp -= 1;
  NEXT_DOUBLE(fmod(a, b))
}

[[maybe_unused]] static s64 notco_fallback(ARGS_VOID, int index) {
  return bytecode_tables[index & 0x3][index >> 2](thread, frame, insns_, fuel_, sp_, arg_1, arg_2, arg_3);
}

[[maybe_unused]] static standard_debugger *get_active_debugger(vm *vm) { return vm->debugger; }

__attribute__((noinline)) void debugger_pause(vm_thread *thread, stack_frame *frame) {
  continuation_frame *cont = async_stack_push(thread);
  *cont = (continuation_frame){.pnt = CONT_DEBUGGER_PAUSE, .frame = frame};
  frame->is_async_suspended = true;
}

#if DO_TAILS
static s64 entry_impl_void(ARGS_VOID) { STACK_POLYMORPHIC_JMP(*(sp - 1)) }
#else
__attribute__((always_inline)) static s64 entry_notco_impl(vm_thread *thread, stack_frame *frame, bytecode_insn *code,
                                                           s32 fuel, stack_value *sp_, unsigned handler_i,
                                                           bool check_stepping) {
  struct spill_s {
    bytecode_insn *temp_code;
    stack_value *temp_sp_;
    int64_t temp_int_tos;
    double temp_double_tos;
    float temp_float_tos;
    int temp_fuel;
  } spill;

  int64_t int_tos = (sp_ - 1)->l;
  double double_tos = (sp_ - 1)->d;
  float float_tos = (sp_ - 1)->f;

  while (true) {
    if (check_stepping && unlikely(thread->is_single_stepping)) {
      standard_debugger *dbg = get_active_debugger(thread->vm);
      DCHECK(dbg && "Debugger not active");
      frame->program_counter = code - frame->code;
      bool should_pause = dbg->should_pause(dbg, thread, frame);
      if (should_pause) {
        debugger_pause(thread, frame);
        return 0;
      }
    }

    enum {
      tos_int = TOS_INT,
      tos_double = TOS_DOUBLE,
      tos_float = TOS_FLOAT,
      tos_void = TOS_VOID,
    };

    switch (handler_i) {

#define INL(insn__, tos__)                                                                                             \
  case 4 * insn_##insn__ + tos_##tos__:                                                                                \
    handler_i = insn__##_impl_##tos__(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);           \
    break;

#include "interpreter2-notco.inc"

    case 4 * insn_return + TOS_INT:
    case 4 * insn_return + TOS_DOUBLE:
    case 4 * insn_return + TOS_FLOAT:
    case 4 * insn_return + TOS_VOID:
      return return_impl_void(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);

    case 4 * insn_areturn + TOS_INT:
      return areturn_impl_int(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_dreturn + TOS_DOUBLE:
      return dreturn_impl_double(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_freturn + TOS_FLOAT:
      return freturn_impl_float(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_ireturn + TOS_INT:
      return ireturn_impl_int(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_lreturn + TOS_INT:
      return lreturn_impl_int(thread, frame, &code, &fuel, &sp_, &int_tos, &float_tos, &double_tos);

    case 0:
    case RETVAL_EXCEPTION_THROWN:
    case RETVAL_FUEL_CHECK:
    case RETVAL_ASYNC_SUSPEND: // special value in case of exception or suspend or invoke (theoretically also
                               // nop_impl_void, but javac doesn't use that)
      return handler_i;
    default: {
      // outlined case
      spill.temp_code = code;
      spill.temp_sp_ = sp_;
      spill.temp_double_tos = double_tos;
      spill.temp_float_tos = float_tos;
      spill.temp_int_tos = int_tos;
      spill.temp_fuel = fuel;

#ifdef EMSCRIPTEN
#define notco_call __interpreter_intrinsic_notco_call_outlined
#else
#define notco_call notco_fallback
#endif

      handler_i = notco_call(thread, frame, &spill.temp_code, &spill.temp_fuel, &spill.temp_sp_, &spill.temp_int_tos,
                             &spill.temp_float_tos, &spill.temp_double_tos, (int)handler_i);

      // All branch instructions are inlined, and are the only fuel-changing insns, so no need to reload temp_fuel
      code = spill.temp_code;
      sp_ = spill.temp_sp_;
      int_tos = spill.temp_int_tos;
      float_tos = spill.temp_float_tos;
      double_tos = spill.temp_double_tos;

      break;
    }
    }
  }
}

[[maybe_unused]] __attribute__((noinline)) static s64 entry_notco_no_stepping(vm_thread *thread, stack_frame *frame,
                                                                              bytecode_insn *code, s32 fuel_,
                                                                              stack_value *sp_, unsigned handler_i) {
  return entry_notco_impl(thread, frame, code, fuel_, sp_, handler_i, false);
}

[[maybe_unused]] __attribute__((noinline)) static s64 entry_notco_with_stepping(vm_thread *thread, stack_frame *frame,
                                                                                bytecode_insn *code, s32 fuel_,
                                                                                stack_value *sp_, unsigned handler_i) {
  return entry_notco_impl(thread, frame, code, fuel_, sp_, handler_i, true);
}
#endif

static exception_table_entry *find_exception_handler(vm_thread *thread, stack_frame *frame, classdesc *exception_type) {
  DCHECK(is_interpreter_frame(frame));

  attribute_exception_table *table = frame->method->code->exception_table;
  if (!table)
    return nullptr;

  int const pc_ = frame->program_counter;

  for (int i = 0; i < table->entries_count; ++i) {
    exception_table_entry *ent = &table->entries[i];

    if (ent->start_insn <= pc_ && pc_ < ent->end_insn) {
      if (ent->catch_type) {
        int error = resolve_class(thread, ent->catch_type) || link_class(thread, ent->catch_type->classdesc);
        if (error)
          continue; // can happen if the current classloader != verifier classloader?
      }

      if (!ent->catch_type || instanceof(exception_type, ent->catch_type->classdesc)) {
        if (ent->catch_type)
          DCHECK(ent->catch_type->classdesc->state >= CD_STATE_INITIALIZED);

        return ent;
      }
    }
  }

  return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
static s64 async_resume_impl_void(ARGS_VOID) {
  // We need to pop (not peek) because if we re-enter this method, it'll need to pop its frame
  continuation_frame cont = *async_stack_pop(thread);

  future_t fut;

  bool advance_pc = false;
  bool needs_polymorphic_jump = false;

  switch (cont.pnt) {
  case CONT_RESOLVE:
    bytecode_insn *in = cont.ctx.resolve_insn.args.inst;
    fut = resolve_insn(&cont.ctx.resolve_insn);

    int fail = cont.ctx.resolve_insn._result;
    if (!fail && in->kind == insn_invokestatic && fut.status == FUTURE_READY) {
      needs_polymorphic_jump = intrinsify(in);
    }
    break;

  case CONT_DEBUGGER_PAUSE:
  case CONT_RESUME_INSN: {
    fut.status = FUTURE_READY;
    needs_polymorphic_jump = true;
    break;
  }

  case CONT_MONITOR_ENTER:
    fut = monitor_acquire(&cont.ctx.acquire_monitor);
    advance_pc = true;
    break;

  case CONT_INVOKESIGPOLY:
    fut = invokevirtual_signature_polymorphic(&cont.ctx.sigpoly);
    advance_pc = true;
    break;

  default:
    UNREACHABLE();
  }

  if (fut.status == FUTURE_NOT_READY) {
    cont.wakeup = fut.wakeup;
    *async_stack_push(thread) = cont;
    frame->is_async_suspended = true;
    return RETVAL_ASYNC_SUSPEND;
  }

  // Now calculate sp correctly depending on whether we're advancing an instruction or not
  sp = frame->stack + frame->insn_index_to_sd[pc + advance_pc]; // correct sp

  frame->is_async_suspended = false;
  if (unlikely(thread->current_exception)) {
    return RETVAL_EXCEPTION_THROWN;
  }

#if !DO_TAILS
  arg_1 = &(sp - 1)->l;
  arg_2 = &(sp - 1)->f;
  arg_3 = &(sp - 1)->d;
#endif

  if (advance_pc) {
    STACK_POLYMORPHIC_NEXT(*(sp - 1));
  } else if (needs_polymorphic_jump) {
    STACK_POLYMORPHIC_JMP(*(sp - 1));
  } else {
    JMP_VOID;
  }
}

object get_sync_object(vm_thread *thread, stack_frame *frame) {
  u16 flags = frame->method->access_flags;
  object synchronized_on = nullptr;
  if (unlikely(flags & ACCESS_SYNCHRONIZED)) {
    if (flags & ACCESS_STATIC) { // synchronize on the .class
      synchronized_on = (void *)get_class_mirror(thread, frame->method->my_class);
    } else { // synchronize on "this"
      synchronized_on = frame_locals(frame)[0].obj;
    }
    assert(synchronized_on && "is null");
  }
  return synchronized_on;
}

static bool do_entry_synchronization(future_t *fut, vm_thread *thread, stack_frame *frame, object synchronized_on) {
  bool already_tried = frame->synchronized_state == SYNCHRONIZE_IN_PROGRESS;

  monitor_acquire_t ctx = (monitor_acquire_t){.args = {thread, synchronized_on}};
  if (unlikely(already_tried)) {  // resume the previous call to monitor acuiqre
    continuation_frame *cont = async_stack_pop(thread);
    DCHECK(cont->pnt == CONT_SYNCHRONIZED_METHOD);
    ctx = cont->ctx.acquire_monitor;
  }

  *fut = monitor_acquire(&ctx);
  if (fut->status == FUTURE_NOT_READY) {
    continuation_frame *cont = async_stack_push(thread);
    *cont = (continuation_frame){.pnt = CONT_SYNCHRONIZED_METHOD, .frame = frame, .wakeup = fut->wakeup, .ctx.acquire_monitor = ctx};
    frame->synchronized_state = SYNCHRONIZE_IN_PROGRESS;
    frame->is_async_suspended = true;
    return true;
  }

  frame->synchronized_state = SYNCHRONIZE_DONE;
  frame->is_async_suspended = false;
  return false;
}

// Executed whenever a frame begins or is resumed. (i.e., it can be called multiple times)
static bool on_frame_start(future_t *fut, vm_thread *thread, stack_frame *frame) {
  object synchronized_on = get_sync_object(thread, frame);
  if (unlikely(synchronized_on) && frame->synchronized_state < SYNCHRONIZE_DONE) {
    return do_entry_synchronization(fut, thread, frame, synchronized_on);
  }
  return false;
}

// Executed whenever a frame ends.
static void on_frame_end(vm_thread *thread, stack_frame *frame) {
  object synchronized_on = get_sync_object(thread, frame); // re-compute in case of intervening GC
  if (unlikely(synchronized_on)) {                         // monitor release at the end of synchronized
    int err = monitor_release(thread, synchronized_on);
    if (err) {
      // the current exception is replaced with an IllegalMonitorStateException
      thread->current_exception = nullptr;
      raise_illegal_monitor_state_exception(thread);
    }
  }
}

// The interpreter handles a contiguous chain of Java method calls. An intervening native frame, or something that is
// not a Java method call resulting from an invoke* (e.g., resolving a method ref, calling <clinit>) breaks the chain.
// Entries into JITed code also break the chain.
//
// The flowchart of the interpreter is as follows:
//
//  Entry
//    ↓
// ┌──┴──────────────────┐       ┌──────────────────────────────────────┐
// │ Is async suspended? ├──yes─→┤ Resume at end of chain, no cont. pop │
// └───────┬─────────────┘       └────────┬─────────────────────────────┘
//         ↓ no (current = entry_frame)   │
//      ┌──┴───────────────────────┐      ╎        ╔════════════════════════════════╗
//  ┌──→│ Synchronize on current?  ├─suspended────→║ cont: CONT_SYNCHRONIZED_METHOD ║
//  │   └──┬───────────────────────┘      ╎        ╚════════════════════════════════╝
//  │      ↓ ok               ┌───────────┘
//  │   ┌──┴──────────────────┴─────┐       ┌─────────────────┐            ╔═══════════════════╗
//  │   │ current frame is native?  ├──yes─→┤cont. pop if sus.├─suspended─→║ cont: CONT_NATIVE ║
//  │   └──┬────────────────────────┘       │ run native call │──→─┐       ╚═══════════════════╝
//  │      │                                └─────────────────┘    │ok
//  │      ↓ no                                                    │
//  │   ┌──┴──────────────────┐       ┌────────────────────────┐   ╎        ╔═════════════════╗
//  │   │ Is async suspended? ├──yes─→┤ cont pop, async_resume ├─suspended─→║ cont: (various) ║
//  │   └──┬──────────────────┘       └─┬──────────────────────┘   ╎        ╚═════════════════╝
//  │      ↓ no               ┌──←──────┘ ok                       │
//  │   ┌──┴──────────────────┴┐            ┌────────────────┐ no  │        ╔═════════════════╗
//  │ ┌→│ Interpret Java frame ├─suspended─→│is refuel check?│─→─╌╌│╌╌──────║ cont: (various) ║
//  │ │ └──┬──────────────────┬┘←─┐         └───────┬────────┘     │        ╚═════════════════╝
//  │ │    │                  │   │                 ↓ yes          │
//  │ │    │                  │   │     no  ┌───────┴────────┐ yes │        ╔════════════════════════╗
//  │ │    │                  │   └────────←┤deadline passed?│─→─╌╌│╌╌──────║ cont: CONT_RESUME_INSN ║
//  │ │    │                  │             └────────────────┘     │        ╚════════════════════════╝
//  │ │    ↓ ok               └──←────┐ yes                        │
//  │ │ ┌──┴────────────────┐       ┌─┴──────────────────┐         │
//  │ │ │ Exception thrown? ├──yes─→│ Exception handler  │         │
// y│ │ └──┬────────────────┘       │  in this frame?    │         │
// e│ │    ↓ no                     └─┬───────────────┬──┘         │
// s│ │ ┌──┴────────────────────┐     │ no            │            │
//  └╌│←│Thread top != current? │     │               │            │
//[1] │ │(current := thread top)│     │               │            │
//    │ └──┬────────────────────┘     │               │            │
//    │    ↓ no            ┌──←───────┘               ╎            │
//    │ ┌──┴───────────────┴─┐←────────────────────────────────────┘
//    │ │Unsynchronize frame,│                        ╎
//    │ │     pop frame      │                        │
//    │ └──┬─────────────────┘                        │
//    │    ↓                                          │
//    │ ┌──┴─────────────────┐      ╔═══════════════╗ │
//    │ │frame = entry_frame?├─yes─→║ return result ║ │
//    │ └──┬─────────────────┘      ╚═══════════════╝ │
//    │    ↓ no                                       │
// no │ ┌──┴─────────────────┐                        │
//    └─┤ Pending exception? ├─yes────────────────→───┘
//      └────────────────────┘
//
// Upon an async yield, both the entry frame and the last frame (which may be the same frame; but not intermediate
// frames, if any) are marked as async-suspended. The "entry frame" is the first frame in the chain. The continuation
// frame stores the last Java frame in the chain.
//
// [1] When a invoke* bytecode is hit, it pushes a frame, changing the thread stack top to a new value which is no
// longer equal to the current frame. This is how we signify method entry.
//
// NOLINTNEXTLINE(misc-no-recursion)
stack_value interpret_2(future_t *fut, vm_thread *thread, stack_frame *entry_frame) {
  stack_frame *current_frame = entry_frame;
  if (unlikely(entry_frame->is_async_suspended)) {
    // Advance the interpreter to the frame in this chain of pure Java calls which was suspended
    current_frame = async_stack_peek(thread)->frame;
  }

  stack_value result;
  while (true) {
    if (unlikely(on_frame_start(fut, thread, current_frame))) {
      // Suspension during synchronization
      entry_frame->is_async_suspended = true;
      return (stack_value){0};
    }

    if (is_frame_native(current_frame)) {
      /** Handle native calls */
      run_native_t ctx;

      if (!current_frame->is_async_suspended) {
        // We're not resuming from async. Spawn the native call
        ctx = (run_native_t){.args = {.thread = thread, .frame = current_frame}};
      } else {
        // We were already calling the native and it's an async native. Resume it.
        continuation_frame *cont = async_stack_pop(thread);
        DCHECK(cont->pnt == CONT_RUN_NATIVE);
        ctx = cont->ctx.run_native;
      }

      // Run or continue the native call
      *fut = run_native(&ctx);

      if (likely(fut->status == FUTURE_READY)) {
        // The native call finished. Go to the outer frame as normal.
        current_frame->is_async_suspended = false;
        entry_frame->is_async_suspended = false;
        result = ctx._result;
      } else {
        continuation_frame *cont = async_stack_push(thread);
        *cont = (continuation_frame){
            .pnt = CONT_RUN_NATIVE, .frame = current_frame, .wakeup = fut->wakeup, .ctx.run_native = ctx};
        current_frame->is_async_suspended = true;
        entry_frame->is_async_suspended = true;
        return (stack_value){0};
      }
    } else {
    java_interpret_begin:

      /** Handle Java frames */
      s32 pc_ = current_frame->program_counter;
      s32 fuel_ = (s32)thread->fuel;
      stack_value *sp_ = &current_frame->stack[stack_depth(current_frame)];
      bytecode_insn *insns_ = current_frame->code + pc_;

      // No-TCO case only
      [[maybe_unused]] unsigned handler_i = 4 * insns_->kind + insns_->tos_before;

      if (unlikely(current_frame->is_async_suspended)) {
        entry_frame->is_async_suspended = false;
#if DO_TAILS
        result.l = async_resume_impl_void(thread, current_frame, insns_, fuel_, sp_, 0, 0, 0);
#else
        handler_i = async_resume_impl_void(thread, current_frame, &insns_, &fuel_, &sp_, nullptr, nullptr, nullptr);
#endif
      }

#if DO_TAILS
      else {
        result.l = entry_impl_void(thread, current_frame, insns_, fuel_, sp_, 0, 0, 0);
      }
#else
      if (likely(!current_frame->is_async_suspended && !thread->current_exception)) {
        // In the no-tails case, sp, pc, etc. will have been set up appropriately for this call
        if (thread->is_single_stepping) {
          result.l = entry_notco_with_stepping(thread, current_frame, insns_, fuel_, sp_, handler_i);
        } else {
          result.l = entry_notco_no_stepping(thread, current_frame, insns_, fuel_, sp_, handler_i);
        }
      }
#endif

      if (unlikely(current_frame->is_async_suspended)) {
#if DO_FUEL_CHECKS
        if (result.l == RETVAL_FUEL_CHECK) {
          bool preempting = refuel_check(thread);
          if (!preempting) {
            current_frame->is_async_suspended = false;
            goto java_interpret_begin;
          }
        }
#endif

        // reconstruct future to return
        void *wk = async_stack_peek(thread)->wakeup;
        entry_frame->is_async_suspended = true;
        *fut = (future_t){FUTURE_NOT_READY, wk};
        return (stack_value){0};
      }

      if (thread->stack.top != current_frame) { // Java -> (Java or native) method call
        DCHECK(result.l == 0);
        DCHECK(!current_frame->is_async_suspended);
        current_frame = thread->stack.top;
        continue;
      }

      if (unlikely(thread->current_exception)) {
      find_exception_handler:
        exception_table_entry *handler =
            find_exception_handler(thread, current_frame, thread->current_exception->descriptor);
        if (handler) {
          current_frame->program_counter = handler->handler_insn;
          current_frame->stack[0] = (stack_value){.obj = thread->current_exception};
          thread->current_exception = nullptr;
          goto java_interpret_begin;
        }
      }
    }

    /** End frame logic */
    on_frame_end(thread, current_frame);
    pop_frame(thread, current_frame);
    if (current_frame == entry_frame) { // done with this chain of interpreter frames
      break;
    }
    frame_beginning(current_frame)[0] = result;

    current_frame = thread->stack.top;
    DCHECK(is_interpreter_frame(current_frame));
    if (thread->current_exception) {
      goto find_exception_handler;
    }
    current_frame->program_counter++; // advance past the invoke* instruction
    goto java_interpret_begin;
  }

  fut->status = FUTURE_READY;
  return result;
}

/** Jump table definitions. Must be kept in sync with the enum order. */

#define PAGE_ALIGN _Alignas(4096)

// This comment is searched by codegen and should precede the table list vvv
// CODEGEN BEGIN TABLES
PAGE_ALIGN static s64 (*jmp_table_void[MAX_INSN_KIND])(ARGS_VOID) = {
    [insn_nop] = nop_impl_void,
    [insn_aconst_null] = aconst_null_impl_void,
    [insn_pop] = pop_impl_void,
    [insn_return] = return_impl_void,
    [insn_getstatic] = getstatic_impl_void,
    [insn_invokedynamic] = invokedynamic_impl_void,
    [insn_new] = new_impl_void,
    [insn_invokevirtual] = invokevirtual_impl_void,
    [insn_invokestatic] = invokestatic_impl_void,
    [insn_ldc] = ldc_impl_void,
    [insn_ldc2_w] = ldc2_w_impl_void,
    [insn_dload] = dload_impl_void,
    [insn_fload] = fload_impl_void,
    [insn_iload] = iload_impl_void,
    [insn_lload] = lload_impl_void,
    [insn_aload] = aload_impl_void,
    [insn_goto] = goto_impl_void,
    [insn_iconst] = iconst_impl_void,
    [insn_dconst] = dconst_impl_void,
    [insn_fconst] = fconst_impl_void,
    [insn_lconst] = lconst_impl_void,
    [insn_iinc] = iinc_impl_void,
    [insn_invokeinterface] = invokeinterface_impl_void,
    [insn_new_resolved] = new_resolved_impl_void,
    [insn_invokevtable_monomorphic] = invokeitable_vtable_monomorphic_impl_void,
    [insn_invokevtable_polymorphic] = invokevtable_polymorphic_impl_void,
    [insn_invokeitable_monomorphic] = invokeitable_vtable_monomorphic_impl_void,
    [insn_invokeitable_polymorphic] = invokeitable_polymorphic_impl_void,
    [insn_invokespecial_resolved] = invokespecial_resolved_impl_void,
    [insn_invokestatic_resolved] = invokestatic_resolved_impl_void,
    [insn_invokecallsite] = invokecallsite_impl_void,
    [insn_invokesigpoly] = invokesigpoly_impl_void,
    [insn_getstatic_B] = getstatic_B_impl_void,
    [insn_getstatic_C] = getstatic_C_impl_void,
    [insn_getstatic_S] = getstatic_S_impl_void,
    [insn_getstatic_I] = getstatic_I_impl_void,
    [insn_getstatic_J] = getstatic_J_impl_void,
    [insn_getstatic_F] = getstatic_F_impl_void,
    [insn_getstatic_D] = getstatic_D_impl_void,
    [insn_getstatic_Z] = getstatic_Z_impl_void,
    [insn_getstatic_L] = getstatic_L_impl_void,
};

PAGE_ALIGN static s64 (*jmp_table_double[MAX_INSN_KIND])(ARGS_VOID) = {
    [insn_nop] = nop_impl_double,
    [insn_aconst_null] = aconst_null_impl_double,
    [insn_d2f] = d2f_impl_double,
    [insn_d2i] = d2i_impl_double,
    [insn_d2l] = d2l_impl_double,
    [insn_dadd] = dadd_impl_double,
    [insn_dastore] = dastore_impl_double,
    [insn_dcmpg] = dcmpg_impl_double,
    [insn_dcmpl] = dcmpl_impl_double,
    [insn_ddiv] = ddiv_impl_double,
    [insn_dmul] = dmul_impl_double,
    [insn_dneg] = dneg_impl_double,
    [insn_dreturn] = dreturn_impl_double,
    [insn_dsub] = dsub_impl_double,
    [insn_dup] = dup_impl_double,
    [insn_dup_x1] = dup_x1_impl_double,
    [insn_dup_x2] = dup_x2_impl_double,
    [insn_dup2] = dup2_impl_double,
    [insn_dup2_x1] = dup2_x1_impl_double,
    [insn_dup2_x2] = dup2_x2_impl_double,
    [insn_pop] = pop_impl_double,
    [insn_pop2] = pop2_impl_double,
    [insn_return] = return_impl_double,
    [insn_swap] = swap_impl_double,
    [insn_getstatic] = getstatic_impl_double,
    [insn_invokedynamic] = invokedynamic_impl_double,
    [insn_new] = new_impl_double,
    [insn_putfield] = putfield_impl_double,
    [insn_putstatic] = putstatic_impl_double,
    [insn_invokevirtual] = invokevirtual_impl_double,
    [insn_invokespecial] = invokespecial_impl_double,
    [insn_invokestatic] = invokestatic_impl_double,
    [insn_ldc] = ldc_impl_double,
    [insn_ldc2_w] = ldc2_w_impl_double,
    [insn_dload] = dload_impl_double,
    [insn_fload] = fload_impl_double,
    [insn_iload] = iload_impl_double,
    [insn_lload] = lload_impl_double,
    [insn_dstore] = dstore_impl_double,
    [insn_aload] = aload_impl_double,
    [insn_goto] = goto_impl_double,
    [insn_iconst] = iconst_impl_double,
    [insn_dconst] = dconst_impl_double,
    [insn_fconst] = fconst_impl_double,
    [insn_lconst] = lconst_impl_double,
    [insn_iinc] = iinc_impl_double,
    [insn_invokeinterface] = invokeinterface_impl_double,
    [insn_new_resolved] = new_resolved_impl_double,
    [insn_invokevtable_monomorphic] = invokeitable_vtable_monomorphic_impl_double,
    [insn_invokevtable_polymorphic] = invokevtable_polymorphic_impl_double,
    [insn_invokeitable_monomorphic] = invokeitable_vtable_monomorphic_impl_double,
    [insn_invokeitable_polymorphic] = invokeitable_polymorphic_impl_double,
    [insn_invokespecial_resolved] = invokespecial_resolved_impl_double,
    [insn_invokestatic_resolved] = invokestatic_resolved_impl_double,
    [insn_invokecallsite] = invokecallsite_impl_double,
    [insn_invokesigpoly] = invokesigpoly_impl_double,
    [insn_putfield_D] = putfield_D_impl_double,
    [insn_getstatic_B] = getstatic_B_impl_double,
    [insn_getstatic_C] = getstatic_C_impl_double,
    [insn_getstatic_S] = getstatic_S_impl_double,
    [insn_getstatic_I] = getstatic_I_impl_double,
    [insn_getstatic_J] = getstatic_J_impl_double,
    [insn_getstatic_F] = getstatic_F_impl_double,
    [insn_getstatic_D] = getstatic_D_impl_double,
    [insn_getstatic_Z] = getstatic_Z_impl_double,
    [insn_getstatic_L] = getstatic_L_impl_double,
    [insn_putstatic_D] = putstatic_D_impl_double,
    [insn_drem] = drem_impl_double,
    [insn_pow] = pow_impl_double,
    [insn_sqrt] = sqrt_impl_double};

PAGE_ALIGN static s64 (*jmp_table_int[MAX_INSN_KIND])(ARGS_VOID) = {
    [insn_nop] = nop_impl_int,
    [insn_aaload] = aaload_impl_int,
    [insn_aastore] = aastore_impl_int,
    [insn_aconst_null] = aconst_null_impl_int,
    [insn_areturn] = areturn_impl_int,
    [insn_arraylength] = arraylength_impl_int,
    [insn_athrow] = athrow_impl_int,
    [insn_baload] = baload_impl_int,
    [insn_bastore] = bastore_impl_int,
    [insn_caload] = caload_impl_int,
    [insn_castore] = castore_impl_int,
    [insn_daload] = daload_impl_int,
    [insn_dup] = dup_impl_int,
    [insn_dup_x1] = dup_x1_impl_int,
    [insn_dup_x2] = dup_x2_impl_int,
    [insn_dup2] = dup2_impl_int,
    [insn_dup2_x1] = dup2_x1_impl_int,
    [insn_dup2_x2] = dup2_x2_impl_int,
    [insn_faload] = faload_impl_int,
    [insn_i2b] = i2b_impl_int,
    [insn_i2c] = i2c_impl_int,
    [insn_i2d] = i2d_impl_int,
    [insn_i2f] = i2f_impl_int,
    [insn_i2l] = i2l_impl_int,
    [insn_i2s] = i2s_impl_int,
    [insn_iadd] = iadd_impl_int,
    [insn_iaload] = iaload_impl_int,
    [insn_iand] = iand_impl_int,
    [insn_iastore] = iastore_impl_int,
    [insn_idiv] = idiv_impl_int,
    [insn_imul] = imul_impl_int,
    [insn_ineg] = ineg_impl_int,
    [insn_ior] = ior_impl_int,
    [insn_irem] = irem_impl_int,
    [insn_ireturn] = ireturn_impl_int,
    [insn_ishl] = ishl_impl_int,
    [insn_ishr] = ishr_impl_int,
    [insn_isub] = isub_impl_int,
    [insn_iushr] = iushr_impl_int,
    [insn_ixor] = ixor_impl_int,
    [insn_l2d] = l2d_impl_int,
    [insn_l2f] = l2f_impl_int,
    [insn_l2i] = l2i_impl_int,
    [insn_ladd] = ladd_impl_int,
    [insn_laload] = laload_impl_int,
    [insn_land] = land_impl_int,
    [insn_lastore] = lastore_impl_int,
    [insn_lcmp] = lcmp_impl_int,
    [insn_ldiv] = ldiv_impl_int,
    [insn_lmul] = lmul_impl_int,
    [insn_lneg] = lneg_impl_int,
    [insn_lor] = lor_impl_int,
    [insn_lrem] = lrem_impl_int,
    [insn_lreturn] = lreturn_impl_int,
    [insn_lshl] = lshl_impl_int,
    [insn_lshr] = lshr_impl_int,
    [insn_lsub] = lsub_impl_int,
    [insn_lushr] = lushr_impl_int,
    [insn_lxor] = lxor_impl_int,
    [insn_monitorenter] = monitorenter_impl_int,
    [insn_monitorexit] = monitorexit_impl_int,
    [insn_pop] = pop_impl_int,
    [insn_pop2] = pop2_impl_int,
    [insn_return] = return_impl_int,
    [insn_saload] = saload_impl_int,
    [insn_sastore] = sastore_impl_int,
    [insn_swap] = swap_impl_int,
    [insn_anewarray] = anewarray_impl_int,
    [insn_checkcast] = checkcast_impl_int,
    [insn_getfield] = getfield_impl_int,
    [insn_getstatic] = getstatic_impl_int,
    [insn_instanceof] = instanceof_impl_int,
    [insn_invokedynamic] = invokedynamic_impl_int,
    [insn_new] = new_impl_int,
    [insn_putfield] = putfield_impl_int,
    [insn_putstatic] = putstatic_impl_int,
    [insn_invokevirtual] = invokevirtual_impl_int,
    [insn_invokespecial] = invokespecial_impl_int,
    [insn_invokestatic] = invokestatic_impl_int,
    [insn_ldc] = ldc_impl_int,
    [insn_ldc2_w] = ldc2_w_impl_int,
    [insn_dload] = dload_impl_int,
    [insn_fload] = fload_impl_int,
    [insn_iload] = iload_impl_int,
    [insn_lload] = lload_impl_int,
    [insn_istore] = istore_impl_int,
    [insn_lstore] = lstore_impl_int,
    [insn_aload] = aload_impl_int,
    [insn_astore] = astore_impl_int,
    [insn_goto] = goto_impl_int,
    [insn_if_acmpeq] = if_acmpeq_impl_int,
    [insn_if_acmpne] = if_acmpne_impl_int,
    [insn_if_icmpeq] = if_icmpeq_impl_int,
    [insn_if_icmpne] = if_icmpne_impl_int,
    [insn_if_icmplt] = if_icmplt_impl_int,
    [insn_if_icmpge] = if_icmpge_impl_int,
    [insn_if_icmpgt] = if_icmpgt_impl_int,
    [insn_if_icmple] = if_icmple_impl_int,
    [insn_ifeq] = ifeq_impl_int,
    [insn_ifne] = ifne_impl_int,
    [insn_iflt] = iflt_impl_int,
    [insn_ifge] = ifge_impl_int,
    [insn_ifgt] = ifgt_impl_int,
    [insn_ifle] = ifle_impl_int,
    [insn_ifnonnull] = ifnonnull_impl_int,
    [insn_ifnull] = ifnull_impl_int,
    [insn_iconst] = iconst_impl_int,
    [insn_dconst] = dconst_impl_int,
    [insn_fconst] = fconst_impl_int,
    [insn_lconst] = lconst_impl_int,
    [insn_iinc] = iinc_impl_int,
    [insn_invokeinterface] = invokeinterface_impl_int,
    [insn_multianewarray] = multianewarray_impl_int,
    [insn_newarray] = newarray_impl_int,
    [insn_tableswitch] = tableswitch_impl_int,
    [insn_lookupswitch] = lookupswitch_impl_int,
    [insn_anewarray_resolved] = anewarray_resolved_impl_int,
    [insn_checkcast_resolved] = checkcast_resolved_impl_int,
    [insn_instanceof_resolved] = instanceof_resolved_impl_int,
    [insn_new_resolved] = new_resolved_impl_int,
    [insn_invokevtable_monomorphic] = invokeitable_vtable_monomorphic_impl_int,
    [insn_invokevtable_polymorphic] = invokevtable_polymorphic_impl_int,
    [insn_invokeitable_monomorphic] = invokeitable_vtable_monomorphic_impl_int,
    [insn_invokeitable_polymorphic] = invokeitable_polymorphic_impl_int,
    [insn_invokespecial_resolved] = invokespecial_resolved_impl_int,
    [insn_invokestatic_resolved] = invokestatic_resolved_impl_int,
    [insn_invokecallsite] = invokecallsite_impl_int,
    [insn_invokesigpoly] = invokesigpoly_impl_int,
    [insn_getfield_B] = getfield_B_impl_int,
    [insn_getfield_C] = getfield_C_impl_int,
    [insn_getfield_S] = getfield_S_impl_int,
    [insn_getfield_I] = getfield_I_impl_int,
    [insn_getfield_J] = getfield_J_impl_int,
    [insn_getfield_F] = getfield_F_impl_int,
    [insn_getfield_D] = getfield_D_impl_int,
    [insn_getfield_Z] = getfield_Z_impl_int,
    [insn_getfield_L] = getfield_L_impl_int,
    [insn_putfield_B] = putfield_B_impl_int,
    [insn_putfield_C] = putfield_C_impl_int,
    [insn_putfield_S] = putfield_S_impl_int,
    [insn_putfield_I] = putfield_I_impl_int,
    [insn_putfield_J] = putfield_J_impl_int,
    [insn_putfield_Z] = putfield_Z_impl_int,
    [insn_putfield_L] = putfield_L_impl_int,
    [insn_getstatic_B] = getstatic_B_impl_int,
    [insn_getstatic_C] = getstatic_C_impl_int,
    [insn_getstatic_S] = getstatic_S_impl_int,
    [insn_getstatic_I] = getstatic_I_impl_int,
    [insn_getstatic_J] = getstatic_J_impl_int,
    [insn_getstatic_F] = getstatic_F_impl_int,
    [insn_getstatic_D] = getstatic_D_impl_int,
    [insn_getstatic_Z] = getstatic_Z_impl_int,
    [insn_getstatic_L] = getstatic_L_impl_int,
    [insn_putstatic_B] = putstatic_B_impl_int,
    [insn_putstatic_C] = putstatic_C_impl_int,
    [insn_putstatic_S] = putstatic_S_impl_int,
    [insn_putstatic_I] = putstatic_I_impl_int,
    [insn_putstatic_J] = putstatic_J_impl_int,
    [insn_putstatic_Z] = putstatic_Z_impl_int,
    [insn_putstatic_L] = putstatic_L_impl_int,
};

PAGE_ALIGN static s64 (*jmp_table_float[MAX_INSN_KIND])(ARGS_VOID) = {
    [insn_nop] = nop_impl_float,
    [insn_aconst_null] = aconst_null_impl_float,
    [insn_dup] = dup_impl_float,
    [insn_dup_x1] = dup_x1_impl_float,
    [insn_dup_x2] = dup_x2_impl_float,
    [insn_dup2] = dup2_impl_float,
    [insn_dup2_x1] = dup2_x1_impl_float,
    [insn_dup2_x2] = dup2_x2_impl_float,
    [insn_f2d] = f2d_impl_float,
    [insn_f2i] = f2i_impl_float,
    [insn_f2l] = f2l_impl_float,
    [insn_fadd] = fadd_impl_float,
    [insn_fastore] = fastore_impl_float,
    [insn_fcmpg] = fcmpg_impl_float,
    [insn_fcmpl] = fcmpl_impl_float,
    [insn_fdiv] = fdiv_impl_float,
    [insn_fmul] = fmul_impl_float,
    [insn_fneg] = fneg_impl_float,
    [insn_freturn] = freturn_impl_float,
    [insn_fsub] = fsub_impl_float,
    [insn_pop] = pop_impl_float,
    [insn_pop2] = pop2_impl_float,
    [insn_return] = return_impl_float,
    [insn_swap] = swap_impl_float,
    [insn_getstatic] = getstatic_impl_float,
    [insn_invokedynamic] = invokedynamic_impl_float,
    [insn_new] = new_impl_float,
    [insn_putfield] = putfield_impl_float,
    [insn_putstatic] = putstatic_impl_float,
    [insn_invokevirtual] = invokevirtual_impl_float,
    [insn_invokespecial] = invokespecial_impl_float,
    [insn_invokestatic] = invokestatic_impl_float,
    [insn_ldc] = ldc_impl_float,
    [insn_ldc2_w] = ldc2_w_impl_float,
    [insn_dload] = dload_impl_float,
    [insn_fload] = fload_impl_float,
    [insn_iload] = iload_impl_float,
    [insn_lload] = lload_impl_float,
    [insn_fstore] = fstore_impl_float,
    [insn_aload] = aload_impl_float,
    [insn_goto] = goto_impl_float,
    [insn_iconst] = iconst_impl_float,
    [insn_dconst] = dconst_impl_float,
    [insn_fconst] = fconst_impl_float,
    [insn_lconst] = lconst_impl_float,
    [insn_iinc] = iinc_impl_float,
    [insn_invokeinterface] = invokeinterface_impl_float,
    [insn_new_resolved] = new_resolved_impl_float,
    [insn_invokevtable_monomorphic] = invokeitable_vtable_monomorphic_impl_float,
    [insn_invokevtable_polymorphic] = invokevtable_polymorphic_impl_float,
    [insn_invokeitable_monomorphic] = invokeitable_vtable_monomorphic_impl_float,
    [insn_invokeitable_polymorphic] = invokeitable_polymorphic_impl_float,
    [insn_invokespecial_resolved] = invokespecial_resolved_impl_float,
    [insn_invokestatic_resolved] = invokestatic_resolved_impl_float,
    [insn_invokecallsite] = invokecallsite_impl_float,
    [insn_invokesigpoly] = invokesigpoly_impl_float,
    [insn_putfield_F] = putfield_F_impl_float,
    [insn_getstatic_B] = getstatic_B_impl_float,
    [insn_getstatic_C] = getstatic_C_impl_float,
    [insn_getstatic_S] = getstatic_S_impl_float,
    [insn_getstatic_I] = getstatic_I_impl_float,
    [insn_getstatic_J] = getstatic_J_impl_float,
    [insn_getstatic_F] = getstatic_F_impl_float,
    [insn_getstatic_D] = getstatic_D_impl_float,
    [insn_getstatic_Z] = getstatic_Z_impl_float,
    [insn_getstatic_L] = getstatic_L_impl_float,
    [insn_putstatic_F] = putstatic_F_impl_float,
    [insn_frem] = frem_impl_float,
    [insn_pow] = pow_impl_float,
    [insn_sqrt] = sqrt_impl_float};