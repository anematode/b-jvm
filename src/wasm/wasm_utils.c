#include "wasm_utils.h"
#include <limits.h>

enum {
  SECTION_ID_TYPE = 1,
  SECTION_ID_IMPORT = 2,
  SECTION_ID_FUNCTION = 3,
  SECTION_ID_TABLE = 4,
  SECTION_ID_MEMORY = 5,
  SECTION_ID_GLOBAL = 6,
};

#define MODULE_ALLOCATION_SIZE_BYTES (1 << 16)

#ifndef EMSCRIPTEN
#define EMSCRIPTEN_KEEPALIVE // so that it compiles on non-emscripten
#else
#include <emscripten/emscripten.h>
#endif

static void *module_malloc(bjvm_wasm_module *module, size_t size) {
  if (size > MODULE_ALLOCATION_SIZE_BYTES) {
    // Too big for an arena allocation, just make a new mf
    if (module->arenas_count) {
      char *last_arena = module->arenas[module->arenas_count - 1];
      *VECTOR_PUSH(module->arenas, module->arenas_count, module->arenas_cap) =
          last_arena;
      return module->arenas[module->arenas_count - 2] = malloc(size);
    }
    return *VECTOR_PUSH(module->arenas, module->arenas_count,
                        module->arenas_count) = malloc(size);
  }
  void *result;
  if (module->last_arena_used + size < MODULE_ALLOCATION_SIZE_BYTES) {
    char *last_arena = module->arenas[module->arenas_count - 1];
    result = last_arena + module->last_arena_used;
    module->last_arena_used += size;
  } else {
    result = malloc(MODULE_ALLOCATION_SIZE_BYTES);
    *VECTOR_PUSH(module->arenas, module->arenas_count, module->arenas_cap) =
        result;
    module->last_arena_used = size;
  }
  // align to 8 bytes
  if (size % 8)
    module->last_arena_used += 8 - (size % 8);
  return result;
}

static void *module_calloc(bjvm_wasm_module *module, int size) {
  char *result = module_malloc(module, size);
  memset(result, 0, size);
  return result;
}

static void *module_copy(bjvm_wasm_module *module, const void *src, int size) {
  void *dest = module_malloc(module, size);
  memcpy(dest, src, size);
  return dest;
}

static bjvm_wasm_type from_basic_type(bjvm_wasm_value_type kind) {
  return (bjvm_wasm_type){.val = (uintptr_t)kind};
}

static bjvm_wasm_expression *module_expr(bjvm_wasm_module *module,
                                         bjvm_wasm_expr_kind kind) {
  bjvm_wasm_expression *result =
      module_calloc(module, sizeof(bjvm_wasm_expression));
  result->kind = kind;
  return result;
}

// Write the byte to the serialization context
void write_byte(bjvm_bytevector *ctx, int byte) {
  assert(byte >= 0 && byte <= 255);
  *VECTOR_PUSH(ctx->bytes, ctx->bytes_len, ctx->bytes_cap) = byte;
}

// Write the bytes to the serialization context
void write_slice(bjvm_bytevector *ctx, const uint8_t *bytes, size_t len) {
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
  bjvm_wasm_writeuint(ctx, len);
  write_slice(ctx, (const uint8_t *)str, len); // lol what r u gonna do about it
}

bjvm_wasm_type bjvm_wasm_void() {
  return (bjvm_wasm_type){.val = BJVM_WASM_TYPE_KIND_VOID};
}

bjvm_wasm_type bjvm_wasm_int32() {
  return (bjvm_wasm_type){.val = BJVM_WASM_TYPE_KIND_INT32};
}

bjvm_wasm_type bjvm_wasm_float32() {
  return (bjvm_wasm_type){.val = BJVM_WASM_TYPE_KIND_FLOAT32};
}

bjvm_wasm_type bjvm_wasm_float64() {
  return (bjvm_wasm_type){.val = BJVM_WASM_TYPE_KIND_FLOAT64};
}

