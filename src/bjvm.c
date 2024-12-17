#define AGGRESSIVE_DEBUG 0

#define CACHE_INVOKESTATIC 0
#define CACHE_INVOKENONSTATIC 0
#define ONE_GOTO_PER_INSN 0

#include <assert.h>
#include <limits.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

// Byteswap (for classfile endianness)
#if defined(__APPLE__)
#include <libkern/OSByteOrder.h>
#define __bswap_16(x) OSSwapInt16(x)
#define __bswap_32(x) OSSwapInt32(x)
#define __bswap_64(x) OSSwapInt64(x)
#else
#include <byteswap.h>
#endif

// SIMD support
#ifdef __ARM_NEON__
#include <arm_neon.h>
#elif defined(__SSE__)
#include <immintrin.h>
#elif defined(EMSCRIPTEN)
#include <wasm_simd128.h>
#endif

#include "bjvm.h"

#include <math.h>

#define UNREACHABLE(optional_msg)                                              \
  do {                                                                         \
    fprintf(stderr, "Unreachable code reached at %s:%d. \n" optional_msg,      \
            __FILE__, __LINE__);                                               \
    abort();                                                                   \
  } while (0)

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

bool utf8_equals(const bjvm_utf8 *entry, const char *str) {
  if (entry->len != (int)strlen(str))
    return false;
  for (int i = 0; i < entry->len; ++i)
    if (entry->chars[i] != str[i])
      return false;
  return true;
}

