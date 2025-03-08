//
// Created by alec on 12/18/24.
//

#include <assert.h>
#include <endian.h>
#include <setjmp.h>
#include <stdlib.h>

#include "analysis.h"
#include "classfile.h"
#include "util.h"
#include "bjvm.h"

type_kind kind_to_representable_kind(type_kind kind) {
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
  case TYPE_KIND_CHAR:
  case TYPE_KIND_BYTE:
  case TYPE_KIND_SHORT:
  case TYPE_KIND_INT:
    return TYPE_KIND_INT;
  default:
    return kind;
  }
}

type_kind read_type_kind_char(char c) {
  switch (c) {
  case 'Z':
    return TYPE_KIND_BOOLEAN;
  case 'C':
    return TYPE_KIND_CHAR;
  case 'F':
    return TYPE_KIND_FLOAT;
  case 'D':
    return TYPE_KIND_DOUBLE;
  case 'B':
    return TYPE_KIND_BYTE;
  case 'S':
    return TYPE_KIND_SHORT;
  case 'I':
    return TYPE_KIND_INT;
  case 'J':
    return TYPE_KIND_LONG;
  case 'V':
    return TYPE_KIND_VOID;
  case 'L':
    return TYPE_KIND_REFERENCE;
  default:
    CHECK(false);
  }
}

char type_kind_to_char(type_kind kind) { return "ZCFDBSIJVL"[kind]; }

void free_method(cp_method *method) { free_code_analysis(method->code_analysis); }

void free_classfile(classdesc cf) {
  for (int i = 0; i < cf.methods_count; ++i)
    free_method(&cf.methods[i]);
  arrfree(cf.indy_insns);
  arrfree(cf.sigpoly_insns);
  arena_uninit(&cf.arena);
}

cp_entry *get_constant_pool_entry(constant_pool *pool, int index) {
  DCHECK(index >= 0 && index < pool->entries_len);
  return &pool->entries[index];
}

typedef struct {
  const u8 *bytes;
  size_t len;
} cf_byteslice;

// jmp_buf for format error
_Thread_local jmp_buf format_error_jmp_buf;
_Thread_local char *format_error_msg = nullptr;
_Thread_local bool format_error_needs_free = false;

_Noreturn void format_error_static(const char *reason) {
  format_error_msg = (char *)reason;
  format_error_needs_free = false;
  longjmp(format_error_jmp_buf, 1);
}

_Noreturn void format_error_dynamic(char *reason) {
  format_error_msg = reason;
  format_error_needs_free = true;
  longjmp(format_error_jmp_buf, 1);
}

static u8 const *reader_advance(cf_byteslice *reader, const char *reason, size_t len) {
  if (unlikely(reader->len < len)) {
    format_error_static(reason);
  }

  u8 const *data = reader->bytes;
  reader->bytes += len;
  reader->len -= len;

  return data;
}

#define PUN_READER_NEXT_IMPL(width, type)                                                                              \
  _Static_assert(sizeof(type) * 8 == width);                                                                           \
  static type reader_next_##type(cf_byteslice *reader, const char *reason) {                                           \
    u##width u = reader_next_u##width(reader, reason);                                                                 \
    type s;                                                                                                            \
    memcpy(&s, &u, sizeof(u));                                                                                         \
    return s;                                                                                                          \
  }

