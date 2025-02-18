// Interpreter version 2, based on tail calls.
//
// There are implementations for TOS types of int/long/reference, float, and double. Some instruction/TOS
// combinations are impossible (rejected by the verifier) and are thus omitted. The execution of "stack polymorphic"
// instructions like pop will consult the TOS stack type that the instruction is annotated with (during analysis),
// before loading the value from the stack and using the appropriate jump table.
//
// The general signature is (thread, frame, insns, pc, sp, tos). At appropriate points (whenever the frame might be
// read back, e.g. for GC purposes, or when interrupting), the TOS value and program counter are written to the stack.
//
// In tail call mode, JMP_VOID etc. macros select the correct bytecode to jump to. In asm this will compile down
// to a single branch instruction. Otherwise, they return the appropriate jump table value (kind * 4 + tos_type) for
// the switch-loop to jump to.
//
// In non-tail-call mode, important for in-browser performance, the generator loosely controls inlining by assuming
// that inlining will occur, then preventing the inlining of certain functions through a funcref
// (__interpreter_intrinsic_force_outline). This is done because the WASM runtime often aggressively inlines functions,
// shooting itself in the foot by spilling the most important values (e.g. "insns", "sp") out of registers. The live
// ranges of these variables are simply too large for typical JIT register allocation algorithms to work well.
//
// Failure to inline is verified by the postprocessor, which expects no user-stack allocation to occur.
//
// JS POSTPROCESSING
//
// To force the tail calls to use a funcref table instead of a normal call_indirect to an untyped table, we use
// intrinsics that are lowered in a separate post-processing pass. This reads the jump tables and builds a single
// funcref table, in which kind * 4 + tos_before is the index of the implementation for bytecode "kind" with starting
// tos-type "tos_before". Non-existent entries are replaced with nop_impl_*, so that the funcref table is non-nullable
// and the only check that occurs is a bounds check against the table size.
//
// The tail-call implementation is only fast on Firefox. Until the other JS engines get their shit together, to support
// decent performance on Safari and Chrome, we use the while loop/switch statement.

#include "arrays.h"
#include "bjvm.h"
#include "classfile.h"
#include "util.h"

#include <debugger.h>
#include <math.h>
#include <tgmath.h>

#include <exceptions.h>
#include <linkage.h>
#include <monitors.h>

#include <instrumentation.h>
#include <roundrobin_scheduler.h>
#include <sys/time.h>

// Define this macro to print debug dumps upon the execution of every interpreter instruction. Useful for debugging.
#define DEBUG_CHECK() ;
#if 0
#undef DEBUG_CHECK
#define DEBUG_CHECK()                                                                                                  \
  SPILL_VOID                                                                                                           \
  cp_method *m = frame->method;                                                                                        \
  printf("Calling method %.*s, descriptor %.*s, on class %.*s; %d\n", fmt_slice(m->name),                              \
         fmt_slice(m->unparsed_descriptor), fmt_slice(m->my_class->name), __LINE__);                                   \
  heap_string s = insn_to_string(insn, pc);                                                                            \
  printf("Insn kind: %.*s\n", fmt_slice(s));                                                                           \
  free_heap_str(s);                                                                                                    \
  dump_frame(stdout, frame);
#endif

// If true, use a sequence of tail calls rather than computed goto. We try to make the code reasonably generic to handle
// both cases efficiently.
#ifdef EMSCRIPTEN
#define DO_TAILS 0 // not profitable on web
#else
#define DO_TAILS 1
#endif

#define MUSTTAIL // to allow compilation without -mtail-call

#if DO_TAILS
#define sp sp_
#define pc pc_
#define tos tos_

#define ARGS_BASE                                                                                                      \
  [[maybe_unused]] vm_thread *thread, [[maybe_unused]] stack_frame *frame, [[maybe_unused]] bytecode_insn *insns,      \
      [[maybe_unused]] u16 pc_, [[maybe_unused]] stack_value *sp_

#define ARGS_VOID ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_INT ARGS_BASE, [[maybe_unused]] s64 tos_, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_DOUBLE ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double tos_
#define ARGS_FLOAT ARGS_BASE, [[maybe_unused]] s64 arg_1, [[maybe_unused]] float tos_, [[maybe_unused]] double arg_3

// Indicates that the following return must be a tail call. Supported by modern clang and GCC (but GCC support seems
// somewhat buggy..., so we use -foptimize-sibling-calls and disable ASAN and pray.)
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
    s64 a_undef = 0;                                                                                                       \
    float b_undef = 0;                                                                                                     \
    double c_undef = 0;                                                                                                    \
    MUSTTAIL return (expr);                                                                                            \
  } while (0);
#else
#define WITH_UNDEF(expr)                                                                                               \
do {                                                                                                                 \
s64 a_undef;                                                                                                       \
float b_undef;                                                                                                     \
double c_undef;                                                                                                    \
asm volatile ("" : "=r"(a_undef), "=r"(b_undef), "=r"(c_undef)); \
MUSTTAIL return (expr);                                                                                            \
} while (0);
#endif

#ifdef EMSCRIPTEN

#define JMP_INT(tos)                                                                                                   \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_int(thread, frame, insns, pc, sp, tos, b_undef, c_undef);)
#define JMP_FLOAT(tos)                                                                                                 \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_float(thread, frame, insns, pc, sp, a_undef, tos, c_undef);)
#define JMP_DOUBLE(tos)                                                                                                \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_double(thread, frame, insns, pc, sp, a_undef, b_undef, tos);)

#define NEXT_INT(tos)                                                                                                  \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_int(thread, frame, insns + 1, pc + 1, sp, (int64_t)tos,      \
                                                              b_undef, c_undef);)
#define NEXT_FLOAT(tos)                                                                                                \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_float(thread, frame, insns + 1, pc + 1, sp, a_undef, tos,    \
                                                                c_undef);)
#define NEXT_DOUBLE(tos)                                                                                               \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_double(thread, frame, insns + 1, pc + 1, sp, a_undef,        \
                                                                 b_undef, tos);)

#define JMP_VOID                                                                                                       \
  WITH_UNDEF(                                                                                                          \
      MUSTTAIL return __interpreter_intrinsic_next_void(thread, frame, insns, pc, sp, a_undef, b_undef, c_undef);)
// Jump to the instruction at pc + 1, with nothing in the top of the stack.
#define NEXT_VOID                                                                                                      \
  WITH_UNDEF(MUSTTAIL return __interpreter_intrinsic_next_void(thread, frame, insns + 1, pc + 1, sp, a_undef, b_undef, \
                                                               c_undef);)

