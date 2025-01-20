//
// Created by alec on 12/18/24.
//

#include <assert.h>
#include <setjmp.h>
#include <stdlib.h>

#include "analysis.h"
#include "classfile.h"
#include "util.h"
#include "wasm_jit.h"

static const char *cp_kind_to_string(bjvm_cp_kind kind) {
  switch (kind) {
  case BJVM_CP_KIND_INVALID:
    return "invalid";
  case BJVM_CP_KIND_UTF8:
    return "utf8";
  case BJVM_CP_KIND_INTEGER:
    return "integer";
  case BJVM_CP_KIND_FLOAT:
    return "float";
  case BJVM_CP_KIND_LONG:
    return "long";
  case BJVM_CP_KIND_DOUBLE:
    return "double";
  case BJVM_CP_KIND_CLASS:
    return "class";
  case BJVM_CP_KIND_STRING:
    return "string";
  case BJVM_CP_KIND_FIELD_REF:
    return "field";
  case BJVM_CP_KIND_METHOD_REF:
    return "method";
  case BJVM_CP_KIND_INTERFACE_METHOD_REF:
    return "interfacemethod";
  case BJVM_CP_KIND_NAME_AND_TYPE:
    return "nameandtype";
  case BJVM_CP_KIND_METHOD_HANDLE:
    return "methodhandle";
  case BJVM_CP_KIND_METHOD_TYPE:
    return "methodtype";
  case BJVM_CP_KIND_INVOKE_DYNAMIC:
    return "invokedynamic";
  default:
    UNREACHABLE();
  }
}

void free_method(bjvm_cp_method *method) {
  free_code_analysis(method->code_analysis);
  free_wasm_compiled_method(method->compiled_method);
}

void bjvm_free_classfile(bjvm_classdesc cf) {
  for (int i = 0; i < cf.methods_count; ++i)
    free_method(&cf.methods[i]);
  free(cf.static_fields);
  arrfree(cf.indy_insns);
  bjvm_free_compressed_bitset(cf.static_references);
  bjvm_free_compressed_bitset(cf.instance_references);
  arena_uninit(&cf.arena);
}

bjvm_cp_entry *get_constant_pool_entry(bjvm_constant_pool *pool, int index) {
  assert(index >= 0 && index < pool->entries_len);
  return &pool->entries[index];
}

