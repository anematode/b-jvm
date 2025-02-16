#ifndef REFLECTION_H
#define REFLECTION_H

#include <bjvm.h>
#include <util.h>

void reflect_initialize_field(vm_thread *thread, classdesc *classdesc, cp_field *field);
void reflect_initialize_constructor(vm_thread *thread, classdesc *classdesc, cp_method *method);
void reflect_initialize_method(vm_thread *thread, classdesc *classdesc, cp_method *method);
object reflect_get_method_parameters(vm_thread *thread, cp_method *method);

#endif