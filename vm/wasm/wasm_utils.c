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

static void *module_malloc(wasm_module *module, size_t size) {
  if (size > MODULE_ALLOCATION_SIZE_BYTES) {
    // Too big for an arena allocation, just make a new mf
    if (module->arenas) {
      char *last_arena = arrlast(module->arenas);
      arrput(module->arenas, last_arena);
      return module->arenas[arrlen(module->arenas) - 2] = malloc(size);
    }
    void *res = malloc(size);
    arrput(module->arenas, res);
    return res;
  }
  void *result;
  if (module->last_arena_used + size < MODULE_ALLOCATION_SIZE_BYTES) {
    char *last_arena = arrlast(module->arenas);
    result = last_arena + module->last_arena_used;
    module->last_arena_used += size;
  } else {
    result = malloc(MODULE_ALLOCATION_SIZE_BYTES);
    arrput(module->arenas, result);
    module->last_arena_used = size;
  }
  // align to 8 bytes
  if (size % 8)
    module->last_arena_used += 8 - (size % 8);
  return result;
}

static void *module_calloc(wasm_module *module, int size) {
  char *result = module_malloc(module, size);
  memset(result, 0, size);
  return result;
}

static void *module_copy(wasm_module *module, const void *src, int size) {
  void *dest = module_malloc(module, size);
  memcpy(dest, src, size);
  return dest;
}

static wasm_type from_basic_type(wasm_value_type kind) { return (wasm_type){.val = (uintptr_t)kind}; }

static wasm_expression *module_expr(wasm_module *module, wasm_expr_kind kind) {
  wasm_expression *result = module_calloc(module, sizeof(wasm_expression));
  result->kind = kind;
  return result;
}

// Write the byte to the serialization context
void write_byte(bytevector *ctx, int byte) {
  DCHECK(byte >= 0 && byte <= 255);
  arrput(ctx->bytes, byte);
}

// Write the bytes to the serialization context
void write_slice(bytevector *ctx, const u8 *bytes, size_t len) { memcpy(arraddnptr(ctx->bytes, len), bytes, len); }

void write_u32(bytevector *ctx, u32 value) {
  u8 out[4];
  memcpy(out, &value, 4);
  write_slice(ctx, out, 4);
}

void write_f32(bytevector *ctx, float value) {
  u8 out[4];
  memcpy(out, &value, 4);
  write_slice(ctx, out, 4);
}

void write_f64(bytevector *ctx, double value) {
  u8 out[8];
  memcpy(out, &value, 8);
  write_slice(ctx, out, 8);
}

void write_string(bytevector *ctx, const char *str) {
  size_t len = strlen(str);
  wasm_writeuint(ctx, len);
  write_slice(ctx, (const u8 *)str, len); // lol what r u gonna do about it
}

wasm_type wasm_void() { return (wasm_type){.val = WASM_TYPE_KIND_VOID}; }

wasm_type wasm_int32() { return (wasm_type){.val = WASM_TYPE_KIND_INT32}; }

wasm_type wasm_float32() { return (wasm_type){.val = WASM_TYPE_KIND_FLOAT32}; }

wasm_type wasm_float64() { return (wasm_type){.val = WASM_TYPE_KIND_FLOAT64}; }

wasm_type wasm_int64() { return (wasm_type){.val = WASM_TYPE_KIND_INT64}; }

void wasm_writeuint(bytevector *ctx, u64 value) {
  // Credit: https://en.wikipedia.org/wiki/LEB128
  u8 out[16], *write = out;
  do {
    u8 byte = value & 0x7F;
    value >>= 7;
    *write++ = byte | (value != 0) << 7;
  } while (value != 0);
  write_slice(ctx, out, write - out);
}