bool utf8_equals_utf8(const bjvm_utf8 *left, const bjvm_utf8 *right) {
  if (left->len != right->len)
    return false;
  return wmemcmp(left->chars, right->chars, left->len) == 0;
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
  case BJVM_CP_KIND_METHOD_TYPE: {
    free_method_descriptor(entry->method_type_info.parsed_descriptor);
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

#define VECTOR_PUSH(vector, vector_count, vector_cap)                          \
  ({                                                                           \
    if ((vector_count) >= (vector_cap)) {                                      \
      int new_cap;                                                             \
      int overflow = __builtin_mul_overflow((vector_cap), 2, &new_cap);        \
      assert(!overflow);                                                       \
      if (new_cap < 2)                                                         \
        new_cap = 2;                                                           \
      void *next = realloc(vector, new_cap * sizeof(*vector));                 \
      assert(next);                                                            \
      (vector_cap) = new_cap;                                                  \
      vector = next;                                                           \
    }                                                                          \
    &vector[(vector_count)++];                                                 \
  })

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

bjvm_cp_entry *check_cp_entry(bjvm_cp_entry *entry, int expected_kinds,
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
  return check_cp_entry(&pool->entries[index], expected_kinds, reason);
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
    uint8_t handle_kind = reader_next_u8(reader, "method handle kind");
    uint16_t reference_index = reader_next_u16(reader, "reference index");
    bjvm_cp_entry *entry = skip_linking ? NULL : checked_cp_entry(
                                                   ctx->cp, reference_index,
                                                   -1,
                                                   "method handle reference");
    // TODO validate both
    return (bjvm_cp_entry){
        .kind = BJVM_CP_KIND_METHOD_HANDLE,
        .method_handle_info = {
            .handle_kind = handle_kind,
            .reference = entry}};
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

bool is_kind_wide(bjvm_type_kind kind) {
  return kind == BJVM_TYPE_KIND_LONG || kind == BJVM_TYPE_KIND_DOUBLE;
}

void primitive_type_kind_to_array_info(bjvm_type_kind kind,
                                       const wchar_t **type, int *size) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
    *type = L"[Z";
    *size = 1;
    break;
  case BJVM_TYPE_KIND_CHAR:
    *type = L"[C";
    *size = 2;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    *type = L"[F";
    *size = 4;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    *type = L"[D";
    *size = 8;
    break;
  case BJVM_TYPE_KIND_BYTE:
    *type = L"[B";
    *size = 1;
    break;
  case BJVM_TYPE_KIND_SHORT:
    *type = L"[S";
    *size = 2;
    break;
  case BJVM_TYPE_KIND_INT:
    *type = L"[I";
    *size = 4;
    break;
  case BJVM_TYPE_KIND_LONG:
    *type = L"[J";
    *size = 8;
    break;
  default:
    UNREACHABLE();
  }
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
    char *error = parse_method_descriptor(nat->descriptor, desc);  // TODO free on FormatError
    if (error) {
      char *buf = malloc(1000);
      snprintf(buf, 1000, "Method '%S' has invalid descriptor '%S': %s",
               nat->name->chars, nat->descriptor->chars, error);
      format_error_dynamic(buf);
    }
    entry->methodref.method_descriptor = desc;
    break;
  }
  case BJVM_CP_KIND_METHOD_TYPE: {
    bjvm_method_descriptor *desc = malloc(sizeof(bjvm_method_descriptor));
    free_on_format_error(ctx, desc);
    char *error = parse_method_descriptor(
        entry->method_type_info.descriptor, desc); // TODO free on FormatError
    if (error)
      format_error_dynamic(error);
    entry->method_type_info.parsed_descriptor = desc;
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
    instanceof
    = 0xc1,
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

char *class_info_entry_to_string(const bjvm_cp_class_info *ent) {
  char result[1000];
  snprintf(result, sizeof(result), "Class: %S", ent->name->chars);
  return strdup(result);
}

char *
name_and_type_entry_to_string(const bjvm_cp_name_and_type *name_and_type) {
  char result[1000];
  snprintf(result, sizeof(result), "NameAndType: %S:%S",
           name_and_type->name->chars, name_and_type->descriptor->chars);
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
    return lossy_utf8_entry_to_chars(&ent->utf8);
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
    snprintf(result, sizeof(result), "String: '%S'", ent->string.chars->chars);
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

  return NULL;
fail:;
  char *a_str = print_analy_stack_state(&a),
       *b_str = print_analy_stack_state(&b);
  char *buf = malloc(strlen(a_str) + strlen(b_str) + 128);
  sprintf(buf, "Stack mismatch:\nPreviously inferred: %s\nFound: %s", a_str,
          b_str);
  free(a_str);
  free(b_str);
  return buf;
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

/**
 * Analyze the method's code segment if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis, and returning an error string upon some sort of error.
 */
char *analyze_method_code_segment(bjvm_cp_method *method) {
  bjvm_attribute_code *code = method->code;
  if (!code)
    return NULL; // method has no code

  char *error = NULL;

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
          check_cp_entry(insn->cp, BJVM_CP_KIND_FIELD_REF,
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
          check_cp_entry(insn->cp, BJVM_CP_KIND_INVOKE_DYNAMIC,
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
          check_cp_entry(insn->cp,
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
          check_cp_entry(insn->cp,
                         BJVM_CP_KIND_INTEGER | BJVM_CP_KIND_STRING |
                             BJVM_CP_KIND_FLOAT | BJVM_CP_KIND_CLASS,
                         "ldc argument");
      PUSH_KIND(ent->kind == BJVM_CP_KIND_INTEGER ? BJVM_TYPE_KIND_INT
                : ent->kind == BJVM_CP_KIND_FLOAT ? BJVM_TYPE_KIND_FLOAT
                                                  : BJVM_TYPE_KIND_REFERENCE)
      break;
    }
    case bjvm_insn_ldc2_w: {
      bjvm_cp_entry *ent = check_cp_entry(
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
    error = calloc(50000, 1);
    char *insn_str = insn_to_string(insn, i);
    char *stack_str = print_analy_stack_state(&stack_before);
    char *context = code_attribute_to_string(method->code);
    snprintf(error, 50000,
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

  return error;
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

bool bjvm_is_bitset_compressed(bjvm_compressed_bitset bits) {
  return (bits.bits_inl & 1) != 0; // pointer is aligned
}

bjvm_compressed_bitset bjvm_empty_bitset() {
  return (bjvm_compressed_bitset){.bits_inl = 1};
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

// Go through the InvokeDynamic entries and link their bootstrap method pointers
void link_bootstrap_methods(bjvm_classdesc *cf) {
  bjvm_constant_pool *cp = cf->pool;
  for (int i = 1; i < cp->entries_len; i++) {
    if (cp->entries[i].kind == BJVM_CP_KIND_INVOKE_DYNAMIC) {
      bjvm_cp_indy_info *indy = &cp->entries[i].indy_info;
      int index = (int)(uintptr_t)indy->method;
      if (index < 0 || index >= cf->bootstrap_methods->count) {
        format_error_static("Invalid bootstrap method index");
      }
      indy->method = &cf->bootstrap_methods->methods[index];
    }
  }
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

  link_bootstrap_methods(cf);

  result->state = BJVM_CD_STATE_LOADED;
  free(ctx.free_on_error); // we made it :)
  return NULL;
}

#define MAX_CF_NAME_LENGTH 1000

struct classfile_entry {
  size_t len;
  uint8_t* data;
};

struct classfile_entry* make_cf_entry(const uint8_t* bytes, size_t len) {
  struct classfile_entry *result = malloc(sizeof(struct classfile_entry));
  result->data = malloc(result->len = len);
  memcpy(result->data, bytes, len);
  return result;
}

void free_cf_entry(void* entry_) {
  if (!entry_)
    return;
  struct classfile_entry *entry = entry_;
  free(entry->data);
  free(entry);
}

int add_classfile_bytes(bjvm_vm *vm,
                        const wchar_t *filename, size_t filename_length,
                        const uint8_t *bytes, size_t len) {
  if (filename_length > MAX_CF_NAME_LENGTH)
    return -1;

  struct classfile_entry *entry = make_cf_entry(bytes, len);
  free_cf_entry(bjvm_hash_table_insert(&vm->classfiles, filename, filename_length, entry));
  return 0;
}

bjvm_string_hash_table bjvm_make_hash_table(void (*free_fn)(void *),
                                            double load_factor,
                                            size_t initial_capacity) {
  bjvm_string_hash_table table;
  table.free_fn = free_fn;
  table.entries = calloc(initial_capacity, sizeof(bjvm_hash_table_entry));
  table.entries_count = 0;
  table.entries_cap = initial_capacity;
  table.load_factor = load_factor;
  return table;
}

bjvm_hash_table_iterator
bjvm_hash_table_get_iterator(bjvm_string_hash_table *tbl) {
  bjvm_hash_table_iterator iter;
  iter.current_base = tbl->entries;
  bjvm_hash_table_entry *end = tbl->entries + tbl->entries_cap;
  // advance to first nonzero entry
  while (iter.current_base < end && iter.current_base->key == NULL)
    iter.current_base++;
  iter.current = iter.current_base;
  iter.end = end;
  return iter;
}

bool bjvm_hash_table_iterator_has_next(bjvm_hash_table_iterator iter,
                                       wchar_t **key, size_t *key_len,
                                       void **value) {
  if (iter.current != iter.end) {
    *key = iter.current->key;
    *key_len = iter.current->key_len;
    *value = iter.current->data;
    return true;
  }
  return false;
}

bool bjvm_hash_table_iterator_next(bjvm_hash_table_iterator *iter) {
  if (iter->current_base == iter->end)
    return false;
  if (iter->current->next) {
    iter->current = iter->current->next;
    return true;
  }
  // advance base until a non-null key
  while (++iter->current_base < iter->end && iter->current_base->key == NULL)
    ;
  iter->current = iter->current_base;
  return iter->current_base != iter->end;
}

uint32_t bjvm_hash_string(const wchar_t *key, size_t len) {
  uint64_t hash = 0;
  for (size_t i = 0; i < len; ++i) {
    hash = 31 * hash + key[i]; // yk what I mean ;)
  }
  return hash;
}

bjvm_hash_table_entry *
bjvm_find_hash_table_entry(bjvm_string_hash_table *tbl, const wchar_t *key,
                           size_t len, bool *equal, bool *on_chain,
                           bjvm_hash_table_entry **prev_entry) {
  uint32_t hash = bjvm_hash_string(key, len);
  size_t index = hash % tbl->entries_cap;
  bjvm_hash_table_entry *ent = &tbl->entries[index], *prev = NULL;
  while (ent) {
    *on_chain = prev != NULL;
    if (ent->key && ent->key_len == len && wmemcmp(ent->key, key, len) == 0) {
      *equal = true;
      *prev_entry = prev;
      return ent;
    }
    if (!ent->next) {
      *equal = false;
      *prev_entry = prev;
      return ent;
    }
    prev = ent;
    ent = ent->next;
  }
  *equal = false;
  *on_chain = true;
  if (prev_entry)
    *prev_entry = prev;
  return prev;
}

void *bjvm_hash_table_delete(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? wcslen(key) : len;
  bjvm_hash_table_entry *prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &prev);
  if (!equal)
    return NULL;
  tbl->entries_count--;
  void *ret_val = ent->data;
  free(ent->key);
  ent->key = NULL;
  if (prev) {
    prev->next = ent->next;
    free(ent);
  } else if (ent->next) {
    prev = ent->next;
    *ent = *ent->next;
    free(prev);
  }
  return ret_val;
}

void *bjvm_hash_table_insert_impl(bjvm_string_hash_table *tbl, wchar_t *key,
                                  int len, void *value, bool copy_key) {
  len = len == -1 ? wcslen(key) : len;
  bool equal, on_chain;
  if (tbl->entries_count + 1 >= tbl->load_factor * tbl->entries_cap) {
    bjvm_hash_table_rehash(tbl, tbl->entries_cap * 2);
  }

  bjvm_hash_table_entry *_prev,
      *ent =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  if (equal) {
    void *ret_val = ent->data;
    ent->data = value;
    if (!copy_key)
      free(key);
    return ret_val;
  }
  if (on_chain || ent->key != NULL) {
    ent->next = malloc(sizeof(bjvm_hash_table_entry));
    ent = ent->next;
  }
  ent->next = NULL;
  ent->data = value;
  if (copy_key) {
    wchar_t *new_key = malloc(len * sizeof(wchar_t));
    wmemcpy(new_key, key, len);
    ent->key = new_key;
  } else {
    ent->key = key;
  }
  ent->key_len = len;
  tbl->entries_count++;
  return NULL;
}

void *bjvm_hash_table_insert(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len, void *value) {
  return bjvm_hash_table_insert_impl(tbl, (wchar_t *)key /* key copied */, len,
                                     value, true);
}

void bjvm_hash_table_rehash(bjvm_string_hash_table *tbl, size_t new_capacity) {
  bjvm_string_hash_table new_table =
      bjvm_make_hash_table(tbl->free_fn, tbl->load_factor, new_capacity);
  bjvm_hash_table_iterator iter = bjvm_hash_table_get_iterator(tbl);
  wchar_t *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(iter, &key, &len, &value)) {
    bjvm_hash_table_insert_impl(&new_table, key, len, value,
                                false /* don't copy key */);
    bjvm_hash_table_entry *ent = iter.current;
    bool entry_on_chain = iter.current != iter.current_base;
    bjvm_hash_table_iterator_next(&iter);
    if (entry_on_chain)
      free(ent);
  }
  free(tbl->entries); // Don't need to free the linked lists, keys etc. as they
                      // were moved over
  *tbl = new_table;
}

void *bjvm_hash_table_lookup(bjvm_string_hash_table *tbl, const wchar_t *key,
                             int len) {
  bool equal, on_chain;
  len = len == -1 ? wcslen(key) : len;
  bjvm_hash_table_entry *_prev,
      *entry =
          bjvm_find_hash_table_entry(tbl, key, len, &equal, &on_chain, &_prev);
  return equal ? entry->data : NULL;
}

void bjvm_free_hash_table(bjvm_string_hash_table tbl) {
  bjvm_hash_table_iterator it = bjvm_hash_table_get_iterator(&tbl);
  wchar_t *key;
  size_t len;
  void *value;
  while (bjvm_hash_table_iterator_has_next(it, &key, &len, &value)) {
    bjvm_hash_table_entry *ent = it.current;
    bool needs_free = it.current != it.current_base;
    free(key);
    if (tbl.free_fn)
      tbl.free_fn(value);
    bjvm_hash_table_iterator_next(&it);
    if (needs_free)
      free(ent);
  }
  free(tbl.entries);
  tbl.entries_cap = tbl.entries_count = 0; // good form
}

bjvm_stack_frame *bjvm_push_frame(bjvm_thread *thread, bjvm_cp_method *method) {
  assert(method != NULL);

  const bjvm_attribute_code *code = method->code;
  assert(code);

  const size_t header_bytes = sizeof(bjvm_stack_frame);
  assert(header_bytes % 8 == 0);

  size_t local_bytes =
      ((int)code->max_locals + code->max_stack) * sizeof(bjvm_stack_value);
  size_t total = header_bytes + local_bytes;

  if (total + thread->frame_buffer_used > thread->frame_buffer_capacity) {
    //bjvm_raise_exception_object(thread, thread->stack_overflow_error);
    UNREACHABLE();
  }

  bjvm_stack_frame *frame =
      (bjvm_stack_frame *)(thread->frame_buffer + thread->frame_buffer_used);
  memset(frame, 0, total);
  thread->frame_buffer_used += total;
  *VECTOR_PUSH(thread->frames, thread->frames_count, thread->frames_cap) =
      frame;
  frame->max_locals = code->max_locals;
  frame->max_stack = code->max_stack;
  frame->program_counter = 0;
  frame->stack_depth = 0;
  frame->method = method;

  return frame;
}

char *stack_value_to_string(bjvm_stack_value value) {
  char buf[1000];
  sprintf(buf, "[ ref = %p, int = %d ]", value.obj, value.i);
  return strdup(buf);
}

char *dump_frame(const bjvm_stack_frame *frame) {
  char buf[2000] = {0}, *write = buf, *end = buf + sizeof(buf);

  for (int i = 0; i < frame->stack_depth; ++i) {
    char *stack_val = stack_value_to_string(frame->values[i]);
    write += snprintf(write, end - write, " stack[%d] = %s\n", i, stack_val);
    free(stack_val);
  }

  for (int i = 0; i < frame->max_locals; ++i) {
    char *stack_val =
        stack_value_to_string(frame->values[i + frame->max_stack]);
    write += snprintf(write, end - write, "locals[%d] = %s\n", i, stack_val);
    free(stack_val);
  }

  return strdup(buf);
}

void bjvm_pop_frame(bjvm_thread *thr, const bjvm_stack_frame *reference) {
  assert(thr->frames_count > 0);
  bjvm_stack_frame *frame = thr->frames[thr->frames_count - 1];
  assert(reference == NULL || reference == frame);
  thr->frames_count--;
  thr->frame_buffer_used =
      thr->frames_count == 0 ? 0 : (uint8_t *)frame - thr->frame_buffer;
}

void free_array_classdesc(bjvm_array_classdesc *classdesc) {
  if (classdesc->base.array_type) {
    classdesc->base.array_type = NULL;
    free_array_classdesc((bjvm_array_classdesc *)classdesc->base.array_type);
  }
  free(classdesc);
}

void free_primitive_arr_classdesc(bjvm_primitive_array_classdesc *classdesc);

void free_classdesc(void *classdesc_) {
  bjvm_classdesc *classdesc = classdesc_;
  if (classdesc->kind == BJVM_CD_KIND_ORDINARY) {
    bjvm_free_classfile(*classdesc);
    free(classdesc_);
  } else if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
    free_array_classdesc((bjvm_array_classdesc *)classdesc);
  } else {
    free_primitive_arr_classdesc((bjvm_primitive_array_classdesc *)classdesc);
  }
}

typedef struct {
  bjvm_utf8 name;
  bjvm_utf8 descriptor;

  bjvm_native_callback callback;
} native_entry;

typedef struct {
  native_entry *entries;
  int entries_count;
  int entries_cap;
} native_entries;

void free_native_entries(void *entries_) {
  if (!entries_)
    return;

  native_entries *entries = entries_;
  for (int i = 0; i < entries->entries_count; i++) {
    free_utf8(entries->entries[i].name);
    free_utf8(entries->entries[i].descriptor);
  }
  free(entries);
}

void bjvm_register_native(bjvm_vm *vm, const char *class_name,
                          const char *method_name,
                          const char *method_descriptor,
                          bjvm_native_callback callback) {
  bjvm_utf8 class = bjvm_make_utf8_cstr(class_name);

  native_entries *existing =
      bjvm_hash_table_lookup(&vm->natives, class.chars, class.len);
  if (!existing) {
    existing = calloc(1, sizeof(native_entries));
    (void)bjvm_hash_table_insert(&vm->natives, class.chars, class.len,
                                 existing);
  }
  free_utf8(class);

  native_entry *ent = VECTOR_PUSH(existing->entries, existing->entries_count,
                                  existing->entries_cap);
  ent->name = bjvm_make_utf8_cstr(method_name);
  ent->descriptor = bjvm_make_utf8_cstr(method_descriptor);
  ent->callback = callback;
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 *name,
                                   const bjvm_utf8 *method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces);

// Generally, use this to indicate that a native function is returning void or null
bjvm_stack_value value_null() {
  return (bjvm_stack_value) { .obj = NULL };
}

bjvm_stack_value unimplemented_native(bjvm_thread *, bjvm_obj_header *, bjvm_stack_value *,
                         int) {
  return value_null();
}

// Raise an UnsatisfiedLinkError relating to the given method.
void bjvm_unsatisfied_link_error(bjvm_thread *thread,
                                 const bjvm_cp_method *method) {
  wchar_t err[1000] = {0};
  swprintf(err, 1000, L"Method %S on class %S with descriptor %S",
           method->name->chars, method->my_class->name.chars,
           method->descriptor->chars);
  bjvm_raise_exception(thread, L"java/lang/UnsatisfiedLinkError", err);
}

// Raise a NegativeArraySizeException with the given count value.
void bjvm_negative_array_size_exception(bjvm_thread *thread, int count) {
  wchar_t err[12] = { 0 };
  swprintf(err, 12, L"%d", count);
  bjvm_raise_exception(thread, L"java/lang/NegativeArraySizeException", err);
}

// Raise a NullPointerException.
void bjvm_null_pointer_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, L"java/lang/NullPointerException", NULL);
}

// Raise an ArrayStoreException.
void bjvm_array_store_exception(bjvm_thread *thread) {
  bjvm_raise_exception(thread, L"java/lang/ArrayStoreException", NULL);
}

// Raise an IncompatibleClassChangeError.
void bjvm_incompatible_class_change_error(bjvm_thread *thread, const wchar_t *complaint) {
  bjvm_raise_exception(thread, L"java/lang/IncompatibleClassChangeError", complaint);
}

// Raise an AbstractMethodError.
void bjvm_abstract_method_error(bjvm_thread *thread, const bjvm_cp_method* method) {
  wchar_t complaint[1000];
  swprintf(complaint, 1000, L"Abstract method '%S' on class %S", method->name->chars, method->my_class->name.chars);
  bjvm_raise_exception(thread, L"java/lang/AbstractMethodError", complaint);
}

// Raise an ArithmeticException.
void bjvm_arithmetic_exception(bjvm_thread *thread, const wchar_t* complaint) {
  bjvm_raise_exception(thread, L"java/lang/ArithmeticException", complaint);
}

// Raise an ArrayIndexOutOfBoundsException with the given index and length.
void bjvm_array_index_oob_exception(bjvm_thread *thread, int index, int length) {
  wchar_t complaint[80];
  swprintf(complaint, 80, L"Index %d out of bounds for array of length %d", index, length);
  bjvm_raise_exception(thread, L"java/lang/ArrayIndexOutOfBoundsException", complaint);
}

void read_string(bjvm_obj_header *obj, short **buf, size_t *len) {
  assert(utf8_equals(&obj->descriptor->name, "java/lang/String"));
  bjvm_obj_header *array = ((struct bjvm_native_String*)obj)->value;
  *buf = array_data(array);
  *len = *array_length(array);
}

// TODO read the properties from the VM instead of hardcoding them
bjvm_stack_value bjvm_System_initProperties(bjvm_thread *thread, bjvm_obj_header *,
                               bjvm_stack_value *args, int) {
  bjvm_obj_header *props_obj = args[0].obj;
  const wchar_t *const props[][2] = {
      {L"file.encoding", L"UTF-8"},   {L"stdout.encoding", L"UTF-8"},
      {L"native.encoding", L"UTF-8"}, {L"stderr.encoding", L"UTF-8"},
      {L"line.separator", L"\n"},     {L"path.separator", L":"},
      {L"file.separator", L"/"}};
  bjvm_cp_method *put = bjvm_easy_method_lookup(
      props_obj->descriptor, "put",
      "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;", true, false);
  for (size_t i = 0; i < sizeof(props) / sizeof(props[0]); ++i) {
    bjvm_stack_value put_args[3] = {
        {.obj = props_obj},
        {.obj = bjvm_intern_string(thread, props[i][0], wcslen(props[i][0]))},
        {.obj = bjvm_intern_string(thread, props[i][1], wcslen(props[i][1]))}};
    bjvm_stack_value result;
    // call put() with String key and value
    bjvm_thread_run(thread, put, put_args, &result);
  }
  return value_null();
}

bjvm_stack_value bjvm_System_mapLibraryName(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int) {
  return args[0];
}

void *array_data(bjvm_obj_header *array);
bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *classdesc);

bool is_1d_primitive_array(bjvm_obj_header * src) {
  return src->descriptor->kind != BJVM_CD_KIND_ORDINARY_ARRAY &&
    ((bjvm_primitive_array_classdesc*) src->descriptor)->dimensions == 1;
}

bjvm_stack_value bjvm_System_arraycopy(bjvm_thread* thread, bjvm_obj_header*,
                          bjvm_stack_value *args, int argc) {
  assert(argc == 5);
  bjvm_obj_header *src = args[0].obj;
  bjvm_obj_header *dest = args[2].obj;
  if (src == NULL || dest == NULL) {
    bjvm_null_pointer_exception(thread);
    return value_null();
  }
  if (src->descriptor->kind == BJVM_CD_KIND_ORDINARY || dest->descriptor->kind == BJVM_CD_KIND_ORDINARY) {
    // Can't copy non-array objects to each other
    bjvm_array_store_exception(thread);
    return value_null();
  }
  bool src_is_1d_primitive = is_1d_primitive_array(src), dst_is_1d_primitive = is_1d_primitive_array(dest);
  if (src_is_1d_primitive != dst_is_1d_primitive) {
    bjvm_array_store_exception(thread);
    return value_null();
  }

  int src_pos = args[1].i;
  int dest_pos = args[3].i;
  int length = args[4].i;
  int src_length = *array_length(src);
  int dest_length = *array_length(dest);
  // Verify that everything is in bounds
  // TODO add more descriptive error messages
  if (src_pos < 0 || dest_pos < 0 || length < 0 || (int64_t)src_pos + length > src_length || (int64_t)dest_pos + length > dest_length) {
    bjvm_raise_exception(thread, L"java/lang/ArrayIndexOutOfBoundsException", NULL);
    return value_null();
  }

#define GEN_PRIMITIVE_IMPL(array_type, underlying)                                       \
  if (src->descriptor->kind == BJVM_CD_KIND_##array_type) {                    \
    underlying *src_data = array_data(src);                                    \
    underlying *dest_data = array_data(dest);                                  \
    for (int i = 0; i < length; ++i)                                           \
      dest_data[dest_pos + i] = src_data[src_pos + i];                         \
    return value_null();                                                               \
  }

  if (src_is_1d_primitive) {
    GEN_PRIMITIVE_IMPL(BYTE_ARRAY, int8_t)
    GEN_PRIMITIVE_IMPL(CHAR_ARRAY, uint16_t)
    GEN_PRIMITIVE_IMPL(DOUBLE_ARRAY, double)
    GEN_PRIMITIVE_IMPL(FLOAT_ARRAY, float)
    GEN_PRIMITIVE_IMPL(INT_ARRAY, int32_t)
    GEN_PRIMITIVE_IMPL(LONG_ARRAY, int64_t)
    GEN_PRIMITIVE_IMPL(SHORT_ARRAY, int16_t)
    GEN_PRIMITIVE_IMPL(BOOLEAN_ARRAY, uint8_t)
  }

  // If the component type of the src class is an instanceof the destination class, then we don't
  // need to perform any checks. Otherwise, we need to perform an instanceof check on each element
  // and raise an ArrayStoreException as appropriate.
  if (bjvm_instanceof(src->descriptor->one_fewer_dim, dest->descriptor->one_fewer_dim)) {
    memcpy(array_data(dest) + dest_pos, array_data(src) + src_pos, length * sizeof(void *));
    return value_null();
  }

  for (int i = 0; i < length; ++i) {
    bjvm_obj_header *src_elem = ((bjvm_obj_header **)array_data(src))[src_pos + i];
    if (src_elem && !bjvm_instanceof(src_elem->descriptor, dest->descriptor->one_fewer_dim)) {
      bjvm_array_store_exception(thread);
      return value_null();
    }
    ((bjvm_obj_header **)array_data(dest))[dest_pos + i] = src_elem;
  }

  return value_null();
}

bjvm_stack_value bjvm_System_setOut(bjvm_thread *thread, bjvm_obj_header *,
                       bjvm_stack_value *args, int) {
  // Look up the field System.out
  bjvm_classdesc *system_class =
      bootstrap_class_create(thread, L"java/lang/System");
  bjvm_cp_field *out_field =
      bjvm_easy_field_lookup(system_class, L"out", L"Ljava/io/PrintStream;");

  void *field = &system_class->static_fields[out_field->byte_offset];
  *(bjvm_obj_header **)field = args[0].obj;

  return value_null();
}

bjvm_stack_value bjvm_Object_clone(bjvm_thread *thread, bjvm_obj_header *obj,
                      bjvm_stack_value *, int) {
  switch (obj->descriptor->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY: {
    bjvm_array_classdesc *array_desc = (bjvm_array_classdesc *)obj->descriptor;
    bjvm_obj_header *new_array = create_object_array(
        thread, array_desc->base.base_component, *array_length(obj));
    if (new_array) {
      memcpy(array_data(new_array), array_data(obj),
             *array_length(obj) * sizeof(void *));
    }
    return (bjvm_stack_value) { .obj = new_array };
  }
  case BJVM_CD_KIND_BYTE_ARRAY:
    break;
  case BJVM_CD_KIND_CHAR_ARRAY:
    break;
  case BJVM_CD_KIND_DOUBLE_ARRAY:
    break;
  case BJVM_CD_KIND_FLOAT_ARRAY:
    break;
  case BJVM_CD_KIND_INT_ARRAY:
    break;
  case BJVM_CD_KIND_LONG_ARRAY:
    break;
  case BJVM_CD_KIND_SHORT_ARRAY:
    break;
  case BJVM_CD_KIND_BOOLEAN_ARRAY:
    break;
  case BJVM_CD_KIND_ORDINARY:
    break;
  }
  UNREACHABLE();  // TODO implement
  return value_null();
}

// Get the default hash code of an Object, which is stored in its mark word
bjvm_stack_value bjvm_Object_hashCode(bjvm_thread *, bjvm_obj_header *obj,
                         bjvm_stack_value *, int) {
  return (bjvm_stack_value) { .i = (int)obj->mark_word };
}

bjvm_stack_value bjvm_Class_getPrimitiveClass(bjvm_thread * thread, bjvm_obj_header *,
                                 bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  short *chars;
  size_t len;
  read_string(args[0].obj, &chars, &len);
  if (len > 10) {
    return value_null();
  }
  char as_cstr[11] = {0};
  for (size_t i = 0; i < len; ++i) {
    as_cstr[i] = chars[i];
  }
  bjvm_type_kind kind;
  if (strcmp(as_cstr, "boolean") == 0) {
    kind = BJVM_TYPE_KIND_BOOLEAN;
  } else if (strcmp(as_cstr, "byte") == 0) {
    kind = BJVM_TYPE_KIND_BYTE;
  } else if (strcmp(as_cstr, "char") == 0) {
    kind = BJVM_TYPE_KIND_CHAR;
  } else if (strcmp(as_cstr, "short") == 0) {
    kind = BJVM_TYPE_KIND_SHORT;
  } else if (strcmp(as_cstr, "int") == 0) {
    kind = BJVM_TYPE_KIND_INT;
  } else if (strcmp(as_cstr, "long") == 0) {
    kind = BJVM_TYPE_KIND_LONG;
  } else if (strcmp(as_cstr, "float") == 0) {
    kind = BJVM_TYPE_KIND_FLOAT;
  } else if (strcmp(as_cstr, "double") == 0) {
    kind = BJVM_TYPE_KIND_DOUBLE;
  } else if (strcmp(as_cstr, "void") == 0) {
    kind = BJVM_TYPE_KIND_VOID;
  } else {
    return value_null();
  }

  return (bjvm_stack_value) { .obj = (void*)bjvm_primitive_class_mirror(thread, kind) };
}

bjvm_stack_value bjvm_Class_getComponentType(bjvm_thread *, bjvm_obj_header * obj,
                                 bjvm_stack_value*, int) {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  if (desc->kind == BJVM_CD_KIND_ORDINARY) {
    return value_null();
  }
  return  (bjvm_stack_value) { .obj = (void*)desc->one_fewer_dim->mirror };
}

bjvm_stack_value bjvm_Class_getModifiers(bjvm_thread *, bjvm_obj_header *obj,
                            bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = classdesc->access_flags };
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                       bjvm_classdesc *classdesc);

bjvm_stack_value bjvm_Object_getClass(bjvm_thread *thread, bjvm_obj_header *obj,
                         bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .obj = (void*)bjvm_get_class_mirror(thread, obj->descriptor) };
}

bjvm_stack_value bjvm_Class_getSuperclass(bjvm_thread *, bjvm_obj_header *obj,
                             bjvm_stack_value *, int) {

  return value_null(); // TODO
}

bjvm_stack_value bjvm_Class_getClassLoader(bjvm_thread *, bjvm_obj_header *,
                              bjvm_stack_value *, int) {
  return value_null();  // TODO
}

bjvm_obj_header *make_string(bjvm_thread *thread, const wchar_t *chars, int len);

bjvm_stack_value bjvm_Class_getName(bjvm_thread *thread, bjvm_obj_header *obj,
                       bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .obj = bjvm_intern_string(thread, classdesc->name.chars, classdesc->name.len) };
}

// Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/Class; static
bjvm_stack_value bjvm_Class_forName(bjvm_thread *thread, bjvm_obj_header *,
                       bjvm_stack_value *args, int) {
  // Read args[0] as a string
  bjvm_obj_header *name_obj = args[0].obj;
  short *name;
  size_t len;
  read_string(name_obj, &name, &len);
  wchar_t *name_wchar = calloc(len + 1, sizeof(wchar_t));
  for (size_t i = 0; i < len; ++i) {
    name_wchar[i] = name[i] == '.' ? '/' : name[i];
  }
  bjvm_classdesc *c = bootstrap_class_create(thread, name_wchar);
  if (c) {
    return (bjvm_stack_value){.obj = (void*)bjvm_get_class_mirror(thread, c)};
  }
  return value_null();
}

bjvm_stack_value bjvm_Class_desiredAssertionStatus(bjvm_thread *, bjvm_obj_header *,
                                      bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 };  // TODO add thread option
}

