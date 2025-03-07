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

#define DO_PEDANTIC_YIELDING 0
#if DO_PEDANTIC_YIELDING
#define DEBUG_PEDANTIC_YIELD(WAKEUP_INFO)                                                                              \
  do {                                                                                                                 \
    (WAKEUP_INFO).kind = RR_WAKEUP_YIELDING;                                                                           \
    ASYNC_YIELD((void *)&(WAKEUP_INFO));                                                                               \
    ASYNC_YIELD((void *)&(WAKEUP_INFO));                                                                               \
    ASYNC_YIELD((void *)&(WAKEUP_INFO));                                                                               \
    ASYNC_YIELD((void *)&(WAKEUP_INFO));                                                                               \
  } while (0)
#else
#define DEBUG_PEDANTIC_YIELD(WAKEUP_INFO)                                                                              \
  do {                                                                                                                 \
  } while (0)
#endif

typedef struct {
  // Associated VM
  vm *vm;
  // Preemption frequency in microseconds (clamped to 1000)
  u64 preemption_us;
  // Pointer to implementation
  void *_impl;
} rr_scheduler;

void rr_scheduler_init(rr_scheduler *scheduler, vm *vm);
void rr_scheduler_uninit(rr_scheduler *scheduler);

typedef enum {
  SCHEDULER_RESULT_DONE, // VM is done running tasks
  SCHEDULER_RESULT_MORE, // VM has more tasks to run
  SCHEDULER_RESULT_INVAL // VM in illegal state
} scheduler_status_t;

typedef struct {
  scheduler_status_t current_status; // MORE in order to request work to be done
  void *thread_info; // the work to do (thread_info), nullptr if none
  u64 may_sleep_us;  // how long the VM may sleep before the next task
} scheduler_polled_info_t;

typedef struct {
  scheduler_status_t status; // as long as this is MORE, the method isn't yet finished
  stack_value returned;
  vm_thread *thread; // The thread that this execution record corresponds to

  int js_handle; // TEMPORARY, to prevent GC
  vm *vm;
  void *_impl;
} execution_record;

scheduler_polled_info_t scheduler_poll(rr_scheduler *scheduler); // synchronize and fetch a job (tells scheduler that this is being worked on)
void scheduler_execute(vm *vm, scheduler_polled_info_t info, u64 preemption_us); // doesn't require synchronization todo: does it really need to pass in *info as a pointer?
void scheduler_push_execution_record(rr_scheduler *scheduler, scheduler_polled_info_t info); // synchronize and push the result back
scheduler_polled_info_t scheduler_push_execution_record_and_repoll(rr_scheduler *scheduler, scheduler_polled_info_t info); // same thing, but also poll within the same lock hold

scheduler_status_t rr_scheduler_execute_immediately(execution_record *record); // todo: i'm not sure how to work with threads here

typedef enum {
  RR_WAKEUP_YIELDING,          // timeslice yielded, resume soon
  RR_WAKEUP_SLEEP,             // Thread.sleep
  RR_WAKEUP_REFERENCE_PENDING, // Reference.waitForReferencePendingList
  RR_THREAD_PARK,              // Unsafe.park
  RR_MONITOR_ENTER_WAITING,    // wants to acquire mutex, but it's contended
  RR_MONITOR_WAIT,             // isn't holding, but is waiting for notify
} rr_wakeup_kind;

typedef struct {
  rr_wakeup_kind kind;
  u64 wakeup_us; // At this time, the thread should be rescheduled (0 if indefinitely)
  union {
    struct {
      handle *monitor; // for monitor enter and waiting
      bool ready;      // set by monitorexit and notify
    } monitor_wakeup;
  };
} rr_wakeup_info;

execution_record *rr_scheduler_run(rr_scheduler *scheduler, call_interpreter_t call);
void free_execution_record(execution_record *record);
void rr_scheduler_enumerate_gc_roots(rr_scheduler *scheduler, object **stbds_vector);

void monitor_notify_one(rr_scheduler *scheduler, obj_header *monitor);
void monitor_notify_all(rr_scheduler *scheduler, obj_header *monitor);

#ifdef __cplusplus
}
#endif

#endif // ROUNDROBIN_SCHEDULER_H
