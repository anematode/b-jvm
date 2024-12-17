//
// Created by alec on 12/18/24.
//

#include <stdlib.h>
#include <setjmp.h>
#include <assert.h>

// SIMD support
#ifdef __ARM_NEON__
#include <arm_neon.h>
#elif defined(__SSE__)
#include <immintrin.h>
#elif defined(EMSCRIPTEN)
#include <wasm_simd128.h>
#endif

#include "classfile.h"
#include "bjvm.h"
#include "util.h"

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

// Create a null-terminated UTF-8 entry of the given length
bjvm_utf8 init_utf8_entry(int len) {
  return (bjvm_utf8){.chars = calloc(len + 1, sizeof(wchar_t)), .len = len};
}

void free_utf8(bjvm_utf8 entry) { free(entry.chars); }

void free_method_descriptor(void *descriptor_);

void free_constant_pool_entry(bjvm_cp_entry *entry) {
  switch (entry->kind) {
  case BJVM_CP_KIND_UTF8:
    free_utf8(entry->utf8);
    break;
  case BJVM_CP_KIND_FIELD_REF: {
    bjvm_field_descriptor *desc = entry->fieldref_info.parsed_descriptor;
    free_field_descriptor(*desc);
    free(desc);
    break;
  }
  case BJVM_CP_KIND_INVOKE_DYNAMIC: {
    free_method_descriptor(entry->indy_info.method_descriptor);
    break;
  }
  case BJVM_CP_KIND_METHOD_REF:
  case BJVM_CP_KIND_INTERFACE_METHOD_REF: {
    free_method_descriptor(entry->methodref.method_descriptor);
    break;
  }
  default: // TODO will need to add more as we resolve descriptors
    break;
  }
}

void free_method(bjvm_cp_method *method);
void free_field(bjvm_cp_field *field);

void bjvm_free_constant_pool(bjvm_constant_pool *pool) {
  if (!pool)
    return;
  for (int i = 0; i < pool->entries_len; ++i)
    free_constant_pool_entry(&pool->entries[i]);
  free(pool);
}

void free_bytecode_instruction(bjvm_bytecode_insn insn) {
  switch (insn.kind) {
  case bjvm_insn_tableswitch:
    free(insn.tableswitch.targets);
    break;
  case bjvm_insn_lookupswitch:
    free(insn.lookupswitch.keys);
    free(insn.lookupswitch.targets);
    break;
  default:
    break;
  }
}

void bjvm_free_attribute(bjvm_attribute *attribute);

void bjvm_free_code_attribute(bjvm_attribute_code *code) {
  for (int i = 0; i < code->insn_count; ++i) {
    free_bytecode_instruction(code->code[i]);
  }
  if (code->exception_table) {
    free(code->exception_table->entries);
    free(code->exception_table);
  }
  for (int i = 0; i < code->attributes_count; ++i) {
    bjvm_free_attribute(code->attributes + i);
  }
  free(code->attributes);
  free(code->code);
}

void bjvm_free_bootstrap_methods_attribute(bjvm_attribute_bootstrap_methods *bm) {
  for (int i = 0; i < bm->count; ++i) {
    free(bm->methods[i].args);
  }
  free(bm->methods);
}

void bjvm_free_attribute(bjvm_attribute *attribute) {
  switch (attribute->kind) {
  case BJVM_ATTRIBUTE_KIND_CODE:
    bjvm_free_code_attribute(&attribute->code);
    break;
  case BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS:
    bjvm_free_bootstrap_methods_attribute(&attribute->bootstrap_methods);
    break;
  case BJVM_ATTRIBUTE_KIND_CONSTANT_VALUE:
  case BJVM_ATTRIBUTE_KIND_UNKNOWN:
    break;
  }
}

void bjvm_free_classfile(bjvm_classdesc cf) {
  bjvm_free_constant_pool(cf.pool);
  free(cf.interfaces);
  for (int i = 0; i < cf.attributes_count; ++i)
    bjvm_free_attribute(&cf.attributes[i]);
  for (int i = 0; i < cf.methods_count; ++i)
    free_method(&cf.methods[i]);
  for (int i = 0; i < cf.fields_count; ++i)
    free_field(&cf.fields[i]);
  free(cf.static_fields);
  free(cf.fields);
  free(cf.methods);
  free(cf.attributes);
  bjvm_free_compressed_bitset(cf.static_references);
  bjvm_free_compressed_bitset(cf.instance_references);
}

bjvm_cp_entry *get_constant_pool_entry(bjvm_constant_pool *pool, int index) {
  assert(index >= 0 && index < pool->entries_len);
  return &pool->entries[index];
}

void free_field(bjvm_cp_field *field) {
  for (int i = 0; i < field->attributes_count; ++i)
    bjvm_free_attribute(&field->attributes[i]);
  free_field_descriptor(field->parsed_descriptor);
  free(field->attributes);
}

