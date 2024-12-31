//
// Created by Cowpox on 12/30/24.
//

#include "vtable.h"
#include "bjvm.h"
#include "classfile.h"

static bool same_runtime_package(bjvm_classdesc *a, bjvm_classdesc *b) {
  // Find last slash in both names, and compare the strings up to that point.
  const char* as = strrchr(a->name.chars, '/');
  const char* bs = strrchr(b->name.chars, '/');
  if (as == nullptr || bs == nullptr)
    // both are in the root package
    return as == bs;
  ptrdiff_t a_count = as - a->name.chars, b_count = bs - b->name.chars;
  if (a_count != b_count)
    return false;
  return memcmp(a->name.chars, b->name.chars, a_count) == 0;
}

// The overrides relation is transitive, so we build it up during vtable
// creation.
static bool method_overrides(bjvm_cp_method *overrides, bjvm_cp_method *overridden) {
  if (overrides == nullptr)
    return false;
  // See JVMS 5.4.5: Criteria are that the overriding method is not private,
  // and the method is either public or protected, or package-private and in
  // the same run-time package as the overridden method.
  if (overrides->access_flags & BJVM_ACCESS_PRIVATE)
    return false;
  if (0 == (overrides->access_flags & (BJVM_ACCESS_PROTECTED | BJVM_ACCESS_PUBLIC)))
    return same_runtime_package(overrides->my_class, overridden->my_class);
  return true;
}

static bool include_method(bjvm_cp_method * method) {
  return !utf8_equals(method->name, "<init>") &&
    !(method->access_flags & BJVM_ACCESS_STATIC) &&
    !utf8_equals(method->name, "<clinit>") && !method->overrides;
}

void bjvm_setup_function_tables(bjvm_classdesc *classdesc) {
  bjvm_vtable *vtable = &classdesc->vtable;
  vtable->methods = nullptr;
  int methods_cap = 0;

  // If the class has a superclass, copy its vtable, replacing methods which
  // are overridden.
  if (classdesc->super_class) {
    bjvm_classdesc *super = classdesc->super_class->classdesc;
    for (int i = 0; i < super->vtable.method_count; ++i) {
      bjvm_cp_method *method = super->vtable.methods[i];
      bjvm_cp_method *replacement =
        bjvm_easy_method_lookup(classdesc, method->name, method->unparsed_descriptor, false, false);
      if (method_overrides(replacement, method)) {
        replacement->vtable_index = method->vtable_index;
        assert(method->vtable_index == i);
        method = replacement;
        replacement->overrides = true;
      }
      *VECTOR_PUSH(vtable->methods, vtable->method_count, methods_cap) = method;
    }
  }

  // Then push all new methods for which overrides is false
  for (int i = 0; i < classdesc->methods_count; ++i) {
    bjvm_cp_method *method = classdesc->methods + i;
    if (include_method(method)) {
      method->vtable_index = vtable->method_count;
      *VECTOR_PUSH(vtable->methods, vtable->method_count, methods_cap) = method;
    }
  }
}

void bjvm_free_function_tables(bjvm_classdesc *classdesc) {
  free(classdesc->vtable.methods);
}

bjvm_cp_method * bjvm_vtable_lookup(bjvm_classdesc *classdesc, int index) {
  assert(index >= 0 && index < classdesc->vtable.method_count);
  return classdesc->vtable.methods[index];
}