typedef struct {
  const uint8_t *bytes;
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

#define READER_NEXT_IMPL(name, type)                                           \
  type name(cf_byteslice *reader, const char *reason) {                        \
    if (unlikely(reader->len < sizeof(type))) {                                \
      format_error_static(reason);                                             \
    }                                                                          \
    char data[sizeof(type)];                                                   \
    memcpy(data, reader->bytes, sizeof(type));                                 \
    if (sizeof(type) == 2) {                                                   \
      *(uint16_t *)data = __bswap_16(*(uint16_t *)data);                       \
    } else if (sizeof(type) == 4) {                                            \
      *(uint32_t *)data = __bswap_32(*(uint32_t *)data);                       \
    } else if (sizeof(type) == 8) {                                            \
      *(uint64_t *)data = __bswap_64(*(uint64_t *)data);                       \
    }                                                                          \
    reader->bytes += sizeof(type);                                             \
    reader->len -= sizeof(type);                                               \
    return *(type *)data;                                                      \
  }

READER_NEXT_IMPL(reader_next_u8, uint8_t)
READER_NEXT_IMPL(reader_next_i8, int8_t)
READER_NEXT_IMPL(reader_next_u16, uint16_t)
READER_NEXT_IMPL(reader_next_i16, int16_t)
READER_NEXT_IMPL(reader_next_u32, uint32_t)
READER_NEXT_IMPL(reader_next_i32, int32_t)
READER_NEXT_IMPL(reader_next_u64, uint64_t)
READER_NEXT_IMPL(reader_next_i64, int64_t)
READER_NEXT_IMPL(reader_next_f32, float)
READER_NEXT_IMPL(reader_next_f64, double)

cf_byteslice reader_get_slice(cf_byteslice *reader, size_t len,
                              const char *reason) {
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
  bjvm_constant_pool *cp;
  arena *arena;
  int current_code_max_pc;
  void *temp_allocation;
} bjvm_classfile_parse_ctx;

// See: 4.4.7. The CONSTANT_Utf8_info Structure
bjvm_utf8 parse_modified_utf8(const uint8_t *bytes, int len, arena *arena) {
  char *result = arena_alloc(arena, len + 1, sizeof(char));
  memcpy(result, bytes, len);
  result[len] = '\0';
  return (bjvm_utf8){.chars = result, .len = len};
}

bjvm_cp_entry *bjvm_check_cp_entry(bjvm_cp_entry *entry, int expected_kinds,
                                   const char *reason) {
  assert(reason);
  if (entry->kind & expected_kinds)
    return entry;
  char buf[1000] = {0}, *write = buf, *end = buf + sizeof(buf);
  write = write + snprintf(buf, end - write,
                           "Unexpected constant pool entry kind %d at index "
                           "%d (expected one of: [ ",
                           entry->kind, entry->my_index);
  for (int i = 0; i < 14; ++i)
    if (expected_kinds & 1 << i)
      write += snprintf(write, end - write, "%s ", cp_kind_to_string(1 << i));
  write += snprintf(write, end - write, "]) while reading %s", reason);
  format_error_dynamic(strdup(buf));
}

const bjvm_utf8 * bjvm_lvt_lookup(int index, int original_pc, const bjvm_attribute_local_variable_table *table){
  // Linear scan throught the whole array
  for (int i = 0; i < table->entries_count; ++i) {
    bjvm_attribute_lvt_entry *entry = table->entries + i;
    if (entry->index == index && entry->start_pc <= original_pc && entry->end_pc > original_pc) {
      return &entry->name;
    }
  }
  return nullptr;
}

bjvm_cp_entry *checked_cp_entry(bjvm_constant_pool *pool, int index,
                                int expected_kinds, const char *reason) {
  assert(reason);
  if (!(index >= 0 && index < pool->entries_len)) {
    char buf[256] = {0};
    snprintf(
        buf, sizeof(buf),
        "Invalid constant pool entry index %d (pool size %d) while reading %s",
        index, pool->entries_len, reason);
    format_error_dynamic(strdup(buf));
  }
  return bjvm_check_cp_entry(&pool->entries[index], expected_kinds, reason);
}

bjvm_utf8 checked_get_utf8(bjvm_constant_pool *pool, int index,
                           const char *reason) {
  return checked_cp_entry(pool, index, BJVM_CP_KIND_UTF8, reason)->utf8;
}

char *parse_complete_field_descriptor(const bjvm_utf8 entry,
                                      bjvm_field_descriptor *result,
                                      bjvm_classfile_parse_ctx *ctx) {
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

/**
 * Parse a single constant pool entry.
 * @param reader The reader to parse from.
 * @param ctx The parse context.
 * @param skip_linking If true, don't add pointers to other constant pool
 * entries.
 * @return The resolved entry.
 */
bjvm_cp_entry parse_constant_pool_entry(cf_byteslice *reader,
                                        bjvm_classfile_parse_ctx *ctx,
                                        bool skip_linking) {
  enum {
    CONSTANT_Class = 7,
    CONSTANT_Fieldref = 9,
    CONSTANT_Methodref = 10,
    CONSTANT_InterfaceMethodref = 11,
    CONSTANT_String = 8,
    CONSTANT_Integer = 3,
    CONSTANT_Float = 4,
    CONSTANT_Long = 5,
    CONSTANT_Double = 6,
    CONSTANT_NameAndType = 12,
    CONSTANT_Utf8 = 1,
    CONSTANT_MethodHandle = 15,
    CONSTANT_MethodType = 16,
    CONSTANT_Dynamic = 17,
    CONSTANT_InvokeDynamic = 18,
    CONSTANT_Module = 19,
    CONSTANT_Package = 20
  };

  uint8_t kind = reader_next_u8(reader, "cp kind");
  switch (kind) {
  case CONSTANT_Module:
    [[fallthrough]];
  case CONSTANT_Package:
    [[fallthrough]];
  case CONSTANT_Class: {
    bjvm_cp_kind entry_kind = kind == CONSTANT_Class    ? BJVM_CP_KIND_CLASS
                              : kind == CONSTANT_Module ? BJVM_CP_KIND_MODULE
                                                        : BJVM_CP_KIND_PACKAGE;

    uint16_t index = reader_next_u16(reader, "class index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_CLASS,
        .class_info = {
            .name = skip_linking
                        ? null_str()
                        : checked_get_utf8(ctx->cp, index, "class info name")}};
  }

  case CONSTANT_Fieldref:
  case CONSTANT_Methodref:
  case CONSTANT_InterfaceMethodref: {
    uint16_t class_index = reader_next_u16(reader, "class index");
    uint16_t name_and_type_index =
        reader_next_u16(reader, "name and type index");

    bjvm_cp_kind entry_kind = kind == CONSTANT_Fieldref ? BJVM_CP_KIND_FIELD_REF
                              : kind == CONSTANT_Methodref
                                  ? BJVM_CP_KIND_METHOD_REF
                                  : BJVM_CP_KIND_INTERFACE_METHOD_REF;
    bjvm_cp_class_info *class_info =
        skip_linking ? nullptr
                     : &checked_cp_entry(
                            ctx->cp, class_index, BJVM_CP_KIND_CLASS,
                            "fieldref/methodref/interfacemethodref class info")
                            ->class_info;

    bjvm_cp_name_and_type *name_and_type =
        skip_linking
            ? nullptr
            : &checked_cp_entry(
                   ctx->cp, name_and_type_index, BJVM_CP_KIND_NAME_AND_TYPE,
                   "fieldref/methodref/interfacemethodref name and type")
                   ->name_and_type;

    if (kind == CONSTANT_Fieldref) {
      return (bjvm_cp_entry){.kind = entry_kind,
                             .field = {.class_info = class_info,
                                       .nat = name_and_type,
                                       .field = nullptr}};
    }
    return (bjvm_cp_entry){
        .kind = entry_kind,
        .methodref = {.class_info = class_info, .nat = name_and_type}};
  }
  case CONSTANT_String: {
    uint16_t index = reader_next_u16(reader, "string index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_STRING,
        .string = {.chars = skip_linking ? null_str()
                                         : checked_get_utf8(ctx->cp, index,
                                                            "string value")}};
  }
  case CONSTANT_Integer: {
    int32_t value = reader_next_i32(reader, "integer value");
    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_INTEGER,
                           .integral = {.value = value}};
  }
  case CONSTANT_Float: {
    double value = reader_next_f32(reader, "double value");
    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_FLOAT,
                           .floating = {.value = value}};
  }
  case CONSTANT_Long: {
    int64_t value = reader_next_i64(reader, "long value");
    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_LONG,
                           .integral = {.value = value}};
  }
  case CONSTANT_Double: {
    double value = reader_next_f64(reader, "double value");
    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_DOUBLE,
                           .floating = {.value = value}};
  }
  case CONSTANT_NameAndType: {
    uint16_t name_index = reader_next_u16(reader, "name index");
    uint16_t descriptor_index = reader_next_u16(reader, "descriptor index");

    bjvm_utf8 name = skip_linking ? null_str()
                                  : checked_get_utf8(ctx->cp, name_index,
                                                     "name and type name");

    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_NAME_AND_TYPE,
        .name_and_type = {
            .name = name,
            .descriptor = skip_linking
                              ? null_str()
                              : checked_get_utf8(ctx->cp, descriptor_index,
                                                 "name and type descriptor")}};
  }
  case CONSTANT_Utf8: {
    uint16_t length = reader_next_u16(reader, "utf8 length");
    cf_byteslice bytes_reader = reader_get_slice(reader, length, "utf8 data");
    bjvm_utf8 utf8 = {nullptr};
    if (skip_linking) {
      utf8 = parse_modified_utf8(bytes_reader.bytes, length, ctx->arena);
    }
    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_UTF8, .utf8 = utf8};
  }
  case CONSTANT_MethodHandle: {
    uint8_t handle_kind = reader_next_u8(reader, "method handle kind");
    uint16_t reference_index = reader_next_u16(reader, "reference index");
    bjvm_cp_entry *entry = skip_linking
                               ? nullptr
                               : checked_cp_entry(ctx->cp, reference_index, -1,
                                                  "method handle reference");
    // TODO validate both
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_METHOD_HANDLE,
        .method_handle = {.handle_kind = handle_kind, .reference = entry}};
  }
  case CONSTANT_MethodType: {
    uint16_t desc_index = reader_next_u16(reader, "descriptor index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_METHOD_TYPE,
        .method_type = {.descriptor =
                            skip_linking
                                ? null_str()
                                : checked_get_utf8(ctx->cp, desc_index,
                                                   "method type descriptor")}};
  }
  case CONSTANT_Dynamic:
    [[fallthrough]];
  case CONSTANT_InvokeDynamic: {
    uint16_t bootstrap_method_attr_index =
        reader_next_u16(reader, "bootstrap method attr index");
    uint16_t name_and_type_index =
        reader_next_u16(reader, "name and type index");
    bjvm_cp_name_and_type *name_and_type =
        skip_linking ? nullptr
                     : &checked_cp_entry(ctx->cp, name_and_type_index,
                                         BJVM_CP_KIND_NAME_AND_TYPE,
                                         "indy name and type")
                            ->name_and_type;

    return (bjvm_cp_entry){
        .kind = (kind == CONSTANT_Dynamic) ? BJVM_CP_KIND_DYNAMIC_CONSTANT
                                           : BJVM_CP_KIND_INVOKE_DYNAMIC,
        .indy_info = {// will be converted into a pointer to the method in
                      // link_bootstrap_methods
                      .method = (void *)(uintptr_t)bootstrap_method_attr_index,
                      .name_and_type = name_and_type,
                      .method_descriptor = nullptr}};
  }
  default:
    format_error_static("Invalid constant pool entry kind");
  }
}

bjvm_constant_pool *init_constant_pool(uint16_t count, arena *arena) {
  bjvm_constant_pool *pool = arena_alloc(
      arena, 1,
      sizeof(bjvm_constant_pool) + (count + 1) * sizeof(bjvm_cp_entry));
  pool->entries_len = count + 1;
  return pool;
}

