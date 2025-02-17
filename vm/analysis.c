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
#include <stackmaptable.h>

typedef struct {
  type_kind type;
  stack_variable_source source; // used for NPE reason analysis
} analy_stack_entry;

// State of the stack (or local variable table) during analysis, indexed after
// index swizzling (which occurs very early in our processing)
typedef struct {
  analy_stack_entry *entries;
  int entries_count;
  int entries_cap;
} analy_stack_state;

#define CASE(K)                                                                                                        \
  case insn_##K:                                                                                                       \
    return #K;

const char *insn_code_name(insn_code_kind code) {
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
    CASE(anewarray_resolved)
    CASE(checkcast)
    CASE(checkcast_resolved)
    CASE(getfield)
    CASE(getstatic)
    CASE(instanceof)
    CASE(instanceof_resolved)
    CASE(invokedynamic)
    CASE(new)
    CASE(new_resolved)
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
    CASE(invokestatic_resolved)
    CASE(invokecallsite)
    CASE(getfield_B)
    CASE(getfield_C)
    CASE(getfield_S)
    CASE(getfield_I)
    CASE(getfield_J)
    CASE(getfield_F)
    CASE(getfield_D)
    CASE(getfield_L)
    CASE(getfield_Z)
    CASE(putfield_B)
    CASE(putfield_C)
    CASE(putfield_S)
    CASE(putfield_I)
    CASE(putfield_J)
    CASE(putfield_F)
    CASE(putfield_D)
    CASE(putfield_L)
    CASE(putfield_Z)
    CASE(getstatic_B)
    CASE(getstatic_C)
    CASE(getstatic_S)
    CASE(getstatic_I)
    CASE(getstatic_J)
    CASE(getstatic_F)
    CASE(getstatic_D)
    CASE(getstatic_L)
    CASE(getstatic_Z)
    CASE(putstatic_B)
    CASE(putstatic_C)
    CASE(putstatic_S)
    CASE(putstatic_I)
    CASE(putstatic_J)
    CASE(putstatic_F)
    CASE(putstatic_D)
    CASE(putstatic_L)
    CASE(putstatic_Z)
    CASE(invokesigpoly)
    CASE(sin)
    CASE(cos)
    CASE(sqrt)
  }
  printf("Unknown code: %d\n", code);
  UNREACHABLE();
}

const char *type_kind_to_string(type_kind kind) {
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
    return "boolean";
  case TYPE_KIND_BYTE:
    return "byte";
  case TYPE_KIND_CHAR:
    return "char";
  case TYPE_KIND_SHORT:
    return "short";
  case TYPE_KIND_INT:
    return "int";
  case TYPE_KIND_LONG:
    return "long";
  case TYPE_KIND_FLOAT:
    return "float";
  case TYPE_KIND_DOUBLE:
    return "double";
  case TYPE_KIND_VOID:
    return "void";
  case TYPE_KIND_REFERENCE:
    return "<reference>";
  }
  UNREACHABLE();
}

char *class_info_entry_to_string(cp_kind kind, const cp_class_info *ent) {
  char const *start;
  switch (kind) {
  case CP_KIND_CLASS:
    start = "Class: ";
    break;
  case CP_KIND_MODULE:
    start = "Module: ";
    break;
  case CP_KIND_PACKAGE:
    start = "Package: ";
    break;
  default:
    UNREACHABLE();
  }

  char result[1000];
  snprintf(result, sizeof(result), "%s%.*s", start, ent->name.len, ent->name.chars);
  return strdup(result);
}

char *name_and_type_entry_to_string(const cp_name_and_type *name_and_type) {
  char result[1000];
  snprintf(result, sizeof(result), "NameAndType: %.*s:%.*s", name_and_type->name.len, name_and_type->name.chars,
           name_and_type->descriptor.len, name_and_type->descriptor.chars);
  return strdup(result);
}

char *indy_entry_to_string(const cp_indy_info *indy_info) {
  char *name_and_type = name_and_type_entry_to_string(indy_info->name_and_type); // TODO add bootstrap method
  return name_and_type;
}

/**
 * Convert the constant pool entry to an owned string.
 */
