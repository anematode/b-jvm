
#include "classfile.h"

#include <inttypes.h>
#include <stdint.h>

#define CASE(K)                                                                                                        \
  case insn_##K:                                                                                                       \
    return #K;

const char *insn_code_to_string(insn_code_kind code) {
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
    CASE(sqrt)
  }
  printf("Unknown code: %d\n", code);
  UNREACHABLE();
}

#undef CASE
#define CASE(tk, name)                                                                                                        \
  case TYPE_KIND_##tk:                                                                                                              \
    return #name;

const char *type_kind_to_string(type_kind kind) {
  switch (kind) {
    CASE(BOOLEAN, boolean)
    CASE(BYTE, byte)
    CASE(CHAR, char)
    CASE(SHORT, short)
    CASE(INT, int)
    CASE(LONG, long)
    CASE(FLOAT, float)
    CASE(DOUBLE, double)
    CASE(VOID, void)
    CASE(REFERENCE, reference)
  }
  UNREACHABLE();
}

#undef CASE

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

char *cp_name_and_type_to_string(const cp_name_and_type *name_and_type) {
  char result[1000];
  snprintf(result, sizeof(result), "NameAndType: %.*s:%.*s", name_and_type->name.len, name_and_type->name.chars,
           name_and_type->descriptor.len, name_and_type->descriptor.chars);
  return strdup(result);
}

char *cp_indy_info_to_string(const cp_indy_info *indy_info) {
  return cp_name_and_type_to_string(indy_info->name_and_type);
}

/**
 * Convert the constant pool entry to an owned string.
 */
char *cp_entry_to_string(const cp_entry *ent) {
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
    char *field_name = cp_name_and_type_to_string(ent->field.nat);

    snprintf(result, sizeof(result), "FieldRef: %s.%s", class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case CP_KIND_METHOD_REF:
  case CP_KIND_INTERFACE_METHOD_REF: {
    char *class_name = class_info_entry_to_string(CP_KIND_CLASS, ent->field.class_info);
    char *field_name = cp_name_and_type_to_string(ent->field.nat);
    snprintf(result, sizeof(result), "%s: %s; %s", ent->kind == CP_KIND_METHOD_REF ? "MethodRef" : "InterfaceMethodRef",
             class_name, field_name);
    free(class_name);
    free(field_name);
    break;
  }
  case CP_KIND_NAME_AND_TYPE: {
    return cp_name_and_type_to_string(&ent->name_and_type);
  }
  case CP_KIND_METHOD_HANDLE: {
    return strdup("<method handle>"); // TODO
  }
  case CP_KIND_METHOD_TYPE: {
    return strdup("<method type>"); // TODO
  }
  case CP_KIND_INVOKE_DYNAMIC:
    return cp_indy_info_to_string(&ent->indy_info);
  case CP_KIND_DYNAMIC_CONSTANT:
    return cp_indy_info_to_string(&ent->indy_info);
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
  write = build_str(&result, write, "%s ", insn_code_to_string(insn->kind));
  if (insn->kind <= insn_swap) {
    // no operands
  } else if (insn->kind <= insn_ldc2_w) {
    // indexes into constant pool
    char *cp_str = cp_entry_to_string(insn->cp);
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