bjvm_wasm_type bjvm_wasm_int64() {
  return (bjvm_wasm_type){.val = BJVM_WASM_TYPE_KIND_INT64};
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

bjvm_wasm_module *bjvm_wasm_module_create() {
  bjvm_wasm_module *module = calloc(1, sizeof(bjvm_wasm_module));
  module->last_arena_used = INT_MAX / 2;
  return module;
}

uint32_t bjvm_register_function_type(bjvm_wasm_module *module,
                                       bjvm_wasm_type params,
                                       bjvm_wasm_type results) {
  bjvm_wasm_ser_function_type search = {params, results};
  // Search function types for an existing one. Since we intern types we can
  // compare by value
  for (int i = 0; i < module->fn_types_count; ++i) {
    if (memcmp(module->fn_types + i, &search,
               sizeof(bjvm_wasm_ser_function_type)) == 0)
      return i;
  }

  *VECTOR_PUSH(module->fn_types, module->fn_types_count, module->fn_types_cap) =
      search;
  return module->fn_types_count - 1;
}

void write_tuple_type(bjvm_bytevector *result, bjvm_wasm_type params) {
  bjvm_wasm_tuple_type *tuple = bjvm_wasm_get_tuple_type(params);
  if (tuple) {
    bjvm_wasm_writeuint(result, tuple->types_len);
    for (int i = 0; i < tuple->types_len; ++i) {
      bjvm_wasm_value_type kind = tuple->types[i];
      write_byte(result, kind);
    }
  } else {
    if (params.val == BJVM_WASM_TYPE_KIND_VOID) {
      write_byte(result, 0x00);
    } else {
      write_byte(result, 0x01);
      write_byte(result, params.val);
    }
  }
}

void serialize_typesection(bjvm_bytevector *result, bjvm_wasm_module *module) {
  write_byte(result, SECTION_ID_TYPE);
  bjvm_bytevector sect = {nullptr};
  bjvm_wasm_writeuint(&sect, module->fn_types_count);
  for (int i = 0; i < module->fn_types_count; ++i) {
    bjvm_wasm_ser_function_type *fn_type = module->fn_types + i;
    write_byte(&sect, 0x60); // function type
    write_tuple_type(&sect, fn_type->params);
    write_tuple_type(&sect, fn_type->results);
  }
  bjvm_wasm_writeuint(result, sect.bytes_len);
  write_slice(result, sect.bytes, sect.bytes_len);
  free(sect.bytes);
}

void serialize_functionsection(bjvm_bytevector *result,
                               bjvm_wasm_module *module) {
  write_byte(result, SECTION_ID_FUNCTION);
  bjvm_bytevector sect = {nullptr};
  bjvm_wasm_writeuint(&sect, module->function_count);
  for (int i = 0; i < module->function_count; ++i) {
    bjvm_wasm_function *fn = module->functions[i];
    uint32_t typeidx = bjvm_register_function_type(module, fn->params, fn->results);
    bjvm_wasm_writeuint(&sect, typeidx);
    fn->my_index = module->fn_index++;
  }
  bjvm_wasm_writeuint(result, sect.bytes_len);
  write_slice(result, sect.bytes, sect.bytes_len);
  free(sect.bytes);
}

void serialize_importsection(bjvm_bytevector *result,
                             bjvm_wasm_module *module) {
  const int PREDEFINED_IMPORT_COUNT = 2; // memory
  write_byte(result, SECTION_ID_IMPORT);

  bjvm_bytevector sect = {nullptr};
  bjvm_wasm_writeuint(&sect, module->import_count + PREDEFINED_IMPORT_COUNT);
  // Predefined imports
  write_string(&sect, "env2");
  write_string(&sect, "memory");
  write_byte(&sect, BJVM_WASM_IMPORT_KIND_MEMORY); // memory
  write_byte(&sect, 0x00);                         // flags
  bjvm_wasm_writeuint(&sect, 1);                   // initial
  // Import table
  write_string(&sect, "env2");
  write_string(&sect, "table");
  write_byte(&sect, BJVM_WASM_IMPORT_KIND_TABLE);
  write_byte(&sect, 0x70); // funcref
  write_byte(&sect, 0x00); // only minimum limit present
  bjvm_wasm_writeuint(&sect, 1); // initial

  for (int i = 0; i < module->import_count; ++i) {
    bjvm_wasm_import *import = module->imports + i;
    write_string(&sect, import->module);
    write_string(&sect, import->name);
    write_byte(&sect, BJVM_WASM_IMPORT_KIND_FUNC);
    bjvm_wasm_writeuint(&sect, import->func.type);
    import->func.associated->my_index = i;
  }
  module->fn_index = module->import_count;
  bjvm_wasm_writeuint(result, sect.bytes_len);
  write_slice(result, sect.bytes, sect.bytes_len);
  free(sect.bytes);
}

typedef struct expression_ser_ctx {
  bjvm_wasm_module *module;
  bjvm_wasm_function *enclosing;
  bjvm_wasm_expression *associated_block;

  // Pointer to previous block: used when computing label depths
  struct expression_ser_ctx *prev_block_ctx;
} expression_ser_ctx;

uint32_t walk_to_find_label(expression_ser_ctx *ctx,
                            bjvm_wasm_expression *break_to) {
  uint32_t i = 0;
  while (ctx && ctx->associated_block != break_to) {
    // printf("Seeking block %p, found %p\n", break_to, ctx->associated_block);
    ++i;
    ctx = ctx->prev_block_ctx;
  }
  assert(ctx && "trying to branch to a non-enclosing block");
  return i;
}

void serialize_expression(expression_ser_ctx *ctx, bjvm_bytevector *body,
                          bjvm_wasm_expression *expr) {
  switch (expr->kind) {
  case BJVM_WASM_EXPR_KIND_DROP:
    write_byte(body, 0x1A);
    break;
  case BJVM_WASM_EXPR_KIND_UNREACHABLE: {
    write_byte(body, 0x00);
    break;
  }
  case BJVM_WASM_EXPR_KIND_RETURN:
    if (expr->return_expr) {
      serialize_expression(ctx, body, expr->return_expr);
    }
    write_byte(body, 0x0F);
    break;
  case BJVM_WASM_EXPR_KIND_CALL: {
    for (int i = 0; i < expr->call.arg_count; ++i)
      serialize_expression(ctx, body, expr->call.args[i]);
    write_byte(body, 0x10);
    bjvm_wasm_writeuint(body, expr->call.to_call->my_index);
    break;
  }
  case BJVM_WASM_EXPR_KIND_CALL_INDIRECT: {
    for (int i = 0; i < expr->call_indirect.arg_count; ++i)
      serialize_expression(ctx, body, expr->call_indirect.args[i]);
    serialize_expression(ctx, body, expr->call_indirect.index);
    write_byte(body, expr->call_indirect.tail_call ? 0x13 : 0x11);
    bjvm_wasm_writeuint(body, expr->call_indirect.function_type);
    bjvm_wasm_writeuint(body, expr->call_indirect.table_index);
    break;
  }
  case BJVM_WASM_EXPR_KIND_CONST: {
    write_byte(body, expr->literal.kind);
    switch (expr->literal.kind) { // int
    case BJVM_WASM_LITERAL_KIND_I32:
      int32_t value;
      memcpy(&value, expr->literal.bytes, 4);
      bjvm_wasm_writeint(body, value);
      break;
    case BJVM_WASM_LITERAL_KIND_F32: {
      float value;
      memcpy(&value, expr->literal.bytes, 4);
      write_f32(body, value);
      break;
    }
    case BJVM_WASM_LITERAL_KIND_F64: {
      double value;
      memcpy(&value, expr->literal.bytes, 8);
      write_f64(body, value);
      break;
    }
    case BJVM_WASM_LITERAL_KIND_I64: {
      int64_t value;
      memcpy(&value, expr->literal.bytes, 8);
      bjvm_wasm_writeint(body, value);
      break;
    }
    }
    break;
  }
  case BJVM_WASM_EXPR_KIND_LOAD: {
    serialize_expression(ctx, body, expr->load.addr);
    write_byte(body, expr->load.op);
    write_byte(body, expr->load.align);
    bjvm_wasm_writeuint(body, expr->load.offset);
    break;
  }
  case BJVM_WASM_EXPR_KIND_STORE: {
    serialize_expression(ctx, body, expr->store.addr);
    serialize_expression(ctx, body, expr->store.value);
    write_byte(body, expr->store.op);
    write_byte(body, expr->store.align);
    bjvm_wasm_writeuint(body, expr->store.offset);
    break;
  }
  case BJVM_WASM_EXPR_KIND_SELECT:
    // (select (condition) (true_expr) (false_expr))
    serialize_expression(ctx, body, expr->select.true_expr);
    serialize_expression(ctx, body, expr->select.false_expr);
    serialize_expression(ctx, body, expr->select.condition);
    write_byte(body, 0x1B);
    break;
  case BJVM_WASM_EXPR_KIND_GET_LOCAL: {
    write_byte(body, 0x20);
    bjvm_wasm_writeuint(body, expr->local_get);
    break;
  }
  case BJVM_WASM_EXPR_KIND_SET_LOCAL: {
    serialize_expression(ctx, body, expr->local_set.value);
    write_byte(body, 0x21);
    bjvm_wasm_writeuint(body, expr->local_set.local_index);
    break;
  }
  case BJVM_WASM_EXPR_KIND_UNARY_OP: {
    serialize_expression(ctx, body, expr->unary_op.arg);
    bjvm_wasm_unary_op_kind op = expr->unary_op.op;
    if (op > 0xff)
      write_byte(body, 0xFC); // extended opcode
    write_byte(body, op & 0xff);
    break;
  }
  case BJVM_WASM_EXPR_KIND_BINARY_OP: {
    serialize_expression(ctx, body, expr->binary_op.left);
    serialize_expression(ctx, body, expr->binary_op.right);
    bjvm_wasm_binary_op_kind op = expr->binary_op.op;
    write_byte(body, op);
    break;
  }
  case BJVM_WASM_EXPR_KIND_BLOCK: {
    write_byte(body, expr->block.is_loop ? 0x03 : 0x02);
    // For now, all of our blocks are epsilon so just write 0x40 but we will
    // need to do more advanced stuff soon
    write_byte(body, 0x40);
    expression_ser_ctx new_ctx = *ctx;
    new_ctx.associated_block = expr;
    new_ctx.prev_block_ctx = ctx;
    for (int i = 0; i < expr->block.list.expr_count; ++i) {
      serialize_expression(&new_ctx, body, expr->block.list.exprs[i]);
    }
    write_byte(body, 0x0B); // end block
    break;
  }
  case BJVM_WASM_EXPR_KIND_IF: {
    serialize_expression(ctx, body, expr->if_.condition);
    write_byte(body, 0x04);
    write_byte(body, 0x40); // TODO add block types
    expression_ser_ctx new_ctx = *ctx;
    new_ctx.associated_block = expr;
    new_ctx.prev_block_ctx = ctx;
    serialize_expression(&new_ctx, body, expr->if_.true_expr);
    if (expr->if_.false_expr) {
      write_byte(body, 0x05);
      serialize_expression(&new_ctx, body, expr->if_.false_expr);
    }
    write_byte(body, 0x0B); // end if
    break;
  }
  case BJVM_WASM_EXPR_KIND_BR: {
    if (expr->br.condition) {
      serialize_expression(ctx, body, expr->br.condition);
    }
    write_byte(body, expr->br.condition ? 0x0D : 0x0C);
    uint32_t label_index = walk_to_find_label(ctx, expr->br.break_to);
    bjvm_wasm_writeuint(body, label_index);
    break;
  }
  case BJVM_WASM_EXPR_KIND_BR_TABLE: {
    serialize_expression(ctx, body, expr->br_table.condition);

    write_byte(body, 0x0E);
    bjvm_wasm_writeuint(body, expr->br_table.expr_count);
    for (int i = 0; i < expr->br_table.expr_count; ++i) {
      uint32_t label_index = walk_to_find_label(ctx, expr->br_table.exprs[i]);
      bjvm_wasm_writeuint(body, label_index);
    }
    uint32_t label_index = walk_to_find_label(ctx, expr->br_table.dflt);
    bjvm_wasm_writeuint(body, label_index);
    break;
  }
  default:
    UNREACHABLE();
  }
}

void write_compressed_locals(bjvm_bytevector *body,
                             const bjvm_wasm_tuple_type *locals) {
  bjvm_bytevector types = {nullptr};
  int count = 0;
  if (locals && locals->types_len) {
    int i = 1, j = 0;
    for (; i < locals->types_len; ++i) {
      if (locals->types[i] != locals->types[i - 1]) {
        // i - j locals, all with the same type
        bjvm_wasm_writeuint(&types, i - j);
        write_byte(&types, locals->types[j]);
        j = i;
        ++count;
      }
    }
    bjvm_wasm_writeuint(&types, i - j);
    write_byte(&types, locals->types[j]);
    ++count;
  }
  bjvm_wasm_writeuint(body, count);
  write_slice(body, types.bytes, types.bytes_len);
  free(types.bytes);
}

void serialize_function_locals_and_code(bjvm_bytevector *body,
                                        bjvm_wasm_module *module,
                                        bjvm_wasm_function *function) {
  // Locals first
  const bjvm_wasm_tuple_type *locals =
      bjvm_wasm_get_tuple_type(function->locals);
  write_compressed_locals(body, locals);
  // Now write the expression
  expression_ser_ctx ctx = {module};
  ctx.enclosing = function;
  serialize_expression(&ctx, body, function->body);
  // End function
  write_byte(body, 0x00);
  write_byte(body, 0x0B);
}

void serialize_codesection(bjvm_bytevector *code_section,
                           bjvm_wasm_module *module) {
  write_byte(code_section, 0x0A);
  bjvm_bytevector body = {nullptr};
  bjvm_wasm_writeuint(&body, module->function_count);
  for (int i = 0; i < module->function_count; ++i) {
    bjvm_bytevector boi = {nullptr};
    serialize_function_locals_and_code(&boi, module, module->functions[i]);
    bjvm_wasm_writeuint(&body, boi.bytes_len);
    write_slice(&body, boi.bytes, boi.bytes_len);
    free(boi.bytes);
  }
  bjvm_wasm_writeuint(code_section, body.bytes_len);
  write_slice(code_section, body.bytes, body.bytes_len);
  free(body.bytes);
}

void serialize_exportsection(bjvm_bytevector *rest, bjvm_wasm_module *module) {
  write_byte(rest, 0x07);
  bjvm_bytevector sect = {nullptr};
  int export_count = 0;
  for (int i = 0; i < module->function_count; ++i) {
    if (module->functions[i]->exported)
      ++export_count;
  }
  bjvm_wasm_writeuint(&sect, export_count);
  for (int i = 0; i < module->function_count; ++i) {
    bjvm_wasm_function *fn = module->functions[i];
    if (fn->exported) {
      write_string(&sect, fn->name);
      write_byte(&sect, 0x00); // func
      bjvm_wasm_writeuint(&sect, fn->my_index);
    }
  }
  bjvm_wasm_writeuint(rest, sect.bytes_len);
  write_slice(rest, sect.bytes, sect.bytes_len);
  free(sect.bytes);
}

bjvm_bytevector bjvm_wasm_module_serialize(bjvm_wasm_module *module) {
  const char *WASM_MAGIC = "\0asm";
  bjvm_bytevector result = {nullptr};
  write_byte(&result, WASM_MAGIC[0]);
  write_byte(&result, WASM_MAGIC[1]);
  write_byte(&result, WASM_MAGIC[2]);
  write_byte(&result, WASM_MAGIC[3]);
  write_u32(&result, 1); // version

  bjvm_bytevector rest = {nullptr};
  serialize_importsection(&rest, module);
  serialize_functionsection(&rest, module);
  // serialize_tablesection(&rest, module);
  // serialize_memorysection(&rest, module);
  // serialize_globalsection(&rest, module);
  serialize_exportsection(&rest, module);
  // serialize_startsection(&rest, module);
  // serialize_elementsection(&rest, module);
  serialize_codesection(&rest, module);
  // serialize_datasection(&rest, module);
  serialize_typesection(&result, module);
  write_slice(&result, rest.bytes, rest.bytes_len);

  free(rest.bytes);

  return result;
}

void bjvm_wasm_module_free(bjvm_wasm_module *module) {
  for (int i = 0; i < module->arenas_count; ++i) {
    free(module->arenas[i]);
  }
  free(module->arenas);
  free(module->imports);
  free(module->interned_result_types);
  free(module->functions);
  free(module->fn_types);
  free(module);
}

bjvm_wasm_type bjvm_wasm_make_tuple(bjvm_wasm_module *module,
                                    bjvm_wasm_value_type *components,
                                    int length) {
  // Search for an existing tuple type
  for (int i = 0; i < module->result_types_count; ++i) {
    bjvm_wasm_tuple_type *tuple = module->interned_result_types[i];
    if (tuple->types_len != length)
      continue;
    if (memcmp(tuple->types, components,
               length * sizeof(bjvm_wasm_value_type)) == 0)
      return (bjvm_wasm_type){.val = (uintptr_t)tuple};
  }
  bjvm_wasm_tuple_type *tuple =
      module_malloc(module, sizeof(bjvm_wasm_tuple_type) +
                                length * sizeof(bjvm_wasm_value_type));
  tuple->types_len = length;
  memcpy(tuple->types, components, length * sizeof(bjvm_wasm_value_type));
  *VECTOR_PUSH(module->interned_result_types, module->result_types_count,
               module->result_types_cap) = tuple;
  return (bjvm_wasm_type){.val = (uintptr_t)tuple};
}

bjvm_wasm_tuple_type *bjvm_wasm_get_tuple_type(bjvm_wasm_type type) {
  if (type.val < 255)
    return nullptr;
  return (void *)type.val;
}

bjvm_wasm_value_type bjvm_wasm_get_basic_type(bjvm_wasm_type type) {
  assert(type.val < 255);
  return type.val;
}

void bjvm_wasm_export_function(bjvm_wasm_module *module,
                               bjvm_wasm_function *fn) {
  fn->exported = true;
}

const char *bjvm_wasm_copy_string(bjvm_wasm_module *module, const char *str) {
  char *result = module_malloc(module, strlen(str) + 1);
  strcpy(result, str);
  return result;
}

bjvm_wasm_function *
bjvm_wasm_add_function(bjvm_wasm_module *module, bjvm_wasm_type params,
                       bjvm_wasm_type results, bjvm_wasm_type locals,
                       bjvm_wasm_expression *body, const char *name) {
  bjvm_wasm_function *fn = module_calloc(module, sizeof(bjvm_wasm_function));
  fn->params = params;
  fn->results = results;
  fn->locals = locals;
  fn->body = body;
  fn->name = bjvm_wasm_copy_string(module, name);
  *VECTOR_PUSH(module->functions, module->function_count,
               module->function_cap) = fn;
  return fn;
}

static bjvm_wasm_value_type char_to_basic_type(char a) {
  switch (a) {
  case 'd':
    return BJVM_WASM_TYPE_KIND_FLOAT64;
  case 'i':
    return BJVM_WASM_TYPE_KIND_INT32;
  case 'j':
    return BJVM_WASM_TYPE_KIND_INT64;
  case 'f':
    return BJVM_WASM_TYPE_KIND_FLOAT32;
  case 'v':
    return BJVM_WASM_TYPE_KIND_VOID;
  default:
    UNREACHABLE();
  }
}

bjvm_wasm_type bjvm_wasm_string_to_tuple(bjvm_wasm_module *module,
                                         const char *str) {
  int len = strlen(str);
  bjvm_wasm_value_type types[256];
  assert(len < 256);
  int i = 0;
  for (; i < len; ++i) {
    types[i] = char_to_basic_type(str[i]);
  }
  return bjvm_wasm_make_tuple(module, types, i);
}

bjvm_wasm_function *
bjvm_wasm_import_runtime_function_impl(bjvm_wasm_module *module,
                                       const char *c_name, const char *params,
                                       const char *result, void *dummy) {
  (void)dummy;

  bjvm_wasm_function *fn = module_calloc(module, sizeof(bjvm_wasm_function));
  fn->params = bjvm_wasm_string_to_tuple(module, params);
  fn->results = from_basic_type(char_to_basic_type(result[0]));
  fn->locals = bjvm_wasm_void();
  const char *name_cpy = bjvm_wasm_copy_string(module, c_name);
  fn->name = name_cpy;
  bjvm_wasm_import *import =
      VECTOR_PUSH(module->imports, module->import_count, module->import_cap);
  import->module = "env";
  import->name = name_cpy;
  import->func = (bjvm_wasm_func_import){
      .type = bjvm_register_function_type(module, fn->params, fn->results),
      .associated = fn,
  };
  return fn;
}
bjvm_wasm_expression *bjvm_wasm_f32_const(bjvm_wasm_module *module,
                                          float value) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x43;
  memcpy(result->literal.bytes, &value, 4);
  return result;
}

