// Java bytecode analysis, rewriting (to make longs/doubles take up one local
// or stack slot), and mild verification.
//
// Long term I'd like this to be a full verifier, fuzzed against HotSpot.

#include <assert.h>
#include <stdlib.h>

#include "analysis.h"
#include "classfile.h"

#include <inttypes.h>
#include <limits.h>

typedef bjvm_type_kind bjvm_analy_stack_entry;

// State of the stack (or local variable table) during analysis, indexed after
// index swizzling (which occurs very early in our processing)
typedef struct {
  bjvm_analy_stack_entry *entries;
  int entries_count;
  int entries_cap;

  bool from_jump_target;
  bool is_exc_handler;

  int exc_handler_start;
} bjvm_analy_stack_state;

#define CASE(K)                                                                \
  case bjvm_insn_##K:                                                          \
    return #K;

const char *bjvm_insn_code_name(bjvm_insn_code_kind code) {
  switch (code) {
    CASE(aaload)
    CASE(aastore)
    CASE(aconst_null)
    CASE(areturn)
    CASE(arraylength)
    CASE(athrow)
    CASE(baload)
    CASE(bastore)
    CASE(caload)
    CASE(castore)
    CASE(d2f)
    CASE(d2i)
    CASE(d2l)
    CASE(dadd)
    CASE(daload)
    CASE(dastore)
    CASE(dcmpg)
    CASE(dcmpl)
    CASE(ddiv)
    CASE(dmul)
    CASE(dneg)
    CASE(drem)
    CASE(dreturn)
    CASE(dsub)
    CASE(dup)
    CASE(dup_x1)
    CASE(dup_x2)
    CASE(dup2)
    CASE(dup2_x1)
    CASE(dup2_x2)
    CASE(f2d)
    CASE(f2i)
    CASE(f2l)
    CASE(fadd)
    CASE(faload)
    CASE(fastore)
    CASE(fcmpg)
    CASE(fcmpl)
    CASE(fdiv)
    CASE(fmul)
    CASE(fneg)
    CASE(frem)
    CASE(freturn)
    CASE(fsub)
    CASE(i2b)
    CASE(i2c)
    CASE(i2d)
    CASE(i2f)
    CASE(i2l)
    CASE(i2s)
    CASE(iadd)
    CASE(iaload)
    CASE(iand)
    CASE(iastore)
    CASE(idiv)
    CASE(imul)
    CASE(ineg)
    CASE(ior)
    CASE(irem)
    CASE(ireturn)
    CASE(ishl)
    CASE(ishr)
    CASE(isub)
    CASE(iushr)
    CASE(ixor)
    CASE(l2d)
    CASE(l2f)
    CASE(l2i)
    CASE(ladd)
    CASE(laload)
    CASE(land)
    CASE(lastore)
    CASE(lcmp)
    CASE(ldc)
    CASE(ldc2_w)
    CASE(ldiv)
    CASE(lmul)
    CASE(lneg)
    CASE(lor)
    CASE(lrem)
    CASE(lreturn)
    CASE(lshl)
    CASE(lshr)
    CASE(lsub)
    CASE(lushr)
    CASE(lxor)
    CASE(monitorenter)
    CASE(monitorexit)
    CASE(nop)
    CASE(pop)
    CASE(pop2)
    CASE(return)
    CASE(saload)
    CASE(sastore)
    CASE(swap)
    CASE(dload)
    CASE(fload)
    CASE(iload)
    CASE(lload)
    CASE(dstore)
    CASE(fstore)
    CASE(istore)
    CASE(lstore)
    CASE(aload)
    CASE(astore)
    CASE(anewarray)
    CASE(checkcast)
    CASE(getfield)
    CASE(getstatic)
    CASE(instanceof)
    CASE(invokedynamic)
    CASE(new)
    CASE(putfield)
    CASE(putstatic)
    CASE(invokevirtual)
    CASE(invokespecial)
    CASE(invokestatic)
    CASE(goto)
    CASE(jsr)
    CASE(ret)
    CASE(if_acmpeq)
    CASE(if_acmpne)
    CASE(if_icmpeq)
    CASE(if_icmpne)
    CASE(if_icmplt)
    CASE(if_icmpge)
    CASE(if_icmpgt)
    CASE(if_icmple)
    CASE(ifeq)
    CASE(ifne)
    CASE(iflt)
    CASE(ifge)
    CASE(ifgt)
    CASE(ifle)
    CASE(ifnonnull)
    CASE(ifnull)
    CASE(iconst)
    CASE(dconst)
    CASE(fconst)
    CASE(lconst)
    CASE(iinc)
    CASE(invokeinterface)
    CASE(multianewarray)
    CASE(newarray)
    CASE(tableswitch)
    CASE(lookupswitch)
    CASE(invokevtable_monomorphic)
    CASE(invokevtable_polymorphic)
    CASE(invokeitable_monomorphic)
    CASE(invokeitable_polymorphic)
    CASE(invokespecial_resolved)
    CASE(getfield_B)
    CASE(getfield_C)
    CASE(getfield_S)
    CASE(getfield_I)
    CASE(getfield_J)
    CASE(getfield_F)
    CASE(getfield_D)
    CASE(getfield_L)
  }
  UNREACHABLE();
}

const char *bjvm_type_kind_to_string(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    return "boolean";
  case BJVM_TYPE_KIND_BYTE:
    return "byte";
  case BJVM_TYPE_KIND_CHAR:
    return "char";
  case BJVM_TYPE_KIND_SHORT:
    return "short";
  case BJVM_TYPE_KIND_INT:
    return "int";
  case BJVM_TYPE_KIND_LONG:
    return "long";
  case BJVM_TYPE_KIND_FLOAT:
    return "float";
  case BJVM_TYPE_KIND_DOUBLE:
    return "double";
  case BJVM_TYPE_KIND_VOID:
    return "void";
  case BJVM_TYPE_KIND_REFERENCE:
    return "<reference>";
  }
  UNREACHABLE();
}

char *class_info_entry_to_string(const bjvm_cp_class_info *ent) {
  char result[1000];
  snprintf(result, sizeof(result), "Class: %.*s", ent->name.len,
           ent->name.chars);
  return strdup(result);
}

char *
name_and_type_entry_to_string(const bjvm_cp_name_and_type *name_and_type) {
  char result[1000];
  snprintf(result, sizeof(result), "NameAndType: %.*s:%.*s",
           name_and_type->name.len, name_and_type->name.chars,
           name_and_type->descriptor.len, name_and_type->descriptor.chars);
  return strdup(result);
}

char *indy_entry_to_string(const bjvm_cp_indy_info *indy_info) {
  char *name_and_type = name_and_type_entry_to_string(
      indy_info->name_and_type); // TODO add bootstrap method
  return name_and_type;
}

/**
 * Convert the constant pool entry to an owned string.
 */
