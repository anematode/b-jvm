#ifndef LINKAGE_H
#define LINKAGE_H

#include <bjvm.h>

int link_class(vm_thread *thread, classdesc *cd);
int link_class_impl(vm_thread *thread, classdesc *cd); // internal, assume already locked
void setup_super_hierarchy(classdesc *cd);

#endif