void wasm_writeint(bytevector *ctx, s64 value) {
  // Credit: https://en.wikipedia.org/wiki/LEB128
  u8 byte;
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

wasm_module *wasm_module_create() {
  wasm_module *module = calloc(1, sizeof(wasm_module));
  module->last_arena_used = INT_MAX / 2;
  return module;
}

u32 register_function_type(wasm_module *module, wasm_type params, wasm_type results) {
  wasm_ser_function_type search = {params, results};
  // Search function types for an existing one. Since we intern types we can
  // compare by value
  for (int i = 0; i < arrlen(module->fn_types); ++i) {
    if (memcmp(module->fn_types + i, &search, sizeof(wasm_ser_function_type)) == 0)
      return i;
  }

  arrput(module->fn_types, search);
  return arrlen(module->fn_types) - 1;
}

void write_tuple_type(bytevector *result, wasm_type params) {
  wasm_tuple_type *tuple = wasm_get_tuple_type(params);
  if (tuple) {
    wasm_writeuint(result, tuple->types_len);
    for (int i = 0; i < tuple->types_len; ++i) {
      wasm_value_type kind = tuple->types[i];
      write_byte(result, kind);
    }
  } else {
    if (params.val == WASM_TYPE_KIND_VOID) {
      write_byte(result, 0x00);
    } else {
      write_byte(result, 0x01);
      write_byte(result, params.val);
    }
  }
}

void serialize_typesection(bytevector *result, wasm_module *module) {
  write_byte(result, SECTION_ID_TYPE);
  bytevector sect = {nullptr};
  wasm_writeuint(&sect, arrlen(module->fn_types));
  for (int i = 0; i < arrlen(module->fn_types); ++i) {
    wasm_ser_function_type *fn_type = module->fn_types + i;
    write_byte(&sect, 0x60); // function type
    write_tuple_type(&sect, fn_type->params);
    write_tuple_type(&sect, fn_type->results);
  }
  wasm_writeuint(result, arrlen(sect.bytes));
  write_slice(result, sect.bytes, arrlen(sect.bytes));
  arrfree(sect.bytes);
}

void serialize_functionsection(bytevector *result, wasm_module *module) {
  write_byte(result, SECTION_ID_FUNCTION);
  bytevector sect = {nullptr};
  wasm_writeuint(&sect, arrlen(module->functions));
  for (int i = 0; i < arrlen(module->functions); ++i) {
    wasm_function *fn = module->functions[i];
    u32 typeidx = register_function_type(module, fn->params, fn->results);
    wasm_writeuint(&sect, typeidx);
    fn->my_index = module->fn_index++;
  }
  wasm_writeuint(result, arrlen(sect.bytes));
  write_slice(result, sect.bytes, arrlen(sect.bytes));
  arrfree(sect.bytes);
}

void serialize_importsection(bytevector *result, wasm_module *module) {
  const int PREDEFINED_IMPORT_COUNT = 2; // memory
  write_byte(result, SECTION_ID_IMPORT);

  bytevector sect = {nullptr};
  wasm_writeuint(&sect, arrlen(module->imports) + PREDEFINED_IMPORT_COUNT);
  // Predefined imports
  write_string(&sect, "env2");
  write_string(&sect, "memory");
  write_byte(&sect, WASM_IMPORT_KIND_MEMORY); // memory
  write_byte(&sect, 0x00);                    // flags
  wasm_writeuint(&sect, 1);                   // initial
  // Import table
  write_string(&sect, "env2");
  write_string(&sect, "table");
  write_byte(&sect, WASM_IMPORT_KIND_TABLE);
  write_byte(&sect, 0x70);  // funcref
  write_byte(&sect, 0x00);  // only minimum limit present
  wasm_writeuint(&sect, 1); // initial

  for (int i = 0; i < arrlen(module->imports); ++i) {
    wasm_import *import = module->imports + i;
    write_string(&sect, import->module);
    write_string(&sect, import->name);
    write_byte(&sect, WASM_IMPORT_KIND_FUNC);
    wasm_writeuint(&sect, import->func.type);
    import->func.associated->my_index = i;
  }
  module->fn_index = arrlen(module->imports);
  wasm_writeuint(result, arrlen(sect.bytes));
  write_slice(result, sect.bytes, arrlen(sect.bytes));
  arrfree(sect.bytes);
}

typedef struct expression_ser_ctx {
  wasm_module *module;
  wasm_function *enclosing;
  wasm_expression *associated_block;

  // Pointer to previous block: used when computing label depths
  struct expression_ser_ctx *prev_block_ctx;
} expression_ser_ctx;

u32 walk_to_find_label(expression_ser_ctx *ctx, wasm_expression *break_to) {
  u32 i = 0;
  while (ctx && ctx->associated_block != break_to) {
    // printf("Seeking block %p, found %p\n", break_to, ctx->associated_block);
    ++i;
    ctx = ctx->prev_block_ctx;
  }
  DCHECK(ctx, "trying to branch to a non-enclosing block");
  return i;
}

void serialize_expression(expression_ser_ctx *ctx, bytevector *body, wasm_expression *expr) {
  switch (expr->kind) {
  case WASM_EXPR_KIND_DROP:
    write_byte(body, 0x1A);
    break;
  case WASM_EXPR_KIND_UNREACHABLE: {
    write_byte(body, 0x00);
    break;
  }
  case WASM_EXPR_KIND_RETURN:
    if (expr->return_expr) {
      serialize_expression(ctx, body, expr->return_expr);
    }
    write_byte(body, 0x0F);
    break;
  case WASM_EXPR_KIND_CALL: {
    for (int i = 0; i < expr->call.arg_count; ++i)
      serialize_expression(ctx, body, expr->call.args[i]);
    write_byte(body, 0x10);
    wasm_writeuint(body, expr->call.to_call->my_index);
    break;
  }
  case WASM_EXPR_KIND_CALL_INDIRECT: {
    for (int i = 0; i < expr->call_indirect.arg_count; ++i)
      serialize_expression(ctx, body, expr->call_indirect.args[i]);
    serialize_expression(ctx, body, expr->call_indirect.index);
    write_byte(body, expr->call_indirect.tail_call ? 0x13 : 0x11);
    wasm_writeuint(body, expr->call_indirect.function_type);
    wasm_writeuint(body, expr->call_indirect.table_index);
    break;
  }
  case WASM_EXPR_KIND_CONST: {
    write_byte(body, expr->literal.kind);
    switch (expr->literal.kind) { // int
    case WASM_LITERAL_KIND_I32:
      s32 value;
      memcpy(&value, expr->literal.bytes, 4);
      wasm_writeint(body, value);
      break;
    case WASM_LITERAL_KIND_F32: {
      float value;
      memcpy(&value, expr->literal.bytes, 4);
      write_f32(body, value);
      break;
    }
    case WASM_LITERAL_KIND_F64: {
      double value;
      memcpy(&value, expr->literal.bytes, 8);
      write_f64(body, value);
      break;
    }
    case WASM_LITERAL_KIND_I64: {
      s64 value;
      memcpy(&value, expr->literal.bytes, 8);
      wasm_writeint(body, value);
      break;
    }
    }
    break;
  }
  case WASM_EXPR_KIND_LOAD: {
    serialize_expression(ctx, body, expr->load.addr);
    write_byte(body, expr->load.op);
    write_byte(body, expr->load.align);
    wasm_writeuint(body, expr->load.offset);
    break;
  }
  case WASM_EXPR_KIND_STORE: {
    serialize_expression(ctx, body, expr->store.addr);
    serialize_expression(ctx, body, expr->store.value);
    write_byte(body, expr->store.op);
    write_byte(body, expr->store.align);
    wasm_writeuint(body, expr->store.offset);
    break;
  }
  case WASM_EXPR_KIND_SELECT:
    // (select (condition) (true_expr) (false_expr))
    serialize_expression(ctx, body, expr->select.true_expr);
    serialize_expression(ctx, body, expr->select.false_expr);
    serialize_expression(ctx, body, expr->select.condition);
    write_byte(body, 0x1B);
    break;
  case WASM_EXPR_KIND_GET_LOCAL: {
    write_byte(body, 0x20);
    wasm_writeuint(body, expr->local_get);
    break;
  }
  case WASM_EXPR_KIND_SET_LOCAL: {
    serialize_expression(ctx, body, expr->local_set.value);
    write_byte(body, 0x21);
    wasm_writeuint(body, expr->local_set.local_index);
    break;
  }
  case WASM_EXPR_KIND_UNARY_OP: {
    serialize_expression(ctx, body, expr->unary_op.arg);
    wasm_unary_op_kind op = expr->unary_op.op;
    if (op > 0xff)
      write_byte(body, 0xFC); // extended opcode
    write_byte(body, op & 0xff);
    break;
  }
  case WASM_EXPR_KIND_BINARY_OP: {
    serialize_expression(ctx, body, expr->binary_op.left);
    serialize_expression(ctx, body, expr->binary_op.right);
    wasm_binary_op_kind op = expr->binary_op.op;
    write_byte(body, op);
    break;
  }
  case WASM_EXPR_KIND_BLOCK: {
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
  case WASM_EXPR_KIND_IF: {
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
  case WASM_EXPR_KIND_BR: {
    if (expr->br.condition) {
      serialize_expression(ctx, body, expr->br.condition);
    }
    write_byte(body, expr->br.condition ? 0x0D : 0x0C);
    u32 label_index = walk_to_find_label(ctx, expr->br.break_to);
    wasm_writeuint(body, label_index);
    break;
  }
  case WASM_EXPR_KIND_BR_TABLE: {
    serialize_expression(ctx, body, expr->br_table.condition);

    write_byte(body, 0x0E);
    wasm_writeuint(body, expr->br_table.expr_count);
    for (int i = 0; i < expr->br_table.expr_count; ++i) {
      u32 label_index = walk_to_find_label(ctx, expr->br_table.exprs[i]);
      wasm_writeuint(body, label_index);
    }
    u32 label_index = walk_to_find_label(ctx, expr->br_table.dflt);
    wasm_writeuint(body, label_index);
    break;
  }
  default:
    UNREACHABLE();
  }
}

void write_compressed_locals(bytevector *body, const wasm_tuple_type *locals) {
  bytevector types = {nullptr};
  int count = 0;
  if (locals && locals->types_len) {
    int i = 1, j = 0;
    for (; i < locals->types_len; ++i) {
      if (locals->types[i] != locals->types[i - 1]) {
        // i - j locals, all with the same type
        wasm_writeuint(&types, i - j);
        write_byte(&types, locals->types[j]);
        j = i;
        ++count;
      }
    }
    wasm_writeuint(&types, i - j);
    write_byte(&types, locals->types[j]);
    ++count;
  }
  wasm_writeuint(body, count);
  write_slice(body, types.bytes, arrlen(types.bytes));
  arrfree(types.bytes);
}

void serialize_function_locals_and_code(bytevector *body, wasm_module *module, wasm_function *function) {
  // Locals first
  const wasm_tuple_type *locals = wasm_get_tuple_type(function->locals);
  write_compressed_locals(body, locals);
  // Now write the expression
  expression_ser_ctx ctx = {module};
  ctx.enclosing = function;
  serialize_expression(&ctx, body, function->body);
  // End function
  write_byte(body, 0x00);
  write_byte(body, 0x0B);
}

void serialize_codesection(bytevector *code_section, wasm_module *module) {
  write_byte(code_section, 0x0A);
  bytevector body = {nullptr};
  wasm_writeuint(&body, arrlen(module->functions));
  for (int i = 0; i < arrlen(module->functions); ++i) {
    bytevector boi = {nullptr};
    serialize_function_locals_and_code(&boi, module, module->functions[i]);
    wasm_writeuint(&body, arrlen(boi.bytes));
    write_slice(&body, boi.bytes, arrlen(boi.bytes));
    arrfree(boi.bytes);
  }
  wasm_writeuint(code_section, arrlen(body.bytes));
  write_slice(code_section, body.bytes, arrlen(body.bytes));
  arrfree(body.bytes);
}

void serialize_exportsection(bytevector *rest, wasm_module *module) {
  write_byte(rest, 0x07);
  bytevector sect = {nullptr};
  int export_count = 0;
  for (int i = 0; i < arrlen(module->functions); ++i) {
    if (module->functions[i]->exported)
      ++export_count;
  }
  wasm_writeuint(&sect, export_count);
  for (int i = 0; i < arrlen(module->functions); ++i) {
    wasm_function *fn = module->functions[i];
    if (fn->exported) {
      write_string(&sect, fn->name);
      write_byte(&sect, 0x00); // func
      wasm_writeuint(&sect, fn->my_index);
    }
  }
  wasm_writeuint(rest, arrlen(sect.bytes));
  write_slice(rest, sect.bytes, arrlen(sect.bytes));
  arrfree(sect.bytes);
}

bytevector wasm_module_serialize(wasm_module *module) {
  const char *WASM_MAGIC = "\0asm";
  bytevector result = {nullptr};
  write_byte(&result, WASM_MAGIC[0]);
  write_byte(&result, WASM_MAGIC[1]);
  write_byte(&result, WASM_MAGIC[2]);
  write_byte(&result, WASM_MAGIC[3]);
  write_u32(&result, 1); // version

  bytevector rest = {nullptr};
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
  write_slice(&result, rest.bytes, arrlen(rest.bytes));

  arrfree(rest.bytes);

  return result;
}

void wasm_module_free(wasm_module *module) {
  for (int i = 0; i < arrlen(module->arenas); ++i) {
    free(module->arenas[i]);
  }
  arrfree(module->arenas);
  arrfree(module->imports);
  arrfree(module->interned_result_types);
  arrfree(module->functions);
  arrfree(module->fn_types);
  free(module);
}

wasm_type wasm_make_tuple(wasm_module *module, wasm_value_type *components, int length) {
  // Search for an existing tuple type
  for (int i = 0; i < arrlen(module->interned_result_types); ++i) {
    wasm_tuple_type *tuple = module->interned_result_types[i];
    if (tuple->types_len != length)
      continue;
    if (memcmp(tuple->types, components, length * sizeof(wasm_value_type)) == 0)
      return (wasm_type){.val = (uintptr_t)tuple};
  }
  wasm_tuple_type *tuple = module_malloc(module, sizeof(wasm_tuple_type) + length * sizeof(wasm_value_type));
  tuple->types_len = length;
  memcpy(tuple->types, components, length * sizeof(wasm_value_type));
  arrput(module->interned_result_types, tuple);
  return (wasm_type){.val = (uintptr_t)tuple};
}

wasm_tuple_type *wasm_get_tuple_type(wasm_type type) {
  if (type.val < 255)
    return nullptr;
  return (void *)type.val;
}

wasm_value_type wasm_get_basic_type(wasm_type type) {
  DCHECK(type.val < 255);
  return type.val;
}

void wasm_export_function(wasm_module *module, wasm_function *fn) { fn->exported = true; }

const char *wasm_copy_string(wasm_module *module, const char *str) {
  char *result = module_malloc(module, strlen(str) + 1);
  strcpy(result, str);
  return result;
}

wasm_function *wasm_add_function(wasm_module *module, wasm_type params, wasm_type results, wasm_type locals,
                                 wasm_expression *body, const char *name) {
  wasm_function *fn = module_calloc(module, sizeof(wasm_function));
  fn->params = params;
  fn->results = results;
  fn->locals = locals;
  fn->body = body;
  fn->name = wasm_copy_string(module, name);
  arrput(module->functions, fn);
  return fn;
}

static wasm_value_type char_to_basic_type(char a) {
  switch (a) {
  case 'd':
    return WASM_TYPE_KIND_FLOAT64;
  case 'i':
    return WASM_TYPE_KIND_INT32;
  case 'j':
    return WASM_TYPE_KIND_INT64;
  case 'f':
    return WASM_TYPE_KIND_FLOAT32;
  case 'v':
    return WASM_TYPE_KIND_VOID;
  default:
    UNREACHABLE();
  }
}

wasm_type wasm_string_to_tuple(wasm_module *module, const char *str) {
  int len = strlen(str);
  wasm_value_type types[256];
  DCHECK(len < 256);
  int i = 0;
  for (; i < len; ++i) {
    types[i] = char_to_basic_type(str[i]);
  }
  return wasm_make_tuple(module, types, i);
}

wasm_function *wasm_import_runtime_function_impl(wasm_module *module, const char *c_name, const char *sig) {
  DCHECK(strlen(sig) > 1);

  // Search for existing import
  for (int i = 0; i < arrlen(module->imports); ++i) {
    if (strcmp(module->imports[i].name, c_name) == 0)
      return module->imports[i].func.associated;
  }

  wasm_function *fn = module_calloc(module, sizeof(wasm_function));
  fn->params = wasm_string_to_tuple(module, sig + 1);
  fn->results = from_basic_type(char_to_basic_type(sig[0]));
  fn->locals = wasm_void();
  const char *name_cpy = wasm_copy_string(module, c_name);
  fn->name = name_cpy;
  wasm_import *import = arraddnptr(module->imports, 1);
  import->module = "env";
  import->name = name_cpy;
  import->func = (wasm_func_import){
      .type = register_function_type(module, fn->params, fn->results),
      .associated = fn,
  };
  return fn;
}
wasm_expression *wasm_f32_const(wasm_module *module, float value) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x43;
  memcpy(result->literal.bytes, &value, 4);
  return result;
}

wasm_expression *wasm_f64_const(wasm_module *module, double value) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x44;
  memcpy(result->literal.bytes, &value, 8);
  return result;
}

wasm_expression *wasm_i32_const(wasm_module *module, s32 value) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x41;
  memcpy(result->literal.bytes, &value, 4);
  return result;
}

