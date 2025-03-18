#ifndef REFLECTION_H
#define REFLECTION_H

#include <bjvm.h>
#include <util.h>

object reflect_get_method_parameters(vm_thread *thread, cp_method *method);

#endif