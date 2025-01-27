#include "arrays.h"
#include "bjvm.h"
#include "classfile.h"
#include "util.h"

#include <math.h>
#include <tgmath.h>

// Interpreter v2 naming conventions:
//
// There are implementations for TOS types of int/long/reference, float, and double. Some instruction/TOS
// combinations are impossible (rejected by the verifier) and are thus omitted. The execution of "stack polymorphic"
// instructions like pop will consult the TOS stack type that the instruction is annotated with (during analysis),
// before loading the value from the stack and using the appropriate jump table.
//
// The general signature is (thread, frame, insns, pc, sp, tos). At appropriate points (whenever the frame might be
// read back, e.g. for GC purposes, or when interrupting), the TOS value and

#define DEBUG_CHECK
#if 0
#undef DEBUG_CHECK
#define DEBUG_CHECK                                                                                                    \
  SPILL_VOID                                                                                                           \
  bjvm_cp_method *m = frame->method;                                                                                   \
  printf("Calling method %.*s, descriptor %.*s, on class %.*s; %d\n", fmt_slice(m->name),                              \
         fmt_slice(m->unparsed_descriptor), fmt_slice(m->my_class->name), __LINE__);                                   \
  heap_string s = insn_to_string(insn, pc);                                                                            \
  printf("Insn kind: %.*s\n", fmt_slice(s));                                                                           \
  free_heap_str(s);                                                                                                    \
  dump_frame(stderr, frame);                                                                                           \
  assert(stack_depth(frame) == sp - frame->plain.stack);
#endif

// If true, use a sequence of tail calls rather than computed goto and an aggressively inlined function. We try to make
// the code reasonably generic to handle both cases efficiently.
#define DO_TAILS 1

#if DO_TAILS
#define sp sp_
#define pc pc_
#define tos tos_

#define ARGS_BASE                                                                                                      \
  [[maybe_unused]] bjvm_thread *thread, [[maybe_unused]] bjvm_stack_frame *frame,                                      \
      [[maybe_unused]] bjvm_bytecode_insn *insns, [[maybe_unused]] uint16_t pc_,                                       \
      [[maybe_unused]] bjvm_stack_value *sp_

#define ARGS_VOID ARGS_BASE, [[maybe_unused]] int64_t arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_INT ARGS_BASE, [[maybe_unused]] int64_t tos_, [[maybe_unused]] float arg_2, [[maybe_unused]] double arg_3
#define ARGS_DOUBLE                                                                                                    \
  ARGS_BASE, [[maybe_unused]] int64_t arg_1, [[maybe_unused]] float arg_2, [[maybe_unused]] double tos_
#define ARGS_FLOAT ARGS_BASE, [[maybe_unused]] int64_t arg_1, [[maybe_unused]] float tos_, [[maybe_unused]] double arg_3

#else
#error "Not implemented"
#endif

// The current instruction
#define insn (&insns[0])

// Indicates that the following return must be a tail call. Supported by modern clang and GCC (but GCC support seems
// somewhat buggy...)
#define MUSTTAIL [[clang::musttail]]
#define MAX_INSN_KIND (bjvm_insn_dsqrt + 1)

// Used when the TOS is int (i.e., the stack is empty)
static int64_t (*jmp_table_void[MAX_INSN_KIND])(ARGS_VOID);
// Used when the TOS is int, long, or a reference (wasm signature: i64). In the int case, the result is sign-extended;
// in the reference case, the result is zero-extended.
static int64_t (*jmp_table_int[MAX_INSN_KIND])(ARGS_INT);
// Used when the TOS is float (wasm signature: f32)
static int64_t (*jmp_table_float[MAX_INSN_KIND])(ARGS_FLOAT);
// Used when the TOS is double (wasm signature: f64)
static int64_t (*jmp_table_double[MAX_INSN_KIND])(ARGS_DOUBLE);

#if DO_TAILS

#ifdef EMSCRIPTEN
// Sad :(
#define WITH_UNDEF(expr)                                                                                               \
  {                                                                                                                    \
    int64_t a_undef;                                                                                                   \
    float b_undef;                                                                                                     \
    double c_undef;                                                                                                    \
    expr                                                                                                               \
  }
#else
#define WITH_UNDEF(expr)                                                                                               \
  {                                                                                                                    \
    int64_t a_undef;                                                                                                   \
    float b_undef;                                                                                                     \
    double c_undef;                                                                                                    \
    asm("" : "=r"(a_undef), "=r"(b_undef), "=r"(c_undef));                                                             \
    expr                                                                                                               \
  }
#endif

#define JMP_INT(tos)                                                                                                   \
  int k = insns[0].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_int[k](thread, frame, insns, pc, sp, tos, b_undef, c_undef);)
#define JMP_FLOAT(tos)                                                                                                 \
  int k = insns[0].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_float[k](thread, frame, insns, pc, sp, a_undef, tos, c_undef);)
#define JMP_DOUBLE(tos)                                                                                                \
  int k = insns[0].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_double[k](thread, frame, insns, pc, sp, a_undef, b_undef, tos);)

#define NEXT_INT(tos)                                                                                                  \
  int k = insns[1].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_int[k](thread, frame, insns + 1, pc + 1, sp, (int64_t)tos, b_undef, c_undef);)
#define NEXT_FLOAT(tos)                                                                                                \
  int k = insns[1].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_float[k](thread, frame, insns + 1, pc + 1, sp, a_undef, tos, c_undef);)
#define NEXT_DOUBLE(tos)                                                                                               \
  int k = insns[1].kind;                                                                                               \
  WITH_UNDEF(MUSTTAIL return jmp_table_double[k](thread, frame, insns + 1, pc + 1, sp, a_undef, b_undef, tos);)

// Jump to the instruction at pc, with nothing in the top of the stack. This does NOT imply that sp = 0, only that
// all stack values are in memory (rather than in a register)
#define JMP_VOID                                                                                                       \
  WITH_UNDEF(MUSTTAIL return jmp_table_void[insns[0].kind](thread, frame, insns, pc, sp, a_undef, b_undef, c_undef);)
// Jump to the instruction at pc + 1, with nothing in the top of the stack.
#define NEXT_VOID                                                                                                      \
  WITH_UNDEF(                                                                                                          \
      MUSTTAIL return jmp_table_void[insns[1].kind](thread, frame, insns + 1, pc + 1, sp, a_undef, b_undef, c_undef);)
#else
#endif

// Spill all the information currently in locals/registers to the frame (required at safepoints and when interrupting)
#define SPILL(tos)                                                                                                     \
  frame->plain.program_counter = pc;                                                                                   \
  *(sp - 1) = _Generic((tos),                                                                                          \
      int64_t: (bjvm_stack_value){.l = (int64_t)tos},                                                                  \
      float: (bjvm_stack_value){.f = (float)tos},                                                                      \
      double: (bjvm_stack_value){.d = (double)tos},                                                                    \
      bjvm_obj_header *: (bjvm_stack_value){.obj = (bjvm_obj_header *)(uintptr_t)tos} /* shut up float branch */       \
  );
// Same as SPILL(tos), but when no top-of-stack value is available
#define SPILL_VOID frame->plain.program_counter = pc;

// Reload the top of stack type -- used after an instruction which may have instigated a GC. RELOAD_VOID is not
// required.
#define RELOAD(tos)                                                                                                    \
  tos = _Generic(tos,                                                                                                  \
      int64_t: (*(sp - 1)).l,                                                                                          \
      float: (*(sp - 1)).f,                                                                                            \
      double: (*(sp - 1)).d,                                                                                           \
      bjvm_obj_header *: (*(sp - 1)).obj);

// Go to the next instruction, but where we don't know a priori the top-of-stack type for that instruction, and must
// look it up from the analyzed tos type.
#define STACK_POLYMORPHIC_NEXT(tos)                                                                                    \
  switch (insn->tos_after) {                                                                                           \
  case TOS_VOID: {                                                                                                     \
    NEXT_VOID                                                                                                          \
  }                                                                                                                    \
  case TOS_INT: {                                                                                                      \
    NEXT_INT((tos).l)                                                                                                  \
  }                                                                                                                    \
  case TOS_FLOAT: {                                                                                                    \
    NEXT_FLOAT((tos).f)                                                                                                \
  }                                                                                                                    \
  case TOS_DOUBLE: {                                                                                                   \
    NEXT_DOUBLE((tos).d)                                                                                               \
  }                                                                                                                    \
  default:                                                                                                             \
    __builtin_unreachable();                                                                                           \
  }

// Go to the instruction at pc, but where we don't know a priori the top-of-stack type for that instruction, and must
// look it up from the analyzed tos type.
#define STACK_POLYMORPHIC_JMP(tos)                                                                                     \
  switch (insn->tos_before) {                                                                                          \
  case TOS_VOID: {                                                                                                     \
    JMP_VOID                                                                                                           \
  }                                                                                                                    \
  case TOS_INT: {                                                                                                      \
    JMP_INT((tos).l)                                                                                                   \
  }                                                                                                                    \
  case TOS_FLOAT: {                                                                                                    \
    JMP_FLOAT((tos).f)                                                                                                 \
  }                                                                                                                    \
  case TOS_DOUBLE: {                                                                                                   \
    JMP_DOUBLE((tos).d)                                                                                                \
  }                                                                                                                    \
  default:                                                                                                             \
    __builtin_unreachable();                                                                                           \
  }

// For a bytecode that takes no arguments, given an implementation for the int TOS type, generate adapter funcsptions
// which push the current TOS value onto the stack and then call the void TOS implementation.
#define FORWARD_TO_NULLARY(which)                                                                                      \
  static int64_t which##_impl_int(ARGS_INT) {                                                                          \
    *(sp - 1) = (bjvm_stack_value){.l = tos};                                                                          \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc, sp, tos, arg_2, arg_3);                                \
  }                                                                                                                    \
                                                                                                                       \
  static int64_t which##_impl_float(ARGS_FLOAT) {                                                                      \
    *(sp - 1) = (bjvm_stack_value){.f = tos};                                                                          \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc, sp, arg_1, tos, arg_3);                                \
  }                                                                                                                    \
                                                                                                                       \
  static int64_t which##_impl_double(ARGS_DOUBLE) {                                                                    \
    *(sp - 1) = (bjvm_stack_value){.d = tos};                                                                          \
    MUSTTAIL return which##_impl_void(thread, frame, insns, pc, sp, arg_1, arg_2, tos);                                \
  }

/** Helper functions */

int32_t java_idiv_(int32_t const a, int32_t const b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return INT_MIN;
  return a / b;
}

int64_t java_irem_(int32_t const a, int32_t const b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return 0;
  return a % b;
}

int64_t java_ldiv_(int64_t const a, int64_t const b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return LONG_MIN;
  return a / b;
}

int64_t java_lrem_(int64_t const a, int64_t const b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return 0;
  return a % b;
}

// Java saturates the conversion
static int double_to_int(double const x) {
  if (x > INT_MAX)
    return INT_MAX;
  if (x < INT_MIN)
    return INT_MIN;
  if (isnan(x))
    return 0;
  return (int)x;
}

// Java saturates the conversion
static int64_t double_to_long(double const x) {
  if (x >= (double)(ULLONG_MAX / 2))
    return LLONG_MAX;
  if (x < (double)LLONG_MIN)
    return LLONG_MIN;
  if (isnan(x))
    return 0;
  return (int64_t)x;
}

// Convert getstatic and putstatic instructions into one of the resolved forms -- or throw a linkage error if
// appropriate. The stack should be made consistent before this function is called, as it may interrupt.
DECLARE_ASYNC(int, resolve_getstatic_putstatic,
  locals(bjvm_cp_field_info *field_info; bjvm_cp_class_info *class),
  arguments(bjvm_thread *thread; bjvm_bytecode_insn *inst;),
  invoked_methods(invoked_method(bjvm_initialize_class)));

// Convert getfield and putfield instructions into one of the resolved forms -- or throw a linkage error if
// appropriate. The stack should be made consistent before this function is called, as it may interrupt.
DECLARE_ASYNC(int, resolve_getfield_putfield,
  locals(bjvm_cp_field_info *field_info; bjvm_cp_class_info *class),
  arguments(bjvm_thread *thread; bjvm_bytecode_insn *inst; bjvm_plain_frame *frame; bjvm_stack_value *sp_;),
  invoked_methods(invoked_method(bjvm_initialize_class)));

/// In the interpreter, we don't use the DECLARE_ASYNC/DEFINE_ASYNC macros;  instead, we manually
/// define the different positions an async continuation may return to.  When an async function yields,
/// the top of the async stack contains (a) the state index and (b) a pointer to a malloc'd
/// context struct.
start_counter(state_index, 1);
typedef enum { STATE_DONE, STATE_FAILED, STATE_YIELD } async_task_status;

typedef enum {
  CONT_GETSTATIC,
  CONT_PUTSTATIC,
  CONT_INVOKE,
  CONT_INVOKESIGPOLY // tos must be reloaded
} continuation_point;

typedef struct {
  async_wakeup_info *wakeup;
  continuation_point pnt;

  union {
    resolve_getstatic_putstatic_t resolve_getstatic_putstatic;
    resolve_getfield_putfield_t resolve_getfield_putfield;
    bjvm_invokevirtual_signature_polymorphic_t sigpoly;
    struct {
      bjvm_stack_frame *frame;
      uint8_t argc;
      bool returns;
    } interp_call;
  } ctx;

} continuation_frame;

