//
// Created by Max Cai on 2/7/25.
//

#ifndef MONITORS_H
#define MONITORS_H

#include <async.h>
#include <bjvm.h>
#include <roundrobin_scheduler.h>
#include <types.h>

DECLARE_ASYNC(int, monitor_acquire,
                    locals(handle *handle; rr_wakeup_info wakeup_info;), arguments(vm_thread *thread; obj_header *obj;),
                    invoked_methods());

// sets the hold count to a given value
DECLARE_ASYNC(int, monitor_reacquire_hold_count,
                    locals(handle *handle; rr_wakeup_info wakeup_info;), arguments(vm_thread *thread; obj_header *obj; int hold_count),
                    invoked_methods());

// reserve, but do not set a hold count
bool attempt_monitor_reserve(vm_thread *thread, obj_header *obj);

// returns how the hold count this thread has on the monitor (0 if not held by current thread)
u32 current_thread_hold_count(vm_thread *thread, obj_header *obj);

// returns the hold count that got released (returns 0 if fail)
u32 monitor_release_all_hold_count(vm_thread *thread, obj_header *obj);

// DECLARE_ASYNC(int, monitor_wait,
//                     locals(), arguments(thread *thread; obj_header *obj;),
//                     invoked_methods());

int monitor_release(vm_thread *thread, obj_header *obj);

#endif // MONITORS_H
