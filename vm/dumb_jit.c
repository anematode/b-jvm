// The baseline JIT for WebAssembly, also known as the "dumb JIT".
//
// There is a one-to-one mapping between the operand stack/locals and WASM locals. Construction of the CFG
// structure is done using the "Stackifier" algorithm. Inlining is not yet implemented.

#include "dumb_jit.h"

#include <analysis.h>
#include <wasm/wasm_utils.h>

typedef wasm_expression* expression;

typedef struct {
  wasm_module *module;
  wasm_value_type *params;
  wasm_value_type *locals;
  wasm_type returns;
  int next_local;
} function_builder;

static wasm_type tuple_from_array(wasm_module *module, wasm_value_type *array) {
  return wasm_make_tuple(module, array, arrlen(array));
}

void init_function_builder(wasm_module *module, function_builder *builder, const wasm_value_type *params, wasm_type returns) {
  builder->module = module;
  builder->params = nullptr;
  builder->returns = returns;
  for (int i = 0; i < arrlen(params); ++i) {
    arrput(builder->params, params[i]);
  }
  builder->locals = nullptr;
  builder->next_local = arrlen(builder->params);
}

[[maybe_unused]] static int fb_new_local(function_builder *builder, wasm_value_type local) {
  int local_i = builder->next_local++;
  arrput(builder->locals, local);
  DCHECK(arrlen(builder->locals) + arrlen(builder->params) == local_i);
  return local_i;
}

wasm_function *finalize_function_builder(function_builder *builder, const char *name, expression body) {
  wasm_type params = tuple_from_array(builder->module, builder->params);
  wasm_type results = builder->returns;
  wasm_type locals = tuple_from_array(builder->module, builder->locals);

  wasm_function *fn = wasm_add_function(builder->module, params, results, locals, body, name);

  arrfree(builder->params);
  arrfree(builder->locals);

  return fn;
}

typedef struct {
  int start, end;
} loop_range_t;

typedef struct {
  // these future blocks requested a wasm block to begin here, so that
  // forward edges can be implemented with a br or br_if
  int *requested;
} bb_creations_t;

typedef struct {
  // new order -> block index
  int *topo_to_block;
  // block index -> new order
  int *block_to_topo;
  // Mapping from topological # to the blocks (also in topo #) which requested that a block begin here.
  bb_creations_t *creations;
  // Mapping from topological block index to a loop bjvm_wasm_expression such
  // that backward edges should continue to this loop block.
  expression *loop_headers;
  // Mapping from topological block index to a block bjvm_wasm_expression such
  // that forward edges should break out of this block.
  expression *block_ends;
  // Now implementation stuffs
  int current_block_i;
  int topo_i;
  int loop_depth;
  // For each block in topological indexing, the number of loops that contain
  // it.
  int *loop_depths;
  int *visited, *incoming_count;
  int blockc;
} method_jit_ctx;

void free_topo_ctx(method_jit_ctx ctx) {
  free(ctx.topo_to_block);
  free(ctx.block_to_topo);
  free(ctx.visited);
  free(ctx.incoming_count);
  free(ctx.loop_depths);
  free(ctx.block_ends);
  free(ctx.loop_headers);
  for (int i = 0; i < ctx.blockc; ++i) {
    free(ctx.creations[i].requested);
  }
  free(ctx.creations);
}

void find_block_insertion_points(code_analysis *analy, method_jit_ctx *ctx) {
  // For each bb, if there is a bb preceding it (in the topological sort)
  // which branches to that bb, which is NOT its immediate predecessor in the
  // sort, we need to introduce a wasm (block) to handle the branch. We place
  // it at the point in the topo sort where the depth becomes equal to the
  // depth of the topological predecessor of bb.
  for (int i = 0; i < analy->block_count; ++i) {
    basic_block *b = analy->blocks + i;
    for (int j = 0; j < arrlen(b->prev); ++j) {
      int prev = b->prev[j];
      if (ctx->block_to_topo[prev] >= ctx->block_to_topo[i] - 1)
        continue;
      bb_creations_t *creations = ctx->creations + ctx->block_to_topo[b->idom];
      arrput(creations->requested, (ctx->block_to_topo[i] << 1) + 1);
      break;
    }
  }
}