struct async_stack {
  uint16_t max_height;
  uint16_t height;
  continuation_frame frames[];
};

static int32_t grow_async_stack(bjvm_thread *thread) {
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

  stk->max_height = new_capacity;
  return 0;
}

static continuation_frame *async_stack_push(bjvm_thread *thread) {
  struct async_stack *stk = thread->async_stack;

  if (unlikely(stk->height == stk->max_height)) {
    if (grow_async_stack(thread) < 0) {
      thread->current_exception = thread->stack_overflow_error;
      return nullptr;
    }
  }

  return &thread->async_stack->frames[thread->async_stack->height++];
}

static continuation_frame *async_stack_pop(bjvm_thread *thread) {
  assert(thread->async_stack->height > 0);
  return &thread->async_stack->frames[--thread->async_stack->height];
}

static async_wakeup_info *async_stack_top(bjvm_thread *thread) {
  assert(thread->async_stack->height > 0);
  return thread->async_stack->frames[thread->async_stack->height - 1].wakeup;
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
bjvm_insn_code_kind getstatic_putstatic_resolved_kind(bool putstatic, bjvm_type_kind field_kind) {
  switch (field_kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return putstatic ? bjvm_insn_putstatic_B : bjvm_insn_getstatic_B;
  case BJVM_TYPE_KIND_CHAR:
    return putstatic ? bjvm_insn_putstatic_C : bjvm_insn_getstatic_C;
  case BJVM_TYPE_KIND_FLOAT:
    return putstatic ? bjvm_insn_putstatic_F : bjvm_insn_getstatic_F;
  case BJVM_TYPE_KIND_DOUBLE:
    return putstatic ? bjvm_insn_putstatic_D : bjvm_insn_getstatic_D;
  case BJVM_TYPE_KIND_BYTE:
    return putstatic ? bjvm_insn_putstatic_B : bjvm_insn_getstatic_B;
  case BJVM_TYPE_KIND_SHORT:
    return putstatic ? bjvm_insn_putstatic_S : bjvm_insn_getstatic_S;
  case BJVM_TYPE_KIND_INT:
    return putstatic ? bjvm_insn_putstatic_I : bjvm_insn_getstatic_I;
  case BJVM_TYPE_KIND_LONG:
    return putstatic ? bjvm_insn_putstatic_J : bjvm_insn_getstatic_J;
  case BJVM_TYPE_KIND_REFERENCE:
    return putstatic ? bjvm_insn_putstatic_L : bjvm_insn_getstatic_L;
  default:
    UNREACHABLE();
    break;
  }
}

DEFINE_ASYNC_SL(resolve_getstatic_putstatic, 100) {
  // For brevity
#define inst self->args.inst
#define thread self->args.thread

  bool putstatic = inst->kind == bjvm_insn_putstatic;
  self->field_info = &inst->cp->field;
  self->class = self->field_info->class_info;

  // First, attempt to resolve the class that the instruction is referring to.
  if (bjvm_resolve_class(thread, self->class))
    ASYNC_RETURN(-1);

  // Then, attempt to initialize the class.
  AWAIT(bjvm_initialize_class, thread, self->class->classdesc);
  if (thread->current_exception)
    ASYNC_RETURN(-1);

#define field self->field_info->field

  // Look up the field on the class.
  field = bjvm_field_lookup(self->class->classdesc, self->field_info->nat->name, self->field_info->nat->descriptor);

  // Check that the field exists on the class and is static. (TODO access checks)
  if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
    INIT_STACK_STRING(complaint, 1000);
    bprintf(complaint, "Expected static field %.*s on class %.*s", fmt_slice(self->field_info->nat->name),
            fmt_slice(self->field_info->class_info->name));
    bjvm_incompatible_class_change_error(thread, complaint);
    ASYNC_RETURN(-1);
  }

  // Select the appropriate resolved instruction kind.
  inst->kind = getstatic_putstatic_resolved_kind(putstatic, field_to_kind(self->field_info->parsed_descriptor));

  // Store the static address of the field in the instruction.
  inst->ic = (void *)field->my_class->static_fields + field->byte_offset;

#undef thread
#undef inst
#undef field

  ASYNC_END(0);
}

static int64_t getstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  resolve_getstatic_putstatic_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.inst = insn;
  SPILL_VOID
  future_t fut = resolve_getstatic_putstatic(&ctx);

  if (unlikely(fut.status == FUTURE_NOT_READY)) {
    continuation_frame *cont = async_stack_push(thread);
    frame->is_async_suspended = true;
    *cont = (continuation_frame){.pnt = CONT_GETSTATIC, .ctx.resolve_getstatic_putstatic = ctx};
    return 0;
  }

  if (unlikely(thread->current_exception)) {
    return 0;
  }
  assert(fut.status == FUTURE_READY); // for now
  JMP_VOID                            // we rewrote this instruction to a resolved form, so jump to that implementation
}
FORWARD_TO_NULLARY(getstatic)

// Never actually directly called -- we just do it this way because it's easier and we might as well merge code paths
// for different TOS types.
static int64_t putstatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  resolve_getstatic_putstatic_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.inst = insn;
  SPILL_VOID
  future_t fut = resolve_getstatic_putstatic(&ctx);
  if (thread->current_exception) {
    return 0;
  }
  assert(fut.status == FUTURE_READY); // for now
  STACK_POLYMORPHIC_JMP(*(sp - 1));
}
FORWARD_TO_NULLARY(putstatic)

static int64_t getstatic_L_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT(*(bjvm_obj_header **)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_L)

static int64_t getstatic_F_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_FLOAT(*(float *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_F)

static int64_t getstatic_D_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_DOUBLE(*(double *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_D)

static int64_t getstatic_J_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT(*(int64_t *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_J)

static int64_t getstatic_I_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT(*(int *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_I)

static int64_t getstatic_S_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT((int64_t)*(int16_t *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_S)

static int64_t getstatic_C_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT((int64_t)*(uint16_t *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_C)

static int64_t getstatic_B_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT((int64_t)*(int8_t *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_B)

static int64_t getstatic_Z_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  assert(insn->ic && "Static field location not found");
  sp++;
  NEXT_INT((int64_t)*(int8_t *)insn->ic)
}
FORWARD_TO_NULLARY(getstatic_Z)

static int64_t putstatic_B_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(int8_t *)insn->ic = (int8_t)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_C_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(uint16_t *)insn->ic = (uint16_t)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_S_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(int16_t *)insn->ic = (int16_t)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_I_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(int *)insn->ic = (int)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_J_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(int64_t *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_F_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK
  *(float *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_D_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK
  *(double *)insn->ic = tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_L_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(bjvm_obj_header **)insn->ic = (bjvm_obj_header *)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putstatic_Z_impl_int(ARGS_INT) {

  DEBUG_CHECK
  *(int8_t *)insn->ic = (int8_t)tos;
  --sp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** getfield/putfield */

bjvm_insn_code_kind getfield_putfield_resolved_kind(bool putfield, bjvm_type_kind field_kind) {
  switch (field_kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return putfield ? bjvm_insn_putfield_B : bjvm_insn_getfield_B;
  case BJVM_TYPE_KIND_CHAR:
    return putfield ? bjvm_insn_putfield_C : bjvm_insn_getfield_C;
  case BJVM_TYPE_KIND_FLOAT:
    return putfield ? bjvm_insn_putfield_F : bjvm_insn_getfield_F;
  case BJVM_TYPE_KIND_DOUBLE:
    return putfield ? bjvm_insn_putfield_D : bjvm_insn_getfield_D;
  case BJVM_TYPE_KIND_BYTE:
    return putfield ? bjvm_insn_putfield_B : bjvm_insn_getfield_B;
  case BJVM_TYPE_KIND_SHORT:
    return putfield ? bjvm_insn_putfield_S : bjvm_insn_getfield_S;
  case BJVM_TYPE_KIND_INT:
    return putfield ? bjvm_insn_putfield_I : bjvm_insn_getfield_I;
  case BJVM_TYPE_KIND_LONG:
    return putfield ? bjvm_insn_putfield_J : bjvm_insn_getfield_J;
  case BJVM_TYPE_KIND_REFERENCE:
    return putfield ? bjvm_insn_putfield_L : bjvm_insn_getfield_L;
  default:
    UNREACHABLE();
  }
}

DEFINE_ASYNC_SL(resolve_getfield_putfield, 100) {
  // For brevity
#define inst self->args.inst
#define thread self->args.thread
#define frame self->args.frame
#define sp_ self->args.sp_

  bool putfield = inst->kind == bjvm_insn_putfield;

  bjvm_obj_header *obj = (*(sp_ - 1 - putfield)).obj;
  if (!obj) {
    bjvm_null_pointer_exception(thread);
    ASYNC_RETURN(-1);
  }
  bjvm_cp_field_info *field_info = &inst->cp->field;
  if (bjvm_resolve_field(thread, field_info)) {
    ASYNC_RETURN(-1);
  }
  if (field_info->field->access_flags & BJVM_ACCESS_STATIC) {
    INIT_STACK_STRING(complaint, 1000);
    bprintf(complaint, "Expected nonstatic field %.*s on class %.*s", fmt_slice(field_info->nat->name),
            fmt_slice(field_info->class_info->name));
    bjvm_incompatible_class_change_error(thread, complaint);
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

static int64_t getfield_impl_int(ARGS_INT) {

  SPILL(tos)
  DEBUG_CHECK
  resolve_getfield_putfield_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.inst = insn;
  ctx.args.frame = &frame->plain;
  ctx.args.sp = sp;
  future_t fut = resolve_getfield_putfield(&ctx);
  if (thread->current_exception) {
    return 0;
  }
  RELOAD(tos)
  assert(fut.status == FUTURE_READY); // for now
  JMP_INT(tos)
}

static int64_t putfield_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  resolve_getfield_putfield_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.inst = insn;
  ctx.args.frame = &frame->plain;
  ctx.args.sp = sp;
  SPILL_VOID
  future_t fut = resolve_getfield_putfield(&ctx);
  if (thread->current_exception) {
    return 0;
  }
  assert(fut.status == FUTURE_READY); // for now
  STACK_POLYMORPHIC_JMP(*(sp - 1))
}
FORWARD_TO_NULLARY(putfield)

static int64_t getfield_B_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int8_t *field = (int8_t *)((char *)tos + (int)insn->ic2);
  NEXT_INT((int64_t)*field)
}

static int64_t getfield_C_impl_int(ARGS_INT) {

  DEBUG_CHECK
  uint16_t *field = (uint16_t *)((char *)tos + (int)insn->ic2);
  NEXT_INT((int64_t)*field)
}

static int64_t getfield_S_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int16_t *field = (int16_t *)((char *)tos + (int)insn->ic2);
  NEXT_INT((int64_t)*field)
}

static int64_t getfield_I_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int *field = (int *)((char *)tos + (int)insn->ic2);
  NEXT_INT((int64_t)*field)
}

static int64_t getfield_J_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t *field = (int64_t *)((char *)tos + (int)insn->ic2);
  NEXT_INT(*field)
}

static int64_t getfield_F_impl_int(ARGS_INT) {

  DEBUG_CHECK
  float *field = (float *)((char *)tos + (int)insn->ic2);
  NEXT_FLOAT(*field)
}

static int64_t getfield_D_impl_int(ARGS_INT) {

  DEBUG_CHECK
  double *field = (double *)((char *)tos + (int)insn->ic2);
  NEXT_DOUBLE(*field)
}

static int64_t getfield_L_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_obj_header **field = (bjvm_obj_header **)((char *)tos + (int)insn->ic2);
  NEXT_INT(*field)
}

static int64_t getfield_Z_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int8_t *field = (int8_t *)((char *)tos + (int)insn->ic2);
  NEXT_INT((int64_t)*field)
}

static int64_t putfield_B_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int8_t *field = (int8_t *)((char *)(*(sp - 2)).obj + (int)insn->ic2);
  *field = (int8_t)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_C_impl_int(ARGS_INT) {

  DEBUG_CHECK
  uint16_t *field = (uint16_t *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = (uint16_t)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_S_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int16_t *field = (int16_t *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = (int16_t)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_I_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int *field = (int *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = (int)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_J_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t *field = (int64_t *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_L_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_obj_header **field = (bjvm_obj_header **)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = (bjvm_obj_header *)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_Z_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int8_t *field = (int8_t *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = (int8_t)tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_F_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK
  float *field = (float *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t putfield_D_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK
  double *field = (double *)((char *)(sp - 2)->obj + (int)insn->ic2);
  *field = tos;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** Arithmetic operations */

#define ALLOW_OVERFLOW                                                                                                 \
  __attribute__((no_sanitize("unsigned-integer-overflow"), no_sanitize("unsigned-shift-base")))

// Binary operation on two integers (ints or longs)
#define INTEGER_BIN_OP(which, eval)                                                                                    \
  ALLOW_OVERFLOW static int64_t which##_impl_int(ARGS_INT) {                                                           \
    DEBUG_CHECK                                                                                                        \
    int64_t a = (sp - 2)->l, b = tos;                                                                                  \
    int64_t result = eval;                                                                                             \
    sp--;                                                                                                              \
    NEXT_INT(result)                                                                                                   \
  }

INTEGER_BIN_OP(iadd, (int32_t)((uint32_t)a + (uint32_t)b))
INTEGER_BIN_OP(ladd, (int64_t)((uint64_t)a + (uint64_t)b))
INTEGER_BIN_OP(isub, (int32_t)((uint32_t)a - (uint32_t)b))
INTEGER_BIN_OP(lsub, (int64_t)((uint64_t)a - (uint64_t)b))
INTEGER_BIN_OP(imul, (int32_t)((uint32_t)a * (uint32_t)b))
INTEGER_BIN_OP(lmul, (int64_t)((uint64_t)a * (uint64_t)b))
INTEGER_BIN_OP(iand, (int32_t)((uint32_t)a & (uint32_t)b))
INTEGER_BIN_OP(land, (int64_t)((uint64_t)a & (uint64_t)b))
INTEGER_BIN_OP(ior, (int32_t)((uint32_t)a | (uint32_t)b))
INTEGER_BIN_OP(lor, (int64_t)((uint64_t)a | (uint64_t)b))
INTEGER_BIN_OP(ixor, (int32_t)((uint32_t)a ^ (uint32_t)b))
INTEGER_BIN_OP(lxor, (int64_t)((uint64_t)a ^ (uint64_t)b))
INTEGER_BIN_OP(ishl, (int32_t)((uint32_t)a << (b & 0x1f)))
INTEGER_BIN_OP(lshl, (int64_t)((uint64_t)a << (b & 0x3f)))
INTEGER_BIN_OP(ishr, (uint32_t)((int32_t)a >> (b & 0x1f)))
INTEGER_BIN_OP(lshr, (int64_t)a >> (b & 0x3f))
INTEGER_BIN_OP(iushr, (int32_t)((uint32_t)a >> (b & 0x1f)))
INTEGER_BIN_OP(lushr, (int64_t)((uint64_t)a >> (b & 0x3f)))

#undef INTEGER_BIN_OP

#define INTEGER_UN_OP(which, eval, NEXT)                                                                               \
  ALLOW_OVERFLOW static int64_t which##_impl_int(ARGS_INT) {                                                           \
    DEBUG_CHECK                                                                                                        \
    int64_t a = tos;                                                                                                   \
    NEXT(eval)                                                                                                         \
  }

INTEGER_UN_OP(ineg, (int32_t)(-(uint32_t)a), NEXT_INT)
INTEGER_UN_OP(lneg, (int64_t)(-(uint64_t)a), NEXT_INT)
INTEGER_UN_OP(i2l, (int64_t)a, NEXT_INT)
INTEGER_UN_OP(i2s, (int64_t)(int16_t)a, NEXT_INT)
INTEGER_UN_OP(i2b, (int64_t)(int8_t)a, NEXT_INT)
INTEGER_UN_OP(i2c, (int64_t)(uint16_t)a, NEXT_INT)
INTEGER_UN_OP(i2f, (float)a, NEXT_FLOAT)
INTEGER_UN_OP(i2d, (double)a, NEXT_DOUBLE)
INTEGER_UN_OP(l2i, (int)a, NEXT_INT)
INTEGER_UN_OP(l2f, (float)a, NEXT_FLOAT)
INTEGER_UN_OP(l2d, (double)a, NEXT_DOUBLE)

#undef INTEGER_UN_OP

#define FLOAT_BIN_OP(which, eval, out_float, out_double, NEXT1, NEXT2)                                                 \
  static int64_t f##which##_impl_float(ARGS_FLOAT) {                                                                   \
    DEBUG_CHECK                                                                                                        \
    float a = (sp - 2)->f, b = tos;                                                                                    \
    out_float result = eval;                                                                                           \
    sp--;                                                                                                              \
    NEXT1(result)                                                                                                      \
  }                                                                                                                    \
  static int64_t d##which##_impl_double(ARGS_DOUBLE) {                                                                 \
    DEBUG_CHECK                                                                                                        \
    double a = (sp - 2)->d, b = tos;                                                                                   \
    out_double result = eval;                                                                                          \
    sp--;                                                                                                              \
    NEXT2(result)                                                                                                      \
  }

#define FLOAT_UN_OP(which, eval, out, NEXT)                                                                            \
  static int64_t which##_impl_float(ARGS_FLOAT) {                                                                      \
    DEBUG_CHECK                                                                                                        \
    float a = tos;                                                                                                     \
    out result = eval;                                                                                                 \
    NEXT(result)                                                                                                       \
  }

#define DOUBLE_UN_OP(which, eval, out, NEXT)                                                                           \
  static int64_t which##_impl_double(ARGS_DOUBLE) {                                                                    \
    DEBUG_CHECK                                                                                                        \
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
FLOAT_UN_OP(f2i, double_to_int(a), int, NEXT_INT)
FLOAT_UN_OP(f2l, double_to_long(a), int64_t, NEXT_INT)
FLOAT_UN_OP(f2d, (double)a, double, NEXT_DOUBLE)

DOUBLE_UN_OP(dneg, -a, double, NEXT_DOUBLE)
DOUBLE_UN_OP(d2i, double_to_int(a), int, NEXT_INT)
DOUBLE_UN_OP(d2l, double_to_long(a), int64_t, NEXT_INT)
DOUBLE_UN_OP(d2f, (float)a, float, NEXT_FLOAT)

static int64_t idiv_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int a = (sp - 2)->i, b = (int)tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    bjvm_arithmetic_exception(thread, STR("/ by zero"));
    return 0;
  }
  sp--;
  NEXT_INT(java_idiv_(a, b));
}

static int64_t ldiv_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t a = (sp - 2)->l, b = tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    bjvm_arithmetic_exception(thread, STR("/ by zero"));
    return 0;
  }
  sp--;
  NEXT_INT(java_ldiv_(a, b));
}

static int64_t lcmp_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t a = (sp - 2)->l, b = tos;
  sp--;
  NEXT_INT(a > b ? 1 : (a < b ? -1 : 0));
}

static int64_t irem_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int a = (sp - 2)->i, b = (int)tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    bjvm_arithmetic_exception(thread, STR("/ by zero"));
    return 0;
  }
  sp--;
  NEXT_INT(java_irem_(a, b));
}

