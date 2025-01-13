//
// Created by alec on 1/13/25.
//

#include "monitor.h"

bjvm_monitor_state bjvm_acquire_monitor(bjvm_thread *thread, bjvm_obj_header *obj) {
    return BJVM_MONITOR_STATE_ACQUIRED;
}
bjvm_monitor_state bjvm_release_monitor(bjvm_thread *thread, bjvm_obj_header *obj) {
    return BJVM_MONITOR_STATE_FREE;
}