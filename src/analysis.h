//
// Created by alec on 12/18/24.
//

#ifndef BJVM_ANALYSIS_H
#define BJVM_ANALYSIS_H

#include "classfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct bjvm_basic_block {
  int my_index;

  const bjvm_bytecode_insn *start; // non-owned
  int start_index;
  int insn_count;

  // May contain duplicates in the presence of a switch or cursed if*
  uint32_t *next;
  int next_count;
  int next_cap;

  // May contain duplicates in the presence of a switch or cursed if*
  uint32_t *prev;
  int prev_count;
  int prev_cap;

  // Immediate dominator of this block
  uint32_t idom;
  // Pre- and postorder in the immediate dominator tree
  uint32_t idom_pre, idom_post;
} bjvm_basic_block;

// Result of the analysis of a code segment. During analysis, stack operations
// on longs/doubles are simplified as if they only took up one stack slot (e.g.,
// pop2 on a double becomes a pop, while pop2 on two ints stays as a pop2).
// Also, we progressively resolve the state of the stack and local variable
// table at each program counter, and store a bitset of which stack/local
// variables are references, so that the GC can follow them.
typedef struct {
  union {
    struct {
      // wasm jit depends on the order here
      bjvm_compressed_bitset *insn_index_to_references;
      bjvm_compressed_bitset *insn_index_to_ints;
      bjvm_compressed_bitset *insn_index_to_floats;
      bjvm_compressed_bitset *insn_index_to_doubles;
      bjvm_compressed_bitset *insn_index_to_longs;
    };

    bjvm_compressed_bitset *insn_index_to_kinds[5];
  };

  uint16_t *insn_index_to_stack_depth;
  int insn_count;

  // block 0 = entry point
  bjvm_basic_block *blocks;
  int block_count;
} bjvm_code_analysis;

typedef bjvm_type_kind bjvm_analy_stack_entry;

// State of the stack (or local variable table) during analysis, indexed by
// formal JVM semantics (i.e., long/double take up two slots, and the second
// slot is unusable).
typedef struct {
  bjvm_analy_stack_entry *entries;
  int entries_count;
  int entries_cap;

  bool from_jump_target;
  bool is_exc_handler;

  int exc_handler_start;
} bjvm_analy_stack_state;

/**
 * Analyze the method's code segment if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis.
 * <br/>
 * Returns -1 if an error occurred, and writes the error message into error.
 */
int bjvm_analyze_method_code_segment(bjvm_cp_method *method,
                                     heap_string *error);

void free_code_analysis(bjvm_code_analysis *analy);
int bjvm_scan_basic_blocks(const bjvm_attribute_code *code,
                           bjvm_code_analysis *analy);
void bjvm_compute_dominator_tree(bjvm_code_analysis *analy);
void bjvm_dump_cfg_to_graphviz(FILE *out, const bjvm_code_analysis *analysis);

#ifdef __cplusplus
}
#endif

#endif // BJVM_ANALYSIS_H