bjvm_wasm_expression *bjvm_wasm_f64_const(bjvm_wasm_module *module,
                                          double value) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x44;
  memcpy(result->literal.bytes, &value, 8);
  return result;
}

bjvm_wasm_expression *bjvm_wasm_i32_const(bjvm_wasm_module *module,
                                          int32_t value) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x41;
  memcpy(result->literal.bytes, &value, 4);
  return result;
}

bjvm_wasm_expression *bjvm_wasm_i64_const(bjvm_wasm_module *module,
                                          int64_t value) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x42;
  memcpy(result->literal.bytes, &value, 8);
  return result;
}

bjvm_wasm_expression *bjvm_wasm_local_get(bjvm_wasm_module *module,
                                          uint32_t index, bjvm_wasm_type kind) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_GET_LOCAL);
  result->local_get = index;
  result->expr_type = kind;
  return result;
}

bjvm_wasm_expression *bjvm_wasm_local_set(bjvm_wasm_module *module,
                                          uint32_t index,
                                          bjvm_wasm_expression *value) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_SET_LOCAL);
  result->local_set =
      (bjvm_wasm_local_set_expression){.local_index = index, .value = value};
  result->expr_type = bjvm_wasm_void();
  return result;
}

bjvm_wasm_expression *bjvm_wasm_unreachable(bjvm_wasm_module *module) {
  return module_expr(module, BJVM_WASM_EXPR_KIND_UNREACHABLE);
}

