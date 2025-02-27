#include "vtable.h"
#include "bjvm.h"
#include "classfile.h"

static bool same_runtime_package(const classdesc *a, const classdesc *b) {
  // Find last slash in both names, and compare the strings up to that point.
  const char *as = strrchr(a->name.chars, '/');
  const char *bs = strrchr(b->name.chars, '/');
  if (as == nullptr || bs == nullptr)
    // both are in the root package
    return as == bs;
  ptrdiff_t a_count = as - a->name.chars, b_count = bs - b->name.chars;
  return a_count == b_count && memcmp(a->name.chars, b->name.chars, a_count) == 0;
}

// The overrides relation is transitive, so we build it up during vtable
// creation.
static bool method_overrides(const cp_method *overrides, const cp_method *overridden) {
  if (overrides == nullptr)
    return false;
  // See JVMS 5.4.5: Criteria are that the overriding method is not private,
  // and the method is either public or protected, or package-private and in
  // the same run-time package as the overridden method.
  if (overrides->access_flags & ACCESS_PRIVATE)
    return false;
  if (0 == (overrides->access_flags & (ACCESS_PROTECTED | ACCESS_PUBLIC)))
    return same_runtime_package(overrides->my_class, overridden->my_class);
  return true;
}

static bool vtable_include(const cp_method *method) {
  return !method->is_ctor && !method->is_clinit && !(method->access_flags & ACCESS_STATIC) && !method->overrides;
}

static cp_method *get_unambiguous_method(itable_method_t m) {
  return (cp_method *)(m & ~(ITABLE_METHOD_BIT_INVALID | ITABLE_METHOD_BIT_AMBIGUOUS));
}

static bool is_ambiguous(itable_method_t m) { return m & ITABLE_METHOD_BIT_AMBIGUOUS; }

static itable_method_t mark_ambiguous(itable_method_t m) {
  return m | ITABLE_METHOD_BIT_INVALID | ITABLE_METHOD_BIT_AMBIGUOUS;
}

static itable_method_t make(const cp_method *m) {
  itable_method_t v = (itable_method_t)m;
  if (m->access_flags & ACCESS_ABSTRACT) {
    v |= ITABLE_METHOD_BIT_INVALID;
  }
  return v;
}

static itable copy_itable(const itable *src) {
  itable result = {0};
  result.interface = src->interface;
  ptrdiff_t c = arrlen(src->methods);
  if (c) {
    memcpy(arraddnptr(result.methods, c), src->methods, c * sizeof(itable_method_t));
  }
  return result;
}

// Concatenate the method name and descriptor, separated by a colon.
static slice identify(slice *scratch, const cp_method *method) {
  char *write = stpncpy(scratch->chars, method->name.chars, scratch->len);
  *write++ = ':';
  write = stpncpy(write, method->unparsed_descriptor.chars, scratch->chars + scratch->len + 1 - write);
  scratch->len = write - scratch->chars;
  return *scratch;
}

static void merge_itable(itable *dst, const itable *src, string_hash_table poisoned, classdesc *classdesc) {
  INIT_STACK_STRING(scratch, 1024);
  DCHECK(dst->interface == src->interface);
  DCHECK(arrlen(dst->methods) == arrlen(src->methods));
  for (int i = 0; i < arrlen(src->methods); ++i) {
    itable_method_t d = dst->methods[i], s = src->methods[i], result;
    DCHECK(s != 0, "i-table method must not be null");
    DCHECK(d != 0, "i-table method must not be null");

    [[maybe_unused]]
    bool d_abs = get_unambiguous_method(d)->access_flags & ACCESS_ABSTRACT,
         c_abs = get_unambiguous_method(s)->access_flags & ACCESS_ABSTRACT;
    if (d_abs || d == s) {
      result = s; // c is concrete, or both are abstract so choose one
    } else {
      result = d; // d is concrete
    }

    // Now check if the method is ambiguous
    cp_method *d_method = get_unambiguous_method(d);
    slice identifier = identify(&scratch, d_method);
    if (hash_table_lookup(&poisoned, identifier.chars, identifier.len)) {
      result = mark_ambiguous(result);
    }

    // Then check to see if the class or a superclass has a matching method
    // which overrides the method in question.
    cp_method *maybe_overrides = method_lookup(classdesc, d_method->name, d_method->unparsed_descriptor, true, false);
    if (method_overrides(maybe_overrides, d_method)) {
      result = make(maybe_overrides); // unambiguous of course
    }
    dst->methods[i] = result;
  }
}

static bool itable_include(const cp_method *method) {
  return !method->is_ctor && !method->is_clinit && !(method->access_flags & ACCESS_STATIC);
}

