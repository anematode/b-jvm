//
// Created by Tim Herchen on 3/7/25.
//

#ifndef CLASSLOADER_H
#define CLASSLOADER_H

#include "classfile.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct obj_header obj_header;

// Lazily created when a ClassLoader actually defines a class
typedef struct classloader {
  obj_header *mirror;   // Java object
  string_hash_table classes;
} classloader;

void free_classloader(classloader *classloader);

#ifdef __cplusplus
}
#endif

#endif //CLASSLOADER_H
