//
// Created by Tim Herchen on 3/8/25.
//

#ifndef CLASSLOADER_H
#define CLASSLOADER_H

#include "adt.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct obj_header obj_header;
typedef struct vm vm;

typedef struct classloader {
  // The Java mirror of this class loader (null iff this is the bootstrap class loader)
  obj_header *java_mirror;
  // whether this classloader is the bootstrap class loader
  bool is_bootstrap;
  // parent class loader (null iff this is the bootstrap class loader)
  struct classloader *parent;
  // set of classes which this loader has defined
  string_hash_table loaded;
  // set of classes for which this loader is an initiating loader
  string_hash_table initiating;
} classloader;

// CONTRACT: java_mirror must be null or a (subclass of) java/lang/ClassLoader, and not already registered.
// If java_mirror is null, creates the bootstrap classloader.
int classloader_init(vm *vm, classloader *cl, obj_header *java_mirror);
// Returns null if the class loader hasn't loaded a class yet. Returns the bootstrap class loader if java_mirror is
// null.
classloader *unmirror_classloader(vm *vm, obj_header *java_mirror);
void classloader_uninit(classloader *cl);

#ifdef __cplusplus
}
#endif

#endif // CLASSLOADER_H
