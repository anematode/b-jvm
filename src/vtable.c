#include "vtable.h"
#include "bjvm.h"
#include "classfile.h"

static bool same_runtime_package(const bjvm_classdesc *a,
                                 const bjvm_classdesc *b) {
  // Find last slash in both names, and compare the strings up to that point.
  const char *as = strrchr(a->name.chars, '/');
  const char *bs = strrchr(b->name.chars, '/');
  if (as == nullptr || bs == nullptr)
    // both are in the root package
    return as == bs;
  ptrdiff_t a_count = as - a->name.chars, b_count = bs - b->name.chars;
  return a_count == b_count &&
         memcmp(a->name.chars, b->name.chars, a_count) == 0;
}

// The overrides relation is transitive, so we build it up during vtable
// creation.
static bool method_overrides(const bjvm_cp_method *overrides,
                             const bjvm_cp_method *overridden) {
  if (overrides == nullptr)
    return false;
  // See JVMS 5.4.5: Criteria are that the overriding method is not private,
  // and the method is either public or protected, or package-private and in
  // the same run-time package as the overridden method.
  if (overrides->access_flags & BJVM_ACCESS_PRIVATE)
    return false;
  if (0 ==
      (overrides->access_flags & (BJVM_ACCESS_PROTECTED | BJVM_ACCESS_PUBLIC)))
    return same_runtime_package(overrides->my_class, overridden->my_class);
  return true;
}

static bool vtable_include(const bjvm_cp_method *method) {
  return !utf8_equals(method->name, "<init>") &&
         !utf8_equals(method->name, "<clinit>") &&
         !(method->access_flags & BJVM_ACCESS_STATIC) && !method->overrides;
}

static bjvm_cp_method *get_unambiguous_method(bjvm_itable_method_t m) {
  return (bjvm_cp_method *)(m & ~(BJVM_ITABLE_METHOD_BIT_INVALID |
                                  BJVM_ITABLE_METHOD_BIT_AMBIGUOUS));
}

static bool is_ambiguous(bjvm_itable_method_t m) {
  return m & BJVM_ITABLE_METHOD_BIT_AMBIGUOUS;
}

static bjvm_itable_method_t mark_ambiguous(bjvm_itable_method_t m) {
  return m | BJVM_ITABLE_METHOD_BIT_INVALID | BJVM_ITABLE_METHOD_BIT_AMBIGUOUS;
}

static bjvm_itable_method_t make(const bjvm_cp_method *m) {
  bjvm_itable_method_t v = (bjvm_itable_method_t)m;
  if (m->access_flags & BJVM_ACCESS_ABSTRACT) {
    v |= BJVM_ITABLE_METHOD_BIT_INVALID;
  }
  return v;
}

static bjvm_itable copy_itable(const bjvm_itable *src) {
  bjvm_itable result = {0};
  result.interface = src->interface;
  ptrdiff_t c = arrlen(src->methods);
  if (c) {
    memcpy(arraddnptr(result.methods, c), src->methods,
           c * sizeof(bjvm_itable_method_t));
  }
  return result;
}

// Concatenate the method name and descriptor, separated by a colon.
static bjvm_utf8 identify(bjvm_utf8 *scratch, const bjvm_cp_method *method) {
  char *write = stpncpy(scratch->chars, method->name.chars, scratch->len);
  *write++ = ':';
  write = stpncpy(write, method->unparsed_descriptor.chars,
                  scratch->chars + scratch->len + 1 - write);
  scratch->len = write - scratch->chars;
  return *scratch;
}

