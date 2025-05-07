#ifndef LINKAGE_H
#define LINKAGE_H

#include <bjvm.h>

int link_class(vm_thread *thread, classdesc *cd);
int link_class_impl(vm_thread *thread, classdesc *cd); // internal, assume already locked
int link_class_impl_super_impl(vm_thread *thread, classdesc *cd); // assumes the class and the super class are locked
void setup_super_hierarchy(classdesc *cd);

#endif