#define READER_NEXT_IMPL(width)                                                                                        \
  static u##width reader_next_u##width(cf_byteslice *reader, const char *reason) {                                     \
    return read_u##width##_be(reader_advance(reader, reason, sizeof(u##width)));                                       \
  }                                                                                                                    \
  PUN_READER_NEXT_IMPL(width, s##width);

READER_NEXT_IMPL(8);
READER_NEXT_IMPL(16);
READER_NEXT_IMPL(32);
READER_NEXT_IMPL(64);

PUN_READER_NEXT_IMPL(32, f32);
PUN_READER_NEXT_IMPL(64, f64);

#undef READER_NEXT_IMPL
#undef PUN_READER_NEXT_IMPL

cf_byteslice reader_get_slice(cf_byteslice *reader, size_t len, const char *reason) {
  if (unlikely(reader->len < len)) {
    char *msg = malloc(strlen(reason) + 100);
    strcpy(stpcpy(msg, "End of slice while reading "), reason);
    format_error_dynamic(msg);
  }
  cf_byteslice result = {.bytes = reader->bytes, .len = len};
  reader->bytes += len;
  reader->len -= len;
  return result;
}

typedef struct {
  constant_pool *cp;
  arena *arena;
  int current_code_max_pc;
  void *temp_allocation;
} classfile_parse_ctx;

// See: 4.4.7. The CONSTANT_Utf8_info Structure
slice parse_modified_utf8(const u8 *bytes, int len, arena *arena) {
  char *result = arena_alloc(arena, len + 1, sizeof(char));
  memcpy(result, bytes, len);
  result[len] = '\0';
  return (slice){.chars = result, .len = len};
}

cp_entry *check_cp_entry(cp_entry *entry, cp_kind expected_kinds, const char *reason) {
  DCHECK(reason);
  if ((entry->kind & expected_kinds) || (!expected_kinds))
    return entry;
  char buf[1000] = {0}, *write = buf, *end = buf + sizeof(buf);
  write = write + snprintf(buf, end - write,
                           "Unexpected constant pool entry kind %d at index "
                           "%d (expected one of: [ ",
                           entry->kind, entry->my_index);
  for (size_t i = 0; (1 << i) <= (int)CP_KIND_LAST; ++i)
    if (expected_kinds & 1 << i)
      write += snprintf(write, end - write, "%s ", cp_kind_to_string(1 << i));
  write += snprintf(write, end - write, "]) while reading %s", reason);
  format_error_dynamic(strdup(buf));
}

const slice *lvt_lookup(int index, int original_pc, const attribute_local_variable_table *table) {
  // Linear scan throught the whole array
  for (int i = 0; i < table->entries_count; ++i) {
    attribute_lvt_entry *entry = table->entries + i;
    if (entry->index == index && entry->start_pc <= original_pc && entry->end_pc > original_pc) {
      return &entry->name;
    }
  }
  return nullptr;
}

cp_entry *checked_cp_entry(constant_pool *pool, int index, cp_kind expected_kinds, const char *reason) {
  DCHECK(reason);
  if (!(index >= 0 && index < pool->entries_len)) {
    char buf[256] = {0};
    snprintf(buf, sizeof(buf), "Invalid constant pool entry index %d (pool size %d) while reading %s", index,
             pool->entries_len, reason);
    format_error_dynamic(strdup(buf));
  }
  return check_cp_entry(&pool->entries[index], expected_kinds, reason);
}

slice checked_get_utf8(constant_pool *pool, int index, const char *reason) {
  return checked_cp_entry(pool, index, CP_KIND_UTF8, reason)->utf8;
}

char *parse_complete_field_descriptor(const slice entry, field_descriptor *result, classfile_parse_ctx *ctx) {
  const char *chars = entry.chars;
  char *error = parse_field_descriptor(&chars, entry.len, result, ctx->arena);
  if (error)
    return error;
  if (chars != entry.chars + entry.len) {
    char buf[64];
    snprintf(buf, sizeof(buf), "trailing character(s): '%c'", *chars);
    return strdup(buf);
  }
  return nullptr;
}

enum cp_resolution_pass {
  READ, // parse UTF-8 entries and list out all other entries, but don't link them
  LINK  // link entries using the partially filled-out constant pool
};

/**
 * Parse a single constant pool entry.
 * @param reader The reader to parse from.
 * @param ctx The parse context.
 * @param pass Whether we're reading or linking. UTF-8 entries are
 * @return The resolved entry.
 */
cp_entry parse_constant_pool_entry(cf_byteslice *reader, classfile_parse_ctx *ctx, enum cp_resolution_pass pass) {
  bool skip_linking = pass == READ;
  enum { // given by the spec
    CONSTANT_Utf8 = 1,
    CONSTANT_Integer = 3,
    CONSTANT_Float = 4,
    CONSTANT_Long = 5,
    CONSTANT_Double = 6,
    CONSTANT_Class = 7,
    CONSTANT_String = 8,
    CONSTANT_Fieldref = 9,
    CONSTANT_Methodref = 10,
    CONSTANT_InterfaceMethodref = 11,
    CONSTANT_NameAndType = 12,
    CONSTANT_MethodHandle = 15,
    CONSTANT_MethodType = 16,
    CONSTANT_Dynamic = 17,
    CONSTANT_InvokeDynamic = 18,
    CONSTANT_Module = 19,
    CONSTANT_Package = 20
  };

  u8 kind = reader_next_u8(reader, "cp kind");
  switch (kind) {
  case CONSTANT_Module:
  case CONSTANT_Package:
  case CONSTANT_Class: {
    cp_kind entry_kind = kind == CONSTANT_Class    ? CP_KIND_CLASS
                         : kind == CONSTANT_Module ? CP_KIND_MODULE
                                                   : CP_KIND_PACKAGE;

    u16 index = reader_next_u16(reader, "class index");
    return (cp_entry){
        .kind = entry_kind,
        .class_info = {.name = skip_linking ? null_str() : checked_get_utf8(ctx->cp, index, "class info name")}};
  }

  case CONSTANT_Fieldref:
  case CONSTANT_Methodref:
  case CONSTANT_InterfaceMethodref: {
    u16 class_index = reader_next_u16(reader, "class index");
    u16 name_and_type_index = reader_next_u16(reader, "name and type index");

    cp_kind entry_kind = kind == CONSTANT_Fieldref    ? CP_KIND_FIELD_REF
                         : kind == CONSTANT_Methodref ? CP_KIND_METHOD_REF
                                                      : CP_KIND_INTERFACE_METHOD_REF;
    cp_class_info *class_info = skip_linking ? nullptr
                                             : &checked_cp_entry(ctx->cp, class_index, CP_KIND_CLASS,
                                                                 "fieldref/methodref/interfacemethodref class info")
                                                    ->class_info;

    cp_name_and_type *name_and_type = skip_linking
                                          ? nullptr
                                          : &checked_cp_entry(ctx->cp, name_and_type_index, CP_KIND_NAME_AND_TYPE,
                                                              "fieldref/methodref/interfacemethodref name and type")
                                                 ->name_and_type;

    if (kind == CONSTANT_Fieldref) {
      return (cp_entry){.kind = entry_kind,
                        .field = {.class_info = class_info, .nat = name_and_type, .field = nullptr}};
    }
    return (cp_entry){.kind = entry_kind, .methodref = {.class_info = class_info, .nat = name_and_type}};
  }
  case CONSTANT_String: {
    u16 index = reader_next_u16(reader, "string index");
    return (cp_entry){.kind = CP_KIND_STRING,
                      .string = {.chars = skip_linking ? null_str() : checked_get_utf8(ctx->cp, index, "string value"),
                                 .interned = nullptr}};
  }
  case CONSTANT_Integer: {
    s32 value = reader_next_s32(reader, "integer value");
    return (cp_entry){.kind = CP_KIND_INTEGER, .integral = {.value = (s64)value}};
  }
  case CONSTANT_Float: {
    double value = reader_next_f32(reader, "double value");
    return (cp_entry){.kind = CP_KIND_FLOAT, .floating = {.value = (double)value}};
  }
  case CONSTANT_Long: {
    s64 value = reader_next_s64(reader, "long value");
    return (cp_entry){.kind = CP_KIND_LONG, .integral = {.value = value}};
  }
  case CONSTANT_Double: {
    double value = reader_next_f64(reader, "double value");
    return (cp_entry){.kind = CP_KIND_DOUBLE, .floating = {.value = value}};
  }
  case CONSTANT_NameAndType: {
    u16 name_index = reader_next_u16(reader, "name index");
    u16 descriptor_index = reader_next_u16(reader, "descriptor index");

    slice name = skip_linking ? null_str() : checked_get_utf8(ctx->cp, name_index, "name and type name");
    return (cp_entry){.kind = CP_KIND_NAME_AND_TYPE,
                      .name_and_type = {.name = name,
                                        .descriptor = skip_linking ? null_str()
                                                                   : checked_get_utf8(ctx->cp, descriptor_index,
                                                                                      "name and type descriptor")}};
  }
  case CONSTANT_Utf8: {
    u16 length = reader_next_u16(reader, "utf8 length");
    cf_byteslice bytes_reader = reader_get_slice(reader, length, "utf8 data");
    slice utf8 = {nullptr};
    if (skip_linking) {
      utf8 = parse_modified_utf8(bytes_reader.bytes, length, ctx->arena);
    }
    return (cp_entry){.kind = CP_KIND_UTF8, .utf8 = utf8};
  }
  case CONSTANT_MethodHandle: {
    u8 handle_kind = reader_next_u8(reader, "method handle kind");
    u16 reference_index = reader_next_u16(reader, "reference index");
    cp_entry *entry = skip_linking
                          ? nullptr
                          : checked_cp_entry(ctx->cp, reference_index,
                                             CP_KIND_FIELD_REF | CP_KIND_METHOD_REF | CP_KIND_INTERFACE_METHOD_REF,
                                             "method handle reference");
    // TODO validate both
    return (cp_entry){.kind = CP_KIND_METHOD_HANDLE, .method_handle = {.handle_kind = handle_kind, .reference = entry}};
  }
  case CONSTANT_MethodType: {
    u16 desc_index = reader_next_u16(reader, "descriptor index");
    return (cp_entry){
        .kind = CP_KIND_METHOD_TYPE,
        .method_type = {.descriptor = skip_linking ? null_str()
                                                   : checked_get_utf8(ctx->cp, desc_index, "method type descriptor")}};
  }
  case CONSTANT_Dynamic:
  case CONSTANT_InvokeDynamic: {
    u16 bootstrap_method_attr_index = reader_next_u16(reader, "bootstrap method attr index");
    u16 name_and_type_index = reader_next_u16(reader, "name and type index");
    cp_name_and_type *name_and_type =
        skip_linking ? nullptr
                     : &checked_cp_entry(ctx->cp, name_and_type_index, CP_KIND_NAME_AND_TYPE, "indy name and type")
                            ->name_and_type;

    // The "method" will be converted into an actual pointer to the method in link_bootstrap_methods
    return (cp_entry){.kind = (kind == CONSTANT_Dynamic) ? CP_KIND_DYNAMIC_CONSTANT : CP_KIND_INVOKE_DYNAMIC,
                      .indy_info = {.method = (void *)(uintptr_t)bootstrap_method_attr_index,
                                    .name_and_type = name_and_type,
                                    .method_descriptor = nullptr}};
  }
  default:
    format_error_static("Invalid constant pool entry kind");
  }
}

constant_pool *init_constant_pool(u16 count, arena *arena) {
  constant_pool *pool = arena_alloc(arena, 1, sizeof(constant_pool) + (count + 1) * sizeof(cp_entry));
  pool->entries_len = count + 1;
  return pool;
}

void finish_constant_pool_entry(cp_entry *entry, classfile_parse_ctx *ctx) {
  switch (entry->kind) {
  case CP_KIND_FIELD_REF: {
    field_descriptor *parsed_descriptor = nullptr;
    cp_name_and_type *name_and_type = entry->field.nat;

    entry->field.parsed_descriptor = parsed_descriptor = arena_alloc(ctx->arena, 1, sizeof(field_descriptor));
    char *error = parse_complete_field_descriptor(name_and_type->descriptor, parsed_descriptor, ctx);
    if (error)
      format_error_dynamic(error);
    break;
  }
  case CP_KIND_INVOKE_DYNAMIC: {
    method_descriptor *desc = arena_alloc(ctx->arena, 1, sizeof(method_descriptor));
    char *error = parse_method_descriptor(entry->indy_info.name_and_type->descriptor, desc, ctx->arena);
    if (error)
      format_error_dynamic(error);
    entry->indy_info.method_descriptor = desc;
    break;
  }
  case CP_KIND_METHOD_REF:
  case CP_KIND_INTERFACE_METHOD_REF: {
    method_descriptor *desc = arena_alloc(ctx->arena, 1, sizeof(method_descriptor));
    cp_name_and_type *nat = entry->methodref.nat;
    char *error = parse_method_descriptor(nat->descriptor, desc, ctx->arena);
    if (error) {
      char *buf = malloc(1000);
      snprintf(buf, 1000, "Method '%.*s' has invalid descriptor '%.*s': %s", fmt_slice(nat->name),
               fmt_slice(nat->descriptor), error);
      free(error);
      format_error_dynamic(buf);
    }
    entry->methodref.descriptor = desc;
    break;
  }
  case CP_KIND_METHOD_TYPE: {
    method_descriptor *desc = arena_alloc(ctx->arena, 1, sizeof(method_descriptor));
    char *error = parse_method_descriptor(entry->method_type.descriptor, desc, ctx->arena);
    if (error)
      format_error_dynamic(error);
    entry->method_type.parsed_descriptor = desc;
    break;
  }
  default:
    break;
  }
}

/**
 * Parse the constant pool from the given byteslice. Basic validation is
 * performed for format checking, i.e., all within-pool pointers are resolved.
 */
constant_pool *parse_constant_pool(cf_byteslice *reader, classfile_parse_ctx *ctx) {
  u16 cp_count = reader_next_u16(reader, "constant pool count");

  constant_pool *pool = init_constant_pool(cp_count, ctx->arena);
  ctx->cp = pool;

  get_constant_pool_entry(pool, 0)->kind = CP_KIND_INVALID; // entry at 0 is always invalid
  cf_byteslice initial_reader_state = *reader;

  enum cp_resolution_pass passes[2] = {READ, LINK};
  for (int pass_i = 0; pass_i < 2; ++pass_i) {
    enum cp_resolution_pass pass = passes[pass_i];

    for (int cp_i = 1; cp_i < cp_count; ++cp_i) {
      cp_entry *ent = get_constant_pool_entry(pool, cp_i);
      cp_entry new_entry = parse_constant_pool_entry(reader, ctx, pass);
      if (new_entry.kind != CP_KIND_UTF8 || pass == READ) {
        *ent = new_entry; // we skip UTF-8 entries on the second pass because they don't link to anything
      }
      ent->my_index = cp_i;

      if (ent->kind == CP_KIND_LONG || ent->kind == CP_KIND_DOUBLE) {
        get_constant_pool_entry(pool, cp_i + 1)->kind = CP_KIND_INVALID;
        cp_i++;
      }
    }
    if (pass_i == 0) // go back to the beginning to parse
      *reader = initial_reader_state;
  }

  for (int cp_i = 1; cp_i < cp_count; cp_i++) {
    finish_constant_pool_entry(get_constant_pool_entry(pool, cp_i), ctx);
  }

  return pool;
}

int checked_pc(u32 insn_pc, int offset, classfile_parse_ctx *ctx) {
  int target;
  int overflow = __builtin_add_overflow(insn_pc, offset, &target);
  if (overflow || target < 0 || target >= ctx->current_code_max_pc) {
    format_error_static("Branch target out of bounds");
  }
  return target;
}

bytecode_insn parse_tableswitch_insn(cf_byteslice *reader, int pc, classfile_parse_ctx *ctx) {
  int original_pc = pc++;

  // consume u8s until pc = 0 mod 4
  while (pc % 4 != 0) {
    reader_next_u8(reader, "tableswitch padding");
    pc++;
  }

  int default_target = checked_pc(original_pc, reader_next_s32(reader, "tableswitch default target"), ctx);
  int low = reader_next_s32(reader, "tableswitch low");
  int high = reader_next_s32(reader, "tableswitch high");
  s64 targets_count = (s64)high - low + 1;

  if (targets_count > 1 << 15) {
    // preposterous, won't fit in the code segment
    format_error_static("tableswitch instruction is too large");
  }
  if (targets_count <= 0) {
    format_error_static("tableswitch high < low");
  }

  struct tableswitch_data *data = arena_alloc(ctx->arena, 1, sizeof(*data) + targets_count * sizeof(int));
  *data = (struct tableswitch_data){
      .default_target = default_target, .low = low, .high = high, .targets_count = (int)targets_count};
  for (int i = 0; i < targets_count; ++i) {
    data->targets[i] = checked_pc(original_pc, reader_next_s32(reader, "tableswitch target"), ctx);
  }

  return (bytecode_insn){.kind = insn_tableswitch, .original_pc = original_pc, .tableswitch = data};
}

bytecode_insn parse_lookupswitch_insn(cf_byteslice *reader, int pc, classfile_parse_ctx *ctx) {
  int original_pc = pc++;
  while (pc % 4 != 0) {
    reader_next_u8(reader, "tableswitch padding");
    pc++;
  }

  int default_target = checked_pc(original_pc, reader_next_s32(reader, "lookupswitch default target"), ctx);
  int pairs_count = reader_next_s32(reader, "lookupswitch pairs count");

  if (pairs_count > 1 << 15 || pairs_count < 0) {
    format_error_static("lookupswitch instruction is too large");
  }

  int *keys = arena_alloc(ctx->arena, pairs_count, sizeof(int));
  int *targets = arena_alloc(ctx->arena, pairs_count, sizeof(int));

  for (int i = 0; i < pairs_count; ++i) {
    keys[i] = reader_next_s32(reader, "lookupswitch key");
    targets[i] = checked_pc(original_pc, reader_next_s32(reader, "lookupswitch target"), ctx);
  }

  struct lookupswitch_data *data = arena_alloc(ctx->arena, 1, sizeof(*data));
  *data = (struct lookupswitch_data){.default_target = default_target,
                                     .keys = keys,
                                     .keys_count = pairs_count,
                                     .targets = targets,
                                     .targets_count = pairs_count};
  return (bytecode_insn){.kind = insn_lookupswitch, .original_pc = original_pc, .lookupswitch = data};
}

type_kind parse_atype(u8 atype) {
  switch (atype) {
  case 4:
    return TYPE_KIND_BOOLEAN;
  case 5:
    return TYPE_KIND_CHAR;
  case 6:
    return TYPE_KIND_FLOAT;
  case 7:
    return TYPE_KIND_DOUBLE;
  case 8:
    return TYPE_KIND_BYTE;
  case 9:
    return TYPE_KIND_SHORT;
  case 10:
    return TYPE_KIND_INT;
  case 11:
    return TYPE_KIND_LONG;
  default: {
    char buf[64];
    snprintf(buf, sizeof(buf), "invalid newarray type %d", atype);
    format_error_dynamic(strdup(buf));
  }
  }
}

bytecode_insn parse_insn_impl(cf_byteslice *reader, u32 pc, classfile_parse_ctx *ctx) {
  /** Raw instruction codes (to be canonicalized). */
  enum {
    nop = 0x00,
    aconst_null = 0x01,
    iconst_m1 = 0x02,
    iconst_0 = 0x03,
    iconst_1 = 0x04,
    iconst_2 = 0x05,
    iconst_3 = 0x06,
    iconst_4 = 0x07,
    iconst_5 = 0x08,
    lconst_0 = 0x09,
    lconst_1 = 0x0a,
    fconst_0 = 0x0b,
    fconst_1 = 0x0c,
    fconst_2 = 0x0d,
    dconst_0 = 0x0e,
    dconst_1 = 0x0f,
    bipush = 0x10,
    sipush = 0x11,
    ldc = 0x12,
    ldc_w = 0x13,
    ldc2_w = 0x14,
    iload = 0x15,
    lload = 0x16,
    fload = 0x17,
    dload = 0x18,
    aload = 0x19,
    iload_0 = 0x1a,
    iload_1 = 0x1b,
    iload_2 = 0x1c,
    iload_3 = 0x1d,
    lload_0 = 0x1e,
    lload_1 = 0x1f,
    lload_2 = 0x20,
    lload_3 = 0x21,
    fload_0 = 0x22,
    fload_1 = 0x23,
    fload_2 = 0x24,
    fload_3 = 0x25,
    dload_0 = 0x26,
    dload_1 = 0x27,
    dload_2 = 0x28,
    dload_3 = 0x29,
    aload_0 = 0x2a,
    aload_1 = 0x2b,
    aload_2 = 0x2c,
    aload_3 = 0x2d,
    iaload = 0x2e,
    laload = 0x2f,
    faload = 0x30,
    daload = 0x31,
    aaload = 0x32,
    baload = 0x33,
    caload = 0x34,
    saload = 0x35,
    istore = 0x36,
    lstore = 0x37,
    fstore = 0x38,
    dstore = 0x39,
    astore = 0x3a,
    istore_0 = 0x3b,
    istore_1 = 0x3c,
    istore_2 = 0x3d,
    istore_3 = 0x3e,
    lstore_0 = 0x3f,
    lstore_1 = 0x40,
    lstore_2 = 0x41,
    lstore_3 = 0x42,
    fstore_0 = 0x43,
    fstore_1 = 0x44,
    fstore_2 = 0x45,
    fstore_3 = 0x46,
    dstore_0 = 0x47,
    dstore_1 = 0x48,
    dstore_2 = 0x49,
    dstore_3 = 0x4a,
    astore_0 = 0x4b,
    astore_1 = 0x4c,
    astore_2 = 0x4d,
    astore_3 = 0x4e,
    iastore = 0x4f,
    lastore = 0x50,
    fastore = 0x51,
    dastore = 0x52,
    aastore = 0x53,
    bastore = 0x54,
    castore = 0x55,
    sastore = 0x56,
    pop = 0x57,
    pop2 = 0x58,
    dup = 0x59,
    dup_x1 = 0x5a,
    dup_x2 = 0x5b,
    dup2 = 0x5c,
    dup2_x1 = 0x5d,
    dup2_x2 = 0x5e,
    swap = 0x5f,
    iadd = 0x60,
    ladd = 0x61,
    fadd = 0x62,
    dadd = 0x63,
    isub = 0x64,
    lsub = 0x65,
    fsub = 0x66,
    dsub = 0x67,
    imul = 0x68,
    lmul = 0x69,
    fmul = 0x6a,
    dmul = 0x6b,
    idiv = 0x6c,
    ldiv = 0x6d,
    fdiv = 0x6e,
    ddiv = 0x6f,
    irem = 0x70,
    lrem = 0x71,
    frem = 0x72,
    drem = 0x73,
    ineg = 0x74,
    lneg = 0x75,
    fneg = 0x76,
    dneg = 0x77,
    ishl = 0x78,
    lshl = 0x79,
    ishr = 0x7a,
    lshr = 0x7b,
    iushr = 0x7c,
    lushr = 0x7d,
    iand = 0x7e,
    land = 0x7f,
    ior = 0x80,
    lor = 0x81,
    ixor = 0x82,
    lxor = 0x83,
    iinc = 0x84,
    i2l = 0x85,
    i2f = 0x86,
    i2d = 0x87,
    l2i = 0x88,
    l2f = 0x89,
    l2d = 0x8a,
    f2i = 0x8b,
    f2l = 0x8c,
    f2d = 0x8d,
    d2i = 0x8e,
    d2l = 0x8f,
    d2f = 0x90,
    i2b = 0x91,
    i2c = 0x92,
    i2s = 0x93,
    lcmp = 0x94,
    fcmpl = 0x95,
    fcmpg = 0x96,
    dcmpl = 0x97,
    dcmpg = 0x98,
    ifeq = 0x99,
    ifne = 0x9a,
    iflt = 0x9b,
    ifge = 0x9c,
    ifgt = 0x9d,
    ifle = 0x9e,
    if_icmpeq = 0x9f,
    if_icmpne = 0xa0,
    if_icmplt = 0xa1,
    if_icmpge = 0xa2,
    if_icmpgt = 0xa3,
    if_icmple = 0xa4,
    if_acmpeq = 0xa5,
    if_acmpne = 0xa6,
    goto_ = 0xa7,
    jsr = 0xa8,
    ret = 0xa9,
    tableswitch = 0xaa,
    lookupswitch = 0xab,
    ireturn = 0xac,
    lreturn = 0xad,
    freturn = 0xae,
    dreturn = 0xaf,
    areturn = 0xb0,
    return_ = 0xb1,
    getstatic = 0xb2,
    putstatic = 0xb3,
    getfield = 0xb4,
    putfield = 0xb5,
    invokevirtual = 0xb6,
    invokespecial = 0xb7,
    invokestatic = 0xb8,
    invokeinterface = 0xb9,
    invokedynamic = 0xba,
    new_ = 0xbb,
    newarray = 0xbc,
    anewarray = 0xbd,
    arraylength = 0xbe,
    athrow = 0xbf,
    checkcast = 0xc0,
    instanceof = 0xc1,
    monitorenter = 0xc2,
    monitorexit = 0xc3,
    wide = 0xc4,
    multianewarray = 0xc5,
    ifnull = 0xc6,
    ifnonnull = 0xc7,
    goto_w = 0xc8,
    jsr_w = 0xc9
  };

  u8 opcode = reader_next_u8(reader, "instruction opcode");

  switch (opcode) {
  case nop:
    return (bytecode_insn){.kind = insn_nop};
  case aconst_null:
    return (bytecode_insn){.kind = insn_aconst_null};
  case iconst_m1:
  case iconst_0:
  case iconst_1:
  case iconst_2:
  case iconst_3:
  case iconst_4:
  case iconst_5:
    return (bytecode_insn){.kind = insn_iconst, .integer_imm = opcode - iconst_0};
  case lconst_0:
  case lconst_1:
    return (bytecode_insn){.kind = insn_lconst, .integer_imm = opcode - lconst_0};
  case fconst_0:
  case fconst_1:
  case fconst_2:
    return (bytecode_insn){.kind = insn_fconst, .f_imm = (float)(opcode - fconst_0)};
  case dconst_0:
  case dconst_1:
    return (bytecode_insn){.kind = insn_dconst, .d_imm = (double)(opcode - dconst_0)};
  case bipush:
    return (bytecode_insn){.kind = insn_iconst, .integer_imm = reader_next_s8(reader, "bipush immediate")};
  case sipush:
    return (bytecode_insn){.kind = insn_iconst, .integer_imm = reader_next_s16(reader, "sipush immediate")};
  case ldc:
    return (bytecode_insn){.kind = insn_ldc,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u8(reader, "ldc index"),
                                                  CP_KIND_INTEGER | CP_KIND_FLOAT | CP_KIND_STRING | CP_KIND_CLASS,
                                                  "ldc index")};
  case ldc_w:
    return (bytecode_insn){.kind = insn_ldc,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "ldc_w index"),
                                                  CP_KIND_INTEGER | CP_KIND_FLOAT | CP_KIND_STRING | CP_KIND_CLASS,
                                                  "ldc_w index")};
  case ldc2_w:
    return (bytecode_insn){.kind = insn_ldc2_w,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "ldc2_w index"),
                                                  CP_KIND_DOUBLE | CP_KIND_LONG, "ldc2_w index")};
  case iload:
    return (bytecode_insn){.kind = insn_iload, .index = reader_next_u8(reader, "iload index")};
  case lload:
    return (bytecode_insn){.kind = insn_lload, .index = reader_next_u8(reader, "lload index")};
  case fload:
    return (bytecode_insn){.kind = insn_fload, .index = reader_next_u8(reader, "fload index")};
  case dload:
    return (bytecode_insn){.kind = insn_dload, .index = reader_next_u8(reader, "dload index")};
  case aload:
    return (bytecode_insn){.kind = insn_aload, .index = reader_next_u8(reader, "aload index")};
  case iload_0:
  case iload_1:
  case iload_2:
  case iload_3:
    return (bytecode_insn){.kind = insn_iload, .index = opcode - iload_0};
  case lload_0:
  case lload_1:
  case lload_2:
  case lload_3:
    return (bytecode_insn){.kind = insn_lload, .index = opcode - lload_0};
  case fload_0:
  case fload_1:
  case fload_2:
  case fload_3:
    return (bytecode_insn){.kind = insn_fload, .index = opcode - fload_0};
  case dload_0:
  case dload_1:
  case dload_2:
  case dload_3:
    return (bytecode_insn){.kind = insn_dload, .index = opcode - dload_0};
  case aload_0:
  case aload_1:
  case aload_2:
  case aload_3:
    return (bytecode_insn){.kind = insn_aload, .index = opcode - aload_0};
  case iaload:
    return (bytecode_insn){.kind = insn_iaload};
  case laload:
    return (bytecode_insn){.kind = insn_laload};
  case faload:
    return (bytecode_insn){.kind = insn_faload};
  case daload:
    return (bytecode_insn){.kind = insn_daload};
  case aaload:
    return (bytecode_insn){.kind = insn_aaload};
  case baload:
    return (bytecode_insn){.kind = insn_baload};
  case caload:
    return (bytecode_insn){.kind = insn_caload};
  case saload:
    return (bytecode_insn){.kind = insn_saload};
  case istore:
    return (bytecode_insn){.kind = insn_istore, .index = reader_next_u8(reader, "istore index")};
  case lstore:
    return (bytecode_insn){.kind = insn_lstore, .index = reader_next_u8(reader, "lstore index")};
  case fstore:
    return (bytecode_insn){.kind = insn_fstore, .index = reader_next_u8(reader, "fstore index")};
  case dstore:
    return (bytecode_insn){.kind = insn_dstore, .index = reader_next_u8(reader, "dstore index")};
  case astore:
    return (bytecode_insn){.kind = insn_astore, .index = reader_next_u8(reader, "astore index")};
  case istore_0:
  case istore_1:
  case istore_2:
  case istore_3:
    return (bytecode_insn){.kind = insn_istore, .index = opcode - istore_0};
  case lstore_0:
  case lstore_1:
  case lstore_2:
  case lstore_3:
    return (bytecode_insn){.kind = insn_lstore, .index = opcode - lstore_0};
  case fstore_0:
  case fstore_1:
  case fstore_2:
  case fstore_3:
    return (bytecode_insn){.kind = insn_fstore, .index = opcode - fstore_0};
  case dstore_0:
  case dstore_1:
  case dstore_2:
  case dstore_3:
    return (bytecode_insn){.kind = insn_dstore, .index = opcode - dstore_0};
  case astore_0:
  case astore_1:
  case astore_2:
  case astore_3:
    return (bytecode_insn){.kind = insn_astore, .index = opcode - astore_0};
  case iastore:
    return (bytecode_insn){.kind = insn_iastore};
  case lastore:
    return (bytecode_insn){.kind = insn_lastore};
  case fastore:
    return (bytecode_insn){.kind = insn_fastore};
  case dastore:
    return (bytecode_insn){.kind = insn_dastore};
  case aastore:
    return (bytecode_insn){.kind = insn_aastore};
  case bastore:
    return (bytecode_insn){.kind = insn_bastore};
  case castore:
    return (bytecode_insn){.kind = insn_castore};
  case sastore:
    return (bytecode_insn){.kind = insn_sastore};
  case pop:
    return (bytecode_insn){.kind = insn_pop};
  case pop2:
    return (bytecode_insn){.kind = insn_pop2};
  case dup:
    return (bytecode_insn){.kind = insn_dup};
  case dup_x1:
    return (bytecode_insn){.kind = insn_dup_x1};
  case dup_x2:
    return (bytecode_insn){.kind = insn_dup_x2};
  case dup2:
    return (bytecode_insn){.kind = insn_dup2};
  case dup2_x1:
    return (bytecode_insn){.kind = insn_dup2_x1};
  case dup2_x2:
    return (bytecode_insn){.kind = insn_dup2_x2};
  case swap:
    return (bytecode_insn){.kind = insn_swap};
  case iadd:
    return (bytecode_insn){.kind = insn_iadd};
  case ladd:
    return (bytecode_insn){.kind = insn_ladd};
  case fadd:
    return (bytecode_insn){.kind = insn_fadd};
  case dadd:
    return (bytecode_insn){.kind = insn_dadd};
  case isub:
    return (bytecode_insn){.kind = insn_isub};
  case lsub:
    return (bytecode_insn){.kind = insn_lsub};
  case fsub:
    return (bytecode_insn){.kind = insn_fsub};
  case dsub:
    return (bytecode_insn){.kind = insn_dsub};
  case imul:
    return (bytecode_insn){.kind = insn_imul};
  case lmul:
    return (bytecode_insn){.kind = insn_lmul};
  case fmul:
    return (bytecode_insn){.kind = insn_fmul};
  case dmul:
    return (bytecode_insn){.kind = insn_dmul};
  case idiv:
    return (bytecode_insn){.kind = insn_idiv};
  case ldiv:
    return (bytecode_insn){.kind = insn_ldiv};
  case fdiv:
    return (bytecode_insn){.kind = insn_fdiv};
  case ddiv:
    return (bytecode_insn){.kind = insn_ddiv};
  case irem:
    return (bytecode_insn){.kind = insn_irem};
  case lrem:
    return (bytecode_insn){.kind = insn_lrem};
  case frem:
    return (bytecode_insn){.kind = insn_frem};
  case drem:
    return (bytecode_insn){.kind = insn_drem};
  case ineg:
    return (bytecode_insn){.kind = insn_ineg};
  case lneg:
    return (bytecode_insn){.kind = insn_lneg};
  case fneg:
    return (bytecode_insn){.kind = insn_fneg};
  case dneg:
    return (bytecode_insn){.kind = insn_dneg};
  case ishl:
    return (bytecode_insn){.kind = insn_ishl};
  case lshl:
    return (bytecode_insn){.kind = insn_lshl};
  case ishr:
    return (bytecode_insn){.kind = insn_ishr};
  case lshr:
    return (bytecode_insn){.kind = insn_lshr};
  case iushr:
    return (bytecode_insn){.kind = insn_iushr};
  case lushr:
    return (bytecode_insn){.kind = insn_lushr};
  case iand:
    return (bytecode_insn){.kind = insn_iand};
  case land:
    return (bytecode_insn){.kind = insn_land};
  case ior:
    return (bytecode_insn){.kind = insn_ior};
  case lor:
    return (bytecode_insn){.kind = insn_lor};
  case ixor:
    return (bytecode_insn){.kind = insn_ixor};
  case lxor:
    return (bytecode_insn){.kind = insn_lxor};
  case iinc: {
    u16 index = reader_next_u8(reader, "iinc index");
    s16 const_ = (s16)reader_next_s8(reader, "iinc const");
    return (bytecode_insn){.kind = insn_iinc, .iinc = {index, const_}};
  }
  case i2l:
    return (bytecode_insn){.kind = insn_i2l};
  case i2f:
    return (bytecode_insn){.kind = insn_i2f};
  case i2d:
    return (bytecode_insn){.kind = insn_i2d};
  case l2i:
    return (bytecode_insn){.kind = insn_l2i};
  case l2f:
    return (bytecode_insn){.kind = insn_l2f};
  case l2d:
    return (bytecode_insn){.kind = insn_l2d};
  case f2i:
    return (bytecode_insn){.kind = insn_f2i};
  case f2l:
    return (bytecode_insn){.kind = insn_f2l};
  case f2d:
    return (bytecode_insn){.kind = insn_f2d};
  case d2i:
    return (bytecode_insn){.kind = insn_d2i};
  case d2l:
    return (bytecode_insn){.kind = insn_d2l};
  case d2f:
    return (bytecode_insn){.kind = insn_d2f};
  case i2b:
    return (bytecode_insn){.kind = insn_i2b};
  case i2c:
    return (bytecode_insn){.kind = insn_i2c};
  case i2s:
    return (bytecode_insn){.kind = insn_i2s};
  case lcmp:
    return (bytecode_insn){.kind = insn_lcmp};
  case fcmpl:
    return (bytecode_insn){.kind = insn_fcmpl};
  case fcmpg:
    return (bytecode_insn){.kind = insn_fcmpg};
  case dcmpl:
    return (bytecode_insn){.kind = insn_dcmpl};
  case dcmpg:
    return (bytecode_insn){.kind = insn_dcmpg};

  case ifeq:
    return (bytecode_insn){.kind = insn_ifeq, .index = checked_pc(pc, reader_next_s16(reader, "if_eq offset"), ctx)};
  case ifne:
    return (bytecode_insn){.kind = insn_ifne, .index = checked_pc(pc, reader_next_s16(reader, "if_ne offset"), ctx)};
  case iflt:
    return (bytecode_insn){.kind = insn_iflt, .index = checked_pc(pc, reader_next_s16(reader, "if_lt offset"), ctx)};
  case ifge:
    return (bytecode_insn){.kind = insn_ifge, .index = checked_pc(pc, reader_next_s16(reader, "if_ge offset"), ctx)};
  case ifgt:
    return (bytecode_insn){.kind = insn_ifgt, .index = checked_pc(pc, reader_next_s16(reader, "if_gt offset"), ctx)};
  case ifle:
    return (bytecode_insn){.kind = insn_ifle, .index = checked_pc(pc, reader_next_s16(reader, "if_le offset"), ctx)};

  case if_icmpeq:
    return (bytecode_insn){.kind = insn_if_icmpeq,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmpeq offset"), ctx)};
  case if_icmpne:
    return (bytecode_insn){.kind = insn_if_icmpne,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmpne offset"), ctx)};
  case if_icmplt:
    return (bytecode_insn){.kind = insn_if_icmplt,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmplt offset"), ctx)};
  case if_icmpge:
    return (bytecode_insn){.kind = insn_if_icmpge,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmpge offset"), ctx)};
  case if_icmpgt:
    return (bytecode_insn){.kind = insn_if_icmpgt,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmpgt offset"), ctx)};
  case if_icmple:
    return (bytecode_insn){.kind = insn_if_icmple,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_icmple offset"), ctx)};
  case if_acmpeq:
    return (bytecode_insn){.kind = insn_if_acmpeq,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_acmpeq offset"), ctx)};
  case if_acmpne:
    return (bytecode_insn){.kind = insn_if_acmpne,
                           .index = checked_pc(pc, reader_next_s16(reader, "if_acmpne offset"), ctx)};

  case goto_:
    return (bytecode_insn){.kind = insn_goto, .index = checked_pc(pc, reader_next_s16(reader, "goto offset"), ctx)};
  case jsr:
    return (bytecode_insn){.kind = insn_jsr, .index = checked_pc(pc, reader_next_s16(reader, "jsr offset"), ctx)};
  case ret:
    return (bytecode_insn){.kind = insn_ret, .index = reader_next_u8(reader, "ret index")};
  case tableswitch: {
    return parse_tableswitch_insn(reader, pc, ctx);
  }
  case lookupswitch: {
    return parse_lookupswitch_insn(reader, pc, ctx);
  }
  case ireturn:
    return (bytecode_insn){.kind = insn_ireturn};
  case lreturn:
    return (bytecode_insn){.kind = insn_lreturn};
  case freturn:
    return (bytecode_insn){.kind = insn_freturn};
  case dreturn:
    return (bytecode_insn){.kind = insn_dreturn};
  case areturn:
    return (bytecode_insn){.kind = insn_areturn};
  case return_:
    return (bytecode_insn){.kind = insn_return};

  case getstatic:
    return (bytecode_insn){.kind = insn_getstatic,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "getstatic index"),
                                                  CP_KIND_FIELD_REF, "getstatic field ref")};
  case putstatic:
    return (bytecode_insn){.kind = insn_putstatic,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "putstatic index"),
                                                  CP_KIND_FIELD_REF, "putstatic field ref")};

  case getfield:
    return (bytecode_insn){.kind = insn_getfield,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "getfield index"), CP_KIND_FIELD_REF,
                                                  "getfield field ref")};
  case putfield:
    return (bytecode_insn){.kind = insn_putfield,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "putfield index"), CP_KIND_FIELD_REF,
                                                  "putfield field ref")};

  case invokevirtual:
    return (bytecode_insn){.kind = insn_invokevirtual,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "invokevirtual index"),
                                                  CP_KIND_METHOD_REF | CP_KIND_INTERFACE_METHOD_REF,
                                                  "invokevirtual method ref")};
  case invokespecial:
    return (bytecode_insn){.kind = insn_invokespecial,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "invokespecial index"),
                                                  CP_KIND_METHOD_REF | CP_KIND_INTERFACE_METHOD_REF,
                                                  "invokespecial method ref")};
  case invokestatic:
    return (bytecode_insn){.kind = insn_invokestatic,
                           .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "invokestatic index"),
                                                  CP_KIND_METHOD_REF | CP_KIND_INTERFACE_METHOD_REF,
                                                  "invokestatic method ref")};

  case invokeinterface: {
    u16 index = reader_next_u16(reader, "invokeinterface index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_INTERFACE_METHOD_REF, "invokeinterface method ref");
    reader_next_u8(reader, "invokeinterface count");
    reader_next_u8(reader, "invokeinterface zero");
    return (bytecode_insn){.kind = insn_invokeinterface, .cp = entry};
  }

  case invokedynamic: {
    u16 index = reader_next_u16(reader, "invokedynamic index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_INVOKE_DYNAMIC, "indy method ref");
    reader_next_u16(reader, "invokedynamic zero");
    return (bytecode_insn){.kind = insn_invokedynamic, .cp = entry};
  }

  case new_: {
    u16 index = reader_next_u16(reader, "new index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_CLASS, "new class");
    return (bytecode_insn){.kind = insn_new, .cp = entry};
  }

  case newarray: {
    u8 atype = reader_next_u8(reader, "newarray type");
    type_kind parsed = parse_atype(atype);
    return (bytecode_insn){.kind = insn_newarray, .array_type = parsed};
  }

  case anewarray: {
    u16 index = reader_next_u16(reader, "anewarray index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_CLASS, "anewarray class");
    return (bytecode_insn){.kind = insn_anewarray, .cp = entry};
  }

  case arraylength:
    return (bytecode_insn){.kind = insn_arraylength};
  case athrow:
    return (bytecode_insn){.kind = insn_athrow};
  case checkcast: {
    u16 index = reader_next_u16(reader, "checkcast index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_CLASS, "checkcast class");
    return (bytecode_insn){.kind = insn_checkcast, .cp = entry};
  }

  case instanceof: {
    u16 index = reader_next_u16(reader, "instanceof index");
    cp_entry *entry = checked_cp_entry(ctx->cp, index, CP_KIND_CLASS, "instanceof class");
    return (bytecode_insn){.kind = insn_instanceof, .cp = entry};
  }

  case monitorenter:
    return (bytecode_insn){.kind = insn_monitorenter};
  case monitorexit:
    return (bytecode_insn){.kind = insn_monitorexit};

  case wide: {
    switch (reader_next_u8(reader, "widened opcode")) {
    case iload: {
      return (bytecode_insn){.kind = insn_iload, .index = reader_next_u16(reader, "wide iload index")};
    }
    case lload: {
      return (bytecode_insn){.kind = insn_lload, .index = reader_next_u16(reader, "wide lload index")};
    }
    case fload: {
      return (bytecode_insn){.kind = insn_fload, .index = reader_next_u16(reader, "wide fload index")};
    }
    case dload: {
      return (bytecode_insn){.kind = insn_dload, .index = reader_next_u16(reader, "wide dload index")};
    }
    case aload: {
      return (bytecode_insn){.kind = insn_aload, .index = reader_next_u16(reader, "wide aload index")};
    }
    case istore: {
      return (bytecode_insn){.kind = insn_istore, .index = reader_next_u16(reader, "wide istore index")};
    }
    case lstore: {
      return (bytecode_insn){.kind = insn_lstore, .index = reader_next_u16(reader, "wide lstore index")};
    }
    case fstore: {
      return (bytecode_insn){.kind = insn_fstore, .index = reader_next_u16(reader, "wide fstore index")};
    }
    case dstore: {
      return (bytecode_insn){.kind = insn_dstore, .index = reader_next_u16(reader, "wide dstore index")};
    }
    case astore: {
      return (bytecode_insn){.kind = insn_astore, .index = reader_next_u16(reader, "wide astore index")};
    }
    case iinc: {
      u16 index = reader_next_u16(reader, "wide iinc index");
      s16 const_ = reader_next_s16(reader, "wide iinc const");
      return (bytecode_insn){.kind = insn_iinc, .iinc = {index, const_}};
    }
    case ret: {
      return (bytecode_insn){.kind = insn_ret, .index = reader_next_u16(reader, "wide ret index")};
    }

    default: {
      char buf[64];
      snprintf(buf, sizeof(buf), "invalid wide opcode %d", opcode);
      format_error_dynamic(strdup(buf));
    }
    }
  }

  case multianewarray: {
    u16 index = reader_next_u16(reader, "multianewarray index");
    u8 dimensions = reader_next_u8(reader, "multianewarray dimensions");
    cp_class_info *entry = &checked_cp_entry(ctx->cp, index, CP_KIND_CLASS, "multianewarray class")->class_info;
    struct multianewarray_data *data = arena_alloc(ctx->arena, 1, sizeof(*data));
    data->entry = entry;
    data->dimensions = dimensions;
    return (bytecode_insn){.kind = insn_multianewarray, .multianewarray = data};
  }

  case ifnull:
    return (bytecode_insn){.kind = insn_ifnull, .index = checked_pc(pc, reader_next_s16(reader, "ifnull offset"), ctx)};
  case ifnonnull:
    return (bytecode_insn){.kind = insn_ifnonnull,
                           .index = checked_pc(pc, reader_next_s16(reader, "ifnonnull offset"), ctx)};
  case goto_w:
    return (bytecode_insn){.kind = insn_goto, .index = checked_pc(pc, reader_next_s32(reader, "goto_w offset"), ctx)};
  case jsr_w:
    return (bytecode_insn){.kind = insn_jsr, .index = checked_pc(pc, reader_next_s32(reader, "jsr_w offset"), ctx)};

  default: {
    char buf[64];
    snprintf(buf, sizeof(buf), "invalid opcode %d", opcode);
    format_error_dynamic(strdup(buf));
  }
  }
}

/**
 * Parse an instruction at the given program counter and advance the reader.
 * @return The parsed instruction.
 */
bytecode_insn parse_insn(cf_byteslice *reader, u32 pc, classfile_parse_ctx *ctx) {
  bytecode_insn insn = parse_insn_impl(reader, pc, ctx);
  insn.original_pc = pc;
  return insn;
}

int convert_pc_to_insn(int pc, int *pc_to_insn, u32 max_pc) {
  DCHECK(pc < (int)max_pc && pc >= 0); // checked pc should have caught this earlier
  int insn = pc_to_insn[pc];
  if (insn == -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), "invalid program counter %d", pc);
    format_error_dynamic(strdup(buf));
  }
  return insn;
}

void convert_pc_offsets_to_insn_offsets(bytecode_insn *code, int insn_count, int *pc_to_insn, u32 max_pc) {
  for (int i = 0; i < insn_count; ++i) {
    bytecode_insn *insn = &code[i];
    if (insn->kind == insn_tableswitch) {
      insn->tableswitch->default_target = convert_pc_to_insn(insn->tableswitch->default_target, pc_to_insn, max_pc);
      int count = insn->tableswitch->high - insn->tableswitch->low + 1;
      for (int j = 0; j < count; ++j) {
        insn->tableswitch->targets[j] = convert_pc_to_insn(insn->tableswitch->targets[j], pc_to_insn, max_pc);
      }
    } else if (insn->kind == insn_lookupswitch) {
      insn->lookupswitch->default_target = convert_pc_to_insn(insn->lookupswitch->default_target, pc_to_insn, max_pc);
      for (int j = 0; j < insn->lookupswitch->targets_count; ++j) {
        insn->lookupswitch->targets[j] = convert_pc_to_insn(insn->lookupswitch->targets[j], pc_to_insn, max_pc);
      }
    } else if (insn->kind >= insn_goto && insn->kind <= insn_ifnull) {
      // instruction uses index to store PC; convert to instruction
      insn->index = convert_pc_to_insn(insn->index, pc_to_insn,
                                       max_pc); // always decreases, so ok
    }
  }
}

void parse_bootstrap_methods_attribute(cf_byteslice attr_reader, attribute *attr, classfile_parse_ctx *ctx) {
  attr->kind = ATTRIBUTE_KIND_BOOTSTRAP_METHODS;
  u16 count = attr->bootstrap_methods.count = reader_next_u16(&attr_reader, "bootstrap methods count");
  bootstrap_method *methods = attr->bootstrap_methods.methods =
      arena_alloc(ctx->arena, count, sizeof(bootstrap_method));

  for (int i = 0; i < count; ++i) {
    bootstrap_method *method = methods + i;
    method->ref = &checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "bootstrap method ref"),
                                    CP_KIND_METHOD_HANDLE, "bootstrap method ref")
                       ->method_handle;
    u16 arg_count = reader_next_u16(&attr_reader, "bootstrap method arg count");
    method->args_count = arg_count;
    method->args = arena_alloc(ctx->arena, arg_count, sizeof(cp_entry *));
    for (int j = 0; j < arg_count; ++j) {
      const int allowed = CP_KIND_STRING | CP_KIND_INTEGER | CP_KIND_FLOAT | CP_KIND_LONG | CP_KIND_DOUBLE |
                          CP_KIND_METHOD_HANDLE | CP_KIND_METHOD_TYPE | CP_KIND_CLASS | CP_KIND_DYNAMIC_CONSTANT;
      method->args[j] = checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "bootstrap method arg"), allowed,
                                         "bootstrap method arg");
    }
  }
}