void finish_constant_pool_entry(bjvm_cp_entry *entry,
                                bjvm_classfile_parse_ctx *ctx) {
  switch (entry->kind) {
  case BJVM_CP_KIND_FIELD_REF: {
    bjvm_field_descriptor *parsed_descriptor = nullptr;
    bjvm_cp_name_and_type *name_and_type = entry->field.nat;

    entry->field.parsed_descriptor = parsed_descriptor =
        arena_alloc(ctx->arena, 1, sizeof(bjvm_field_descriptor));
    char *error = parse_complete_field_descriptor(name_and_type->descriptor,
                                                  parsed_descriptor, ctx);
    if (error)
      format_error_dynamic(error);
    break;
  }
  case BJVM_CP_KIND_INVOKE_DYNAMIC: {
    bjvm_method_descriptor *desc =
        arena_alloc(ctx->arena, 1, sizeof(bjvm_method_descriptor));
    char *error = parse_method_descriptor(
        entry->indy_info.name_and_type->descriptor, desc, ctx->arena);
    if (error)
      format_error_dynamic(error);
    entry->indy_info.method_descriptor = desc;
    break;
  }
  case BJVM_CP_KIND_METHOD_REF:
  case BJVM_CP_KIND_INTERFACE_METHOD_REF: {
    bjvm_method_descriptor *desc =
        arena_alloc(ctx->arena, 1, sizeof(bjvm_method_descriptor));
    bjvm_cp_name_and_type *nat = entry->methodref.nat;
    char *error = parse_method_descriptor(nat->descriptor, desc, ctx->arena);
    if (error) {
      char *buf = malloc(1000);
      snprintf(buf, 1000, "Method '%.*s' has invalid descriptor '%.*s': %s",
               fmt_slice(nat->name), fmt_slice(nat->descriptor), error);
      free(error);
      format_error_dynamic(buf);
    }
    entry->methodref.descriptor = desc;
    break;
  }
  case BJVM_CP_KIND_METHOD_TYPE: {
    bjvm_method_descriptor *desc =
        arena_alloc(ctx->arena, 1, sizeof(bjvm_method_descriptor));
    char *error = parse_method_descriptor(entry->method_type.descriptor, desc,
                                          ctx->arena);
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
bjvm_constant_pool *parse_constant_pool(cf_byteslice *reader,
                                        bjvm_classfile_parse_ctx *ctx) {
  uint16_t cp_count = reader_next_u16(reader, "constant pool count");

  bjvm_constant_pool *pool = init_constant_pool(cp_count, ctx->arena);
  ctx->cp = pool;

  get_constant_pool_entry(pool, 0)->kind =
      BJVM_CP_KIND_INVALID; // entry at 0 is always invalid
  cf_byteslice initial_reader_state = *reader;
  for (int resolution_pass = 0; resolution_pass < 2; ++resolution_pass) {
    // In the first pass, read entries; in the second pass, link them via
    // pointers
    for (int cp_i = 1; cp_i < cp_count; ++cp_i) {
      bjvm_cp_entry *ent = get_constant_pool_entry(pool, cp_i);
      bjvm_cp_entry new_entry =
          parse_constant_pool_entry(reader, ctx, !(bool)resolution_pass);
      if (new_entry.kind != BJVM_CP_KIND_UTF8 || resolution_pass == 0) {
        *ent = new_entry; // don't store UTF-8 entries on the second pass
        // TODO fix ^^ this is janky af
      }
      ent->my_index = cp_i;

      if (ent->kind == BJVM_CP_KIND_LONG || ent->kind == BJVM_CP_KIND_DOUBLE) {
        get_constant_pool_entry(pool, cp_i + 1)->kind = BJVM_CP_KIND_INVALID;
        cp_i++;
      }
    }
    if (resolution_pass == 0)
      *reader = initial_reader_state;
  }

  for (int cp_i = 1; cp_i < cp_count; cp_i++) {
    finish_constant_pool_entry(get_constant_pool_entry(pool, cp_i), ctx);
  }

  return pool;
}

int checked_pc(uint32_t insn_pc, int offset, bjvm_classfile_parse_ctx *ctx) {
  int target;
  int overflow = __builtin_add_overflow(insn_pc, offset, &target);
  if (overflow || target < 0 || target >= ctx->current_code_max_pc) {
    format_error_static("Branch target out of bounds");
  }
  return target;
}

bjvm_bytecode_insn parse_tableswitch_insn(cf_byteslice *reader, int pc,
                                          bjvm_classfile_parse_ctx *ctx) {
  int original_pc = pc++;

  // consume u8s until pc = 0 mod 4
  while (pc % 4 != 0) {
    reader_next_u8(reader, "tableswitch padding");
    pc++;
  }

  int default_target = checked_pc(
      original_pc, reader_next_i32(reader, "tableswitch default target"), ctx);
  int low = reader_next_i32(reader, "tableswitch low");
  int high = reader_next_i32(reader, "tableswitch high");
  int64_t targets_count = (int64_t)high - low + 1;

  if (targets_count > 1 << 15) {
    // preposterous, won't fit in the code segment
    format_error_static("tableswitch instruction is too large");
  }
  if (targets_count <= 0) {
    format_error_static("tableswitch high < low");
  }

  int *targets = arena_alloc(ctx->arena, targets_count, sizeof(int));
  for (int i = 0; i < targets_count; ++i) {
    targets[i] = checked_pc(original_pc,
                            reader_next_i32(reader, "tableswitch target"), ctx);
  }
  return (bjvm_bytecode_insn){.kind = bjvm_insn_tableswitch,
                              .original_pc = original_pc,
                              .tableswitch = {.default_target = default_target,
                                              .low = low,
                                              .high = high,
                                              .targets = targets,
                                              .targets_count = targets_count}};
}

bjvm_bytecode_insn parse_lookupswitch_insn(cf_byteslice *reader, int pc,
                                           bjvm_classfile_parse_ctx *ctx) {
  int original_pc = pc++;
  while (pc % 4 != 0) {
    reader_next_u8(reader, "tableswitch padding");
    pc++;
  }

  int default_target = checked_pc(
      original_pc, reader_next_i32(reader, "lookupswitch default target"), ctx);
  int pairs_count = reader_next_i32(reader, "lookupswitch pairs count");

  if (pairs_count > 1 << 15 || pairs_count < 0) {
    format_error_static("lookupswitch instruction is too large");
  }

  int *keys = arena_alloc(ctx->arena, pairs_count, sizeof(int));
  int *targets = arena_alloc(ctx->arena, pairs_count, sizeof(int));

  for (int i = 0; i < pairs_count; ++i) {
    keys[i] = reader_next_i32(reader, "lookupswitch key");
    targets[i] = checked_pc(
        original_pc, reader_next_i32(reader, "lookupswitch target"), ctx);
  }

  return (bjvm_bytecode_insn){.kind = bjvm_insn_lookupswitch,
                              .original_pc = original_pc,
                              .lookupswitch = {.default_target = default_target,
                                               .keys = keys,
                                               .keys_count = pairs_count,
                                               .targets = targets,
                                               .targets_count = pairs_count}};
}

bjvm_type_kind parse_atype(uint8_t atype) {
  switch (atype) {
  case 4:
    return BJVM_TYPE_KIND_BOOLEAN;
  case 5:
    return BJVM_TYPE_KIND_CHAR;
  case 6:
    return BJVM_TYPE_KIND_FLOAT;
  case 7:
    return BJVM_TYPE_KIND_DOUBLE;
  case 8:
    return BJVM_TYPE_KIND_BYTE;
  case 9:
    return BJVM_TYPE_KIND_SHORT;
  case 10:
    return BJVM_TYPE_KIND_INT;
  case 11:
    return BJVM_TYPE_KIND_LONG;
  default: {
    char buf[64];
    snprintf(buf, sizeof(buf), "invalid newarray type %d", atype);
    format_error_dynamic(strdup(buf));
  }
  }
}

bjvm_bytecode_insn parse_insn_impl(cf_byteslice *reader, uint32_t pc,
                                   bjvm_classfile_parse_ctx *ctx) {
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

  uint8_t opcode = reader_next_u8(reader, "instruction opcode");

  switch (opcode) {
  case nop:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_nop};
  case aconst_null:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_aconst_null};
  case iconst_m1:
  case iconst_0:
  case iconst_1:
  case iconst_2:
  case iconst_3:
  case iconst_4:
  case iconst_5:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iconst,
                                .integer_imm = opcode - iconst_0};
  case lconst_0:
  case lconst_1:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lconst,
                                .integer_imm = opcode - lconst_0};
  case fconst_0:
  case fconst_1:
  case fconst_2:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fconst,
                                .f_imm = (float)(opcode - fconst_0)};
  case dconst_0:
  case dconst_1:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dconst,
                                .d_imm = (double)(opcode - dconst_0)};
  case bipush:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iconst,
                                .integer_imm =
                                    reader_next_i8(reader, "bipush immediate")};
  case sipush:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_iconst,
        .integer_imm = reader_next_i16(reader, "sipush immediate")};
  case ldc:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ldc,
        .cp = checked_cp_entry(ctx->cp, reader_next_u8(reader, "ldc index"),
                               BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_FLOAT |
                                   BJVM_CP_KIND_STRING | BJVM_CP_KIND_CLASS,
                               "ldc index")};
  case ldc_w:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ldc,
        .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "ldc_w index"),
                               BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_FLOAT |
                                   BJVM_CP_KIND_STRING | BJVM_CP_KIND_CLASS,
                               "ldc_w index")};
  case ldc2_w:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ldc2_w,
        .cp = checked_cp_entry(ctx->cp, reader_next_u16(reader, "ldc2_w index"),
                               BJVM_CP_KIND_DOUBLE | BJVM_CP_KIND_LONG,
                               "ldc2_w index")};
  case iload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iload,
                                .index = reader_next_u8(reader, "iload index")};
  case lload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lload,
                                .index = reader_next_u8(reader, "lload index")};
  case fload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fload,
                                .index = reader_next_u8(reader, "fload index")};
  case dload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dload,
                                .index = reader_next_u8(reader, "dload index")};
  case aload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_aload,
                                .index = reader_next_u8(reader, "aload index")};
  case iload_0:
  case iload_1:
  case iload_2:
  case iload_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iload,
                                .index = opcode - iload_0};
  case lload_0:
  case lload_1:
  case lload_2:
  case lload_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lload,
                                .index = opcode - lload_0};
  case fload_0:
  case fload_1:
  case fload_2:
  case fload_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fload,
                                .index = opcode - fload_0};
  case dload_0:
  case dload_1:
  case dload_2:
  case dload_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dload,
                                .index = opcode - dload_0};
  case aload_0:
  case aload_1:
  case aload_2:
  case aload_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_aload,
                                .index = opcode - aload_0};
  case iaload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iaload};
  case laload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_laload};
  case faload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_faload};
  case daload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_daload};
  case aaload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_aaload};
  case baload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_baload};
  case caload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_caload};
  case saload:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_saload};
  case istore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_istore,
                                .index =
                                    reader_next_u8(reader, "istore index")};
  case lstore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lstore,
                                .index =
                                    reader_next_u8(reader, "lstore index")};
  case fstore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fstore,
                                .index =
                                    reader_next_u8(reader, "fstore index")};
  case dstore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dstore,
                                .index =
                                    reader_next_u8(reader, "dstore index")};
  case astore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_astore,
                                .index =
                                    reader_next_u8(reader, "astore index")};
  case istore_0:
  case istore_1:
  case istore_2:
  case istore_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_istore,
                                .index = opcode - istore_0};
  case lstore_0:
  case lstore_1:
  case lstore_2:
  case lstore_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lstore,
                                .index = opcode - lstore_0};
  case fstore_0:
  case fstore_1:
  case fstore_2:
  case fstore_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fstore,
                                .index = opcode - fstore_0};
  case dstore_0:
  case dstore_1:
  case dstore_2:
  case dstore_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dstore,
                                .index = opcode - dstore_0};
  case astore_0:
  case astore_1:
  case astore_2:
  case astore_3:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_astore,
                                .index = opcode - astore_0};
  case iastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iastore};
  case lastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lastore};
  case fastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fastore};
  case dastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dastore};
  case aastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_aastore};
  case bastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_bastore};
  case castore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_castore};
  case sastore:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_sastore};
  case pop:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_pop};
  case pop2:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_pop2};
  case dup:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup};
  case dup_x1:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup_x1};
  case dup_x2:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup_x2};
  case dup2:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup2};
  case dup2_x1:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup2_x1};
  case dup2_x2:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dup2_x2};
  case swap:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_swap};
  case iadd:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iadd};
  case ladd:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ladd};
  case fadd:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fadd};
  case dadd:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dadd};
  case isub:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_isub};
  case lsub:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lsub};
  case fsub:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fsub};
  case dsub:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dsub};
  case imul:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_imul};
  case lmul:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lmul};
  case fmul:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fmul};
  case dmul:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dmul};
  case idiv:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_idiv};
  case ldiv:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ldiv};
  case fdiv:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fdiv};
  case ddiv:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ddiv};
  case irem:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_irem};
  case lrem:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lrem};
  case frem:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_frem};
  case drem:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_drem};
  case ineg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ineg};
  case lneg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lneg};
  case fneg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fneg};
  case dneg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dneg};
  case ishl:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ishl};
  case lshl:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lshl};
  case ishr:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ishr};
  case lshr:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lshr};
  case iushr:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iushr};
  case lushr:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lushr};
  case iand:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iand};
  case land:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_land};
  case ior:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ior};
  case lor:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lor};
  case ixor:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ixor};
  case lxor:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lxor};
  case iinc: {
    uint16_t index = reader_next_u8(reader, "iinc index");
    int16_t const_ = (int16_t)reader_next_i8(reader, "iinc const");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_iinc,
                                .iinc = {index, const_}};
  }
  case i2l:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2l};
  case i2f:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2f};
  case i2d:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2d};
  case l2i:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_l2i};
  case l2f:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_l2f};
  case l2d:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_l2d};
  case f2i:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_f2i};
  case f2l:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_f2l};
  case f2d:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_f2d};
  case d2i:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_d2i};
  case d2l:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_d2l};
  case d2f:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_d2f};
  case i2b:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2b};
  case i2c:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2c};
  case i2s:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_i2s};
  case lcmp:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lcmp};
  case fcmpl:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fcmpl};
  case fcmpg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_fcmpg};
  case dcmpl:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dcmpl};
  case dcmpg:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dcmpg};

  case ifeq:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifeq,
        .index = checked_pc(pc, reader_next_i16(reader, "if_eq offset"), ctx)};
  case ifne:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifne,
        .index = checked_pc(pc, reader_next_i16(reader, "if_ne offset"), ctx)};
  case iflt:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_iflt,
        .index = checked_pc(pc, reader_next_i16(reader, "if_lt offset"), ctx)};
  case ifge:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifge,
        .index = checked_pc(pc, reader_next_i16(reader, "if_ge offset"), ctx)};
  case ifgt:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifgt,
        .index = checked_pc(pc, reader_next_i16(reader, "if_gt offset"), ctx)};
  case ifle:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifle,
        .index = checked_pc(pc, reader_next_i16(reader, "if_le offset"), ctx)};

  case if_icmpeq:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmpeq,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmpeq offset"), ctx)};
  case if_icmpne:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmpne,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmpne offset"), ctx)};
  case if_icmplt:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmplt,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmplt offset"), ctx)};
  case if_icmpge:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmpge,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmpge offset"), ctx)};
  case if_icmpgt:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmpgt,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmpgt offset"), ctx)};
  case if_icmple:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_icmple,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_icmple offset"), ctx)};
  case if_acmpeq:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_acmpeq,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_acmpeq offset"), ctx)};
  case if_acmpne:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_if_acmpne,
        .index =
            checked_pc(pc, reader_next_i16(reader, "if_acmpne offset"), ctx)};

  case goto_:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_goto,
        .index = checked_pc(pc, reader_next_i16(reader, "goto offset"), ctx)};
  case jsr:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_jsr,
        .index = checked_pc(pc, reader_next_i16(reader, "jsr offset"), ctx)};
  case ret:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ret,
                                .index = reader_next_u8(reader, "ret index")};
  case tableswitch: {
    return parse_tableswitch_insn(reader, pc, ctx);
  }
  case lookupswitch: {
    return parse_lookupswitch_insn(reader, pc, ctx);
  }
  case ireturn:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_ireturn};
  case lreturn:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_lreturn};
  case freturn:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_freturn};
  case dreturn:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_dreturn};
  case areturn:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_areturn};
  case return_:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_return};

  case getstatic:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_getstatic,
        .cp = checked_cp_entry(ctx->cp,
                               reader_next_u16(reader, "getstatic index"),
                               BJVM_CP_KIND_FIELD_REF, "getstatic field ref")};
  case putstatic:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_putstatic,
        .cp = checked_cp_entry(ctx->cp,
                               reader_next_u16(reader, "putstatic index"),
                               BJVM_CP_KIND_FIELD_REF, "putstatic field ref")};

  case getfield:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_getfield,
        .cp =
            checked_cp_entry(ctx->cp, reader_next_u16(reader, "getfield index"),
                             BJVM_CP_KIND_FIELD_REF, "getfield field ref")};
  case putfield:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_putfield,
        .cp =
            checked_cp_entry(ctx->cp, reader_next_u16(reader, "putfield index"),
                             BJVM_CP_KIND_FIELD_REF, "putfield field ref")};

  case invokevirtual:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_invokevirtual,
        .cp = checked_cp_entry(
            ctx->cp, reader_next_u16(reader, "invokevirtual index"),
            BJVM_CP_KIND_METHOD_REF | BJVM_CP_KIND_INTERFACE_METHOD_REF,
            "invokevirtual method ref")};
  case invokespecial:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_invokespecial,
        .cp = checked_cp_entry(
            ctx->cp, reader_next_u16(reader, "invokespecial index"),
            BJVM_CP_KIND_METHOD_REF | BJVM_CP_KIND_INTERFACE_METHOD_REF,
            "invokespecial method ref")};
  case invokestatic:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_invokestatic,
        .cp = checked_cp_entry(
            ctx->cp, reader_next_u16(reader, "invokestatic index"),
            BJVM_CP_KIND_METHOD_REF | BJVM_CP_KIND_INTERFACE_METHOD_REF,
            "invokestatic method ref")};

  case invokeinterface: {
    uint16_t index = reader_next_u16(reader, "invokeinterface index");
    bjvm_cp_entry *entry =
        checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_INTERFACE_METHOD_REF,
                         "invokeinterface method ref");
    reader_next_u8(reader, "invokeinterface count");
    reader_next_u8(reader, "invokeinterface zero");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_invokeinterface, .cp = entry};
  }

  case invokedynamic: {
    uint16_t index = reader_next_u16(reader, "invokedynamic index");
    bjvm_cp_entry *entry = checked_cp_entry(
        ctx->cp, index, BJVM_CP_KIND_INVOKE_DYNAMIC, "indy method ref");
    reader_next_u16(reader, "invokedynamic zero");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_invokedynamic, .cp = entry};
  }

  case new_: {
    uint16_t index = reader_next_u16(reader, "new index");
    bjvm_cp_entry *entry =
        checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_CLASS, "new class");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_new, .cp = entry};
  }

  case newarray: {
    uint8_t atype = reader_next_u8(reader, "newarray type");
    bjvm_type_kind parsed = parse_atype(atype);
    return (bjvm_bytecode_insn){.kind = bjvm_insn_newarray,
                                .array_type = parsed};
  }

  case anewarray: {
    uint16_t index = reader_next_u16(reader, "anewarray index");
    bjvm_cp_entry *entry =
        checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_CLASS, "anewarray class");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_anewarray, .cp = entry};
  }

  case arraylength:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_arraylength};
  case athrow:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_athrow};
  case checkcast: {
    uint16_t index = reader_next_u16(reader, "checkcast index");
    bjvm_cp_entry *entry =
        checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_CLASS, "checkcast class");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_checkcast, .cp = entry};
  }

  case instanceof: {
    uint16_t index = reader_next_u16(reader, "instanceof index");
    bjvm_cp_entry *entry = checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_CLASS,
                                            "instanceof class");
    return (bjvm_bytecode_insn){.kind = bjvm_insn_instanceof, .cp = entry};
  }

  case monitorenter:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_monitorenter};
  case monitorexit:
    return (bjvm_bytecode_insn){.kind = bjvm_insn_monitorexit};

  case wide: {
    switch (reader_next_u8(reader, "widened opcode")) {
    case iload: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_iload,
          .index = reader_next_u16(reader, "wide iload index")};
    }
    case lload: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_lload,
          .index = reader_next_u16(reader, "wide lload index")};
    }
    case fload: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_fload,
          .index = reader_next_u16(reader, "wide fload index")};
    }
    case dload: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_dload,
          .index = reader_next_u16(reader, "wide dload index")};
    }
    case aload: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_aload,
          .index = reader_next_u16(reader, "wide aload index")};
    }
    case istore: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_istore,
          .index = reader_next_u16(reader, "wide istore index")};
    }
    case lstore: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_lstore,
          .index = reader_next_u16(reader, "wide lstore index")};
    }
    case fstore: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_fstore,
          .index = reader_next_u16(reader, "wide fstore index")};
    }
    case dstore: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_dstore,
          .index = reader_next_u16(reader, "wide dstore index")};
    }
    case astore: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_astore,
          .index = reader_next_u16(reader, "wide astore index")};
    }
    case iinc: {
      uint16_t index = reader_next_u16(reader, "wide iinc index");
      int16_t const_ = reader_next_i16(reader, "wide iinc const");
      return (bjvm_bytecode_insn){.kind = bjvm_insn_iinc,
                                  .iinc = {index, const_}};
    }
    case ret: {
      return (bjvm_bytecode_insn){
          .kind = bjvm_insn_ret,
          .index = reader_next_u16(reader, "wide ret index")};
    }

    default: {
      char buf[64];
      snprintf(buf, sizeof(buf), "invalid wide opcode %d", opcode);
      format_error_dynamic(strdup(buf));
    }
    }
  }

  case multianewarray: {
    uint16_t index = reader_next_u16(reader, "multianewarray index");
    uint8_t dimensions = reader_next_u8(reader, "multianewarray dimensions");
    bjvm_cp_class_info *entry =
        &checked_cp_entry(ctx->cp, index, BJVM_CP_KIND_CLASS,
                          "multianewarray class")
             ->class_info;
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_multianewarray,
        .multianewarray = {.entry = entry, .dimensions = dimensions}};
  }

  case ifnull:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifnull,
        .index = checked_pc(pc, reader_next_i16(reader, "ifnull offset"), ctx)};
  case ifnonnull:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_ifnonnull,
        .index =
            checked_pc(pc, reader_next_i16(reader, "ifnonnull offset"), ctx)};
  case goto_w:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_goto,
        .index = checked_pc(pc, reader_next_i32(reader, "goto_w offset"), ctx)};
  case jsr_w:
    return (bjvm_bytecode_insn){
        .kind = bjvm_insn_jsr,
        .index = checked_pc(pc, reader_next_i32(reader, "jsr_w offset"), ctx)};

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
bjvm_bytecode_insn parse_insn(cf_byteslice *reader, uint32_t pc,
                              bjvm_classfile_parse_ctx *ctx) {
  bjvm_bytecode_insn insn = parse_insn_impl(reader, pc, ctx);
  insn.original_pc = pc;
  return insn;
}