static void merge_itable(bjvm_itable *dst, const bjvm_itable *src,
                         bjvm_string_hash_table poisoned,
                         bjvm_classdesc *classdesc) {
  INIT_STACK_STRING(scratch, 1024);
  assert(dst->interface == src->interface);
  assert(arrlen(dst->methods) == arrlen(src->methods));
  for (int i = 0; i < arrlen(src->methods); ++i) {
    bjvm_itable_method_t d = dst->methods[i], s = src->methods[i], result;
    assert(s != 0 && "i-table method must not be null");
    assert(d != 0 && "i-table method must not be null");
    bool d_abs = get_unambiguous_method(d)->access_flags & BJVM_ACCESS_ABSTRACT,
         c_abs = get_unambiguous_method(s)->access_flags & BJVM_ACCESS_ABSTRACT;
    if (d_abs || d == s) {
      result = s; // c is concrete, or both are abstract so choose one
    } else if (c_abs) {
      result = d; // d is concrete
    } else {
      result = mark_ambiguous(d); // both are concrete -- ambiguous!
    }

    // Now check if the method is ambiguous
    bjvm_cp_method *d_method = get_unambiguous_method(d);
    bjvm_utf8 identifier = identify(&scratch, d_method);
    if (bjvm_hash_table_lookup(&poisoned, identifier.chars, identifier.len)) {
      result = mark_ambiguous(result);
    }

    // Then check to see if the class or a superclass has a matching method
    // which overrides the method in question.
    bjvm_cp_method *maybe_overrides = bjvm_method_lookup(
        classdesc, d_method->name, d_method->unparsed_descriptor, true, false);
    if (method_overrides(maybe_overrides, d_method)) {
      result = make(maybe_overrides); // unambiguous of course
    }
    dst->methods[i] = result;
  }
}

static bool itable_include(const bjvm_cp_method *method) {
  return !utf8_equals(method->name, "<init>") &&
         !utf8_equals(method->name, "<clinit>") &&
         !(method->access_flags & (BJVM_ACCESS_STATIC | BJVM_ACCESS_PRIVATE));
}

static void setup_itables(bjvm_classdesc *super, bjvm_classdesc *classdesc) {
  bjvm_itables *itables = &classdesc->itables;
  assert(!itables->interfaces && "i-tables already set up");

  // Scan superinterface itables for conflicting methods, i.e., methods which
  // have the same name and descriptor.
  INIT_STACK_STRING(scratch, 1024);
  // Map methodname:descriptor to a pointer to the existing method
  bjvm_string_hash_table discovered = bjvm_make_hash_table(nullptr, 0.75, 16);
  // Map methodname:descriptor to 1 if that method name is ambiguous among
  // the superinterfaces.
  bjvm_string_hash_table ambiguous = bjvm_make_hash_table(nullptr, 0.75, 16);

  for (int iface_i = 0; iface_i <= classdesc->interfaces_count; ++iface_i) {
    bjvm_classdesc *iface;
    if (iface_i < classdesc->interfaces_count)
      iface = classdesc->interfaces[iface_i]->classdesc;
    else
      iface = super;
    assert(iface && "Superclass or -interface not resolved");
    for (int itable_i = 0; itable_i < arrlen(iface->itables.interfaces);
         ++itable_i) {
      bjvm_itable *super_itable = iface->itables.entries + itable_i;
      for (int method_i = 0; method_i < arrlen(super_itable->methods);
           ++method_i) {
        bjvm_itable_method_t ref = super_itable->methods[method_i];
        bjvm_cp_method *underlying = get_unambiguous_method(ref);
        // Skip methods which are defined in a class, since those take
        // priority over superinterfaces
        if (!(underlying->my_class->access_flags & BJVM_ACCESS_INTERFACE))
          continue;
        bjvm_utf8 ident = identify(&scratch, underlying);
        void *existing =
            bjvm_hash_table_lookup(&discovered, ident.chars, ident.len);
        if ((existing && existing != underlying) || is_ambiguous(ref)) {
          // Ambiguity across superinterfaces :o
          (void)bjvm_hash_table_insert(&ambiguous, ident.chars, ident.len,
                                       (void *)1);
        } else if (existing == nullptr) {
          (void)bjvm_hash_table_insert(&discovered, ident.chars, ident.len,
                                       underlying);
        }
      }
    }
  }

  bjvm_free_hash_table(discovered);

  // Now merge the itables of the superinterfaces. Whenever we encounter a
  // poisoned method in an itable, replace that entry with nullptr, unless
  // a definition of that method is found in a superclass or in the current
  // class, in which case we can happily use that definition per the resolution
  // rules, which prioritise superclasses.
  for (int iface_i = 0; iface_i <= classdesc->interfaces_count; ++iface_i) {
    bjvm_classdesc *iface;
    iface = iface_i < classdesc->interfaces_count
                ? classdesc->interfaces[iface_i]->classdesc
                : super;
    assert(iface && "Superclass or -interface not resolved");
    for (int itable_i = 0; itable_i < arrlen(iface->itables.interfaces);
         ++itable_i) {
      // Look if we already implement this interface, and if so, merge with
      // what we have.
      const bjvm_itable *super_itable = iface->itables.entries + itable_i;
      bjvm_itable *merge_with;
      for (int k = 0; k < arrlen(itables->interfaces); ++k) {
        if (itables->interfaces[k] == super_itable->interface) {
          merge_with = itables->entries + k;
          assert(merge_with->interface == super_itable->interface);
          goto merge;
        }
      }

      // New interface for this class, push a copy of it
      arrput(itables->interfaces, super_itable->interface);
      merge_with = arraddnptr(itables->entries, 1);
      *merge_with = copy_itable(super_itable);

    merge:
      merge_itable(merge_with, super_itable, ambiguous, classdesc);
    }
  }

  // make sure the lookup vector and the result vector have the same length
  assert(arrlen(itables->interfaces) == arrlen(itables->entries));
  bjvm_free_hash_table(ambiguous);
}