bjvm_classdesc *load_class_of_field_descriptor(bjvm_thread *thread,
                                               const wchar_t *chars) {
  if (chars[0] == 'L') {
    wchar_t *cow = wcsdup(chars);
    cow[wcslen(chars) - 1] = L'\0';
    return bootstrap_class_create(thread, cow + 1);
  }
  if (chars[0] == '[')
    return bootstrap_class_create(thread, chars);
  return NULL; // TODO
}

void bjvm_reflect_initialize_field(bjvm_thread *thread,
                                   bjvm_classdesc *classdesc,
                                   bjvm_cp_field *field) {
  bjvm_classdesc *reflect_Field =
      bootstrap_class_create(thread, L"java/lang/reflect/Field");
  bjvm_initialize_class(thread, reflect_Field);
  struct bjvm_native_Field* result = field->reflection_field =
      (void*)new_object(thread, reflect_Field);

  result->reflected_field = field;
  result->name = bjvm_intern_string(thread, field->name->chars, field->name->len);
  result->clazz = (void*)bjvm_get_class_mirror(thread, classdesc);
  result->type = (void*)bjvm_get_class_mirror(
      thread, load_class_of_field_descriptor(thread, field->descriptor->chars));
  result->modifiers = field->access_flags;
}

void bjvm_reflect_initialize_constructor(bjvm_thread *thread,
                                         bjvm_classdesc *classdesc,
                                         bjvm_cp_method *method) {
  assert(utf8_equals(method->name, "<init>"));

  bjvm_classdesc *reflect_Constructor =
      bootstrap_class_create(thread, L"java/lang/reflect/Constructor");
  bjvm_initialize_class(thread, reflect_Constructor);

  struct bjvm_native_Constructor *result = method->reflection_ctor =
      (void*)new_object(thread, reflect_Constructor);
  result->reflected_ctor = method;
  result->clazz = (void*)bjvm_get_class_mirror(thread, classdesc);
  result->modifiers = method->access_flags;

  // TODO fill these in
  result->parameterTypes = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/Class"),
      method->parsed_descriptor->args_count);
}

bjvm_stack_value bjvm_Class_getDeclaredFields(bjvm_thread *thread, bjvm_obj_header *obj,
                                 bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  bjvm_stack_value ret;
  ret.obj = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/reflect/Field"),
      classdesc->fields_count);

  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_reflect_initialize_field(thread, classdesc, classdesc->fields + i);
    *((struct bjvm_native_Field**)array_data(ret.obj) + i) =
        classdesc->fields[i].reflection_field;
  }
  return ret;
}

bjvm_stack_value bjvm_Class_getDeclaredConstructors(bjvm_thread *thread,
                                       bjvm_obj_header *obj,
                                       bjvm_stack_value *, int) {
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);

  int count = 0;
  for (int i = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      bjvm_reflect_initialize_constructor(thread, classdesc,
                                          classdesc->methods + i);
      ++count;
    }
  }

  bjvm_stack_value ret;
  ret.obj = create_object_array(
      thread, bootstrap_class_create(thread, L"java/lang/reflect/Constructor"),
      count);
  for (int i = 0, j = 0; i < classdesc->methods_count; ++i) {
    if (utf8_equals(classdesc->methods[i].name, "<init>")) {
      *((struct bjvm_native_Constructor **)array_data(ret.obj) + j++) =
          classdesc->methods[i].reflection_ctor;
    }
  }
  return ret;
}

bjvm_stack_value bjvm_Class_isAssignableFrom(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 }; // TODO
}

bjvm_stack_value bjvm_Class_isArray(bjvm_thread *, bjvm_obj_header * obj,
                                bjvm_stack_value *, int) {
  bjvm_classdesc *desc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = desc->kind != BJVM_CD_KIND_ORDINARY };
}

bjvm_stack_value bjvm_Float_floatToRawIntBits(bjvm_thread *, bjvm_obj_header *,
                                 bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .i = args[0].i };
}

bjvm_stack_value bjvm_Double_doubleToRawLongBits(bjvm_thread *, bjvm_obj_header *,
                                    bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .l = args[0].l };
}

bjvm_stack_value bjvm_Unsafe_arrayBaseOffset(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 24 };
}

// (Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z
bjvm_stack_value bjvm_Unsafe_compareAndSwapObject(bjvm_thread *, bjvm_obj_header *,
                                     bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 }; // TODO
}

bjvm_stack_value bjvm_Unsafe_objectFieldOffset(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  bjvm_cp_field *reflect_field = *bjvm_unmirror_field(args[0].obj);
  return  (bjvm_stack_value) { .l = reflect_field->byte_offset };
}

bjvm_stack_value bjvm_Unsafe_getIntVolatile(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int argc) {
  assert(argc == 2);
  return  (bjvm_stack_value) { .i = *(int *)((char *)args[0].obj + args[1].l) };
}

// (Ljava/lang/Object;JII)Z
bjvm_stack_value bjvm_Unsafe_compareAndSwapInt(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *args, int argc) {
  assert(argc == 4);
  bjvm_obj_header *obj = args[0].obj;
  int64_t offset = args[1].l;
  int expected = args[2].i, update = args[3].i;
  int ret = __sync_bool_compare_and_swap((int *)((char *)obj + offset), expected,
                                        update);
  return  (bjvm_stack_value) { .i = ret };
}

bjvm_stack_value bjvm_Unsafe_arrayIndexScale(bjvm_thread *, bjvm_obj_header *,
                                bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  bjvm_classdesc *desc = bjvm_unmirror_class(args[0].obj);
  switch (desc->kind) {
  case BJVM_CD_KIND_ORDINARY_ARRAY:
    return  (bjvm_stack_value) { .i = sizeof(void *) };
  case BJVM_CD_KIND_BYTE_ARRAY:
  case BJVM_CD_KIND_CHAR_ARRAY:
  case BJVM_CD_KIND_FLOAT_ARRAY:
  case BJVM_CD_KIND_SHORT_ARRAY:
  case BJVM_CD_KIND_BOOLEAN_ARRAY:
  case BJVM_CD_KIND_INT_ARRAY:
    return  (bjvm_stack_value) { .i = 4 };
  case BJVM_CD_KIND_DOUBLE_ARRAY:
  case BJVM_CD_KIND_LONG_ARRAY:
    return  (bjvm_stack_value) { .i = 8 };
  case BJVM_CD_KIND_ORDINARY:
  default:  // invalid
    return  (bjvm_stack_value) { .i = 0 };
  }
}

bjvm_stack_value bjvm_Unsafe_addressSize(bjvm_thread *, bjvm_obj_header *,
                            bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = sizeof(void*) };
}

bjvm_stack_value bjvm_Unsafe_allocateMemory(bjvm_thread *, bjvm_obj_header *,
                               bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return  (bjvm_stack_value) { .l = (int64_t)malloc(args[0].l) };
}

bjvm_stack_value bjvm_Unsafe_freeMemory(bjvm_thread *, bjvm_obj_header *,
                           bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  free((void *)args[0].l);
  return value_null();
}

bjvm_stack_value bjvm_Unsafe_putLong(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *args, int argc) {
  assert(argc == 2);
  *(int64_t *)args[0].l = args[1].l;
  return value_null();
}

bjvm_stack_value bjvm_Unsafe_getByte(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return  (bjvm_stack_value) { .i = *(int8_t *)args[0].l };
}

bjvm_stack_value bjvm_AtomicLong_VMSupportsCS8(bjvm_thread *, bjvm_obj_header *,
                                  bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 1 };
}

bjvm_stack_value bjvm_FileDescriptor_set(bjvm_thread *, bjvm_obj_header *,
                            bjvm_stack_value *args, int) {
  return  (bjvm_stack_value) { .l = args[0].i };
}

bjvm_stack_value bjvm_Reflection_getCallerClass(bjvm_thread *thread, bjvm_obj_header *,
                                   bjvm_stack_value *, int) {
  // Look at frame before latest frame
  if (thread->frames_count < 2) {
    return value_null();
  }
  bjvm_stack_frame *frame = thread->frames[thread->frames_count - 2];
  return  (bjvm_stack_value) { .obj = (void*) bjvm_get_class_mirror(thread, frame->method->my_class) };
}

bjvm_stack_value bjvm_Reflection_getClassAccessFlags(bjvm_thread *, bjvm_obj_header *,
                                        bjvm_stack_value *args, int) {
  bjvm_obj_header *obj = args[0].obj;
  bjvm_classdesc *classdesc = bjvm_unmirror_class(obj);
  return  (bjvm_stack_value) { .i = classdesc->access_flags };
}

bjvm_stack_value bjvm_NativeConstructorAccessImpl_newInstance(bjvm_thread *thread,
                                                 bjvm_obj_header *,
                                                 bjvm_stack_value *args, int) {
  bjvm_cp_method *method = *bjvm_unmirror_ctor(args[0].obj);
  bjvm_stack_value result;
  if (bjvm_initialize_class(thread, method->my_class))
    return value_null();
  bjvm_obj_header *obj = new_object(thread, method->my_class);

  bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}}, &result);
  return  (bjvm_stack_value) { .obj = obj };
}

bjvm_stack_value bjvm_Thread_currentThread(bjvm_thread *thread, bjvm_obj_header *,
                              bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .obj = (void*) thread->thread_obj };
}

bjvm_stack_value bjvm_Thread_isAlive(bjvm_thread *, bjvm_obj_header *,
                        bjvm_stack_value *, int) {
  return  (bjvm_stack_value) { .i = 0 }; // TODO
}

bjvm_stack_value bjvm_Thread_start(bjvm_thread *, bjvm_obj_header *,
                      bjvm_stack_value *, int) {
  return value_null(); // TODO
}

bjvm_stack_value bjvm_Throwable_fillInStackTrace(bjvm_thread *, bjvm_obj_header *,
                                    bjvm_stack_value *args, int) {
  return args[0];  // TODO
}

bjvm_stack_value bjvm_AccessController_doPrivileged(bjvm_thread *thread, bjvm_obj_header *,
                                       bjvm_stack_value *args, int argc) {
  // Look up method "run" on obj
  assert(argc == 1);

  bjvm_obj_header *obj = args[0].obj;
  bjvm_classdesc *classdesc = obj->descriptor;

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  bjvm_cp_method *method =
      bjvm_easy_method_lookup(classdesc, "run", NULL, true, true);
  if (!method) {
    // TODO figure out what JVM normally does here
    UNREACHABLE();
  }

  bjvm_stack_value method_args[1] = {(bjvm_stack_value){.obj = obj}};
  bjvm_stack_value ret;
  bjvm_thread_run(thread, method, method_args, &ret);
  return ret;
}

bjvm_obj_header *bjvm_intern_string(bjvm_thread *thread, const wchar_t *chars,
                                    size_t len) {
  bjvm_obj_header *str =
      bjvm_hash_table_lookup(&thread->vm->interned_strings, chars, len);
  if (str)
    return str;
  bjvm_obj_header *new_str = make_string(thread, chars, len);
  (void)bjvm_hash_table_insert(&thread->vm->interned_strings, chars, len,
                               new_str);
  return new_str;
}

void bjvm_raise_exception_object(bjvm_thread *thread, bjvm_obj_header *obj) {
  thread->current_exception = obj;
}

// Helper function to raise VM-generated exceptions
int bjvm_raise_exception(bjvm_thread *thread, const wchar_t *exception_name,
                         const wchar_t *exception_string) {
  bjvm_classdesc *classdesc = bootstrap_class_create(thread, exception_name);
  bjvm_initialize_class(thread, classdesc);

  // Create the exception object
  bjvm_obj_header *obj = new_object(thread, classdesc);
  if (exception_string) {
    bjvm_obj_header *str = make_string(thread, exception_string, -1);
    bjvm_cp_method *method = bjvm_easy_method_lookup(
        classdesc, "<init>", "(Ljava/lang/String;)V", true, false);
    bjvm_thread_run(thread, method,
                    (bjvm_stack_value[]){{.obj = obj}, {.obj = str}}, NULL);
  } else {
    bjvm_cp_method *method =
        bjvm_easy_method_lookup(classdesc, "<init>", "()V", true, false);
    bjvm_thread_run(thread, method, (bjvm_stack_value[]){{.obj = obj}}, NULL);
  }

#ifndef EMSCRIPTEN
  wprintf(L"Exception: %S: %S\n", exception_name, exception_string);
#endif
  bjvm_raise_exception_object(thread, obj);
  return 0;
}

struct bjvm_native_Class * bjvm_primitive_class_mirror(bjvm_thread *thread, bjvm_type_kind prim_kind) {
  bjvm_vm *vm = thread->vm;
  return vm->primitive_classes[prim_kind - BJVM_TYPE_KIND_BOOLEAN];
}

bjvm_classdesc *bjvm_make_primitive_classdesc(const wchar_t* name, struct bjvm_native_Class* mirror) {
  bjvm_classdesc *desc = calloc(1, sizeof(bjvm_classdesc));

  desc->kind = BJVM_CD_KIND_ORDINARY;
  desc->super_class = NULL;
  desc->name = bjvm_make_utf8(name);
  desc->access_flags = 0x411;  // result of int.class.getModifiers()
  desc->array_type = NULL;  // TODO
  desc->mirror = mirror;

  return desc;
}

void bjvm_vm_init_primitive_classes(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (vm->primitive_classes[0]) return; // already initialized

  bjvm_classdesc *desc = bootstrap_class_create(thread, L"java/lang/Class");
  assert(desc->state == BJVM_CD_STATE_INITIALIZED);

  int offs = BJVM_TYPE_KIND_BOOLEAN;
  struct bjvm_native_Class *Boolean, *Byte, *Char, *Short, *Int, *Long, *Float, *Double, *Void;
  Boolean = vm->primitive_classes[BJVM_TYPE_KIND_BOOLEAN - offs] = (void*)new_object(thread, desc);
  Byte = vm->primitive_classes[BJVM_TYPE_KIND_BYTE - offs] = (void*)new_object(thread, desc);
  Char = vm->primitive_classes[BJVM_TYPE_KIND_CHAR - offs] = (void*)new_object(thread, desc);
  Short = vm->primitive_classes[BJVM_TYPE_KIND_SHORT - offs] = (void*)new_object(thread, desc);
  Int = vm->primitive_classes[BJVM_TYPE_KIND_INT - offs] = (void*)new_object(thread, desc);
  Long = vm->primitive_classes[BJVM_TYPE_KIND_LONG - offs] = (void*)new_object(thread, desc);
  Float = vm->primitive_classes[BJVM_TYPE_KIND_FLOAT - offs] = (void*)new_object(thread, desc);
  Double = vm->primitive_classes[BJVM_TYPE_KIND_DOUBLE - offs] = (void*)new_object(thread, desc);
  Void = vm->primitive_classes[BJVM_TYPE_KIND_VOID - offs] = (void*)new_object(thread, desc);

  Boolean->reflected_class = bjvm_make_primitive_classdesc(L"boolean", Boolean);
  Byte->reflected_class = bjvm_make_primitive_classdesc(L"byte", Byte);
  Char->reflected_class = bjvm_make_primitive_classdesc(L"char", Char);
  Short->reflected_class = bjvm_make_primitive_classdesc(L"short", Short);
  Int->reflected_class = bjvm_make_primitive_classdesc(L"int", Int);
  Long->reflected_class = bjvm_make_primitive_classdesc(L"long", Long);
  Float->reflected_class = bjvm_make_primitive_classdesc(L"float", Float);
  Double->reflected_class = bjvm_make_primitive_classdesc(L"double", Double);
  Void->reflected_class = bjvm_make_primitive_classdesc(L"void", Void);
}