int convert_pc_to_insn(int pc, int *pc_to_insn, uint32_t max_pc) {
  assert(pc < (int)max_pc &&
         pc >= 0); // checked pc should have caught this earlier
  int insn = pc_to_insn[pc];
  if (insn == -1) {
    char buf[64];
    snprintf(buf, sizeof(buf), "invalid program counter %d", pc);
    format_error_dynamic(strdup(buf));
  }
  return insn;
}

void convert_pc_offsets_to_insn_offsets(bjvm_bytecode_insn *code,
                                        int insn_count, int *pc_to_insn,
                                        uint32_t max_pc) {
  for (int i = 0; i < insn_count; ++i) {
    bjvm_bytecode_insn *insn = &code[i];
    if (insn->kind == bjvm_insn_tableswitch) {
      insn->tableswitch.default_target = convert_pc_to_insn(
          insn->tableswitch.default_target, pc_to_insn, max_pc);
      int count = insn->tableswitch.high - insn->tableswitch.low + 1;
      for (int j = 0; j < count; ++j) {
        insn->tableswitch.targets[j] = convert_pc_to_insn(
            insn->tableswitch.targets[j], pc_to_insn, max_pc);
      }
    } else if (insn->kind == bjvm_insn_lookupswitch) {
      insn->lookupswitch.default_target = convert_pc_to_insn(
          insn->lookupswitch.default_target, pc_to_insn, max_pc);
      for (int j = 0; j < insn->lookupswitch.targets_count; ++j) {
        insn->lookupswitch.targets[j] = convert_pc_to_insn(
            insn->lookupswitch.targets[j], pc_to_insn, max_pc);
      }
    } else if (insn->kind >= bjvm_insn_goto && insn->kind <= bjvm_insn_ifnull) {
      // instruction uses index to store PC; convert to instruction
      insn->index = convert_pc_to_insn(insn->index, pc_to_insn,
                                       max_pc); // always decreases, so ok
    }
  }
}