void parse_attribute(cf_byteslice *reader, classfile_parse_ctx *ctx, attribute *attr);

attribute_code parse_code_attribute(cf_byteslice attr_reader, classfile_parse_ctx *ctx) {
  u16 max_stack = reader_next_u16(&attr_reader, "max stack");
  u16 max_locals = reader_next_u16(&attr_reader, "max locals");
  u32 code_length = reader_next_u32(&attr_reader, "code length");

  const u8 *code_start = attr_reader.bytes;

  cf_byteslice code_reader = reader_get_slice(&attr_reader, code_length, "code");
  bytecode_insn *code = arena_alloc(ctx->arena, code_length, sizeof(bytecode_insn));

  ctx->current_code_max_pc = code_length;

  int *pc_to_insn = malloc(code_length * sizeof(int)); // -1 = no corresponding instruction to that PC
  ctx->temp_allocation = pc_to_insn;
  memset(pc_to_insn, -1, code_length * sizeof(int));

  int insn_count = 0;
  while (code_reader.len > 0) {
    int pc = code_reader.bytes - code_start;
    pc_to_insn[pc] = insn_count;
    code[insn_count] = parse_insn(&code_reader, pc, ctx);
    ++insn_count;
  }

  convert_pc_offsets_to_insn_offsets(code, insn_count, pc_to_insn, code_length);

  u16 exception_table_length = reader_next_u16(&attr_reader, "exception table length");
  attribute_exception_table *table = nullptr;
  if (exception_table_length) {
    table = arena_alloc(ctx->arena, 1, sizeof(attribute_exception_table));
    table->entries =
        arena_alloc(ctx->arena, table->entries_count = exception_table_length, sizeof(exception_table_entry));

    for (int i = 0; i < exception_table_length; ++i) {
      exception_table_entry *ent = table->entries + i;
      u16 start_pc = reader_next_u16(&attr_reader, "exception start pc");
      u16 end_pc = reader_next_u16(&attr_reader, "exception end pc");
      u16 handler_pc = reader_next_u16(&attr_reader, "exception handler pc");
      u16 catch_type = reader_next_u16(&attr_reader, "exception catch type");

      if (start_pc >= code_length || end_pc > code_length || handler_pc >= code_length)
        format_error_static("exception table entry out of bounds");
      if (start_pc >= end_pc)
        format_error_static("exception table entry start >= end");

      ent->start_insn = convert_pc_to_insn(start_pc, pc_to_insn, code_length);
      ent->end_insn =
          end_pc == code_length ? (s32)code_length : (s32)convert_pc_to_insn(end_pc, pc_to_insn, code_length);
      ent->handler_insn = convert_pc_to_insn(handler_pc, pc_to_insn, code_length);
      ent->catch_type = catch_type == 0
                            ? nullptr
                            : &checked_cp_entry(ctx->cp, catch_type, CP_KIND_CLASS, "exception catch type")->class_info;
    }
  }

  free(pc_to_insn);
  ctx->temp_allocation = nullptr;

  u16 attributes_count = reader_next_u16(&attr_reader, "code attributes count");
  attribute *attributes = arena_alloc(ctx->arena, attributes_count, sizeof(attribute));

  attribute_line_number_table *lnt = NULL;
  attribute_local_variable_table *lvt = NULL;
  for (int i = 0; i < attributes_count; ++i) {
    attribute *attr = attributes + i;
    parse_attribute(&attr_reader, ctx, attr);
    if (attr->kind == ATTRIBUTE_KIND_LINE_NUMBER_TABLE) {
      lnt = &attr->lnt;
    } else if (attr->kind == ATTRIBUTE_KIND_LOCAL_VARIABLE_TABLE) {
      lvt = &attr->lvt;
    }
  }

  return (attribute_code){.max_stack = max_stack,
                          .max_locals = max_locals,
                          .insn_count = insn_count,
                          .frame_size = sizeof(stack_frame) + max_stack * sizeof(stack_value),
                          .max_formal_pc = ctx->current_code_max_pc,
                          .code = code,
                          .attributes = attributes,
                          .exception_table = table,
                          .line_number_table = lnt,
                          .local_variable_table = lvt,
                          .attributes_count = attributes_count};
}