char *constant_pool_entry_to_string(const cp_entry *ent) {
  char result[200];
  switch (ent->kind) {
  case CP_KIND_INVALID:
    return strdup("<invalid>");
  case CP_KIND_UTF8:
    return strndup(ent->utf8.chars, ent->utf8.len);
  case CP_KIND_INTEGER:
    snprintf(result, sizeof(result), "%" PRId64, ent->integral.value);
    break;
  case CP_KIND_FLOAT:
    snprintf(result, sizeof(result), "%.9gf", (float)ent->floating.value);
    break;
  case CP_KIND_LONG:
    snprintf(result, sizeof(result), "%" PRId64 "L", ent->integral.value);
    break;
  case CP_KIND_DOUBLE:
    snprintf(result, sizeof(result), "%.15gd", (float)ent->floating.value);
    break;
  case CP_KIND_MODULE:
    [[fallthrough]];
  case CP_KIND_PACKAGE:
    [[fallthrough]];
  case CP_KIND_CLASS:
    return class_info_entry_to_string(ent->kind, &ent->class_info);
  case CP_KIND_STRING: {
    snprintf(result, sizeof(result), "String: '%.*s'", ent->string.chars.len, ent->string.chars.chars);
    break;
  }
  case CP_KIND_FIELD_REF: {
    char *class_name = class_info_entry_to_string(CP_KIND_CLASS, ent->field.class_info);
    char *field_name = name_and_type_entry_to_string(ent->field.nat);

    snprintf(result, sizeof(result), "FieldRef: %s.%s", class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case CP_KIND_METHOD_REF:
  case CP_KIND_INTERFACE_METHOD_REF: {
    char *class_name = class_info_entry_to_string(CP_KIND_CLASS, ent->field.class_info);
    char *field_name = name_and_type_entry_to_string(ent->field.nat);
    snprintf(result, sizeof(result), "%s: %s; %s", ent->kind == CP_KIND_METHOD_REF ? "MethodRef" : "InterfaceMethodRef",
             class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case CP_KIND_NAME_AND_TYPE: {
    return name_and_type_entry_to_string(&ent->name_and_type);
  }
  case CP_KIND_METHOD_HANDLE: {
    return strdup("<method handle>"); // TODO
  }
  case CP_KIND_METHOD_TYPE: {
    return strdup("<method type>"); // TODO
  }
  case CP_KIND_INVOKE_DYNAMIC:
    return indy_entry_to_string(&ent->indy_info);
  case CP_KIND_DYNAMIC_CONSTANT:
    return indy_entry_to_string(&ent->indy_info);
  }
  return strdup(result);
}

int method_argc(const cp_method *method) {
  bool nonstatic = !(method->access_flags & ACCESS_STATIC);
  return method->descriptor->args_count + (nonstatic ? 1 : 0);
}

heap_string insn_to_string(const bytecode_insn *insn, int insn_index) {
  heap_string result = make_heap_str(10);
  int write = 0;
  write = build_str(&result, write, "%04d = pc %04d: ", insn_index, insn->original_pc);
  write = build_str(&result, write, "%s ", insn_code_name(insn->kind));
  if (insn->kind <= insn_swap) {
    // no operands
  } else if (insn->kind <= insn_ldc2_w) {
    // indexes into constant pool
    char *cp_str = constant_pool_entry_to_string(insn->cp);
    write = build_str(&result, write, "%s", cp_str);
    free(cp_str);
  } else if (insn->kind <= insn_astore) {
    // indexes into local variables
    write = build_str(&result, write, "#%d", insn->index);
  } else if (insn->kind <= insn_ifnull) {
    // indexes into the instruction array
    write = build_str(&result, write, "inst %d", insn->index);
  } else if (insn->kind == insn_lconst || insn->kind == insn_iconst) {
    write = build_str(&result, write, "%" PRId64, insn->integer_imm);
  } else if (insn->kind == insn_dconst || insn->kind == insn_fconst) {
    write = build_str(&result, write, "%.15g", insn->f_imm);
  } else if (insn->kind == insn_tableswitch) {
    write = build_str(&result, write, "[ default -> %d", insn->tableswitch->default_target);
    for (int i = 0, j = insn->tableswitch->low; i < insn->tableswitch->targets_count; ++i, ++j) {
      write = build_str(&result, write, ", %d -> %d", j, insn->tableswitch->targets[i]);
    }
    write = build_str(&result, write, " ]");
  } else if (insn->kind == insn_lookupswitch) {
    write = build_str(&result, write, "[ default -> %d", insn->lookupswitch->default_target);
    for (int i = 0; i < insn->lookupswitch->targets_count; ++i) {
      write = build_str(&result, write, ", %d -> %d", insn->lookupswitch->keys[i], insn->lookupswitch->targets[i]);
    }
    write = build_str(&result, write, " ]");
  } else {
    // TODO
  }
  return result;
}

heap_string code_attribute_to_string(const attribute_code *attrib) {
  heap_string result = make_heap_str(1000);
  int write = 0;
  for (int i = 0; i < attrib->insn_count; ++i) {
    heap_string insn_str = insn_to_string(attrib->code + i, i);
    write = build_str(&result, write, "%.*s\n", fmt_slice(insn_str));
  }
  return result;
}

char *print_analy_stack_state(const analy_stack_state *state) {
  char buf[1000], *end = buf + 1000;
  char *write = buf;
  write = stpncpy(write, "[ ", end - write);
  for (int i = 0; i < state->entries_count; ++i) {
    write = stpncpy(write, type_kind_to_string(state->entries[i].type), end - write);
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
void copy_analy_stack_state(analy_stack_state st, analy_stack_state *out) {
  if (out->entries_cap < st.entries_count || !out->entries) {
    out->entries_cap = st.entries_count + 2 /* we'll probably push more */;
    out->entries = realloc(out->entries, out->entries_cap * sizeof(analy_stack_entry));
    DCHECK(out->entries);
  }
  memcpy(out->entries, st.entries, st.entries_count * sizeof(analy_stack_entry));
  out->entries_count = st.entries_count;
}

bool is_kind_wide(type_kind kind) { return kind == TYPE_KIND_LONG || kind == TYPE_KIND_DOUBLE; }

bool is_field_wide(field_descriptor desc) { return is_kind_wide(desc.base_kind) && !desc.dimensions; }

void write_kinds_to_bitset(const analy_stack_state *inferred_stack, int offset, compressed_bitset *compressed_bitset,
                           type_kind test) {
  for (int i = 0; i < inferred_stack->entries_count; ++i) {
    if (inferred_stack->entries[i].type == test)
      test_set_compressed_bitset(compressed_bitset, offset + i);
  }
}

/** Possible value "sources" for NPE analysis */
static analy_stack_entry parameter_source(type_kind type, int j) {
  return (analy_stack_entry){type, {.kind = VARIABLE_SRC_KIND_PARAMETER, .index = j}};
}

static analy_stack_entry this_source() { return parameter_source(TYPE_KIND_REFERENCE, 0); }

static analy_stack_entry insn_source(type_kind type, int j) {
  return (analy_stack_entry){type, {.kind = VARIABLE_SRC_KIND_INSN, .index = j}};
}

static analy_stack_entry local_source(type_kind type, int pc) {
  return (analy_stack_entry){type, {.kind = VARIABLE_SRC_KIND_LOCAL, .index = pc}};
}

int locals_on_method_entry(const cp_method *method, analy_stack_state *locals, int **locals_swizzle) {
  const attribute_code *code = method->code;
  const method_descriptor *desc = method->descriptor;
  DCHECK(code);
  u16 max_locals = code->max_locals;
  locals->entries = calloc(max_locals, sizeof(analy_stack_entry));
  *locals_swizzle = malloc(max_locals * sizeof(int));
  for (int i = 0; i < max_locals; ++i) {
    locals->entries[i].type = TYPE_KIND_VOID;
    (*locals_swizzle)[i] = -1;
  }
  int i = 0, j = 0;
  bool is_static = method->access_flags & ACCESS_STATIC;
  if (!is_static) {
    // if the method is nonstatic, the first local is a reference 'this'
    if (max_locals == 0)
      goto fail;
    (*locals_swizzle)[0] = 0; // map 0 -> 0
    locals->entries[j++] = this_source();
  }
  locals->entries_cap = locals->entries_count = max_locals;
  for (; i < desc->args_count && j < max_locals; ++i, ++j) {
    field_descriptor arg = desc->args[i];
    int swizzled = i + !is_static;
    locals->entries[swizzled] = parameter_source(field_to_kind(&arg), i + 1 /* 1-indexed */);
    // map nth local to nth argument if static, n+1th if nonstatic
    (*locals_swizzle)[j] = swizzled;
    if (is_field_wide(arg)) {
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
  const attribute_code *code;
  analy_stack_state stack, locals;
  bool stack_terminated;
  int *locals_swizzle;
  char *insn_error;
  heap_string *error;
  struct edge *edges;
};

// Pop a value from the analysis stack and return it.
#define POP_VAL                                                                                                        \
  ({                                                                                                                   \
    if (ctx->stack.entries_count == 0)                                                                                 \
      goto stack_underflow;                                                                                            \
    ctx->stack.entries[--ctx->stack.entries_count];                                                                    \
  })
// Pop a value from the analysis stack and assert its kind.
#define POP_KIND(kind)                                                                                                 \
  {                                                                                                                    \
    analy_stack_entry popped_kind = POP_VAL;                                                                           \
    if (kind != popped_kind.type)                                                                                      \
      goto stack_type_mismatch;                                                                                        \
  }
#define POP(kind) POP_KIND(TYPE_KIND_##kind)
// Push a kind to the analysis stack.
#define PUSH_ENTRY(kind)                                                                                               \
  {                                                                                                                    \
    if (ctx->stack.entries_count == ctx->stack.entries_cap)                                                            \
      goto stack_overflow;                                                                                             \
    if (kind.type != TYPE_KIND_VOID) {                                                                                 \
      ctx->stack.entries[ctx->stack.entries_count++] = kind;                                                           \
      if (kind.type == 0)                                                                                              \
        UNREACHABLE();                                                                                                 \
    }                                                                                                                  \
  }
#define PUSH(kind) PUSH_ENTRY(insn_source(TYPE_KIND_##kind, insn_index))

// Set the kind of the local variable, in pre-swizzled indices.
#define SET_LOCAL(index, kind)                                                                                         \
  {                                                                                                                    \
    if (index >= ctx->code->max_locals)                                                                                \
      goto local_overflow;                                                                                             \
    ctx->locals.entries[index] = local_source(TYPE_KIND_##kind, insn_index);                                           \
  }
// Remap the index to the new local variable index after unwidening.
#define SWIZZLE_LOCAL(index)                                                                                           \
  {                                                                                                                    \
    if (index >= ctx->code->max_locals)                                                                                \
      goto local_overflow;                                                                                             \
    index = ctx->locals_swizzle[index];                                                                                \
  }

#define CHECK_LOCAL(index, kind)                                                                                       \
  {                                                                                                                    \
    if (ctx->locals.entries[index].type != TYPE_KIND_##kind)                                                           \
      goto local_type_mismatch;                                                                                        \
  }

int push_branch_target(struct method_analysis_ctx *ctx, u32 curr, u32 target) {
  DCHECK((int)target < ctx->code->insn_count);
  struct edge e = (struct edge){.start = curr, .end = target};
  arrput(ctx->edges, e);
  return 0;
}

void calculate_tos_type(struct method_analysis_ctx *ctx, reduced_tos_kind *reduced) {
  if (ctx->stack.entries_count == 0) {
    *reduced = TOS_VOID;
  } else {
    switch (ctx->stack.entries[ctx->stack.entries_count - 1].type) {
    case TYPE_KIND_BOOLEAN:
    case TYPE_KIND_CHAR:
    case TYPE_KIND_BYTE:
    case TYPE_KIND_SHORT:
    case TYPE_KIND_INT:
    case TYPE_KIND_LONG:
    case TYPE_KIND_REFERENCE:
      *reduced = TOS_INT;
      break;
    case TYPE_KIND_FLOAT:
      *reduced = TOS_FLOAT;
      break;
    case TYPE_KIND_DOUBLE:
      *reduced = TOS_DOUBLE;
      break;
    case TYPE_KIND_VOID:
      UNREACHABLE();
    }
  }
}

int analyze_instruction(bytecode_insn *insn, int insn_index, struct method_analysis_ctx *ctx) {
  // Add top of stack type before the instruction executes
  calculate_tos_type(ctx, &insn->tos_before);

  switch (insn->kind) {
  case insn_nop:
  case insn_ret:
    break;
  case insn_aaload:
    POP(INT) POP(REFERENCE) PUSH(REFERENCE) break;
  case insn_aastore:
    POP(REFERENCE) POP(INT) POP(REFERENCE) break;
  case insn_aconst_null:
    PUSH(REFERENCE) break;
  case insn_areturn:
    POP(REFERENCE)
    ctx->stack_terminated = true;
    break;
  case insn_arraylength:
    POP(REFERENCE) PUSH(INT) break;
  case insn_athrow:
    POP(REFERENCE)
    ctx->stack_terminated = true;
    break;
  case insn_baload:
  case insn_caload:
  case insn_saload:
  case insn_iaload:
    POP(INT) POP(REFERENCE) PUSH(INT) break;
  case insn_bastore:
  case insn_castore:
  case insn_sastore:
  case insn_iastore:
    POP(INT) POP(INT) POP(REFERENCE) break;
  case insn_d2f:
    POP(DOUBLE) PUSH(FLOAT) break;
  case insn_d2i:
    POP(DOUBLE) PUSH(INT) break;
  case insn_d2l:
    POP(DOUBLE) PUSH(LONG) break;
  case insn_dadd:
  case insn_ddiv:
  case insn_dmul:
  case insn_drem:
  case insn_dsub:
    POP(DOUBLE) POP(DOUBLE) PUSH(DOUBLE) break;
  case insn_daload:
    POP(INT) POP(REFERENCE) PUSH(DOUBLE) break;
  case insn_dastore:
    POP(DOUBLE) POP(INT) POP(REFERENCE) break;
  case insn_dcmpg:
  case insn_dcmpl:
    POP(DOUBLE) POP(DOUBLE) PUSH(INT) break;
  case insn_dneg:
    POP(DOUBLE) PUSH(DOUBLE) break;
  case insn_dreturn:
    POP(DOUBLE)
    ctx->stack_terminated = true;
    break;
  case insn_dup: {
    if (ctx->stack.entries_count == 0)
      goto stack_underflow;
    analy_stack_entry kind = ctx->stack.entries[ctx->stack.entries_count - 1];
    PUSH_ENTRY(kind)
    break;
  }
  case insn_dup_x1: {
    if (ctx->stack.entries_count <= 1)
      goto stack_underflow;
    analy_stack_entry kind1 = POP_VAL, kind2 = POP_VAL;
    if (is_kind_wide(kind1.type) || is_kind_wide(kind2.type))
      goto stack_type_mismatch;
    PUSH_ENTRY(kind1) PUSH_ENTRY(kind2) PUSH_ENTRY(kind1) break;
  }
  case insn_dup_x2: {
    analy_stack_entry to_dup = POP_VAL, kind2 = POP_VAL, kind3;
    if (is_kind_wide(to_dup.type))
      goto stack_type_mismatch;
    if (is_kind_wide(kind2.type)) {
      PUSH_ENTRY(to_dup) PUSH_ENTRY(kind2) insn->kind = insn_dup_x1;
    } else {
      kind3 = POP_VAL;
      PUSH_ENTRY(to_dup) PUSH_ENTRY(kind3) PUSH_ENTRY(kind2)
    }
    PUSH_ENTRY(to_dup)
    break;
  }
  case insn_dup2: {
    analy_stack_entry to_dup = POP_VAL, kind2;
    if (is_kind_wide(to_dup.type)) {
      PUSH_ENTRY(to_dup) PUSH_ENTRY(to_dup) insn->kind = insn_dup;
    } else {
      kind2 = POP_VAL;
      if (is_kind_wide(kind2.type))
        goto stack_type_mismatch;
      PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup) PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup)
    }
    break;
  }
  case insn_dup2_x1: {
    analy_stack_entry to_dup = POP_VAL, kind2 = POP_VAL, kind3;
    if (is_kind_wide(to_dup.type)) {
      PUSH_ENTRY(to_dup)
      PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup) insn->kind = insn_dup_x1;
    } else {
      kind3 = POP_VAL;
      if (is_kind_wide(kind3.type))
        goto stack_type_mismatch;
      PUSH_ENTRY(kind2)
      PUSH_ENTRY(to_dup) PUSH_ENTRY(kind3) PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup)
    }
    break;
  }
  case insn_dup2_x2: {
    analy_stack_entry to_dup = POP_VAL, kind2 = POP_VAL, kind3, kind4;
    if (is_kind_wide(to_dup.type)) {
      if (is_kind_wide(kind2.type)) {
        PUSH_ENTRY(to_dup)
        PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup) insn->kind = insn_dup_x1;
      } else {
        kind3 = POP_VAL;
        if (is_kind_wide(kind3.type))
          goto stack_type_mismatch;
        PUSH_ENTRY(to_dup)
        PUSH_ENTRY(kind3)
        PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup) insn->kind = insn_dup_x2;
      }
    } else {
      kind3 = POP_VAL;
      if (is_kind_wide(kind3.type)) {
        PUSH_ENTRY(kind2)
        PUSH_ENTRY(to_dup)
        PUSH_ENTRY(kind3)
        PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup) insn->kind = insn_dup2_x1;
      } else {
        kind4 = POP_VAL;
        if (is_kind_wide(kind4.type))
          goto stack_type_mismatch;
        PUSH_ENTRY(kind2)
        PUSH_ENTRY(to_dup)
        PUSH_ENTRY(kind4) PUSH_ENTRY(kind3) PUSH_ENTRY(kind2) PUSH_ENTRY(to_dup)
      }
    }
    break;
  }
  case insn_f2d: {
    POP(FLOAT) PUSH(DOUBLE) break;
  }
  case insn_f2i: {
    POP(FLOAT) PUSH(INT) break;
  }
  case insn_f2l: {
    POP(FLOAT) PUSH(LONG) break;
  }
  case insn_fadd: {
    POP(FLOAT) POP(FLOAT) PUSH(FLOAT) break;
  }
  case insn_faload: {
    POP(INT) POP(REFERENCE) PUSH(FLOAT) break;
  }
  case insn_fastore: {
    POP(FLOAT) POP(INT) POP(REFERENCE) break;
  }
  case insn_fcmpg:
  case insn_fcmpl: {
    POP(FLOAT) POP(FLOAT) PUSH(INT) break;
  }
  case insn_fdiv:
  case insn_fmul:
  case insn_frem:
  case insn_fsub: {
    POP(FLOAT) POP(FLOAT) PUSH(FLOAT) break;
  }
  case insn_fneg: {
    POP(FLOAT) PUSH(FLOAT);
    break;
  }
  case insn_freturn: {
    POP(FLOAT)
    ctx->stack_terminated = true;
    break;
  }
  case insn_i2b:
  case insn_i2c: {
    POP(INT) PUSH(INT) break;
  }
  case insn_i2d: {
    POP(INT) PUSH(DOUBLE) break;
  }
  case insn_i2f: {
    POP(INT) PUSH(FLOAT) break;
  }
  case insn_i2l: {
    POP(INT) PUSH(LONG) break;
  }
  case insn_i2s: {
    POP(INT) PUSH(INT) break;
  }
  case insn_iadd:
  case insn_iand:
  case insn_idiv:
  case insn_imul:
  case insn_irem:
  case insn_ior:
  case insn_ishl:
  case insn_ishr:
  case insn_isub:
  case insn_ixor:
  case insn_iushr: {
    POP(INT) POP(INT) PUSH(INT)
  } break;
  case insn_ineg: {
    POP(INT) PUSH(INT) break;
  }
  case insn_ireturn: {
    POP(INT);
    break;
  }
  case insn_l2d: {
    POP(LONG) PUSH(DOUBLE) break;
  }
  case insn_l2f: {
    POP(LONG) PUSH(FLOAT) break;
  }
  case insn_l2i: {
    POP(LONG) PUSH(INT) break;
  }
  case insn_ladd:
  case insn_land:
  case insn_ldiv:
  case insn_lmul:
  case insn_lor:
  case insn_lrem:
  case insn_lsub:
  case insn_lxor: {
    POP(LONG) POP(LONG) PUSH(LONG) break;
  }
  case insn_lshl:
  case insn_lshr:
  case insn_lushr: {
    POP(INT) POP(LONG) PUSH(LONG) break;
  }
  case insn_laload: {
    POP(INT) POP(REFERENCE) PUSH(LONG) break;
  }
  case insn_lastore: {
    POP(LONG) POP(INT) POP(REFERENCE) break;
  }
  case insn_lcmp: {
    POP(LONG) POP(LONG) PUSH(INT) break;
  }
  case insn_lneg: {
    POP(LONG) PUSH(LONG) break;
  }
  case insn_lreturn: {
    POP(LONG)
    ctx->stack_terminated = true;
    break;
  }
  case insn_monitorenter: {
    POP(REFERENCE)
    break;
  }
  case insn_monitorexit: {
    POP(REFERENCE)
    break;
  }
  case insn_pop: {
    analy_stack_entry kind = POP_VAL;
    if (is_kind_wide(kind.type))
      goto stack_type_mismatch;
    break;
  }
  case insn_pop2: {
    analy_stack_entry kind = POP_VAL;
    if (!is_kind_wide(kind.type)) {
      analy_stack_entry kind2 = POP_VAL;
      if (is_kind_wide(kind2.type))
        goto stack_type_mismatch;
    } else {
      insn->kind = insn_pop;
    }
    break;
  }
  case insn_return: {
    ctx->stack_terminated = true;
    break;
  }
  case insn_swap: {
    analy_stack_entry kind1 = POP_VAL, kind2 = POP_VAL;
    if (is_kind_wide(kind1.type) || is_kind_wide(kind2.type))
      goto stack_type_mismatch;
    ;
    PUSH_ENTRY(kind1) PUSH_ENTRY(kind2) break;
  }
  case insn_anewarray: {
    POP(INT) PUSH(REFERENCE) break;
  }
  case insn_checkcast: {
    POP(REFERENCE) PUSH(REFERENCE) break;
  }
  case insn_getfield:
    POP(REFERENCE)
    [[fallthrough]];
  case insn_getstatic: {
    field_descriptor *field =
        check_cp_entry(insn->cp, CP_KIND_FIELD_REF, "getstatic/getfield argument")->field.parsed_descriptor;
    PUSH_ENTRY(insn_source(field_to_kind(field), insn_index));
    break;
  }
  case insn_instanceof: {
    POP(REFERENCE) PUSH(INT) break;
  }
  case insn_invokedynamic: {
    method_descriptor *descriptor =
        check_cp_entry(insn->cp, CP_KIND_INVOKE_DYNAMIC, "invokedynamic argument")->indy_info.method_descriptor;
    for (int j = descriptor->args_count - 1; j >= 0; --j) {
      field_descriptor *field = descriptor->args + j;
      POP_KIND(field_to_kind(field));
    }
    if (descriptor->return_type.base_kind != TYPE_KIND_VOID)
      PUSH_ENTRY(insn_source(field_to_kind(&descriptor->return_type), insn_index))
    break;
  }
  case insn_new: {
    PUSH(REFERENCE)
    break;
  }
  case insn_putfield:
  case insn_putstatic: {
    analy_stack_entry kind = POP_VAL;
    // TODO check that the field matches
    (void)kind;
    if (insn->kind == insn_putfield) {
      POP(REFERENCE)
    }
    break;
  }
  case insn_invokevirtual:
  case insn_invokespecial:
  case insn_invokeinterface:
  case insn_invokestatic: {
    method_descriptor *descriptor =
        check_cp_entry(insn->cp, CP_KIND_METHOD_REF | CP_KIND_INTERFACE_METHOD_REF, "invoke* argument")
            ->methodref.descriptor;
    for (int j = descriptor->args_count - 1; j >= 0; --j) {
      field_descriptor *field = descriptor->args + j;
      POP_KIND(field_to_kind(field))
    }
    if (insn->kind != insn_invokestatic) {
      POP(REFERENCE)
    }
    if (descriptor->return_type.base_kind != TYPE_KIND_VOID)
      PUSH_ENTRY(insn_source(field_to_kind(&descriptor->return_type), insn_index));
    break;
  }
  case insn_ldc: {
    cp_entry *ent = check_cp_entry(
        insn->cp, CP_KIND_INTEGER | CP_KIND_STRING | CP_KIND_FLOAT | CP_KIND_CLASS | CP_KIND_DYNAMIC_CONSTANT,
        "ldc argument");
    type_kind kind = ent->kind == CP_KIND_INTEGER ? TYPE_KIND_INT
                     : ent->kind == CP_KIND_FLOAT ? TYPE_KIND_FLOAT
                                                  : TYPE_KIND_REFERENCE;
    PUSH_ENTRY(insn_source(kind, insn_index))
    if (ent->kind == CP_KIND_INTEGER) { // rewrite to iconst or lconst
      insn->kind = insn_iconst;
      insn->integer_imm = (s32)ent->integral.value;
    } else if (ent->kind == CP_KIND_FLOAT) {
      insn->kind = insn_fconst;
      insn->f_imm = (float)ent->floating.value;
    }
    break;
  }
  case insn_ldc2_w: {
    cp_entry *ent = check_cp_entry(insn->cp, CP_KIND_DOUBLE | CP_KIND_LONG, "ldc2_w argument");
    type_kind kind = ent->kind == CP_KIND_DOUBLE ? TYPE_KIND_DOUBLE : TYPE_KIND_LONG;
    PUSH_ENTRY(insn_source(kind, insn_index))
    if (ent->kind == CP_KIND_LONG) {
      insn->kind = insn_lconst;
      insn->integer_imm = ent->integral.value;
    } else {
      insn->kind = insn_dconst;
      insn->d_imm = ent->floating.value;
    }
    break;
  }
  case insn_dload: {
    SWIZZLE_LOCAL(insn->index)
    PUSH_ENTRY(ctx->locals.entries[insn->index])
    CHECK_LOCAL(insn->index, DOUBLE)
    break;
  }
  case insn_fload: {
    SWIZZLE_LOCAL(insn->index)
    PUSH_ENTRY(ctx->locals.entries[insn->index])
    CHECK_LOCAL(insn->index, FLOAT)
    break;
  }
  case insn_iload: {
    SWIZZLE_LOCAL(insn->index)
    PUSH_ENTRY(ctx->locals.entries[insn->index])
    CHECK_LOCAL(insn->index, INT)
    break;
  }
  case insn_lload: {
    SWIZZLE_LOCAL(insn->index)
    PUSH_ENTRY(ctx->locals.entries[insn->index])
    CHECK_LOCAL(insn->index, LONG)
    break;
  }
  case insn_dstore: {
    POP(DOUBLE)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, DOUBLE)
    break;
  }
  case insn_fstore: {
    POP(FLOAT)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, FLOAT)
    break;
  }
  case insn_istore: {
    POP(INT)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, INT)
    break;
  }
  case insn_lstore: {
    POP(LONG)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, LONG)
    break;
  }
  case insn_aload: {
    SWIZZLE_LOCAL(insn->index)
    PUSH_ENTRY(ctx->locals.entries[insn->index])
    CHECK_LOCAL(insn->index, REFERENCE)
    break;
  }
  case insn_astore: {
    POP(REFERENCE)
    SWIZZLE_LOCAL(insn->index)
    SET_LOCAL(insn->index, REFERENCE)
    break;
  }
  case insn_goto: {
    if (push_branch_target(ctx, insn_index, insn->index))
      goto error;
    ctx->stack_terminated = true;
    break;
  }
  case insn_jsr: {
    PUSH(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      goto error;
    break;
  }
  case insn_if_acmpeq:
  case insn_if_acmpne: {
    POP(REFERENCE)
    POP(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      goto error;
    break;
  }
  case insn_if_icmpeq:
  case insn_if_icmpne:
  case insn_if_icmplt:
  case insn_if_icmpge:
  case insn_if_icmpgt:
  case insn_if_icmple:
    POP(INT)
    [[fallthrough]];
  case insn_ifeq:
  case insn_ifne:
  case insn_iflt:
  case insn_ifge:
  case insn_ifgt:
  case insn_ifle: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->index))
      goto error;
    break;
  }
  case insn_ifnonnull:
  case insn_ifnull: {
    POP(REFERENCE)
    if (push_branch_target(ctx, insn_index, insn->index))
      goto error;
    break;
  }
  case insn_iconst:
    PUSH(INT) break;
  case insn_dconst:
    PUSH(DOUBLE) break;
  case insn_fconst:
    PUSH(FLOAT) break;
  case insn_lconst:
    PUSH(LONG) break;
  case insn_iinc:
    SWIZZLE_LOCAL(insn->iinc.index);
    break;
  case insn_multianewarray: {
    for (int i = 0; i < insn->multianewarray->dimensions; ++i)
      POP(INT)
    PUSH(REFERENCE)
    break;
  }
  case insn_newarray: {
    POP(INT) PUSH(REFERENCE) break;
  }
  case insn_tableswitch: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->tableswitch->default_target))
      goto error;
    for (int i = 0; i < insn->tableswitch->targets_count; ++i)
      if (push_branch_target(ctx, insn_index, insn->tableswitch->targets[i]))
        goto error;
    ctx->stack_terminated = true;
    break;
  }
  case insn_lookupswitch: {
    POP(INT)
    if (push_branch_target(ctx, insn_index, insn->lookupswitch->default_target))
      goto error;
    for (int i = 0; i < insn->lookupswitch->targets_count; ++i)
      if (push_branch_target(ctx, insn_index, insn->lookupswitch->targets[i]))
        goto error;
    ctx->stack_terminated = true;
    break;
  }
  default: // instruction shouldn't come out of the parser
    UNREACHABLE();
  }

  // Add top of stack type after the instruction executes
  calculate_tos_type(ctx, &insn->tos_after);

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
  ctx->insn_error = strdup("Stack type mismatch:");