void parse_bootstrap_methods_attribute(cf_byteslice attr_reader,
                                       bjvm_attribute *attr,
                                       bjvm_classfile_parse_ctx *ctx) {
  attr->kind = BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS;
  uint16_t count = attr->bootstrap_methods.count =
      reader_next_u16(&attr_reader, "bootstrap methods count");
  bjvm_bootstrap_method *methods = attr->bootstrap_methods.methods =
      arena_alloc(ctx->arena, count, sizeof(bjvm_bootstrap_method));

  for (int i = 0; i < count; ++i) {
    bjvm_bootstrap_method *method = methods + i;
    method->ref =
        &checked_cp_entry(ctx->cp,
                          reader_next_u16(&attr_reader, "bootstrap method ref"),
                          BJVM_CP_KIND_METHOD_HANDLE, "bootstrap method ref")
             ->method_handle;
    uint16_t arg_count =
        reader_next_u16(&attr_reader, "bootstrap method arg count");
    method->args_count = arg_count;
    method->args = arena_alloc(ctx->arena, arg_count, sizeof(bjvm_cp_entry *));
    for (int j = 0; j < arg_count; ++j) {
      const int allowed = BJVM_CP_KIND_STRING | BJVM_CP_KIND_INTEGER |
                          BJVM_CP_KIND_FLOAT | BJVM_CP_KIND_LONG |
                          BJVM_CP_KIND_DOUBLE | BJVM_CP_KIND_METHOD_HANDLE |
                          BJVM_CP_KIND_METHOD_TYPE | BJVM_CP_KIND_CLASS |
                          BJVM_CP_KIND_DYNAMIC_CONSTANT;
      method->args[j] = checked_cp_entry(
          ctx->cp, reader_next_u16(&attr_reader, "bootstrap method arg"),
          allowed, "bootstrap method arg");
    }
  }
}

