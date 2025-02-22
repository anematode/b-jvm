#ifndef GC_H
#define GC_H

#include <bjvm.h>

int in_heap(vm *vm, object field);
void major_gc(vm *vm);
size_t size_of_object(obj_header *obj);

#endif