static void setup_itables(classdesc *super, classdesc *cd) {
  itables *itables = &cd->itables;
  DCHECK(!itables->interfaces, "i-tables already set up");

  // Scan superinterface itables for conflicting methods, i.e., methods which
  // have the same name and descriptor.
  INIT_STACK_STRING(scratch, 1024);
  // Map methodname:descriptor to a pointer to the existing method
  string_hash_table discovered = make_hash_table(nullptr, 0.75, 16);
  // Map methodname:descriptor to 1 if that method name is ambiguous among
  // the superinterfaces.
  string_hash_table ambiguous = make_hash_table(nullptr, 0.75, 16);

  for (int iface_i = 0; iface_i <= cd->interfaces_count; ++iface_i) {
    classdesc *iface;
    if (iface_i < cd->interfaces_count)
      iface = cd->interfaces[iface_i]->classdesc;
    else
      iface = super;
    DCHECK(iface, "Superclass or -interface not resolved");
    for (int itable_i = 0; itable_i < arrlen(iface->itables.interfaces); ++itable_i) {
      itable *super_itable = iface->itables.entries + itable_i;
      for (int method_i = 0; method_i < arrlen(super_itable->methods); ++method_i) {
        itable_method_t ref = super_itable->methods[method_i];
        cp_method *underlying = get_unambiguous_method(ref);
        // Skip methods which are defined in a class, since those take
        // priority over superinterfaces
        if (!(underlying->my_class->access_flags & ACCESS_INTERFACE))
          continue;
        slice ident = identify(&scratch, underlying);
        void *existing = hash_table_lookup(&discovered, ident.chars, ident.len);
        if ((existing && existing != underlying) || is_ambiguous(ref)) {
          // Ambiguity across superinterfaces :o
          (void)hash_table_insert(&ambiguous, ident.chars, ident.len, (void *)1);
        } else if (existing == nullptr) {
          (void)hash_table_insert(&discovered, ident.chars, ident.len, underlying);
        }
      }
    }
  }

  free_hash_table(discovered);

  // Now merge the itables of the superinterfaces. Whenever we encounter a
  // poisoned method in an itable, replace that entry with nullptr, unless
  // a definition of that method is found in a superclass or in the current
  // class, in which case we can happily use that definition per the resolution
  // rules, which prioritise superclasses.
  for (int iface_i = 0; iface_i <= cd->interfaces_count; ++iface_i) {
    classdesc *iface;
    iface = iface_i < cd->interfaces_count ? cd->interfaces[iface_i]->classdesc : super;
    DCHECK(iface, "Superclass or -interface not resolved");
    for (int itable_i = 0; itable_i < arrlen(iface->itables.interfaces); ++itable_i) {
      // Look if we already implement this interface, and if so, merge with
      // what we have.
      const itable *super_itable = iface->itables.entries + itable_i;
      itable *merge_with;
      for (int k = 0; k < arrlen(itables->interfaces); ++k) {
        if (itables->interfaces[k] == super_itable->interface) {
          merge_with = itables->entries + k;
          DCHECK(merge_with->interface == super_itable->interface);
          goto merge;
        }
      }

      // New interface for this class, push a copy of it
      arrput(itables->interfaces, super_itable->interface);
      merge_with = arraddnptr(itables->entries, 1);
      *merge_with = copy_itable(super_itable);

    merge:
      merge_itable(merge_with, super_itable, ambiguous, cd);
    }
  }

  // make sure the lookup vector and the result vector have the same length
  DCHECK(arrlen(itables->interfaces) == arrlen(itables->entries));
  free_hash_table(ambiguous);
}

// TODO consider optimizing final methods out of the tables?
void set_up_function_tables(classdesc *cd) {
  vtable *vtable = &cd->vtable;
  DCHECK(!vtable->methods, "v-table already set up");

  // If the class has a superclass, copy its vtable, replacing methods which
  // are overridden.
  if (cd->super_class) {
    classdesc *super = cd->super_class->classdesc;
    for (size_t i = 0; i < arrlenu(super->vtable.methods); ++i) {
      cp_method *method = super->vtable.methods[i];
      cp_method *replacement = method_lookup(cd, method->name, method->unparsed_descriptor, false, false);
      if (method_overrides(replacement, method)) {
        replacement->vtable_index = method->vtable_index;
        DCHECK(method->vtable_index == i);
        method = replacement;
        replacement->overrides = true;
      }
      arrpush(vtable->methods, method);
    }

    setup_itables(super, cd);
  }

  // If the class is itself an interface, create a new itable representing that
  // interface, only including the appropriate functions (i.e., functions which
  // are public and non-static).
  if (cd->access_flags & ACCESS_INTERFACE) {
    itable itable = {cd};
    for (int i = 0; i < cd->methods_count; ++i) {
      cp_method *method = cd->methods + i;
      if (itable_include(method)) {
        method->itable_index = arrlen(itable.methods);
        arrpush(itable.methods, make(method));
      }
    }
    itables *itables = &cd->itables;
    arrput(itables->interfaces, cd);
    arrput(itables->entries, itable);
  }

  // Then push all new methods which are not represented in a vtable into the
  // class's vtable
  for (int i = 0; i < cd->methods_count; ++i) {
    cp_method *method = cd->methods + i;
    if (vtable_include(method)) {
      method->vtable_index = arrlenu(vtable->methods);
      arrpush(vtable->methods, method);
    }
  }
}

void free_function_tables(classdesc *classdesc) {
  arrfree(classdesc->vtable.methods);
  for (int i = 0; i < arrlen(classdesc->itables.entries); ++i) {
    arrfree(classdesc->itables.entries[i].methods);
  }
  arrfree(classdesc->itables.interfaces);
  arrfree(classdesc->itables.entries);
}

cp_method *vtable_lookup(classdesc const *classdesc, size_t index) {
  DCHECK(index < arrlenu(classdesc->vtable.methods) && "vtable index out of range");
  return classdesc->vtable.methods[index];
}

cp_method *itable_lookup(classdesc const *cd, classdesc const *interface, size_t index) {
  for (int i = 0; i < arrlen(cd->itables.interfaces); ++i) {
    if (cd->itables.interfaces[i] == interface) {
      itable itable = cd->itables.entries[i];
      DCHECK(index < arrlenu(itable.methods) && "itable index out of range");
      itable_method_t m = itable.methods[index];
      if (m & ITABLE_METHOD_BIT_INVALID) {
        return nullptr;
      }
      cp_method *method = (cp_method *)m;
      return method;
    }
  }
  return nullptr;
}