static int64_t lrem_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t a = (sp - 2)->l, b = tos;
  if (unlikely(b == 0)) {
    SPILL(tos);
    bjvm_arithmetic_exception(thread, STR("/ by zero"));
    return 0;
  }
  sp--;
  NEXT_INT(java_lrem_(a, b));
}

/** Array instructions (arraylength, array loads, array stores) */

static int64_t arraylength_impl_int(ARGS_INT) {
  DEBUG_CHECK
  bjvm_obj_header *array = (bjvm_obj_header *)tos;
  if (unlikely(!array)) {
    SPILL(tos);
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  NEXT_INT(*ArrayLength(array))
}

#define ARRAY_LOAD(which, load, type, NEXT)                                                                            \
  static int64_t which##_impl_int(ARGS_INT) {                                                                          \
    DEBUG_CHECK                                                                                                        \
    bjvm_obj_header *array = (bjvm_obj_header *)(sp - 2)->obj;                                                         \
    int index = (int)tos;                                                                                              \
    if (unlikely(!array)) {                                                                                            \
      SPILL(tos);                                                                                                      \
      bjvm_null_pointer_exception(thread);                                                                             \
      return 0;                                                                                                        \
    }                                                                                                                  \
    int length = *ArrayLength(array);                                                                                  \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL(tos);                                                                                                      \
      bjvm_array_index_oob_exception(thread, index, length);                                                           \
      return 0;                                                                                                        \
    }                                                                                                                  \
    sp--;                                                                                                              \
    type cow = (type)load(array, index);                                                                               \
    NEXT(cow)                                                                                                          \
  }

ARRAY_LOAD(iaload, IntArrayLoad, int, NEXT_INT)
ARRAY_LOAD(laload, LongArrayLoad, int64_t, NEXT_INT)
ARRAY_LOAD(faload, FloatArrayLoad, float, NEXT_FLOAT)
ARRAY_LOAD(daload, DoubleArrayLoad, double, NEXT_DOUBLE)
ARRAY_LOAD(aaload, ReferenceArrayLoad, bjvm_obj_header *, NEXT_INT)
ARRAY_LOAD(baload, ByteArrayLoad, int64_t, NEXT_INT)
ARRAY_LOAD(saload, ShortArrayLoad, int64_t, NEXT_INT)
ARRAY_LOAD(caload, CharArrayLoad, int64_t, NEXT_INT)

#define ARRAY_STORE(which, tt1, args, tt3, store)                                                                      \
  static int64_t which##_impl_##tt1(args) {                                                                            \
    DEBUG_CHECK                                                                                                        \
    bjvm_obj_header *array = (bjvm_obj_header *)(sp - 3)->obj;                                                         \
    int index = (int)(sp - 2)->i;                                                                                      \
    if (unlikely(!array)) {                                                                                            \
      SPILL(tos);                                                                                                      \
      bjvm_null_pointer_exception(thread);                                                                             \
      return 0;                                                                                                        \
    }                                                                                                                  \
    int length = *ArrayLength(array);                                                                                  \
    if (unlikely(index < 0 || index >= length)) {                                                                      \
      SPILL(tos);                                                                                                      \
      bjvm_array_index_oob_exception(thread, index, length);                                                           \
      return 0;                                                                                                        \
    }                                                                                                                  \
    store(array, index, (tt3)tos);                                                                                     \
    sp -= 3;                                                                                                           \
    STACK_POLYMORPHIC_NEXT(*(sp - 1));                                                                                 \
  }

ARRAY_STORE(iastore, int, ARGS_INT, int, IntArrayStore)
ARRAY_STORE(lastore, int, ARGS_INT, int64_t, LongArrayStore)
ARRAY_STORE(fastore, float, ARGS_FLOAT, float, FloatArrayStore)
ARRAY_STORE(dastore, double, ARGS_DOUBLE, double, DoubleArrayStore)
ARRAY_STORE(bastore, int, ARGS_INT, int8_t, ByteArrayStore)
ARRAY_STORE(sastore, int, ARGS_INT, int16_t, ShortArrayStore)
ARRAY_STORE(castore, int, ARGS_INT, uint16_t, CharArrayStore)

// We implement aastore separately because it needs an additional instanceof check for ArrayStoreExceptions.
// <array> <index> <value>  ->  <void>
static int64_t aastore_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_obj_header *array = (bjvm_obj_header *)(sp - 3)->obj;
  bjvm_obj_header *value = (bjvm_obj_header *)tos;
  int index = (int)(sp - 2)->i;
  if (unlikely(!array)) {
    SPILL(tos);
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  int length = *ArrayLength(array);
  if (unlikely(index < 0 || index >= length)) {
    SPILL(tos);
    bjvm_array_index_oob_exception(thread, index, length);
    return 0;
  }
  // Instanceof check against the component type
  if (value && !bjvm_instanceof(value->descriptor, array->descriptor->one_fewer_dim)) {
    SPILL(tos);
    bjvm_array_store_exception(thread, hslc(value->descriptor->name));
    return 0;
  }
  ReferenceArrayStore(array, index, value);
  sp -= 3;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** Control-flow instructions (returns, jumps, branches) */

static int64_t return_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  SPILL_VOID
  return 0;
}
FORWARD_TO_NULLARY(return)

static int64_t areturn_impl_int(ARGS_INT) {
  DEBUG_CHECK
  SPILL(tos);
  return (int64_t)(uintptr_t)tos;
}

static int64_t ireturn_impl_int(ARGS_INT) {
  DEBUG_CHECK
  SPILL(tos);
  return (int)tos;
}

static int64_t lreturn_impl_int(ARGS_INT) {
  DEBUG_CHECK
  SPILL(tos);
  return tos;
}

static int64_t freturn_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK
  SPILL(tos);
  int64_t a = 0;
  memcpy(&a, &tos, sizeof(float));
  return a;
}

static int64_t dreturn_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK
  SPILL(tos);
  int64_t a = 0;
  memcpy(&a, &tos, sizeof(double));
  return a;
}

static int64_t goto_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  int32_t delta = (int32_t)insn->index - (int32_t)pc;
  pc = insn->index;
  insns += delta;
  JMP_VOID
}

static int64_t goto_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK
  int32_t delta = (int32_t)insn->index - (int32_t)pc;
  pc = insn->index;
  insns += delta;
  JMP_DOUBLE(tos)
}

static int64_t goto_impl_float(ARGS_FLOAT) {
  DEBUG_CHECK
  int32_t delta = (int32_t)insn->index - (int32_t)pc;
  pc = insn->index;
  insns += delta;
  JMP_FLOAT(tos)
}

static int64_t goto_impl_int(ARGS_INT) {
  DEBUG_CHECK
  int32_t delta = (int32_t)insn->index - (int32_t)pc;
  pc = insn->index;
  insns += delta;
  JMP_INT(tos)
}