// 4.2.2. Unqualified Names
void check_unqualified_name(slice name, bool is_method, const char *reading) {
  // "An unqualified name must contain at least one Unicode code point and must
  // not contain any of the ASCII characters . ; [ /"
  // "Method names are further constrained so that ... they must not contain
  // the ASCII characters < or > (that is, left angle bracket or right angl
  // bracket)."
  if (name.len == 0) {
    char complaint[64];
    snprintf(complaint, sizeof(complaint), "empty %s name", reading);
    format_error_dynamic(strdup(complaint));
  }
  if (utf8_equals(name, "<init>") || utf8_equals(name, "<clinit>")) {
    return; // only valid method names
  }
  for (u32 i = 0; i < name.len; ++i) {
    char c = name.chars[i];
    if (c == '.' || c == ';' || c == '[' || c == '/' || (is_method && (c == '<' || c == '>'))) {
      char complaint[1024];
      snprintf(complaint, sizeof(complaint), "invalid %s name: '%.*s'", reading, fmt_slice(name));
      format_error_dynamic(strdup(complaint));
    }
  }
}

void parse_attribute(cf_byteslice *reader, classfile_parse_ctx *ctx, attribute *attr) {
  u16 index = reader_next_u16(reader, "method attribute name");
  attr->name = checked_get_utf8(ctx->cp, index, "method attribute name");
  attr->length = reader_next_u32(reader, "method attribute length");

  cf_byteslice attr_reader = reader_get_slice(reader, attr->length, "Attribute data");
  if (utf8_equals(attr->name, "Code")) {
    attr->kind = ATTRIBUTE_KIND_CODE;
    attr->code = parse_code_attribute(attr_reader, ctx);
  } else if (utf8_equals(attr->name, "StackMapTable")) {
    attr->kind = ATTRIBUTE_KIND_STACK_MAP_TABLE;
    attr->smt.entries_count = reader_next_u16(&attr_reader, "stack map table count");
    attr->smt.data = arena_alloc(ctx->arena, attr_reader.len, sizeof(uint8_t));
    attr->smt.length = attr_reader.len;
    memcpy(attr->smt.data, attr_reader.bytes, attr_reader.len);
  } else if (utf8_equals(attr->name, "ConstantValue")) {
    attr->kind = ATTRIBUTE_KIND_CONSTANT_VALUE;
    attr->constant_value = checked_cp_entry(
        ctx->cp, reader_next_u16(&attr_reader, "constant value index"),
        CP_KIND_STRING | CP_KIND_INTEGER | CP_KIND_FLOAT | CP_KIND_LONG | CP_KIND_DOUBLE, "constant value");
  } else if (utf8_equals(attr->name, "BootstrapMethods")) {
    parse_bootstrap_methods_attribute(attr_reader, attr, ctx);
  } else if (utf8_equals(attr->name, "EnclosingMethod")) {
    attr->kind = ATTRIBUTE_KIND_ENCLOSING_METHOD;
    u16 enclosing_class_index = reader_next_u16(&attr_reader, "enclosing class index");
    u16 enclosing_method_index = reader_next_u16(&attr_reader, "enclosing method index");
    attr->enclosing_method = (attribute_enclosing_method){
        enclosing_class_index
            ? &checked_cp_entry(ctx->cp, enclosing_class_index, CP_KIND_CLASS, "enclosing class")->class_info
            : nullptr,
        enclosing_method_index
            ? &checked_cp_entry(ctx->cp, enclosing_method_index, CP_KIND_NAME_AND_TYPE, "enclosing method")
                   ->name_and_type
            : nullptr};
  } else if (utf8_equals(attr->name, "SourceFile")) {
    attr->kind = ATTRIBUTE_KIND_SOURCE_FILE;
    attr->source_file.name =
        checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "source file index"), CP_KIND_UTF8, "source file")
            ->utf8;
  } else if (utf8_equals(attr->name, "LineNumberTable")) {
    attr->kind = ATTRIBUTE_KIND_LINE_NUMBER_TABLE;
    u16 count = attr->lnt.entry_count = reader_next_u16(&attr_reader, "line number table count");
    attr->lnt.entries = arena_alloc(ctx->arena, count, sizeof(line_number_table_entry));
    for (int i = 0; i < count; ++i) {
      line_number_table_entry *entry = attr->lnt.entries + i;
      entry->start_pc = reader_next_u16(&attr_reader, "line number start pc");
      entry->line = reader_next_u16(&attr_reader, "line number");
    }
  } else if (utf8_equals(attr->name, "MethodParameters")) {
    attr->kind = ATTRIBUTE_KIND_METHOD_PARAMETERS;
    int count = attr->method_parameters.count = reader_next_u8(&attr_reader, "method parameters count");
    method_parameter_info *params = attr->method_parameters.params =
        arena_alloc(ctx->arena, count, sizeof(method_parameter_info));

    for (int i = 0; i < count; ++i) {
      u16 name_index = reader_next_u16(&attr_reader, "method parameter name");
      // "If the value of the name_index item is zero, then this parameters
      // element indicates a formal parameter with no name"
      if (name_index) {
        slice param_name = checked_get_utf8(ctx->cp, name_index, "method parameter name");
        check_unqualified_name(param_name, false, "method parameter name");
        params[i].name = param_name;
      } else {
        params[i].name = null_str();
      }

      params[i].access_flags = reader_next_u16(&attr_reader, "method parameter access flags");
    }
  } else if (utf8_equals(attr->name, "RuntimeVisibleAnnotations")) {
#define BYTE_ARRAY_ANNOTATION(attr_kind, union_member)                                                                 \
  attr->kind = attr_kind;                                                                                              \
  u8 *data = attr->annotations.data = arena_alloc(ctx->arena, attr_reader.len, sizeof(u8));                            \
  memcpy(data, attr_reader.bytes, attr_reader.len);                                                                    \
  attr->annotations.length = attr_reader.len;

    BYTE_ARRAY_ANNOTATION(ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS, annotations);
  } else if (utf8_equals(attr->name, "RuntimeVisibleParameterAnnotations")) {
    BYTE_ARRAY_ANNOTATION(ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS, parameter_annotations);
  } else if (utf8_equals(attr->name, "RuntimeVisibleTypeAnnotations")) {
    BYTE_ARRAY_ANNOTATION(ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS, type_annotations);
  } else if (utf8_equals(attr->name, "AnnotationDefault")) {
    BYTE_ARRAY_ANNOTATION(ATTRIBUTE_KIND_ANNOTATION_DEFAULT, annotation_default);
  } else if (utf8_equals(attr->name, "Signature")) {
    attr->kind = ATTRIBUTE_KIND_SIGNATURE;
    attr->signature.utf8 =
        checked_get_utf8(ctx->cp, reader_next_u16(&attr_reader, "Signature index"), "Signature index");
  } else if (utf8_equals(attr->name, "NestHost")) {
    attr->kind = ATTRIBUTE_KIND_NEST_HOST;
    attr->nest_host =
        &checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "NestHost index"), CP_KIND_CLASS, "NestHost index")
             ->class_info;
  } else if (utf8_equals(attr->name, "LocalVariableTable")) {
    attr->kind = ATTRIBUTE_KIND_LOCAL_VARIABLE_TABLE;
    u16 count = attr->lvt.entries_count = reader_next_u16(&attr_reader, "local variable table count");
    attr->lvt.entries = arena_alloc(ctx->arena, count, sizeof(attribute_lvt_entry));
    for (int i = 0; i < count; ++i) {
      attribute_lvt_entry *entry = &attr->lvt.entries[i];
      entry->start_pc = reader_next_u16(&attr_reader, "local variable start pc");
      entry->end_pc = entry->start_pc + reader_next_u16(&attr_reader, "local variable length");
      entry->name = checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "local variable name"), CP_KIND_UTF8,
                                     "local variable name")
                        ->utf8;
      entry->descriptor = checked_cp_entry(ctx->cp, reader_next_u16(&attr_reader, "local variable descriptor"),
                                           CP_KIND_UTF8, "local variable descriptor")
                              ->utf8;
      entry->index = reader_next_u16(&attr_reader, "local variable index");
    }
  } else {
    attr->kind = ATTRIBUTE_KIND_UNKNOWN;
  }
}