// Go to the next instruction, but where we don't know a priori the top-of-stack type for that instruction, and must
// look it up from the analyzed tos type.
#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  stack_value __tos = (tos);                                                                                           \
  MUSTTAIL return __interpreter_intrinsic_next_polymorphic(thread, frame, insns + 1, pc + 1, sp, __tos.l, __tos.f,     \
                                                           __tos.d);

// Go to the instruction at pc, but where we don't know a priori the top-of-stack type for that instruction, and must
// look it up from the analyzed tos type.
#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  stack_value __tos = (tos);                                                                                           \
  MUSTTAIL return __interpreter_intrinsic_next_polymorphic(thread, frame, insns, pc, sp, __tos.l, __tos.f, __tos.d);

#else // !ifdef EMSCRIPTEN
#define ADVANCE_INT_(tos, insn_off)                                                                                    \
  int k = insns[insn_off].kind;                                                                                        \
  s64 __tos = (s64)(tos);                                                                                              \
  WITH_UNDEF(jmp_table_int[k](thread, frame, insns + insn_off, pc + insn_off, sp, (s64)__tos, b_undef, c_undef));
#define ADVANCE_FLOAT_(tos, insn_off)                                                                                  \
  int k = insns[insn_off].kind;                                                                                        \
  float __tos = (tos);                                                                                                 \
  WITH_UNDEF(jmp_table_float[k](thread, frame, insns + insn_off, pc + insn_off, sp, a_undef, __tos, c_undef));
#define ADVANCE_DOUBLE_(tos, insn_off)                                                                                 \
  int k = insns[insn_off].kind;                                                                                      \
  double __tos = (tos);                                                                                              \
  WITH_UNDEF(jmp_table_double[k](thread, frame, insns + insn_off, pc + insn_off, sp, a_undef, b_undef, __tos));      \

#define JMP_INT(tos) ADVANCE_INT_(tos, 0)
#define JMP_FLOAT(tos) ADVANCE_FLOAT_(tos, 0)
#define JMP_DOUBLE(tos) ADVANCE_DOUBLE_(tos, 0)

#define NEXT_INT(tos) ADVANCE_INT_(tos, 1)
#define NEXT_FLOAT(tos) ADVANCE_FLOAT_(tos, 1)
#define NEXT_DOUBLE(tos) ADVANCE_DOUBLE_(tos, 1)

// Jump to the instruction at pc, with nothing in the top of the stack. This does NOT imply that sp = 0, only that
// all stack values are in memory (rather than in a register)
#define JMP_VOID WITH_UNDEF(jmp_table_void[insns[0].kind](thread, frame, insns, pc, sp, a_undef, b_undef, c_undef));
// Jump to the instruction at pc + 1, with nothing in the top of the stack.
#define NEXT_VOID                                                                                                      \
  WITH_UNDEF(jmp_table_void[insns[1].kind](thread, frame, insns + 1, pc + 1, sp, a_undef, b_undef, c_undef));

#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  stack_value __tos = (tos);                                                                                           \
  MUSTTAIL return bytecode_tables[insn->tos_after][insns[1].kind](thread, frame, insns + 1, pc + 1, sp, __tos.l,       \
                                                                  __tos.f, __tos.d);

#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  stack_value __tos = (tos);                                                                                           \
  MUSTTAIL return bytecode_tables[insn->tos_before][insns[0].kind](thread, frame, insns, pc, sp, __tos.l, __tos.f,     \
                                                                   __tos.d);
#endif // ifdef EMSCRIPTEN
#else  // !DO_TAILS

#define sp (*sp_)
#define pc (*pc_)
#define tos (*tos_)

#define ARGS_BASE                                                                                                      \
  [[maybe_unused]] vm_thread *thread, [[maybe_unused]] stack_frame *frame, [[maybe_unused]] bytecode_insn *insns,      \
      [[maybe_unused]] u16 *pc_, [[maybe_unused]] stack_value **sp_

#define ARGS_VOID ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *arg_3
#define ARGS_INT ARGS_BASE, [[maybe_unused]] s64 *tos_, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *arg_3
#define ARGS_DOUBLE ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *arg_2, [[maybe_unused]] double *tos_
#define ARGS_FLOAT ARGS_BASE, [[maybe_unused]] s64 *arg_1, [[maybe_unused]] float *tos_, [[maybe_unused]] double *arg_3

bool *tos_; // used by JMP_* and NEXT_* to detect what type of function we're in.

s64 *arg_1; // not actually used, just to silence compiler errors in branches not taken at compile time
float *arg_2;
double *arg_3;

#define JMP_VOID return 4 * insn->kind;
#define NEXT_VOID                                                                                                      \
  pc++;                                                                                                                \
  return 4 * (insn + 1)->kind;

#define SET_INT(tos) *(_Generic((*tos_), s64: true, default: false) ? (s64 *)tos_ : arg_1) = (s64)tos;
#define SET_FLOAT(tos) *(_Generic((*tos_), float: true, default: false) ? (float *)tos_ : arg_2) = tos;
#define SET_DOUBLE(tos) *(_Generic((*tos_), double: true, default: false) ? (double *)tos_ : arg_3) = tos;

#define JMP_INT(tos)                                                                                                   \
  SET_INT(tos);                                                                                                        \
  return 4 * insn->kind + TOS_INT;
#define NEXT_INT(tos)                                                                                                  \
  SET_INT(tos);                                                                                                        \
  pc++;                                                                                                                \
  return 4 * (insn + 1)->kind + TOS_INT;

#define JMP_FLOAT(tos)                                                                                                 \
  SET_FLOAT(tos);                                                                                                      \
  return 4 * insn->kind + TOS_FLOAT;
#define NEXT_FLOAT(tos)                                                                                                \
  SET_FLOAT(tos);                                                                                                      \
  pc++;                                                                                                                \
  return 4 * (insn + 1)->kind + TOS_FLOAT;

#define JMP_DOUBLE(tos)                                                                                                \
  SET_DOUBLE(tos);                                                                                                     \
  return 4 * insn->kind + TOS_DOUBLE;
#define NEXT_DOUBLE(tos)                                                                                               \
  SET_DOUBLE(tos);                                                                                                     \
  pc++;                                                                                                                \
  return 4 * (insn + 1)->kind + TOS_DOUBLE;

#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  SET_FLOAT((tos).f);                                                                                                  \
  SET_INT((tos).l);                                                                                                    \
  SET_DOUBLE((tos).d);                                                                                                 \
  return 4 * insn->kind + insn->tos_before;
