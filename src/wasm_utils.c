#include "wasm_utils.h"

enum {
  SECTION_ID_TYPE = 1,
  SECTION_ID_IMPORT = 2,
  SECTION_ID_FUNCTION = 3,
  SECTION_ID_TABLE = 4,
  SECTION_ID_MEMORY = 5,
  SECTION_ID_GLOBAL = 6,
};

#define MODULE_ALLOCATION_SIZE_BYTES (1 << 16)

static char* module_malloc(bjvm_wasm_module *module, int size) {
  if (size > MODULE_ALLOCATION_SIZE_BYTES) {
    // Too big for an arena allocation, just make a new mf
    if (module->arenas_count) {
      char* last_arena = module->arenas[module->arenas_count - 1];
      *VECTOR_PUSH(module->arenas, module->arenas_count, module->arenas_cap) = last_arena;
      return module->arenas[module->arenas_count - 2] = malloc(size);
    }
    return *VECTOR_PUSH(module->arenas, module->arenas_count, module->arenas_count) = malloc(size);
  }
  if (module->last_arena_used + size < MODULE_ALLOCATION_SIZE_BYTES) {
    char* last_arena = module->arenas[module->arenas_count - 1];
    char* result = last_arena + module->last_arena_used;
    module->last_arena_used += size;
    return result;
  }
  char* new_arena = malloc(MODULE_ALLOCATION_SIZE_BYTES);
  *VECTOR_PUSH(module->arenas, module->arenas_count, module->arenas_cap) = new_arena;
  module->last_arena_used = size;
  return new_arena;
}

static char* module_calloc(bjvm_wasm_module *module, int size) {
  char* result = module_malloc(module, size);
  memset(result, 0, size);
  return result;
}

// Write the byte to the serialization context
void write_byte(bjvm_bytevector *ctx, int byte) {
  assert(byte >= 0 && byte <= 255);
  *VECTOR_PUSH(ctx->bytes, ctx->bytes_len, ctx->bytes_cap) = byte;
}

// Write the bytes to the serialization context
void write_slice(bjvm_bytevector *ctx, const uint8_t* bytes, size_t len) {
  size_t new_length = ctx->bytes_len + len;
  if (new_length > ctx->bytes_cap) {
    size_t new_cap = ctx->bytes_cap * 2;
    if (new_cap < new_length)
      new_cap = new_length;
    ctx->bytes = realloc(ctx->bytes, new_cap);
    assert(ctx->bytes);
    ctx->bytes_cap = new_cap;
  }
  memcpy(ctx->bytes + ctx->bytes_len, bytes, len);
  ctx->bytes_len = new_length;
}

void write_u32(bjvm_bytevector *ctx, uint32_t value) {
  uint8_t out[4];
  memcpy(out, &value, 4);
  write_slice(ctx, out, 4);
}

void write_f32(bjvm_bytevector *ctx, float value) {
  uint8_t out[4];
  memcpy(out, &value, 4);
  write_slice(ctx, out, 4);
}

void write_f64(bjvm_bytevector *ctx, double value) {
  uint8_t out[8];
  memcpy(out, &value, 8);
  write_slice(ctx, out, 8);
}

void write_string(bjvm_bytevector *ctx, const char *str) {
  size_t len = strlen(str);
  write_u32(ctx, len);
  write_slice(ctx, (const uint8_t *)str, len); // lol what r u gonna do about it
}

void bjvm_wasm_writeuint(bjvm_bytevector *ctx, uint64_t value) {
  // Credit: https://en.wikipedia.org/wiki/LEB128
  uint8_t out[16], *write = out;
  do {
    uint8_t byte = value & 0x7F;
    value >>= 7;
    *write++ = byte | (value != 0) << 7;
  } while (value != 0);
  write_slice(ctx, out, write - out);
}

void bjvm_wasm_writeint(bjvm_bytevector *ctx, int64_t value) {
  // Credit: https://en.wikipedia.org/wiki/LEB128
  uint8_t byte;
  while (true) {
    byte = value & 0x7F;
    value >>= 7;
    // termination argument: fixed point of x <- x >> 7 is -1 or 0
    if ((value == 0 && !(byte & 0x40)) || (value == -1 && byte & 0x40))
      break;
    write_byte(ctx, byte | 0x80);
  }
  write_byte(ctx, byte);
}

bjvm_wasm_module * bjvm_wasm_module_create() {
  bjvm_wasm_module *module = calloc(1, sizeof(bjvm_wasm_module));
  return module;
}

void serialize_typesection(bjvm_bytevector *result, bjvm_wasm_module *module) {

}

bjvm_bytevector
bjvm_wasm_module_serialize(bjvm_wasm_module *module) {
  const char *WASM_MAGIC = "\0asm";
  bjvm_bytevector result = {nullptr};
  write_string(&result, WASM_MAGIC);
  write_u32(&result, 1);  // version

  serialize_typesection(&result, module);
  // serialize_importsection(&result, module);
  // serialize_functionsection(&result, module);
  // serialize_tablesection(&result, module);
  // serialize_memorysection(&result, module);
  // serialize_globalsection(&result, module);
  // serialize_exportsection(&result, module);
  // serialize_startsection(&result, module);
  // serialize_elementsection(&result, module);
  // serialize_codesection(&result, module);
  // serialize_datasection(&result, module);

  return result;
}

void bjvm_wasm_module_destroy(bjvm_wasm_module *module) {
  for (int i = 0; i < module->arenas_count; ++i) {
    free(module->arenas[i]);
  }
  free(module->arenas);
  free(module);
}