attribute *find_attribute_by_kind(classdesc *desc, attribute_kind kind) {
  for (int i = 0; i < desc->attributes_count; ++i) {
    if (desc->attributes[i].kind == kind) {
      return desc->attributes + i;
    }
  }
  return nullptr;
}

/**
 * Parse a method in a classfile.
 */
cp_method parse_method(cf_byteslice *reader, classfile_parse_ctx *ctx) {
  cp_method method = {0};
  method.access_flags = reader_next_u16(reader, "method access flags");
  method.name = checked_get_utf8(ctx->cp, reader_next_u16(reader, "method name"), "method name");
  method.unparsed_descriptor =
      checked_get_utf8(ctx->cp, reader_next_u16(reader, "method descriptor"), "method descriptor");
  method.attributes_count = reader_next_u16(reader, "method attributes count");
  method.attributes = arena_alloc(ctx->arena, method.attributes_count, sizeof(attribute));
  method.descriptor = arena_alloc(ctx->arena, 1, sizeof(method_descriptor));
  method.is_ctor = utf8_equals(method.name, "<init>");
  method.is_clinit = utf8_equals(method.name, "<clinit>");
  char *error = parse_method_descriptor(method.unparsed_descriptor, method.descriptor, ctx->arena);
  if (error) {
    format_error_dynamic(error);
  }

  for (int i = 0; i < method.attributes_count; i++) {
    attribute *attrib = &method.attributes[i];
    parse_attribute(reader, ctx, attrib);
    if (attrib->kind == ATTRIBUTE_KIND_CODE) {
      method.code = &attrib->code;
    }
  }

  return method;
}