#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  SET_FLOAT((tos).f);                                                                                                  \
  SET_INT((tos).l);                                                                                                    \
  SET_DOUBLE((tos).d);                                                                                                 \
  pc++;                                                                                                                \
  return 4 * (insn + 1)->kind + insn->tos_after;

#endif // DO_TAILS

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

extern int64_t __interpreter_intrinsic_next_polymorphic(ARGS_VOID);
extern int64_t __interpreter_intrinsic_next_void(ARGS_VOID);
extern int64_t __interpreter_intrinsic_next_int(ARGS_INT);
extern int64_t __interpreter_intrinsic_next_double(ARGS_DOUBLE);
extern int64_t __interpreter_intrinsic_next_float(ARGS_FLOAT);

// Call the bytecode implementation through a funcref table. This prevents inlining by the runtime and regalloc from
// dying.
extern int64_t __interpreter_intrinsic_notco_call_outlined(ARGS_VOID, int index);

// These point into .rodata and are used to recover the function pointers for the funcref jump tables.
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

// Spill all the information currently in locals/registers to the frame (required at safepoints and when interrupting)
#define SPILL(tos)                                                                                                     \
  frame->plain.program_counter = pc;                                                                                   \
  *(sp - 1) = _Generic((tos),                                                                                          \
      s64: (stack_value){.l = (s64)tos},                                                                               \
      float: (stack_value){.f = (float)tos},                                                                           \
      double: (stack_value){.d = (double)tos},                                                                         \
      obj_header *: (stack_value){.obj = (obj_header *)(uintptr_t)tos} /* shut up float branch */                      \
  );
// Same as SPILL(tos), but when no top-of-stack value is available
#define SPILL_VOID frame->plain.program_counter = pc;

// Reload the top of stack type -- used after an instruction which may have instigated a GC. RELOAD_VOID is not
// required.
#define RELOAD(tos)                                                                                                    \
  tos = _Generic(tos, s64: (*(sp - 1)).l, float: (*(sp - 1)).f, double: (*(sp - 1)).d, obj_header *: (*(sp - 1)).obj);

// For a bytecode that takes no arguments, given an implementation for the int TOS type, generate adapter funcsptions
// which push the current TOS value onto the stack and then call the void TOS implementation.
#define FORWARD_TO_NULLARY(which)                                                                                      \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    *(sp - 1) = (stack_value){.l = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc_, sp_, tos_, arg_2, arg_3);                             \
  }                                                                                                                    \
                                                                                                                       \
  static s64 which##_impl_float(ARGS_FLOAT) {                                                                          \
    *(sp - 1) = (stack_value){.f = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc_, sp_, arg_1, tos_, arg_3);                             \
  }                                                                                                                    \
                                                                                                                       \
  static s64 which##_impl_double(ARGS_DOUBLE) {                                                                        \
    *(sp - 1) = (stack_value){.d = tos};                                                                               \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc_, sp_, arg_1, arg_2, tos_);                             \
  }

// Emit a null pointer exception when the given expression is null.
#define NPE_ON_NULL(expr) \
  if (unlikely(!expr)) { \
    SPILL_VOID \
    raise_null_pointer_exception(thread); \
    return 0; \
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
  arguments(vm_thread *thread; bytecode_insn *inst; plain_frame *frame; stack_value *sp_;),
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
              arguments(vm_thread *thread; bytecode_insn *inst; plain_frame *frame; stack_value *sp_;),
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
  CONT_INVOKE,
  CONT_INVOKESIGPOLY, // tos must be reloaded
  CONT_RUN_NATIVE,
  CONT_MONITOR_ENTER,
  CONT_RESUME_INSN,
  CONT_DEBUGGER_PAUSE
} continuation_point;

typedef struct {
  void *wakeup;
  continuation_point pnt;

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
  struct async_stack *stk = thread->async_stack;

  size_t new_capacity = stk->max_height + stk->max_height / 2;
  if (new_capacity == 0)
    new_capacity = 4;

  stk = realloc(thread->async_stack, sizeof(struct async_stack) + new_capacity * sizeof(continuation_frame));
  ;
  if (unlikely(!stk)) {
    thread->current_exception = thread->stack_overflow_error;
    return -1;
  }
  thread->async_stack = stk;

  stk->max_height = new_capacity;
  return 0;
}

static continuation_frame *async_stack_push(vm_thread *thread) {
#define stk thread->async_stack

  if (unlikely(stk->height == stk->max_height)) {
    if ((grow_async_stack(thread)) < 0) {
      thread->current_exception = thread->stack_overflow_error;
      return nullptr;
    }
  }

  return &thread->async_stack->frames[thread->async_stack->height++];

#undef stk
}

static continuation_frame *async_stack_pop(vm_thread *thread) {
  DCHECK(thread->async_stack->height > 0);
  return &thread->async_stack->frames[--thread->async_stack->height];
}

static void *async_stack_top(vm_thread *thread) {
  assert(thread->async_stack->height > 0);
  return thread->async_stack->frames[thread->async_stack->height - 1].wakeup;
}

/** FUEL CHECKING */

__attribute__((noinline)) static bool refuel_check(vm_thread *thread) {
  const int REFUEL = 50000;
  thread->fuel = REFUEL;

  if (thread->synchronous_depth) // we're in a synchronous call, don't try to yield
    return false;

  // Get the current time in milliseconds since 1970
  struct timeval tv;
  gettimeofday(&tv, NULL);
  u64 now = tv.tv_sec * 1000000 + tv.tv_usec;
  if (thread->yield_at_time != 0 && now >= thread->yield_at_time) {
    arrlast(thread->frames)->is_async_suspended = true;

    continuation_frame *cont = async_stack_push(thread);
    // Provide a way for us to free the wakeup info. There will never be multiple refuel checks in flight within a
    // single thread.
    rr_wakeup_info *wakeup = thread->refuel_wakeup_info ? thread->refuel_wakeup_info
                                                        : (thread->refuel_wakeup_info = malloc(sizeof(rr_wakeup_info)));
    wakeup->kind = RR_WAKEUP_YIELDING;
    *cont = (continuation_frame){.pnt = CONT_RESUME_INSN, .wakeup = (void *)wakeup};
    return true;
  }
  return false;
}

static bool fuel_check_impl(vm_thread *thread) {
  if (unlikely(thread->fuel-- == 0)) {
    return refuel_check(thread);
  }
  return false;
}

#define FUEL_CHECK                                                                                                     \
  if (fuel_check_impl(thread)) {                                                                                       \
    SPILL(tos);                                                                                                        \
    return 0;                                                                                                          \
  }