char *constant_pool_entry_to_string(const bjvm_cp_entry *ent) {
  char result[200];
  switch (ent->kind) {
  case BJVM_CP_KIND_INVALID:
    return strdup("<invalid>");
  case BJVM_CP_KIND_UTF8:
    return strndup(ent->utf8.chars, ent->utf8.len);
  case BJVM_CP_KIND_INTEGER:
    snprintf(result, sizeof(result), "%" PRId64, ent->integral.value);
    break;
  case BJVM_CP_KIND_FLOAT:
    snprintf(result, sizeof(result), "%.9gf", (float)ent->floating.value);
    break;
  case BJVM_CP_KIND_LONG:
    snprintf(result, sizeof(result), "%" PRId64 "L", ent->integral.value);
    break;
  case BJVM_CP_KIND_DOUBLE:
    snprintf(result, sizeof(result), "%.15gd", (float)ent->floating.value);
    break;
  case BJVM_CP_KIND_CLASS:
    return class_info_entry_to_string(&ent->class_info);
  case BJVM_CP_KIND_STRING: {
    snprintf(result, sizeof(result), "String: '%.*s'", ent->string.chars.len,
             ent->string.chars.chars);
    break;
  }
  case BJVM_CP_KIND_FIELD_REF: {
    char *class_name = class_info_entry_to_string(ent->field.class_info);
    char *field_name = name_and_type_entry_to_string(ent->field.nat);

    snprintf(result, sizeof(result), "FieldRef: %s.%s", class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case BJVM_CP_KIND_METHOD_REF:
  case BJVM_CP_KIND_INTERFACE_METHOD_REF: {
    char *class_name = class_info_entry_to_string(ent->field.class_info);
    char *field_name = name_and_type_entry_to_string(ent->field.nat);
    snprintf(result, sizeof(result), "%s: %s; %s",
             ent->kind == BJVM_CP_KIND_METHOD_REF ? "MethodRef"
                                                  : "InterfaceMethodRef",
             class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case BJVM_CP_KIND_NAME_AND_TYPE: {
    return name_and_type_entry_to_string(&ent->name_and_type);
  }
  case BJVM_CP_KIND_METHOD_HANDLE: {
    return strdup("<method handle>"); // TODO
  }
  case BJVM_CP_KIND_METHOD_TYPE: {
    return strdup("<method type>"); // TODO
  }
  case BJVM_CP_KIND_INVOKE_DYNAMIC:
    return indy_entry_to_string(&ent->indy_info);
  }
  return strdup(result);
}

heap_string insn_to_string(const bjvm_bytecode_insn *insn, int insn_index) {
  heap_string result = make_heap_str(10);
  int write = 0;
  write = build_str(&result, write, "%04d = pc %04d: ", insn_index,
                    insn->original_pc);
  write = build_str(&result, write, "%s ", bjvm_insn_code_name(insn->kind));
  if (insn->kind <= bjvm_insn_swap) {
    // no operands
  } else if (insn->kind <= bjvm_insn_ldc2_w) {
    // indexes into constant pool
    char *cp_str = constant_pool_entry_to_string(insn->cp);
    write = build_str(&result, write, "%s", cp_str);
    free(cp_str);
  } else if (insn->kind <= bjvm_insn_astore) {
    // indexes into local variables
    write = build_str(&result, write, "#%d", insn->index);
  } else if (insn->kind <= bjvm_insn_ifnull) {
    // indexes into the instruction array
    write = build_str(&result, write, "inst %d", insn->index);
  } else if (insn->kind == bjvm_insn_lconst || insn->kind == bjvm_insn_iconst) {
    write = build_str(&result, write, "%" PRId64, insn->integer_imm);
  } else if (insn->kind == bjvm_insn_dconst || insn->kind == bjvm_insn_fconst) {
    write = build_str(&result, write, "%.15g", insn->f_imm);
  } else if (insn->kind == bjvm_insn_tableswitch) {
    write = build_str(&result, write, "[ default -> %d",
                      insn->tableswitch.default_target);
    for (int i = 0, j = insn->tableswitch.low;
         i < insn->tableswitch.targets_count; ++i, ++j) {
      write = build_str(&result, write, ", %d -> %d", j,
                        insn->tableswitch.targets[i]);
    }
    write = build_str(&result, write, " ]");
  } else if (insn->kind == bjvm_insn_lookupswitch) {
    write = build_str(&result, write, "[ default -> %d",
                      insn->lookupswitch.default_target);
    for (int i = 0; i < insn->lookupswitch.targets_count; ++i) {
      write =
          build_str(&result, write, ", %d -> %d", insn->lookupswitch.keys[i],
                    insn->lookupswitch.targets[i]);
    }
    write = build_str(&result, write, " ]");
  } else {
    // TODO
  }
  return result;
}

heap_string code_attribute_to_string(const bjvm_attribute_code *attrib) {
  heap_string result = make_heap_str(1000);
  int write = 0;
  for (int i = 0; i < attrib->insn_count; ++i) {
    heap_string insn_str = insn_to_string(attrib->code + i, i);
    write = build_str(&result, write, "%.*s\n", fmt_slice(insn_str));
  }
  return result;
}

char *print_analy_stack_state(const bjvm_analy_stack_state *state) {
  char buf[1000], *end = buf + 1000;
  char *write = buf;
  write = stpncpy(write, "[ ", end - write);
  for (int i = 0; i < state->entries_count; ++i) {
    write = stpncpy(write, bjvm_type_kind_to_string(state->entries[i]),
                    end - write);
    if (i + 1 < state->entries_count)
      write = stpncpy(write, ", ", end - write);
  }
  write = stpncpy(write, " ]", end - write);
  return strdup(buf);
}

/**
 * Copy the stack state in st to the (possibly already allocated) stack state in
 * out.
 */
void copy_analy_stack_state(bjvm_analy_stack_state st,
                            bjvm_analy_stack_state *out) {
  if (out->entries_cap < st.entries_count || !out->entries) {
    out->entries_cap = st.entries_count + 2 /* we'll probably push more */;
    out->entries = realloc(out->entries,
                           out->entries_cap * sizeof(bjvm_analy_stack_entry));
    assert(out->entries);
  }
  memcpy(out->entries, st.entries,
         st.entries_count * sizeof(bjvm_analy_stack_entry));
  out->entries_count = st.entries_count;
}

char *expect_analy_stack_states_equal(bjvm_analy_stack_state a,
                                      bjvm_analy_stack_state b) {
  if (a.entries_count != b.entries_count)
    goto fail;
  for (int i = 0; i < a.entries_count; ++i) {
    if (a.entries[i] != b.entries[i]) {
      goto fail;
    }
  }
  return nullptr;
fail:;
  char *a_str = print_analy_stack_state(&a),
       *b_str = print_analy_stack_state(&b);
  char *buf = malloc(strlen(a_str) + strlen(b_str) + 128);
  snprintf(buf, strlen(a_str) + strlen(b_str) + 128,
           "Stack mismatch:\nPreviously inferred: %s\nFound: %s", a_str, b_str);
  free(a_str);
  free(b_str);
  return buf;
}

bool is_kind_wide(bjvm_type_kind kind) {
  return kind == BJVM_TYPE_KIND_LONG || kind == BJVM_TYPE_KIND_DOUBLE;
}

bool bjvm_is_field_wide(bjvm_field_descriptor desc) {
  return is_kind_wide(desc.base_kind) && !desc.dimensions;
}

bjvm_type_kind kind_to_representable_kind(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
    return BJVM_TYPE_KIND_INT;
  default:
    return kind;
  }
}

bjvm_type_kind field_to_kind(const bjvm_field_descriptor *field) {
  if (field->dimensions)
    return BJVM_TYPE_KIND_REFERENCE;
  return kind_to_representable_kind(field->base_kind);
}

void write_kinds_to_bitset(const bjvm_analy_stack_state *inferred_stack,
                           int offset,
                           bjvm_compressed_bitset *bjvm_compressed_bitset,
                           bjvm_type_kind test) {
  for (int i = 0; i < inferred_stack->entries_count; ++i) {
    if (inferred_stack->entries[i] == test)
      bjvm_test_set_compressed_bitset(bjvm_compressed_bitset, offset + i);
  }
}

int bjvm_locals_on_method_entry(const bjvm_cp_method *method,
                                bjvm_analy_stack_state *locals,
                                int **locals_swizzle) {
  const bjvm_attribute_code *code = method->code;
  const bjvm_method_descriptor *desc = method->descriptor;
  assert(code);
  uint16_t max_locals = code->max_locals;
  locals->entries = calloc(max_locals, sizeof(bjvm_analy_stack_entry));
  *locals_swizzle = malloc(max_locals * sizeof(int));
  for (int i = 0; i < max_locals; ++i) {
    locals->entries[i] = BJVM_TYPE_KIND_VOID;
    (*locals_swizzle)[i] = -1;
  }
  int i = 0, j = 0;
  bool is_static = method->access_flags & BJVM_ACCESS_STATIC;
  if (!is_static) {
    // if the method is nonstatic, the first local is a reference 'this'
    if (max_locals == 0)
      goto fail;
    (*locals_swizzle)[0] = 0; // map 0 -> 0
    locals->entries[j++] = BJVM_TYPE_KIND_REFERENCE;
  }
  locals->entries_cap = locals->entries_count = max_locals;
  for (; i < desc->args_count && j < max_locals; ++i, ++j) {
    bjvm_field_descriptor arg = desc->args[i];
    int swizzled = i + !is_static;
    locals->entries[swizzled] = field_to_kind(&arg);
    // map nth local to nth argument if static, n+1th if nonstatic
    (*locals_swizzle)[j] = swizzled;
    if (bjvm_is_field_wide(arg)) {
      if (++j >= max_locals)
        goto fail;
    }
  }
  if (i != desc->args_count)
    goto fail;
  j = 0;
  // Map the rest of the locals
  for (; j < max_locals; ++j)
    if ((*locals_swizzle)[j] == -1)
      (*locals_swizzle)[j] = i++ + !is_static;
  return 0;

fail:
  free(locals->entries);
  free(*locals_swizzle);
  return -1;
}

struct edge {
  int start, end;
};

struct method_analysis_ctx {
  const bjvm_attribute_code *code;
  bjvm_analy_stack_state stack, stack_before, locals;
  bool stack_terminated;
  int *locals_swizzle;
  bjvm_analy_stack_state *inferred_stacks;
  bjvm_analy_stack_state *inferred_locals;
  int *branch_q;
  int branch_count;
  char *insn_error;
  heap_string *error;
  struct edge *edges;
  int edges_count;
  int edges_cap;
};

// Pop a value from the analysis stack and return it.
#define POP_VAL                                                                \
  ({                                                                           \
    if (ctx->stack.entries_count == 0)                                         \
      goto stack_underflow;                                                    \
    ctx->stack.entries[--ctx->stack.entries_count];                            \
  })
// Pop a value from the analysis stack and assert its kind.
#define POP_KIND(kind)                                                         \
  {                                                                            \
    bjvm_analy_stack_entry popped_kind = POP_VAL;                              \
    if (kind != popped_kind)                                                   \
      goto stack_type_mismatch;                                                \
  }
#define POP(kind) POP_KIND(BJVM_TYPE_KIND_##kind)
// Push a kind to the analysis stack.
#define PUSH_KIND(kind)                                                        \
  {                                                                            \
    if (ctx->stack.entries_count == ctx->stack.entries_cap)                    \
      goto stack_overflow;                                                     \
    if (kind != BJVM_TYPE_KIND_VOID)                                           \
      ctx->stack.entries[ctx->stack.entries_count++] =                         \
          kind_to_representable_kind(kind);                                    \
  }
#define PUSH(kind) PUSH_KIND(BJVM_TYPE_KIND_##kind)

// Set the kind of the local variable, in pre-swizzled indices.
#define SET_LOCAL(index, kind)                                                 \
  {                                                                            \
    if (index >= ctx->code->max_locals)                                        \
      goto local_overflow;                                                     \
    ctx->locals.entries[index] = BJVM_TYPE_KIND_##kind;                        \
  }
// Remap the index to the new local variable index after unwidening.
#define SWIZZLE_LOCAL(index)                                                   \
  {                                                                            \
    if (index >= ctx->code->max_locals)                                        \
      goto local_overflow;                                                     \
    index = ctx->locals_swizzle[index];                                        \
  }

#define CHECK_LOCAL(index, kind)                                               \
  {                                                                            \
    if (ctx->locals.entries[index] != BJVM_TYPE_KIND_##kind)                   \
      goto local_type_mismatch;                                                \
  }

int push_branch_target(struct method_analysis_ctx *ctx, uint32_t curr,
                       uint32_t target) {
  assert((int)target < ctx->code->insn_count);
  if (ctx->inferred_stacks[target].entries) {
    if ((ctx->insn_error = expect_analy_stack_states_equal(
             ctx->inferred_stacks[target], ctx->stack))) {
      return -1;
    }
  } else {
    copy_analy_stack_state(ctx->stack, &ctx->inferred_stacks[target]);
    ctx->inferred_stacks[target].from_jump_target = true;
    ctx->branch_q[ctx->branch_count++] = target;

    *VECTOR_PUSH(ctx->edges, ctx->edges_count, ctx->edges_cap) =
        (struct edge){.start = curr, .end = target};
  }
  return 0;
}

int analyze_instruction(bjvm_bytecode_insn *insn, int insn_index,
                        struct method_analysis_ctx *ctx) {
  switch (insn->kind) {
  case bjvm_insn_nop:
  case bjvm_insn_ret:
    break;
  case bjvm_insn_aaload:
    POP(INT) POP(REFERENCE) PUSH(REFERENCE) break;
  case bjvm_insn_aastore:
    POP(REFERENCE) POP(INT) POP(REFERENCE) break;
  case bjvm_insn_aconst_null:
    PUSH(REFERENCE) break;
  case bjvm_insn_areturn:
    POP(REFERENCE)
    ctx->stack_terminated = true;
    break;
  case bjvm_insn_arraylength:
    POP(REFERENCE) PUSH(INT) break;
  case bjvm_insn_athrow:
    POP(REFERENCE)
    ctx->stack_terminated = true;
    break;
  case bjvm_insn_baload:
  case bjvm_insn_caload:
  case bjvm_insn_saload:
  case bjvm_insn_iaload:
    POP(INT) POP(REFERENCE) PUSH(INT) break;
  case bjvm_insn_bastore:
  case bjvm_insn_castore:
  case bjvm_insn_sastore:
  case bjvm_insn_iastore:
    POP(INT) POP(INT) POP(REFERENCE) break;
  case bjvm_insn_d2f:
    POP(DOUBLE) PUSH(FLOAT) break;
  case bjvm_insn_d2i:
    POP(DOUBLE) PUSH(INT) break;
  case bjvm_insn_d2l:
    POP(DOUBLE) PUSH(LONG) break;
  case bjvm_insn_dadd:
  case bjvm_insn_ddiv:
  case bjvm_insn_dmul:
  case bjvm_insn_drem:
  case bjvm_insn_dsub:
    POP(DOUBLE) POP(DOUBLE) PUSH(DOUBLE) break;
  case bjvm_insn_daload:
    POP(INT) POP(REFERENCE) PUSH(DOUBLE) break;
  case bjvm_insn_dastore:
    POP(DOUBLE) POP(INT) POP(REFERENCE) break;
  case bjvm_insn_dcmpg:
  case bjvm_insn_dcmpl:
    POP(DOUBLE) POP(DOUBLE) PUSH(INT) break;
  case bjvm_insn_dneg:
    POP(DOUBLE) PUSH(DOUBLE) break;
  case bjvm_insn_dreturn:
    POP(DOUBLE)
    ctx->stack_terminated = true;
    break;
  case bjvm_insn_dup: {
    if (ctx->stack.entries_count == 0)
      goto stack_underflow;
    PUSH_KIND(ctx->stack.entries[ctx->stack.entries_count - 1])
    break;
  }
  case bjvm_insn_dup_x1: {
    if (ctx->stack.entries_count <= 1)
      goto stack_underflow;
    bjvm_type_kind kind1 = POP_VAL, kind2 = POP_VAL;
    if (is_kind_wide(kind1) || is_kind_wide(kind2))
      goto stack_type_mismatch;
    PUSH_KIND(kind1) PUSH_KIND(kind2) PUSH_KIND(kind1) break;
  }
  case bjvm_insn_dup_x2: {
    bjvm_type_kind to_dup = POP_VAL, kind2 = POP_VAL, kind3;
    if (is_kind_wide(to_dup))
      goto stack_type_mismatch;
    if (is_kind_wide(kind2)) {
      PUSH_KIND(to_dup) PUSH_KIND(kind2) insn->kind = bjvm_insn_dup_x1;
    } else {
      kind3 = POP_VAL;
      PUSH_KIND(to_dup) PUSH_KIND(kind3) PUSH_KIND(kind2)
    }
    PUSH_KIND(to_dup)
    break;
  }
  case bjvm_insn_dup2: {
    bjvm_type_kind to_dup = POP_VAL, kind2;
    if (is_kind_wide(to_dup)) {
      PUSH_KIND(to_dup) PUSH_KIND(to_dup) insn->kind = bjvm_insn_dup;
    } else {
      kind2 = POP_VAL;
      if (is_kind_wide(kind2))
        goto stack_type_mismatch;
      PUSH_KIND(kind2) PUSH_KIND(to_dup) PUSH_KIND(kind2) PUSH_KIND(to_dup)
    }
    break;
  }
  case bjvm_insn_dup2_x1: {
    bjvm_type_kind to_dup = POP_VAL, kind2 = POP_VAL, kind3;
    if (is_kind_wide(to_dup)) {
      PUSH_KIND(to_dup)
      PUSH_KIND(kind2) PUSH_KIND(to_dup) insn->kind = bjvm_insn_dup_x1;
    } else {
      kind3 = POP_VAL;
      if (is_kind_wide(kind3))
        goto stack_type_mismatch;
      PUSH_KIND(kind2)
      PUSH_KIND(to_dup) PUSH_KIND(kind3) PUSH_KIND(kind2) PUSH_KIND(to_dup)
    }
    break;
  }
  case bjvm_insn_dup2_x2: {
    bjvm_type_kind to_dup = POP_VAL, kind2 = POP_VAL, kind3, kind4;
    if (is_kind_wide(to_dup)) {
      if (is_kind_wide(kind2)) {
        PUSH_KIND(to_dup)
        PUSH_KIND(kind2) PUSH_KIND(to_dup) insn->kind = bjvm_insn_dup_x1;
      } else {
        kind3 = POP_VAL;
        if (is_kind_wide(kind3))
          goto stack_type_mismatch;
        PUSH_KIND(to_dup)
        PUSH_KIND(kind3)
        PUSH_KIND(kind2) PUSH_KIND(to_dup) insn->kind = bjvm_insn_dup_x2;
      }
    } else {
      kind3 = POP_VAL;
      if (is_kind_wide(kind3)) {
        PUSH_KIND(kind2)
        PUSH_KIND(to_dup)
        PUSH_KIND(kind3)
        PUSH_KIND(kind2) PUSH_KIND(to_dup) insn->kind = bjvm_insn_dup2_x1;
      } else {
        kind4 = POP_VAL;
        if (is_kind_wide(kind4))
          goto stack_type_mismatch;
        PUSH_KIND(kind2)
        PUSH_KIND(to_dup)
        PUSH_KIND(kind4) PUSH_KIND(kind3) PUSH_KIND(kind2) PUSH_KIND(to_dup)
      }
    }
    break;
  }
  case bjvm_insn_f2d: {
    POP(FLOAT) PUSH(DOUBLE) break;
  }
  case bjvm_insn_f2i: {
    POP(FLOAT) PUSH(INT) break;
  }
  case bjvm_insn_f2l: {
    POP(FLOAT) PUSH(LONG) break;
  }
  case bjvm_insn_fadd: {
    POP(FLOAT) POP(FLOAT) PUSH(FLOAT) break;
  }
  case bjvm_insn_faload: {
    POP(INT) POP(REFERENCE) PUSH(FLOAT) break;
  }
  case bjvm_insn_fastore: {
    POP(FLOAT) POP(INT) POP(REFERENCE) break;
  }
  case bjvm_insn_fcmpg:
  case bjvm_insn_fcmpl: {
    POP(FLOAT) POP(FLOAT) PUSH(INT) break;
  }
  case bjvm_insn_fdiv:
  case bjvm_insn_fmul:
  case bjvm_insn_frem:
  case bjvm_insn_fsub: {
    POP(FLOAT) POP(FLOAT) PUSH(FLOAT) break;
  }
  case bjvm_insn_fneg: {
    POP(FLOAT) PUSH(FLOAT);
    break;
  }
  case bjvm_insn_freturn: {
    POP(FLOAT)
    ctx->stack_terminated = true;
    break;
  }
  case bjvm_insn_i2b:
  case bjvm_insn_i2c: {
    POP(INT) PUSH(INT) break;
  }
  case bjvm_insn_i2d: {
    POP(INT) PUSH(DOUBLE) break;
  }
  case bjvm_insn_i2f: {
    POP(INT) PUSH(FLOAT) break;
  }
  case bjvm_insn_i2l: {
    POP(INT) PUSH(LONG) break;
  }
  case bjvm_insn_i2s: {
    POP(INT) PUSH(INT) break;
  }
  case bjvm_insn_iadd:
  case bjvm_insn_iand:
  case bjvm_insn_idiv:
  case bjvm_insn_imul:
  case bjvm_insn_irem:
  case bjvm_insn_ior:
  case bjvm_insn_ishl:
  case bjvm_insn_ishr:
  case bjvm_insn_isub:
  case bjvm_insn_ixor:
  case bjvm_insn_iushr: {
    POP(INT) POP(INT) PUSH(INT)
  } break;
  case bjvm_insn_ineg: {
    POP(INT) PUSH(INT) break;
  }
  case bjvm_insn_ireturn: {
    POP(INT);
    break;
  }
  case bjvm_insn_l2d: {
    POP(LONG) PUSH(DOUBLE) break;
  }
  case bjvm_insn_l2f: {
    POP(LONG) PUSH(FLOAT) break;
  }
  case bjvm_insn_l2i: {
    POP(LONG) PUSH(INT) break;
  }
  case bjvm_insn_ladd:
  case bjvm_insn_land:
  case bjvm_insn_ldiv:
  case bjvm_insn_lmul:
  case bjvm_insn_lor:
  case bjvm_insn_lrem:
  case bjvm_insn_lsub:
  case bjvm_insn_lxor: {
    POP(LONG) POP(LONG) PUSH(LONG) break;
  }
  case bjvm_insn_lshl:
  case bjvm_insn_lshr:
  case bjvm_insn_lushr: {
    POP(INT) POP(LONG) PUSH(LONG) break;
  }
  case bjvm_insn_laload: {
    POP(INT) POP(REFERENCE) PUSH(LONG) break;
  }
  case bjvm_insn_lastore: {
    POP(LONG) POP(INT) POP(REFERENCE) break;
  }
  case bjvm_insn_lcmp: {
    POP(LONG) POP(LONG) PUSH(INT) break;
  }
  case bjvm_insn_lneg: {
    POP(LONG) PUSH(LONG) break;
  }
  case bjvm_insn_lreturn: {
    POP(LONG)
    ctx->stack_terminated = true;
    break;
  }
  case bjvm_insn_monitorenter: {
    POP(REFERENCE)
    break;
  }
  case bjvm_insn_monitorexit: {
    POP(REFERENCE)
    break;
  }
  case bjvm_insn_pop: {
    bjvm_type_kind kind = POP_VAL;
    if (is_kind_wide(kind))
      goto stack_type_mismatch;
    break;
  }
  case bjvm_insn_pop2: {
    bjvm_type_kind kind = POP_VAL;
    if (!is_kind_wide(kind)) {
      bjvm_type_kind kind2 = POP_VAL;
      if (is_kind_wide(kind2))
        goto stack_type_mismatch;
    } else {
      insn->kind = bjvm_insn_pop;
    }
    break;
  }
  case bjvm_insn_return: {
    ctx->stack_terminated = true;
    break;
  }
  case bjvm_insn_swap: {
    bjvm_type_kind kind1 = POP_VAL, kind2 = POP_VAL;
    if (is_kind_wide(kind1) || is_kind_wide(kind2))
      goto stack_type_mismatch;
    ;
    PUSH_KIND(kind1) PUSH_KIND(kind2) break;
  }
  case bjvm_insn_anewarray: {
    POP(INT) PUSH(REFERENCE) break;
  }
  case bjvm_insn_checkcast: {
    POP(REFERENCE) PUSH(REFERENCE) break;
  }
  case bjvm_insn_getfield:
    POP(REFERENCE)
    [[fallthrough]];
  case bjvm_insn_getstatic: {
    bjvm_field_descriptor *field =
        bjvm_check_cp_entry(insn->cp, BJVM_CP_KIND_FIELD_REF,
                            "getstatic/getfield argument")
            ->field.parsed_descriptor;
    PUSH_KIND(field_to_kind(field));
    break;
  }
  case bjvm_insn_instanceof: {
    POP(REFERENCE) PUSH(INT) break;
  }
  case bjvm_insn_invokedynamic: {
    bjvm_method_descriptor *descriptor =
        bjvm_check_cp_entry(insn->cp, BJVM_CP_KIND_INVOKE_DYNAMIC,
                            "invokedynamic argument")
            ->indy_info.method_descriptor;
    for (int j = descriptor->args_count - 1; j >= 0; --j) {
      bjvm_field_descriptor *field = descriptor->args + j;
      POP_KIND(field_to_kind(field));
    }
    if (descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID)
      PUSH_KIND(field_to_kind(&descriptor->return_type))
    break;
  }
  case bjvm_insn_new: {
    PUSH(REFERENCE)
    break;
  }
  case bjvm_insn_putfield:
  case bjvm_insn_putstatic: {
    bjvm_type_kind kind = POP_VAL;
    // TODO check that the field matches
    (void)kind;
    if (insn->kind == bjvm_insn_putfield) {
      POP(REFERENCE)
    }
    break;
  }
  case bjvm_insn_invokevirtual:
  case bjvm_insn_invokespecial:
  case bjvm_insn_invokeinterface:
  case bjvm_insn_invokestatic: {
    bjvm_method_descriptor *descriptor =
        bjvm_check_cp_entry(insn->cp,
                            BJVM_CP_KIND_METHOD_REF |
                                BJVM_CP_KIND_INTERFACE_METHOD_REF,
                            "invoke* argument")
            ->methodref.descriptor;
    for (int j = descriptor->args_count - 1; j >= 0; --j) {
      bjvm_field_descriptor *field = descriptor->args + j;
      POP_KIND(field_to_kind(field))
    }
    if (insn->kind != bjvm_insn_invokestatic) {
      POP(REFERENCE)
    }
    if (descriptor->return_type.base_kind != BJVM_TYPE_KIND_VOID)
      PUSH_KIND(field_to_kind(&descriptor->return_type));
    break;
  }
  case bjvm_insn_ldc: {
    bjvm_cp_entry *ent =
        bjvm_check_cp_entry(insn->cp,
                            BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_STRING |
                                BJVM_CP_KIND_FLOAT | BJVM_CP_KIND_CLASS,
                            "ldc argument");
    PUSH_KIND(ent->kind == BJVM_CP_KIND_INTEGER ? BJVM_TYPE_KIND_INT
              : ent->kind == BJVM_CP_KIND_FLOAT ? BJVM_TYPE_KIND_FLOAT
                                                : BJVM_TYPE_KIND_REFERENCE)
    break;
  }
  case bjvm_insn_ldc2_w: {
    bjvm_cp_entry *ent = bjvm_check_cp_entry(
        insn->cp, BJVM_CP_KIND_DOUBLE | BJVM_CP_KIND_LONG, "ldc2_w argument");
    PUSH_KIND(ent->kind == BJVM_CP_KIND_DOUBLE ? BJVM_TYPE_KIND_DOUBLE
                                               : BJVM_TYPE_KIND_LONG)
    break;
  }
  case bjvm_insn_dload: {
    PUSH(DOUBLE)
    SWIZZLE_LOCAL(insn->index)
    CHECK_LOCAL(insn->index, DOUBLE)
    break;
  }
  case bjvm_insn_fload: {
    PUSH(FLOAT)
    SWIZZLE_LOCAL(insn->index)
    CHECK_LOCAL(insn->index, FLOAT)
    break;
  }
  case bjvm_insn_iload: {
    PUSH(INT)
    SWIZZLE_LOCAL(insn->index)
    CHECK_LOCAL(insn->index, INT)
    break;
  }
  case bjvm_insn_lload: {
    PUSH(LONG)
    SWIZZLE_LOCAL(insn->index)
    CHECK_LOCAL(insn->index, LONG)
    break;
  }
  case bjvm_insn_dstore: {
    POP(DOUBLE)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, DOUBLE)
    break;
  }
  case bjvm_insn_fstore: {
    POP(FLOAT)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, FLOAT)
    break;
  }
  case bjvm_insn_istore: {
    POP(INT)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, INT)
    break;
  }
  case bjvm_insn_lstore: {
    POP(LONG)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, LONG)
    break;
  }
  case bjvm_insn_aload: {
    PUSH(REFERENCE)
    SWIZZLE_LOCAL(insn->index)
    CHECK_LOCAL(insn->index, REFERENCE)
    break;
  }
  case bjvm_insn_astore: {
    POP(REFERENCE)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, REFERENCE)
    break;
  }
  case bjvm_insn_goto: {
    if (push_branch_target(ctx, insn_index, insn->index))
      return -1;
    ctx->stack_terminated = true;
    break;
  }
  case bjvm_insn_jsr: {
    PUSH(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      return -1;
    break;
  }
  case bjvm_insn_if_acmpeq:
  case bjvm_insn_if_acmpne: {
    POP(REFERENCE)
    POP(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      return -1;
    break;
  }
  case bjvm_insn_if_icmpeq:
  case bjvm_insn_if_icmpne:
  case bjvm_insn_if_icmplt:
  case bjvm_insn_if_icmpge:
  case bjvm_insn_if_icmpgt:
  case bjvm_insn_if_icmple:
    POP(INT)
    [[fallthrough]];
  case bjvm_insn_ifeq:
  case bjvm_insn_ifne:
  case bjvm_insn_iflt:
  case bjvm_insn_ifge:
  case bjvm_insn_ifgt:
  case bjvm_insn_ifle: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->index))
      return -1;
    break;
  }
  case bjvm_insn_ifnonnull:
  case bjvm_insn_ifnull: {
    POP(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      return -1;
    break;
  }
  case bjvm_insn_iconst:
    PUSH(INT) break;
  case bjvm_insn_dconst:
    PUSH(DOUBLE) break;
  case bjvm_insn_fconst:
    PUSH(FLOAT) break;
  case bjvm_insn_lconst:
    PUSH(LONG) break;
  case bjvm_insn_iinc:
    SWIZZLE_LOCAL(insn->iinc.index);
    break;
  case bjvm_insn_multianewarray: {
    for (int i = 0; i < insn->multianewarray.dimensions; ++i)
      POP(INT)
    PUSH(REFERENCE)
    break;
  }
  case bjvm_insn_newarray: {
    POP(INT) PUSH(REFERENCE) break;
  }
  case bjvm_insn_tableswitch: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->tableswitch.default_target))
      return -1;
    for (int i = 0; i < insn->tableswitch.targets_count; ++i)
      if (push_branch_target(ctx, insn_index, insn->tableswitch.targets[i]))
        return -1;
    ctx->stack_terminated = true;
    break;
  }
  case bjvm_insn_lookupswitch: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->lookupswitch.default_target))
      return -1;
    for (int i = 0; i < insn->lookupswitch.targets_count; ++i)
      if (push_branch_target(ctx, insn_index, insn->lookupswitch.targets[i]))
        return -1;
    ctx->stack_terminated = true;
    break;
  }
  }

  return 0; // ok

  // Error cases
