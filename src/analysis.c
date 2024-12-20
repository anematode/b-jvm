//
// Created by alec on 12/18/24.
//

#include <assert.h>
#include <stdlib.h>

#include "analysis.h"
#include "classfile.h"

const char *insn_code_name(bjvm_insn_code_kind code) {
  switch (code) {
  case bjvm_insn_aaload:
    return "aaload";
  case bjvm_insn_aastore:
    return "aastore";
  case bjvm_insn_aconst_null:
    return "aconst_null";
  case bjvm_insn_areturn:
    return "areturn";
  case bjvm_insn_arraylength:
    return "arraylength";
  case bjvm_insn_athrow:
    return "athrow";
  case bjvm_insn_baload:
    return "baload";
  case bjvm_insn_bastore:
    return "bastore";
  case bjvm_insn_caload:
    return "caload";
  case bjvm_insn_castore:
    return "castore";
  case bjvm_insn_d2f:
    return "d2f";
  case bjvm_insn_d2i:
    return "d2i";
  case bjvm_insn_d2l:
    return "d2l";
  case bjvm_insn_dadd:
    return "dadd";
  case bjvm_insn_daload:
    return "daload";
  case bjvm_insn_dastore:
    return "dastore";
  case bjvm_insn_dcmpg:
    return "dcmpg";
  case bjvm_insn_dcmpl:
    return "dcmpl";
  case bjvm_insn_ddiv:
    return "ddiv";
  case bjvm_insn_dmul:
    return "dmul";
  case bjvm_insn_dneg:
    return "dneg";
  case bjvm_insn_drem:
    return "drem";
  case bjvm_insn_dreturn:
    return "dreturn";
  case bjvm_insn_dsub:
    return "dsub";
  case bjvm_insn_dup:
    return "dup";
  case bjvm_insn_dup_x1:
    return "dup_x1";
  case bjvm_insn_dup_x2:
    return "dup_x2";
  case bjvm_insn_dup2:
    return "dup2";
  case bjvm_insn_dup2_x1:
    return "dup2_x1";
  case bjvm_insn_dup2_x2:
    return "dup2_x2";
  case bjvm_insn_f2d:
    return "f2d";
  case bjvm_insn_f2i:
    return "f2i";
  case bjvm_insn_f2l:
    return "f2l";
  case bjvm_insn_fadd:
    return "fadd";
  case bjvm_insn_faload:
    return "faload";
  case bjvm_insn_fastore:
    return "fastore";
  case bjvm_insn_fcmpg:
    return "fcmpg";
  case bjvm_insn_fcmpl:
    return "fcmpl";
  case bjvm_insn_fdiv:
    return "fdiv";
  case bjvm_insn_fmul:
    return "fmul";
  case bjvm_insn_fneg:
    return "fneg";
  case bjvm_insn_frem:
    return "frem";
  case bjvm_insn_freturn:
    return "freturn";
  case bjvm_insn_fsub:
    return "fsub";
  case bjvm_insn_i2b:
    return "i2b";
  case bjvm_insn_i2c:
    return "i2c";
  case bjvm_insn_i2d:
    return "i2d";
  case bjvm_insn_i2f:
    return "i2f";
  case bjvm_insn_i2l:
    return "i2l";
  case bjvm_insn_i2s:
    return "i2s";
  case bjvm_insn_iadd:
    return "iadd";
  case bjvm_insn_iaload:
    return "iaload";
  case bjvm_insn_iand:
    return "iand";
  case bjvm_insn_iastore:
    return "iastore";
  case bjvm_insn_idiv:
    return "idiv";
  case bjvm_insn_imul:
    return "imul";
  case bjvm_insn_ineg:
    return "ineg";
  case bjvm_insn_ior:
    return "ior";
  case bjvm_insn_irem:
    return "irem";
  case bjvm_insn_ireturn:
    return "ireturn";
  case bjvm_insn_ishl:
    return "ishl";
  case bjvm_insn_ishr:
    return "ishr";
  case bjvm_insn_isub:
    return "isub";
  case bjvm_insn_iushr:
    return "iushr";
  case bjvm_insn_ixor:
    return "ixor";
  case bjvm_insn_l2d:
    return "l2d";
  case bjvm_insn_l2f:
    return "l2f";
  case bjvm_insn_l2i:
    return "l2i";
  case bjvm_insn_ladd:
    return "ladd";
  case bjvm_insn_laload:
    return "laload";
  case bjvm_insn_land:
    return "land";
  case bjvm_insn_lastore:
    return "lastore";
  case bjvm_insn_lcmp:
    return "lcmp";
  case bjvm_insn_ldc:
    return "ldc";
  case bjvm_insn_ldc2_w:
    return "ldc2_w";
  case bjvm_insn_ldiv:
    return "ldiv";
  case bjvm_insn_lmul:
    return "lmul";
  case bjvm_insn_lneg:
    return "lneg";
  case bjvm_insn_lor:
    return "lor";
  case bjvm_insn_lrem:
    return "lrem";
  case bjvm_insn_lreturn:
    return "lreturn";
  case bjvm_insn_lshl:
    return "lshl";
  case bjvm_insn_lshr:
    return "lshr";
  case bjvm_insn_lsub:
    return "lsub";
  case bjvm_insn_lushr:
    return "lushr";
  case bjvm_insn_lxor:
    return "lxor";
  case bjvm_insn_monitorenter:
    return "monitorenter";
  case bjvm_insn_monitorexit:
    return "monitorexit";
  case bjvm_insn_nop:
    return "nop";
  case bjvm_insn_pop:
    return "pop";
  case bjvm_insn_pop2:
    return "pop2";
  case bjvm_insn_return:
    return "return_";
  case bjvm_insn_saload:
    return "saload";
  case bjvm_insn_sastore:
    return "sastore";
  case bjvm_insn_swap:
    return "swap";
  case bjvm_insn_dload:
    return "dload";
  case bjvm_insn_fload:
    return "fload";
  case bjvm_insn_iload:
    return "iload";
  case bjvm_insn_lload:
    return "lload";
  case bjvm_insn_dstore:
    return "dstore";
  case bjvm_insn_fstore:
    return "fstore";
  case bjvm_insn_istore:
    return "istore";
  case bjvm_insn_lstore:
    return "lstore";
  case bjvm_insn_aload:
    return "aload";
  case bjvm_insn_astore:
    return "astore";
  case bjvm_insn_anewarray:
    return "anewarray";
  case bjvm_insn_checkcast:
    return "checkcast";
  case bjvm_insn_getfield:
    return "getfield";
  case bjvm_insn_getstatic:
    return "getstatic";
  case bjvm_insn_instanceof:
    return "instanceof";
  case bjvm_insn_invokedynamic:
    return "invokedynamic";
  case bjvm_insn_new:
    return "new";
  case bjvm_insn_putfield:
    return "putfield";
  case bjvm_insn_putstatic:
    return "putstatic";
  case bjvm_insn_invokevirtual:
    return "invokevirtual";
  case bjvm_insn_invokespecial:
    return "invokespecial";
  case bjvm_insn_invokestatic:
    return "invokestatic";
  case bjvm_insn_goto:
    return "goto";
  case bjvm_insn_jsr:
    return "jsr";
  case bjvm_insn_ret:
    return "ret";
  case bjvm_insn_if_acmpeq:
    return "if_acmpeq";
  case bjvm_insn_if_acmpne:
    return "if_acmpne";
  case bjvm_insn_if_icmpeq:
    return "if_icmpeq";
  case bjvm_insn_if_icmpne:
    return "if_icmpne";
  case bjvm_insn_if_icmplt:
    return "if_icmplt";
  case bjvm_insn_if_icmpge:
    return "if_icmpge";
  case bjvm_insn_if_icmpgt:
    return "if_icmpgt";
  case bjvm_insn_if_icmple:
    return "if_icmple";
  case bjvm_insn_ifeq:
    return "ifeq";
  case bjvm_insn_ifne:
    return "ifne";
  case bjvm_insn_iflt:
    return "iflt";
  case bjvm_insn_ifge:
    return "ifge";
  case bjvm_insn_ifgt:
    return "ifgt";
  case bjvm_insn_ifle:
    return "ifle";
  case bjvm_insn_ifnonnull:
    return "ifnonnull";
  case bjvm_insn_ifnull:
    return "ifnull";
  case bjvm_insn_iconst:
    return "iconst";
  case bjvm_insn_dconst:
    return "dconst";
  case bjvm_insn_fconst:
    return "fconst";
  case bjvm_insn_lconst:
    return "lconst";
  case bjvm_insn_iinc:
    return "iinc";
  case bjvm_insn_invokeinterface:
    return "invokeinterface";
  case bjvm_insn_multianewarray:
    return "multianewarray";
  case bjvm_insn_newarray:
    return "newarray";
  case bjvm_insn_tableswitch:
    return "tableswitch";
  case bjvm_insn_lookupswitch:
    return "lookupswitch";
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
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
    return "<retaddr>";
  }
  UNREACHABLE();
}