#define FUEL_CHECK_VOID                                                                                                \
  if (fuel_check_impl(thread)) {                                                                                       \
    SPILL_VOID return 0;                                                                                               \
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
  inst->kind = getstatic_putstatic_resolved_kind(putstatic, field_to_kind(self->field_info->parsed_descriptor));

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
      *cont = (continuation_frame){.pnt = CONT_RESOLVE, .ctx.resolve_insn = ctx};                                      \
      return 0;                                                                                                        \
    }                                                                                                                  \
  } while (0)

__attribute__((noinline)) static s64 getstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID

  TryResolve(thread, insn, &frame->plain, sp);

  if (unlikely(thread->current_exception)) {
    return 0;
  }

  JMP_VOID // we rewrote this instruction to a resolved form, so jump to that implementation
}
FORWARD_TO_NULLARY(getstatic)

// Never actually directly called -- we just do it this way because it's easier and we might as well merge code paths
// for different TOS types.
__attribute__((noinline)) static s64 putstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, &frame->plain, sp);
  if (thread->current_exception) {
    return 0;
  }
  STACK_POLYMORPHIC_JMP(*(sp - 1));
}
FORWARD_TO_NULLARY(putstatic)

force_inline static s64 getstatic_L_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(obj_header **)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_L)

force_inline static s64 getstatic_F_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_FLOAT(*(float *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_F)

force_inline static s64 getstatic_D_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_DOUBLE(*(double *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_D)

force_inline static s64 getstatic_J_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(s64 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_J)

force_inline static s64 getstatic_I_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT(*(int *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_I)

force_inline static s64 getstatic_S_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s16 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_S)

force_inline static s64 getstatic_C_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (u16 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_C)

force_inline static s64 getstatic_B_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s8 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_B)

force_inline static s64 getstatic_Z_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  DCHECK(insn->ic, "Static field location not found");
  sp++;
  NEXT_INT((s64) * (s8 *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_Z)

static s64 putstatic_B_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(s8 *)insn->ic = (s8)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_C_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(u16 *)insn->ic = (u16)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_S_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(s16 *)insn->ic = (s16)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_I_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(int *)insn->ic = (int)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_J_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(s64 *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_F_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  *(float *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_D_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  *(double *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_L_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  *(obj_header **)insn->ic = (obj_header *)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 putstatic_Z_impl_int(ARGS_INT) {
  DEBUG_CHECK();
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

  inst->kind = getfield_putfield_resolved_kind(putfield, field_to_kind(field_info->parsed_descriptor));
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

  TryResolve(thread, insn, &frame->plain, sp);

  RELOAD(tos)
  JMP_INT(tos)
}

force_inline static s64 putfield_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, &frame->plain, sp);
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
  force_inline static s64 which##_impl_int(ARGS_INT) {                                                                 \
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
  force_inline static s64 which##_impl_int(ARGS_INT) {                                                                 \
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
  force_inline static s64 f##which##_impl_float(ARGS_FLOAT) {                                                          \
    DEBUG_CHECK();                                                                                                     \
    float a = (sp - 2)->f, b = tos;                                                                                    \
    out_float result = eval;                                                                                           \
    sp--;                                                                                                              \
    NEXT1(result)                                                                                                      \
  }                                                                                                                    \
  force_inline static s64 d##which##_impl_double(ARGS_DOUBLE) {                                                        \
    DEBUG_CHECK();                                                                                                     \
    double a = (sp - 2)->d, b = tos;                                                                                   \
    out_double result = eval;                                                                                          \
    sp--;                                                                                                              \
    NEXT2(result)                                                                                                      \
  }

#define FLOAT_UN_OP(which, eval, out, NEXT)                                                                            \
  force_inline static s64 which##_impl_float(ARGS_FLOAT) {                                                             \
    DEBUG_CHECK();                                                                                                     \
    float a = tos;                                                                                                     \
    out result = eval;                                                                                                 \
    NEXT(result)                                                                                                       \
  }

#define DOUBLE_UN_OP(which, eval, out, NEXT)                                                                           \
  force_inline static s64 which##_impl_double(ARGS_DOUBLE) {                                                           \
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
    return 0;
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
    return 0;
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
    return 0;
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
    return 0;
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
    int length = ArrayLength(array);                                                                                  \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL_VOID;                                                                                                      \
      raise_array_index_oob_exception(thread, index, length);                                                          \
      return 0;                                                                                                        \
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
    NPE_ON_NULL(array);                                                                                                            \
    int length = ArrayLength(array);                                                                                  \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL_VOID;                                                                                                      \
      raise_array_index_oob_exception(thread, index, length);                                                          \
      return 0;                                                                                                        \
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
    return 0;
  }
  // Instanceof check against the component type
  if (value && !instanceof(value->descriptor, array->descriptor->one_fewer_dim)) {
    SPILL(tos);
    raise_array_store_exception(thread, value->descriptor->name);
    return 0;
  }
  ReferenceArrayStore(array, index, value);
  sp -= 3;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** Control-flow instructions (returns, jumps, branches) */

force_inline static s64 return_impl_void(ARGS_VOID) {
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
  s32 delta = (s32)insn->index - (s32)pc;
  pc = insn->index;
  insns += delta;
  JMP_VOID
}

static s64 goto_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  FUEL_CHECK
  s32 delta = (s32)insn->index - (s32)pc;
  pc = insn->index;
  insns += delta;
  JMP_DOUBLE(tos)
}

static s64 goto_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  FUEL_CHECK
  s32 delta = (s32)insn->index - (s32)pc;
  pc = insn->index;
  insns += delta;
  JMP_FLOAT(tos)
}

static s64 goto_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  FUEL_CHECK
  s32 delta = (s32)insn->index - (s32)pc;
  pc = insn->index;
  insns += delta;
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
    pc = insn->tableswitch->default_target - 1;
    insns += delta;
  } else {
    int delta = (offsets[index - low] - 1) - pc;
    pc = offsets[index - low] - 1;
    insns += delta;
  }
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static s64 lookupswitch_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  struct bc_lookupswitch_data data = *insn->lookupswitch;

  s32 key = (s32)tos;
  s32 *keys = data.keys;
  s32 *offsets = data.targets;
  s32 n = data.keys_count;
  s32 default_target = data.default_target;
  // TODO replace this with a binary search

  for (int i = 0; i < n; i++) {
    if (keys[i] == key) {
      int delta = (offsets[i] - 1) - pc;
      pc = offsets[i] - 1;
      insns += delta;
      sp--;
      STACK_POLYMORPHIC_NEXT(*(sp - 1));
    }
  }
  int delta = (default_target - 1) - pc;
  pc = default_target - 1;
  insns += delta;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