local_type_mismatch:
  ctx->insn_error = strdup("Local type mismatch:");
  goto error;
local_overflow:
  ctx->insn_error = strdup("Local overflow:");
  goto error;
stack_overflow:
  ctx->insn_error = strdup("Stack overflow:");
  goto error;
stack_underflow:
  ctx->insn_error = strdup("Stack underflow:");
  goto error;
stack_type_mismatch:
  ctx->insn_error = "Stack type mismatch:";
error:;
  *ctx->error = make_heap_str(50000);
  heap_string insn_str = insn_to_string(insn, insn_index);
  char *stack_str = print_analy_stack_state(&ctx->stack_before);
  char *locals_str = print_analy_stack_state(&ctx->locals);
  heap_string context = code_attribute_to_string(ctx->code);
  bprintf(hslc(*ctx->error),
          "%s\nInstruction: %.*s\nStack preceding insn: %s\nLocals state: "
          "%s\nContext:\n%.*s\n",
          ctx->insn_error, fmt_slice(insn_str), stack_str, locals_str,
          fmt_slice(context));
  free_heap_str(insn_str);
  free(stack_str);
  free(locals_str);
  free_heap_str(context);
  free(ctx->insn_error);
  return -1;
}

bool filter_locals(bjvm_analy_stack_state *s1,
                   const bjvm_analy_stack_state *s2) {
  if (s1->entries_count != s2->entries_count) {
    printf("%s\n%s\n", print_analy_stack_state(s1),
           print_analy_stack_state(s2));
  }
  assert(s1->entries_count == s2->entries_count);
  bool changed = false;
  for (int i = 0; i < s1->entries_count; ++i) {
    if (s1->entries[i] != BJVM_TYPE_KIND_VOID &&
        s1->entries[i] != s2->entries[i]) {
      s1->entries[i] = BJVM_TYPE_KIND_VOID;
      changed = true;
    }
  }
  return changed;
}

