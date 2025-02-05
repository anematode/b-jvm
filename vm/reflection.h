#ifndef REFLECTION_H
#define REFLECTION_H

#include <util.h>
#include <bjvm.h>

slice bjvm_unparse_field_descriptor(slice str, const bjvm_field_descriptor *desc);
void bjvm_reflect_initialize_field(bjvm_thread *thread, bjvm_classdesc *classdesc, bjvm_cp_field *field);
void bjvm_reflect_initialize_constructor(bjvm_thread *thread, bjvm_classdesc *classdesc, bjvm_cp_method *method);
void bjvm_reflect_initialize_method(bjvm_thread *thread, bjvm_classdesc *classdesc, bjvm_cp_method *method);
object bjvm_reflect_get_method_parameters(bjvm_thread *thread, bjvm_cp_method *method);

#endif