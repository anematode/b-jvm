//
// Created by alec on 1/13/25.
//

#ifndef BJVM_MONITOR_H
#define BJVM_MONITOR_H

#define BJVM_MULTITASKING

#include "bjvm.h"

typedef enum {
    BJVM_MONITOR_STATE_FREE = 0,
    BJVM_MONITOR_STATE_ACQUIRED = 1,
    BJVM_MONITOR_STATE_WAITING = 2,
} bjvm_monitor_state;

bjvm_monitor_state bjvm_acquire_monitor(bjvm_thread *thread, bjvm_obj_header *obj);
bjvm_monitor_state bjvm_release_monitor(bjvm_thread *thread, bjvm_obj_header *obj);

#endif //BJVM_MONITOR_H