void topo_walk_idom(code_analysis *analy, method_jit_ctx *ctx) {
  int current = ctx->current_block_i;
  int start = ctx->topo_i;
  ctx->visited[current] = 1;
  ctx->block_to_topo[current] = ctx->topo_i;
  ctx->topo_to_block[ctx->topo_i++] = current;
  ctx->loop_depths[current] = ctx->loop_depth;
  basic_block *b = analy->blocks + current;
  bool is_loop_header = b->is_loop_header;
  // Sort successors by reverse post-order in the original DFS
  dominated_list_t idom = b->idominates;
  int *sorted = malloc(arrlen(idom.list) * sizeof(int));
  memcpy(sorted, idom.list, arrlen(idom.list) * sizeof(int));
  for (int i = 1; i < arrlen(idom.list); ++i) {
    int j = i;
    while (j > 0 && analy->blocks[sorted[j - 1]].dfs_post <
                        analy->blocks[sorted[j]].dfs_post) {
      int tmp = sorted[j];
      sorted[j] = sorted[j - 1];
      sorted[j - 1] = tmp;
      j--;
    }
  }
  // Recurse on the sorted successors
  ctx->loop_depth += is_loop_header;
  for (int i = 0; i < arrlen(idom.list); ++i) {
    int next = sorted[i];
    ctx->current_block_i = next;
    topo_walk_idom(analy, ctx);
  }
  ctx->loop_depth -= is_loop_header;
  bb_creations_t *creations = ctx->creations + start;
  if (is_loop_header)
    arrput(creations->requested, ctx->topo_i << 1);
  free(sorted);
}

method_jit_ctx make_topo_sort_ctx(code_analysis *analy) {
  method_jit_ctx ctx;
  ctx.topo_to_block = calloc(analy->block_count, sizeof(int));
  ctx.block_to_topo = calloc(analy->block_count, sizeof(int));
  ctx.visited = calloc(analy->block_count, sizeof(int));
  ctx.incoming_count = calloc(analy->block_count, sizeof(int));
  ctx.loop_depths = calloc(analy->block_count, sizeof(int));
  ctx.creations = calloc(analy->block_count, sizeof(bb_creations_t));
  ctx.block_ends = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx.loop_headers = calloc(analy->block_count, sizeof(wasm_expression *));
  ctx.blockc = analy->block_count;
  ctx.current_block_i = ctx.topo_i = ctx.loop_depth = 0;
  for (int i = 0; i < analy->block_count; ++i) {
    basic_block *b = analy->blocks + i;
    for (int j = 0; j < arrlen(b->next); ++j)
      ctx.incoming_count[b->next[j]] += !b->is_backedge[j];
  }
  // Perform a post-order traversal of the immediate dominator tree. Whenever
  // reaching a loop header, output the loop header immediately, then everything
  // in the subtree as one contiguous block. We output them in reverse postorder
  // relative to a DFS on the original CFG, to guarantee that the final
  // topological sort respects the forward edges in the original graph.
  topo_walk_idom(analy, &ctx);
  assert(ctx.topo_i == analy->block_count);
  find_block_insertion_points(analy, &ctx);
  return ctx;
}
typedef struct {
  expression ref;
  // Wasm block started here (in topological sort)
  int started_at;
  // Once we get to this basic block, close the wasm block and push the
  // expression to the stack
  int close_at;
  // Whether to emit a (loop) for this block
  bool is_loop;
} inchoate_expression;

static int cmp_ints_reverse(const void *a, const void *b) {
  return *(int *)b - *(int *)a;
}

