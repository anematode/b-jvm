//
// Created by alec on 12/18/24.
//

#ifndef BJVM_CLASSFILE_H
#define BJVM_CLASSFILE_H

#include "bjvm.h"

bjvm_cp_entry *bjvm_check_cp_entry(bjvm_cp_entry *entry, int expected_kinds,
                                   const char *reason);

#endif // BJVM_CLASSFILE_H