bool insn_changes_locals_kinds(bjvm_bytecode_insn *insn) {
  switch (insn->kind) {
  case bjvm_insn_dstore:
  case bjvm_insn_fstore:
  case bjvm_insn_istore:
  case bjvm_insn_lstore:
  case bjvm_insn_astore:
    return true;
  default:
    return false;
  }
}

bool gross_quadratic_algorithm_to_refine_local_references(
    struct method_analysis_ctx *ctx) {
  bool any_changed = false;

  // Iterate over edges and intersect the target locals with the source locals.
  // No branching instruction changes the locals state, so this is easy enough
  // to write out
  for (int i = 0; i < ctx->edges_count; ++i) {
    struct edge edge = ctx->edges[i];
    bool changed = filter_locals(ctx->inferred_locals + edge.end,
                                 ctx->inferred_locals + edge.start);
    any_changed = any_changed || changed;
  }

  // Now iterate over instructions and refine the locals state
  bool stack_terminated = true;
  for (int i = 0; i < ctx->code->insn_count; ++i) {
    bjvm_bytecode_insn *insn = ctx->code->code + i;
    if (stack_terminated) {
      copy_analy_stack_state(ctx->inferred_locals[i], &ctx->locals);
      stack_terminated = false;
    } else {
      filter_locals(ctx->inferred_locals + i, &ctx->locals);
    }

    switch (insn->kind) {
    case bjvm_insn_goto:
    case bjvm_insn_athrow:
    case bjvm_insn_areturn:
    case bjvm_insn_dreturn:
    case bjvm_insn_freturn:
    case bjvm_insn_ireturn:
    case bjvm_insn_lreturn:
    case bjvm_insn_return:
    case bjvm_insn_jsr:
    case bjvm_insn_ret:
    case bjvm_insn_tableswitch:
    case bjvm_insn_lookupswitch:
      stack_terminated = true;
      break;
    case bjvm_insn_astore:
      ctx->locals.entries[insn->index] = BJVM_TYPE_KIND_REFERENCE;
      break;
    case bjvm_insn_dstore:
      ctx->locals.entries[insn->index] = BJVM_TYPE_KIND_DOUBLE;
      break;
    case bjvm_insn_fstore:
      ctx->locals.entries[insn->index] = BJVM_TYPE_KIND_FLOAT;
      break;
    case bjvm_insn_istore:
      ctx->locals.entries[insn->index] = BJVM_TYPE_KIND_INT;
      break;
    case bjvm_insn_lstore:
      ctx->locals.entries[insn->index] = BJVM_TYPE_KIND_LONG;
      break;
    default:
      break;
    }
  }

  return any_changed;
}

