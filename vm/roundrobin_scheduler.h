//
// Created by Cowpox on 2/2/25.
//

#ifndef ROUNDROBIN_SCHEDULER_H
#define ROUNDROBIN_SCHEDULER_H

#include "bjvm.h"

#include <sys/time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  // Associated VM
  vm *vm;

  // Pointer to implementation
  void *_impl;
} rr_scheduler;

void rr_scheduler_init(rr_scheduler *scheduler, vm *vm);
void rr_scheduler_uninit(rr_scheduler *scheduler);

typedef enum { SCHEDULER_RESULT_DONE, SCHEDULER_RESULT_MORE, SCHEDULER_RESULT_INVAL } scheduler_status_t;

scheduler_status_t rr_scheduler_step(rr_scheduler *scheduler);
u64 rr_scheduler_may_sleep_us(rr_scheduler *scheduler);

typedef struct {
  scheduler_status_t status; // as long as this is MORE, the method isn't yet finished
  stack_value returned;
  vm_thread *thread; // The thread that this execution record corresponds to

  int js_handle; // TEMPORARY, to prevent GC
  vm *vm;
  void *_impl;
} execution_record;

scheduler_status_t rr_scheduler_execute_immediately(execution_record *record);

typedef enum {
  RR_WAKEUP_YIELDING,       // timeslice yielded, continue immediately
  RR_WAKEUP_SLEEP,          // Thread.sleep
  RR_MONITOR_ENTER_WAITING, // wants to acquire mutex, but it's contended
  RR_MONITOR_WAIT,          // isn't holding, but is waiting for notify
} rr_wakeup_kind;

typedef struct {
  rr_wakeup_kind kind;
  union {
    u64 wakeup_us; // At this time, the thread should be rescheduled
  };
} rr_wakeup_info;

execution_record *rr_scheduler_run(rr_scheduler *scheduler, call_interpreter_t call);
void free_execution_record(execution_record *record);
void rr_scheduler_enumerate_gc_roots(rr_scheduler *scheduler, object **stbds_vector);

#ifdef __cplusplus
}
#endif

#endif // ROUNDROBIN_SCHEDULER_H