cp_field read_field(cf_byteslice *reader, classfile_parse_ctx *ctx) {
  cp_field field = {.access_flags = reader_next_u16(reader, "field access flags"),
                    .name = checked_get_utf8(ctx->cp, reader_next_u16(reader, "field name"), "field name"),
                    .descriptor =
                        checked_get_utf8(ctx->cp, reader_next_u16(reader, "field descriptor"), "field descriptor"),
                    .attributes_count = reader_next_u16(reader, "field attributes count")};
  field.attributes = arena_alloc(ctx->arena, field.attributes_count, sizeof(attribute));

  for (int i = 0; i < field.attributes_count; i++) {
    parse_attribute(reader, ctx, field.attributes + i);
  }

  char *error = parse_complete_field_descriptor(field.descriptor, &field.parsed_descriptor, ctx);
  if (error)
    format_error_dynamic(error);

  return field;
}

/**
 * Parse the field descriptor in the string slice starting at **chars, with
 * length len, writing the result to result and returning an owned error message
 * if there was an error.
 */
char *parse_field_descriptor(const char **chars, size_t len, field_descriptor *result, arena *arena) {
  const char *field_start = *chars;
  const char *end = *chars + len;
  int dimensions = 0;
  while (*chars < end) {
    result->dimensions = dimensions;
    if (dimensions > 255)
      return strdup("too many dimensions (max 255)");
    char c = *(*chars)++;
    switch (c) {
    case 'B':
    case 'C':
    case 'D':
    case 'F':
    case 'I':
    case 'J':
    case 'S':
    case 'Z':
    case 'V':
      type_kind prim = read_type_kind_char(c);
      result->base_kind = prim;
      result->repr_kind = result->dimensions ? TYPE_KIND_REFERENCE : kind_to_representable_kind(prim);
      result->unparsed = arena_make_str(arena, field_start, *chars - field_start);
      if (c == 'V' && dimensions > 0)
        return strdup("void cannot have dimensions");
      return nullptr;
    case '[':
      ++dimensions;
      break;
    case 'L': {
      const char *class_start = *chars;
      while (*chars < end && **chars != ';')
        ++*chars;
      if (*chars == end)
        return strdup("missing ';' in reference type");
      int class_name_len = *chars - class_start;
      if (class_name_len == 0) {
        return strdup("missing reference type name");
      }
      ++*chars;
      result->base_kind = result->repr_kind = TYPE_KIND_REFERENCE;
      result->unparsed = arena_make_str(arena, field_start, *chars - field_start);
      DCHECK(class_start > field_start);
      DCHECK(*chars > field_start + 1);
      result->class_name = subslice_to(result->unparsed, class_start - field_start, *chars - field_start - 1);
      return nullptr;
    }
    default: {
      char buf[64];
      snprintf(buf, sizeof(buf), "invalid field descriptor character '%c'", *(*chars - 1));
      return strdup(buf);
    }
    }
  }

  return strdup("Expected field descriptor character, but reached end of string");
}