void add_exception_edges(struct method_analysis_ctx *ctx) {
  // Add sufficient edges between instructions and accessible exception handlers
  // such that the locals filtration works correctly when jumping to handlers.
  bjvm_attribute_exception_table *exc = ctx->code->exception_table;
  if (!exc)
    return;

  // jsr edges
  for (int i = 0; i < ctx->code->insn_count; ++i) {
    bjvm_bytecode_insn *insn = ctx->code->code + i;
    if (insn->kind == bjvm_insn_jsr) {
      *VECTOR_PUSH(ctx->edges, ctx->edges_count, ctx->edges_cap) =
          (struct edge){.start = i, .end = insn->index};
    }
  }

  for (int i = 0; i < exc->entries_count; ++i) {
    // Scan instructions in [start_pc, end_pc). For any instruction changing
    // the locals state, add an edge from the instruction after to the handler.
    bjvm_exception_table_entry ent = exc->entries[i];
    *VECTOR_PUSH(ctx->edges, ctx->edges_count, ctx->edges_cap) =
        (struct edge){.start = ent.start_insn, .end = ent.handler_insn};
    for (int j = ent.start_insn; j < ent.end_insn - 1; ++j) {
      if (insn_changes_locals_kinds(ctx->code->code + j)) {
        *VECTOR_PUSH(ctx->edges, ctx->edges_count, ctx->edges_cap) =
            (struct edge){.start = j + 1, .end = ent.handler_insn};
      }
    }
  }
}