void parse_attribute(cf_byteslice *reader, bjvm_classfile_parse_ctx *ctx,
                     bjvm_attribute *attr);

bjvm_attribute_code parse_code_attribute(cf_byteslice attr_reader,
                                         bjvm_classfile_parse_ctx *ctx) {
  uint16_t max_stack = reader_next_u16(&attr_reader, "max stack");
  uint16_t max_locals = reader_next_u16(&attr_reader, "max locals");
  uint32_t code_length = reader_next_u32(&attr_reader, "code length");

  const uint8_t *code_start = attr_reader.bytes;

  cf_byteslice code_reader =
      reader_get_slice(&attr_reader, code_length, "code");
  bjvm_bytecode_insn *code =
      arena_alloc(ctx->arena, code_length, sizeof(bjvm_bytecode_insn));

  ctx->current_code_max_pc = code_length;

  int *pc_to_insn =
      malloc(code_length *
             sizeof(int)); // -1 = no corresponding instruction to that PC
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

  uint16_t exception_table_length =
      reader_next_u16(&attr_reader, "exception table length");
  bjvm_attribute_exception_table *table = nullptr;
  if (exception_table_length) {
    table = arena_alloc(ctx->arena, 1, sizeof(bjvm_attribute_exception_table));
    table->entries =
        arena_alloc(ctx->arena, table->entries_count = exception_table_length,
                    sizeof(bjvm_exception_table_entry));

    for (int i = 0; i < exception_table_length; ++i) {
      bjvm_exception_table_entry *ent = table->entries + i;
      uint16_t start_pc = reader_next_u16(&attr_reader, "exception start pc");
      uint16_t end_pc = reader_next_u16(&attr_reader, "exception end pc");
      uint16_t handler_pc =
          reader_next_u16(&attr_reader, "exception handler pc");
      uint16_t catch_type =
          reader_next_u16(&attr_reader, "exception catch type");

      if (start_pc >= code_length || end_pc > code_length ||
          handler_pc >= code_length)
        format_error_static("exception table entry out of bounds");
      if (start_pc >= end_pc)
        format_error_static("exception table entry start >= end");

      ent->start_insn = convert_pc_to_insn(start_pc, pc_to_insn, code_length);
      ent->end_insn = end_pc == code_length
                          ? code_length
                          : convert_pc_to_insn(end_pc, pc_to_insn, code_length);
      ent->handler_insn =
          convert_pc_to_insn(handler_pc, pc_to_insn, code_length);
      ent->catch_type =
          catch_type == 0
              ? nullptr
              : &checked_cp_entry(ctx->cp, catch_type, BJVM_CP_KIND_CLASS,
                                  "exception catch type")
                     ->class_info;
    }
  }

  free(pc_to_insn);
  ctx->temp_allocation = nullptr;

  uint16_t attributes_count =
      reader_next_u16(&attr_reader, "code attributes count");
  bjvm_attribute *attributes =
      arena_alloc(ctx->arena, attributes_count, sizeof(bjvm_attribute));

  bjvm_attribute_line_number_table *lnt = NULL;
  bjvm_attribute_local_variable_table *lvt = NULL;
  for (int i = 0; i < attributes_count; ++i) {
    bjvm_attribute *attr = attributes + i;
    parse_attribute(&attr_reader, ctx, attr);
    if (attr->kind == BJVM_ATTRIBUTE_KIND_LINE_NUMBER_TABLE) {
      lnt = &attr->lnt;
    } else if (attr->kind == BJVM_ATTRIBUTE_KIND_LOCAL_VARIABLE_TABLE) {
      lvt = &attr->lvt;
    }
  }

  return (bjvm_attribute_code){.max_stack = max_stack,
                               .max_locals = max_locals,
                               .insn_count = insn_count,
                               .max_formal_pc = ctx->current_code_max_pc,
                               .code = code,
                               .attributes = attributes,
                               .exception_table = table,
                               .line_number_table = lnt,
                               .local_variable_table = lvt,
                               .attributes_count = attributes_count};
}

// 4.2.2. Unqualified Names
void check_unqualified_name(bjvm_utf8 name, bool is_method,
                            const char *reading) {
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
  for (int i = 0; i < name.len; ++i) {
    char c = name.chars[i];
    if (c == '.' || c == ';' || c == '[' || c == '/' ||
        (is_method && (c == '<' || c == '>'))) {
      char complaint[1024];
      snprintf(complaint, sizeof(complaint), "invalid %s name: '%.*s'", reading,
               fmt_slice(name));
      format_error_dynamic(strdup(complaint));
    }
  }
}