error:;

  *ctx->error = make_heap_str(50000);
  heap_string insn_str = insn_to_string(insn, insn_index);
  char *stack_str = print_analy_stack_state(&ctx->stack);
  char *locals_str = print_analy_stack_state(&ctx->locals);
  heap_string context = code_attribute_to_string(ctx->code);
  bprintf(hslc(*ctx->error),
          "%s\nInstruction: %.*s\nStack preceding insn: %s\nLocals state: "
          "%s\nContext:\n%.*s\n",
          ctx->insn_error, fmt_slice(insn_str), stack_str, locals_str, fmt_slice(context));
  free_heap_str(insn_str);
  free(stack_str);
  free(locals_str);
  free_heap_str(context);
  free(ctx->insn_error);
  return -1;
}

static type_kind validation_type_kind_to_representation(stack_map_frame_validation_type_kind kind) {
  switch (kind) {
  case STACK_MAP_FRAME_VALIDATION_TYPE_INTEGER:
    return TYPE_KIND_INT;
  case STACK_MAP_FRAME_VALIDATION_TYPE_FLOAT:
    return TYPE_KIND_FLOAT;
  case STACK_MAP_FRAME_VALIDATION_TYPE_LONG:
    return TYPE_KIND_LONG;
  case STACK_MAP_FRAME_VALIDATION_TYPE_DOUBLE:
    return TYPE_KIND_DOUBLE;
  case STACK_MAP_FRAME_VALIDATION_TYPE_OBJECT:
  case STACK_MAP_FRAME_VALIDATION_TYPE_NULL:
  case STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT_THIS:
  case STACK_MAP_FRAME_VALIDATION_TYPE_UNINIT:
    return TYPE_KIND_REFERENCE;
  case STACK_MAP_FRAME_VALIDATION_TYPE_TOP:
    return TYPE_KIND_VOID;
  }
  UNREACHABLE();
}