/**
 * Analyze the method's code attribute if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis, and returning an error string upon some sort of error.
 */
int bjvm_analyze_method_code(bjvm_cp_method *method, heap_string *error) {
  bjvm_attribute_code *code = method->code;
  if (!code || method->code_analysis) {
    return 0;
  }
  struct method_analysis_ctx ctx = {code};

  // Swizzle local entries so that the first n arguments correspond to the first
  // n locals (i.e., we should remap aload #1 to aload swizzle[#1])
  if (bjvm_locals_on_method_entry(method, &ctx.locals, &ctx.locals_swizzle)) {
    return -1;
  }

  int result = 0;
  ctx.stack.entries =
      calloc(code->max_stack + 1, sizeof(bjvm_analy_stack_entry));

  // After jumps, we can infer the stack and locals at these points
  bjvm_analy_stack_state *inferred_stacks = ctx.inferred_stacks =
      calloc(code->insn_count, sizeof(bjvm_analy_stack_state));
  bjvm_analy_stack_state *inferred_locals = ctx.inferred_locals =
      calloc(code->insn_count, sizeof(bjvm_analy_stack_state));
  uint16_t *insn_index_to_stack_depth =
      calloc(code->insn_count, sizeof(uint16_t));

  // Initialize stack to the stack at exception handler entry
  ctx.stack.entries_cap = code->max_stack + 1;
  ctx.stack.entries_count = 1;
  ctx.stack.entries[0] = BJVM_TYPE_KIND_REFERENCE;

  ctx.error = error;

  // Mark all exception handlers as having a stack which is just a reference
  // (that reference is the exception object)
  if (code->exception_table) {
    for (int i = 0; i < code->exception_table->entries_count; ++i) {
      bjvm_exception_table_entry *ent = code->exception_table->entries + i;
      if (!inferred_stacks[ent->handler_insn].entries) {
        bjvm_analy_stack_state *target = inferred_stacks + ent->handler_insn;
        copy_analy_stack_state(ctx.stack, target);
        target->is_exc_handler = true;
        target->exc_handler_start = ent->start_insn;
      }
    }
  }

  ctx.stack.entries_count = 0;

  bjvm_code_analysis *analy = method->code_analysis =
      malloc(sizeof(bjvm_code_analysis));

  analy->insn_count = code->insn_count;
  analy->dominator_tree_computed = false;
  for (int i = 0; i < 5; ++i) {
    analy->insn_index_to_kinds[i] =
        calloc(code->insn_count, sizeof(bjvm_compressed_bitset));
  }
  analy->blocks = nullptr;
  analy->insn_index_to_stack_depth = insn_index_to_stack_depth;
  ctx.branch_q = calloc(code->max_formal_pc, sizeof(int));

  for (int i = 0; i < code->insn_count; ++i) {
    if (inferred_stacks[i].entries) {
      copy_analy_stack_state(inferred_stacks[i], &ctx.stack);
      bjvm_analy_stack_state *this_locals = &inferred_locals[i];
      if (inferred_locals[i].is_exc_handler) {
        // At exception handlers, use the local variable table of the start of
        // the exception block. Later we'll intersect this down.
        copy_analy_stack_state(inferred_locals[this_locals->exc_handler_start],
                               &ctx.locals);
        copy_analy_stack_state(ctx.locals, this_locals);
      }
      inferred_stacks[i].from_jump_target = false;
      ctx.stack_terminated = false;
    }
    if (inferred_locals[i].entries) {
      copy_analy_stack_state(inferred_locals[i], &ctx.locals);
    }

    if (ctx.stack_terminated) {
      // We expect to be able to recover the stack/locals from a previously
      // encountered jump, or an exception handler. If this isn't possible then
      // there will be weeping and wailing and gnashing of teeth, and we'll
      // choose a different branch to continue analyzing from.
      if (ctx.branch_count == 0) {
        break;
      }
      i = ctx.branch_q[--ctx.branch_count];
      copy_analy_stack_state(inferred_stacks[i], &ctx.stack);
      copy_analy_stack_state(inferred_locals[i], &ctx.locals);
      inferred_stacks[i].from_jump_target = false;
      ctx.stack_terminated = false;
    }

    copy_analy_stack_state(ctx.stack, &ctx.stack_before);
    if (!inferred_stacks[i].entries)
      copy_analy_stack_state(ctx.stack, &inferred_stacks[i]);
    if (!inferred_locals[i].entries)
      copy_analy_stack_state(ctx.locals, &inferred_locals[i]);

    bjvm_bytecode_insn *insn = &code->code[i];
    if (analyze_instruction(insn, i, &ctx)) {
      result = -1;
      goto done;
    }
  }

  add_exception_edges(&ctx);

  // Check that all entries have been filled
  for (int i = 0; i < code->insn_count; ++i) {
    if (!inferred_stacks[i].entries) {
      heap_string context = code_attribute_to_string(method->code);
      heap_string unreachable_code_error = make_heap_str(context.len + 100);
      bprintf(hslc(unreachable_code_error),
              "Unreachable code detected at instruction %d\nContext:\n%.*s\n",
              i, fmt_slice(context));
      // TODO report
      UNREACHABLE();
      break;
    }
  }

  while (gross_quadratic_algorithm_to_refine_local_references(&ctx))
    ;

done:
  for (int i = 0; i < code->insn_count; ++i) {
    insn_index_to_stack_depth[i] = inferred_stacks[i].entries_count;

    for (int j = 0; j < 5; ++j) {
      bjvm_type_kind order[5] = {BJVM_TYPE_KIND_REFERENCE, BJVM_TYPE_KIND_INT,
                                 BJVM_TYPE_KIND_FLOAT, BJVM_TYPE_KIND_DOUBLE,
                                 BJVM_TYPE_KIND_LONG};
      bjvm_compressed_bitset *bitset = analy->insn_index_to_kinds[j] + i;
      bjvm_init_compressed_bitset(bitset, code->max_stack + code->max_locals);

      write_kinds_to_bitset(&inferred_stacks[i], 0, bitset, order[j]);
      write_kinds_to_bitset(&inferred_locals[i], code->max_stack, bitset,
                            order[j]);
    }

    free(inferred_stacks[i].entries);
    free(inferred_locals[i].entries);
  }
  free(ctx.branch_q);
  free(ctx.stack.entries);
  free(ctx.locals.entries);
  free(ctx.stack_before.entries);
  free(ctx.locals_swizzle);
  free(ctx.edges);
  free(inferred_stacks);
  free(inferred_locals);

  return result;
}