bjvm_wasm_expression *bjvm_wasm_binop(bjvm_wasm_module *module,
                                      bjvm_wasm_binary_op_kind op,
                                      bjvm_wasm_expression *left,
                                      bjvm_wasm_expression *right) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_BINARY_OP);
  result->binary_op = (bjvm_wasm_binary_expression){
      .op = op,
      .left = left,
      .right = right,
  };
  return result;
}

bjvm_wasm_expression *bjvm_wasm_select(bjvm_wasm_module *module,
                                       bjvm_wasm_expression *condition,
                                       bjvm_wasm_expression *true_expr,
                                       bjvm_wasm_expression *false_expr) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_SELECT);
  result->select = (bjvm_wasm_select_expression){
      .condition = condition,
      .true_expr = true_expr,
      .false_expr = false_expr,
  };
  return result;
}

bjvm_wasm_expression *
bjvm_wasm_update_block(bjvm_wasm_module *module,
                       bjvm_wasm_expression *existing_block,
                       bjvm_wasm_expression **exprs, int expr_count,
                       bjvm_wasm_type type, bool is_loop) {
  // Create a block expression
  existing_block->block = (bjvm_wasm_block_expression){
      .list =
          (bjvm_wasm_expression_list){
              .exprs = module_copy(module, exprs,
                                   sizeof(bjvm_wasm_expression *) * expr_count),
              .expr_count = expr_count,
          },
      .is_loop = is_loop};
  existing_block->expr_type = type;
  return existing_block;
}