bjvm_stack_value bjvm_String_intern(bjvm_thread *thread, bjvm_obj_header *obj,
                       bjvm_stack_value *, int) {
  if (obj == NULL) {
    bjvm_raise_exception(thread, L"NullPointerException", NULL);
    return value_null();
  }
  short *buf;
  size_t len;
  read_string(obj, &buf, &len);
  wchar_t *data = malloc((len + 1) * sizeof(wchar_t));
  for (size_t i = 0; i < len; ++i)
    data[i] = buf[i];
  data[len] = 0;
  bjvm_stack_value result;
  result.obj = bjvm_intern_string(thread, data, len);
  free(data);
  return result;
}

bjvm_stack_value bjvm_FileOutputStream_writeBytes(bjvm_thread *thread, bjvm_obj_header *,
                                     bjvm_stack_value *args, int) {
  bjvm_obj_header *bytes = args[0].obj;
  int offset = args[1].i;
  int length = args[2].i;
  assert(bytes->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
  char *data = (char *)array_data(bytes);
  for (int i = 0; i < length; ++i) {
    if (thread->vm->write_stdout)
      thread->vm->write_stdout(data[offset + i], thread->vm->write_byte_param);
    else
      fprintf(stderr, "%c", data[offset + i]);
  }
  return (bjvm_stack_value) { .i = 0 };
}

/** Begin StrictMath natives */

bjvm_stack_value bjvm_StrictMath_log(bjvm_thread *, bjvm_obj_header*,
                       bjvm_stack_value *args, int argc) {
  assert(argc == 1);
  return (bjvm_stack_value) { .d = log(args[0].d) };
}

// Natives for the java.lang.StrictMath class.
void bjvm_register_natives_StrictMath(bjvm_vm *vm) {
  bjvm_register_native(vm, "java/lang/StrictMath", "log", "(D)D", bjvm_StrictMath_log);
}

/** End StrictMath natives, begin Class natives */

// Natives for the java.lang.Class class.
void bjvm_register_natives_Class(bjvm_vm *vm) {

}

bjvm_vm *bjvm_create_vm(bjvm_vm_options options) {
  bjvm_vm *vm = calloc(1, sizeof(bjvm_vm));

  vm->load_classfile = options.load_classfile;
  vm->load_classfile_param = options.load_classfile_param;

  vm->classfiles = bjvm_make_hash_table(free_cf_entry, 0.75, 16);
  vm->classes = bjvm_make_hash_table(free_classdesc, 0.75, 16);
  vm->inchoate_classes = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->natives = bjvm_make_hash_table(free_native_entries, 0.75, 16);
  vm->interned_strings = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->class_padding = bjvm_make_hash_table(NULL, 0.75, 16);
  vm->main_thread_group = NULL;

  vm->write_stdout = options.write_stdout;
  vm->write_stderr = options.write_stderr;
  vm->write_byte_param = options.write_byte_param;

  bjvm_register_native_padding(vm);

  bjvm_register_natives_StrictMath(vm);

  bjvm_register_native(vm, "java/lang/System", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/System", "mapLibraryName",
                       "(Ljava/lang/String;)Ljava/lang/String;",
                       bjvm_System_mapLibraryName);
  bjvm_register_native(vm, "java/lang/System", "initProperties",
                       "(Ljava/util/Properties;)Ljava/util/Properties;",
                       bjvm_System_initProperties);
  bjvm_register_native(vm, "java/lang/System", "arraycopy",
                       "(Ljava/lang/Object;ILjava/lang/Object;II)V",
                       bjvm_System_arraycopy);
  bjvm_register_native(vm, "java/lang/System", "setIn0",
                       "(Ljava/io/InputStream;)V", unimplemented_native);
  bjvm_register_native(vm, "java/lang/System", "setOut0",
                       "(Ljava/io/PrintStream;)V", bjvm_System_setOut);
  bjvm_register_native(vm, "java/lang/System", "setErr0",
                       "(Ljava/io/PrintStream;)V", unimplemented_native);
  bjvm_register_native(vm, "java/util/concurrent/atomic/AtomicLong",
                       "VMSupportsCS8", "()Z", bjvm_AtomicLong_VMSupportsCS8);
  bjvm_register_native(vm, "java/lang/String", "intern", "()Ljava/lang/String;",
                       bjvm_String_intern);
  bjvm_register_native(vm, "java/lang/Object", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Object", "clone", "()Ljava/lang/Object;",
                       bjvm_Object_clone);
  bjvm_register_native(vm, "java/lang/Class", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "getPrimitiveClass",
                       "(Ljava/lang/String;)Ljava/lang/Class;",
                       bjvm_Class_getPrimitiveClass);
  bjvm_register_native(vm, "java/lang/Class", "getComponentType",
                       "()Ljava/lang/Class;",
                       bjvm_Class_getComponentType);
  bjvm_register_native(vm, "java/lang/Class", "getModifiers", "()I",
                       bjvm_Class_getModifiers);
  bjvm_register_native(vm, "java/lang/Object", "getClass",
                       "()Ljava/lang/Class;", bjvm_Object_getClass);
  bjvm_register_native(vm, "java/lang/Class", "getSuperclass",
                       "()Ljava/lang/Class;", bjvm_Class_getSuperclass);
  bjvm_register_native(vm, "java/lang/Class", "getClassLoader",
                       "()Ljava/lang/ClassLoader;", bjvm_Class_getClassLoader);
  bjvm_register_native(vm, "java/lang/Class", "getName0",
                       "()Ljava/lang/String;", bjvm_Class_getName);
  bjvm_register_native(vm, "java/lang/Class", "forName0",
                       "(Ljava/lang/String;ZLjava/lang/ClassLoader;Ljava/lang/"
                       "Class;)Ljava/lang/Class;",
                       bjvm_Class_forName);
  bjvm_register_native(vm, "java/lang/Class", "desiredAssertionStatus0",
                       "(Ljava/lang/Class;)Z",
                       bjvm_Class_desiredAssertionStatus);
  bjvm_register_native(vm, "java/lang/Class", "getDeclaredFields0",
                       "(Z)[Ljava/lang/reflect/Field;",
                       bjvm_Class_getDeclaredFields);
  bjvm_register_native(vm, "java/lang/Class", "getDeclaredConstructors0",
                       "(Z)[Ljava/lang/reflect/Constructor;",
                       bjvm_Class_getDeclaredConstructors);
  bjvm_register_native(vm, "java/lang/Class", "isPrimitive", "()Z",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "isInterface", "()Z",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Class", "isAssignableFrom",
  "(Ljava/lang/Class;)Z", bjvm_Class_isAssignableFrom);
  bjvm_register_native(vm, "java/lang/Class", "isArray",
                       "()Z", bjvm_Class_isArray);
  bjvm_register_native(vm, "java/lang/ClassLoader", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Float", "floatToRawIntBits", "(F)I",
                       bjvm_Float_floatToRawIntBits);
  bjvm_register_native(vm, "java/lang/Double", "doubleToRawLongBits", "(D)J",
                       bjvm_Double_doubleToRawLongBits);
  bjvm_register_native(vm, "java/lang/Double", "longBitsToDouble", "(J)D",
                       bjvm_Double_doubleToRawLongBits);
  bjvm_register_native(vm, "java/io/FileInputStream", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/WinNTFileSystem", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileOutputStream", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileOutputStream", "writeBytes", "([BIIZ)V",
                       bjvm_FileOutputStream_writeBytes);
  bjvm_register_native(vm, "java/io/FileDescriptor", "initIDs", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/io/FileDescriptor", "set", "(I)J",
                       bjvm_FileDescriptor_set);

  bjvm_register_native(
      vm, "java/security/AccessController", "doPrivileged",
      "(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;",
      bjvm_AccessController_doPrivileged);
  bjvm_register_native(
      vm, "java/security/AccessController", "getStackAccessControlContext",
      "()Ljava/security/AccessControlContext;",
      unimplemented_native);
  bjvm_register_native(vm, "java/security/AccessController", "doPrivileged",
                       "(Ljava/security/PrivilegedAction;)Ljava/lang/Object;",
                       bjvm_AccessController_doPrivileged);

  bjvm_register_native(vm, "sun/misc/VM", "initialize", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "sun/misc/Unsafe", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "sun/misc/Unsafe", "arrayBaseOffset",
                       "(Ljava/lang/Class;)I", bjvm_Unsafe_arrayBaseOffset);
  bjvm_register_native(
      vm, "sun/misc/Unsafe", "compareAndSwapObject",
      "(Ljava/lang/Object;JLjava/lang/Object;Ljava/lang/Object;)Z",
      bjvm_Unsafe_compareAndSwapObject);
  bjvm_register_native(vm, "sun/misc/Unsafe", "objectFieldOffset",
                       "(Ljava/lang/reflect/Field;)J",
                       bjvm_Unsafe_objectFieldOffset);
  bjvm_register_native(vm, "sun/misc/Unsafe", "arrayIndexScale",
                       "(Ljava/lang/Class;)I", bjvm_Unsafe_arrayIndexScale);
  bjvm_register_native(vm, "sun/misc/Unsafe", "getIntVolatile",
                       "(Ljava/lang/Object;J)I", bjvm_Unsafe_getIntVolatile);
  bjvm_register_native(vm, "sun/misc/Unsafe", "compareAndSwapInt",
                       "(Ljava/lang/Object;JII)Z",
                       bjvm_Unsafe_compareAndSwapInt);
  bjvm_register_native(vm, "sun/misc/Unsafe", "addressSize", "()I",
                       bjvm_Unsafe_addressSize);
  bjvm_register_native(vm, "sun/misc/Unsafe", "allocateMemory", "(J)J",
                       bjvm_Unsafe_allocateMemory);
  bjvm_register_native(vm, "sun/misc/Unsafe", "freeMemory", "(J)V",
                       bjvm_Unsafe_freeMemory);
  bjvm_register_native(vm, "sun/misc/Unsafe", "putLong", "(JJ)V",
                       bjvm_Unsafe_putLong);
  bjvm_register_native(vm, "sun/misc/Unsafe", "getByte", "(J)B",
                       bjvm_Unsafe_getByte);
  bjvm_register_native(vm, "sun/reflect/Reflection", "getCallerClass",
                       "()Ljava/lang/Class;", bjvm_Reflection_getCallerClass);
  bjvm_register_native(vm, "sun/reflect/Reflection", "getClassAccessFlags",
                       "(Ljava/lang/Class;)I",
                       bjvm_Reflection_getClassAccessFlags);
  bjvm_register_native(
      vm, "sun/reflect/NativeConstructorAccessorImpl", "newInstance0",
      "(Ljava/lang/reflect/Constructor;[Ljava/lang/Object;)Ljava/lang/Object;",
      bjvm_NativeConstructorAccessImpl_newInstance);
  bjvm_register_native(vm, "java/lang/Object", "hashCode", "()I",
                       bjvm_Object_hashCode);

  bjvm_register_native(vm, "java/lang/Thread", "registerNatives", "()V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Thread", "currentThread",
                       "()Ljava/lang/Thread;", bjvm_Thread_currentThread);
  bjvm_register_native(vm, "java/lang/Thread", "setPriority0", "(I)V",
                       unimplemented_native);
  bjvm_register_native(vm, "java/lang/Thread", "isAlive", "()Z",
                       bjvm_Thread_isAlive);
  bjvm_register_native(vm, "java/lang/Thread", "start0", "()V",
                       bjvm_Thread_start);
  bjvm_register_native(vm, "java/lang/Throwable", "fillInStackTrace",
                       "(I)Ljava/lang/Throwable;",
                       bjvm_Throwable_fillInStackTrace);

  return vm;
}

void bjvm_free_vm(bjvm_vm *vm) {
  bjvm_free_hash_table(vm->classfiles);
  bjvm_free_hash_table(vm->classes);
  bjvm_free_hash_table(vm->inchoate_classes);
  bjvm_free_hash_table(vm->interned_strings);
  free(vm);
}

bjvm_thread_options bjvm_default_thread_options() {
  bjvm_thread_options options = {};
  options.stack_space = 1 << 20;
  options.js_jit_enabled = true;
  options.thread_group = NULL;
  return options;
}

bjvm_cp_field *bjvm_field_lookup(bjvm_classdesc *classdesc, bjvm_utf8 *name,
                                 bjvm_utf8 *descriptor) {
  for (int i = 0; i < classdesc->fields_count; ++i) {
    bjvm_cp_field *field = classdesc->fields + i;
    if (utf8_equals_utf8(field->name, name) &&
        utf8_equals_utf8(field->descriptor, descriptor)) {
      return field;
    }
  }

  if (classdesc->super_class) {
    return bjvm_field_lookup(classdesc->super_class->classdesc, name,
                             descriptor);
  }

  return NULL;
}

bjvm_cp_field *bjvm_easy_field_lookup(bjvm_classdesc *classdesc,
                                      const wchar_t *name,
                                      const wchar_t *descriptor) {
  bjvm_utf8 name_utf8 = bjvm_make_utf8(name),
            descriptor_utf8 = bjvm_make_utf8(descriptor);
  bjvm_cp_field *result =
      bjvm_field_lookup(classdesc, &name_utf8, &descriptor_utf8);
  free_utf8(name_utf8);
  free_utf8(descriptor_utf8);
  return result;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread);

void store_stack_value(void *field_location, bjvm_stack_value value,
                       bjvm_type_kind kind);
bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind);

void bjvm_set_field(bjvm_obj_header *obj, bjvm_cp_field *field,
                    bjvm_stack_value bjvm_stack_value) {
  store_stack_value((char *)obj + field->byte_offset, bjvm_stack_value,
                    field_to_representable_kind(&field->parsed_descriptor));
}

bjvm_stack_value bjvm_get_field(bjvm_obj_header *obj, bjvm_cp_field *field) {
  return load_stack_value(
      (char *)obj + field->byte_offset,
      field_to_representable_kind(&field->parsed_descriptor));
}

bjvm_thread *bjvm_create_thread(bjvm_vm *vm, bjvm_thread_options options) {
  bjvm_thread *thr = calloc(1, sizeof(bjvm_thread));
  thr->vm = vm;
  thr->frame_buffer =
      calloc(1, thr->frame_buffer_capacity = options.stack_space);
  thr->js_jit_enabled = options.js_jit_enabled;

  bjvm_classdesc *desc;

  // Link (but don't initialize) java.lang.Class immediately
  desc = bootstrap_class_create(thr, L"java/lang/Class");
  bjvm_initialize_class(thr, desc);

  bjvm_vm_init_primitive_classes(thr);

  desc = bootstrap_class_create(thr, L"java/lang/reflect/Field");
  desc = bootstrap_class_create(thr, L"java/lang/reflect/Constructor");

  // Initialize java.lang.Thread mirror
  desc = bootstrap_class_create(thr, L"java/lang/Thread");
  bjvm_initialize_class(thr, desc);

  struct bjvm_native_Thread *thread_obj = (void*)new_object(thr, desc);
  thr->thread_obj = thread_obj;

  thread_obj->vm_thread = thr;
  thread_obj->priority = 5;
  thread_obj->name = bjvm_intern_string(thr, L"main", -1);

  bjvm_obj_header *main_thread_group = options.thread_group;
  if (!main_thread_group) {
    main_thread_group = get_main_thread_group(thr);
  }

  // Call (Ljava/lang/ThreadGroup;Ljava/lang/String;)V
  bjvm_cp_method *make_thread = bjvm_easy_method_lookup(
      desc, "<init>", "(Ljava/lang/ThreadGroup;Ljava/lang/String;)V", false,
      false);
  bjvm_obj_header *name = make_string(thr, L"main", -1);
  bjvm_thread_run(thr, make_thread,
                  (bjvm_stack_value[]){{.obj = (void*)thread_obj},
                                       {.obj = main_thread_group},
                                       {.obj = name}},
                  NULL);

  // Call System.initializeSystemClass()
  desc = bootstrap_class_create(thr, L"java/lang/System");
  bjvm_initialize_class(thr, desc);

  bjvm_cp_method *method = bjvm_easy_method_lookup(
      desc, "initializeSystemClass", "()V", false, false);
  bjvm_stack_value ret;
  bjvm_thread_run(thr, method, NULL, &ret);

  thr->current_exception = NULL;

  return thr;
}

void bjvm_free_thread(bjvm_thread *thread) {
  // TODO what happens to ->current_exception etc.?
  free(thread->frame_buffer);
  free(thread);
}

int bjvm_vm_preregister_classfile(bjvm_vm *vm, const wchar_t *filename,
                                  const uint8_t *bytes, size_t len) {
  return add_classfile_bytes(vm, filename,
                             wcslen(filename), bytes, len);
}

int bjvm_vm_read_classfile(bjvm_vm *vm, const wchar_t *filename,
                           const uint8_t **bytes, size_t *len) {
  struct classfile_entry* entry = bjvm_hash_table_lookup(&vm->classfiles, filename, wcslen(filename));
  if (entry) {
    if (bytes)
      *bytes = entry->data;
    if (len)
      *len = entry->len;
    return 0;
  }

  // Otherwise, try to read it from the vm->load_classfile implementation
  if (vm->load_classfile) {
    uint8_t *loaded_bytes = NULL;

    size_t mbs_len = wcslen(filename) * 4 + 1;
    char *as_mbs = malloc(mbs_len);
    wcstombs(as_mbs, filename, mbs_len);

    int status = vm->load_classfile(as_mbs, vm->load_classfile_param,
                                    &loaded_bytes, len);
    free(as_mbs);

    if (status) {
      free(loaded_bytes);
      return -1;
    }

    bjvm_vm_preregister_classfile(vm, filename, loaded_bytes, *len);
    free(loaded_bytes);
    return bjvm_vm_read_classfile(vm, filename, bytes, len);
  }

  return -1;
}

void bjvm_vm_list_classfiles(bjvm_vm *vm, wchar_t **strings, size_t *count) {
  *count = vm->classfiles.entries_count;
  if (strings) {
    bjvm_hash_table_iterator iter = bjvm_hash_table_get_iterator(&vm->classfiles);
    size_t key_len, i = 0;
    void* value;
    while (i < *count && bjvm_hash_table_iterator_has_next(iter, &strings[i], &key_len, &value)) {
      wchar_t* key = calloc(key_len + 1, sizeof(wchar_t));
      wmemcpy(key, strings[i], key_len);
      strings[i] = key;
      ++i;
      bjvm_hash_table_iterator_next(&iter);
    }
  }
}

// Called for both primitive and object arrays
void fill_array_classdesc(bjvm_thread *thread, bjvm_classdesc *base) {
  base->access_flags = BJVM_ACCESS_PUBLIC | BJVM_ACCESS_FINAL;

  bjvm_utf8 *java_lang_Object = malloc(sizeof(bjvm_utf8));
  *java_lang_Object = bjvm_make_utf8(L"java/lang/Object");
  bjvm_cp_class_info *info = calloc(1, sizeof(bjvm_cp_class_info));

  info->classdesc = bootstrap_class_create(thread, java_lang_Object->chars);
  info->name = java_lang_Object;
  base->super_class = info;
  base->fields_count = 1;

  bjvm_cp_field *fields = calloc(1, sizeof(bjvm_cp_field));
  base->fields = fields;
  fields->access_flags =
      BJVM_ACCESS_PUBLIC | BJVM_ACCESS_STATIC | BJVM_ACCESS_FINAL;

  bjvm_utf8 *length = malloc(sizeof(bjvm_utf8)), *I = malloc(sizeof(bjvm_utf8));
  *length = bjvm_make_utf8(L"length");
  *I = bjvm_make_utf8(L"I");
  fields->name = length;
  fields->descriptor = I;
}

bjvm_classdesc *primitive_arr_classdesc(bjvm_thread *thread, int dimensions,
                                        bjvm_classdesc_kind classdesc_kind,
                                        bjvm_type_kind prim_kind) {
  bjvm_primitive_array_classdesc *result =
      calloc(1, sizeof(bjvm_primitive_array_classdesc));
  bjvm_classdesc *base = &result->base;

  base->state = BJVM_CD_STATE_INITIALIZED;
  base->base_component = bjvm_primitive_class_mirror(thread, prim_kind)->reflected_class;

  base->kind = classdesc_kind;
  fill_array_classdesc(thread, base);

  result->dimensions = dimensions;
  result->element_type = prim_kind;

  if (dimensions > 1) {
    wchar_t one_fewer_name[MAX_CF_NAME_LENGTH] = { 0 };
    int i = 0;
    for (; i < dimensions - 1; ++i)
      one_fewer_name[i] = '[';
    const wchar_t* name;
    int size;
    primitive_type_kind_to_array_info(prim_kind, &name, &size);
    one_fewer_name[i] = name[1];
    result->base.one_fewer_dim = bootstrap_class_create(thread, one_fewer_name);
  } else {
    result->base.one_fewer_dim = base->base_component;
  }

  return (bjvm_classdesc *)result;
}

void free_base_arr_classdesc(bjvm_classdesc *base) {
  free_utf8(*base->fields->name);
  free_utf8(*base->fields->descriptor);
  free(base->fields->name);
  free(base->fields->descriptor);
  free(base->fields);
}

void free_primitive_arr_classdesc(bjvm_primitive_array_classdesc *desc) {
  bjvm_classdesc *base = &desc->base;
  free_base_arr_classdesc(base);
  free(desc);
}

bjvm_classdesc *ordinary_arr_classdesc(bjvm_thread *thread,
                                       bjvm_classdesc *base, int dimensions) {
  bjvm_array_classdesc *result = calloc(1, sizeof(bjvm_array_classdesc));
  result->base.kind = BJVM_CD_KIND_ORDINARY_ARRAY;
  result->base.state = base->state;
  fill_array_classdesc(thread, &result->base);

  result->dimensions = dimensions;
  if (base->kind == BJVM_CD_KIND_ORDINARY) {
    result->base.base_component = result->base.one_fewer_dim = base;
  } else {
    bjvm_array_classdesc *array_base = (bjvm_array_classdesc *)base;
    result->base.base_component = array_base->base.base_component;
    result->base.one_fewer_dim = (bjvm_classdesc*) array_base;
    assert(result->dimensions == array_base->dimensions + 1);
  }

  return (bjvm_classdesc *)result;
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info);

int bjvm_resolve_method_type(bjvm_thread *thread, bjvm_cp_method_type_info *info) {
  // Resolve each class in the arguments list, as well as the return type if it exists
  bjvm_method_descriptor *method = info->parsed_descriptor;
  assert(method);

  // TODO create reflected objects

  for (int i = 0; i < method->args_count; ++i) {
    bjvm_classdesc *desc = bootstrap_class_create(thread, method->args[i].class_name.chars);
    if (!desc) {
      // TODO
      UNREACHABLE();
    }
  }
  if (method->return_type.kind != BJVM_TYPE_KIND_VOID) {
    bjvm_classdesc *desc = bootstrap_class_create(thread, method->return_type.class_name.chars);
    if (!desc) {
      // TODO
      UNREACHABLE();
    }
  }

  return 0;
}

// name = "java/lang/Object" or "[[J" or "[Ljava/lang/String;"
bjvm_classdesc *bootstrap_class_create(bjvm_thread *thread,
                                       const wchar_t *name) {
  bjvm_vm *vm = thread->vm;

  int dimensions = 0;
  const wchar_t *chars = name;
  int len = wcslen(name), orig_len = len;
  while (len > 0 && *chars == '[') // munch '[' at beginning
    dimensions++, len--, chars++;

  assert(dimensions < 255);
  assert(len > 0);

  if (dimensions) { // array type
    if (*chars != 'L') {
      bjvm_classdesc *desc = bjvm_hash_table_lookup(&vm->classes, name, orig_len);
      if (desc)
        return desc;
      switch (*chars) {
      case 'J':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_LONG_ARRAY, BJVM_TYPE_KIND_LONG);
        break;
      case 'D':
        desc = primitive_arr_classdesc(thread, dimensions,
                                       BJVM_CD_KIND_DOUBLE_ARRAY,
                                       BJVM_TYPE_KIND_DOUBLE);
        break;
      case 'F':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_FLOAT_ARRAY, BJVM_TYPE_KIND_FLOAT);
        break;
      case 'I':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_INT_ARRAY, BJVM_TYPE_KIND_INT);
        break;
      case 'S':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_SHORT_ARRAY, BJVM_TYPE_KIND_SHORT);
        break;
      case 'B':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_BYTE_ARRAY, BJVM_TYPE_KIND_BYTE);
        break;
      case 'C':
        desc = primitive_arr_classdesc(
            thread, dimensions, BJVM_CD_KIND_CHAR_ARRAY, BJVM_TYPE_KIND_CHAR);
        break;
      case 'Z':
        desc = primitive_arr_classdesc(thread, dimensions,
                                       BJVM_CD_KIND_BOOLEAN_ARRAY,
                                       BJVM_TYPE_KIND_BOOLEAN);
        break;
      default:
        UNREACHABLE();
      }
      (void)bjvm_hash_table_insert(&vm->classes, name, orig_len, desc);
      return desc;
    }

    chars++;
    assert(len >= 3);
    len -= 2;
  }

  // Check whether the class is already loaded
  bjvm_classdesc *base_class = bjvm_hash_table_lookup(&vm->classes, chars, len);

  if (!base_class) {
    // Add entry to inchoate_classes
    (void)bjvm_hash_table_insert(&vm->inchoate_classes, chars, len, (void *)1);

    // e.g. "java/lang/Object.class"
    const wchar_t *cf_ending = L".class";
    wchar_t filename[MAX_CF_NAME_LENGTH + 7];
    wcscpy(filename, chars);
    wcscpy(filename + len, cf_ending);

    uint8_t *bytes;
    size_t cf_len;
    int read_status =
        bjvm_vm_read_classfile(vm, filename, (const uint8_t **)&bytes, &cf_len);
    if (read_status) {
      int i = 0;
      for (; i < len; ++i)
        filename[i] = filename[i] == '/' ? '.' : filename[i];
      filename[i] = L'\0';
      // ClassNotFoundException: com.google.DontBeEvil
      bjvm_raise_exception(thread, L"java/lang/ClassNotFoundException",
                           filename);
      return NULL;
    }

    base_class = calloc(1, sizeof(bjvm_classdesc));
    char *error = bjvm_parse_classfile(bytes, cf_len, base_class);
    if (error) {
      free(base_class);
      free(error);
      // TODO raise VerifyError
      UNREACHABLE();
    }

    // 3. If C has a direct superclass, the symbolic reference from C to its
    // direct superclass is resolved using the algorithm of §5.4.3.1.
    bjvm_cp_class_info *super = base_class->super_class;
    if (super) {
      // If the superclass is currently being loaded -> circularity  error
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name->chars,
                                 super->name->len)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, base_class->super_class);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
      }
    }

    // 4. If C has any direct superinterfaces, the symbolic references from C to
    // its direct superinterfaces are resolved using the algorithm of §5.4.3.1.
    for (int i = 0; i < base_class->interfaces_count; ++i) {
      bjvm_cp_class_info *super = base_class->interfaces[i];
      if (bjvm_hash_table_lookup(&vm->inchoate_classes, super->name->chars,
                                 -1)) {
        // TODO raise ClassCircularityError
        UNREACHABLE();
      }

      int status = bjvm_resolve_class(thread, base_class->interfaces[i]);
      if (status) {
        // TODO raise NoClassDefFoundError
        UNREACHABLE();
      }
    }

    // Look up in the native methods list and add native handles as appropriate
    native_entries *entries = bjvm_hash_table_lookup(&vm->natives, chars, len);
    if (entries) {
      for (int i = 0; i < entries->entries_count; i++) {
        native_entry *entry = entries->entries + i;

        for (int j = 0; j < base_class->methods_count; ++j) {
          bjvm_cp_method *method = base_class->methods + j;

          if (utf8_equals_utf8(method->name, &entry->name) &&
              utf8_equals_utf8(method->descriptor, &entry->descriptor)) {
            method->native_handle = entry->callback;
            break;
          }
        }
      }
    }

    // Remove from inchoate_classes
    (void)bjvm_hash_table_delete(&vm->inchoate_classes, chars, len);
    (void)bjvm_hash_table_insert(&vm->classes, chars, len, base_class);
  }

  // Derive nth dimension
  bjvm_classdesc *result = base_class;
  result->kind = BJVM_CD_KIND_ORDINARY;
  for (int i = 1; i <= dimensions; ++i) {
    if (!result->array_type)
      result->array_type = ordinary_arr_classdesc(thread, result, i);
    result = result->array_type;
  }

  return result;
}