static int64_t tableswitch_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int32_t index = (int32_t)tos;
  int32_t low = insn->tableswitch->low;
  int32_t high = insn->tableswitch->high;
  int32_t *offsets = insn->tableswitch->targets;
  if (index < low || index > high) {
    int delta = (int32_t)(insn->tableswitch->default_target - 1) - (int32_t)pc;
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

static int64_t lookupswitch_impl_int(ARGS_INT) {

  DEBUG_CHECK
  struct bjvm_bc_lookupswitch_data data = *insn->lookupswitch;

  int32_t key = (int32_t)tos;
  int32_t *keys = data.keys;
  int32_t *offsets = data.targets;
  int32_t n = data.keys_count;
  int32_t default_target = data.default_target;
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
  static int64_t which##_impl_int(ARGS_INT) {                                                                          \
    DEBUG_CHECK;                                                                                                       \
    uint16_t old_pc = pc;                                                                                              \
    pc = ((int32_t)tos op 0) ? insn->index : (pc + 1);                                                                 \
    insns += (int32_t)pc - (int32_t)old_pc;                                                                            \
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
  static int64_t which##_impl_int(ARGS_INT) {                                                                          \
    DEBUG_CHECK                                                                                                        \
    int64_t a = (sp - 2)->i, b = (int)tos;                                                                             \
    uint16_t old_pc = pc;                                                                                              \
    pc = a op b ? insn->index : (pc + 1);                                                                              \
    insns += (int32_t)pc - (int32_t)old_pc;                                                                            \
    sp -= 2;                                                                                                           \
    STACK_POLYMORPHIC_JMP(*(sp - 1));                                                                                  \
  }

MAKE_INT_BRANCH(if_icmpeq, ==)
MAKE_INT_BRANCH(if_icmpne, !=)
MAKE_INT_BRANCH(if_icmplt, <)
MAKE_INT_BRANCH(if_icmpge, >=)
MAKE_INT_BRANCH(if_icmpgt, >)
MAKE_INT_BRANCH(if_icmple, <=)

static int64_t if_acmpeq_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t a = (sp - 2)->l, b = tos;
  int old_pc = pc;
  pc = a == b ? (insn->index - 1) : pc;
  insns += pc - old_pc;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}

static int64_t if_acmpne_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int64_t a = (sp - 2)->l, b = tos;
  uint16_t old_pc = pc;
  pc = a != b ? (insn->index - 1) : pc;
  insns += pc - old_pc;
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}

/** Monitors */

// TODO actually implement this stuff
static int64_t monitorenter_impl_int(ARGS_INT) {

  DEBUG_CHECK
  if (unlikely(!tos)) {
    SPILL(tos);
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t monitorexit_impl_int(ARGS_INT) {

  DEBUG_CHECK
  if (unlikely(!tos)) {
    SPILL(tos);
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

/** New object creation */

static int64_t new_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  SPILL_VOID

  bjvm_cp_class_info *info = &insn->cp->class_info;
  int error = bjvm_resolve_class(thread, info);
  if (error)
    return 0;

  if (insn->cp->class_info.classdesc->state < BJVM_CD_STATE_INITIALIZED) {
    bjvm_initialize_class_t init = {};
    init.args.thread = thread;
    init.args.classdesc = insn->cp->class_info.classdesc;
    future_t fut = bjvm_initialize_class(&init);
    assert(fut.status == FUTURE_READY); // for now
    if (thread->current_exception)
      return 0;
  }

  insn->kind = bjvm_insn_new_resolved;
  insn->classdesc = info->classdesc;

  JMP_VOID
}
FORWARD_TO_NULLARY(new)

static int64_t new_resolved_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  SPILL_VOID
  bjvm_obj_header *obj = new_object(thread, insn->classdesc);
  if (!obj)
    return 0;

  sp++;
  NEXT_INT(obj)
}
FORWARD_TO_NULLARY(new_resolved)

static int64_t newarray_impl_int(ARGS_INT) {
  DEBUG_CHECK
  int count = tos;
  SPILL(tos)
  if (unlikely(count < 0)) {
    bjvm_negative_array_size_exception(thread, count);
    return 0;
  }
  bjvm_obj_header *array = CreatePrimitiveArray1D(thread, insn->array_type, count);
  if (unlikely(!array)) {
    return 0; // oom
  }
  NEXT_INT(array)
}

static int64_t anewarray_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  if (bjvm_resolve_class(thread, info)) {
    return 0;
  }
  assert(info->classdesc);
  if (bjvm_link_class(thread, info->classdesc)) {
    return 0;
  }
  insn->classdesc = info->classdesc;
  insn->kind = bjvm_insn_anewarray_resolved;
  JMP_INT(tos)
}

// <length> -> <object>
static int64_t anewarray_resolved_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int count = tos;
  SPILL(tos)
  if (count < 0) {
    bjvm_negative_array_size_exception(thread, count);
    return 0;
  }
  bjvm_obj_header *array = CreateObjectArray1D(thread, insn->classdesc, count);
  if (array) {
    NEXT_INT(array)
  }
  return 0; // oom
}

static int64_t multianewarray_impl_int(ARGS_INT) {
  DEBUG_CHECK
  SPILL(tos)
  uint16_t temp_sp = sp - frame_stack(frame);
  if (bjvm_multianewarray(thread, &frame->plain, insn->multianewarray, &temp_sp))
    return 0;

  sp = frame_stack(frame) + temp_sp;
  NEXT_INT(frame_stack(frame)[temp_sp - 1].obj)
}

/** Method invocations */

static int intrinsify(bjvm_bytecode_insn *inst) {
  bjvm_cp_method *method = inst->ic;
  if (utf8_equals(hslc(method->my_class->name), "java/lang/Math")) {
    if (utf8_equals(method->name, "sqrt") && utf8_equals(method->unparsed_descriptor, "(D)D")) {
      inst->kind = bjvm_insn_dsqrt;
      return 1;
    }
  }
  return 0;
}

DECLARE_ASYNC(int, resolve_invokestatic,
              locals(),
              arguments(bjvm_thread *thread; bjvm_bytecode_insn *insn_),
              invoked_method(resolve_methodref)
);

DEFINE_ASYNC(resolve_invokestatic) {
  AWAIT(resolve_methodref, self->args.thread, &self->args.insn_->cp->methodref);
  if (self->args.thread->current_exception) {
    ASYNC_RETURN(-1);
  }

  bjvm_cp_method_info *info = &self->args.insn_->cp->methodref;
  self->args.insn_->kind = bjvm_insn_invokestatic_resolved;
  self->args.insn_->ic = info->resolved;
  self->args.insn_->args = info->descriptor->args_count;

  ASYNC_END(0);
};

__attribute__((noinline)) static int64_t invokestatic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_cp_method_info *info = &insn->cp->methodref;

  SPILL_VOID
  resolve_invokestatic_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.insn_ = insn;
  future_t fut = resolve_invokestatic(&ctx);
  if (thread->current_exception)
    return 0;

  info = &insn->cp->methodref;
  insn->kind = bjvm_insn_invokestatic_resolved;
  insn->ic = info->resolved;
  insn->args = info->descriptor->args_count;

  if (intrinsify(insn)) {
    STACK_POLYMORPHIC_JMP(*(sp - 1));
  }

  JMP_VOID
}
FORWARD_TO_NULLARY(invokestatic)

static inline uint8_t attempt_invoke(bjvm_thread *thread, bjvm_stack_frame *invoked_frame,
                                     bjvm_stack_frame *outer_frame, uint8_t argc, bool returns,
                                     bjvm_stack_value *result) {
  future_t fut;
  bjvm_stack_value result_ = bjvm_interpret_2(&fut, thread, invoked_frame);
  if (unlikely(fut.status == FUTURE_NOT_READY)) {
    assert(invoked_frame->is_async_suspended);
    continuation_frame *cont = async_stack_push(thread);
    *cont = (continuation_frame){.pnt = CONT_INVOKE,
                                 .wakeup = fut.wakeup,
                                 .ctx.interp_call = {.frame = invoked_frame, .argc = argc, .returns = returns}};
    outer_frame->is_async_suspended = true;
    return 1;
  }
  *result = result_;

  return 0;
}

#define AttemptInvoke(thread, invoked_frame, argc, returns)                                                            \
  ({                                                                                                                   \
    bjvm_stack_value result;                                                                                           \
    if (unlikely(attempt_invoke(thread, invoked_frame, frame, argc, returns, &result))) {                              \
      return 0;                                                                                                        \
    }                                                                                                                  \
    result;                                                                                                            \
  })

__attribute__((always_inline)) static int64_t invokestatic_resolved_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_cp_method *method = insn->ic;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  bjvm_stack_frame *invoked_frame;
  SPILL_VOID
  if (method->is_signature_polymorphic) {
    invoked_frame = bjvm_push_native_frame(thread, method, insn->cp->methodref.descriptor, sp - insn->args, insn->args);
  } else {
    invoked_frame = bjvm_push_frame(thread, method, sp - insn->args, insn->args);
  }
  if (unlikely(!invoked_frame)) {
    return 0;
  }

  bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

__attribute__((noinline)) static int64_t invokevirtual_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  bjvm_obj_header *target = (sp - argc)->obj;

  SPILL_VOID
  if (!target) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  assert(fut.status == FUTURE_READY);
  if (thread->current_exception) {
    return 0;
  }
  method_info = &insn->cp->methodref;

  // If we found a signature-polymorphic method, transmogrify into a bjvm_insn_invokesigpoly
  if (method_info->resolved->is_signature_polymorphic) {
    insn->kind = bjvm_insn_invokesigpoly;
    insn->ic = method_info->resolved;
    insn->ic2 = bjvm_resolve_method_type(thread, method_info->descriptor);

    arrput(frame->method->my_class->sigpoly_insns, insn); // so GC can move around ic2

    if (unlikely(!insn->ic)) {
      // todo: linkage error
      UNREACHABLE();
    }

    JMP_VOID
  }

  // If we found an interface method, transmogrify into a invokeinterface
  if (method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE) {
    insn->kind = bjvm_insn_invokeinterface;
    JMP_VOID
  }

  insn->kind = bjvm_insn_invokevtable_monomorphic;
  insn->ic = bjvm_vtable_lookup(target->descriptor, method_info->resolved->vtable_index);
  insn->ic2 = target->descriptor;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokevirtual)

__attribute__((noinline)) static int64_t invokespecial_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  bjvm_obj_header *target = (sp - argc)->obj;
  SPILL_VOID
  if (!target) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  assert(fut.status == FUTURE_READY);
  if (thread->current_exception) {
    return 0;
  }

  method_info = &insn->cp->methodref;
  bjvm_classdesc *lookup_on = method_info->resolved->my_class;
  bjvm_cp_method *method = frame->method;

  // "If all of the following are true, let C [the class to look up the
  // method upon] be the direct superclass of the current class:
  //
  // The resolved method is not an instance initialization method;
  // If the symbolic reference names a class (not an interface), then that
  // class is a superclass of the current class."
  if (!utf8_equals(method_info->resolved->name, "<init>") &&
      (!(lookup_on->access_flags & BJVM_ACCESS_INTERFACE) &&
       (method->my_class != lookup_on && bjvm_instanceof(method->my_class, lookup_on)))) {
    lookup_on = method->my_class->super_class->classdesc;
  }

  // Look at the class and its superclasses first
  bjvm_cp_method *candidate = bjvm_method_lookup(lookup_on, method_info->resolved->name,
                                                 method_info->resolved->unparsed_descriptor, true, false);
  if (!candidate) {
    // Then perform an itable lookup (I rly don't think this is correct...)
    if (method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE) {
      candidate = bjvm_itable_lookup(lookup_on, method_info->resolved->my_class, method_info->resolved->itable_index);
    }
    if (!candidate) {
      bjvm_abstract_method_error(thread, method_info->resolved);
      return 0;
    }
  } else if (candidate->access_flags & BJVM_ACCESS_ABSTRACT) {
    bjvm_abstract_method_error(thread, candidate);
    return 0;
  }

  // If this is the <init> method of Object, make it a nop
  if (utf8_equals(hslc(candidate->my_class->name), "java/lang/Object") && utf8_equals(candidate->name, "<init>")) {
    insn->kind = bjvm_insn_pop;
  } else {
    insn->kind = bjvm_insn_invokespecial_resolved;
    insn->ic = candidate;
  }
  JMP_VOID
}
FORWARD_TO_NULLARY(invokespecial)

__attribute__((always_inline)) static int64_t invokespecial_resolved_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  SPILL_VOID
  if (target == nullptr) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  bjvm_cp_method *target_method = insn->ic;
  bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

__attribute__((noinline)) static int64_t invokeinterface_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_cp_method_info *method_info = &insn->cp->methodref;
  int argc = insn->args = method_info->descriptor->args_count + 1;
  bjvm_obj_header *target = (sp - argc)->obj;
  SPILL_VOID
  if (!target) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  resolve_methodref_t ctx = {};
  ctx.args.thread = thread;
  ctx.args.info = &insn->cp->methodref;
  future_t fut = resolve_methodref(&ctx);
  assert(fut.status == FUTURE_READY);
  if (thread->current_exception)
    return 0;

  method_info = &insn->cp->methodref;
  if (!(method_info->resolved->my_class->access_flags & BJVM_ACCESS_INTERFACE)) {
    insn->kind = bjvm_insn_invokevirtual;
    JMP_VOID
  }
  insn->ic =
      bjvm_itable_lookup(target->descriptor, method_info->resolved->my_class, method_info->resolved->itable_index);
  insn->ic2 = target->descriptor;
  if (!insn->ic) {
    bjvm_abstract_method_error(thread, method_info->resolved);
    return 0;
  }
  insn->kind = bjvm_insn_invokeitable_monomorphic;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokeinterface)

__attribute__((noinline)) void make_invokevtable_polymorphic_(bjvm_bytecode_insn *inst) {
  assert(inst->kind == bjvm_insn_invokevtable_monomorphic);
  bjvm_cp_method *method = inst->ic;
  assert(method);
  inst->kind = bjvm_insn_invokevtable_polymorphic;
  inst->ic2 = (void *)method->vtable_index;
}