char *class_info_entry_to_string(const bjvm_cp_class_info *ent) {
  char result[1000];
  snprintf(result, sizeof(result), "Class: %.*s", ent->name.len, ent->name.chars);
  return strdup(result);
}

char *
name_and_type_entry_to_string(const bjvm_cp_name_and_type *name_and_type) {
  char result[1000];
  snprintf(result, sizeof(result), "NameAndType: %.*s:%.*s",
           name_and_type->name.len, name_and_type->name.chars, name_and_type->descriptor.len, name_and_type->descriptor.chars);
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
    return lossy_utf8_entry_to_chars(hslc(ent->utf8));
  case BJVM_CP_KIND_INTEGER:
    snprintf(result, sizeof(result), "%d", (int)ent->integral.value);
    break;
  case BJVM_CP_KIND_FLOAT:
    snprintf(result, sizeof(result), "%.9gf", (float)ent->floating.value);
    break;
  case BJVM_CP_KIND_LONG:
    snprintf(result, sizeof(result), "%lldL", ent->integral.value);
    break;
  case BJVM_CP_KIND_DOUBLE:
    snprintf(result, sizeof(result), "%.15gd", (float)ent->floating.value);
    break;
  case BJVM_CP_KIND_CLASS:
    return class_info_entry_to_string(&ent->class_info);
  case BJVM_CP_KIND_STRING: {
    snprintf(result, sizeof(result), "String: '%.*s'", ent->string.chars.len, ent->string.chars.chars);
    break;
  }
  case BJVM_CP_KIND_FIELD_REF: {
    char *class_name =
        class_info_entry_to_string(ent->fieldref_info.class_info);
    char *field_name = name_and_type_entry_to_string(ent->fieldref_info.nat);

    snprintf(result, sizeof(result), "FieldRef: %s.%s", class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case BJVM_CP_KIND_METHOD_REF:
  case BJVM_CP_KIND_INTERFACE_METHOD_REF: {
    char *class_name =
        class_info_entry_to_string(ent->fieldref_info.class_info);
    char *field_name = name_and_type_entry_to_string(ent->fieldref_info.nat);
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

char *insn_to_string(const bjvm_bytecode_insn *insn, int insn_index) {
  char buf[4000];
  char *write = buf, *end = write + sizeof(buf);

  write += snprintf(write, sizeof(buf), "%04d = pc %04d: ", insn_index,
                    insn->program_counter);
  write += snprintf(write, end - write, "%s ", insn_code_name(insn->kind));

  if (insn->kind <= bjvm_insn_swap) {
    // no operands
  } else if (insn->kind <= bjvm_insn_ldc2_w) {
    // indexes into constant pool
    char *cp_str = constant_pool_entry_to_string(insn->cp);
    write += snprintf(write, end - write, "%s", cp_str);
    free(cp_str);
  } else if (insn->kind <= bjvm_insn_astore) {
    // indexes into local variables
    write += snprintf(write, end - write, "#%d", insn->index);
  } else if (insn->kind <= bjvm_insn_ifnull) {
    // indexes into the instruction array
    write += snprintf(write, end - write, "-> inst %d", insn->index);
  } else if (insn->kind == bjvm_insn_lconst || insn->kind == bjvm_insn_iconst) {
    write += snprintf(write, end - write, "%lld", insn->integer_imm);
  } else if (insn->kind == bjvm_insn_dconst || insn->kind == bjvm_insn_fconst) {
    write += snprintf(write, end - write, "%.15g", insn->f_imm);
  } else if (insn->kind == bjvm_insn_tableswitch) {
    write += snprintf(write, end - write, "[ default -> %d",
                      insn->tableswitch.default_target);
    printf("TARGET COUNT: %d\n",
           insn->tableswitch
               .targets_count); // TODO figure out why snprintf is dumb
    for (int i = 0, j = insn->tableswitch.low;
         i < insn->tableswitch.targets_count; ++i, ++j) {
      write += snprintf(write, end - write, ", %d -> %d", i,
                        insn->tableswitch.targets[i]);
    }
    write += snprintf(write, end - write, " ]");
  } else if (insn->kind == bjvm_insn_lookupswitch) {
    write += snprintf(write, end - write, "[ default -> %d",
                      insn->lookupswitch.default_target);
    for (int i = 0; i < insn->lookupswitch.targets_count; ++i) {
      write +=
          snprintf(write, end - write, ", %d -> %d", insn->lookupswitch.keys[i],
                   insn->lookupswitch.targets[i]);
    }
    write += snprintf(write, end - write, " ]");
  } else {
    // TODO
  }
  return strdup(buf);
}

char *code_attribute_to_string(const bjvm_attribute_code *attrib) {
  char **insns = malloc(attrib->insn_count * sizeof(char *));
  size_t total_length = 0;
  for (int i = 0; i < attrib->insn_count; ++i) {
    char *insn_str = insn_to_string(attrib->code + i, i);
    insns[i] = insn_str;
    total_length += strlen(insn_str) + 1;
  }
  char *result = calloc(total_length + 1, 1), *write = result;
  for (int i = 0; i < attrib->insn_count; ++i) {
    write = stpcpy(write, insns[i]);
    *write++ = '\n';
    free(insns[i]);
  }
  free(insns);
  *write = '\0';
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

bjvm_type_kind kind_to_representable_kind(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
    return BJVM_TYPE_KIND_INT;
  case BJVM_TYPE_KIND_FLOAT:
  case BJVM_TYPE_KIND_DOUBLE:
  case BJVM_TYPE_KIND_LONG:
  case BJVM_TYPE_KIND_REFERENCE:
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
    return kind;
  case BJVM_TYPE_KIND_VOID:
  default:
    UNREACHABLE();
    break;
  }
}

bjvm_type_kind field_to_representable_kind(const bjvm_field_descriptor *field) {
  if (field->dimensions)
    return BJVM_TYPE_KIND_REFERENCE;

  return kind_to_representable_kind(field->kind);
}

int bjvm_analyze_method_code_segment(bjvm_cp_method *method, heap_string *error) {
  bjvm_attribute_code *code = method->code;
  if (!code) {
    return 0;
  }

  int result = 0;

  // After jumps, we can infer the stack and locals at these points
  bjvm_analy_stack_state *inferred_stacks =
      calloc(code->insn_count, sizeof(bjvm_analy_stack_state));
  bjvm_compressed_bitset *insn_index_to_references =
      calloc(code->insn_count, sizeof(bjvm_compressed_bitset));

  for (int i = 0; i < code->insn_count; ++i)
    insn_index_to_references[i] = bjvm_empty_bitset();

  bjvm_analy_stack_state stack;
  stack.entries = calloc(code->max_stack + 1, sizeof(bjvm_analy_stack_entry));

  // Initialize stack to exception handler--looking stack
  stack.entries_cap = code->max_stack + 1;
  stack.entries_count = 1;
  stack.entries[0] = BJVM_TYPE_KIND_REFERENCE;

  // Mark all exception handlers as having a stack which is just a reference
  if (code->exception_table) {
    for (int i = 0; i < code->exception_table->entries_count; ++i) {
      bjvm_exception_table_entry *ent = code->exception_table->entries + i;
      if (!inferred_stacks[ent->handler_pc].entries)
        copy_analy_stack_state(stack, &inferred_stacks[ent->handler_pc]);
    }
  }

  stack.entries_count = 0;

  bjvm_code_analysis *analy = method->code_analysis =
      malloc(sizeof(bjvm_code_analysis));

  analy->insn_count = code->insn_count;
  analy->insn_index_to_references = insn_index_to_references;

  int *branch_targets_to_process = calloc(code->max_formal_pc, sizeof(int));
  int branch_targets_count = 0;

#define POP_VAL                                                                \
  ({                                                                           \
    if (stack.entries_count == 0)                                              \
      goto stack_underflow;                                                    \
    stack.entries[--stack.entries_count];                                      \
  })
#define POP_KIND(kind)                                                         \
  {                                                                            \
    bjvm_analy_stack_entry popped_kind = POP_VAL;                              \
    if (kind != popped_kind)                                                   \
      goto stack_type_mismatch;                                                \
  }
#define POP(kind) POP_KIND(BJVM_TYPE_KIND_##kind)
#define PUSH_KIND(kind)                                                        \
  {                                                                            \
    if (stack.entries_count == stack.entries_cap)                              \
      goto stack_overflow;                                                     \
    if (kind != BJVM_TYPE_KIND_VOID)                                           \
      stack.entries[stack.entries_count++] = kind_to_representable_kind(kind); \
  }
#define PUSH(kind) PUSH_KIND(BJVM_TYPE_KIND_##kind)

#define PUSH_BRANCH_TARGET(target)                                             \
  {                                                                            \
    assert((int)target < code->insn_count && target >= 0);                     \
    if (inferred_stacks[target].entries) {                                     \
      error_str =                                                              \
          expect_analy_stack_states_equal(inferred_stacks[target], stack);     \
      if (error_str) {                                                         \
        error_str_needs_free = true;                                           \
        goto error;                                                            \
      }                                                                        \
    } else {                                                                   \
      copy_analy_stack_state(stack, &inferred_stacks[target]);                 \
      inferred_stacks[target].from_jump_target = true;                         \
      branch_targets_to_process[branch_targets_count++] = target;              \
    }                                                                          \
  }

  bjvm_analy_stack_state stack_before = {0};

  char *error_str;
  bool error_str_needs_free = false;
  bool stack_terminated = false;
  for (int i = 0; i < code->insn_count; ++i) {
    if (inferred_stacks[i].entries) {
      copy_analy_stack_state(inferred_stacks[i], &stack);
      inferred_stacks[i].from_jump_target = false;
      stack_terminated = false;
    }

    if (stack_terminated) {
      // We expect to be able to recover the stack/locals from a previously
      // encountered jump, or an exception handler. If this isn't possible then
      // there will be weeping and wailing and gnashing of teeth, and we'll
      // choose a different branch to continue analyzing from.
      if (branch_targets_count == 0) {
        break; // gahhhhhh
      }

      i = branch_targets_to_process[--branch_targets_count];
      copy_analy_stack_state(inferred_stacks[i], &stack);
      inferred_stacks[i].from_jump_target = false;
      stack_terminated = false;
    }

    copy_analy_stack_state(stack, &stack_before);
    if (!inferred_stacks[i].entries)
      copy_analy_stack_state(stack, &inferred_stacks[i]);

    bjvm_bytecode_insn *insn = &code->code[i];
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
      stack_terminated = true;
      break;
    case bjvm_insn_arraylength:
      POP(REFERENCE) PUSH(INT) break;
    case bjvm_insn_athrow:
      POP(REFERENCE)
      stack_terminated = true;
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
      stack_terminated = true;
      break;
    case bjvm_insn_dup: {
      if (stack.entries_count == 0)
        goto stack_underflow;
      PUSH_KIND(stack.entries[stack.entries_count - 1])
      break;
    }
    case bjvm_insn_dup_x1: {
      if (stack.entries_count <= 1)
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
      stack_terminated = true;
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
      stack_terminated = true;
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
      stack_terminated = true;
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
              ->fieldref_info.parsed_descriptor;
      PUSH_KIND(field_to_representable_kind(field));
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
        POP_KIND(field_to_representable_kind(field));
      }
      if (descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
        PUSH_KIND(field_to_representable_kind(&descriptor->return_type))
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
              ->methodref.method_descriptor;
      for (int j = descriptor->args_count - 1; j >= 0; --j) {
        bjvm_field_descriptor *field = descriptor->args + j;
        POP_KIND(field_to_representable_kind(field))
      }
      if (insn->kind != bjvm_insn_invokestatic) {
        POP(REFERENCE)
      }
      if (descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
        PUSH_KIND(field_to_representable_kind(&descriptor->return_type));
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
      break;
    }
    case bjvm_insn_fload: {
      PUSH(FLOAT)
      break;
    }
    case bjvm_insn_iload: {
      PUSH(INT)
      break;
    }
    case bjvm_insn_lload: {
      PUSH(LONG)
      break;
    }
    case bjvm_insn_dstore: {
      POP(DOUBLE)
      break;
    }
    case bjvm_insn_fstore: {
      POP(FLOAT)
      break;
    }
    case bjvm_insn_istore: {
      POP(INT)
      break;
    }
    case bjvm_insn_lstore: {
      POP(LONG)
      break;
    }
    case bjvm_insn_aload: {
      PUSH(REFERENCE)
      break;
    }
    case bjvm_insn_astore: {
      POP(REFERENCE)
      break;
    }
    case bjvm_insn_goto: {
      PUSH_BRANCH_TARGET(insn->index)
      stack_terminated = true;
      break;
    }
    case bjvm_insn_jsr: {
      PUSH(RETURN_ADDRESS)
      break;
    }
    case bjvm_insn_if_acmpeq:
    case bjvm_insn_if_acmpne: {
      POP(REFERENCE) POP(REFERENCE) PUSH_BRANCH_TARGET(insn->index);
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
      PUSH_BRANCH_TARGET(insn->index);
      break;
    }
    case bjvm_insn_ifnonnull:
    case bjvm_insn_ifnull: {
      POP(REFERENCE)
      PUSH_BRANCH_TARGET(insn->index);
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
      PUSH_BRANCH_TARGET(insn->tableswitch.default_target);
      for (int i = 0; i < insn->tableswitch.targets_count; ++i)
        PUSH_BRANCH_TARGET(insn->tableswitch.targets[i]);
      stack_terminated = true;
      break;
    }
    case bjvm_insn_lookupswitch: {
      POP(INT)
      PUSH_BRANCH_TARGET(insn->lookupswitch.default_target);
      for (int i = 0; i < insn->lookupswitch.targets_count; ++i)
        PUSH_BRANCH_TARGET(insn->lookupswitch.targets[i]);
      stack_terminated = true;
      break;
    }
    }

    continue;

  stack_overflow:
    error_str = "Stack overflow:";
    goto error;
  stack_underflow:
    error_str = "Stack underflow:";
    goto error;
  stack_type_mismatch: {
    error_str = "Stack type mismatch:";
  error:;
    result = -1;
    *error = make_heap_str(50000);
    char *insn_str = insn_to_string(insn, i);
    char *stack_str = print_analy_stack_state(&stack_before);
    char *context = code_attribute_to_string(method->code);
    bprintf(hslc(*error),
             "%s\nInstruction: %s\nStack preceding insn: %s\nContext: %s\n",
             error_str, insn_str, stack_str, context);
    free(insn_str);
    free(stack_str);
    free(context);
    if (error_str_needs_free)
      free(error_str);
    break;
  }
  }

  // Check that all entries have been filled
  for (int i = 0; i < code->insn_count; ++i) {
    if (!inferred_stacks[i].entries && !error) {
      char buf[1000], *write = buf, *end = buf + sizeof(buf);
      write +=
          snprintf(buf, 1000, "Unreachable code detected at instruction %d", i);
      char *context = code_attribute_to_string(method->code);
      write += snprintf(write, end - write, "\nContext: %s\n", context);
      break;
    }
  }

  free(branch_targets_to_process);
  for (int i = 0; i < code->insn_count; ++i) {
    free(inferred_stacks[i].entries);
  }
  free(inferred_stacks);
  free(stack.entries);
  free(stack_before.entries);

  return result;
}

bjvm_analy_stack_state bjvm_init_analy_stack_state(int initial_size) {
  return (bjvm_analy_stack_state){
      .entries = calloc(initial_size, sizeof(bjvm_analy_stack_entry)),
      .entries_count = initial_size,
      .entries_cap = initial_size};
}

void bjvm_free_analy_stack_state(bjvm_analy_stack_state state) {
  free(state.entries);
}

typedef struct {
  bjvm_compressed_bitset
      stack; // whenever a bit in here is true, that stack entry is a reference
  bjvm_compressed_bitset locals; // whenever a bit in here is true, that local
                                 // variable entry is a reference
} bjvm_analy_reference_bitset_state;

void bjvm_free_analy_reference_bitset_state(
    bjvm_analy_reference_bitset_state state) {
  bjvm_free_compressed_bitset(state.stack);
  bjvm_free_compressed_bitset(state.locals);
}