int bjvm_link_array_class(bjvm_thread *thread,
                          bjvm_array_classdesc *classdesc) {
  int status = bjvm_link_class(thread, classdesc->base.base_component);
  if (status) {
    // TODO mark all arrays of this class as fucked up
    UNREACHABLE();
  }
  classdesc->base.state = classdesc->base.base_component->state;
  return status;
}

int allocate_field(int *current, bjvm_type_kind kind) {
  int result;
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE: {
    result = *current;
    (*current)++;
    break;
  }
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT: {
    *current = (*current + 1) & ~1;
    result = *current;
    *current += 2;
    break;
  }
  case BJVM_TYPE_KIND_FLOAT:
  case BJVM_TYPE_KIND_INT:
#ifdef EMSCRIPTEN
  case BJVM_TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 3) & ~3;
    result = *current;
    *current += 4;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
  case BJVM_TYPE_KIND_LONG:
#ifndef EMSCRIPTEN
  case BJVM_TYPE_KIND_REFERENCE:
#endif
    *current = (*current + 7) & ~7;
    result = *current;
    *current += 8;
    break;
  case BJVM_TYPE_KIND_VOID:
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
    UNREACHABLE();
  }
  return result;
}

int bjvm_link_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  assert(classdesc);
  if (classdesc->state != BJVM_CD_STATE_LOADED) // already linked
    return 0;

  if (classdesc->kind != BJVM_CD_KIND_ORDINARY) {
    assert(classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
    return bjvm_link_array_class(thread, (bjvm_array_classdesc *)classdesc);
  }

  // Link superclasses
  if (classdesc->super_class) {
    int status = bjvm_link_class(thread, classdesc->super_class->classdesc);
    if (status) {
      // TODO raise VerifyError
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }

  // Link superinterfaces
  for (int i = 0; i < classdesc->interfaces_count; ++i) {
    int status = bjvm_link_class(thread, classdesc->interfaces[i]->classdesc);
    if (status) {
      // TODO raise VerifyError
      classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
      return status;
    }
  }

  classdesc->state = BJVM_CD_STATE_LINKED;

  // Analyze/rewrite all methods
  for (int method_i = 0; method_i < classdesc->methods_count; ++method_i) {
    bjvm_cp_method *method = classdesc->methods + method_i;
    if (method->code) {
      char *error = analyze_method_code_segment(method);
      if (error) {
        // TODO raise VerifyError
        classdesc->state = BJVM_CD_STATE_LINKAGE_ERROR;
        printf("Error analyzing method %S: %s\n", method->name->chars, error);
        UNREACHABLE();
      }
    }
  }

  int imp_padding = (int)(uintptr_t)bjvm_hash_table_lookup(&thread->vm->class_padding,
                                                classdesc->name.chars,
                                                classdesc->name.len);

  // Assign memory locations to all static/non-static fields
  int static_offset = 0,
      nonstatic_offset = classdesc->super_class
                             ? classdesc->super_class->classdesc->data_bytes
                             : sizeof(bjvm_obj_header);
  nonstatic_offset += imp_padding;
  for (int field_i = 0; field_i < classdesc->fields_count; ++field_i) {
    bjvm_cp_field *field = classdesc->fields + field_i;
    bjvm_type_kind kind =
        field_to_representable_kind(&field->parsed_descriptor);
    field->byte_offset = field->access_flags & BJVM_ACCESS_STATIC
                             ? allocate_field(&static_offset, kind)
                             : allocate_field(&nonstatic_offset, kind);

#if AGGRESSIVE_DEBUG
    printf("Allocating nonstatic field %S for class %S at %d\n", field->name->chars, classdesc->name.chars,
           field->byte_offset);
#endif
  }

  // Create static field memory, initializing all to 0
  classdesc->static_fields = calloc(static_offset, 1);
  classdesc->data_bytes = nonstatic_offset;

  return 0;
}

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result);

int bjvm_initialize_class(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  assert(classdesc);
  if (classdesc->state == BJVM_CD_STATE_INITIALIZED)
    return 0; // already initialized
  if (classdesc->state != BJVM_CD_STATE_LINKED) {
    int error = bjvm_link_class(thread, classdesc);
    if (error)
      return error;
  }

  classdesc->state = BJVM_CD_STATE_INITIALIZED;

  bjvm_cp_method *clinit =
      bjvm_easy_method_lookup(classdesc, "<clinit>", "()V", false, false);
  int error = 0;
  if (clinit) {
    bjvm_stack_frame *frame = bjvm_push_frame(thread, clinit);
    error = bjvm_bytecode_interpret(thread, frame, NULL);
    bjvm_pop_frame(thread, frame);
  }
  classdesc->state =
      error ? BJVM_CD_STATE_LINKAGE_ERROR : BJVM_CD_STATE_INITIALIZED;
  return error;
}

#define checked_pop(frame)                                                     \
  ({                                                                           \
    assert(frame->stack_depth > 0);                                            \
    frame->values[--frame->stack_depth];                                       \
  })

void checked_push(bjvm_stack_frame *frame, bjvm_stack_value value) {
  assert(frame->stack_depth < frame->max_stack);
  frame->values[frame->stack_depth++] = value;
}

int32_t java_idiv(int32_t a, int32_t b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return INT_MIN;
  return a / b;
}

int64_t java_irem(int32_t a, int32_t b) {
  assert(b != 0);
  if (a == INT_MIN && b == -1)
    return 0;
  return a % b;
}

