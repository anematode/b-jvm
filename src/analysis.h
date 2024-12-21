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
} bjvm_analy_stack_state;

char *bjvm_locals_on_function_entry(const bjvm_utf8 *descriptor,
                                    bjvm_analy_stack_state *locals);

/**
 * Analyze the method's code segment if it exists, rewriting instructions in
 * place to make longs/doubles one stack value wide, writing the analysis into
 * analysis.
 * <br/>
 * Returns -1 if an error occurred, and writes the error message into error.
 */
int bjvm_analyze_method_code_segment(bjvm_cp_method *method,
                                     heap_string *error);

#endif // BJVM_ANALYSIS_H
