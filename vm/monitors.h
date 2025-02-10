//
// Created by Max Cai on 2/7/25.
//

#ifndef BJVM_MONITORS_H
#define BJVM_MONITORS_H

#include <async.h>
#include <types.h>
#include <bjvm.h>
#include <roundrobin_scheduler.h>

DECLARE_ASYNC(int, monitor_acquire,
                    locals(bjvm_handle *handle; rr_wakeup_info wakeup_info;), arguments(bjvm_thread *thread; bjvm_obj_header *obj;),
                    invoked_methods());

//DECLARE_ASYNC(int, monitor_wait,
//                    locals(), arguments(bjvm_thread *thread; bjvm_obj_header *obj;),
//                    invoked_methods());

int monitor_release(bjvm_thread *thread, bjvm_obj_header *obj);


#endif // BJVM_MONITORS_H