char *parse_method_descriptor(const slice entry, method_descriptor *result, arena *arena) {
  // MethodDescriptor:
  // ( { ParameterDescriptor } )
  // ParameterDescriptor:
  // FieldType
  const size_t len = entry.len;
  const char *chars = entry.chars, *end = chars + len;
  if (len < 1 || *chars++ != '(')
    return strdup("Expected '('");
  result->args_count = 0;
  field_descriptor *fields = nullptr;
  while (chars < end && *chars != ')') {
    field_descriptor arg;

    char *error = parse_field_descriptor(&chars, end - chars, &arg, arena);
    if (error || arg.base_kind == TYPE_KIND_VOID) {
      free(fields);
      return error ? error : strdup("void as method parameter");
    }

    arrput(fields, arg);
    result->args_count++;
  }
  result->args = arena_alloc(arena, result->args_count, sizeof(field_descriptor));
  if (result->args_count) {
    memcpy(result->args, fields, result->args_count * sizeof(field_descriptor));
  }
  arrfree(fields);
  if (chars >= end) {
    return strdup("missing ')' in method descriptor");
  }
  chars++; // skip ')'
  char *error = parse_field_descriptor(&chars, end - chars, &result->return_type, arena);
  return error;
}

// Go through the InvokeDynamic entries and link their bootstrap method pointers
void link_bootstrap_methods(classdesc *cf) {
  constant_pool *cp = cf->pool;
  for (int i = 1; i < cp->entries_len; i++) {
    if (cp->entries[i].kind == CP_KIND_INVOKE_DYNAMIC) {
      cp_indy_info *indy = &cp->entries[i].indy_info;
      int index = (int)(uintptr_t)indy->method;
      if (!cf->bootstrap_methods) {
        format_error_static("Missing BootstrapMethods attribute");
      }
      if (index < 0 || index >= cf->bootstrap_methods->count) {
        format_error_static("Invalid bootstrap method index");
      }
      indy->method = &cf->bootstrap_methods->methods[index];
    }
  }
}