void free_code_analysis(bjvm_code_analysis *analy) {
  if (!analy)
    return;
  if (analy->insn_index_to_references) {
    for (int j = 0; j < 5; ++j) {
      for (int i = 0; i < analy->insn_count; ++i)
        bjvm_free_compressed_bitset(analy->insn_index_to_kinds[j][i]);
      free(analy->insn_index_to_kinds[j]);
    }
  }
  if (analy->blocks) {
    for (int i = 0; i < analy->block_count; ++i) {
      free(analy->blocks[i].next);
      free(analy->blocks[i].is_backedge);
      free(analy->blocks[i].prev);
      free(analy->blocks[i].idominates.list);
    }
    free(analy->blocks);
  }
  free(analy->insn_index_to_stack_depth);
  free(analy);
}

static void push_bb_branch(bjvm_basic_block *current, bjvm_basic_block *next) {
  *VECTOR_PUSH(current->next, current->next_count, current->next_cap) =
      next->my_index;
}

static int cmp_ints(const void *a, const void *b) {
  return *(int *)a - *(int *)b;
}

// Used to find which blocks are accessible from the entry without throwing
// exceptions.
void dfs_nothrow_accessible(bjvm_basic_block *bs, int i) {
  bjvm_basic_block *b = bs + i;
  if (b->nothrow_accessible)
    return;
  b->nothrow_accessible = true;
  for (int j = 0; j < b->next_count; ++j)
    dfs_nothrow_accessible(bs, b->next[j]);
}

// Scan basic blocks in the code. Code that is not accessible without throwing
// an exception is DELETED because we're not handling exceptions at all in
// JIT compiled code. (Once an exception is thrown in a frame, it is
// interpreted for the rest of its life.)
int bjvm_scan_basic_blocks(const bjvm_attribute_code *code,
                           bjvm_code_analysis *analy) {
  assert(analy);
  if (analy->blocks)
    return 0; // already done
  // First, record all branch targets.
  int *ts = calloc(code->max_formal_pc, sizeof(uint32_t));
  int tc = 0;
  ts[tc++] = 0; // mark entry point
  for (int i = 0; i < code->insn_count; ++i) {
    const bjvm_bytecode_insn *insn = code->code + i;
    if (insn->kind >= bjvm_insn_goto && insn->kind <= bjvm_insn_ifnull) {
      ts[tc++] = insn->index;
      if (insn->kind != bjvm_insn_goto)
        ts[tc++] = i + 1; // fallthrough
    } else if (insn->kind == bjvm_insn_tableswitch ||
               insn->kind == bjvm_insn_lookupswitch) {
      const struct bjvm_bc_tableswitch_data *tsd = &insn->tableswitch;
      // Layout is the same between tableswitch and lookupswitch, so ok
      ts[tc++] = tsd->default_target;
      memcpy(ts + tc, tsd->targets, tsd->targets_count * sizeof(int));
      tc += tsd->targets_count;
    }
  }
  // Then, sort, remove duplicates and create basic block entries for each
  qsort(ts, tc, sizeof(int), cmp_ints);
  int block_count = 0;
  for (int i = 0; i < tc; ++i) // remove dups
    ts[block_count += ts[block_count] != ts[i]] = ts[i];
  bjvm_basic_block *bs = calloc(++block_count, sizeof(bjvm_basic_block));
  for (int i = 0; i < block_count; ++i) {
    bs[i].start_index = ts[i];
    bs[i].start = code->code + ts[i];
    bs[i].insn_count =
        i + 1 < block_count ? ts[i + 1] - ts[i] : code->insn_count - ts[i];
    bs[i].my_index = i;
  }
#define FIND_TARGET_BLOCK(index)                                               \
  &bs[(int *)bsearch(&index, ts, block_count, sizeof(int), cmp_ints) - ts]
  // Then, record edges between bbs.
  for (int block_i = 0; block_i < block_count; ++block_i) {
    bjvm_basic_block *b = bs + block_i;
    const bjvm_bytecode_insn *last = b->start + b->insn_count - 1;
    if (last->kind >= bjvm_insn_goto && last->kind <= bjvm_insn_ifnull) {
      push_bb_branch(b, FIND_TARGET_BLOCK(last->index));
      if (last->kind == bjvm_insn_goto)
        continue;
    } else if (last->kind == bjvm_insn_tableswitch ||
               last->kind == bjvm_insn_lookupswitch) {
      const struct bjvm_bc_tableswitch_data *tsd = &last->tableswitch;
      push_bb_branch(b, FIND_TARGET_BLOCK(tsd->default_target));
      for (int i = 0; i < tsd->targets_count; ++i)
        push_bb_branch(b, FIND_TARGET_BLOCK(tsd->targets[i]));
      continue;
    }
    if (block_i + 1 < block_count)
      push_bb_branch(b, &bs[block_i + 1]);
  }
  // Record which blocks are nothrow-accessible from entry block.
  dfs_nothrow_accessible(bs, 0);
  // Delete inaccessible blocks and renumber the rest. (Reusing ts for this)
  int j = 0;
  for (int i = 0; i < block_count; ++i) {
    if (bs[i].nothrow_accessible) {
      bs[j] = bs[i];
      ts[i] = j++;
    } else {
      free(bs[i].next);
    }
  }
  // Renumber edges and add "prev" edges
  block_count = j;
  for (int block_i = 0; block_i < block_count; ++block_i) {
    bjvm_basic_block *b = bs + block_i;
    b->my_index = block_i;
    for (int j = 0; j < b->next_count; ++j) {
      bjvm_basic_block *next = bs + (b->next[j] = ts[b->next[j]]);
      *VECTOR_PUSH(next->prev, next->prev_count, next->prev_cap) = block_i;
    }
  }
  // Create some allocations for later analyses
  for (int block_i = 0; block_i < block_count; ++block_i) {
    bjvm_basic_block *b = bs + block_i;
    b->is_backedge = calloc(b->next_count, sizeof(bool));
  }
  analy->block_count = block_count;
  analy->blocks = bs;
  free(ts);
  return 0;
#undef FIND_TARGET_BLOCK
}