__attribute__((noinline)) void make_invokeitable_polymorphic_(bjvm_bytecode_insn *inst) {
  assert(inst->kind == bjvm_insn_invokeitable_monomorphic);
  inst->kind = bjvm_insn_invokeitable_polymorphic;
  inst->ic = (void *)inst->cp->methodref.resolved->my_class;
  inst->ic2 = (void *)inst->cp->methodref.resolved->itable_index;
}

__attribute__((always_inline)) static int64_t invokeitable_vtable_monomorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  SPILL_VOID
  if (target == nullptr) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  if (unlikely(target->descriptor != insn->ic2)) {
    if (insn->kind == bjvm_insn_invokevtable_monomorphic)
      make_invokevtable_polymorphic_(insn);
    else
      make_invokeitable_polymorphic_(insn);
    JMP_VOID
  }
  bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, insn->ic, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

__attribute__((always_inline)) static int64_t invokesigpoly_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  SPILL_VOID
  if (target == nullptr) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }

  bjvm_invokevirtual_signature_polymorphic_t ctx = {
      .args = {
          .thread = thread, .method = insn->ic, .provider_mt = insn->ic2, .sp_ = sp - insn->args, .target = target}};

  future_t fut = bjvm_invokevirtual_signature_polymorphic(&ctx);
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

static int64_t invokeitable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  SPILL_VOID
  if (target == nullptr) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  bjvm_cp_method *target_method = bjvm_itable_lookup(target->descriptor, insn->ic, (int)insn->ic2);
  if (unlikely(!target_method)) {
    bjvm_abstract_method_error(thread, insn->cp->methodref.resolved);
    return 0;
  }
  assert(target_method);
  bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

static int64_t invokevtable_polymorphic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  bjvm_obj_header *target = (sp - insn->args)->obj;
  bool returns = insn->cp->methodref.descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID;
  SPILL_VOID
  if (target == nullptr) {
    bjvm_null_pointer_exception(thread);
    return 0;
  }
  bjvm_cp_method *target_method = bjvm_vtable_lookup(target->descriptor, (int)insn->ic2);
  assert(target_method);
  bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, target_method, sp - insn->args, insn->args);
  if (!invoked_frame)
    return 0;

  bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

__attribute__((noinline)) static int64_t invokedynamic_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  SPILL_VOID

  bjvm_cp_indy_info *indy = &insn->cp->indy_info;
  indy_resolve_t ctx = {};
  ctx.args.thread = thread;
#undef insn
  ctx.args.insn =
#define insn (&insns[0])
      insn;
  ctx.args.indy = indy;
  future_t fut = indy_resolve(&ctx);
  if (thread->current_exception) {
    return 0;
  }

  assert(insn->ic);
  insn->kind = bjvm_insn_invokecallsite;
  struct bjvm_native_CallSite *cs = insn->ic;
  struct bjvm_native_MethodHandle *mh = (void *)cs->target;
  struct bjvm_native_LambdaForm *form = (void *)mh->form;
  struct bjvm_native_MemberName *name = (void *)form->vmentry;
  insn->args = form->arity;
  JMP_VOID
}
FORWARD_TO_NULLARY(invokedynamic)

static int64_t invokecallsite_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  // Call the "vmtarget" method with the correct number of arguments
  struct bjvm_native_CallSite *cs = insn->ic;
  struct bjvm_native_MethodHandle *mh = (void *)cs->target;
  struct bjvm_native_LambdaForm *form = (void *)mh->form;
  struct bjvm_native_MemberName *name = (void *)form->vmentry;

  bjvm_method_handle_kind kind = (name->flags >> 24) & 0xf;
  SPILL_VOID
  if (kind == BJVM_MH_KIND_INVOKE_STATIC) {
    bool returns = form->result != -1;

    // Invoke name->vmtarget with arguments mh, args
    bjvm_cp_method *invoke = name->vmtarget;

    // State of the stack:
    // ... args
    //   ^------^ length insn->args - 1
    // Want prepend an mh:
    // [mh] ... args
    // ^------------^ length insn->args
    bjvm_stack_value *arguments = sp - insn->args + 1;

    memmove(arguments + 1, arguments, (insn->args - 1) * sizeof(bjvm_stack_value));
    arguments[0] = (bjvm_stack_value){.obj = (void *)mh}; // MethodHandle

    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, invoke, arguments, insn->args);
    if (!invoked_frame) {
      return 0;
    }

    bjvm_stack_value result = AttemptInvoke(thread, invoked_frame, insn->args, returns);

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

__attribute__((always_inline)) bjvm_stack_value *get_local(bjvm_stack_frame *frame, bjvm_bytecode_insn *inst) {
  return frame_locals(frame) + inst->index;
}

/** Local variable accessors */
__attribute__((always_inline)) static int64_t iload_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  sp++;
  NEXT_INT(get_local(frame, insn)->i)
}
FORWARD_TO_NULLARY(iload)

__attribute__((always_inline)) static int64_t fload_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  sp++;
  NEXT_FLOAT(get_local(frame, insn)->f)
}
FORWARD_TO_NULLARY(fload)

__attribute__((always_inline)) static int64_t dload_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_DOUBLE(get_local(frame, insn)->d)
}
FORWARD_TO_NULLARY(dload)

__attribute__((always_inline)) static int64_t lload_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_INT(get_local(frame, insn)->l)
}
FORWARD_TO_NULLARY(lload)

__attribute__((always_inline)) static int64_t aload_impl_void(ARGS_VOID) {
  DEBUG_CHECK
  sp++;
  NEXT_INT(get_local(frame, insn)->obj)
}
FORWARD_TO_NULLARY(aload)

__attribute__((always_inline)) static int64_t astore_impl_int(ARGS_INT) {
  DEBUG_CHECK
  get_local(frame, insn)->obj = (bjvm_obj_header *)tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t istore_impl_int(ARGS_INT) {
  DEBUG_CHECK
  get_local(frame, insn)->i = (int)tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t fstore_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK
  get_local(frame, insn)->f = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t dstore_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK
  get_local(frame, insn)->d = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t lstore_impl_int(ARGS_INT) {

  DEBUG_CHECK
  get_local(frame, insn)->l = tos;
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}

static int64_t iinc_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_VOID
}

static int64_t iinc_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_DOUBLE(tos)
}

static int64_t iinc_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_FLOAT(tos)
}

static int64_t iinc_impl_int(ARGS_INT) {

  DEBUG_CHECK
  int *a = &frame_locals(frame)[insn->iinc.index].i;
  __builtin_add_overflow(*a, insn->iinc.const_, a);
  NEXT_INT(tos)
}

/** Constant-pushing instructions */

static int64_t aconst_null_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_INT((int64_t)0)
}
FORWARD_TO_NULLARY(aconst_null)

static int64_t ldc_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  bjvm_cp_entry *ent = insn->cp;
  switch (ent->kind) {
  case BJVM_CP_KIND_INTEGER: {
    NEXT_INT((int64_t)ent->integral.value);
  }
  case BJVM_CP_KIND_FLOAT: {
    NEXT_FLOAT((float)ent->floating.value);
  }
  case BJVM_CP_KIND_CLASS: {
    // Initialize the class, then get its Java mirror
    SPILL_VOID
    if (bjvm_resolve_class(thread, &ent->class_info))
      return 0;
    if (bjvm_link_class(thread, ent->class_info.classdesc))
      return 0;

    bjvm_obj_header *obj = (void *)bjvm_get_class_mirror(thread, ent->class_info.classdesc);
    NEXT_INT(obj);
  }
  case BJVM_CP_KIND_STRING: {
    bjvm_utf8 s = ent->string.chars;
    SPILL_VOID
    bjvm_obj_header *obj = bjvm_intern_string(thread, s);
    if (!obj) // oom
      return 0;

    NEXT_INT(obj);
  }
  default:
    UNREACHABLE();
  }
}
FORWARD_TO_NULLARY(ldc)

static int64_t ldc2_w_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  bjvm_cp_entry *ent = insn->cp;
  switch (ent->kind) {
  case BJVM_CP_KIND_DOUBLE: {
    NEXT_DOUBLE(ent->floating.value);
  }
  case BJVM_CP_KIND_LONG: {
    NEXT_INT(ent->integral.value);
  }
  default:
    UNREACHABLE();
  }
}
FORWARD_TO_NULLARY(ldc2_w)

static int64_t iconst_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_INT(insn->integer_imm)
}
FORWARD_TO_NULLARY(iconst)

static int64_t fconst_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_FLOAT(insn->f_imm);
}
FORWARD_TO_NULLARY(fconst)

static int64_t dconst_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_DOUBLE(insn->d_imm);
}
FORWARD_TO_NULLARY(dconst)

static int64_t lconst_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp++;
  NEXT_INT(insn->integer_imm);
}
FORWARD_TO_NULLARY(lconst)

/** Stack manipulation instructions */

static int64_t pop_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp--;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(pop)

static int64_t pop2_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  sp -= 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(pop2)

// Never directly called
static int64_t swap_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value tmp = *(sp - 1);
  *(sp - 1) = *(sp - 2);
  *(sp - 2) = tmp;
  STACK_POLYMORPHIC_NEXT(*(sp - 1));
}
FORWARD_TO_NULLARY(swap)

static int64_t nop_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  NEXT_VOID
}

static int64_t nop_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK
  NEXT_DOUBLE(tos)
}

static int64_t nop_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK
  NEXT_FLOAT(tos)
}

static int64_t nop_impl_int(ARGS_INT) {

  DEBUG_CHECK
  NEXT_INT(tos)
}

static int64_t dup_impl_int(ARGS_INT) {
  DEBUG_CHECK(sp++ - 1)->l = tos;
  NEXT_INT(tos)
}

static int64_t dup_impl_float(ARGS_FLOAT) {

  DEBUG_CHECK(sp++ - 1)->f = tos;
  NEXT_FLOAT(tos)
}

static int64_t dup_impl_double(ARGS_DOUBLE) {

  DEBUG_CHECK(sp++ - 1)->d = tos;
  NEXT_DOUBLE(tos)
}

// Rare enough (especially after analysis phase) that we might as well just have the one implementation and forward
// every TOS type to them.

// ..., val2, val1 -> ..., val2, val1, val2, val1
static int64_t dup2_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value val1 = *(sp - 1), val2 = *(sp - 2);
  *(sp) = val2;
  *(sp + 1) = val1;
  sp += 2;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup2)

// ..., val2, val1 -> ..., val1, val2, val1
static int64_t dup_x1_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value val1 = *(sp - 1), val2 = *(sp - 2);
  *(sp - 2) = val1;
  *(sp - 1) = val2;
  *(sp) = val1;
  sp++;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup_x1)

// ..., val3, val2, val1 -> val1, val3, val2, val1
static int64_t dup_x2_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3);
  *(sp - 3) = val1;
  *(sp - 2) = val3;
  *(sp - 1) = val2;
  *(sp) = val1;
  sp++;
  STACK_POLYMORPHIC_NEXT(*(sp - 1))
}
FORWARD_TO_NULLARY(dup_x2)

// ..., val3, val2, val1 -> ..., val2, val1, val3, val2, val1
static int64_t dup2_x1_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3);
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
static int64_t dup2_x2_impl_void(ARGS_VOID) {

  DEBUG_CHECK
  bjvm_stack_value val1 = *(sp - 1), val2 = *(sp - 2), val3 = *(sp - 3), val4 = *(sp - 4);
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

__attribute__((always_inline)) static int64_t entry(ARGS_VOID) { STACK_POLYMORPHIC_JMP(*(sp - 1)) }

/** Misc. */
static int64_t athrow_impl_int(ARGS_INT) {
  DEBUG_CHECK
  SPILL(tos)
  thread->current_exception = (bjvm_obj_header *)tos;
  return 0;
}

static int64_t checkcast_impl_int(ARGS_INT) {
  DEBUG_CHECK
  bjvm_cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = bjvm_resolve_class(thread, info);
  if (error)
    return 0;

  RELOAD(tos)
  insn->classdesc = info->classdesc;
  insn->kind = bjvm_insn_checkcast_resolved;
  JMP_INT(tos)
}

static int64_t checkcast_resolved_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_obj_header *obj = (bjvm_obj_header *)tos;
  if (obj && unlikely(!bjvm_instanceof(obj->descriptor, insn->classdesc))) {
    INIT_STACK_STRING(complaint, 1000);
    complaint = bprintf(complaint, "Expected instance of %.*s, got %.*s", fmt_slice(insn->classdesc->name),
                        fmt_slice(obj->descriptor->name));
    SPILL(tos)
    bjvm_raise_vm_exception(thread, STR("java/lang/ClassCastException"), complaint);
    return 0;
  }
  NEXT_INT(tos)
}

static int64_t instanceof_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_cp_class_info *info = &insn->cp->class_info;
  SPILL(tos)
  int error = bjvm_resolve_class(thread, info);
  if (error)
    return 0;

  RELOAD(tos)
  insn->classdesc = info->classdesc;
  insn->kind = bjvm_insn_instanceof_resolved;
  JMP_INT(tos)
}

static int64_t instanceof_resolved_impl_int(ARGS_INT) {

  DEBUG_CHECK
  bjvm_obj_header *obj = (bjvm_obj_header *)tos;
  int result = obj ? bjvm_instanceof(obj->descriptor, insn->classdesc) : 0;
  NEXT_INT(result)
}