wasm_expression *wasm_i64_const(wasm_module *module, s64 value) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CONST);
  result->literal.kind = 0x42;
  memcpy(result->literal.bytes, &value, 8);
  return result;
}

wasm_expression *wasm_local_get(wasm_module *module, u32 index, wasm_type kind) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_GET_LOCAL);
  result->local_get = index;
  result->expr_type = kind;
  return result;
}

wasm_expression *wasm_local_set(wasm_module *module, u32 index, wasm_expression *value) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_SET_LOCAL);
  result->local_set = (wasm_local_set_expression){.local_index = index, .value = value};
  result->expr_type = wasm_void();
  return result;
}

wasm_expression *wasm_unreachable(wasm_module *module) { return module_expr(module, WASM_EXPR_KIND_UNREACHABLE); }

wasm_expression *wasm_binop(wasm_module *module, wasm_binary_op_kind op, wasm_expression *left,
                            wasm_expression *right) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_BINARY_OP);
  result->binary_op = (wasm_binary_expression){
      .op = op,
      .left = left,
      .right = right,
  };
  return result;
}

wasm_expression *wasm_select(wasm_module *module, wasm_expression *condition, wasm_expression *true_expr,
                             wasm_expression *false_expr) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_SELECT);
  result->select = (wasm_select_expression){
      .condition = condition,
      .true_expr = true_expr,
      .false_expr = false_expr,
  };
  return result;
}