#define MAKE_INT_BRANCH_AGAINST_0(which, op)                                                                           \
  static s64 which##_impl_int(ARGS_INT) {                                                                              \
    DEBUG_CHECK();                                                                                                     \
    FUEL_CHECK                                                                                                         \
    u16 old_pc = pc;                                                                                                   \
    pc = ((s32)tos op 0) ? insn->index : (u32)(pc + 1);                                                                \
    insns += (s32)pc - (s32)old_pc;                                                                                    \
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
    u16 old_pc = pc;                                                                                                   \
    pc = a op b ? insn->index : (u32)(pc + 1);                                                                         \
    insns += (s32)pc - (s32)old_pc;                                                                                    \
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
  int old_pc = pc;
  pc = a == b ? (insn->index - 1) : pc;
  insns += pc - old_pc;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}

static s64 if_acmpne_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  FUEL_CHECK
  obj_header *a = (sp - 2)->obj, *b = (obj_header *)tos;
  int old_pc = pc;
  pc = a != b ? (insn->index - 1) : pc;
  insns += pc - old_pc;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}

/** Monitors */

// Push the necessary continuation frame for a monitorenter instruction that is contended.
void push_async_monitor_enter(vm_thread *thread, stack_frame *frame, monitor_acquire_t *ctx) {
  continuation_frame *cont = async_stack_push(thread);
  frame->is_async_suspended = true;
  *cont = (continuation_frame){.pnt = CONT_MONITOR_ENTER, .ctx.acquire_monitor = *ctx};
}

static s64 monitorenter_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  NPE_ON_NULL(tos);

  do {
    monitor_acquire_t ctx = {.args = {thread, (obj_header *)tos}};
    future_t fut = monitor_acquire(&ctx);
    if (unlikely(thread->current_exception)) { // oom
      return 0;
    }
    if (fut.status == FUTURE_NOT_READY) { // monitor is contended
      push_async_monitor_enter(thread, frame, &ctx);
      return 0;
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
  assert(!thread->current_exception && "monitor_release raised an exception");

  if (unlikely(result)) {
    SPILL_VOID
    raise_illegal_monitor_state_exception(thread);
    return 0;
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

force_inline static s64 new_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID

  cp_class_info *info = &insn->cp->class_info;
  int error = resolve_class(thread, info);
  if (error)
    return 0;

  TryResolve(thread, insn, &frame->plain, sp);
  JMP_VOID
}
FORWARD_TO_NULLARY(new)

force_inline static s64 new_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  obj_header *obj = new_object(thread, insn->classdesc);
  if (!obj)
    return 0;

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
    return 0;
  }
  obj_header *array = CreatePrimitiveArray1D(thread, insn->array_type, count);
  if (unlikely(!array)) {
    return 0; // oom
  }
  NEXT_INT(array)
}

static s64 anewarray_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL_VOID
  cp_class_info *info = &insn->cp->class_info;
  if (resolve_class(thread, info)) {
    return 0;
  }
  DCHECK(info->classdesc);
  if (link_class(thread, info->classdesc)) {
    return 0;
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
    return 0;
  }
  obj_header *array = CreateObjectArray1D(thread, insn->classdesc, count);
  if (array) {
    NEXT_INT(array)
  }
  return 0; // oom
}

static s64 multianewarray_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  SPILL(tos)
  u16 temp_sp = sp - frame_stack(frame);
  if (multianewarray(thread, &frame->plain, insn->multianewarray, &temp_sp))
    return 0;

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
    if (utf8_equals(method->name, "cos")) {
      inst->kind = insn_cos;
      return 1;
    }
    if (utf8_equals(method->name, "sin")) {
      inst->kind = insn_sin;
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

  ASYNC_END(0);
}

__attribute__((noinline)) static s64 invokestatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  SPILL_VOID
  TryResolve(thread, insn, &frame->plain, sp);
  if (thread->current_exception)
    return 0;

  if (intrinsify(insn)) {
    STACK_POLYMORPHIC_JMP(*(sp - 1));
  }

  JMP_VOID
}
FORWARD_TO_NULLARY(invokestatic)

static u8
attempt_invoke(vm_thread *thread, stack_frame *invoked_frame, stack_frame *outer_frame, u8 argc, bool returns,
               stack_value *result) {
  future_t fut;
  *result = interpret_2(&fut, thread, invoked_frame);
  if (unlikely(fut.status == FUTURE_NOT_READY)) {
    DCHECK(invoked_frame->is_async_suspended);
    continuation_frame *cont = async_stack_push(thread);
    *cont = (continuation_frame){.pnt = CONT_INVOKE,
                                 .wakeup = fut.wakeup,
                                 .ctx.interp_call = {.frame = invoked_frame, .argc = argc, .returns = returns}};
    outer_frame->is_async_suspended = true;
    return 1;
  }

  return 0;
}

#define AttemptInvoke(thread, invoked_frame, argc, returns)                                                            \
  ({                                                                                                                   \
    stack_value result;                                                                                                \
    if (unlikely(attempt_invoke(thread, invoked_frame, frame, argc, returns, &result))) {                              \
      return 0;                                                                                                        \
    }                                                                                                                  \
    result;                                                                                                            \
  })

force_inline static s64 invokestatic_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method *method = insn->ic;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  stack_frame *invoked_frame;
  SPILL_VOID
  if (method->is_signature_polymorphic) {
    invoked_frame = push_native_frame(thread, method, insn->cp->methodref.descriptor, sp - insn->args, insn->args);
  } else {
    invoked_frame = push_frame(thread, method, sp - insn->args, insn->args);
  }
  if (unlikely(!invoked_frame)) {
    return 0;
  }

  stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

  if (thread->current_exception) {
    return 0;
  }

  if (returns) {
    *(sp - insn->args) = result;
  }

  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(invokestatic_resolved)

