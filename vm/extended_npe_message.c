#include "analysis.h"

static void replace_slashes(char *str, int len) {
  for (int i = 0; i < len; ++i) {
    if (str[i] == '/') {
      str[i] = '.';
    }
  }
}

static void npe_stringify_type(string_builder *B, const field_descriptor *F) {
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

static void npe_stringify_method(string_builder *B, const cp_method_info *M) {
  INIT_STACK_STRING(no_slashes, 1024);
  exchange_slashes_and_dots(&no_slashes, M->class_info->name);
  string_builder_append(B, "%.*s.%.*s(", fmt_slice(no_slashes), fmt_slice(M->nat->name));
  for (int i = 0; i < M->descriptor->args_count; ++i) {
    if (i > 0)
      string_builder_append(B, ", ");
    npe_stringify_type(B, M->descriptor->args + i);
  }
  string_builder_append(B, ")");
}

// NOLINTNEXTLINE(misc-no-recursion)
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
      if (lvt && ((ent = local_variable_table_lookup(source->index, original_pc, lvt)))) {
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

    if (lvt && ((ent = local_variable_table_lookup(insn->extra_data.index, original_pc, lvt)))) {
      string_builder_append(builder, "%.*s", fmt_slice(*ent));
    } else {
      string_builder_append(builder, "<local%d>", insn->extra_data.index);
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
    case insn_getfield_B ... insn_getfield_L: {
      // <a>.name or just "name" if a can't be resolved
      int err = extended_npe_phase2(method, &analy->sources[index].a, index, builder, false);
      if (!err) {
        string_builder_append(builder, ".");
      }
      string_builder_append(builder, "%.*s", fmt_slice(insn->extra_data.cp->field.nat->name));
      break;
    }
    case insn_getstatic:
    case insn_getstatic_B ... insn_getstatic_L: {
      // Class.name
      string_builder_append(builder, "%.*s.%.*s", fmt_slice(insn->extra_data.cp->field.class_info->name),
                            fmt_slice(insn->extra_data.cp->field.nat->name));
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
      npe_stringify_method(builder, &insn->extra_data.cp->methodref);
      break;
    }
    case insn_iconst: {
      string_builder_append(builder, "%d", (int)insn->extra_data.integer_imm);
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
  case insn_getfield_B ... insn_getfield_L:
    string_builder_append(&builder, "Cannot read field \"%.*s\"", fmt_slice(faulting_insn->extra_data.cp->field.nat->name));
    break;
  case insn_putfield:
  case insn_putfield_B ... insn_putfield_L:
    string_builder_append(&builder, "Cannot assign field \"%.*s\"", fmt_slice(faulting_insn->extra_data.cp->field.nat->name));
    break;
  case insn_invokevirtual:
  case insn_invokeinterface:
  case insn_invokespecial:
  case insn_invokespecial_resolved:
  case insn_invokeitable_monomorphic:
  case insn_invokeitable_polymorphic:
  case insn_invokevtable_monomorphic:
  case insn_invokevtable_polymorphic: {
    cp_method_info *invoked = &faulting_insn->extra_data.cp->methodref;
    string_builder_append(&builder, "Cannot invoke \"");
    npe_stringify_method(&builder, invoked);
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