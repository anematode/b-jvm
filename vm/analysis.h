//
// Created by alec on 12/18/24.
//

#ifndef ANALYSIS_H
#define ANALYSIS_H

#include "classfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  int *list;
} dominated_list_t;

typedef struct bytecode_insn bytecode_insn;

typedef struct basic_block {
  int my_index;

  const bytecode_insn *start; // non-owned
  int start_index;
  int insn_count;

  // May contain duplicates in the presence of a switch or cursed if*.
  // The order of branches is guaranteed for convenience. For if instructions,
  // the TAKEN branch is FIRST, and the FALLTHROUGH branch is SECOND. For
  // lookupswitch and tableswitch, the default branch is last.
  int *next;
  u8 *is_backedge;

  // May contain duplicates in the presence of a switch or cursed if*
  int *prev;

  // Pre- and postorder in a DFS on the original CFG
  u32 dfs_pre, dfs_post;
  // Immediate dominator of this block
  u32 idom;
  // Blocks that this block immediately dominates
  dominated_list_t idominates;
  // Pre- and postorder in the immediate dominator tree
  u32 idom_pre, idom_post;
  // Whether this block is the target of a backedge
  bool is_loop_header;
  // Whether we can get here in the method without an exception being thrown
  bool nothrow_accessible;
} basic_block;

typedef enum : u16 {
  // The nth parameter to the function (0 = implicit 'this')
  VARIABLE_SRC_KIND_PARAMETER,
  // The nth local (not parameter) of the function
  VARIABLE_SRC_KIND_LOCAL,
  // The nth instruction produced this variable in the course of execution (seeing through
  // instructions like dup etc.)
  VARIABLE_SRC_KIND_INSN,
  // Comes from multiple possible instructions
  VARIABLE_SRC_KIND_UNK
} variable_source_kind;

typedef struct {
  u16 index;
  variable_source_kind kind;
} stack_variable_source;

// Summary of the state of the stack and locals at a given PC. Used by the GC.
typedef struct {
  u16 stack;
  u16 locals;
  type_kind entries[];  // first 'stack' entries, then 'locals' entries
} stack_summary;

// Result of the analysis of a code segment. During analysis, stack operations
// on longs/doubles are simplified as if they only took up one stack slot (e.g.,
// pop2 on a double becomes a pop, while pop2 on two ints stays as a pop2).
// Also, we progressively resolve the state of the stack and local variable
// table at each program counter, and store a bitset of which stack/local
// variables are references, so that the GC can follow them.
typedef struct code_analysis {
  // For each instruction, the simplified state of the stack and local variables at that instruction
  stack_summary **stack_states;

  // For each instruction, the stack depth at that instruction
  u16 *insn_index_to_sd;
  int insn_count;

  // For each instruction that might participate in extended NPE message resolution,
  // the sources of its first two operands.
  struct {
    stack_variable_source a, b;
  } *sources;

  // block 0 = entry point
  basic_block *blocks;
  int block_count;

  bool dominator_tree_computed;
} code_analysis;

/**
 * Analyze the method's code segment if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis.
 * <br/>
 * Returns -1 if an error occurred, and writes the error message into error.
 */
int analyze_method_code(cp_method *method, heap_string *error);
void free_code_analysis(code_analysis *analy);
int scan_basic_blocks(const attribute_code *code, code_analysis *analy);
void compute_dominator_tree(code_analysis *analy);
void dump_cfg_to_graphviz(FILE *out, const code_analysis *analysis);
// Returns true iff dominator dominates dominated. dom dom dom.
bool query_dominance(const basic_block *dominator, const basic_block *dominated);
// Try to reduce the CFG and mark the edges/blocks accordingly.
int attempt_reduce_cfg(code_analysis *analy);
int get_extended_npe_message(cp_method *method, u16 pc, heap_string *result);

#ifdef __cplusplus
}
#endif

#endif // ANALYSIS_H
