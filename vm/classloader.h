//
// Created by alec on 1/13/25.
//

#ifndef BJVM_CLASSLOADER_H
#define BJVM_CLASSLOADER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bjvm.h"

typedef struct {
    bjvm_obj_header *classloader_obj;
} bjvm_classloader;

extern bjvm_classloader bjvm_bootstrap_classloader;


#ifdef __cplusplus
}
#endif

#endif //BJVM_CLASSLOADER_H
