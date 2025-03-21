
#include "compiler.h"

#define GET_INSTRUCTION(enum_, type, x) \
  ({ \
    __typeof(x) _x = x; \
    DCHECK(_x->kind == enum_); \
    _Generic((x), ctl_inst*: (type *)x, const ctl_inst *: (const type *)x); \
  })

#define get_argument_inst(x) GET_INSTRUCTION(CTL_INST_GET_ARGUMENT, ctl_inst_get_argument, x)

// Aborts if the instruction is invalid
void validate_instruction(const ctl_function *function, const ctl_inst *inst) {
  switch (inst->kind) {
  case CTL_INST_CONST:
    return;
  case CTL_INST_GET_ARGUMENT: {
    CHECK(get_argument_inst(inst)->arg_i < arrlen(function->arguments));
    return;
  }
  case CTL_INST_BRANCH: {

  }
break;case CTL_INST_CALL:
break;case CTL_INST_DEOPT:
break;case CTL_INST_UPSILON:
break;case CTL_INST_PHI:
break;case CTL_INST_RETURN:
break;case CTL_INST_MEM:
break;case CTL_INST_OP:
break;case CTL_INST_VAR:
break;}
}

heap_string ctl_dump_basic_block(ctl_basic_block *bb) {

}

void ctl_dump_function(FILE* out, const ctl_function *func) {
  fprintf(out, "digraph cfg {\n");
  for (int bb_i = 0; bb_i < arrlen(func->blocks); ++bb_i) {
    ctl_basic_block *bb = func->blocks[bb_i];
    heap_string bb_dump = ctl_dump_basic_block(bb);
    fprintf(out, "%d [label=\"%.*s\"];\n", bb_i, fmt_slice(bb_dump));
    free_heap_str(bb_dump);

    for (int succ_i = 0; succ_i < arrlen(bb->succ); ++succ_i) {
      fprintf(out, "%d -> %d;\n", bb_i, bb->succ[succ_i]);
    }
  }
  fprintf(out, "}\n");
}