static int64_t sqrt_impl_double(ARGS_DOUBLE) {
  DEBUG_CHECK
  NEXT_DOUBLE(sqrt(tos))
}

static bjvm_exception_table_entry *find_exception_handler(bjvm_thread *thread, bjvm_stack_frame *frame,
                                                          bjvm_classdesc *exception_type) {
  assert(!frame->is_native);

  bjvm_attribute_exception_table *table = frame->method->code->exception_table;
  if (!table)
    return nullptr;

  uint16_t const pc = frame->plain.program_counter;

  for (uint16_t i = 0; i < table->entries_count; ++i) {
    bjvm_exception_table_entry *ent = &table->entries[i];

    if (ent->start_insn <= pc && pc < ent->end_insn) {
      if (ent->catch_type) {
        int error = bjvm_resolve_class(thread, ent->catch_type);
        if (error)
          continue; // can happen if the current classloader != verifier classloader?
      }

      if (!ent->catch_type || bjvm_instanceof(exception_type, ent->catch_type->classdesc)) {
        if (ent->catch_type)
          assert(ent->catch_type->classdesc->state >= BJVM_CD_STATE_INITIALIZED);

        return ent;
      }
    }
  }

  return nullptr;
}

// NOLINTNEXTLINE(misc-no-recursion)
static int64_t async_resume(ARGS_VOID) {
  // we need to pop (not peek) because if we re-enter this method, it'll need to pop its fram
  continuation_frame cont = *async_stack_pop(thread);
  bjvm_stack_value result;
  future_t fut;

  bool has_result = false;
  bool advance_pc = false;

  switch (cont.pnt) {
  case CONT_GETSTATIC:
  case CONT_PUTSTATIC:
    fut = resolve_getstatic_putstatic(&cont.ctx.resolve_getstatic_putstatic);
    break;

  case CONT_INVOKE:
    result = bjvm_interpret_2(&fut, thread, cont.ctx.interp_call.frame);
    sp -= cont.ctx.interp_call.argc; // todo: wrong union member
    has_result = cont.ctx.interp_call.returns;
    break;

  case CONT_INVOKESIGPOLY:
    fut = bjvm_invokevirtual_signature_polymorphic(&cont.ctx.sigpoly);
    sp -= cont.ctx.interp_call.argc; // todo: wrong union member
    has_result = cont.ctx.interp_call.returns;
    break;

  default:
    UNREACHABLE();
  }

  if (fut.status == FUTURE_NOT_READY) {
    cont.wakeup = fut.wakeup;
    *async_stack_push(thread) = cont;
    return 0;
  }

  if (unlikely(thread->current_exception)) {
    return 0;
  }

  frame->is_async_suspended = false;

  if (has_result) {
    *sp++ = result;
  }

  if (advance_pc) {
    STACK_POLYMORPHIC_NEXT(*(sp - 1));
  } else {
    JMP_VOID;
  }
}

// NOLINTNEXTLINE(misc-no-recursion)
bjvm_stack_value bjvm_interpret_2(future_t *fut, bjvm_thread *thread, bjvm_stack_frame *frame_) {
  bjvm_stack_value result;

  // Handle native frames
  if (bjvm_is_frame_native(frame_)) {
    bjvm_run_native_t ctx = {};
    ctx.args.thread = thread;
    ctx.args.frame = frame_;
    *fut = bjvm_run_native(&ctx);
    assert(fut->status == FUTURE_READY); // todo: fix
    bjvm_pop_frame(thread, frame_);
    return ctx._result;
  }

  do {
  interpret_begin:
    bjvm_plain_frame *frame = bjvm_get_plain_frame(frame_);
    bjvm_stack_value *sp_ = &frame->stack[stack_depth(frame_)];
    uint16_t pc_ = frame->program_counter;
    bjvm_bytecode_insn *insns = frame_->method->code->code;

    if (unlikely(frame_->is_async_suspended)) {
      result.l = async_resume(thread, frame_, insns + pc_, pc_, sp_, 0, 0, 0);
    } else {
      result.l = entry(thread, frame_, insns + pc_, pc_, sp_, 0, 0, 0);
    }

    // we really should just have all the methods return a future_t via a pointer, but whatever
    if (unlikely(frame_->is_async_suspended)) {
      // reconstruct future to return
      async_wakeup_info *wk = async_stack_top(thread);
      *fut = (future_t){FUTURE_NOT_READY, wk};
      return (bjvm_stack_value){0};
    }

    if (unlikely(thread->current_exception)) {
      bjvm_exception_table_entry *handler =
          find_exception_handler(thread, frame_, thread->current_exception->descriptor);

      if (handler) {
        frame->program_counter = handler->handler_insn;
        frame->stack[0] = (bjvm_stack_value){.obj = thread->current_exception};
        thread->current_exception = nullptr;

        goto interpret_begin;
      }
    }
  } while (0);

  bjvm_pop_frame(thread, frame_);
  *fut = (future_t){FUTURE_READY};
  return result;
}

/** Jump table definitions. Must be kept in sync with the enum order. */

#define PAGE_ALIGN _Alignas(4096)

PAGE_ALIGN static int64_t (*jmp_table_void[MAX_INSN_KIND])(ARGS_VOID) = {
    nop_impl_void,
    nullptr /* aaload_impl_void */,
    nullptr /* aastore_impl_void */,
    aconst_null_impl_void,
    nullptr /* areturn_impl_void */,
    nullptr /* arraylength_impl_void */,
    nullptr /* athrow_impl_void */,
    nullptr /* baload_impl_void */,
    nullptr /* bastore_impl_void */,
    nullptr /* caload_impl_void */,
    nullptr /* castore_impl_void */,
    nullptr /* d2f_impl_void */,
    nullptr /* d2i_impl_void */,
    nullptr /* d2l_impl_void */,
    nullptr /* dadd_impl_void */,
    nullptr /* daload_impl_void */,
    nullptr /* dastore_impl_void */,
    nullptr /* dcmpg_impl_void */,
    nullptr /* dcmpl_impl_void */,
    nullptr /* ddiv_impl_void */,
    nullptr /* dmul_impl_void */,
    nullptr /* dneg_impl_void */,
    nullptr /* drem_impl_void */,
    nullptr /* dreturn_impl_void */,
    nullptr /* dsub_impl_void */,
    nullptr /* dup_impl_void */,
    nullptr /* dup_x1_impl_void */,
    nullptr /* dup_x2_impl_void */,
    nullptr /* dup2_impl_void */,
    nullptr /* dup2_x1_impl_void */,
    nullptr /* dup2_x2_impl_void */,
    nullptr /* f2d_impl_void */,
    nullptr /* f2i_impl_void */,
    nullptr /* f2l_impl_void */,
    nullptr /* fadd_impl_void */,
    nullptr /* faload_impl_void */,
    nullptr /* fastore_impl_void */,
    nullptr /* fcmpg_impl_void */,
    nullptr /* fcmpl_impl_void */,
    nullptr /* fdiv_impl_void */,
    nullptr /* fmul_impl_void */,
    nullptr /* fneg_impl_void */,
    nullptr /* frem_impl_void */,
    nullptr /* freturn_impl_void */,
    nullptr /* fsub_impl_void */,
    nullptr /* i2b_impl_void */,
    nullptr /* i2c_impl_void */,
    nullptr /* i2d_impl_void */,
    nullptr /* i2f_impl_void */,
    nullptr /* i2l_impl_void */,
    nullptr /* i2s_impl_void */,
    nullptr /* iadd_impl_void */,
    nullptr /* iaload_impl_void */,
    nullptr /* iand_impl_void */,
    nullptr /* iastore_impl_void */,
    nullptr /* idiv_impl_void */,
    nullptr /* imul_impl_void */,
    nullptr /* ineg_impl_void */,
    nullptr /* ior_impl_void */,
    nullptr /* irem_impl_void */,
    nullptr /* ireturn_impl_void */,
    nullptr /* ishl_impl_void */,
    nullptr /* ishr_impl_void */,
    nullptr /* isub_impl_void */,
    nullptr /* iushr_impl_void */,
    nullptr /* ixor_impl_void */,
    nullptr /* l2d_impl_void */,
    nullptr /* l2f_impl_void */,
    nullptr /* l2i_impl_void */,
    nullptr /* ladd_impl_void */,
    nullptr /* laload_impl_void */,
    nullptr /* land_impl_void */,
    nullptr /* lastore_impl_void */,
    nullptr /* lcmp_impl_void */,
    nullptr /* ldiv_impl_void */,
    nullptr /* lmul_impl_void */,
    nullptr /* lneg_impl_void */,
    nullptr /* lor_impl_void */,
    nullptr /* lrem_impl_void */,
    nullptr /* lreturn_impl_void */,
    nullptr /* lshl_impl_void */,
    nullptr /* lshr_impl_void */,
    nullptr /* lsub_impl_void */,
    nullptr /* lushr_impl_void */,
    nullptr /* lxor_impl_void */,
    nullptr /* monitorenter_impl_void */,
    nullptr /* monitorexit_impl_void */,
    pop_impl_void,
    nullptr /* pop2_impl_void */,
    return_impl_void,
    nullptr /* saload_impl_void */,
    nullptr /* sastore_impl_void */,
    nullptr /* swap_impl_void */,
    nullptr /* anewarray_impl_void */,
    nullptr /* checkcast_impl_void */,
    nullptr /* getfield_impl_void */,
    getstatic_impl_void,
    nullptr /* instanceof_impl_void */,
    invokedynamic_impl_void,
    new_impl_void,
    nullptr /* putfield_impl_void */,
    nullptr /* putstatic_impl_void */,
    invokevirtual_impl_void,
    nullptr /* invokespecial_impl_void */,
    invokestatic_impl_void,
    ldc_impl_void,
    ldc2_w_impl_void,
    dload_impl_void,
    fload_impl_void,
    iload_impl_void,
    lload_impl_void,
    nullptr /* dstore_impl_void */,
    nullptr /* fstore_impl_void */,
    nullptr /* istore_impl_void */,
    nullptr /* lstore_impl_void */,
    aload_impl_void,
    nullptr /* astore_impl_void */,
    goto_impl_void,
    nullptr /* jsr_impl_void */,
    nullptr /* if_acmpeq_impl_void */,
    nullptr /* if_acmpne_impl_void */,
    nullptr /* if_icmpeq_impl_void */,
    nullptr /* if_icmpne_impl_void */,
    nullptr /* if_icmplt_impl_void */,
    nullptr /* if_icmpge_impl_void */,
    nullptr /* if_icmpgt_impl_void */,
    nullptr /* if_icmple_impl_void */,
    nullptr /* ifeq_impl_void */,
    nullptr /* ifne_impl_void */,
    nullptr /* iflt_impl_void */,
    nullptr /* ifge_impl_void */,
    nullptr /* ifgt_impl_void */,
    nullptr /* ifle_impl_void */,
    nullptr /* ifnonnull_impl_void */,
    nullptr /* ifnull_impl_void */,
    iconst_impl_void,
    dconst_impl_void,
    fconst_impl_void,
    lconst_impl_void,
    iinc_impl_void,
    invokeinterface_impl_void,
    nullptr /* multianewarray_impl_void */,
    nullptr /* newarray_impl_void */,
    nullptr /* tableswitch_impl_void */,
    nullptr /* lookupswitch_impl_void */,
    nullptr /* ret_impl_void */,
    nullptr /* anewarray_resolved_impl_void */,
    nullptr /* checkcast_resolved_impl_void */,
    nullptr /* instanceof_resolved_impl_void */,
    new_resolved_impl_void,
    invokeitable_vtable_monomorphic_impl_void,
    invokevtable_polymorphic_impl_void,
    invokeitable_vtable_monomorphic_impl_void,
    invokeitable_polymorphic_impl_void,
    invokespecial_resolved_impl_void,
    invokestatic_resolved_impl_void,
    invokecallsite_impl_void,
    invokesigpoly_impl_void,
    nullptr /* getfield_B_impl_void */,
    nullptr /* getfield_C_impl_void */,
    nullptr /* getfield_S_impl_void */,
    nullptr /* getfield_I_impl_void */,
    nullptr /* getfield_J_impl_void */,
    nullptr /* getfield_F_impl_void */,
    nullptr /* getfield_D_impl_void */,
    nullptr /* getfield_Z_impl_void */,
    nullptr /* getfield_L_impl_void */,
    nullptr /* putfield_B_impl_void */,
    nullptr /* putfield_C_impl_void */,
    nullptr /* putfield_S_impl_void */,
    nullptr /* putfield_I_impl_void */,
    nullptr /* putfield_J_impl_void */,
    nullptr /* putfield_F_impl_void */,
    nullptr /* putfield_D_impl_void */,
    nullptr /* putfield_Z_impl_void */,
    nullptr /* putfield_L_impl_void */,
    getstatic_B_impl_void,
    getstatic_C_impl_void,
    getstatic_S_impl_void,
    getstatic_I_impl_void,
    getstatic_J_impl_void,
    getstatic_F_impl_void,
    getstatic_D_impl_void,
    getstatic_Z_impl_void,
    getstatic_L_impl_void,
    nullptr /* putstatic_B_impl_void */,
    nullptr /* putstatic_C_impl_void */,
    nullptr /* putstatic_S_impl_void */,
    nullptr /* putstatic_I_impl_void */,
    nullptr /* putstatic_J_impl_void */,
    nullptr /* putstatic_F_impl_void */,
    nullptr /* putstatic_D_impl_void */,
    nullptr /* putstatic_Z_impl_void */,
    nullptr /* putstatic_L_impl_void */,
};

