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
size_t size_of_object(obj_header *obj);

#endif