void use_stack_map_frame(struct method_analysis_ctx *ctx, const stack_map_frame_iterator *iter) {
  ctx->stack.entries_count = iter->stack_size;
  for (int i = 0; i < iter->stack_size; ++i) {
    ctx->stack.entries[i].type = validation_type_kind_to_representation(iter->stack[i].kind);
  }
  // First set all locals to void
  for (int i = 0; i < ctx->locals.entries_count; ++i) {
    ctx->locals.entries[i].type = TYPE_KIND_VOID;
  }
  // Then use iterator locals, but don't forget to swizzle
  for (int i = 0; i < iter->locals_size; ++i) {
    ctx->locals.entries[ctx->locals_swizzle[i]].type = validation_type_kind_to_representation(iter->locals[i].kind);
  }
}

/**
 * Analyze the method's code attribute if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis, and returning an error string upon some sort of error.
 */
int analyze_method_code(cp_method *method, heap_string *error) {
  attribute_code *code = method->code;
  if (!code || method->code_analysis) {
    return 0;
  }
  struct method_analysis_ctx ctx = {code};

  // Swizzle local entries so that the first n arguments correspond to the first
  // n locals (i.e., we should remap aload #1 to aload swizzle[#1])
  if (locals_on_method_entry(method, &ctx.locals, &ctx.locals_swizzle)) {
    return -1;
  }

  int result = 0;
  ctx.stack.entries = calloc(code->max_stack + 1, sizeof(analy_stack_entry));
  ctx.stack.entries_cap = code->max_stack + 1;

  // After jumps, we can infer the stack and locals at these points
  u16 *insn_index_to_stack_depth = calloc(code->insn_count, sizeof(u16));

  if (code->local_variable_table) {
    for (int i = 0; i < code->local_variable_table->entries_count; ++i) {
      attribute_lvt_entry *ent = &code->local_variable_table->entries[i];
      if (ent->index >= code->max_locals) {
        result = -1;
        goto invalid_vt;
      }
      ent->index = ctx.locals_swizzle[ent->index];
    }
  }

  ctx.error = error;

  code_analysis *analy = method->code_analysis = malloc(sizeof(code_analysis));

  stack_map_frame_iterator iter;
  stack_map_frame_iterator_init(&iter, method);

  analy->insn_count = code->insn_count;
  analy->dominator_tree_computed = false;
  for (int i = 0; i < 5; ++i) {
    analy->insn_index_to_kinds[i] = calloc(code->insn_count, sizeof(compressed_bitset));
  }
  analy->blocks = nullptr;
  analy->insn_index_to_stack_depth = insn_index_to_stack_depth;
  analy->sources = calloc(code->insn_count, sizeof(*analy->sources));

  for (int i = 0; i < code->insn_count; ++i) {
    bytecode_insn *insn = &code->code[i];
    if (insn->original_pc == iter.pc) {
      use_stack_map_frame(&ctx, &iter);
      if (stack_map_frame_iterator_has_next(&iter)) {
        const char *c_str_error;
        if (stack_map_frame_iterator_next(&iter, &c_str_error)) { // stackmaptable issue
          *error = make_heap_str(strlen(c_str_error));
          strncpy(error->chars, c_str_error, error->len);
          result = -1;
          goto invalid_vt;
        }
      }
    }

    stack_variable_source a = {}, b = {};
    analy_stack_state *stack = &ctx.stack;
    int sd = stack->entries_count;

    // Instructions that can intrinsically raise NPE
    switch (insn->kind) {
    case insn_aload:
    case insn_iload:
    case insn_lload:
    case insn_fload:
    case insn_dload:
    case insn_astore:
    case insn_istore:
    case insn_lstore:
    case insn_fstore:
    case insn_dstore:
      break;
    case insn_putfield:
    case insn_aaload:
    case insn_baload:
    case insn_caload:
    case insn_faload:
    case insn_iaload:
    case insn_daload:
    case insn_laload:
    case insn_saload: {
      a = stack->entries[sd - 2].source;
      b = stack->entries[sd - 1].source;
      break;
    }
    case insn_aastore:
    case insn_bastore:
    case insn_castore:
    case insn_fastore:
    case insn_dastore:
    case insn_iastore:
    case insn_lastore:
    case insn_sastore: {
      a = stack->entries[sd - 3].source; // trying to store into a null array
      b = stack->entries[sd - 2].source;
      break;
    }
    case insn_invokevirtual:
    case insn_invokeinterface:
    case insn_invokespecial: { // Trying to invoke on an object
      int argc = insn->cp->methodref.descriptor->args_count;
      a = stack->entries[sd - argc].source;
      break;
    }
    case insn_arraylength:
    case insn_athrow:
    case insn_monitorenter:
    case insn_monitorexit:
    case insn_getfield: {
      a = stack->entries[sd - 1].source;
      break;
    }
    default:
      break;
    }
    analy->sources[i].a = a;
    analy->sources[i].b = b;

    insn_index_to_stack_depth[i] = stack->entries_count;

    for (int j = 0; j < 5; ++j) {
      type_kind order[5] = {TYPE_KIND_REFERENCE, TYPE_KIND_INT, TYPE_KIND_FLOAT, TYPE_KIND_DOUBLE, TYPE_KIND_LONG};
      compressed_bitset *bitset = analy->insn_index_to_kinds[j] + i;
      init_compressed_bitset(bitset, code->max_stack + code->max_locals);

      write_kinds_to_bitset(&ctx.stack, 0, bitset, order[j]);
      write_kinds_to_bitset(&ctx.locals, code->max_stack, bitset, order[j]);
    }

    if (analyze_instruction(insn, i, &ctx)) {
      result = -1;
      goto invalid_vt;
    }
  }

invalid_vt:
  stack_map_frame_iterator_uninit(&iter);
  free(ctx.stack.entries);
  free(ctx.locals.entries);
  free(ctx.locals_swizzle);
  arrfree(ctx.edges);

  return result;
}

