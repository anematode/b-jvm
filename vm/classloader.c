#include "classloader.h"
#include "cached_classdescs.h"
#include "bjvm.h"

// NOLINTNEXTLINE(misc-no-recursion)
int classloader_init(vm *vm, classloader *cl, obj_header *java_mirror) {
  DCHECK(java_mirror == nullptr || instanceof(java_mirror->descriptor, cached_classes(vm)->class_loader),
    "Object is not a java/lang/ClassLoader");

  struct native_ClassLoader* mirror = (struct native_ClassLoader*)java_mirror;
  DCHECK(mirror == nullptr || mirror->reflected_loader == nullptr, "Class loader is already registered");

  cl->java_mirror = java_mirror;
  cl->is_bootstrap = java_mirror == nullptr;
  cl->parent = cl->is_bootstrap ? nullptr : unmirror_classloader(vm, mirror->parent);
  cl->loaded = make_hash_table(free_classdesc, 0.75, 1);
  cl->initiating = make_hash_table(nullptr /* all classes are loaded by exactly one class loader */, 0.75, 1);
  if (mirror) {
    mirror->reflected_loader = cl;
  }
  return 0;
}

// NOLINTNEXTLINE(misc-no-recursion)
classloader *unmirror_classloader(vm *vm, obj_header *java_mirror) {
  if (java_mirror == nullptr) {
    return vm->bootstrap_classloader;
  }

  DCHECK(instanceof(java_mirror->descriptor, cached_classes(vm)->class_loader));
  struct native_ClassLoader* mirror = (struct native_ClassLoader*)java_mirror;
  if (mirror->reflected_loader == nullptr) {
    // Class loader is not yet registered: push a new one
    classloader *cl = calloc(1, sizeof(classloader));
    classloader_init(vm, cl, java_mirror);
    arrput(vm->active_classloaders, cl);
    mirror->reflected_loader = cl;
    cl->parent = unmirror_classloader(vm, mirror->parent);
  }
  return mirror->reflected_loader;
}

void classloader_uninit(classloader *cl) {
  free_hash_table(cl->loaded);
  free_hash_table(cl->initiating);
}