int64_t java_ldiv(int64_t a, int64_t b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return LONG_MIN;
  return a / b;
}

int64_t java_lrem(int64_t a, int64_t b) {
  assert(b != 0);
  if (a == LONG_MIN && b == -1)
    return 0;
  return a % b;
}

bool method_candidate_matches(const bjvm_cp_method *candidate,
                              const bjvm_utf8 *name,
                              const bjvm_utf8 *method_descriptor) {
  return utf8_equals_utf8(candidate->name, name) &&
         (candidate->is_signature_polymorphic || !method_descriptor ||
          utf8_equals_utf8(candidate->descriptor, method_descriptor));
}

bjvm_cp_method *bjvm_method_lookup(bjvm_classdesc *descriptor,
                                   const bjvm_utf8 *name,
                                   const bjvm_utf8 *method_descriptor,
                                   bool search_superclasses,
                                   bool search_superinterfaces) {
  assert(descriptor->state >= BJVM_CD_STATE_LINKED);
  bjvm_classdesc *search = descriptor;
  // if the object is an array and we're looking for a superclass method, the
  // method must be on a superclass
  if (search->kind != BJVM_CD_KIND_ORDINARY && search_superclasses)
    search = search->super_class->classdesc;
  while (true) {
    for (int i = 0; i < search->methods_count; ++i)
      if (method_candidate_matches(search->methods + i, name,
                                   method_descriptor))
        return search->methods + i;
    if (search_superclasses && search->super_class) {
      search = search->super_class->classdesc;
    } else {
      break;
    }
  }
  if (!search_superinterfaces)
    return NULL;

  for (int i = 0; i < descriptor->interfaces_count; ++i) {
    bjvm_cp_method *result =
        bjvm_method_lookup(descriptor->interfaces[i]->classdesc, name,
                           method_descriptor, false, true);
    if (result)
      return result;
  }

  return NULL;
}

bjvm_cp_method *bjvm_easy_method_lookup(bjvm_classdesc *classdesc,
                                        const char *name,
                                        const char *descriptor,
                                        bool superclasses,
                                        bool superinterfaces) {
  if (!classdesc) return NULL;
  bjvm_utf8 name_wide = bjvm_make_utf8_cstr(name),
            descriptor_wide;
  if (descriptor)
    descriptor_wide = bjvm_make_utf8_cstr(descriptor);
  bjvm_cp_method *result = bjvm_method_lookup(
      classdesc, &name_wide, descriptor ? &descriptor_wide : NULL, superclasses, superinterfaces);
  free_utf8(name_wide);
  if (descriptor)
    free_utf8(descriptor_wide);
  return result;
}

int bjvm_thread_run(bjvm_thread *thread, bjvm_cp_method *method,
                     bjvm_stack_value *args, bjvm_stack_value *result) {
  assert(method);

  bjvm_stack_frame *frame = bjvm_push_frame(thread, method);
  if (!frame) return -1;

  int object_argument = !(method->access_flags & BJVM_ACCESS_STATIC);
  for (int i = 0; i < method->parsed_descriptor->args_count + object_argument;
       ++i) {
    frame->values[frame->max_stack + i] = args[i];
  }
  bjvm_bytecode_interpret(thread, frame, result);
  bjvm_pop_frame(thread, frame);
  return 0;
}

int bjvm_resolve_class(bjvm_thread *thread, bjvm_cp_class_info *info) {
  // TODO use current class loader
  // TODO synchronize on some object, probably the class which this info is a
  // part of

  if (info->classdesc)
    return 0; // already succeeded
  if (info->resolution_error) {
    bjvm_raise_exception_object(thread,
                                info->resolution_error); // already failed
    return -1;
  }
  info->classdesc = bootstrap_class_create(thread, info->name->chars);
  if (!info->classdesc) {
    info->resolution_error = thread->current_exception;
    return -1;
  }

  // TODO check that the class is accessible

  return 0;
}

int bjvm_resolve_field(bjvm_thread *thread, bjvm_cp_field_info *info) {
  bjvm_cp_class_info *class = info->class_info;
  int error = bjvm_resolve_class(thread, class);
  if (error)
    return error;
  error = bjvm_link_class(thread, class->classdesc);
  if (error)
    return error;

  // Get offset of field
  assert(class->classdesc->state >= BJVM_CD_STATE_LINKED);
  bjvm_cp_field *field = bjvm_field_lookup(class->classdesc, info->nat->name,
                                           info->nat->descriptor);
  info->field = field;
  return field == NULL;
}

void make_array_classdesc(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  if (classdesc->array_type)
    return;
  if (classdesc->kind == BJVM_CD_KIND_ORDINARY)
    classdesc->array_type = ordinary_arr_classdesc(thread, classdesc, 1);
  else if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
    bjvm_array_classdesc *arr = (bjvm_array_classdesc *)classdesc;
    classdesc->array_type = ordinary_arr_classdesc(thread, arr->base.base_component,
                                                   arr->dimensions + 1);
  } else {
    bjvm_primitive_array_classdesc *arr =
        (bjvm_primitive_array_classdesc *)classdesc;
    classdesc->array_type = primitive_arr_classdesc(
        thread, arr->dimensions + 1, classdesc->kind, arr->element_type);
  }
}

void store_stack_value(void *field_location, bjvm_stack_value value,
                       bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    *(int8_t *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_SHORT:
    *(int16_t *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    *(float *)field_location = value.f;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    *(double *)field_location = value.d;
    break;
  case BJVM_TYPE_KIND_INT:
    *(int *)field_location = value.i;
    break;
  case BJVM_TYPE_KIND_LONG:
    *(int64_t *)field_location = value.l;
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    *(void **)field_location = value.obj;
    break;
  case BJVM_TYPE_KIND_VOID:
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
  default:
    UNREACHABLE();
  }
}

bjvm_stack_value load_stack_value(void *field_location, bjvm_type_kind kind) {
  bjvm_stack_value result;
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_BYTE:
    result.i = *(int8_t *)field_location;
    break;
  case BJVM_TYPE_KIND_CHAR:
    result.i = *(uint16_t *)field_location;
    break;
  case BJVM_TYPE_KIND_SHORT:
    result.i = *(int16_t *)field_location;
    break;
  case BJVM_TYPE_KIND_FLOAT:
    result.f = *(float *)field_location;
    break;
  case BJVM_TYPE_KIND_DOUBLE:
    result.d = *(double *)field_location;
    break;
  case BJVM_TYPE_KIND_INT:
    result.i = *(int *)field_location;
    break;
  case BJVM_TYPE_KIND_LONG:
    result.l = *(int64_t *)field_location;
    break;
  case BJVM_TYPE_KIND_REFERENCE:
    result.obj = *(void **)field_location;
    break;
  case BJVM_TYPE_KIND_VOID:
  case BJVM_TYPE_KIND_RETURN_ADDRESS:
  default:
    UNREACHABLE();
  }
  return result;
}

bjvm_obj_header *create_object_array(bjvm_thread *thread,
                                     bjvm_classdesc *classdesc, int count) {
  assert(classdesc);
  if (count < 0) {
    bjvm_negative_array_size_exception(thread, count);
    return NULL;
  }
  bjvm_obj_header *array = calloc(1, 24 + count * sizeof(void *));
  make_array_classdesc(thread, classdesc);
  array->descriptor = classdesc->array_type;
  *array_length(array) = count;
  return array;
}

bjvm_obj_header *create_primitive_array(bjvm_thread *thread,
                                        bjvm_type_kind array_type, int count) {
  const wchar_t *type;
  int size;

  if (count < 0) {
    bjvm_negative_array_size_exception(thread, count);
    return NULL;
  }

  primitive_type_kind_to_array_info(array_type, &type, &size);
  bjvm_classdesc *desc = bootstrap_class_create(thread, type);
  assert(desc);

  bjvm_obj_header *array = calloc(1, 24 + count * size);
  array->descriptor = desc;
  *array_length(array) = count;

  return array;
}

uint64_t hash_code_rng = 0;
uint64_t next_hash_code() {
  hash_code_rng = hash_code_rng * 0x5DEECE66D + 0xB;
  return hash_code_rng >> 16;
}

bjvm_obj_header *new_object(bjvm_thread *thread, bjvm_classdesc *classdesc) {
  (void)thread;
  bjvm_obj_header *obj =
      calloc(1, classdesc->data_bytes + sizeof(bjvm_obj_header));
  obj->descriptor = classdesc;
  obj->mark_word = next_hash_code();
  return obj;
}

int *array_length(bjvm_obj_header *array) {
  return (int *)((char *)array + 16);
}

void *array_data(bjvm_obj_header *array) { return (char *)array + 24; }

bool bjvm_is_instanceof_name(const bjvm_obj_header *mirror, const char* name) {
  return utf8_equals(&mirror->descriptor->name, name);
}

// NULLABLE because bjvm_unmirror_class on int.class etc. will return NULL! (For now...)
bjvm_classdesc *bjvm_unmirror_class(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/Class"));
  return ((struct bjvm_native_Class *)mirror)->reflected_class;
}

bjvm_cp_field **bjvm_unmirror_field(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/reflect/Field"));
  // Fields get copied around, but all reference the "root" created by the VM
  bjvm_obj_header* root = ((struct bjvm_native_Field *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Field *)mirror)->reflected_field;
}

bjvm_cp_method **bjvm_unmirror_ctor(bjvm_obj_header *mirror) {
  assert(bjvm_is_instanceof_name(mirror, "java/lang/reflect/Constructor"));
  // Constructors get copied around, but all reference the "root" created by the VM
  bjvm_obj_header* root = ((struct bjvm_native_Constructor *)mirror)->root;
  if (root)
    mirror = root;
  return &((struct bjvm_native_Constructor *)mirror)->reflected_ctor;
}

bjvm_obj_header *make_string(bjvm_thread *thread, const wchar_t *chars, int len) {
  bjvm_classdesc *java_lang_String =
      bootstrap_class_create(thread, L"java/lang/String");
  bjvm_initialize_class(thread, java_lang_String);
  struct bjvm_native_String *str = (void*)new_object(thread, java_lang_String);
  len = len < 0 ? wcslen(chars) : len;
  str->value = create_primitive_array(thread, BJVM_TYPE_KIND_CHAR, len);
  for (int i = 0; i < len; ++i)
    *((uint16_t *)array_data(str->value) + i) = chars[i];
  return (void*)str;
}

struct bjvm_native_Class *bjvm_get_class_mirror(bjvm_thread *thread,
                                       bjvm_classdesc *classdesc) {
  if (!classdesc)
    return NULL;
  if (classdesc->mirror)
    return classdesc->mirror;

  bjvm_classdesc *java_lang_Class =
      bootstrap_class_create(thread, L"java/lang/Class");
  struct bjvm_native_Class *class_mirror = classdesc->mirror = (void*)new_object(thread, java_lang_Class);
  class_mirror->reflected_class = classdesc;

  return class_mirror;
}

bool bjvm_instanceof_interface(const bjvm_classdesc *o,
                               const bjvm_classdesc *classdesc) {
  if (o == classdesc)
    return true;
  for (int i = 0; i < o->interfaces_count; ++i) {
    if (bjvm_instanceof_interface(o->interfaces[i]->classdesc, classdesc)) {
      return true;
    }
  }
  return false;
}

bool bjvm_instanceof(const bjvm_classdesc *o, const bjvm_classdesc *classdesc) {
  // Walk the superclasses/superinterfaces of bjvm_obj_header and see if any are
  // equal to classdesc
  // TODO compare class loaders too, superinterfaces
  // TODO arrays
  if (o == NULL) return true;

  if (classdesc->kind != BJVM_CD_KIND_ORDINARY) {
    if (classdesc->kind == BJVM_CD_KIND_ORDINARY_ARRAY) {
      const bjvm_array_classdesc *arr = (const bjvm_array_classdesc *)classdesc;
      if (o->kind != BJVM_CD_KIND_ORDINARY_ARRAY)
        return false;
      const bjvm_array_classdesc *o_arr = (const bjvm_array_classdesc *)o;
      return arr->dimensions == o_arr->dimensions &&
             bjvm_instanceof(o_arr->base.base_component, arr->base.base_component);
    }
    return classdesc->kind == o->kind;
  }

  assert(classdesc->kind == BJVM_CD_KIND_ORDINARY);
  const bjvm_classdesc *desc = o;
  while (desc) {
    if (bjvm_instanceof_interface(desc, classdesc))
      return true;
    desc = desc->super_class ? desc->super_class->classdesc : NULL;
  }
  return false;
}

// Implementation of invokespecial/invokeinterface/invokevirtual
int bjvm_invokenonstatic(bjvm_thread *thread, bjvm_stack_frame *frame, bjvm_bytecode_insn *insn) {
  assert(insn->cp->kind == BJVM_CP_KIND_METHOD_REF || insn->cp->kind == BJVM_CP_KIND_INTERFACE_METHOD_REF);
  bjvm_cp_method *method = insn->ic;

  int args = insn->args;
  bjvm_obj_header *target = frame->values[frame->stack_depth - args].obj;

  if (!method || (insn->kind != bjvm_insn_invokespecial && target->descriptor != insn->ic2) || !CACHE_INVOKENONSTATIC) {
    const bjvm_cp_method_info *info = &insn->cp->methodref;
    args = insn->args = info->method_descriptor->args_count + 1;

    assert(args <= frame->stack_depth);
    target = frame->values[frame->stack_depth - args].obj;

    if (target == NULL) {
      bjvm_null_pointer_exception(thread);
      return -1;
    }
    if (insn->kind == bjvm_insn_invokespecial) {
      int error = bjvm_resolve_class(thread, info->class_info);
      if (error)
        return -1;
    }
    bjvm_classdesc *lookup_on = insn->ic2 = insn->kind == bjvm_insn_invokespecial
                                    ? info->class_info->classdesc
                                    : target->descriptor;
    if (lookup_on->state != BJVM_CD_STATE_INITIALIZED) {
      int error = bjvm_initialize_class(thread, lookup_on);
      if (error)
        return -1;
    }
    method = insn->ic = bjvm_method_lookup(
        lookup_on, info->name_and_type->name, info->name_and_type->descriptor,
        true, insn->kind == bjvm_insn_invokeinterface);
    insn->ic2 = lookup_on;
    if (!method) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Could not find method %S with descriptor %S on %s %S",
               info->name_and_type->name->chars, info->name_and_type->descriptor->chars,
               lookup_on->access_flags & BJVM_ACCESS_INTERFACE ? "interface" : "class",
               lookup_on->name.chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
    if (method->access_flags & BJVM_ACCESS_STATIC) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Method %S is static", method->name->chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }

    if (method->access_flags & BJVM_ACCESS_ABSTRACT)
      bjvm_abstract_method_error(thread, method);
  }

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    invoked_result = method->native_handle(thread, target,
                          frame->values + frame->stack_depth - args + 1,
                          args - 1);
    frame->stack_depth -= args;
    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    for (int i = 0, j = 0; i < args; ++i, ++j) {
      invoked_frame->values[invoked_frame->max_stack + j] = frame->values[frame->stack_depth - args + i];
      if (i >= 1)
        j += bjvm_is_field_wide(method->parsed_descriptor->args[i - 1]);
    }
    frame->stack_depth -= args;

    int err =
        bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }
  if (method->parsed_descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);

  return 0;
}

int bjvm_invokestatic(bjvm_thread * thread, bjvm_stack_frame * frame, bjvm_bytecode_insn * insn) {
  bjvm_cp_method *method = insn->ic;
  if (!method || !CACHE_INVOKESTATIC) {
    const bjvm_cp_method_info *info = &insn->cp->methodref;
    bjvm_cp_class_info *class = info->class_info;

    int error = bjvm_resolve_class(thread, class);
    if (error)
      return -1;
    error = bjvm_initialize_class(thread, class->classdesc);
    if (error)
      return -1;

    method = insn->ic =
        bjvm_method_lookup(class->classdesc, info->name_and_type->name,
                           info->name_and_type->descriptor, false, false);
    if (!method) {
      wchar_t complaint[1000];
      swprintf(complaint, 1000, L"Could not find method %S with descriptor %S on class %S",
              info->name_and_type->name->chars, info->name_and_type->descriptor->chars,
              class->name->chars);
      bjvm_incompatible_class_change_error(thread, complaint);
      return -1;
    }
  }

  int args = method->parsed_descriptor->args_count;
  assert(args <= frame->stack_depth);

  bjvm_stack_value invoked_result;
  if (method->access_flags & BJVM_ACCESS_NATIVE) {
    if (!method->native_handle) {
      bjvm_unsatisfied_link_error(thread, method);
      return -1;
    }

    invoked_result = method->native_handle(thread, NULL,
                          frame->values + frame->stack_depth - args, args);
    frame->stack_depth -= args;
    if (thread->current_exception)
      return -1;
  } else {
    bjvm_stack_frame *invoked_frame = bjvm_push_frame(thread, method);
    for (int i = 0, j = 0; i < args; ++i, ++j) {
      invoked_frame->values[invoked_frame->max_stack + j] = frame->values[frame->stack_depth - args + i];
      j += bjvm_is_field_wide(method->parsed_descriptor->args[i]);
    }
    frame->stack_depth -= args;
    int err = bjvm_bytecode_interpret(thread, invoked_frame, &invoked_result);
    bjvm_pop_frame(thread, invoked_frame);
    if (err)
      return -1;
  }

  if (method->parsed_descriptor->return_type.kind != BJVM_TYPE_KIND_VOID)
    checked_push(frame, invoked_result);
  return 0;
}