void free_code_analysis(code_analysis *analy) {
  if (!analy)
    return;
  if (analy->insn_index_to_references) {
    for (int j = 0; j < 5; ++j) {
      for (int i = 0; i < analy->insn_count; ++i)
        free_compressed_bitset(analy->insn_index_to_kinds[j][i]);
      free(analy->insn_index_to_kinds[j]);
    }
  }
  if (analy->blocks) {
    for (int i = 0; i < analy->block_count; ++i) {
      arrfree(analy->blocks[i].next);
      free(analy->blocks[i].is_backedge);
      arrfree(analy->blocks[i].prev);
      arrfree(analy->blocks[i].idominates.list);
    }
    free(analy->blocks);
  }
  free(analy->sources);
  free(analy->insn_index_to_stack_depth);
  free(analy);
}

static void push_bb_branch(basic_block *current, basic_block *next) { arrput(current->next, next->my_index); }

static int cmp_ints(const void *a, const void *b) { return *(int *)a - *(int *)b; }

// Used to find which blocks are accessible from the entry without throwing
// exceptions.
void dfs_nothrow_accessible(basic_block *bs, int i) {
  basic_block *b = bs + i;
  if (b->nothrow_accessible)
    return;
  b->nothrow_accessible = true;
  for (int j = 0; j < arrlen(b->next); ++j)
    dfs_nothrow_accessible(bs, b->next[j]);
}