__attribute__((noinline)) static s64 invokevirtual_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *target = (sp - argc)->obj;

  SPILL_VOID
  if (!target) {
    raise_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  thread->synchronous_depth++; // TODO remove
  future_t fut = resolve_methodref(&ctx);
  thread->synchronous_depth--;
  CHECK(fut.status == FUTURE_READY);
  if (thread->current_exception) {
    return 0;
  }
  method_info = &insn->cp->methodref;

  // If we found a signature-polymorphic method, transmogrify into a insn_invokesigpoly
  if (method_info->resolved->is_signature_polymorphic) {
    insn->kind = insn_invokesigpoly;
    insn->ic = method_info->resolved;
    insn->ic2 = resolve_method_type(thread, method_info->descriptor);

    arrput(frame->method->my_class->sigpoly_insns, insn); // so GC can move around ic2

    if (unlikely(!insn->ic)) {
      // todo: linkage error
      UNREACHABLE();
    }

    JMP_VOID
  }

  // If we found an interface method, transmogrify into a invokeinterface
  if (method_info->resolved->my_class->access_flags & ACCESS_INTERFACE) {
    insn->kind = insn_invokeinterface;
    JMP_VOID
  }

  insn->kind = insn_invokevtable_monomorphic;
  insn->ic = vtable_lookup(target->descriptor, method_info->resolved->vtable_index);
  insn->ic2 = target->descriptor;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokevirtual)

__attribute__((noinline)) static s64 invokespecial_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *target = (sp - argc)->obj;
  SPILL_VOID
  if (!target) {
    raise_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  CHECK(fut.status == FUTURE_READY);
  if (thread->current_exception) {
    return 0;
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
      return 0;
    }
  } else if (candidate->access_flags & ACCESS_ABSTRACT) {
    raise_abstract_method_error(thread, candidate);
    return 0;
  }

  // If this is the <init> method of Object, make it a nop
  if (utf8_equals(candidate->my_class->name, "java/lang/Object") && utf8_equals(candidate->name, "<init>")) {
    insn->kind = insn_pop;
  } else {
    insn->kind = insn_invokespecial_resolved;
    insn->ic = candidate;
  }
  JMP_VOID
}
FORWARD_TO_NULLARY(invokespecial)

force_inline static s64 invokespecial_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  SPILL_VOID
  NPE_ON_NULL(target);
  cp_method *target_method = insn->ic;
  stack_frame *invoked_frame = push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

  if (thread->current_exception)
    return 0;

  if (returns) {
    *(sp - insn->args) = result;
  }

  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokespecial_resolved)

__attribute__((noinline)) static s64 invokeinterface_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  obj_header *target = (sp - argc)->obj;
  SPILL_VOID
  if (!target) {
    raise_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  CHECK(fut.status == FUTURE_READY);
  if (thread->current_exception)
    return 0;

  method_info = &insn->cp->methodref;
  if (!(method_info->resolved->my_class->access_flags & ACCESS_INTERFACE)) {
    insn->kind = insn_invokevirtual;
    JMP_VOID
  }
  insn->ic = itable_lookup(target->descriptor, method_info->resolved->my_class, method_info->resolved->itable_index);
  insn->ic2 = target->descriptor;
  if (!insn->ic) {
    raise_abstract_method_error(thread, method_info->resolved);
    return 0;
  }
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

force_inline static s64 invokeitable_vtable_monomorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  SPILL_VOID
  NPE_ON_NULL(target);
  if (unlikely(target->descriptor != insn->ic2)) {
    if (insn->kind == insn_invokevtable_monomorphic)
      make_invokevtable_polymorphic_(insn);
    else
      make_invokeitable_polymorphic_(insn);
    JMP_VOID
  }
  stack_frame *invoked_frame = push_frame(thread, insn->ic, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

  if (thread->current_exception)
    return 0;

  if (returns) {
    *(sp - insn->args) = result;
  }
  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokeitable_vtable_monomorphic)

__attribute__((noinline))
static s64 invokesigpoly_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  SPILL_VOID
  NPE_ON_NULL(target);

  invokevirtual_signature_polymorphic_t ctx = {
      .args = {
          .thread = thread, .method = insn->ic, .provider_mt = insn->ic2, .sp_ = sp - insn->args, .target = target}};

  future_t fut = invokevirtual_signature_polymorphic(&ctx);
  if (unlikely(fut.status == FUTURE_NOT_READY)) {
    continuation_frame *cont = async_stack_push(thread);
    frame->is_async_suspended = true;
    *cont = (continuation_frame){.pnt = CONT_INVOKESIGPOLY, .ctx.sigpoly = ctx};
    return 0;
  }

  if (thread->current_exception)
    return 0;

  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokesigpoly)

static s64 invokeitable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  SPILL_VOID
  NPE_ON_NULL(target);
  cp_method *target_method = itable_lookup(target->descriptor, insn->ic, (size_t)insn->ic2);
  if (unlikely(!target_method)) {
    raise_abstract_method_error(thread, insn->cp->methodref.resolved);
    return 0;
  }
  DCHECK(target_method);
  stack_frame *invoked_frame = push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

  if (thread->current_exception)
    return 0;

  if (returns) {
    *(sp - insn->args) = result;
  }
  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokeitable_polymorphic)

static s64 invokevtable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != TYPE_KIND_VOID;
  SPILL_VOID
  NPE_ON_NULL(target);
  cp_method *target_method = vtable_lookup(target->descriptor, (size_t)insn->ic2);
  DCHECK(target_method);
  stack_frame *invoked_frame = push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

  if (thread->current_exception)
    return 0;

  if (returns) {
    *(sp - insn->args) = result;
  }
  sp -= insn->args;
  sp += returns;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
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
  thread->synchronous_depth++;
  future_t fut = indy_resolve(&ctx);
  thread->synchronous_depth--;
  CHECK(fut.status == FUTURE_READY);

  if (thread->current_exception) {
    return 0;
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
    bool returns = form->result != -1;

    // Invoke name->vmtarget with arguments mh, args
    cp_method *invoke = name->vmtarget;

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
      return 0;
    }

    stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

    if (returns) {
      *(sp - insn->args + 1) = result;
    }

    sp -= insn->args - 1;
    sp += returns;

    if (thread->current_exception) {
      return 0;
    }
  } else {
    UNREACHABLE();
  }
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(invokecallsite)

force_inline static stack_value *get_local(stack_frame *frame, bytecode_insn *inst) {
  return frame_locals(frame) + inst->index;
}

/** Local variable accessors */
force_inline static s64 iload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(get_local(frame, insn)->i)
}
FORWARD_TO_NULLARY(iload)

force_inline static s64 fload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_FLOAT(get_local(frame, insn)->f)
}
FORWARD_TO_NULLARY(fload)

force_inline static s64 dload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_DOUBLE(get_local(frame, insn)->d)
}
FORWARD_TO_NULLARY(dload)

force_inline static s64 lload_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  sp++;
  NEXT_INT(get_local(frame, insn)->l)
}
FORWARD_TO_NULLARY(lload)