void parse_attribute(cf_byteslice *reader, bjvm_classfile_parse_ctx *ctx,
                     bjvm_attribute *attr) {
  uint16_t index = reader_next_u16(reader, "method attribute name");
  attr->name = checked_get_utf8(ctx->cp, index, "method attribute name");
  attr->length = reader_next_u32(reader, "method attribute length");

  cf_byteslice attr_reader =
      reader_get_slice(reader, attr->length, "Attribute data");
  if (utf8_equals(attr->name, "Code")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_CODE;
    attr->code = parse_code_attribute(attr_reader, ctx);
  } else if (utf8_equals(attr->name, "ConstantValue")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE;
    attr->constant_value = checked_cp_entry(
        ctx->cp, reader_next_u16(&attr_reader, "constant value index"),
        BJVM_CP_KIND_STRING | BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_FLOAT |
            BJVM_CP_KIND_LONG | BJVM_CP_KIND_DOUBLE,
        "constant value");
  } else if (utf8_equals(attr->name, "BootstrapMethods")) {
    parse_bootstrap_methods_attribute(attr_reader, attr, ctx);
  } else if (utf8_equals(attr->name, "EnclosingMethod")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_ENCLOSING_METHOD;
    uint16_t enclosing_class_index =
        reader_next_u16(&attr_reader, "enclosing class index");
    uint16_t enclosing_method_index =
        reader_next_u16(&attr_reader, "enclosing method index");
    attr->enclosing_method = (bjvm_attribute_enclosing_method){
        enclosing_class_index
            ? &checked_cp_entry(ctx->cp, enclosing_class_index,
                                BJVM_CP_KIND_CLASS, "enclosing class")
                   ->class_info
            : nullptr,
        enclosing_method_index
            ? &checked_cp_entry(ctx->cp, enclosing_method_index,
                                BJVM_CP_KIND_NAME_AND_TYPE, "enclosing method")
                   ->name_and_type
            : nullptr};
  } else if (utf8_equals(attr->name, "SourceFile")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_SOURCE_FILE;
    attr->source_file.name =
        checked_cp_entry(ctx->cp,
                         reader_next_u16(&attr_reader, "source file index"),
                         BJVM_CP_KIND_UTF8, "source file")
            ->utf8;
  } else if (utf8_equals(attr->name, "LineNumberTable")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_LINE_NUMBER_TABLE;
    uint16_t count = attr->lnt.entry_count =
        reader_next_u16(&attr_reader, "line number table count");
    attr->lnt.entries =
        arena_alloc(ctx->arena, count, sizeof(bjvm_line_number_table_entry));
    for (int i = 0; i < count; ++i) {
      bjvm_line_number_table_entry *entry = attr->lnt.entries + i;
      entry->start_pc = reader_next_u16(&attr_reader, "line number start pc");
      entry->line = reader_next_u16(&attr_reader, "line number");
    }
  } else if (utf8_equals(attr->name, "MethodParameters")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_METHOD_PARAMETERS;
    int count = attr->method_parameters.count =
        reader_next_u8(&attr_reader, "method parameters count");
    bjvm_method_parameter_info *params = attr->method_parameters.params =
        arena_alloc(ctx->arena, count, sizeof(bjvm_method_parameter_info));

    for (int i = 0; i < count; ++i) {
      uint16_t name_index =
          reader_next_u16(&attr_reader, "method parameter name");
      // "If the value of the name_index item is zero, then this parameters
      // element indicates a formal parameter with no name"
      if (name_index) {
        bjvm_utf8 param_name =
            checked_get_utf8(ctx->cp, name_index, "method parameter name");
        check_unqualified_name(param_name, false, "method parameter name");
        params[i].name = param_name;
      } else {
        params[i].name = null_str();
      }

      params[i].access_flags =
          reader_next_u16(&attr_reader, "method parameter access flags");
    }
  } else if (utf8_equals(attr->name, "RuntimeVisibleAnnotations")) {
#define BYTE_ARRAY_ANNOTATION(attr_kind, union_member)                         \
  attr->kind = attr_kind;                                                      \
  uint8_t *data = attr->annotations.data =                                     \
      arena_alloc(ctx->arena, attr_reader.len, sizeof(uint8_t));               \
  memcpy(data, attr_reader.bytes, attr_reader.len);                            \
  attr->annotations.length = attr_reader.len;

    BYTE_ARRAY_ANNOTATION(BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_ANNOTATIONS,
                          annotations);
  } else if (utf8_equals(attr->name, "RuntimeVisibleParameterAnnotations")) {
    BYTE_ARRAY_ANNOTATION(
        BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_PARAMETER_ANNOTATIONS,
        parameter_annotations);
  } else if (utf8_equals(attr->name, "RuntimeVisibleTypeAnnotations")) {
    BYTE_ARRAY_ANNOTATION(BJVM_ATTRIBUTE_KIND_RUNTIME_VISIBLE_TYPE_ANNOTATIONS,
                          type_annotations);
  } else if (utf8_equals(attr->name, "AnnotationDefault")) {
    BYTE_ARRAY_ANNOTATION(BJVM_ATTRIBUTE_KIND_ANNOTATION_DEFAULT,
                          annotation_default);
  } else if (utf8_equals(attr->name, "Signature")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_SIGNATURE;
    attr->signature.utf8 = checked_get_utf8(
        ctx->cp, reader_next_u16(&attr_reader, "Signature index"),
        "Signature index");
  } else if (utf8_equals(attr->name, "NestHost")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_NEST_HOST;
    attr->nest_host = &checked_cp_entry(
            ctx->cp, reader_next_u16(&attr_reader, "NestHost index"),
            BJVM_CP_KIND_CLASS, "NestHost index")->class_info;
  } else if (utf8_equals(attr->name, "LocalVariableTable")) {
    attr->kind = BJVM_ATTRIBUTE_KIND_LOCAL_VARIABLE_TABLE;
    uint16_t count = attr->lvt.entries_count =
          reader_next_u16(&attr_reader, "local variable table count");
    attr->lvt.entries =
        arena_alloc(ctx->arena, count, sizeof(bjvm_attribute_lvt_entry));
    for (int i = 0; i < count; ++i) {
      bjvm_attribute_lvt_entry *entry =
          &attr->lvt.entries[i];
      entry->start_pc = reader_next_u16(&attr_reader, "local variable start pc");
      entry->end_pc = entry->start_pc + reader_next_u16(&attr_reader, "local variable length");
      entry->name = checked_cp_entry(ctx->cp,
                                     reader_next_u16(&attr_reader, "local variable name"),
                                     BJVM_CP_KIND_UTF8, "local variable name")->utf8;
      entry->descriptor = checked_cp_entry(ctx->cp,
                                           reader_next_u16(&attr_reader, "local variable descriptor"),
                                           BJVM_CP_KIND_UTF8, "local variable descriptor")->utf8;
      entry->index = reader_next_u16(&attr_reader, "local variable index");
    }
  } else {
    attr->kind = BJVM_ATTRIBUTE_KIND_UNKNOWN;
  }
}

/**
 * Parse a method in a classfile.
 */
bjvm_cp_method parse_method(cf_byteslice *reader,
                            bjvm_classfile_parse_ctx *ctx) {
  bjvm_cp_method method = {0};
  method.access_flags = reader_next_u16(reader, "method access flags");
  method.name = checked_get_utf8(
      ctx->cp, reader_next_u16(reader, "method name"), "method name");
  method.unparsed_descriptor =
      checked_get_utf8(ctx->cp, reader_next_u16(reader, "method descriptor"),
                       "method descriptor");
  method.attributes_count = reader_next_u16(reader, "method attributes count");
  method.attributes =
      arena_alloc(ctx->arena, method.attributes_count, sizeof(bjvm_attribute));
  method.descriptor =
      arena_alloc(ctx->arena, 1, sizeof(bjvm_method_descriptor));
  char *error = parse_method_descriptor(method.unparsed_descriptor,
                                        method.descriptor, ctx->arena);
  if (error) {
    format_error_dynamic(error);
  }

  for (int i = 0; i < method.attributes_count; i++) {
    bjvm_attribute *attrib = &method.attributes[i];
    parse_attribute(reader, ctx, attrib);
    if (attrib->kind == BJVM_ATTRIBUTE_KIND_CODE) {
      method.code = &attrib->code;
    }
  }

  return method;
}

bjvm_cp_field read_field(cf_byteslice *reader, bjvm_classfile_parse_ctx *ctx) {
  bjvm_cp_field field = {
      .access_flags = reader_next_u16(reader, "field access flags"),
      .name = checked_get_utf8(ctx->cp, reader_next_u16(reader, "field name"),
                               "field name"),
      .descriptor =
          checked_get_utf8(ctx->cp, reader_next_u16(reader, "field descriptor"),
                           "field descriptor"),
      .attributes_count = reader_next_u16(reader, "field attributes count")};
  field.attributes =
      arena_alloc(ctx->arena, field.attributes_count, sizeof(bjvm_attribute));

  for (int i = 0; i < field.attributes_count; i++) {
    parse_attribute(reader, ctx, field.attributes + i);
  }

  char *error = parse_complete_field_descriptor(field.descriptor,
                                                &field.parsed_descriptor, ctx);
  if (error)
    format_error_dynamic(error);

  return field;
}

/**
 * Parse the field descriptor in the string slice starting at **chars, with
 * length len, writing the result to result and returning an owned error message
 * if there was an error.
 */
char *parse_field_descriptor(const char **chars, size_t len,
                             bjvm_field_descriptor *result, arena *arena) {
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
      result->base_kind = (bjvm_type_kind)c;
      if (c == 'V' && dimensions > 0)
        return strdup("void cannot have dimensions");
      return nullptr;
    case '[':
      ++dimensions;
      break;
    case 'L': {
      const char *start = *chars;
      while (*chars < end && **chars != ';')
        ++*chars;
      if (*chars == end)
        return strdup("missing ';' in reference type");
      int class_name_len = *chars - start;
      if (class_name_len == 0) {
        return strdup("missing reference type name");
      }
      ++*chars;
      result->base_kind = BJVM_TYPE_KIND_REFERENCE;
      result->class_name = arena_make_str(arena, start, class_name_len);
      return nullptr;
    }
    default: {
      char buf[64];
      snprintf(buf, sizeof(buf), "invalid field descriptor character '%c'",
               *(*chars - 1));
      return strdup(buf);
    }
    }
  }

  return strdup(
      "Expected field descriptor character, but reached end of string");
}

