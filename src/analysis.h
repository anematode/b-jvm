//
// Created by alec on 12/18/24.
//

#ifndef BJVM_ANALYSIS_H
#define BJVM_ANALYSIS_H

#include "classfile.h"

// Result of the analysis of a code segment. During analysis, stack operations
// on longs/doubles are simplified as if they only took up one stack slot (e.g.,
// pop2 on a double becomes a pop, while pop2 on two ints stays as a pop2).
// Also, we progressively resolve the state of the stack and local variable
// table at each program counter, and store a bitset of which stack/local
// variables are references, so that the GC can follow them.
typedef struct {
  bjvm_compressed_bitset *insn_index_to_references;
  uint16_t *insn_index_to_stack_depth;
  int insn_count;
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

int bjvm_locals_on_method_entry(const bjvm_cp_method *descriptor,
                                    bjvm_analy_stack_state *locals);

char *bjvm_analyze_method_code_segment(bjvm_cp_method *method);

#endif // BJVM_ANALYSIS_H