wasm_expression *wasm_update_block(wasm_module *module, wasm_expression *existing_block, wasm_expression **exprs,
                                   int expr_count, wasm_type type, bool is_loop) {
  // Create a block expression
  existing_block->block =
      (wasm_block_expression){.list =
                                  (wasm_expression_list){
                                      .exprs = module_copy(module, exprs, sizeof(wasm_expression *) * expr_count),
                                      .expr_count = expr_count,
                                  },
                              .is_loop = is_loop};
  existing_block->expr_type = type;
  return existing_block;
}

wasm_expression *wasm_block(wasm_module *module, wasm_expression **exprs, int expr_count, wasm_type type,
                            bool is_loop) {
  wasm_expression *block = module_expr(module, WASM_EXPR_KIND_BLOCK);
  return wasm_update_block(module, block, exprs, expr_count, type, is_loop);
}

wasm_expression *wasm_br(wasm_module *module, wasm_expression *condition, wasm_expression *break_to) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_BR);
  result->br = (wasm_br_expression){
      .condition = condition,
      .break_to = break_to,
  };
  return result;
}

wasm_expression *wasm_call(wasm_module *module, wasm_function *fn, wasm_expression **args, int arg_count) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CALL);
  wasm_expression **cpy = module_copy(module, args, sizeof(wasm_expression *) * arg_count);
  result->call = (wasm_call_expression){
      .to_call = fn,
      .args = cpy,
      .arg_count = arg_count,
  };
  return result;
}