void free_method_descriptor(void *descriptor_) {
  bjvm_method_descriptor *descriptor = descriptor_;
  for (int i = 0; i < descriptor->args_count; ++i)
    free_field_descriptor(descriptor->args[i]);
  free_field_descriptor(descriptor->return_type);
  free(descriptor->args);
  free(descriptor);
}

void free_code_analysis(bjvm_code_analysis *code_analysis) {
  if (!code_analysis)
    return;
  if (code_analysis->insn_index_to_references) {
    printf("%p\n", code_analysis->insn_index_to_references);
    fflush(stdout);
    for (int i = 0; i < code_analysis->insn_count; ++i)
      bjvm_free_compressed_bitset(code_analysis->insn_index_to_references[i]);
    free(code_analysis->insn_index_to_references);
  }
  free(code_analysis);
}

void free_method(bjvm_cp_method *method) {
  for (int i = 0; i < method->attributes_count; ++i)
    bjvm_free_attribute(&method->attributes[i]);
  free_code_analysis(method->code_analysis);
  free_method_descriptor(method->parsed_descriptor);
  free(method->attributes);
}

typedef struct {
  uint8_t *bytes;
  size_t len;
} cf_byteslice;

// jmp_buf for format error
_Thread_local jmp_buf format_error_jmp_buf;
_Thread_local char *format_error_msg = NULL;
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
    if (reader->len < sizeof(type)) {                                          \
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
  if (reader->len < len) {
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
  // Free these pointers if a format error happens, to avoid memory leaks. Pairs
  // of (void*, free_fn)
  void **free_on_error;
  int free_on_error_cap;
  int free_on_error_count;

  int current_code_max_pc;

  bjvm_constant_pool *cp;
} bjvm_classfile_parse_ctx;

/**
 * Used to unmark an otherwise to-be-freed pointer in the format-checking
 * context.
 */
typedef struct {
  bjvm_classfile_parse_ctx *ctx;
  size_t offset;
} ctx_free_ticket;

void free_ticket(ctx_free_ticket ticket) {
  ticket.ctx->free_on_error[ticket.offset] = NULL;
}

#define PUSH_FREE                                                              \
  *VECTOR_PUSH(ctx->free_on_error, ctx->free_on_error_count,                   \
               ctx->free_on_error_cap)

/**
 * Record that this pointer needs to be freed if we encounter a VerifyError
 * while parsing the classfile.
 */
ctx_free_ticket free_on_format_error(bjvm_classfile_parse_ctx *ctx, void *ptr) {
  if (!ctx)
    return (ctx_free_ticket){};
  PUSH_FREE = ptr;
  PUSH_FREE = free;
  return (ctx_free_ticket){.ctx = ctx, .offset = ctx->free_on_error_count - 2};
}

ctx_free_ticket complex_free_on_format_error(bjvm_classfile_parse_ctx *ctx,
                                             void *ptr,
                                             void (*free_fn)(void *)) {
  if (!ctx)
    return (ctx_free_ticket){};
  PUSH_FREE = ptr;
  PUSH_FREE = free_fn;
  return (ctx_free_ticket){.ctx = ctx, .offset = ctx->free_on_error_count - 2};
}

#undef PUSH_FREE

// See: 4.4.7. The CONSTANT_Utf8_info Structure
bjvm_utf8 parse_modified_utf8(const uint8_t *bytes, int len) {
  bjvm_utf8 result = init_utf8_entry(len); // conservatively large
  int i = 0, j = 0;

  uint32_t idxs[16] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
  for (int i = 0; i < 16; ++i)
    idxs[i] -= 256;

  for (; i + 15 < len; i += 16, j += 16) {
    // fuck maintainability!!! (measured perf improvement: 12% for cf parsing on
    // my M1 laptop. There are a shit ton of strings.)
#ifdef __ARM_NEON__
    uint8x16_t v = vld1q_u8(bytes + i);
    uint8x16_t ascii = vcgtq_s8(v, vdupq_n_s8(0));
    if (vminvq_u8(ascii) == 0)
      break;
#pragma clang loop unroll(enable)
    for (int k = 0; k < 4; ++k) {
      uint8x16_t idx = vld1q_u8((uint8_t *)idxs + 16 * k);
      vst1q_u8((uint8_t *)&result.chars[j + 4 * k], vqtbl1q_u8(v, idx));
    }
#elif defined(__SSSE3__)
    __m128i v = _mm_loadu_si128((const __m128i *)(bytes + i));
    int ascii = _mm_movemask_epi8(_mm_cmpgt_epi8(v, _mm_set1_epi8(0)));
    if (ascii != 0xffff)
      break;
#pragma GCC unroll 4
    for (int k = 0; k < 4; ++k) {
      __m128i idx = _mm_loadu_si128((const __m128i *)idxs + k);
      _mm_storeu_si128((__m128i *)&result.chars[j + 4 * k],
                       _mm_shuffle_epi8(v, idx));
    }
#else
    break;
#endif
  }

  for (; i < len; ++i) {
    // "Code points in the range '\u0001' to '\u007F' are represented by a
    // single byte"
    if (bytes[i] >= 0x01 && bytes[i] <= 0x7F) {
      result.chars[j++] = bytes[i];
    } else if ((bytes[i] & 0xE0) == 0xC0) {
      // "Code points in the range '\u0080' to '\u07FF' are represented by two
      // bytes"
      if (i >= len - 1)
        goto inval;
      result.chars[j++] = ((bytes[i] & 0x1F) << 6) | (bytes[i + 1] & 0x3F);
      i++;
    } else if ((bytes[i] & 0xF0) == 0xE0) {
      // "Code points in the range '\u0800' to '\uFFFF' are represented by three
      // bytes"
      if (i >= len - 2)
        goto inval;
      result.chars[j++] = ((bytes[i] & 0x0F) << 12) |
                          ((bytes[i + 1] & 0x3F) << 6) | (bytes[i + 2] & 0x3F);
      i += 2;
    } else {
      // "No byte may have the value (byte)0 or lie in the range (byte)0xf0 -
      // (byte)0xff."
      goto inval;
    }
  }
  result.len = j;
  return result;
inval:
  free_utf8(result);
  format_error_static("Invalid UTF-8 sequence");
}

bjvm_utf8 bjvm_make_utf8_cstr(const char *c_literal) {
  return parse_modified_utf8((const uint8_t*) c_literal, strlen(c_literal));
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

bjvm_utf8 *checked_get_utf8(bjvm_constant_pool *pool, int index,
                            const char *reason) {
  return &checked_cp_entry(pool, index, BJVM_CP_KIND_UTF8, reason)->utf8;
}

bjvm_utf8 bjvm_wchar_slice_to_utf8(const wchar_t *chars, size_t len) {
  bjvm_utf8 init = init_utf8_entry(len);
  wmemcpy(init.chars, chars, len);
  return init;
}

char *parse_complete_field_descriptor(const bjvm_utf8 *entry,
                                      bjvm_field_descriptor *result,
                                      bjvm_classfile_parse_ctx *ctx) {
  const wchar_t *chars = entry->chars;
  char *error = parse_field_descriptor(&chars, entry->len, result);
  if (error)
    return error;
  if (result->kind == BJVM_TYPE_KIND_REFERENCE)
    free_on_format_error(ctx, result->class_name.chars);
  if (chars != entry->chars + entry->len) {
    char buf[64];
    snprintf(buf, sizeof(buf), "trailing character(s): '%c'", *chars);
    return strdup(buf);
  }
  return NULL;
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
    CONSTANT_InvokeDynamic = 18,
  };

  uint8_t kind = reader_next_u8(reader, "cp kind");
  switch (kind) {
  case CONSTANT_Class: {
    uint16_t index = reader_next_u16(reader, "class index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_CLASS,
        .class_info = {
            .name = skip_linking
                        ? NULL
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
        skip_linking ? NULL
                     : &checked_cp_entry(
                            ctx->cp, class_index, BJVM_CP_KIND_CLASS,
                            "fieldref/methodref/interfacemethodref class info")
                            ->class_info;

    bjvm_cp_name_and_type *name_and_type =
        skip_linking
            ? NULL
            : &checked_cp_entry(
                   ctx->cp, name_and_type_index, BJVM_CP_KIND_NAME_AND_TYPE,
                   "fieldref/methodref/interfacemethodref name and type")
                   ->name_and_type;

    if (kind == CONSTANT_Fieldref) {
      return (bjvm_cp_entry){.kind = entry_kind,
                             .fieldref_info = {.class_info = class_info,
                                               .nat = name_and_type,
                                               .field = NULL}};
    }
    return (bjvm_cp_entry){.kind = entry_kind,
                           .methodref = {.class_info = class_info,
                                         .name_and_type = name_and_type}};
  }
  case CONSTANT_String: {
    uint16_t index = reader_next_u16(reader, "string index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_STRING,
        .string = {.chars = skip_linking ? NULL
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

    bjvm_utf8 *name = skip_linking ? NULL
                                   : checked_get_utf8(ctx->cp, name_index,
                                                      "name and type name");

    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_NAME_AND_TYPE,
        .name_and_type = {
            .name = name,
            .descriptor = skip_linking
                              ? NULL
                              : checked_get_utf8(ctx->cp, descriptor_index,
                                                 "name and type descriptor")}};
  }
  case CONSTANT_Utf8: {
    uint16_t length = reader_next_u16(reader, "utf8 length");
    cf_byteslice bytes_reader = reader_get_slice(reader, length, "utf8 data");

    bjvm_utf8 utf8 = {0};

    if (!skip_linking) {
      utf8 = parse_modified_utf8(bytes_reader.bytes, length);
      free_on_format_error(ctx, utf8.chars);
    }

    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_UTF8, .utf8 = utf8};
  }
  case CONSTANT_MethodHandle: {
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_METHOD_HANDLE,
        .method_handle_info = {
            .handle_kind = reader_next_u8(reader, "method handle kind"),
            .reference_index = reader_next_u16(reader, "reference index")}};
  }
  case CONSTANT_MethodType: {
    uint16_t desc_index = reader_next_u16(reader, "descriptor index");
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_METHOD_TYPE,
        .method_type_info = {
            .descriptor = skip_linking
                              ? NULL
                              : checked_get_utf8(ctx->cp, desc_index,
                                                 "method type descriptor")}};
  }
  case CONSTANT_InvokeDynamic: {
    uint16_t bootstrap_method_attr_index =
        reader_next_u16(reader, "bootstrap method attr index");
    uint16_t name_and_type_index =
        reader_next_u16(reader, "name and type index");
    bjvm_cp_name_and_type *name_and_type =
        skip_linking ? NULL
                     : &checked_cp_entry(ctx->cp, name_and_type_index,
                                         BJVM_CP_KIND_NAME_AND_TYPE,
                                         "indy name and type")
                            ->name_and_type;

    return (bjvm_cp_entry){.kind = BJVM_CP_KIND_INVOKE_DYNAMIC,
                           .indy_info = {.method = (void*)bootstrap_method_attr_index,  // will be fixed up later
                                         .name_and_type = name_and_type,
                                         .method_descriptor = NULL}};
  }
  default:
    format_error_static("Invalid constant pool entry kind");
  }
}

bjvm_constant_pool *init_constant_pool(uint16_t count) {
  bjvm_constant_pool *pool = calloc(1, sizeof(bjvm_constant_pool) +
                                           (count + 1) * sizeof(bjvm_cp_entry));
  pool->entries_len = count + 1;
  return pool;
}

void finish_constant_pool_entry(bjvm_cp_entry *entry,
                                bjvm_classfile_parse_ctx *ctx) {
  switch (entry->kind) {
  case BJVM_CP_KIND_FIELD_REF: {
    bjvm_field_descriptor *parsed_descriptor = NULL;
    bjvm_cp_name_and_type *name_and_type = entry->fieldref_info.nat;

    entry->fieldref_info.parsed_descriptor = parsed_descriptor =
        calloc(1, sizeof(bjvm_field_descriptor));
    free_on_format_error(ctx, parsed_descriptor);

    char *error = parse_complete_field_descriptor(name_and_type->descriptor,
                                                  parsed_descriptor, ctx);
    if (error)
      format_error_dynamic(error);
    break;
  }
  case BJVM_CP_KIND_INVOKE_DYNAMIC: {
    bjvm_method_descriptor *desc = malloc(sizeof(bjvm_method_descriptor));
    free_on_format_error(ctx, desc);
    char *error = parse_method_descriptor(
        entry->indy_info.name_and_type->descriptor, desc);
    if (error)
      format_error_dynamic(error);
    entry->indy_info.method_descriptor = desc;
    break;
  }
  case BJVM_CP_KIND_METHOD_REF:
  case BJVM_CP_KIND_INTERFACE_METHOD_REF: {
    bjvm_method_descriptor *desc = malloc(sizeof(bjvm_method_descriptor));
    free_on_format_error(ctx, desc);
    bjvm_cp_name_and_type *nat = entry->methodref.name_and_type;
    char *error = parse_method_descriptor(nat->descriptor, desc);
    if (error) {
      char *buf = malloc(1000);
      snprintf(buf, 1000, "Method '%S' has invalid descriptor '%S': %s",
               nat->name->chars, nat->descriptor->chars, error);
      format_error_dynamic(buf);
    }
    entry->methodref.method_descriptor = desc;
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

  bjvm_constant_pool *pool = init_constant_pool(cp_count);
  ctx->cp = pool;
  free_on_format_error(ctx, pool);

  get_constant_pool_entry(pool, 0)->kind =
      BJVM_CP_KIND_INVALID; // entry at 0 is always invalid
  cf_byteslice initial_reader_state = *reader;
  for (int resolution_pass = 0; resolution_pass < 2; ++resolution_pass) {
    // In the first pass, read entries; in the second pass, link them via
    // pointers
    for (int cp_i = 1; cp_i < cp_count; ++cp_i) {
      bjvm_cp_entry *ent = get_constant_pool_entry(pool, cp_i);
      *ent = parse_constant_pool_entry(reader, ctx, !(bool)resolution_pass);
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

  int *targets = malloc(targets_count * sizeof(int));
  for (int i = 0; i < targets_count; ++i) {
    targets[i] = checked_pc(original_pc,
                            reader_next_i32(reader, "tableswitch target"), ctx);
  }
  free_on_format_error(ctx, targets);
  return (bjvm_bytecode_insn){.kind = bjvm_insn_tableswitch,
                              .program_counter = original_pc,
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

  int *keys = malloc(pairs_count * sizeof(int));
  int *targets = malloc(pairs_count * sizeof(int));
  free_on_format_error(ctx, keys);
  free_on_format_error(ctx, targets);

  for (int i = 0; i < pairs_count; ++i) {
    keys[i] = reader_next_i32(reader, "lookupswitch key");
    targets[i] = checked_pc(
        original_pc, reader_next_i32(reader, "lookupswitch target"), ctx);
  }

  return (bjvm_bytecode_insn){.kind = bjvm_insn_lookupswitch,
                              .program_counter = original_pc,
                              .lookupswitch = {.default_target = default_target,
                                               .keys = keys,
                                               .keys_count = pairs_count,
                                               .targets = targets,
                                               .targets_count = pairs_count}};
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
    if (atype < BJVM_TYPE_KIND_BOOLEAN || atype > BJVM_TYPE_KIND_LONG) {
      char buf[64];
      snprintf(buf, sizeof(buf), "invalid newarray type %d", atype);
      format_error_dynamic(strdup(buf));
    }
    return (bjvm_bytecode_insn){.kind = bjvm_insn_newarray,
                                .array_type = atype};
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

char *lossy_utf8_entry_to_chars(const bjvm_utf8 *utf8) {
  char *result = malloc(utf8->len + 1);
  int i = 0;
  for (; i < utf8->len; ++i) {
    result[i] = (char)utf8->chars[i];
  }
  result[i] = '\0';
  return result;
}

bjvm_utf8 bjvm_make_utf8(const wchar_t *c_literal) {
  return (bjvm_utf8){.chars = wcsdup(c_literal), .len = wcslen(c_literal)};
}

/**
 * Parse an instruction at the given program counter and advance the reader.
 * @return The parsed instruction.
 */
bjvm_bytecode_insn parse_insn(cf_byteslice *reader, uint32_t pc,
                              bjvm_classfile_parse_ctx *ctx) {
  bjvm_bytecode_insn insn = parse_insn_impl(reader, pc, ctx);
  insn.program_counter = pc;
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

void parse_bootstrap_methods_attribute(cf_byteslice attr_reader, bjvm_attribute* attr, bjvm_classfile_parse_ctx * ctx) {
  attr->kind = BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS;
  uint16_t count = attr->bootstrap_methods.count = reader_next_u16(&attr_reader, "bootstrap methods count");
  bjvm_bootstrap_method* methods = attr->bootstrap_methods.methods = calloc(count, sizeof(bjvm_bootstrap_method));
  free_on_format_error(ctx, methods);
  for (int i = 0; i < count; ++i) {
    bjvm_bootstrap_method *method = methods + i;
    method->ref = &checked_cp_entry(
                       ctx->cp, reader_next_u16(&attr_reader, "bootstrap method ref"),
                       BJVM_CP_KIND_METHOD_HANDLE, "bootstrap method ref")->method_handle_info;
    uint16_t arg_count =
        reader_next_u16(&attr_reader, "bootstrap method arg count");
    method->args_count = arg_count;
    method->args = calloc(arg_count, sizeof(bjvm_cp_entry *));
    free_on_format_error(ctx, method->args);
    for (int j = 0; j < arg_count; ++j) {
      const int allowed = BJVM_CP_KIND_STRING | BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_FLOAT |
                          BJVM_CP_KIND_LONG | BJVM_CP_KIND_DOUBLE | BJVM_CP_KIND_METHOD_HANDLE | BJVM_CP_KIND_METHOD_TYPE;
      method->args[j] = checked_cp_entry(ctx->cp,
                                         reader_next_u16(&attr_reader, "bootstrap method arg"),
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

  uint8_t *code_start = attr_reader.bytes;

  cf_byteslice code_reader =
      reader_get_slice(&attr_reader, code_length, "code");
  bjvm_bytecode_insn *code = malloc(code_length * sizeof(bjvm_bytecode_insn));

  free_on_format_error(ctx, code);
  ctx->current_code_max_pc = code_length;

  int *pc_to_insn =
      malloc(code_length *
             sizeof(int)); // -1 = no corresponding instruction to that PC
  ctx_free_ticket ticket = free_on_format_error(ctx, pc_to_insn);
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
  bjvm_attribute_exception_table *table = NULL;
  if (exception_table_length) {
    table = calloc(1, sizeof(bjvm_attribute_exception_table));
    free_on_format_error(ctx, table);
    table->entries = calloc(table->entries_count = exception_table_length,
                            sizeof(bjvm_exception_table_entry));
    free_on_format_error(ctx, table->entries);

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
      ent->handler_pc = convert_pc_to_insn(handler_pc, pc_to_insn, code_length);
      ent->catch_type =
          catch_type == 0
              ? NULL
              : &checked_cp_entry(ctx->cp, catch_type, BJVM_CP_KIND_CLASS,
                                  "exception catch type")
                     ->class_info;
    }
  }

  free(pc_to_insn);
  free_ticket(ticket);

  uint16_t attributes_count =
      reader_next_u16(&attr_reader, "code attributes count");
  bjvm_attribute *attributes =
      malloc(attributes_count * sizeof(bjvm_attribute));
  for (int i = 0; i < attributes_count; ++i) {
    bjvm_attribute *attr = attributes + i;
    parse_attribute(&attr_reader, ctx, attr);
  }

  return (bjvm_attribute_code){.max_stack = max_stack,
                               .max_locals = max_locals,
                               .insn_count = insn_count,
                               .max_formal_pc = ctx->current_code_max_pc,
                               .code = code,
                               .attributes = attributes,
                               .exception_table = table,
                               .attributes_count = attributes_count};
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
  } else {
    attr->kind = BJVM_ATTRIBUTE_KIND_UNKNOWN;
  }
}

bool bjvm_is_field_wide(bjvm_field_descriptor desc) {
  return (desc.kind == BJVM_TYPE_KIND_LONG ||
          desc.kind == BJVM_TYPE_KIND_DOUBLE) &&
         !desc.dimensions;
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
  method.descriptor =
      checked_get_utf8(ctx->cp, reader_next_u16(reader, "method descriptor"),
                       "method descriptor");
  method.attributes_count = reader_next_u16(reader, "method attributes count");

  method.attributes = malloc(method.attributes_count * sizeof(bjvm_attribute));
  free_on_format_error(ctx, method.attributes);

  method.parsed_descriptor = calloc(1, sizeof(bjvm_method_descriptor));
  char *error =
      parse_method_descriptor(method.descriptor, method.parsed_descriptor);
  if (error) {
    free(method.parsed_descriptor);
    format_error_dynamic(error);
  }
  complex_free_on_format_error(ctx, method.parsed_descriptor,
                               free_method_descriptor);

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
  field.attributes = calloc(field.attributes_count, sizeof(bjvm_attribute));
  free_on_format_error(ctx, field.attributes);

  for (int i = 0; i < field.attributes_count; i++) {
    parse_attribute(reader, ctx, field.attributes + i);
  }

  char *error = parse_complete_field_descriptor(field.descriptor,
                                                &field.parsed_descriptor, ctx);
  if (error)
    format_error_dynamic(error);

  return field;
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

bjvm_compressed_bitset bjvm_empty_bitset() {
  return (bjvm_compressed_bitset){.bits_inl = 1};
}

bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits) {
  return (bits.bits_inl & 1) != 0; // pointer is aligned
}

void bjvm_free_compressed_bitset(bjvm_compressed_bitset bits) {
  if (!bjvm_is_bitset_compressed(bits))
    free(bits.ptr.bits);
}

/**
 * List all set bits, starting from 0, in the given bitset. Stores the list into
 * the given buffer, which must have the existing length in words (or
 * existing_buf = NULL, length = 0). Returns a (possibly reallocated) buffer.
 *
 * Used to follow references during garbage collection.
 */
int *bjvm_list_compressed_bitset_bits(bjvm_compressed_bitset bits,
                                      int *existing_buf, int *length,
                                      int *capacity) {
  if (bjvm_is_bitset_compressed(bits)) {
    for (int i = 1; i < 64; ++i)
      if (bits.bits_inl & (1ULL << i))
        *VECTOR_PUSH(existing_buf, (*length), (*capacity)) = i - 1;
  } else {
    for (uint32_t i = 0; i < bits.ptr.size_words; ++i)
      for (int j = 0; j < 64; ++j)
        if (bits.ptr.bits[i] & (1ULL << j))
          *VECTOR_PUSH(existing_buf, (*length), (*capacity)) = i * 64 + j;
  }
  return existing_buf;
}

bjvm_compressed_bitset bjvm_init_compressed_bitset(int bits_capacity) {
  if (bits_capacity > 63) {
    uint32_t size_words = (bits_capacity + 63) / 64;
    uint64_t *buffer = calloc(size_words, sizeof(uint64_t));
    return (bjvm_compressed_bitset){.ptr.bits = buffer,
                                    .ptr.size_words = size_words};
  }
  return (bjvm_compressed_bitset){
      .bits_inl = 1 // lowest bit = 1
  };
}

bjvm_compressed_bitset
bjvm_copy_compressed_bitset(bjvm_compressed_bitset bits) {
  if (bjvm_is_bitset_compressed(bits)) {
    return bits;
  }
  const size_t buf_size = bits.ptr.size_words * sizeof(uint64_t);
  uint64_t *buffer = malloc(buf_size);
  memcpy(buffer, bits.ptr.bits, buf_size);
  return (bjvm_compressed_bitset){.ptr.bits = buffer,
                                  .ptr.size_words = bits.ptr.size_words};
}

void get_compressed_bitset_word_and_offset(bjvm_compressed_bitset *bits,
                                           size_t bit_index, uint64_t **word,
                                           uint8_t *offset) {
  if (bjvm_is_bitset_compressed(*bits)) {
    assert(bit_index < 63);
    *word = &bits->bits_inl;
    *offset = bit_index + 1;
  } else {
    assert(bit_index < 64 * bits->ptr.size_words);
    *word = &bits->ptr.bits[bit_index >> 6];
    *offset = bit_index & 0x3f;
  }
}

bool bjvm_test_compressed_bitset(const bjvm_compressed_bitset bits,
                                 size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset((bjvm_compressed_bitset *)&bits,
                                        bit_index, &word, &offset);
  return *word & (1ULL << offset);
}

bool bjvm_test_reset_compressed_bitset(bjvm_compressed_bitset *bits,
                                       size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset(bits, bit_index, &word, &offset);
  bool test = *word & (1ULL << offset);
  *word &= ~(1ULL << offset);
  return test;
}

bool bjvm_test_set_compressed_bitset(bjvm_compressed_bitset *bits,
                                     size_t bit_index) {
  uint64_t *word;
  uint8_t offset;
  get_compressed_bitset_word_and_offset(bits, bit_index, &word, &offset);
  bool test = *word & (1ULL << offset);
  *word |= 1ULL << offset;
  return test;
}

void free_field_descriptor(bjvm_field_descriptor descriptor) {
  if (descriptor.kind == BJVM_TYPE_KIND_REFERENCE) {
    free_utf8(descriptor.class_name);
  }
}

/**
 * Parse the field descriptor in the string slice starting at **chars, with
 * length len, writing the result to result and returning an owned error message
 * if there was an error.
 */
char *parse_field_descriptor(const wchar_t **chars, size_t len,
                             bjvm_field_descriptor *result) {
  const wchar_t *end = *chars + len;
  int dimensions = 0;
  while (*chars < end) {
    result->dimensions = dimensions;
    if (dimensions > 255)
      return strdup("too many dimensions (max 255)");
    wchar_t c = **chars;
    (*chars)++;
    switch (c) {
    case L'B':
      result->kind = BJVM_TYPE_KIND_BYTE;
      return NULL;
    case L'C':
      result->kind = BJVM_TYPE_KIND_CHAR;
      return NULL;
    case L'D':
      result->kind = BJVM_TYPE_KIND_DOUBLE;
      return NULL;
    case L'F':
      result->kind = BJVM_TYPE_KIND_FLOAT;
      return NULL;
    case L'I':
      result->kind = BJVM_TYPE_KIND_INT;
      return NULL;
    case L'J':
      result->kind = BJVM_TYPE_KIND_LONG;
      return NULL;
    case L'S':
      result->kind = BJVM_TYPE_KIND_SHORT;
      return NULL;
    case L'Z':
      result->kind = BJVM_TYPE_KIND_BOOLEAN;
      return NULL;
    case L'V': {
      result->kind = BJVM_TYPE_KIND_VOID;
      if (dimensions > 0)
        return strdup("void cannot have dimensions");
      return NULL; // lol, check this later
    }
    case L'[':
      ++dimensions;
      break;
    case L'L': {
      const wchar_t *start = *chars;
      while (*chars < end && **chars != ';')
        ++*chars;
      if (*chars == end)
        return strdup("missing ';' in reference type");
      size_t class_name_len = *chars - start;
      if (class_name_len == 0) {
        return strdup("missing reference type name");
      }
      ++*chars;
      result->kind = BJVM_TYPE_KIND_REFERENCE;
      result->class_name = bjvm_wchar_slice_to_utf8(start, class_name_len);
      return NULL;
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

char *err_while_parsing_md(bjvm_method_descriptor *result, char *error) {
  for (int i = 0; i < result->args_count; ++i) {
    free_field_descriptor(result->args[i]);
  }
  free(result->args);
  return error;
}

char *parse_method_descriptor(const bjvm_utf8 *entry,
                              bjvm_method_descriptor *result) {
  // MethodDescriptor:
  // ( { ParameterDescriptor } )
  // ParameterDescriptor:
  // FieldType
  const size_t len = entry->len;
  const wchar_t *chars = entry->chars, *end = chars + len;
  if (len < 1 || *chars++ != '(')
    return strdup("Expected '('");
  result->args = NULL;
  result->args_cap = result->args_count = 0;
  while (chars < end && *chars != ')') {
    bjvm_field_descriptor arg;

    char *error = parse_field_descriptor(&chars, end - chars, &arg);
    if (error || arg.kind == BJVM_TYPE_KIND_VOID)
      return err_while_parsing_md(
          result, error ? error : strdup("void as method parameter"));

    *VECTOR_PUSH(result->args, result->args_count, result->args_cap) = arg;
  }
  if (chars >= end)
    return err_while_parsing_md(result,
                                strdup("missing ')' in method descriptor"));
  chars++; // skip ')'
  char *error =
      parse_field_descriptor(&chars, end - chars, &result->return_type);
  return error ? err_while_parsing_md(result, error) : NULL;
}

char *bjvm_parse_classfile(uint8_t *bytes, size_t len, bjvm_classdesc *result) {
  cf_byteslice reader = {.bytes = bytes, .len = len};
  bjvm_classdesc *cf = result;
  bjvm_classfile_parse_ctx ctx = {.free_on_error = NULL,
                                  .free_on_error_count = 0,
                                  .free_on_error_cap = 0,
                                  .cp = NULL};

  if (setjmp(format_error_jmp_buf)) {
    for (int i = 0; i < ctx.free_on_error_count; i += 2) {
      void *to_free = ctx.free_on_error[i];
      if (to_free) {
        void (*free_fn)(void *) = ctx.free_on_error[i + 1];
        free_fn(to_free);
      }
    }

    free(ctx.free_on_error);
    assert(format_error_msg);
    result->state = BJVM_CD_STATE_LINKAGE_ERROR;
    return format_error_needs_free ? format_error_msg
                                   : strdup(format_error_msg);
  }

  const uint32_t magic = reader_next_u32(&reader, "magic");
  if (magic != 0xCAFEBABE) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Invalid magic number 0x%08x", magic);
    format_error_dynamic(strdup(buf));
  }

  cf->minor_version = reader_next_u16(&reader, "minor version");
  cf->major_version = reader_next_u16(&reader, "major version");

  cf->pool = parse_constant_pool(&reader, &ctx);

  cf->access_flags = reader_next_u16(&reader, "access flags");
  bjvm_cp_class_info *this_class =
      &checked_cp_entry(cf->pool, reader_next_u16(&reader, "this class"),
                        BJVM_CP_KIND_CLASS, "this class")
           ->class_info;
  cf->name = *this_class->name;

  bool is_primordial_object = cf->is_primordial_object =
      utf8_equals(&cf->name, "java/lang/Object");

  uint16_t super_class = reader_next_u16(&reader, "super class");
  cf->super_class = is_primordial_object
                        ? NULL
                        : &checked_cp_entry(cf->pool, super_class,
                                            BJVM_CP_KIND_CLASS, "super class")
                               ->class_info;

  // Parse superinterfaces
  cf->interfaces_count = reader_next_u16(&reader, "interfaces count");
  cf->interfaces = malloc(cf->interfaces_count * sizeof(bjvm_cp_class_info *));
  free_on_format_error(&ctx, cf->interfaces);
  for (int i = 0; i < cf->interfaces_count; i++) {
    cf->interfaces[i] =
        &checked_cp_entry(cf->pool, reader_next_u16(&reader, "interface"),
                          BJVM_CP_KIND_CLASS, "superinterface")
             ->class_info;
  }

  // Parse fields
  cf->fields_count = reader_next_u16(&reader, "fields count");
  cf->fields = malloc(cf->fields_count * sizeof(bjvm_cp_field));
  free_on_format_error(&ctx, cf->fields);
  for (int i = 0; i < cf->fields_count; i++) {
    cf->fields[i] = read_field(&reader, &ctx);
    cf->fields[i].my_class = result;
  }
  cf->static_fields = NULL;
  cf->static_references = bjvm_empty_bitset();
  cf->instance_references = bjvm_empty_bitset();

  // Parse methods
  cf->methods_count = reader_next_u16(&reader, "methods count");
  cf->methods = malloc(cf->methods_count * sizeof(bjvm_cp_method));
  free_on_format_error(&ctx, cf->methods);

  cf->bootstrap_methods = NULL;

  bool in_MethodHandle =
      utf8_equals(&cf->name, "java/lang/invoke/MethodHandle");
  for (int i = 0; i < cf->methods_count; ++i) {
    cf->methods[i] = parse_method(&reader, &ctx);
    cf->methods[i].my_class = result;

    // Mark signature polymorphic functions
    if (in_MethodHandle && (utf8_equals(cf->methods[i].name, "invoke") ||
                            utf8_equals(cf->methods[i].name, "invokeExact"))) {
      // "In Java SE 8, the only signature polymorphic methods are the invoke
      // and invokeExact methods of the class java.lang.invoke.MethodHandle."
      cf->methods[i].is_signature_polymorphic = true;
    }
  }

  // Parse attributes
  cf->attributes_count = reader_next_u16(&reader, "class attributes count");
  cf->attributes = malloc(cf->attributes_count * sizeof(bjvm_attribute));
  free_on_format_error(&ctx, cf->attributes);
  for (int i = 0; i < cf->attributes_count; i++) {
    bjvm_attribute *attr = cf->attributes + i;
    parse_attribute(&reader, &ctx, attr);
    if (attr->kind == BJVM_ATTRIBUTE_KIND_BOOTSTRAP_METHODS) {
      cf->bootstrap_methods = &attr->bootstrap_methods;
    }
  }

  result->state = BJVM_CD_STATE_LOADED;
  free(ctx.free_on_error); // we made it :)
  return NULL;
}