// Scan basic blocks in the code. Code that is not accessible without throwing
// an exception is DELETED because we're not handling exceptions at all in
// JIT compiled code. (Once an exception is thrown in a frame, it is
// interpreted for the rest of its life.)
int scan_basic_blocks(const attribute_code *code, code_analysis *analy) {
  DCHECK(analy);
  if (analy->blocks)
    return 0; // already done
  // First, record all branch targets.
  int *ts = calloc(code->max_formal_pc, sizeof(u32));
  int tc = 0;
  ts[tc++] = 0; // mark entry point
  for (int i = 0; i < code->insn_count; ++i) {
    const bytecode_insn *insn = code->code + i;
    if (insn->kind >= insn_goto && insn->kind <= insn_ifnull) {
      ts[tc++] = insn->index;
      if (insn->kind != insn_goto)
        ts[tc++] = i + 1; // fallthrough
    } else if (insn->kind == insn_tableswitch || insn->kind == insn_lookupswitch) {
      const struct bc_tableswitch_data *tsd = insn->tableswitch;
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
  basic_block *bs = calloc(++block_count, sizeof(basic_block));
  for (int i = 0; i < block_count; ++i) {
    bs[i].start_index = ts[i];
    bs[i].start = code->code + ts[i];
    bs[i].insn_count = i + 1 < block_count ? ts[i + 1] - ts[i] : code->insn_count - ts[i];
    bs[i].my_index = i;
  }
#define FIND_TARGET_BLOCK(index) &bs[(int *)bsearch(&index, ts, block_count, sizeof(int), cmp_ints) - ts]
  // Then, record edges between bbs.
  for (int block_i = 0; block_i < block_count; ++block_i) {
    basic_block *b = bs + block_i;
    const bytecode_insn *last = b->start + b->insn_count - 1;
    if (last->kind >= insn_goto && last->kind <= insn_ifnull) {
      push_bb_branch(b, FIND_TARGET_BLOCK(last->index));
      if (last->kind == insn_goto)
        continue;
    } else if (last->kind == insn_tableswitch || last->kind == insn_lookupswitch) {
      const struct bc_tableswitch_data *tsd = last->tableswitch;
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
    basic_block *b = bs + block_i;
    b->my_index = block_i;
    for (int j = 0; j < arrlen(b->next); ++j) {
      basic_block *next = bs + (b->next[j] = ts[b->next[j]]);
      arrput(next->prev, block_i);
    }
  }
  // Create some allocations for later analyses
  for (int block_i = 0; block_i < block_count; ++block_i) {
    basic_block *b = bs + block_i;
    b->is_backedge = calloc(arrlen(b->next), sizeof(bool));
  }
  analy->block_count = block_count;
  analy->blocks = bs;
  free(ts);
  return 0;
#undef FIND_TARGET_BLOCK
}

static void get_dfs_tree(basic_block *block, int *block_to_pre, int *preorder, int *parent, int *preorder_clock,
                         int *postorder_clock) {
  preorder[*preorder_clock] = block->my_index;
  block_to_pre[block->my_index] = block->dfs_pre = (*preorder_clock)++;
  for (int j = 0; j < arrlen(block->next); ++j) {
    if (block_to_pre[block->next[j]] == -1) {
      parent[block->next[j]] = block->my_index;
      get_dfs_tree(block - block->my_index + block->next[j], block_to_pre, preorder, parent, preorder_clock,
                   postorder_clock);
    }
  }
  block->dfs_post = (*postorder_clock)++;
}

static void idom_dfs(basic_block *block, int *visited, u32 *clock) {
  block->idom_pre = (*clock)++;
  visited[block->my_index] = 1;
  dominated_list_t *dlist = &block->idominates;
  for (int i = 0; i < arrlen(dlist->list); ++i) {
    int next = dlist->list[i];
    if (!visited[next])
      idom_dfs(block + next - block->my_index, visited, clock);
  }
  block->idom_post = (*clock)++;
}

// The classic Lengauer-Tarjan algorithm for dominator tree computation
void compute_dominator_tree(code_analysis *analy) {
  // dump_cfg_to_graphviz(stderr, analy);
  DCHECK(analy->blocks, "Basic blocks must have been already scanned");
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
    basic_block *b = analy->blocks + i;
    int sd = INT_MAX; // preorder, not block #
    // Go through predecessor blocks in any order
    for (int prev_i = 0; prev_i < arrlen(b->prev); ++prev_i) {
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
    dominated_list_t *sdlist = &analy->blocks[pre_to_block[sd]].idominates;
    arrput(sdlist->list, i);
    F[i] = parent[i];
  }
  // Compute relative dominators
  int *reldom = calloc(block_count, sizeof(int));
  for (int i = 1; i < block_count; ++i) {
    dominated_list_t *sdlist = &analy->blocks[i].idominates;
    for (int list_i = 0; list_i < arrlen(sdlist->list); ++list_i) {
      int w = sdlist->list[list_i], walk = w, min = INT_MAX;
      DCHECK(semidom[w] == i, "Algorithm invariant");
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
    arrsetlen(analy->blocks[i].idominates.list, 0);
  for (int preorder_i = 1; preorder_i < block_count; ++preorder_i) {
    int i = pre_to_block[preorder_i];
    int idom = analy->blocks[i].idom = i == reldom[i] ? semidom[i] : (s32)analy->blocks[reldom[i]].idom;
    dominated_list_t *sdlist = &analy->blocks[idom].idominates;
    arrput(sdlist->list, i);
  }
  free(block_to_pre);
  free(pre_to_block);
  free(parent);
  free(semidom);
  free(reldom);
  // Now compute a DFS tree on the immediate dominator tree (still need a
  // "visited" array because there are duplicate edges)
  u32 clock = 1;
  // Re-use F as the visited array
  memset(F, 0, block_count * sizeof(int));
  idom_dfs(analy->blocks, F, &clock);
  free(F);
}

bool query_dominance(const basic_block *dominator, const basic_block *dominated) {
  DCHECK(dominator->idom_pre != 0, "dominator tree not computed");
  return dominator->idom_pre <= dominated->idom_pre && dominator->idom_post >= dominated->idom_post;
}

// Check whether the CFG is reducible
static int forward_edges_form_a_cycle(code_analysis *analy, int i, int *visited) {
  basic_block *b = analy->blocks + i;
  visited[i] = 1;
  for (int j = 0; j < arrlen(b->next); ++j) {
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

int attempt_reduce_cfg(code_analysis *analy) {
  // mark back-edges
  for (int i = 0; i < analy->block_count; ++i) {
    basic_block *b = analy->blocks + i, *next;
    for (int j = 0; j < arrlen(b->next); ++j) {
      next = analy->blocks + b->next[j];
      b->is_backedge[j] = query_dominance(next, b);
      next->is_loop_header |= b->is_backedge[j];
    }
  }

  int *visited = calloc(analy->block_count, sizeof(int));
  int fail = forward_edges_form_a_cycle(analy, 0, visited);
  free(visited);
  return fail;
}

void dump_cfg_to_graphviz(FILE *out, const code_analysis *analysis) {
  fprintf(out, "digraph cfg {\n");
  for (int i = 0; i < analysis->block_count; i++) {
    basic_block *b = &analysis->blocks[i];
    fprintf(out, "    %d [label=\"Block %d\"];\n", b->my_index, b->my_index);
  }
  for (int i = 0; i < analysis->block_count; i++) {
    basic_block *b = &analysis->blocks[i];
    for (int j = 0; j < arrlen(b->next); j++) {
      fprintf(out, "    %d -> %u;\n", b->my_index, b->next[j]);
    }
  }
  fprintf(out, "}\n");
}

void replace_slashes(char *str, int len) {
  for (int i = 0; i < len; ++i) {
    if (str[i] == '/') {
      str[i] = '.';
    }
  }
}

void stringify_type(string_builder *B, const field_descriptor *F) {
  switch (F->base_kind) {
  case TYPE_KIND_REFERENCE:
    string_builder_append(B, "%.*s", fmt_slice(F->class_name));
    replace_slashes(B->data + B->write_pos - F->class_name.len, F->class_name.len);
    break;
  default:
    string_builder_append(B, "%s", type_kind_to_string(F->base_kind));
  }
  for (int i = 0; i < F->dimensions; ++i) {
    string_builder_append(B, "[]");
  }
}

void stringify_method(string_builder *B, const cp_method_info *M) {
  INIT_STACK_STRING(no_slashes, 1024);
  exchange_slashes_and_dots(&no_slashes, M->class_info->name);
  string_builder_append(B, "%.*s.%.*s(", fmt_slice(no_slashes), fmt_slice(M->nat->name));
  for (int i = 0; i < M->descriptor->args_count; ++i) {
    if (i > 0)
      string_builder_append(B, ", ");
    stringify_type(B, M->descriptor->args + i);
  }
  string_builder_append(B, ")");
}

static int extended_npe_phase2(const cp_method *method, stack_variable_source *source, int insn_i,
                               string_builder *builder, bool is_first) {
  code_analysis *analy = method->code_analysis;
  attribute_local_variable_table *lvt = method->code->local_variable_table;
  int original_pc = method->code->code[insn_i].original_pc;
  const slice *ent;

  switch (source->kind) {
  case VARIABLE_SRC_KIND_PARAMETER:
    if (source->index == 0 && !(method->access_flags & ACCESS_STATIC)) {
      string_builder_append(builder, "this");
    } else {
      if (lvt && ((ent = lvt_lookup(source->index, original_pc, lvt)))) {
        string_builder_append(builder, "%.*s", fmt_slice(*ent));
      } else {
        string_builder_append(builder, "<parameter%d>", source->index);
      }
    }
    break;
  case VARIABLE_SRC_KIND_LOCAL: {
    int index = source->index;
    DCHECK(index >= 0 && index < method->code->insn_count);
    bytecode_insn *insn = method->code->code + index;

    if (lvt && ((ent = lvt_lookup(insn->index, original_pc, lvt)))) {
      string_builder_append(builder, "%.*s", fmt_slice(*ent));
    } else {
      string_builder_append(builder, "<local%d>", insn->index);
    }
    break;
  }
  case VARIABLE_SRC_KIND_INSN: {
    int index = source->index;
    DCHECK(index >= 0 && index < method->code->insn_count);
    bytecode_insn *insn = method->code->code + index;

    switch (insn->kind) {
    case insn_aconst_null:
      string_builder_append(builder, "\"null\"");
      break;
    case insn_aaload: {
      // <a>[<b>]
      extended_npe_phase2(method, &analy->sources[index].a, index, builder, false);
      string_builder_append(builder, "[");
      extended_npe_phase2(method, &analy->sources[index].b, index, builder, false);
      string_builder_append(builder, "]");
      break;
    }
    case insn_getfield:
    case insn_getfield_B:
    case insn_getfield_C:
    case insn_getfield_S:
    case insn_getfield_I:
    case insn_getfield_J:
    case insn_getfield_F:
    case insn_getfield_D:
    case insn_getfield_Z:
    case insn_getfield_L: {
      // <a>.name or just "name" if a can't be resolved
      int err = extended_npe_phase2(method, &analy->sources[index].a, index, builder, false);
      if (!err) {
        string_builder_append(builder, ".");
      }
      string_builder_append(builder, "%.*s", fmt_slice(insn->cp->field.nat->name));
      break;
    case insn_getstatic:
    case insn_getstatic_B:
    case insn_getstatic_C:
    case insn_getstatic_S:
    case insn_getstatic_I:
    case insn_getstatic_J:
    case insn_getstatic_F:
    case insn_getstatic_D:
    case insn_getstatic_Z:
    case insn_getstatic_L: {
      // Class.name
      string_builder_append(builder, "%.*s.%.*s", fmt_slice(insn->cp->field.class_info->name),
                            fmt_slice(insn->cp->field.nat->name));
      break;
    }
    case insn_invokevirtual:
    case insn_invokeinterface:
    case insn_invokespecial:
    case insn_invokespecial_resolved:
    case insn_invokestatic:
    case insn_invokestatic_resolved:
    case insn_invokeitable_monomorphic:
    case insn_invokeitable_polymorphic:
    case insn_invokevtable_monomorphic:
    case insn_invokevtable_polymorphic: {
      if (is_first) {
        string_builder_append(builder, "the return value of ");
      }
      stringify_method(builder, &insn->cp->methodref);
      break;
    }
    case insn_iconst: {
      string_builder_append(builder, "%d", (int)insn->integer_imm);
      break;
    }
    default: {
      return -1;
    }
    }
    }
    break;
  case VARIABLE_SRC_KIND_UNK: {
    string_builder_append(builder, "...");
    return -1;
  }
  }
  }
  return 0;
}

// If a NullPointerException is thrown by the given instruction, generate a message like "Cannot load from char array
// because the return value of "charAt" is null".
int get_extended_npe_message(cp_method *method, u16 pc, heap_string *result) {
  // See https://openjdk.org/jeps/358 for more information on how this works, but there are basically two phases: One
  // which depends on the particular instruction that failed (e.g. caload -> cannot load from char array) and the other
  // which uses the instruction's sources to produce a more informative message.
  int err = 0;
  code_analysis *analy = method->code_analysis;
  attribute_code *code = method->code;
  if (!analy || !code || pc >= code->insn_count)
    return -1;

  bytecode_insn *faulting_insn = method->code->code + pc;
  string_builder builder, phase2_builder;
  string_builder_init(&builder);
  string_builder_init(&phase2_builder);

#undef CASE
#define CASE(insn, r)                                                                                                  \
  case insn:                                                                                                           \
    string_builder_append(&builder, "%s", r);                                                                          \
    break;

  switch (faulting_insn->kind) {
    CASE(insn_aaload, "Cannot load from object array")
    CASE(insn_baload, "Cannot load from byte array")
    CASE(insn_caload, "Cannot load from char array")
    CASE(insn_laload, "Cannot load from long array")
    CASE(insn_iaload, "Cannot load from int array")
    CASE(insn_saload, "Cannot load from short array")
    CASE(insn_faload, "Cannot load from float array")
    CASE(insn_daload, "Cannot load from double array")
    CASE(insn_aastore, "Cannot store to object array")
    CASE(insn_bastore, "Cannot store to byte array")
    CASE(insn_castore, "Cannot store to char array")
    CASE(insn_iastore, "Cannot store to int array")
    CASE(insn_lastore, "Cannot store to long array")
    CASE(insn_sastore, "Cannot store to short array")
    CASE(insn_fastore, "Cannot store to float array")
    CASE(insn_dastore, "Cannot store to double array")
    CASE(insn_arraylength, "Cannot read the array length")
    CASE(insn_athrow, "Cannot throw exception")
    CASE(insn_monitorenter, "Cannot enter synchronized block")
    CASE(insn_monitorexit, "Cannot exit synchronized block")
  case insn_getfield:
  case insn_getfield_B:
  case insn_getfield_C:
  case insn_getfield_S:
  case insn_getfield_I:
  case insn_getfield_J:
  case insn_getfield_F:
  case insn_getfield_D:
  case insn_getfield_Z:
  case insn_getfield_L:
    string_builder_append(&builder, "Cannot read field \"%.*s\"", fmt_slice(faulting_insn->cp->field.nat->name));
    break;
  case insn_putfield:
  case insn_putfield_B:
  case insn_putfield_C:
  case insn_putfield_S:
  case insn_putfield_I:
  case insn_putfield_J:
  case insn_putfield_F:
  case insn_putfield_D:
  case insn_putfield_Z:
  case insn_putfield_L:
    string_builder_append(&builder, "Cannot assign field \"%.*s\"", fmt_slice(faulting_insn->cp->field.nat->name));
    break;
  case insn_invokevirtual:
  case insn_invokeinterface:
  case insn_invokespecial:
  case insn_invokespecial_resolved:
  case insn_invokeitable_monomorphic:
  case insn_invokeitable_polymorphic:
  case insn_invokevtable_monomorphic:
  case insn_invokevtable_polymorphic: {
    cp_method_info *invoked = &faulting_insn->cp->methodref;
    string_builder_append(&builder, "Cannot invoke \"");
    stringify_method(&builder, invoked);
    string_builder_append(&builder, "\"");
    break;
  }
  default:
    err = -1;
    goto error;
  }

  int phase2_fail = extended_npe_phase2(method, &analy->sources[pc].a, pc, &phase2_builder, true);
  if (!phase2_fail) {
    string_builder_append(&builder, " because \"%.*s\" is null", phase2_builder.write_pos, phase2_builder.data);
  }

  *result = make_heap_str_from((slice){builder.data, builder.write_pos});

error:
  string_builder_free(&builder);
  string_builder_free(&phase2_builder);
  return err;
}