wasm_expression *wasm_call_indirect(wasm_module *module, int table_index, wasm_expression *index,
                                    wasm_expression **args, int arg_count, u32 functype) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_CALL_INDIRECT);
  wasm_expression **cpy = module_copy(module, args, sizeof(wasm_expression *) * arg_count);
  result->call_indirect = (wasm_call_indirect_expression){
      .table_index = table_index, .index = index, .args = cpy, .arg_count = arg_count, .function_type = functype};
  return result;
}

wasm_expression *wasm_load(wasm_module *module, wasm_load_op_kind op, wasm_expression *addr, int align, int offset) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_LOAD);
  result->load = (wasm_load_expression){
      .op = op,
      .addr = addr,
      .align = align,
      .offset = offset,
  };
  // TODO calculate expr_type
  return result;
}

wasm_expression *wasm_store(wasm_module *module, wasm_store_op_kind op, wasm_expression *addr, wasm_expression *value,
                            int align, int offset) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_STORE);
  result->store = (wasm_store_expression){
      .op = op,
      .addr = addr,
      .value = value,
      .align = align,
      .offset = offset,
  };
  return result;
}

wasm_expression *wasm_if_else(wasm_module *module, wasm_expression *cond, wasm_expression *true_expr,
                              wasm_expression *false_expr, wasm_type type) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_IF);
  result->if_ = (wasm_if_expression){
      .condition = cond,
      .true_expr = true_expr,
      .false_expr = false_expr,
  };
  result->expr_type = type;
  return result;
}

