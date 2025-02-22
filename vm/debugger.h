//
// Created by Cowpox on 2/13/25.
//

#ifndef DEBUGGER_H
#define DEBUGGER_H

#include "bjvm.h"

#define DEBUGGER_MAX_BREAKPOINTS 100

typedef struct {
  cp_method *method;
  int pc;
  insn_code_kind replaced_kind;
} debugger_bkpt;

typedef struct standard_debugger standard_debugger;

// If true, the thread will be paused and not resumed or re-scheduled until debugger_resume is called.
// If false, the instruction will be executed as normal.
typedef bool (*bkpt_callback)(standard_debugger *debugger, vm_thread *thread, stack_frame *frame);

typedef struct standard_debugger {
  vm *vm;
  debugger_bkpt *bkpts;
  bkpt_callback should_pause;
} standard_debugger;

// Get a list of potential breakpoints corresponding to the given file name and line number.
debugger_bkpt *list_breakpoints(vm *vm, slice filename, int line);

int create_and_attach_debugger(vm *vm, standard_debugger **debugger);
int debugger_add_breakpoint(standard_debugger *debugger, cp_method *method, int pc);
int debugger_remove_breakpoint(standard_debugger *debugger, cp_method *method, int pc);
// If the given thread is currently paused under the debugger, resume it. nop if the thread is not paused.
void debugger_resume(standard_debugger *debugger, vm_thread *thread);
void free_debugger(standard_debugger *debugger);

#endif // DEBUGGER_H