PAGE_ALIGN static int64_t (*jmp_table_double[MAX_INSN_KIND])(ARGS_VOID) = {
    nop_impl_double,
    nullptr /* aaload_impl_double */,
    nullptr /* aastore_impl_double */,
    aconst_null_impl_double,
    nullptr /* areturn_impl_double */,
    nullptr /* arraylength_impl_double */,
    nullptr /* athrow_impl_double */,
    nullptr /* baload_impl_double */,
    nullptr /* bastore_impl_double */,
    nullptr /* caload_impl_double */,
    nullptr /* castore_impl_double */,
    d2f_impl_double,
    d2i_impl_double,
    d2l_impl_double,
    dadd_impl_double,
    nullptr /* daload_impl_double */,
    dastore_impl_double,
    dcmpg_impl_double,
    dcmpl_impl_double,
    ddiv_impl_double,
    dmul_impl_double,
    dneg_impl_double,
    nullptr /* drem_impl_double */,
    dreturn_impl_double,
    dsub_impl_double,
    dup_impl_double,
    dup_x1_impl_double,
    dup_x2_impl_double,
    dup2_impl_double,
    dup2_x1_impl_double,
    dup2_x2_impl_double,
    nullptr /* f2d_impl_double */,
    nullptr /* f2i_impl_double */,
    nullptr /* f2l_impl_double */,
    nullptr /* fadd_impl_double */,
    nullptr /* faload_impl_double */,
    nullptr /* fastore_impl_double */,
    nullptr /* fcmpg_impl_double */,
    nullptr /* fcmpl_impl_double */,
    nullptr /* fdiv_impl_double */,
    nullptr /* fmul_impl_double */,
    nullptr /* fneg_impl_double */,
    nullptr /* frem_impl_double */,
    nullptr /* freturn_impl_double */,
    nullptr /* fsub_impl_double */,
    nullptr /* i2b_impl_double */,
    nullptr /* i2c_impl_double */,
    nullptr /* i2d_impl_double */,
    nullptr /* i2f_impl_double */,
    nullptr /* i2l_impl_double */,
    nullptr /* i2s_impl_double */,
    nullptr /* iadd_impl_double */,
    nullptr /* iaload_impl_double */,
    nullptr /* iand_impl_double */,
    nullptr /* iastore_impl_double */,
    nullptr /* idiv_impl_double */,
    nullptr /* imul_impl_double */,
    nullptr /* ineg_impl_double */,
    nullptr /* ior_impl_double */,
    nullptr /* irem_impl_double */,
    nullptr /* ireturn_impl_double */,
    nullptr /* ishl_impl_double */,
    nullptr /* ishr_impl_double */,
    nullptr /* isub_impl_double */,
    nullptr /* iushr_impl_double */,
    nullptr /* ixor_impl_double */,
    nullptr /* l2d_impl_double */,
    nullptr /* l2f_impl_double */,
    nullptr /* l2i_impl_double */,
    nullptr /* ladd_impl_double */,
    nullptr /* laload_impl_double */,
    nullptr /* land_impl_double */,
    nullptr /* lastore_impl_double */,
    nullptr /* lcmp_impl_double */,
    nullptr /* ldiv_impl_double */,
    nullptr /* lmul_impl_double */,
    nullptr /* lneg_impl_double */,
    nullptr /* lor_impl_double */,
    nullptr /* lrem_impl_double */,
    nullptr /* lreturn_impl_double */,
    nullptr /* lshl_impl_double */,
    nullptr /* lshr_impl_double */,
    nullptr /* lsub_impl_double */,
    nullptr /* lushr_impl_double */,
    nullptr /* lxor_impl_double */,
    nullptr /* monitorenter_impl_double */,
    nullptr /* monitorexit_impl_double */,
    pop_impl_double,
    pop2_impl_double,
    return_impl_double,
    nullptr /* saload_impl_double */,
    nullptr /* sastore_impl_double */,
    swap_impl_double,
    nullptr /* anewarray_impl_double */,
    nullptr /* checkcast_impl_double */,
    nullptr /* getfield_impl_double */,
    getstatic_impl_double,
    nullptr /* instanceof_impl_double */,
    invokedynamic_impl_double,
    new_impl_double,
    putfield_impl_double,
    putstatic_impl_double,
    invokevirtual_impl_double,
    invokespecial_impl_double,
    invokestatic_impl_double,
    ldc_impl_double,
    ldc2_w_impl_double,
    dload_impl_double,
    fload_impl_double,
    iload_impl_double,
    lload_impl_double,
    dstore_impl_double,
    nullptr /* fstore_impl_double */,
    nullptr /* istore_impl_double */,
    nullptr /* lstore_impl_double */,
    aload_impl_double,
    nullptr /* astore_impl_double */,
    goto_impl_double,
    nullptr /* jsr_impl_double */,
    nullptr /* if_acmpeq_impl_double */,
    nullptr /* if_acmpne_impl_double */,
    nullptr /* if_icmpeq_impl_double */,
    nullptr /* if_icmpne_impl_double */,
    nullptr /* if_icmplt_impl_double */,
    nullptr /* if_icmpge_impl_double */,
    nullptr /* if_icmpgt_impl_double */,
    nullptr /* if_icmple_impl_double */,
    nullptr /* ifeq_impl_double */,
    nullptr /* ifne_impl_double */,
    nullptr /* iflt_impl_double */,
    nullptr /* ifge_impl_double */,
    nullptr /* ifgt_impl_double */,
    nullptr /* ifle_impl_double */,
    nullptr /* ifnonnull_impl_double */,
    nullptr /* ifnull_impl_double */,
    iconst_impl_double,
    dconst_impl_double,
    fconst_impl_double,
    lconst_impl_double,
    iinc_impl_double,
    invokeinterface_impl_double,
    nullptr /* multianewarray_impl_double */,
    nullptr /* newarray_impl_double */,
    nullptr /* tableswitch_impl_double */,
    nullptr /* lookupswitch_impl_double */,
    nullptr /* ret_impl_double */,
    nullptr /* anewarray_resolved_impl_double */,
    nullptr /* checkcast_resolved_impl_double */,
    nullptr /* instanceof_resolved_impl_double */,
    new_resolved_impl_double,
    invokeitable_vtable_monomorphic_impl_double,
    invokevtable_polymorphic_impl_double,
    invokeitable_vtable_monomorphic_impl_double,
    invokeitable_polymorphic_impl_double,
    invokespecial_resolved_impl_double,
    invokestatic_resolved_impl_double,
    invokecallsite_impl_double,
    invokesigpoly_impl_double,
    nullptr /* getfield_B_impl_double */,
    nullptr /* getfield_C_impl_double */,
    nullptr /* getfield_S_impl_double */,
    nullptr /* getfield_I_impl_double */,
    nullptr /* getfield_J_impl_double */,
    nullptr /* getfield_F_impl_double */,
    nullptr /* getfield_D_impl_double */,
    nullptr /* getfield_Z_impl_double */,
    nullptr /* getfield_L_impl_double */,
    nullptr /* putfield_B_impl_double */,
    nullptr /* putfield_C_impl_double */,
    nullptr /* putfield_S_impl_double */,
    nullptr /* putfield_I_impl_double */,
    nullptr /* putfield_J_impl_double */,
    nullptr /* putfield_F_impl_double */,
    putfield_D_impl_double,
    nullptr /* putfield_Z_impl_double */,
    nullptr /* putfield_L_impl_double */,
    getstatic_B_impl_double,
    getstatic_C_impl_double,
    getstatic_S_impl_double,
    getstatic_I_impl_double,
    getstatic_J_impl_double,
    getstatic_F_impl_double,
    getstatic_D_impl_double,
    getstatic_Z_impl_double,
    getstatic_L_impl_double,
    nullptr /* putstatic_B_impl_double */,
    nullptr /* putstatic_C_impl_double */,
    nullptr /* putstatic_S_impl_double */,
    nullptr /* putstatic_I_impl_double */,
    nullptr /* putstatic_J_impl_double */,
    nullptr /* putstatic_F_impl_double */,
    putstatic_D_impl_double,
    nullptr /* putstatic_Z_impl_double */,
    nullptr, /* putstatic_L_impl_double */
    sqrt_impl_double};

PAGE_ALIGN static int64_t (*jmp_table_int[MAX_INSN_KIND])(ARGS_VOID) = {nop_impl_int,
                                                                        aaload_impl_int,
                                                                        aastore_impl_int,
                                                                        aconst_null_impl_int,
                                                                        areturn_impl_int,
                                                                        arraylength_impl_int,
                                                                        athrow_impl_int,
                                                                        baload_impl_int,
                                                                        bastore_impl_int,
                                                                        caload_impl_int,
                                                                        castore_impl_int,
                                                                        nullptr /* d2f_impl_int */,
                                                                        nullptr /* d2i_impl_int */,
                                                                        nullptr /* d2l_impl_int */,
                                                                        nullptr /* dadd_impl_int */,
                                                                        daload_impl_int,
                                                                        nullptr /* dastore_impl_int */,
                                                                        nullptr /* dcmpg_impl_int */,
                                                                        nullptr /* dcmpl_impl_int */,
                                                                        nullptr /* ddiv_impl_int */,
                                                                        nullptr /* dmul_impl_int */,
                                                                        nullptr /* dneg_impl_int */,
                                                                        nullptr /* drem_impl_int */,
                                                                        nullptr /* dreturn_impl_int */,
                                                                        nullptr /* dsub_impl_int */,
                                                                        dup_impl_int,
                                                                        dup_x1_impl_int,
                                                                        dup_x2_impl_int,
                                                                        dup2_impl_int,
                                                                        dup2_x1_impl_int,
                                                                        dup2_x2_impl_int,
                                                                        nullptr /* f2d_impl_int */,
                                                                        nullptr /* f2i_impl_int */,
                                                                        nullptr /* f2l_impl_int */,
                                                                        nullptr /* fadd_impl_int */,
                                                                        faload_impl_int,
                                                                        nullptr /* fastore_impl_int */,
                                                                        nullptr /* fcmpg_impl_int */,
                                                                        nullptr /* fcmpl_impl_int */,
                                                                        nullptr /* fdiv_impl_int */,
                                                                        nullptr /* fmul_impl_int */,
                                                                        nullptr /* fneg_impl_int */,
                                                                        nullptr /* frem_impl_int */,
                                                                        nullptr /* freturn_impl_int */,
                                                                        nullptr /* fsub_impl_int */,
                                                                        i2b_impl_int,
                                                                        i2c_impl_int,
                                                                        i2d_impl_int,
                                                                        i2f_impl_int,
                                                                        i2l_impl_int,
                                                                        i2s_impl_int,
                                                                        iadd_impl_int,
                                                                        iaload_impl_int,
                                                                        iand_impl_int,
                                                                        iastore_impl_int,
                                                                        idiv_impl_int,
                                                                        imul_impl_int,
                                                                        ineg_impl_int,
                                                                        ior_impl_int,
                                                                        irem_impl_int,
                                                                        ireturn_impl_int,
                                                                        ishl_impl_int,
                                                                        ishr_impl_int,
                                                                        isub_impl_int,
                                                                        iushr_impl_int,
                                                                        ixor_impl_int,
                                                                        l2d_impl_int,
                                                                        l2f_impl_int,
                                                                        l2i_impl_int,
                                                                        ladd_impl_int,
                                                                        laload_impl_int,
                                                                        land_impl_int,
                                                                        lastore_impl_int,
                                                                        lcmp_impl_int,
                                                                        ldiv_impl_int,
                                                                        lmul_impl_int,
                                                                        lneg_impl_int,
                                                                        lor_impl_int,
                                                                        lrem_impl_int,
                                                                        lreturn_impl_int,
                                                                        lshl_impl_int,
                                                                        lshr_impl_int,
                                                                        lsub_impl_int,
                                                                        lushr_impl_int,
                                                                        lxor_impl_int,
                                                                        monitorenter_impl_int,
                                                                        monitorexit_impl_int,
                                                                        pop_impl_int,
                                                                        pop2_impl_int,
                                                                        return_impl_int,
                                                                        saload_impl_int,
                                                                        sastore_impl_int,
                                                                        swap_impl_int,
                                                                        anewarray_impl_int,
                                                                        checkcast_impl_int,
                                                                        getfield_impl_int,
                                                                        getstatic_impl_int,
                                                                        instanceof_impl_int,
                                                                        invokedynamic_impl_int,
                                                                        new_impl_int,
                                                                        putfield_impl_int,
                                                                        putstatic_impl_int,
                                                                        invokevirtual_impl_int,
                                                                        invokespecial_impl_int,
                                                                        invokestatic_impl_int,
                                                                        ldc_impl_int,
                                                                        ldc2_w_impl_int,
                                                                        dload_impl_int,
                                                                        fload_impl_int,
                                                                        iload_impl_int,
                                                                        lload_impl_int,
                                                                        nullptr /* dstore_impl_int */,
                                                                        nullptr /* fstore_impl_int */,
                                                                        istore_impl_int,
                                                                        lstore_impl_int,
                                                                        aload_impl_int,
                                                                        astore_impl_int,
                                                                        goto_impl_int,
                                                                        nullptr /* jsr_impl_int */,
                                                                        if_acmpeq_impl_int,
                                                                        if_acmpne_impl_int,
                                                                        if_icmpeq_impl_int,
                                                                        if_icmpne_impl_int,
                                                                        if_icmplt_impl_int,
                                                                        if_icmpge_impl_int,
                                                                        if_icmpgt_impl_int,
                                                                        if_icmple_impl_int,
                                                                        ifeq_impl_int,
                                                                        ifne_impl_int,
                                                                        iflt_impl_int,
                                                                        ifge_impl_int,
                                                                        ifgt_impl_int,
                                                                        ifle_impl_int,
                                                                        ifnonnull_impl_int,
                                                                        ifnull_impl_int,
                                                                        iconst_impl_int,
                                                                        dconst_impl_int,
                                                                        fconst_impl_int,
                                                                        lconst_impl_int,
                                                                        iinc_impl_int,
                                                                        invokeinterface_impl_int,
                                                                        multianewarray_impl_int,
                                                                        newarray_impl_int,
                                                                        tableswitch_impl_int,
                                                                        lookupswitch_impl_int,
                                                                        nullptr /* ret_impl_int */,
                                                                        anewarray_resolved_impl_int,
                                                                        checkcast_resolved_impl_int,
                                                                        instanceof_resolved_impl_int,
                                                                        new_resolved_impl_int,
                                                                        invokeitable_vtable_monomorphic_impl_int,
                                                                        invokevtable_polymorphic_impl_int,
                                                                        invokeitable_vtable_monomorphic_impl_int,
                                                                        invokeitable_polymorphic_impl_int,
                                                                        invokespecial_resolved_impl_int,
                                                                        invokestatic_resolved_impl_int,
                                                                        invokecallsite_impl_int,
                                                                        invokesigpoly_impl_int,
                                                                        getfield_B_impl_int,
                                                                        getfield_C_impl_int,
                                                                        getfield_S_impl_int,
                                                                        getfield_I_impl_int,
                                                                        getfield_J_impl_int,
                                                                        getfield_F_impl_int,
                                                                        getfield_D_impl_int,
                                                                        getfield_Z_impl_int,
                                                                        getfield_L_impl_int,
                                                                        putfield_B_impl_int,
                                                                        putfield_C_impl_int,
                                                                        putfield_S_impl_int,
                                                                        putfield_I_impl_int,
                                                                        putfield_J_impl_int,
                                                                        nullptr /* putfield_F_impl_int */,
                                                                        nullptr /* putfield_D_impl_int */,
                                                                        putfield_Z_impl_int,
                                                                        putfield_L_impl_int,
                                                                        getstatic_B_impl_int,
                                                                        getstatic_C_impl_int,
                                                                        getstatic_S_impl_int,
                                                                        getstatic_I_impl_int,
                                                                        getstatic_J_impl_int,
                                                                        getstatic_F_impl_int,
                                                                        getstatic_D_impl_int,
                                                                        getstatic_Z_impl_int,
                                                                        getstatic_L_impl_int,
                                                                        putstatic_B_impl_int,
                                                                        putstatic_C_impl_int,
                                                                        putstatic_S_impl_int,
                                                                        putstatic_I_impl_int,
                                                                        putstatic_J_impl_int,
                                                                        nullptr /* putstatic_F_impl_int */,
                                                                        nullptr /* putstatic_D_impl_int */,
                                                                        putstatic_Z_impl_int,
                                                                        putstatic_L_impl_int,
                                                                        nullptr};