wasm_expression *wasm_return(wasm_module *module, wasm_expression *expr) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_RETURN);
  result->return_expr = expr;
  return result;
}

EMSCRIPTEN_KEEPALIVE
void wasm_push_export(wasm_instantiation_result *result, const char *name, void *exported_func) {
  wasm_instantiation_export *exp = arraddnptr(result->exports, 1);
  exp->name = make_heap_str_from((slice){.chars = (char *)name, .len = strlen(name)});
  exp->export_ = exported_func;
}

void free_wasm_instantiation_result(wasm_instantiation_result *result) {
  // Delete function pointers from the table
  for (int i = 0; i < arrlen(result->exports); ++i) {
    free_heap_str(result->exports[i].name);
#ifdef EMSCRIPTEN
    EM_ASM_({ removeFunction(wasmTable.get($0)); }, (intptr_t)result->exports[i].export_);
#endif
  }
  free(result);
}

wasm_instantiation_result *wasm_instantiate_module(wasm_module *module, const char *debug_name) {
  wasm_instantiation_result *result = calloc(1, sizeof(wasm_instantiation_result));
#ifndef EMSCRIPTEN
  result->status = WASM_INSTANTIATION_FAIL;
  return result;
#else // EMSCRIPTEN
  // Serialize the module
  bytevector serialized = wasm_module_serialize(module);
  int ptr = EM_ASM_INT(
      {
        var slice = HEAPU8.subarray($0, $1);
        try {
          var module = new WebAssembly.Module(slice);
          var instance =
              new WebAssembly.Instance(module, {env : wasmExports, env2 : {memory : wasmMemory, table : wasmTable}});
          let count = 0;
          for (var exp in instance.exports)
            count++;
          require("fs").writeFileSync("debug/test" + UTF8ToString($2) + ".wasm", slice);
          return addFunction(instance.exports.run, 'iiii');
        } catch (e) {
          // Exit Node.js
          console.log(e);
          require("fs").writeFileSync("debug/broken" + UTF8ToString($2) + ".wasm", slice);
          process.exit(1);
          return 0;
        }
      },
      (intptr_t)serialized.bytes, (intptr_t)(serialized.bytes + arrlen(serialized.bytes)), (intptr_t)debug_name);
  if (ptr) {
    result->status = WASM_INSTANTIATION_SUCCESS;
    result->run = (void *)ptr;
  } else {
    result->status = WASM_INSTANTIATION_FAIL;
  }
#endif

  return result;
}

wasm_expression *wasm_unop(wasm_module *module, wasm_unary_op_kind op, wasm_expression *expr) {
  wasm_expression *result = module_expr(module, WASM_EXPR_KIND_UNARY_OP);
  result->unary_op = (wasm_unary_expression){
      .op = op,
      .arg = expr,
  };
  return result;
}

wasm_type jvm_type_to_wasm(type_kind kind) {
  switch (kind) {
  case TYPE_KIND_BOOLEAN:
  case TYPE_KIND_CHAR:
  case TYPE_KIND_BYTE:
  case TYPE_KIND_SHORT:
  case TYPE_KIND_INT:
  case TYPE_KIND_REFERENCE:
    return wasm_int32();
  case TYPE_KIND_FLOAT:
    return wasm_float32();
  case TYPE_KIND_DOUBLE:
    return wasm_float64();
  case TYPE_KIND_LONG:
    return wasm_int64();
  case TYPE_KIND_VOID:
    [[fallthrough]];
  default:
    UNREACHABLE();
  }
}