void free_dumb_jit_result(dumb_jit_result *result){
  free(result->pc_to_oops.count);
  free(result);
}

dumb_jit_result *dumb_jit_compile(cp_method *method, dumb_jit_options options) {
  attribute_code *code = method->code;
  code_analysis *analy = method->code_analysis;

  DCHECK(code, "Method has no code");
  DCHECK(analy, "Method has no code analysis");

  compute_dominator_tree(analy);
  int fail = attempt_reduce_cfg(analy);
  if (fail) {
    return nullptr;
  }

  pc_to_oop_count pc_to_oops = {};
  pc_to_oops.count = calloc(code->insn_count, sizeof(u16));
  pc_to_oops.max_pc = code->insn_count;

  wasm_module *module = wasm_module_create();
  method_jit_ctx topo = make_topo_sort_ctx(analy);
  inchoate_expression *expr_stack = nullptr;

  // Push an initial boi
  *arraddnptr(expr_stack, 1) = (inchoate_expression){
      wasm_block(module, nullptr, 0, wasm_void(), false), 0,
      analy->block_count, false};

  // Iterate through each block
  for (topo.topo_i = 0; topo.topo_i <= analy->block_count; ++topo.topo_i) {
    // First close off any blocks as appropriate
    int stack_count = arrlen(expr_stack);
    for (int i = stack_count - 1; i >= 0; --i) {
      inchoate_expression *expr = expr_stack + i;
      if (expr->close_at == topo.topo_i) {
        // Take expressions i + 1 through stack_count - 1 inclusive and
        // make them the contents of the block
        expression *exprs = alloca((stack_count - i - 1) * sizeof(expression));
        for (int j = i + 1; j < stack_count; ++j)
          exprs[j - i - 1] = expr_stack[j].ref;
        wasm_update_block(module, expr->ref, exprs, stack_count - i - 1,
                               wasm_void(), expr->is_loop);
        // Update the handles for blocks and loops to break to
        if (topo.topo_i < analy->block_count)
          topo.block_ends[topo.topo_i] = nullptr;
        if (expr->is_loop)
          topo.loop_headers[expr->started_at] = nullptr;
        expr->close_at = -1;
        stack_count = i + 1;
      }
    }
    // Done pushing expressions
    if (topo.topo_i == analy->block_count)
      break;
    // Then create (block)s and (loop)s as appropriate. First create blocks
    // in reverse order of the topological order of their targets. So, if
    // at the current block we are to initiate blocks ending at block with
    // topo indices 9, 12, and 13, push the block for 13 first.
    //
    // Because blocks have a low bit of 1 in the encoding, they are larger in index and placed first.
    bb_creations_t *creations = topo.creations + topo.topo_i;
    qsort(creations->requested, arrlen(creations->requested), sizeof(int),
          cmp_ints_reverse);
    for (int i = 0; i < arrlen(creations->requested); ++i) {
      int block_i = creations->requested[i];
      int is_loop = !(block_i & 1);
      block_i >>= 1;
      expression block = wasm_block(module, nullptr, 0, wasm_void(), is_loop);
      *arraddnptr(expr_stack, 1) =
          (inchoate_expression){block, topo.topo_i, block_i, is_loop};
      if (is_loop)
        topo.loop_headers[topo.topo_i] = block;
      else
        topo.block_ends[block_i] = block;
    }
    int block_i = topo.topo_to_block[topo.topo_i];
    [[maybe_unused]] basic_block *bb = analy->blocks + block_i;
    expression expr = nullptr; //compile_bb(&ctx, bb, &topo);
    if (!expr) {
      goto error_2;
    }
    *arraddnptr(expr_stack, 1) =
        (inchoate_expression){expr, topo.topo_i, -1, false};
  }
  [[maybe_unused]] expression body = expr_stack[0].ref;

error_2:
  free_topo_ctx(topo);
  return nullptr;
}