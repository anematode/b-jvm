//
// Created by Tim Herchen on 3/7/25.
//

#include "classloader.h"


void free_classloader(classloader * classloader) {
  free_hash_table(classloader->classes);
  free(classloader);
}