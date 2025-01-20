//
// Created by alec on 1/13/25.
//

#ifndef BJVM_MODULES_H
#define BJVM_MODULES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <util.h>
#include <adt.h>

typedef enum {
    BJVM_MODULE_NORMAL,
    BJVM_MODULE_OPEN,
} bjvm_module_kind;

typedef enum {
    BJVM_MODULEPATH_ENTRY_DEFINITION,
    BJVM_MODULEPATH_ENTRY_DIRECTORY
} bjvm_modulepath_entry_kind;

typedef struct {
    bjvm_module_kind kind;
} bjvm_module_definition;

typedef struct {
    heap_string path;
    bjvm_module_definition *module_definitions;
    size_t module_count;
} bjvm_module_directory;

typedef struct {
    bjvm_modulepath_entry_kind kind;
    union {
        bjvm_module_definition *module;
        bjvm_module_directory *directory;
    };
} bjvm_modulepath_entry;

typedef struct {
    bjvm_modulepath_entry *entries;
    int entries_len;
    int entries_cap;

    heap_string as_colon_separated;
} bjvm_modulepath;

#ifdef __cplusplus
}
#endif

#endif //BJVM_MODULES_H