bjvm_wasm_expression *bjvm_wasm_block(bjvm_wasm_module *module,
                                      bjvm_wasm_expression **exprs,
                                      int expr_count, bjvm_wasm_type type,
                                      bool is_loop) {
  bjvm_wasm_expression *block = module_expr(module, BJVM_WASM_EXPR_KIND_BLOCK);
  return bjvm_wasm_update_block(module, block, exprs, expr_count, type,
                                is_loop);
}

bjvm_wasm_expression *bjvm_wasm_br(bjvm_wasm_module *module,
                                   bjvm_wasm_expression *condition,
                                   bjvm_wasm_expression *break_to) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_BR);
  result->br = (bjvm_wasm_br_expression){
      .condition = condition,
      .break_to = break_to,
  };
  return result;
}

bjvm_wasm_expression *bjvm_wasm_call(bjvm_wasm_module *module,
                                     bjvm_wasm_function *fn,
                                     bjvm_wasm_expression **args,
                                     int arg_count) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_CALL);
  bjvm_wasm_expression **cpy =
      module_copy(module, args, sizeof(bjvm_wasm_expression *) * arg_count);
  result->call = (bjvm_wasm_call_expression){
      .to_call = fn,
      .args = cpy,
      .arg_count = arg_count,
  };
  return result;
}