int no_smt_found(attribute_code *code) {
  for (int i = 0; i < code->attributes_count; i++)
    if (code->attributes[i].kind == ATTRIBUTE_KIND_STACK_MAP_TABLE)
      return 0;
  return 1;
}

parse_result_t parse_classfile(const u8 *bytes, size_t len, classdesc *result, heap_string *error) {
  cf_byteslice reader = {.bytes = bytes, .len = len};
  classdesc *cf = result;
  arena_init(&cf->arena);
  classfile_parse_ctx ctx = {.arena = &cf->arena, .cp = nullptr};

  if (setjmp(format_error_jmp_buf)) {
    arena_uninit(&cf->arena); // clean up our shit
    free(ctx.temp_allocation);
    if (error) {
      *error = make_heap_str_from((slice){.chars = format_error_msg, .len = strlen(format_error_msg)});
    }
    if (format_error_needs_free)
      free(format_error_msg);
    result->state = CD_STATE_LINKAGE_ERROR;
    return PARSE_ERR;
  }

  const u32 magic = reader_next_u32(&reader, "magic");
  if (magic != 0xCAFEBABE) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Invalid magic number 0x%08x", magic);
    format_error_dynamic(strdup(buf));
  }

  u16 minor = reader_next_u16(&reader, "minor version");
  u16 major = reader_next_u16(&reader, "major version");

  bool maybe_missing_smt = major < 50;
  (void)minor;

  cf->pool = parse_constant_pool(&reader, &ctx);
  cf->access_flags = reader_next_u16(&reader, "access flags");
  // "Compilers to the instruction set of the Java Virtual Machine should se
  // the ACC_SUPER flag. In Java SE 8 and above, the Java Virtual Machine
  // considers the ACC_SUPER flag to be set in every class file, regardless of
  // the actual value of the flag in the class file and the version of the
  // class file."
  // -> getModifiers doesn't return this bit
  cf->access_flags &= ~ACCESS_SYNCHRONIZED; // not a typo, this is the ACC_SUPER bit

  cp_class_info *this_class =
      &checked_cp_entry(cf->pool, reader_next_u16(&reader, "this class"), CP_KIND_CLASS, "this class")->class_info;
  cf->self = this_class;
  cf->name = this_class->name;

  bool is_primordial_object = utf8_equals(cf->name, "java/lang/Object");

  u16 super_class = reader_next_u16(&reader, "super class");
  cf->super_class = ((cf->access_flags & ACCESS_MODULE) | is_primordial_object)
                        ? nullptr
                        : &checked_cp_entry(cf->pool, super_class, CP_KIND_CLASS, "super class")->class_info;

  // Parse superinterfaces
  cf->interfaces_count = reader_next_u16(&reader, "interfaces count");
  cf->interfaces = arena_alloc(ctx.arena, cf->interfaces_count, sizeof(cp_class_info *));

  for (int i = 0; i < cf->interfaces_count; i++) {
    cf->interfaces[i] =
        &checked_cp_entry(cf->pool, reader_next_u16(&reader, "interface"), CP_KIND_CLASS, "superinterface")->class_info;
  }

  // Parse fields
  cf->fields_count = reader_next_u16(&reader, "fields count");
  cf->fields = arena_alloc(ctx.arena, cf->fields_count, sizeof(cp_field));
  for (int i = 0; i < cf->fields_count; i++) {
    cf->fields[i] = read_field(&reader, &ctx);
    cf->fields[i].my_class = result;
  }
  cf->static_fields = nullptr;
  cf->static_references = nullptr;
  cf->instance_references = nullptr;

  // Parse methods
  cf->methods_count = reader_next_u16(&reader, "methods count");
  cf->methods = arena_alloc(ctx.arena, cf->methods_count, sizeof(cp_method));

  cf->bootstrap_methods = nullptr;
  cf->indy_insns = nullptr;
  cf->sigpoly_insns = nullptr;
  cf->array_type = nullptr;

  bool in_MethodHandle =
      utf8_equals(cf->name, "java/lang/invoke/MethodHandle") || utf8_equals(cf->name, "java/lang/invoke/VarHandle");
  for (int i = 0; i < cf->methods_count; ++i) {
    cp_method *method = cf->methods + i;
    *method = parse_method(&reader, &ctx);
    method->my_class = result;
    method->missing_smt = maybe_missing_smt && method->code && no_smt_found(method->code);
    method->my_index = i;

    // Mark signature polymorphic functions
    if (in_MethodHandle && method->access_flags & ACCESS_NATIVE) {
      method->is_signature_polymorphic = true;
    }
  }

  // Parse attributes
  cf->attributes_count = reader_next_u16(&reader, "class attributes count");
  cf->attributes = arena_alloc(ctx.arena, cf->attributes_count, sizeof(attribute));
  for (int i = 0; i < cf->attributes_count; i++) {
    attribute *attr = cf->attributes + i;
    parse_attribute(&reader, &ctx, attr);

    if (attr->kind == ATTRIBUTE_KIND_BOOTSTRAP_METHODS) {
      cf->bootstrap_methods = &attr->bootstrap_methods;
    } else if (attr->kind == ATTRIBUTE_KIND_SOURCE_FILE) {
      cf->source_file = &attr->source_file;
    } else if (attr->kind == ATTRIBUTE_KIND_NEST_HOST) {
      cf->nest_host = attr->nest_host;
    }
  }

  // Check for trailing bytes
  if (reader.len > 0) {
    format_error_static("Trailing bytes in classfile");
  }

  link_bootstrap_methods(cf);
  result->state = CD_STATE_LOADED;

  // Add indy instruction pointers
  for (int i = 0; i < cf->methods_count; ++i) {
    cp_method *method = cf->methods + i;
    if (method->code) {
      // Add pointers to indy instructions for their CallSites to be GC roots
      for (int j = 0; j < method->code->insn_count; ++j) {
        bytecode_insn *insn = method->code->code + j;
        if (insn->kind == insn_invokedynamic) {
          arrput(cf->indy_insns, insn);
        }
      }
    }
  }

  return PARSE_SUCCESS;
}
