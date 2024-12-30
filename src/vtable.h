//
// Created by Cowpox on 12/30/24.
//

#ifndef VTABLE_H
#define VTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

// Structures to support fast invokevirtual, invokespecial and invokeinterface
// calls.

typedef struct bjvm_classdesc bjvm_classdesc;
typedef struct bjvm_cp_method bjvm_cp_method;

typedef struct bjvm_vtable {
  int method_count;
  // The first methods will always be the same methods as those of the super-
  // class. That way, an invokevirtual call can be resolved by simply looking
  // at a constant offset into this table. (This is standard.)
  bjvm_cp_method **methods __attribute__((__counted_by__(method_count)));
} bjvm_vtable;

// The interface pointers are stored separately from the entries so that the
// scanning for the desired interface can be done a bit more quickly. (We will
// write WASM stubs for this, probably using v128 compares.)
typedef struct {
  int method_count;
  // All methods in the interface are found in at a consistent index, as
  // prescribed by the order in the original interface.
  bjvm_cp_method **methods __attribute__((__counted_by__(method_count)));
} bjvm_itable;

typedef struct {
  int interface_count;

  bjvm_classdesc **interfaces __attribute__((__counted_by__(interface_count)));
  bjvm_itable *entries __attribute__((__counted_by__(interface_count)));
} bjvm_itables;

// Set up a class descriptor's itables and vtable, assuming all of its
// superinterfaces and its superclass have already been linked (and their
// itables/vtable set up).
void bjvm_setup_function_tables(bjvm_classdesc *classdesc);

// Free memory associated with a class's vtable and itables.
void bjvm_free_function_tables(bjvm_classdesc *classdesc);

// Look up a method in the vtable. No ranges are checked.
bjvm_cp_method *bjvm_vtable_lookup(bjvm_classdesc *classdesc, int index);

// Look up a method in the itables. No ranges are checked.
bjvm_cp_method *bjvm_itable_lookup(bjvm_classdesc *classdesc,
                                   bjvm_classdesc *interface, int index);

#ifdef __cplusplus
}
#endif

#endif // VTABLE_H