force_inline static s64 aload_impl_void(ARGS_VOID) {
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
      if (resolve_class(thread, &ent->class_info))
        return 0;
      if (link_class(thread, ent->class_info.classdesc))
        return 0;
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
    if (!obj) // oom
      return 0;
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

// Rare enough (especially after analysis phase) that we might as well just have the one implementation and forward
// every TOS type to them.

// ..., val2, val1 -> ..., val2, val1, val2, val1
static s64 dup2_impl_void(ARGS_VOID) {
  DEBUG_CHECK();
  stack_value val1 = *(sp - 1), val2 = *(sp - 2);
  *(sp) = val2;
  *(sp + 1) = val1;
  sp += 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup2)

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
  thread->current_exception = (obj_header *)tos;
  return 0;
}

static s64 checkcast_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = resolve_class(thread, info) || link_class(thread, info->classdesc);
  if (error)
    return 0;

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
    return 0;
  }
  NEXT_INT(tos)
}

static s64 instanceof_impl_int(ARGS_INT) {
  DEBUG_CHECK();
  cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = resolve_class(thread, info) || link_class(thread, info->classdesc);
  if (error)
    return 0;

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

static s64 cos_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  NEXT_DOUBLE(cos(tos))
}

static s64 sin_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  NEXT_DOUBLE(sin(tos))
}

static s64 cos_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  NEXT_FLOAT(cosf(tos))
}

static s64 sin_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  NEXT_FLOAT(sinf(tos))
}

static s64 sqrt_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK();
  NEXT_DOUBLE(sqrt(tos))
}

static s64 sqrt_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK();
  NEXT_FLOAT(sqrt(tos))
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
  return bytecode_tables[index & 0x3][index >> 2](thread, frame, insns, pc_, sp_, arg_1, arg_2, arg_3);
}

[[maybe_unused]] static standard_debugger* get_active_debugger(vm *vm) {
  return vm->debugger;
}

__attribute__((noinline))
void debugger_pause(vm_thread *thread, stack_frame *frame) {
  continuation_frame *cont = async_stack_push(thread);
  *cont = (continuation_frame) {.pnt = CONT_DEBUGGER_PAUSE};
  frame->is_async_suspended = true;
}