static void get_dfs_tree(bjvm_basic_block *block, int *block_to_pre,
                         int *preorder, int *parent, int *preorder_clock,
                         int *postorder_clock) {
  preorder[*preorder_clock] = block->my_index;
  block_to_pre[block->my_index] = block->dfs_pre = (*preorder_clock)++;
  for (int j = 0; j < block->next_count; ++j) {
    if (block_to_pre[block->next[j]] == -1) {
      parent[block->next[j]] = block->my_index;
      get_dfs_tree(block - block->my_index + block->next[j], block_to_pre,
                   preorder, parent, preorder_clock, postorder_clock);
    }
  }
  block->dfs_post = (*postorder_clock)++;
}

static void idom_dfs(bjvm_basic_block *block, int *visited, uint32_t *clock) {
  block->idom_pre = (*clock)++;
  visited[block->my_index] = 1;
  bjvm_dominated_list_t *dlist = &block->idominates;
  for (int i = 0; i < dlist->count; ++i) {
    int next = dlist->list[i];
    if (!visited[next])
      idom_dfs(block + next - block->my_index, visited, clock);
  }
  block->idom_post = (*clock)++;
}

// The classic Lengauer-Tarjan algorithm for dominator tree computation
void bjvm_compute_dominator_tree(bjvm_code_analysis *analy) {
  // bjvm_dump_cfg_to_graphviz(stderr, analy);
  assert(analy->blocks && "Basic blocks must have been already scanned");
  if (analy->dominator_tree_computed)
    return;
  analy->dominator_tree_computed = true;
  int block_count = analy->block_count;
  // block # -> pre-order #
  int *block_to_pre = malloc(block_count * sizeof(int));
  // pre-order # -> block #
  int *pre_to_block = malloc(block_count * sizeof(int));
  // block # -> block #
  int *parent = malloc(block_count * sizeof(int));
  memset(block_to_pre, -1, block_count * sizeof(int));
  int pre = 0, post = 0;
  get_dfs_tree(analy->blocks, block_to_pre, pre_to_block, parent, &pre, &post);
  // Initialize a forest, subset of the DFS tree, F[i] = i initially
  int *F = malloc(block_count * sizeof(int));
  for (int i = 0; i < block_count; ++i)
    F[i] = i;
  // semidom[j] is the semi-dominator of j
  int *semidom = calloc(block_count, sizeof(int));
  // Go through all non-entry blocks in reverse pre-order
  for (int preorder_i = block_count - 1; preorder_i >= 1; --preorder_i) {
    int i = pre_to_block[preorder_i];
    bjvm_basic_block *b = analy->blocks + i;
    int sd = INT_MAX; // preorder, not block #
    // Go through predecessor blocks in any order
    for (int prev_i = 0; prev_i < b->prev_count; ++prev_i) {
      int prev = b->prev[prev_i];
      if (prev == i) // self-loop, doesn't affect dominator properties
        continue;
      if (block_to_pre[prev] < preorder_i) {
        if (block_to_pre[prev] < sd) // prev is a better candidate for semidom
          sd = block_to_pre[prev];
      } else {
        // Get the root in F using union find with path compression
        int root = prev;
        while (F[root] != root)
          root = F[root] = F[F[root]];
        // Walk the preorder from prev to root, and update sd
        do {
          if (block_to_pre[semidom[prev]] < sd)
            sd = block_to_pre[semidom[prev]];
          prev = parent[prev];
        } while (prev != root);
      }
    }
    semidom[i] = pre_to_block[sd];
    bjvm_dominated_list_t *sdlist = &analy->blocks[pre_to_block[sd]].idominates;
    *VECTOR_PUSH(sdlist->list, sdlist->count, sdlist->cap) = i;
    F[i] = parent[i];
  }
  // Compute relative dominators
  int *reldom = calloc(block_count, sizeof(int));
  for (int i = 1; i < block_count; ++i) {
    bjvm_dominated_list_t *sdlist = &analy->blocks[i].idominates;
    for (int list_i = 0; list_i < sdlist->count; ++list_i) {
      int w = sdlist->list[list_i], walk = w, min = INT_MAX;
      assert(semidom[w] == i && "Algorithm invariant");
      // Walk from w to i and record the minimizer of the semidominator value
      while (walk != i) {
        if (block_to_pre[walk] < min) {
          min = block_to_pre[walk];
          reldom[w] = walk;
        }
        walk = parent[walk];
      }
    }
  }
  // Now, we can compute the immediate dominators
  for (int i = 0; i < block_count; ++i)
    analy->blocks[i].idominates.count = 0;
  for (int preorder_i = 1; preorder_i < block_count; ++preorder_i) {
    int i = pre_to_block[preorder_i];
    int idom = analy->blocks[i].idom =
        i == reldom[i] ? semidom[i] : analy->blocks[reldom[i]].idom;
    bjvm_dominated_list_t *sdlist = &analy->blocks[idom].idominates;
    *VECTOR_PUSH(sdlist->list, sdlist->count, sdlist->cap) = i;
  }
  free(block_to_pre);
  free(pre_to_block);
  free(parent);
  free(semidom);
  free(reldom);
  // Now compute a DFS tree on the immediate dominator tree (still need a
  // "visited" array because there are duplicate edges)
  uint32_t clock = 1;
  // Re-use F as the visited array
  memset(F, 0, block_count * sizeof(int));
  idom_dfs(analy->blocks, F, &clock);
  free(F);
}

bool bjvm_query_dominance(const bjvm_basic_block *dominator,
                          const bjvm_basic_block *dominated) {
  assert(dominator->idom_pre != 0 && "dominator tree not computed");
  return dominator->idom_pre <= dominated->idom_pre &&
         dominator->idom_post >= dominated->idom_post;
}

// Check whether the CFG is reducible
static int forward_edges_form_a_cycle(bjvm_code_analysis *analy, int i,
                                      int *visited) {
  bjvm_basic_block *b = analy->blocks + i;
  visited[i] = 1;
  for (int j = 0; j < b->next_count; ++j) {
    int next = b->next[j];
    if (b->is_backedge[j])
      continue;
    if (visited[next] == 0) {
      if (forward_edges_form_a_cycle(analy, next, visited))
        return 1;
    } else {
      if (visited[next] == 1) {
        return 1;
      }
    }
  }
  visited[i] = 2;
  return 0;
}

int bjvm_attempt_reduce_cfg(bjvm_code_analysis *analy) {
  // mark back-edges
  for (int i = 0; i < analy->block_count; ++i) {
    bjvm_basic_block *b = analy->blocks + i, *next;
    for (int j = 0; j < b->next_count; ++j) {
      next = analy->blocks + b->next[j];
      b->is_backedge[j] = bjvm_query_dominance(next, b);
      next->is_loop_header |= b->is_backedge[j];
    }
  }

  int *visited = calloc(analy->block_count, sizeof(int));
  int fail = forward_edges_form_a_cycle(analy, 0, visited);
  free(visited);
  return fail;
}

void bjvm_dump_cfg_to_graphviz(FILE *out, const bjvm_code_analysis *analysis) {
  fprintf(out, "digraph cfg {\n");
  for (int i = 0; i < analysis->block_count; i++) {
    bjvm_basic_block *b = &analysis->blocks[i];
    fprintf(out, "    %d [label=\"Block %d\"];\n", b->my_index, b->my_index);
  }
  for (int i = 0; i < analysis->block_count; i++) {
    bjvm_basic_block *b = &analysis->blocks[i];
    for (int j = 0; j < b->next_count; j++) {
      fprintf(out, "    %d -> %u;\n", b->my_index, b->next[j]);
    }
  }
  fprintf(out, "}\n");
}