bjvm_obj_header *bjvm_multianewarray_impl(bjvm_thread *thread, bjvm_classdesc *desc, bjvm_stack_value *value, int dims) {
  int this_dim = (value + dims - 1)->i;
  if (dims == 1 && desc->kind != BJVM_CD_KIND_ORDINARY_ARRAY) {
    return create_primitive_array(thread, ((bjvm_primitive_array_classdesc*) desc)->element_type, this_dim);
  }
  bjvm_obj_header* arr = create_object_array(thread, desc, this_dim);
  for (int i = 0; i < this_dim; ++i) {
    bjvm_obj_header* next = bjvm_multianewarray_impl(thread, desc->array_type, value, dims - 1);
    *((bjvm_obj_header**) array_data(arr) + i) = next;
  }
  return arr;
}

int bjvm_multianewarray(bjvm_thread *thread, bjvm_stack_frame *frame, struct bjvm_multianewarray_data *multianewarray) {
  int dims = multianewarray->dimensions;
  assert(frame->stack_depth >= dims);
  assert(dims >= 1);

  int error = bjvm_resolve_class(thread, multianewarray->entry);
  if (error)
    return -1;

  for (int i = 0; i < dims; ++i) {
    int dim = frame->values[frame->stack_depth - dims + i].i;
    if (dim < 0) {
      bjvm_negative_array_size_exception(thread, dim);
      return -1;
    }
  }

  bjvm_obj_header *result = bjvm_multianewarray_impl(thread,
    multianewarray->entry->classdesc, &frame->values[frame->stack_depth - dims], dims);
  frame->stack_depth -= dims;
  checked_push(frame, (bjvm_stack_value){.obj = result});
  return 0;
}

bool bjvm_invokedynamic(bjvm_thread * thread, bjvm_stack_frame * frame, bjvm_bytecode_insn * insn) {
  UNREACHABLE("bjvm_invokedynamic");

  (void) thread;
  (void) frame;
  (void) insn;
}