bjvm_wasm_expression *bjvm_wasm_call_indirect(bjvm_wasm_module *module,
                                              int table_index,
                                              bjvm_wasm_expression *index,
                                              bjvm_wasm_expression **args,
                                              int arg_count,
                                              uint32_t functype) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_CALL_INDIRECT);
  bjvm_wasm_expression **cpy =
      module_copy(module, args, sizeof(bjvm_wasm_expression*) * arg_count);
  result->call_indirect = (bjvm_wasm_call_indirect_expression){
      .table_index = table_index,
      .index = index,
      .args = cpy,
      .arg_count = arg_count,
      .function_type = functype
  };
  return result;
}

bjvm_wasm_expression *bjvm_wasm_load(bjvm_wasm_module *module,
                                     bjvm_wasm_load_op_kind op,
                                     bjvm_wasm_expression *addr, int align,
                                     int offset) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_LOAD);
  result->load = (bjvm_wasm_load_expression){
      .op = op,
      .addr = addr,
      .align = align,
      .offset = offset,
  };
  // TODO calculate expr_type
  return result;
}

bjvm_wasm_expression *bjvm_wasm_store(bjvm_wasm_module *module,
                                      bjvm_wasm_store_op_kind op,
                                      bjvm_wasm_expression *addr,
                                      bjvm_wasm_expression *value, int align,
                                      int offset) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_STORE);
  result->store = (bjvm_wasm_store_expression){
      .op = op,
      .addr = addr,
      .value = value,
      .align = align,
      .offset = offset,
  };
  return result;
}