PAGE_ALIGN static int64_t (*jmp_table_float[MAX_INSN_KIND])(ARGS_VOID) = {nop_impl_float,
                                                                          nullptr /* aaload_impl_float */,
                                                                          nullptr /* aastore_impl_float */,
                                                                          aconst_null_impl_float,
                                                                          nullptr /* areturn_impl_float */,
                                                                          nullptr /* arraylength_impl_float */,
                                                                          nullptr /* athrow_impl_float */,
                                                                          nullptr /* baload_impl_float */,
                                                                          nullptr /* bastore_impl_float */,
                                                                          nullptr /* caload_impl_float */,
                                                                          nullptr /* castore_impl_float */,
                                                                          nullptr /* d2f_impl_float */,
                                                                          nullptr /* d2i_impl_float */,
                                                                          nullptr /* d2l_impl_float */,
                                                                          nullptr /* dadd_impl_float */,
                                                                          nullptr /* daload_impl_float */,
                                                                          nullptr /* dastore_impl_float */,
                                                                          nullptr /* dcmpg_impl_float */,
                                                                          nullptr /* dcmpl_impl_float */,
                                                                          nullptr /* ddiv_impl_float */,
                                                                          nullptr /* dmul_impl_float */,
                                                                          nullptr /* dneg_impl_float */,
                                                                          nullptr /* drem_impl_float */,
                                                                          nullptr /* dreturn_impl_float */,
                                                                          nullptr /* dsub_impl_float */,
                                                                          dup_impl_float,
                                                                          dup_x1_impl_float,
                                                                          dup_x2_impl_float,
                                                                          dup2_impl_float,
                                                                          dup2_x1_impl_float,
                                                                          dup2_x2_impl_float,
                                                                          f2d_impl_float,
                                                                          f2i_impl_float,
                                                                          f2l_impl_float,
                                                                          fadd_impl_float,
                                                                          nullptr /* faload_impl_float */,
                                                                          fastore_impl_float,
                                                                          fcmpg_impl_float,
                                                                          fcmpl_impl_float,
                                                                          fdiv_impl_float,
                                                                          fmul_impl_float,
                                                                          fneg_impl_float,
                                                                          nullptr /* frem_impl_float */,
                                                                          freturn_impl_float,
                                                                          fsub_impl_float,
                                                                          nullptr /* i2b_impl_float */,
                                                                          nullptr /* i2c_impl_float */,
                                                                          nullptr /* i2d_impl_float */,
                                                                          nullptr /* i2f_impl_float */,
                                                                          nullptr /* i2l_impl_float */,
                                                                          nullptr /* i2s_impl_float */,
                                                                          nullptr /* iadd_impl_float */,
                                                                          nullptr /* iaload_impl_float */,
                                                                          nullptr /* iand_impl_float */,
                                                                          nullptr /* iastore_impl_float */,
                                                                          nullptr /* idiv_impl_float */,
                                                                          nullptr /* imul_impl_float */,
                                                                          nullptr /* ineg_impl_float */,
                                                                          nullptr /* ior_impl_float */,
                                                                          nullptr /* irem_impl_float */,
                                                                          nullptr /* ireturn_impl_float */,
                                                                          nullptr /* ishl_impl_float */,
                                                                          nullptr /* ishr_impl_float */,
                                                                          nullptr /* isub_impl_float */,
                                                                          nullptr /* iushr_impl_float */,
                                                                          nullptr /* ixor_impl_float */,
                                                                          nullptr /* l2d_impl_float */,
                                                                          nullptr /* l2f_impl_float */,
                                                                          nullptr /* l2i_impl_float */,
                                                                          nullptr /* ladd_impl_float */,
                                                                          nullptr /* laload_impl_float */,
                                                                          nullptr /* land_impl_float */,
                                                                          nullptr /* lastore_impl_float */,
                                                                          nullptr /* lcmp_impl_float */,
                                                                          nullptr /* ldiv_impl_float */,
                                                                          nullptr /* lmul_impl_float */,
                                                                          nullptr /* lneg_impl_float */,
                                                                          nullptr /* lor_impl_float */,
                                                                          nullptr /* lrem_impl_float */,
                                                                          nullptr /* lreturn_impl_float */,
                                                                          nullptr /* lshl_impl_float */,
                                                                          nullptr /* lshr_impl_float */,
                                                                          nullptr /* lsub_impl_float */,
                                                                          nullptr /* lushr_impl_float */,
                                                                          nullptr /* lxor_impl_float */,
                                                                          nullptr /* monitorenter_impl_float */,
                                                                          nullptr /* monitorexit_impl_float */,
                                                                          pop_impl_float,
                                                                          pop2_impl_float,
                                                                          return_impl_float,
                                                                          nullptr /* saload_impl_float */,
                                                                          nullptr /* sastore_impl_float */,
                                                                          swap_impl_float,
                                                                          nullptr /* anewarray_impl_float */,
                                                                          nullptr /* checkcast_impl_float */,
                                                                          nullptr /* getfield_impl_float */,
                                                                          getstatic_impl_float,
                                                                          nullptr /* instanceof_impl_float */,
                                                                          invokedynamic_impl_float,
                                                                          new_impl_float,
                                                                          putfield_impl_float,
                                                                          putstatic_impl_float,
                                                                          invokevirtual_impl_float,
                                                                          invokespecial_impl_float,
                                                                          invokestatic_impl_float,
                                                                          ldc_impl_float,
                                                                          ldc2_w_impl_float,
                                                                          dload_impl_float,
                                                                          fload_impl_float,
                                                                          iload_impl_float,
                                                                          lload_impl_float,
                                                                          nullptr /* dstore_impl_float */,
                                                                          fstore_impl_float,
                                                                          nullptr /* istore_impl_float */,
                                                                          nullptr /* lstore_impl_float */,
                                                                          aload_impl_float,
                                                                          nullptr /* astore_impl_float */,
                                                                          goto_impl_float,
                                                                          nullptr /* jsr_impl_float */,
                                                                          nullptr /* if_acmpeq_impl_float */,
                                                                          nullptr /* if_acmpne_impl_float */,
                                                                          nullptr /* if_icmpeq_impl_float */,
                                                                          nullptr /* if_icmpne_impl_float */,
                                                                          nullptr /* if_icmplt_impl_float */,
                                                                          nullptr /* if_icmpge_impl_float */,
                                                                          nullptr /* if_icmpgt_impl_float */,
                                                                          nullptr /* if_icmple_impl_float */,
                                                                          nullptr /* ifeq_impl_float */,
                                                                          nullptr /* ifne_impl_float */,
                                                                          nullptr /* iflt_impl_float */,
                                                                          nullptr /* ifge_impl_float */,
                                                                          nullptr /* ifgt_impl_float */,
                                                                          nullptr /* ifle_impl_float */,
                                                                          nullptr /* ifnonnull_impl_float */,
                                                                          nullptr /* ifnull_impl_float */,
                                                                          iconst_impl_float,
                                                                          dconst_impl_float,
                                                                          fconst_impl_float,
                                                                          lconst_impl_float,
                                                                          iinc_impl_float,
                                                                          invokeinterface_impl_float,
                                                                          nullptr /* multianewarray_impl_float */,
                                                                          nullptr /* newarray_impl_float */,
                                                                          nullptr /* tableswitch_impl_float */,
                                                                          nullptr /* lookupswitch_impl_float */,
                                                                          nullptr /* ret_impl_float */,
                                                                          nullptr /* anewarray_resolved_impl_float */,
                                                                          nullptr /* checkcast_resolved_impl_float */,
                                                                          nullptr /* instanceof_resolved_impl_float */,
                                                                          new_resolved_impl_float,
                                                                          invokeitable_vtable_monomorphic_impl_float,
                                                                          invokevtable_polymorphic_impl_float,
                                                                          invokeitable_vtable_monomorphic_impl_float,
                                                                          invokeitable_polymorphic_impl_float,
                                                                          invokespecial_resolved_impl_float,
                                                                          invokestatic_resolved_impl_float,
                                                                          invokecallsite_impl_float,
                                                                          invokesigpoly_impl_float,
                                                                          nullptr /* getfield_B_impl_float */,
                                                                          nullptr /* getfield_C_impl_float */,
                                                                          nullptr /* getfield_S_impl_float */,
                                                                          nullptr /* getfield_I_impl_float */,
                                                                          nullptr /* getfield_J_impl_float */,
                                                                          nullptr /* getfield_F_impl_float */,
                                                                          nullptr /* getfield_D_impl_float */,
                                                                          nullptr /* getfield_Z_impl_float */,
                                                                          nullptr /* getfield_L_impl_float */,
                                                                          nullptr /* putfield_B_impl_float */,
                                                                          nullptr /* putfield_C_impl_float */,
                                                                          nullptr /* putfield_S_impl_float */,
                                                                          nullptr /* putfield_I_impl_float */,
                                                                          nullptr /* putfield_J_impl_float */,
                                                                          putfield_F_impl_float,
                                                                          nullptr /* putfield_D_impl_float */,
                                                                          nullptr /* putfield_Z_impl_float */,
                                                                          nullptr /* putfield_L_impl_float */,
                                                                          getstatic_B_impl_float,
                                                                          getstatic_C_impl_float,
                                                                          getstatic_S_impl_float,
                                                                          getstatic_I_impl_float,
                                                                          getstatic_J_impl_float,
                                                                          getstatic_F_impl_float,
                                                                          getstatic_D_impl_float,
                                                                          getstatic_Z_impl_float,
                                                                          getstatic_L_impl_float,
                                                                          nullptr /* putstatic_B_impl_float */,
                                                                          nullptr /* putstatic_C_impl_float */,
                                                                          nullptr /* putstatic_S_impl_float */,
                                                                          nullptr /* putstatic_I_impl_float */,
                                                                          nullptr /* putstatic_J_impl_float */,
                                                                          putstatic_F_impl_float,
                                                                          nullptr /* putstatic_D_impl_float */,
                                                                          nullptr /* putstatic_Z_impl_float */,
                                                                          nullptr /* putstatic_L_impl_float */,
                                                                          nullptr};