// The Cattle compiler

#ifndef COMPILER_H
#define COMPILER_H

#include "analysis.h"
#include "bjvm.h"

typedef struct {

} ctl_type;

typedef enum {
  CTL_INST_CONST,
  CTL_INST_GET_ARGUMENT,
  CTL_INST_BRANCH,  // branch of some kind
  CTL_INST_CALL,    // call a function
  CTL_INST_DEOPT,   // deoptimize with the given arguments (construct an interpreter frame and call the de-opt handler)
  CTL_INST_UPSILON, // https://gist.github.com/pizlonator/cf1e72b8600b1437dda8153ea3fdb963
  CTL_INST_PHI,
  CTL_INST_RETURN,
  CTL_INST_MEM,  // load or store
  CTL_INST_OP,   // unary op or binary op
  CTL_INST_VAR,  // set or get a local variable
} ctl_inst_kind;

typedef enum {
  CTL_OPC_CONST_FLOAT,
} ctl_inst_opcode;

typedef struct ctl_inst ctl_inst;

typedef struct {
  int count;
  ctl_inst *insts;
} ctl_arguments;

// Base instruction class
typedef struct ctl_inst {
  ctl_inst_kind kind;
  ctl_inst_opcode opc;
  u32 name;
  ctl_type *type;
  ctl_arguments arguments;
} ctl_inst;

typedef struct {
  ctl_inst base;
  bytecode_insn *java_inst;  // corresponding Java instruction
  int stack_space;           // necessary stack space for the start of the next frame
} ctl_inst_call;

typedef struct {
  ctl_inst base;
  stack_value value;  // union member is determined by base.type
} ctl_inst_const;

typedef struct {
  ctl_inst base;
  s32 arg_i;
} ctl_inst_get_argument;

// Construct (nested) interpreter frames
typedef struct {
  ctl_inst base;
  int frame_count;  // number of frames to generate
  s32 *pc;  // program counter for each frame
  stack_summary *stacks;  // stack summaries for each frame (so that we know the mapping from arguments to stack values)
} ctl_inst_deopt;

typedef struct {
  ctl_inst *insts;  // stbds array
  int *succ;        // likewise
} ctl_basic_block;

typedef struct {
  cp_method *java_method;
  ctl_basic_block **blocks;  // stbds array
  ctl_type *arguments;  // stbds array
  arena arena;
} ctl_function;

void ctl_dump_function(FILE* out, const ctl_function *func);

#endif //COMPILER_H