#if DO_TAILS
static s64 entry_impl_void(ARGS_VOID) { STACK_POLYMORPHIC_JMP(*(sp - 1)) }
#else
force_inline static s64 entry_notco_impl(vm_thread *thread, stack_frame *frame, bytecode_insn *code, u16 pc_,
                                                 stack_value *sp_, unsigned handler_i, bool check_stepping) {
  struct {
    stack_value *temp_sp_;
    int64_t temp_int_tos;
    double temp_double_tos;
    float temp_float_tos;
    u16 temp_pc;
  } spill;

  int64_t int_tos = (sp_ - 1)->l;
  double double_tos = (sp_ - 1)->d;
  float float_tos = (sp_ - 1)->f;

  while (true) {
    if (check_stepping && unlikely(thread->is_single_stepping)) {
      standard_debugger *dbg = get_active_debugger(thread->vm);
      DCHECK(dbg && "Debugger not active");
      frame->plain.program_counter = pc_;
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
    handler_i = insn__##_impl_##tos__(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);       \
    break;

#include "interpreter2-notco.inc"

    case 4 * insn_return + TOS_INT:
    case 4 * insn_return + TOS_DOUBLE:
    case 4 * insn_return + TOS_FLOAT:
    case 4 * insn_return + TOS_VOID:
      return return_impl_void(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);

    case 4 * insn_areturn + TOS_INT:
      return areturn_impl_int(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_dreturn + TOS_DOUBLE:
      return dreturn_impl_double(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_freturn + TOS_FLOAT:
      return freturn_impl_float(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_ireturn + TOS_INT:
      return ireturn_impl_int(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);
    case 4 * insn_lreturn + TOS_INT:
      return lreturn_impl_int(thread, frame, code + pc_, &pc_, &sp_, &int_tos, &float_tos, &double_tos);

    case 0: // special value in case of exception or suspend (theoretically also nop_impl_void, but javac doesn't use
            // that)
      DCHECK(thread->current_exception || frame->is_async_suspended);
      return 0;
    default: {
      // outlined case
      spill.temp_pc = pc_;
      spill.temp_sp_ = sp_;
      spill.temp_double_tos = double_tos;
      spill.temp_float_tos = float_tos;
      spill.temp_int_tos = int_tos;

#ifdef EMSCRIPTEN
#define notco_call __interpreter_intrinsic_notco_call_outlined
#else
#define notco_call notco_fallback
#endif

      handler_i = notco_call(thread, frame, code + pc_, &spill.temp_pc, &spill.temp_sp_, &spill.temp_int_tos,
                             &spill.temp_float_tos, &spill.temp_double_tos, (int)handler_i);

      pc_ = spill.temp_pc;
      sp_ = spill.temp_sp_;
      int_tos = spill.temp_int_tos;
      float_tos = spill.temp_float_tos;
      double_tos = spill.temp_double_tos;

      break;
    }
    }
  }
}

[[maybe_unused]] __attribute__((noinline)) static s64 entry_notco_no_stepping(
  vm_thread *thread, stack_frame *frame, bytecode_insn *code, u16 pc_, stack_value *sp_, unsigned handler_i) {
  return entry_notco_impl(thread, frame, code, pc_, sp_, handler_i, false);
}

[[maybe_unused]] __attribute__((noinline)) static s64 entry_notco_with_stepping(
  vm_thread *thread, stack_frame *frame, bytecode_insn *code, u16 pc_, stack_value *sp_, unsigned handler_i) {
  return entry_notco_impl(thread, frame, code, pc_, sp_, handler_i, true);
}
#endif

static exception_table_entry *find_exception_handler(vm_thread *thread, stack_frame *frame, classdesc *exception_type) {
  DCHECK(!frame->is_native);

  attribute_exception_table *table = frame->method->code->exception_table;
  if (!table)
    return nullptr;

  int const pc_ = frame->plain.program_counter;

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
  // we need to pop (not peek) because if we re-enter this method, it'll need to pop its fram
  continuation_frame cont = *async_stack_pop(thread);

  stack_value result;
  future_t fut;

  bool has_result = false;
  bool advance_pc = false;
  bool needs_polymorphic_jump = false;

  switch (cont.pnt) {
  case CONT_RESOLVE:
    fut = resolve_insn(&cont.ctx.resolve_insn);

    bytecode_insn *in = cont.ctx.resolve_insn.args.inst;
    if (in->kind == insn_invokestatic && fut.status == FUTURE_READY) {
      needs_polymorphic_jump = intrinsify(in);
    }
    break;

  case CONT_DEBUGGER_PAUSE:
  case CONT_RESUME_INSN: {
    fut.status = FUTURE_READY;
    needs_polymorphic_jump = true;
    break;
  }

  case CONT_INVOKE:
    result = interpret_2(&fut, thread, cont.ctx.interp_call.frame);
    sp -= cont.ctx.interp_call.argc; // todo: wrong union member
    has_result = cont.ctx.interp_call.returns;
    advance_pc = true;
    break;

  case CONT_MONITOR_ENTER:
    fut = monitor_acquire(&cont.ctx.acquire_monitor);
    sp--; // todo: why are we doing this unconditionally
    advance_pc = true;
    break;

  case CONT_INVOKESIGPOLY:
    fut = invokevirtual_signature_polymorphic(&cont.ctx.sigpoly);
    sp -= cont.ctx.sigpoly.argc; // todo: wrong union member
    has_result = false;
    advance_pc = true;
    break;

  default:
    UNREACHABLE();
  }

  if (fut.status == FUTURE_NOT_READY) {
    cont.wakeup = fut.wakeup;
    *async_stack_push(thread) = cont;
    return 0;
  }

  frame->is_async_suspended = false;
  if (unlikely(thread->current_exception)) {
    return 0;
  }

  if (has_result) {
    *sp++ = result;
  }

  if (advance_pc) {
    STACK_POLYMORPHIC_NEXT(*(sp - 1));
  } else if (needs_polymorphic_jump) {
    STACK_POLYMORPHIC_JMP(*(sp - 1));
  } else {
    JMP_VOID;
  }
}

static inline stack_value interpret_native_frame(future_t *fut, vm_thread *thread, stack_frame *frame_) {
  run_native_t ctx;

  if (!frame_->is_async_suspended) {
    ctx = (run_native_t){.args = {.thread = thread, .frame = frame_}};
  } else {
    continuation_frame *cont = async_stack_pop(thread);
    DCHECK(cont->pnt == CONT_RUN_NATIVE);
    ctx = cont->ctx.run_native;
  }

  *fut = run_native(&ctx);

  if (likely(fut->status == FUTURE_READY)) {
    frame_->is_async_suspended = false;
    pop_frame(thread, frame_);
    return ctx._result;
  } else {
    continuation_frame *cont = async_stack_push(thread);
    *cont = (continuation_frame){.pnt = CONT_RUN_NATIVE, .ctx.run_native = ctx};
    frame_->is_async_suspended = true;
    return (stack_value){0};
  }
}

__attribute__((noinline)) static stack_value interpret_java_frame(future_t *fut, vm_thread *thread,
                                                                  stack_frame *frame_) {
  stack_value result;

  do {
  interpret_begin:
    plain_frame *frame = get_plain_frame(frame_);
    stack_value *sp_ = &frame->stack[stack_depth(frame_)];
    u16 pc_ = frame->program_counter;
    bytecode_insn *insns = frame_->method->code->code;
    [[maybe_unused]] unsigned handler_i = 4 * (insns + pc_)->kind + (insns + pc_)->tos_before;

    if (unlikely(frame_->is_async_suspended)) {
#if DO_TAILS
      result.l = async_resume_impl_void(thread, frame_, insns + pc_, pc_, sp_, 0, 0, 0);
#else
      handler_i =
          async_resume_impl_void(thread, frame_, insns + pc_, &pc_, &sp_, &(sp_ - 1)->l, &(sp_ - 1)->f, &(sp_ - 1)->d);
#endif
    }

#if DO_TAILS
    else {
      result.l = entry_impl_void(thread, frame_, insns + pc_, pc_, sp_, 0, 0, 0);
    }
#else
    if (likely(!frame_->is_async_suspended && !thread->current_exception)) {
      // In the no-tails case, sp, pc, etc. will have been set up appropriately for this call
      if (thread->is_single_stepping) {
        result.l = entry_notco_with_stepping(thread, frame_, insns, pc_, sp_, handler_i);
      } else {
        result.l = entry_notco_no_stepping(thread, frame_, insns, pc_, sp_, handler_i);
      }
    }
#endif

    // we really should just have all the methods return a future_t via a pointer, but whatever
    if (unlikely(frame_->is_async_suspended)) {
      // reconstruct future to return
      void *wk = async_stack_top(thread);
      *fut = (future_t){FUTURE_NOT_READY, wk};
      return (stack_value){0};
    }

    if (unlikely(thread->current_exception)) {
      exception_table_entry *handler = find_exception_handler(thread, frame_, thread->current_exception->descriptor);

      if (handler) {
        frame->program_counter = handler->handler_insn;
        frame->stack[0] = (stack_value){.obj = thread->current_exception};
        thread->current_exception = nullptr;

        goto interpret_begin;
      }
    }
  } while (0);

  pop_frame(thread, frame_);
  *fut = (future_t){FUTURE_READY};

  return result;
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

// NOLINTNEXTLINE(misc-no-recursion)
stack_value interpret_2(future_t *fut, vm_thread *thread, stack_frame *frame_) {
  InstrumentMethodEntry(thread, frame_);

  object synchronized_on = get_sync_object(thread, frame_);
  if (unlikely(synchronized_on)) {
    monitor_acquire_t ctx = {.args = {thread, synchronized_on}};
    future_t fut = monitor_acquire(&ctx);
    if (fut.status == FUTURE_NOT_READY) {
      push_async_monitor_enter(thread, frame_, &ctx);
      return value_null();
    }
  }

  stack_value result;
  if (unlikely(is_frame_native(frame_))) {
    result = interpret_native_frame(fut, thread, frame_);
  } else {
    result = interpret_java_frame(fut, thread, frame_);
  }

  if (likely(fut->status == FUTURE_READY)) {
    synchronized_on = get_sync_object(thread, frame_); // re-compute in case of intervening GC
    if (unlikely(synchronized_on)) {
      int err = monitor_release(thread, synchronized_on);
      if (err) {
        // the current exception is replaced with an IllegalMonitorStateException
        thread->current_exception = nullptr;
        raise_illegal_monitor_state_exception(thread);
        return value_null();
      }
    }

    InstrumentMethodReturn(thread, frame_);
  }

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
    [insn_cos] = cos_impl_double,
    [insn_sin] = sin_impl_double,
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
    [insn_sin] = sin_impl_float,
    [insn_cos] = cos_impl_float,
    [insn_sqrt] = sqrt_impl_float};