char *parse_method_descriptor(const bjvm_utf8 entry,
                              bjvm_method_descriptor *result, arena *arena) {
  // MethodDescriptor:
  // ( { ParameterDescriptor } )
  // ParameterDescriptor:
  // FieldType
  const size_t len = entry.len;
  const char *chars = entry.chars, *end = chars + len;
  if (len < 1 || *chars++ != '(')
    return strdup("Expected '('");
  result->args_cap = result->args_count = 0;
  bjvm_field_descriptor *fields = nullptr;
  while (chars < end && *chars != ')') {
    bjvm_field_descriptor arg;

    char *error = parse_field_descriptor(&chars, end - chars, &arg, arena);
    if (error || arg.base_kind == BJVM_TYPE_KIND_VOID) {
      free(fields);
      return error ? error : strdup("void as method parameter");
    }

    *VECTOR_PUSH(fields, result->args_count, result->args_cap) = arg;
  }
  result->args =
      arena_alloc(arena, result->args_count, sizeof(bjvm_field_descriptor));
  if (result->args_count) {
    memcpy(result->args, fields,
           result->args_count * sizeof(bjvm_field_descriptor));
  }
  free(fields);
  if (chars >= end) {
    return strdup("missing ')' in method descriptor");
  }
  chars++; // skip ')'
  char *error =
      parse_field_descriptor(&chars, end - chars, &result->return_type, arena);
  return error;
}

// Go through the InvokeDynamic entries and link their bootstrap method pointers
void link_bootstrap_methods(bjvm_classdesc *cf) {
  bjvm_constant_pool *cp = cf->pool;
  for (int i = 1; i < cp->entries_len; i++) {
    if (cp->entries[i].kind == BJVM_CP_KIND_INVOKE_DYNAMIC) {
      bjvm_cp_indy_info *indy = &cp->entries[i].indy_info;
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

parse_result_t bjvm_parse_classfile(const uint8_t *bytes, size_t len,
                                    bjvm_classdesc *result,
                                    heap_string *error) {
  cf_byteslice reader = {.bytes = bytes, .len = len};
  bjvm_classdesc *cf = result;
  arena_init(&cf->arena);
  bjvm_classfile_parse_ctx ctx = {.arena = &cf->arena, .cp = nullptr};

  if (setjmp(format_error_jmp_buf)) {
    arena_uninit(&cf->arena); // clean up our shit
    free(ctx.temp_allocation);
    if (error) {
      *error = make_heap_str_from((bjvm_utf8){.chars = format_error_msg,
                                              .len = strlen(format_error_msg)});
    }
    if (format_error_needs_free)
      free(format_error_msg);
    result->state = BJVM_CD_STATE_LINKAGE_ERROR;
    return PARSE_ERR;
  }

  const uint32_t magic = reader_next_u32(&reader, "magic");
  if (magic != 0xCAFEBABE) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Invalid magic number 0x%08x", magic);
    format_error_dynamic(strdup(buf));
  }

  uint16_t minor = reader_next_u16(&reader, "minor version");
  uint16_t major = reader_next_u16(&reader, "major version");

  // TODO check these
  (void)minor;
  (void)major;

  cf->pool = parse_constant_pool(&reader, &ctx);
  cf->access_flags = reader_next_u16(&reader, "access flags");
  // "Compilers to the instruction set of the Java Virtual Machine should se
  // the ACC_SUPER flag. In Java SE 8 and above, the Java Virtual Machine
  // considers the ACC_SUPER flag to be set in every class file, regardless of
  // the actual value of the flag in the class file and the version of the
  // class file."
  // -> getModifiers doesn't return this bit
  cf->access_flags &= ~0x0020;

  bjvm_cp_class_info *this_class =
      &checked_cp_entry(cf->pool, reader_next_u16(&reader, "this class"),
                        BJVM_CP_KIND_CLASS, "this class")
           ->class_info;
  cf->name = (heap_string){.chars = this_class->name.chars,
                           .len = this_class->name.len}; // TODO unjank

  bool is_primordial_object = utf8_equals(hslc(cf->name), "java/lang/Object");

  uint16_t super_class = reader_next_u16(&reader, "super class");
  cf->super_class = ((cf->access_flags & BJVM_ACCESS_MODULE) | is_primordial_object)
                        ? nullptr
                        : &checked_cp_entry(cf->pool, super_class,
                                            BJVM_CP_KIND_CLASS, "super class")
                               ->class_info;

  // Parse superinterfaces
  cf->interfaces_count = reader_next_u16(&reader, "interfaces count");
  cf->interfaces = arena_alloc(ctx.arena, cf->interfaces_count,
                               sizeof(bjvm_cp_class_info *));

  for (int i = 0; i < cf->interfaces_count; i++) {
    cf->interfaces[i] =
        &checked_cp_entry(cf->pool, reader_next_u16(&reader, "interface"),
                          BJVM_CP_KIND_CLASS, "superinterface")
             ->class_info;
  }

  // Parse fields
  cf->fields_count = reader_next_u16(&reader, "fields count");
  cf->fields = arena_alloc(ctx.arena, cf->fields_count, sizeof(bjvm_cp_field));
  for (int i = 0; i < cf->fields_count; i++) {
    cf->fields[i] = read_field(&reader, &ctx);
    cf->fields[i].my_class = result;
  }
  cf->static_fields = nullptr;
  cf->static_references = bjvm_empty_bitset();
  cf->instance_references = bjvm_empty_bitset();

  // Parse methods
  cf->methods_count = reader_next_u16(&reader, "methods count");
  cf->methods =
      arena_alloc(ctx.arena, cf->methods_count, sizeof(bjvm_cp_method));

  cf->bootstrap_methods = nullptr;
  cf->indy_insns = nullptr;

  bool in_MethodHandle =
      utf8_equals(hslc(cf->name), "java/lang/invoke/MethodHandle");
  for (int i = 0; i < cf->methods_count; ++i) {
    bjvm_cp_method *method = cf->methods + i;
    *method = parse_method(&reader, &ctx);
    method->my_class = result;

    // Mark signature polymorphic functions
    if (in_MethodHandle && method->access_flags & BJVM_ACCESS_NATIVE) {
      method->is_signature_polymorphic = true;
    }
  }

  // Parse attributes
  cf->attributes_count = reader_next_u16(&reader, "class attributes count");
  cf->attributes =
      arena_alloc(ctx.arena, cf->attributes_count, sizeof(bjvm_attribute));
  for (int i = 0; i < cf->attributes_count; i++) {
    bjvm_attribute *attr = cf->attributes + i;
    parse_attribute(&reader, &ctx, attr);

    if (attr->kind == BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS) {
      cf->bootstrap_methods = &attr->bootstrap_methods;
    } else if (attr->kind == BJVM_ATTRIBUTE_KIND_SOURCE_FILE) {
      cf->source_file = &attr->source_file;
    } else if (attr->kind ==BJVM_ATTRIBUTE_KIND_NEST_HOST) {
      cf->nest_host = attr->nest_host;
    }
  }

  // Check for trailing bytes
  if (reader.len > 0) {
    format_error_static("Trailing bytes in classfile");
  }

  link_bootstrap_methods(cf);
  result->state = BJVM_CD_STATE_LOADED;

  // Add indy instruction pointers
  for (int i = 0; i < cf->methods_count; ++i) {
    bjvm_cp_method *method = cf->methods + i;
    if (method->code) {
      // Add pointers to indy instructions for their CallSites to be GC roots
      for (int j = 0; j < method->code->insn_count; ++j) {
        bjvm_bytecode_insn *insn = method->code->code + j;
        if (insn->kind == bjvm_insn_invokedynamic) {
          arrput(cf->indy_insns, insn);
        }
      }
    }
  }

  return PARSE_SUCCESS;
}
