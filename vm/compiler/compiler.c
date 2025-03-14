
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