bjvm_wasm_expression *bjvm_wasm_if_else(bjvm_wasm_module *module,
                                        bjvm_wasm_expression *cond,
                                        bjvm_wasm_expression *true_expr,
                                        bjvm_wasm_expression *false_expr,
                                        bjvm_wasm_type type) {
  bjvm_wasm_expression *result = module_expr(module, BJVM_WASM_EXPR_KIND_IF);
  result->if_ = (bjvm_wasm_if_expression){
      .condition = cond,
      .true_expr = true_expr,
      .false_expr = false_expr,
  };
  result->expr_type = type;
  return result;
}

bjvm_wasm_expression *bjvm_wasm_return(bjvm_wasm_module *module,
                                       bjvm_wasm_expression *expr) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_RETURN);
  result->return_expr = expr;
  return result;
}

EMSCRIPTEN_KEEPALIVE
void bjvm_wasm_push_export(bjvm_wasm_instantiation_result *result,
                           const char *name, void *exported_func) {
  bjvm_wasm_instantiation_export *exp =
      *VECTOR_PUSH(result->exports, result->export_count, result->export_cap);
  exp->name = make_heap_str_from(
      (bjvm_utf8){.chars = (char *)name, .len = strlen(name)});
  exp->export_ = exported_func;
}

void bjvm_free_wasm_instantiation_result(
    bjvm_wasm_instantiation_result *result) {
  // Delete function pointers from the table
  for (int i = 0; i < result->export_count; ++i) {
    free_heap_str(result->exports[i]->name);
#ifdef EMSCRIPTEN
    EM_ASM_(
        { removeFunction(wasmTable.get($0)); },
        (intptr_t)result->exports[i]->export_);
#endif
  }
  free(result);
}

