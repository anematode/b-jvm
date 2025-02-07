#ifndef INSTRUMENTATION_H
#define INSTRUMENTATION_H

#include <config.h>
#include <bjvm.h>

#if DTRACE_ENABLED
#include <probes.h>
#endif

static inline void InstrumentMethodEntry(bjvm_thread *thread, bjvm_stack_frame *frame) {
#if DTRACE_ENABLED
  BJVM_METHOD_ENTRY(
    thread->tid,
    frame->method->my_class->name.chars, frame->method->my_class->name.len,
    frame->method->name.chars, frame->method->name.len,
    frame->method->unparsed_descriptor.chars, frame->method->unparsed_descriptor.len
  );
#endif
}

static inline void InstrumentMethodReturn(bjvm_thread *thread, bjvm_stack_frame *frame) {
#if DTRACE_ENABLED
  BJVM_METHOD_RETURN(
    thread->tid,
    frame->method->my_class->name.chars, frame->method->my_class->name.len,
    frame->method->name.chars, frame->method->name.len,
    frame->method->unparsed_descriptor.chars, frame->method->unparsed_descriptor.len
  );
#endif
}

static inline void InstrumentVMInitBegin() {
#if DTRACE_ENABLED
  BJVM_VM_INIT_BEGIN();
#endif
}

static inline void InstrumentVMInitEnd() {
#if DTRACE_ENABLED
  BJVM_VM_INIT_END();
#endif
}

static inline void InstrumentVMShutdown() {
#if DTRACE_ENABLED
  BJVM_VM_INIT_END();
#endif
}

static inline void InstrumentGCBegin(bool is_heap_full) {
#if DTRACE_ENABLED
  BJVM_GC_BEGIN(is_heap_full);
#endif
}

static inline void InstrumentGCEnd() {
#if DTRACE_ENABLED
  BJVM_GC_END();
#endif
}

static inline void InstrumentObjectAlloc(bjvm_thread *thread, bjvm_classdesc *cd, size_t size) {
#if DTRACE_ENABLED
  BJVM_OBJECT_ALLOC(thread->tid, cd->name.chars, cd->name.len, size);
#endif
}


#endif