#ifndef GC_H
#define GC_H

#include <bjvm.h>

int in_heap(const vm *vm, object field);

/**
 * Garbage collects the heap.
 * Invoking this method will set vm->gc_pause_requested to true (if not already),
 * then wait for all threads to do call this function.
 * All invocaations to this function will block until the GC has been completed by one particuliar thread.
 */
void scheduled_gc_pause(vm *vm);

/**
 * Pauses all execution to patch vm instructions.
 * If this thread is the leader of the GC, it will only patch instructions, without executing a GC.
 * This is useful early into VM initialization, where we can't GC without fully initialized classdescs.
 */
void scheduled_gc_pause_patch_only(vm *vm);

/**
 * Pauses all execution to patch vm instructions.
 * If this thread is the leader of the GC, it will only patch instructions, without executing a GC.
 * Forces this instruction to be patched before a GC, even if the GC is requested first.
 */
void scheduled_gc_pause_require_patch(vm *vm, bytecode_patch_request *req);

size_t size_of_object(obj_header *obj);

#endif