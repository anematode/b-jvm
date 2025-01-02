//
// Created by Cowpox on 12/30/24.
//

#ifndef VTABLE_H
#define VTABLE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Structures to support fast invokevirtual and invokeinterface calls. All
// classes have a vtable and itables initialized at link time.

typedef struct bjvm_classdesc bjvm_classdesc;
typedef struct bjvm_cp_method bjvm_cp_method;

typedef struct bjvm_vtable {
  int method_count;
  // The first methods will always be the same methods as those of the super-
  // class. That way, an invokevirtual call can be resolved by simply looking
  // at a constant offset into this table. (This is standard.)
  bjvm_cp_method **methods __attribute__((__counted_by__(method_count)));
} bjvm_vtable;

typedef uintptr_t bjvm_itable_method_t; // bjvm_cp_method*

enum {
  // The method is either abstract or ambiguous, and therefore an
  // IncompatibleClassChangeError ought to be raised if it is called.
  BJVM_ITABLE_METHOD_BIT_INVALID = 1 << 0,
  // The method is ambiguous: there are multiple methods with the same name
  // and signature which would be selected by an invokevirtual instruction.
  BJVM_ITABLE_METHOD_BIT_AMBIGUOUS = 1 << 1
};

// The interface pointers are stored separately from the entries so that the
// scanning for the desired interface can be done a bit more quickly. (We will
// write WASM stubs for this.)
typedef struct {
  bjvm_classdesc *interface;
  int method_count, method_cap;

  // All methods in the interface are found in at a consistent index, as
  // prescribed by the order in the original interface.
  //
  // The lower two bits of the method have significance, as explained above,
  // and the method is only valid in the context of an invokeinterface call if
  // the last bit is 0.
  bjvm_itable_method_t *methods __attribute__((__counted_by__(method_count)));
} bjvm_itable;

typedef struct {
  int interface_count, interface_cap, entries_count, entries_cap;

  // Scan this vector for the interface in question ...
  bjvm_classdesc **interfaces __attribute__((__counted_by__(interface_count)));
  // ... and look for the matching itable here
  bjvm_itable *entries __attribute__((__counted_by__(interface_count)));
} bjvm_itables;

// Set up a class descriptor's itables and vtable, assuming all of its
// superinterfaces and its superclass have already been linked (and their
// itables/vtable set up).
void bjvm_set_up_function_tables(bjvm_classdesc *classdesc);

// Free memory associated with a class's vtable and itables.
void bjvm_free_function_tables(bjvm_classdesc *classdesc);

// Look up a method in the vtable. No ranges are checked.
bjvm_cp_method *bjvm_vtable_lookup(bjvm_classdesc *classdesc, int index);

// Look up a method in the itables. No ranges are checked, but nullptr is
// returned if the object does not actually implement the method, or if there
// are multiple methods that could be called.
bjvm_cp_method *bjvm_itable_lookup(bjvm_classdesc *classdesc,
                                   bjvm_classdesc *interface, int index);

#ifdef __cplusplus
}
#endif

#endif // VTABLE_H