bjvm_wasm_instantiation_result *
bjvm_wasm_instantiate_module(bjvm_wasm_module *module, const char *debug_name) {
  bjvm_wasm_instantiation_result *result =
      calloc(1, sizeof(bjvm_wasm_instantiation_result));
#ifndef EMSCRIPTEN
  result->status = BJVM_WASM_INSTANTIATION_FAIL;
  return result;
#else // EMSCRIPTEN
  // Serialize the module
  bjvm_bytevector serialized = bjvm_wasm_module_serialize(module);
  int ptr = EM_ASM_INT(
      {
        var slice = HEAPU8.subarray($0, $1);
        try {
          var module = new WebAssembly.Module(slice);
          var instance = new WebAssembly.Instance(
              module, {env : wasmExports, env2 : {memory : wasmMemory, table : wasmTable }});
          let count = 0;
          for (var exp in instance.exports)
            count++;
          require("fs").writeFileSync("debug/test" + UTF8ToString($2) + ".wasm",
                                      slice);
          return addFunction(instance.exports.run, 'iiii');
        } catch (e) {
          // Exit Node.js
          console.log(e);
          require("fs").writeFileSync(
              "debug/broken" + UTF8ToString($2) + ".wasm", slice);
          process.exit(1);
          return 0;
        }
      },
      (intptr_t)serialized.bytes,
      (intptr_t)(serialized.bytes + serialized.bytes_len),
      (intptr_t)debug_name);
  if (ptr) {
    result->status = BJVM_WASM_INSTANTIATION_SUCCESS;
    result->run = (void *)ptr;
  } else {
    result->status = BJVM_WASM_INSTANTIATION_FAIL;
  }
#endif

  return result;
}

bjvm_wasm_expression *bjvm_wasm_unop(bjvm_wasm_module *module,
                                     bjvm_wasm_unary_op_kind op,
                                     bjvm_wasm_expression *expr) {
  bjvm_wasm_expression *result =
      module_expr(module, BJVM_WASM_EXPR_KIND_UNARY_OP);
  result->unary_op = (bjvm_wasm_unary_expression){
      .op = op,
      .arg = expr,
  };
  return result;
}

bjvm_wasm_type bjvm_jvm_type_to_wasm(bjvm_type_kind kind) {
  switch (kind) {
  case BJVM_TYPE_KIND_BOOLEAN:
  case BJVM_TYPE_KIND_CHAR:
  case BJVM_TYPE_KIND_BYTE:
  case BJVM_TYPE_KIND_SHORT:
  case BJVM_TYPE_KIND_INT:
  case BJVM_TYPE_KIND_REFERENCE:
    return bjvm_wasm_int32();
  case BJVM_TYPE_KIND_FLOAT:
    return bjvm_wasm_float32();
  case BJVM_TYPE_KIND_DOUBLE:
    return bjvm_wasm_float64();
  case BJVM_TYPE_KIND_LONG:
    return bjvm_wasm_int64();
  case BJVM_TYPE_KIND_VOID: [[fallthrough]];
  default:
    UNREACHABLE();
  }
}