// TODO consider optimizing final methods out of the tables?
void bjvm_set_up_function_tables(bjvm_classdesc *classdesc) {
  bjvm_vtable *vtable = &classdesc->vtable;
  assert(!vtable->methods && "v-table already set up");

  // If the class has a superclass, copy its vtable, replacing methods which
  // are overridden.
  if (classdesc->super_class) {
    bjvm_classdesc *super = classdesc->super_class->classdesc;
    for (int i = 0; i < arrlen(super->vtable.methods); ++i) {
      bjvm_cp_method *method = super->vtable.methods[i];
      bjvm_cp_method *replacement = bjvm_method_lookup(
          classdesc, method->name, method->unparsed_descriptor, false, false);
      if (method_overrides(replacement, method)) {
        replacement->vtable_index = method->vtable_index;
        assert(method->vtable_index == i);
        method = replacement;
        replacement->overrides = true;
      }
      arrpush(vtable->methods, method);
    }

    setup_itables(super, classdesc);
  }

  // If the class is itself an interface, create a new itable representing that
  // interface, only including the appropriate functions (i.e., functions which
  // are public and non-static).
  if (classdesc->access_flags & BJVM_ACCESS_INTERFACE) {
    bjvm_itable itable = {classdesc};
    for (int i = 0; i < classdesc->methods_count; ++i) {
      bjvm_cp_method *method = classdesc->methods + i;
      if (itable_include(method)) {
        method->itable_index = arrlen(itable.methods);
        arrpush(itable.methods, make(method));
      }
    }
    bjvm_itables *itables = &classdesc->itables;
    arrput(itables->interfaces, classdesc);
    arrput(itables->entries, itable);
  }

  // Then push all new methods which are not represented in a vtable into the
  // class's vtable
  for (int i = 0; i < classdesc->methods_count; ++i) {
    bjvm_cp_method *method = classdesc->methods + i;
    if (vtable_include(method)) {
      method->vtable_index = arrlen(vtable->methods);
      arrpush(vtable->methods, method);
    }
  }
}

void bjvm_free_function_tables(bjvm_classdesc *classdesc) {
  arrfree(classdesc->vtable.methods);
  for (int i = 0; i < arrlen(classdesc->itables.entries); ++i) {
    arrfree(classdesc->itables.entries[i].methods);
  }
  arrfree(classdesc->itables.interfaces);
  arrfree(classdesc->itables.entries);
}

bjvm_cp_method *bjvm_vtable_lookup(bjvm_classdesc *classdesc, int index) {
  assert(index >= 0 && index < arrlen(classdesc->vtable.methods) &&
         "vtable index out of range");
  return classdesc->vtable.methods[index];
}

bjvm_cp_method *bjvm_itable_lookup(bjvm_classdesc *classdesc,
                                   bjvm_classdesc *interface, int index) {
  for (int i = 0; i < arrlen(classdesc->itables.interfaces); ++i) {
    if (classdesc->itables.interfaces[i] == interface) {
      bjvm_itable itable = classdesc->itables.entries[i];
      assert(index >= 0 && index < arrlen(itable.methods) &&
             "itable index out of range");
      bjvm_itable_method_t m = itable.methods[index];
      if (m & BJVM_ITABLE_METHOD_BIT_INVALID) {
        return nullptr;
      }
      bjvm_cp_method *method = (bjvm_cp_method *)m;
      return method;
    }
  }
  return nullptr;
}