int bjvm_bytecode_interpret(bjvm_thread *thread, bjvm_stack_frame *frame,
                            bjvm_stack_value *result) {
  bjvm_cp_method *method = frame->method;

#if AGGRESSIVE_DEBUG
  printf("Calling method %S, descriptor %S, on class %S\n", method->name->chars,
         method->descriptor->chars, method->my_class->name.chars);
#endif

start:
  while (true) {
    bjvm_bytecode_insn* insn = &method->code->code[frame->program_counter];

#if AGGRESSIVE_DEBUG
    char *dump = dump_frame(frame),
         *insn_dump = insn_to_string(insn, frame->program_counter);
    printf("Insn: %s\n", insn_to_string(insn, frame->program_counter));
    printf("Method: %S in class %S\n", method->name->chars,
           method->my_class->name.chars);
    printf("FRAME:\n%s\n", dump_frame(frame));

    free(dump);
    free(insn_dump);
#endif

    static const void* insn_jump_table[] = { &&bjvm_insn_nop, &&bjvm_insn_aaload, &&bjvm_insn_aastore, &&bjvm_insn_aconst_null,   &&bjvm_insn_areturn,   &&bjvm_insn_arraylength,   &&bjvm_insn_athrow,   &&bjvm_insn_baload,   &&bjvm_insn_bastore,   &&bjvm_insn_caload,   &&bjvm_insn_castore,   &&bjvm_insn_d2f,   &&bjvm_insn_d2i,   &&bjvm_insn_d2l,   &&bjvm_insn_dadd,   &&bjvm_insn_daload,   &&bjvm_insn_dastore,   &&bjvm_insn_dcmpg,   &&bjvm_insn_dcmpl,   &&bjvm_insn_ddiv,   &&bjvm_insn_dmul,   &&bjvm_insn_dneg,   &&bjvm_insn_drem,   &&bjvm_insn_dreturn,   &&bjvm_insn_dsub,   &&bjvm_insn_dup,   &&bjvm_insn_dup_x1,   &&bjvm_insn_dup_x2,   &&bjvm_insn_dup2,   &&bjvm_insn_dup2_x1,   &&bjvm_insn_dup2_x2,   &&bjvm_insn_f2d,   &&bjvm_insn_f2i,   &&bjvm_insn_f2l,   &&bjvm_insn_fadd,   &&bjvm_insn_faload,   &&bjvm_insn_fastore,   &&bjvm_insn_fcmpg,   &&bjvm_insn_fcmpl,   &&bjvm_insn_fdiv,   &&bjvm_insn_fmul,   &&bjvm_insn_fneg,   &&bjvm_insn_frem,   &&bjvm_insn_freturn,   &&bjvm_insn_fsub,   &&bjvm_insn_i2b,   &&bjvm_insn_i2c,   &&bjvm_insn_i2d,   &&bjvm_insn_i2f,   &&bjvm_insn_i2l,   &&bjvm_insn_i2s,   &&bjvm_insn_iadd,   &&bjvm_insn_iaload,   &&bjvm_insn_iand,   &&bjvm_insn_iastore,   &&bjvm_insn_idiv,   &&bjvm_insn_imul,   &&bjvm_insn_ineg,   &&bjvm_insn_ior,   &&bjvm_insn_irem,   &&bjvm_insn_ireturn,   &&bjvm_insn_ishl,   &&bjvm_insn_ishr,   &&bjvm_insn_isub,   &&bjvm_insn_iushr,   &&bjvm_insn_ixor,   &&bjvm_insn_l2d,   &&bjvm_insn_l2f,   &&bjvm_insn_l2i,   &&bjvm_insn_ladd,   &&bjvm_insn_laload,   &&bjvm_insn_land,   &&bjvm_insn_lastore,   &&bjvm_insn_lcmp,   &&bjvm_insn_ldiv,   &&bjvm_insn_lmul,   &&bjvm_insn_lneg,   &&bjvm_insn_lor,   &&bjvm_insn_lrem,   &&bjvm_insn_lreturn,   &&bjvm_insn_lshl,   &&bjvm_insn_lshr,   &&bjvm_insn_lsub,   &&bjvm_insn_lushr,   &&bjvm_insn_lxor,   &&bjvm_insn_monitorenter,   &&bjvm_insn_monitorexit,   &&bjvm_insn_pop,   &&bjvm_insn_pop2,   &&bjvm_insn_return,   &&bjvm_insn_saload,   &&bjvm_insn_sastore,   &&bjvm_insn_swap,   &&bjvm_insn_anewarray,   &&bjvm_insn_checkcast,   &&bjvm_insn_getfield,   &&bjvm_insn_getstatic,   &&bjvm_insn_instanceof,   &&bjvm_insn_invokedynamic,   &&bjvm_insn_new,   &&bjvm_insn_putfield,   &&bjvm_insn_putstatic,   &&bjvm_insn_invokevirtual,   &&bjvm_insn_invokespecial,   &&bjvm_insn_invokestatic,   &&bjvm_insn_ldc,   &&bjvm_insn_ldc2_w,   &&bjvm_insn_dload,   &&bjvm_insn_fload,   &&bjvm_insn_iload,   &&bjvm_insn_lload,   &&bjvm_insn_dstore,   &&bjvm_insn_fstore,   &&bjvm_insn_istore,   &&bjvm_insn_lstore,   &&bjvm_insn_aload,   &&bjvm_insn_astore,   &&bjvm_insn_goto,   &&bjvm_insn_jsr,   &&bjvm_insn_if_acmpeq,   &&bjvm_insn_if_acmpne,   &&bjvm_insn_if_icmpeq,   &&bjvm_insn_if_icmpne,   &&bjvm_insn_if_icmplt,   &&bjvm_insn_if_icmpge,   &&bjvm_insn_if_icmpgt,   &&bjvm_insn_if_icmple,   &&bjvm_insn_ifeq,   &&bjvm_insn_ifne,   &&bjvm_insn_iflt,   &&bjvm_insn_ifge,   &&bjvm_insn_ifgt,   &&bjvm_insn_ifle,   &&bjvm_insn_ifnonnull,   &&bjvm_insn_ifnull,   &&bjvm_insn_iconst,   &&bjvm_insn_dconst,   &&bjvm_insn_fconst,   &&bjvm_insn_lconst,   &&bjvm_insn_iinc,   &&bjvm_insn_invokeinterface,   &&bjvm_insn_multianewarray,   &&bjvm_insn_newarray,   &&bjvm_insn_tableswitch,   &&bjvm_insn_lookupswitch,   &&bjvm_insn_ret };
    goto *insn_jump_table[insn->kind];

#if ONE_GOTO_PER_INSN
#define NEXT_INSN { insn = &method->code->code[++frame->program_counter]; goto *insn_jump_table[insn->kind]; }
#define JMP_INSN { insn = &method->code->code[frame->program_counter]; goto *insn_jump_table[insn->kind]; }
#else
#define NEXT_INSN break
#define JMP_INSN continue
#endif

    switch (0) {

    bjvm_insn_nop:
      NEXT_INSN;
    bjvm_insn_aaload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_ORDINARY_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      bjvm_obj_header *obj = *((bjvm_obj_header **)array_data(array) + index);
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_aastore: {
      bjvm_obj_header *value = checked_pop(frame).obj;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      bjvm_obj_header **obj = (bjvm_obj_header **)array_data(array) + index;
      *obj = value;
      NEXT_INSN;
    }
    bjvm_insn_aconst_null:
      checked_push(frame, (bjvm_stack_value){.obj = NULL});
      NEXT_INSN;
    bjvm_insn_arraylength: {
      bjvm_obj_header *obj = checked_pop(frame).obj;
      if (!obj) {
        // NullPointerException
        bjvm_null_pointer_exception(thread);
        goto done;
      }
      assert(obj->descriptor->kind != BJVM_CD_KIND_ORDINARY);
      checked_push(frame, (bjvm_stack_value){.i = *array_length(obj)});
      NEXT_INSN;
    }
    bjvm_insn_athrow: {
      bjvm_raise_exception_object(thread, checked_pop(frame).obj);
      goto done;
    }
    bjvm_insn_baload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        // ArrayIndexOutOfBoundsException
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int8_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_bastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_BYTE_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int8_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_caload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_CHAR_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((uint16_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_sastore:
    bjvm_insn_castore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_CHAR_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((uint16_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_d2f: {
      checked_push(frame, (bjvm_stack_value){.f = (float)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_d2i: {
      checked_push(frame, (bjvm_stack_value){.i = (int)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_d2l: {
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_dadd: {
      checked_push(frame, (bjvm_stack_value){
                              .d = checked_pop(frame).d + checked_pop(frame).d});
      NEXT_INSN;
    }
    bjvm_insn_dcmpg: {
      double value2 = checked_pop(frame).d, value1 = checked_pop(frame).d;
      if (value1 < value2) {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      }
      NEXT_INSN;
    }
    bjvm_insn_dcmpl: {
      double value2 = checked_pop(frame).d, value1 = checked_pop(frame).d;
      if (value1 > value2) {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      }
      NEXT_INSN;
  }
    bjvm_insn_ddiv: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = a / b });
      NEXT_INSN;
    }
    bjvm_insn_dmul: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = a * b });
      NEXT_INSN;
    }
    bjvm_insn_dneg: {
      double a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = -a });
      NEXT_INSN;
    }
    bjvm_insn_drem: { // deprecated
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = fmod(a, b) });
      NEXT_INSN;
    }
    bjvm_insn_dsub: {
      double b = checked_pop(frame).d, a = checked_pop(frame).d;
      checked_push(frame, (bjvm_stack_value) { .d = a - b });
      NEXT_INSN;
    }
    bjvm_insn_dup: {
      bjvm_stack_value val = checked_pop(frame);
      checked_push(frame, val);
      checked_push(frame, val);
      NEXT_INSN;
    }
    bjvm_insn_dup_x1: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      checked_push(frame, val1);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup_x2: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      checked_push(frame, val1);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2: {
      bjvm_stack_value val1 = checked_pop(frame), val2 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2_x1: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_dup2_x2: {
      bjvm_stack_value val1 = checked_pop(frame);
      bjvm_stack_value val2 = checked_pop(frame);
      bjvm_stack_value val3 = checked_pop(frame);
      bjvm_stack_value val4 = checked_pop(frame);
      checked_push(frame, val2);
      checked_push(frame, val1);
      checked_push(frame, val4);
      checked_push(frame, val3);
      checked_push(frame, val2);
      checked_push(frame, val1);
      NEXT_INSN;
    }
    bjvm_insn_f2d: {
      checked_push(frame, (bjvm_stack_value){.d = (double)checked_pop(frame).f});
      NEXT_INSN;
    }
    bjvm_insn_f2i: {
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.i = (int)a});
      NEXT_INSN;
    }
    bjvm_insn_f2l:
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)checked_pop(frame).f});
      NEXT_INSN;
    bjvm_insn_fadd: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a + b });
      NEXT_INSN;
    }
    bjvm_insn_fcmpg: {
      float value2 = checked_pop(frame).f, value1 = checked_pop(frame).f;

      if (value1 < value2) {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      }

      NEXT_INSN;
    }
    bjvm_insn_fcmpl: {
      float value2 = checked_pop(frame).f, value1 = checked_pop(frame).f;

      if (value1 > value2) {
        checked_push(frame, (bjvm_stack_value){.i = 1});
      } else if (value1 == value2) {
        checked_push(frame, (bjvm_stack_value){.i = 0});
      } else {
        checked_push(frame, (bjvm_stack_value){.i = -1});
      }

      NEXT_INSN;
    }
    bjvm_insn_fdiv:{
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a / b });
      NEXT_INSN;
    }
    bjvm_insn_fmul: {
      float a = checked_pop(frame).f, b = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a * b});
      NEXT_INSN;
    }
    bjvm_insn_fneg:{
      float a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = -a });
      NEXT_INSN;
    }
    bjvm_insn_frem: { // deprecated
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value) { .f = fmodf(a, b) });
      NEXT_INSN;
    }
    bjvm_insn_fsub: {
      float b = checked_pop(frame).f, a = checked_pop(frame).f;
      checked_push(frame, (bjvm_stack_value){.f = a - b });
      NEXT_INSN;
    }
    bjvm_insn_i2b: {
      checked_push(frame,
                   (bjvm_stack_value){.i = (int8_t)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_i2c: {
      checked_push(frame,
                   (bjvm_stack_value){.i = checked_pop(frame).i & 0xffff});
      NEXT_INSN;
    }
    bjvm_insn_i2d: {
      checked_push(frame,
                   (bjvm_stack_value){.d = (double)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_i2f: {
      int a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.f = (float)a});
      NEXT_INSN;
    }
    bjvm_insn_i2l: {
      int a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)a});
      NEXT_INSN;
    }
    bjvm_insn_i2s: {
      checked_push(frame,
                   (bjvm_stack_value){.i = (int16_t)checked_pop(frame).i});
      NEXT_INSN;
    }
    bjvm_insn_iadd: {
      uint32_t a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a + b});
      NEXT_INSN;
    }
    bjvm_insn_faload:
    bjvm_insn_iaload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int32_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_iand: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a & b});
      NEXT_INSN;
    }
    bjvm_insn_fastore:
    bjvm_insn_iastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int32_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_idiv: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, L"/ by zero");
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = java_idiv(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_imul: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i, c;
      __builtin_mul_overflow(a, b, &c);
      checked_push(frame, (bjvm_stack_value){.i = c});
      NEXT_INSN;
    }
    bjvm_insn_ineg: {
      uint32_t a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = -a});
      NEXT_INSN;
    }
    bjvm_insn_ior: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a | b});
      NEXT_INSN;
    }
    bjvm_insn_irem: {
      int32_t b = checked_pop(frame).i, a = checked_pop(frame).i;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, L"/ by zero");
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.i = java_irem(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_dreturn:
    bjvm_insn_areturn:
    bjvm_insn_lreturn:
    bjvm_insn_freturn:
    bjvm_insn_ireturn: {
      *result = checked_pop(frame);
      goto done;
    }
    bjvm_insn_ishl: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      uint32_t c = (uint32_t)a << (b & 0x1f); // fuck u UB
      checked_push(frame, (bjvm_stack_value){.i = (int)c});
      NEXT_INSN;
    }
    bjvm_insn_ishr: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a >> b});
      NEXT_INSN;
    }
    bjvm_insn_isub: {
      uint32_t b = checked_pop(frame).i, a = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a - b});
      NEXT_INSN;
    }
    bjvm_insn_iushr: {
      int b = checked_pop(frame).i, a = checked_pop(frame).i;
      uint32_t c = (uint32_t)a >> (b & 0x1f);
      checked_push(frame, (bjvm_stack_value){.i = (int)c});
      NEXT_INSN;
    }
    bjvm_insn_ixor: {
      int a = checked_pop(frame).i, b = checked_pop(frame).i;
      checked_push(frame, (bjvm_stack_value){.i = a ^ b});
      NEXT_INSN;
    }
    bjvm_insn_l2d: {
      checked_push(frame,
                   (bjvm_stack_value){.d = (double)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_l2f: {
      checked_push(frame, (bjvm_stack_value){.f = (float)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_l2i: {
      checked_push(frame, (bjvm_stack_value){.i = (int)checked_pop(frame).l});
      NEXT_INSN;
    }
    bjvm_insn_ladd: {
      uint64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a + b});
      NEXT_INSN;
    }
    bjvm_insn_laload:
    bjvm_insn_daload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int64_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_land: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a & b});
      NEXT_INSN;
    }
    bjvm_insn_lastore:
    bjvm_insn_dastore: {
      int value = checked_pop(frame).i;
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      *((int64_t *)array_data(array) + index) = value;
      NEXT_INSN;
    }
    bjvm_insn_lcmp: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      int sign = a > b ? 1 : a == b ? 0 : -1;
      checked_push(frame, (bjvm_stack_value){.i = sign});
      NEXT_INSN;
    }
    bjvm_insn_ldiv: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, L"/ by zero");
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.l = java_ldiv(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_lmul: {
      uint64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a * b});
      NEXT_INSN;
    }
    bjvm_insn_lneg: {
      uint64_t a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = -a});
      NEXT_INSN;
    }
    bjvm_insn_lor: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a | b});
      NEXT_INSN;
    }
    bjvm_insn_lrem: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      if (b == 0) {
        bjvm_arithmetic_exception(thread, L"/ by zero");
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){.l = java_lrem(a, b)});
      NEXT_INSN;
    }
    bjvm_insn_lshl: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      uint64_t c = (uint64_t)a << (b & 0x3f); // fuck u UB
      checked_push(frame, (bjvm_stack_value){.l = (int64_t)c});
      NEXT_INSN;
    }
    bjvm_insn_lshr: {
      int64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a >> b});
      NEXT_INSN;
    }
    bjvm_insn_lsub: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a - b});
      NEXT_INSN;
    }
    bjvm_insn_lushr: {
      uint64_t b = checked_pop(frame).l, a = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a >> b});
      NEXT_INSN;
    }
    bjvm_insn_lxor: {
      int64_t a = checked_pop(frame).l, b = checked_pop(frame).l;
      checked_push(frame, (bjvm_stack_value){.l = a ^ b});
      NEXT_INSN;
    }
    bjvm_insn_monitorenter:
    bjvm_insn_monitorexit: {
      // TODO
      checked_pop(frame);
      NEXT_INSN;
    }
    bjvm_insn_pop:
      checked_pop(frame);
      NEXT_INSN;
    bjvm_insn_pop2:
      checked_pop(frame);
      checked_pop(frame);
      NEXT_INSN;
    bjvm_insn_return: {
      goto done;
    }
    bjvm_insn_saload: {
      int index = checked_pop(frame).i;
      bjvm_obj_header *array = checked_pop(frame).obj;
      assert(array->descriptor->kind == BJVM_CD_KIND_SHORT_ARRAY);
      int len = *array_length(array);
      if (index < 0 || index >= len) {
        bjvm_array_index_oob_exception(thread, index, len);
        goto done;
      }
      checked_push(frame, (bjvm_stack_value){
                              .i = *((int16_t *)array_data(array) + index)});
      NEXT_INSN;
    }
    bjvm_insn_swap: {
      bjvm_stack_value a = checked_pop(frame), b = checked_pop(frame);
      checked_push(frame, a);
      checked_push(frame, b);
      NEXT_INSN;
    }
    bjvm_insn_anewarray: {
      int count = checked_pop(frame).i;
      bjvm_cp_class_info *info = &insn->cp->class_info;

      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      assert(info->classdesc);

      bjvm_obj_header *array =
          create_object_array(thread, info->classdesc, count);
      if (array) {
        checked_push(frame, (bjvm_stack_value){.obj = array});
      } else {
        goto done; // failed to create array
      }
      NEXT_INSN;
    }
    bjvm_insn_checkcast: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      bjvm_obj_header *obj = checked_pop(frame).obj;
      if (obj) {
        if (bjvm_instanceof(obj->descriptor, info->classdesc)) {
          checked_push(frame, (bjvm_stack_value){.obj = obj});
        } else {
          bjvm_raise_exception(thread, L"java/lang/ClassCastException", NULL);
          goto done;
        }
      } else {
        checked_push(frame, (bjvm_stack_value){.obj = NULL});
      }
      NEXT_INSN;
    }
    bjvm_insn_instanceof: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;

      bjvm_obj_header *obj = checked_pop(frame).obj;
      checked_push(
          frame, (bjvm_stack_value){.i = obj ? bjvm_instanceof(obj->descriptor,
                                                               info->classdesc)
                                             : 0});
      NEXT_INSN;
    }
    bjvm_insn_invokedynamic: {
      if (bjvm_invokedynamic(thread, frame, insn))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_new: {
      bjvm_cp_class_info *info = &insn->cp->class_info;
      int error = bjvm_resolve_class(thread, info);
      if (error)
        goto done;
      error = bjvm_initialize_class(thread, info->classdesc);
      if (error)
        goto done;
      // Create an instance of the class
      bjvm_obj_header *obj = new_object(thread, info->classdesc);
      checked_push(frame, (bjvm_stack_value){.obj = obj});
      NEXT_INSN;
    }
    bjvm_insn_getfield:
    bjvm_insn_putfield: {
      bjvm_cp_field_info *field_info = &insn->cp->fieldref_info;
      int error = bjvm_resolve_field(thread, field_info);
      if (error || field_info->field->access_flags & BJVM_ACCESS_STATIC) {
        wchar_t complaint[1000];
        swprintf(complaint, 1000, L"Expected nonstatic field %S on class %S", field_info->nat->name, field_info->class_info->name->chars);
        bjvm_incompatible_class_change_error(thread, complaint);
        goto done;
      }
      bjvm_stack_value val;
      if (insn->kind == bjvm_insn_putfield)
        val = checked_pop(frame);
      bjvm_obj_header *obj = checked_pop(frame).obj;

      void *addr = (char *)obj + field_info->field->byte_offset;

      bjvm_type_kind kind =
          field_to_representable_kind(field_info->parsed_descriptor);
      if (insn->kind == bjvm_insn_getfield) {
        checked_push(frame, load_stack_value(addr, kind));
      } else {
        store_stack_value(addr, val, kind);
      }

      NEXT_INSN;
    }
    bjvm_insn_getstatic:
    bjvm_insn_putstatic: {
      bjvm_cp_field_info *field_info = &insn->cp->fieldref_info;
      if (!field_info->field) {
        bjvm_cp_class_info *class = field_info->class_info;

        int error = bjvm_resolve_class(thread, class);
        if (error)
          goto done;

        bjvm_initialize_class(thread, class->classdesc);
        bjvm_cp_field *field = bjvm_field_lookup(
            class->classdesc, field_info->nat->name, field_info->nat->descriptor);
        field_info->field = field;
        if (!field || !(field->access_flags & BJVM_ACCESS_STATIC)) {
          wchar_t complaint[1000];
          swprintf(complaint, 1000, L"Expected static field %S on class %S", field_info->nat->name, field_info->class_info->name->chars);
          bjvm_incompatible_class_change_error(thread, complaint);
          goto done;
        }
      }

      bjvm_cp_field *field = field_info->field;
      void *field_location =
          &field->my_class->static_fields[field->byte_offset];
      bjvm_type_kind kind =
          field_to_representable_kind(field_info->parsed_descriptor);
      if (insn->kind == bjvm_insn_putstatic) {
        store_stack_value(field_location, checked_pop(frame), kind);
      } else {
        checked_push(frame, load_stack_value(field_location, kind));
      }

      NEXT_INSN;
    }
    bjvm_insn_invokespecial:
    bjvm_insn_invokeinterface:
    bjvm_insn_invokevirtual: {
      if (bjvm_invokenonstatic(thread, frame, insn))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_invokestatic: {
      if (bjvm_invokestatic(thread, frame, insn))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_ldc: {
      bjvm_cp_entry *ent = insn->cp;
      switch (ent->kind) {
      case BJVM_CP_KIND_INTEGER:
        checked_push(frame, (bjvm_stack_value){.i = ent->integral.value});
        break;
      case BJVM_CP_KIND_FLOAT:
        checked_push(frame, (bjvm_stack_value){.f = ent->floating.value});
        break;
      case BJVM_CP_KIND_CLASS: {
        // Initialize the class, then get its Java mirror
        int error = bjvm_resolve_class(thread, &ent->class_info);
        if (error)
          goto done;
        error = bjvm_link_class(thread, ent->class_info.classdesc);
        if (error)
          goto done;
        bjvm_obj_header *obj = (void*) bjvm_get_class_mirror(thread, ent->class_info.classdesc);
        checked_push(frame, (bjvm_stack_value){.obj = obj});
        break;
      }
      case BJVM_CP_KIND_STRING: {
        bjvm_utf8* s = ent->string.chars;
        bjvm_obj_header *obj = bjvm_intern_string(thread, s->chars, s->len);
        checked_push(frame, (bjvm_stack_value){.obj = obj});
        break;
      }
      default:
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_ldc2_w: {
      const bjvm_cp_entry *ent = insn->cp;
      switch (ent->kind) {
      case BJVM_CP_KIND_LONG:
        checked_push(frame, (bjvm_stack_value){.l = ent->integral.value});
        break;
      case BJVM_CP_KIND_DOUBLE:
        checked_push(frame, (bjvm_stack_value){.d = ent->floating.value});
        break;
      default:
        UNREACHABLE();
      }
      NEXT_INSN;
    }
    bjvm_insn_aload:
    bjvm_insn_lload:
    bjvm_insn_iload:
    bjvm_insn_fload:
    bjvm_insn_dload: {
      checked_push(frame, frame->values[frame->max_stack + insn->index]);
      NEXT_INSN;
    }
    bjvm_insn_dstore:
    bjvm_insn_fstore:
    bjvm_insn_astore:
    bjvm_insn_lstore:
    bjvm_insn_istore: {
      frame->values[frame->max_stack + insn->index] = checked_pop(frame);
      NEXT_INSN;
    }
    bjvm_insn_goto: {
      frame->program_counter = insn->index;
      JMP_INSN;
    }
    bjvm_insn_jsr:
      UNREACHABLE("bjvm_insn_jsr");
      NEXT_INSN;
    bjvm_insn_if_acmpeq: {
      bjvm_obj_header *a = checked_pop(frame).obj, *b = checked_pop(frame).obj;
      if (a == b) {
        frame->program_counter = insn->index;
        JMP_INSN;
      }
      NEXT_INSN;
    }
    bjvm_insn_if_acmpne: {
      bjvm_obj_header *a = checked_pop(frame).obj, *b = checked_pop(frame).obj;
      if (a != b) {
        frame->program_counter = insn->index;
        JMP_INSN;
      }
      NEXT_INSN;
    }

#define MAKE_INT_COMPARE(op)                                                   \
  {                                                                            \
    int b = checked_pop(frame).i, a = checked_pop(frame).i;                    \
    if (a op b) {                                                              \
      frame->program_counter = insn->index;                                     \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                     \
  }
    bjvm_insn_if_icmpeq:
      MAKE_INT_COMPARE(==);
    bjvm_insn_if_icmpne:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_if_icmplt:
      MAKE_INT_COMPARE(<);
    bjvm_insn_if_icmpge:
      MAKE_INT_COMPARE(>=);
    bjvm_insn_if_icmpgt:
      MAKE_INT_COMPARE(>);
    bjvm_insn_if_icmple:
      MAKE_INT_COMPARE(<=);

#undef MAKE_INT_COMPARE
#define MAKE_INT_COMPARE(op)                                                   \
  {                                                                            \
    int a = checked_pop(frame).i;                                              \
    if (a op 0) {                                                              \
      frame->program_counter = insn->index;                                     \
      JMP_INSN;                                                                \
    }                                                                          \
    NEXT_INSN;                                                                     \
  }

    bjvm_insn_ifeq:
      MAKE_INT_COMPARE(==);
    bjvm_insn_ifne:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_iflt:
      MAKE_INT_COMPARE(<);
    bjvm_insn_ifge:
      MAKE_INT_COMPARE(>=);
    bjvm_insn_ifgt:
      MAKE_INT_COMPARE(>);
    bjvm_insn_ifle:
      MAKE_INT_COMPARE(<=);
    bjvm_insn_ifnonnull:
      MAKE_INT_COMPARE(!=);
    bjvm_insn_ifnull:
      MAKE_INT_COMPARE(==)

#undef MAKE_INT_COMPARE
    bjvm_insn_iconst: {
      checked_push(frame, (bjvm_stack_value){.i = (int)insn->integer_imm});
      NEXT_INSN;
    }
    bjvm_insn_dconst: {
      checked_push(frame, (bjvm_stack_value){.d = (int)insn->d_imm});
      NEXT_INSN;
    }
    bjvm_insn_fconst: {
      checked_push(frame, (bjvm_stack_value){.f = (int)insn->f_imm});
      NEXT_INSN;
    }
    bjvm_insn_lconst: {
      checked_push(frame, (bjvm_stack_value){.l = insn->integer_imm});
      NEXT_INSN;
    }
    bjvm_insn_iinc: {
      frame->values[frame->max_stack + insn->iinc.index].i += insn->iinc.const_;
      NEXT_INSN;
    }
    bjvm_insn_multianewarray: {
      if (bjvm_multianewarray(thread, frame, &insn->multianewarray))
        goto done;
      NEXT_INSN;
    }
    bjvm_insn_newarray: {
      int count = checked_pop(frame).i;
      bjvm_obj_header *array =
          create_primitive_array(thread, insn->array_type, count);
      if (array) {
        checked_push(frame, (bjvm_stack_value){.obj = array});
      } else {  // failed to create array
        goto done;
      }
      NEXT_INSN;
    }
    bjvm_insn_tableswitch: {
      int value = checked_pop(frame).i;
      struct bjvm_bc_tableswitch_data data = insn->tableswitch;
      if (value < data.low || value > data.high) {
        frame->program_counter = data.default_target;
        JMP_INSN;
      }
      frame->program_counter = data.targets[value - data.low];
      JMP_INSN;
    }
    bjvm_insn_lookupswitch: {
      int value = checked_pop(frame).i;
      struct bjvm_bc_lookupswitch_data data = insn->lookupswitch;
      bool found = false;
      for (int i = 0; i < data.keys_count; ++i) {
        if (data.keys[i] == value) {
          frame->program_counter = data.targets[i];
          found = true;
          break;
        }
      }

      if (!found)
        frame->program_counter = data.default_target;
      JMP_INSN;
    }
    bjvm_insn_ret:
      UNREACHABLE("bjvm_insn_ret");
      NEXT_INSN;
    }

    frame->program_counter++;
  }

done:;
  if (thread->current_exception != NULL) {
    bjvm_attribute_exception_table *table = method->code->exception_table;
    if (table) {
      int pc = frame->program_counter;
      for (int i = 0; i < table->entries_count; ++i) {
        bjvm_exception_table_entry ent = table->entries[i];
        if (ent.start_insn <= pc && pc < ent.end_insn) {
          if (ent.catch_type) {
            int error = bjvm_resolve_class(thread, ent.catch_type);
            if (error) goto done;  // O dear
          }
          if (!ent.catch_type ||
              bjvm_instanceof(thread->current_exception->descriptor,
                              ent.catch_type->classdesc)) {
            frame->program_counter = ent.handler_pc;
            frame->stack_depth = 1;
            frame->values[0] =
                (bjvm_stack_value){.obj = thread->current_exception};
            thread->current_exception = NULL;

            goto start;
          }
        }
      }
    }

    return -1;
  }

  return 0;
}

bjvm_obj_header *get_main_thread_group(bjvm_thread *thread) {
  bjvm_vm *vm = thread->vm;
  if (!vm->main_thread_group) {
    bjvm_classdesc *ThreadGroup =
        bootstrap_class_create(thread, L"java/lang/ThreadGroup");
    int error = bjvm_initialize_class(thread, ThreadGroup);
    assert(!error);

    bjvm_cp_method *init =
        bjvm_easy_method_lookup(ThreadGroup, "<init>", "()V", false, false);

    assert(init);

    bjvm_obj_header *thread_group = new_object(thread, ThreadGroup);
    vm->main_thread_group = thread_group;
    bjvm_stack_value args[1] = {(bjvm_stack_value){.obj = thread_group}};
    bjvm_thread_run(thread, init, args, NULL);
  }
  return vm->main_thread_group;
}
