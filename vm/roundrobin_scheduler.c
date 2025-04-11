// TODO better memory management. It's horrible rn

#include "roundrobin_scheduler.h"

#include "exceptions.h"
#include "monitors.h"
#include <pthread.h>

typedef struct {
  call_interpreter_t call;
  execution_record *record;
} pending_call;

typedef struct {
  vm_thread *thread;
  // pending calls for this thread (processed in order)
  pending_call *call_queue;
  rr_wakeup_info *wakeup_info;
  bool is_running;
} thread_info;

typedef struct {
  thread_info **round_robin; // Threads are cycled here
  execution_record **executions;
  pthread_mutex_t mutex; // used for all changes to the scheduler
} impl;

static bool only_daemons_running(thread_info **thread_info);
static u64 rr_scheduler_may_sleep_us(rr_scheduler *scheduler);
static void free_execution_record_impl(execution_record *record);
static void monitor_notify_one_impl(impl *I, obj_header *monitor);
static void monitor_notify_all_impl(impl *I, obj_header *monitor);
static void free_thread_info_shutdown(thread_info *info);
static void free_thread_info(rr_scheduler *scheduler, thread_info *info);

/// the raw operation which assumes the lock is held already
static void monitor_notify_one_impl(impl *I, obj_header *monitor) {
  // iterate through the threads and find one that is waiting on the monitor
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    rr_wakeup_info *wakeup_info = I->round_robin[i]->wakeup_info;
    if (!wakeup_info)
      continue;
    if (wakeup_info->kind == RR_MONITOR_WAIT && wakeup_info->monitor_wakeup.monitor->obj == monitor) {
      wakeup_info->monitor_wakeup.ready = true;
      break;
    }
  }
}

void monitor_notify_one(rr_scheduler *scheduler, obj_header *monitor) {
  // iterate through the threads and find one that is waiting on the monitor
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?
  monitor_notify_one_impl(I, monitor);
  pthread_mutex_unlock(&I->mutex); // todo: check result?
}

/// the raw operation which assumes the lock is held already
static void monitor_notify_all_impl(impl *I, obj_header *monitor) {
  // iterate through the threads and find all that are waiting on the monitor
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    rr_wakeup_info *wakeup_info = I->round_robin[i]->wakeup_info;
    if (!wakeup_info)
      continue;
    if (wakeup_info->kind == RR_MONITOR_WAIT && wakeup_info->monitor_wakeup.monitor->obj == monitor) {
      wakeup_info->monitor_wakeup.ready = true;
    }
  }
}

void monitor_notify_all(rr_scheduler *scheduler, obj_header *monitor) {
  // iterate through the threads and find all that are waiting on the monitor
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?
  monitor_notify_all_impl(I, monitor);
  pthread_mutex_unlock(&I->mutex); // todo: check result?
}

// assumes the lock is held
static void free_thread_info(rr_scheduler *scheduler, thread_info *info) {
  impl *I = scheduler->_impl;
  // technically doesn't need to acquire the monitor to notify, since scheduler is god
  info->thread->thread_obj->eetop = 0; // set the eetop to nullptr
  monitor_notify_all_impl(I, (obj_header *)info->thread->thread_obj); // we can't have reentrancy; call inner

  arrfree(info->call_queue);
  free(info);
}

/// does not reättempt the notifyAll on the thread; assume the scheduler is dead
static void free_thread_info_shutdown(thread_info *info) {
  info->thread->thread_obj->eetop = 0; // set the eetop to nullptr

  for (int call_i = 0; call_i < arrlen(info->call_queue); call_i++) {
    pending_call *pending = &info->call_queue[call_i];
    free_execution_record_impl(pending->record); // todo: why are we doing this? i thought that the caller should free the record when done
    free(pending->call.args.args);
  }

  arrfree(info->call_queue);
  free(info);
}

void rr_scheduler_init(rr_scheduler *scheduler, vm *vm) {
  scheduler->vm = vm;
  scheduler->preemption_us = 30000;
  scheduler->_impl = calloc(1, sizeof(impl));
  impl *I = scheduler->_impl;
  worker_thread_pool_init(&scheduler->thread_pool);
  pthread_mutex_init(&I->mutex, nullptr); // todo: check result of this?
}

void rr_scheduler_uninit(rr_scheduler *scheduler) {
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    free_thread_info_shutdown(I->round_robin[i]);
  }
  for (int i = 0; i < arrlen(I->executions); i++) {
    free(I->executions[i]);
  }
  arrfree(I->executions);
  arrfree(I->round_robin);
  worker_thread_pool_uninit(&scheduler->thread_pool);
  pthread_mutex_unlock(&I->mutex); // todo: check result?
  pthread_mutex_destroy(&I->mutex); // todo: check result of this?
  free(I);
}

static bool is_sleeping(thread_info *info, u64 time) {
  rr_wakeup_info *wakeup_info = info->wakeup_info;
  if (!wakeup_info)
    return false;
  if (wakeup_info->kind == RR_WAKEUP_REFERENCE_PENDING) {
    return !info->thread->vm->reference_pending_list;
  }
  if (wakeup_info->kind == RR_MONITOR_ENTER_WAITING) {
    // try to acquire the monitor
    return !attempt_monitor_reserve(info->thread, wakeup_info->monitor_wakeup.monitor->obj);
  }
  if (wakeup_info->kind == RR_WAKEUP_SLEEP ||
      (wakeup_info->kind == RR_THREAD_PARK && !query_unpark_permit(info->thread)) ||
      (wakeup_info->kind == RR_MONITOR_WAIT && !wakeup_info->monitor_wakeup.ready)) {
    u64 wakeup = wakeup_info->wakeup_us;
    bool interrupted = info->thread->thread_obj->interrupted;
    return !interrupted && (wakeup == 0 || wakeup >= time);
  } else {
    return false; // blocking on something else which presumably can resume soon
  }
}

// assumes the lock is held
static thread_info *get_next_thr(impl *impl) {
  assert(impl->round_robin && "No threads to run");
  thread_info *info = nullptr;
  u64 time = get_unix_us();
  for (int i = 0; i < arrlen(impl->round_robin); ++i) {
    info = impl->round_robin[0];
    arrdel(impl->round_robin, 0);
    arrput(impl->round_robin, info);
    if (!is_sleeping(info, time) && !info->is_running) {
      return info;
    }
  }

  return nullptr;
}

constexpr scheduler_polled_info_t SCHEDULER_POLLED_DONE = { SCHEDULER_RESULT_DONE, nullptr, 0 };

scheduler_polled_info_t scheduler_poll(rr_scheduler *scheduler) {
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?

  if (arrlen(I->round_robin) == 0 || only_daemons_running(I->round_robin)) {
    pthread_mutex_unlock(&I->mutex); // todo: check result?
    return SCHEDULER_POLLED_DONE;
  }

  scheduler_polled_info_t result = { SCHEDULER_RESULT_MORE, nullptr, 0 };
  thread_info *info = get_next_thr(I);
  if (!info) {
    // returned nullptr; no threads are available to run
    result.may_sleep_us = rr_scheduler_may_sleep_us(scheduler);
  } else {
    // else, return it
    result.thread_info = info;
    info->is_running = true;
  }

  pthread_mutex_unlock(&I->mutex); // todo: check result?
  return result;
}

void scheduler_execute(vm *vm, scheduler_polled_info_t info, u64 preemption_us) {
  thread_info *impl_info = (thread_info *)info.thread_info;
  if (unlikely((!impl_info))) return;

  vm_thread *thread = impl_info->thread;
  u64 time = get_unix_us();

  impl_info->wakeup_info = nullptr; // wake up!
  thread->fuel = 200000;

  if (__builtin_add_overflow(time, preemption_us, &thread->yield_at_time)) {
    thread->yield_at_time = UINT64_MAX; // in case someone passes a dubious number for preemption_us
  }

  if (arrlen(impl_info->call_queue) == 0) {
    // idk why this happens
    return;
  }

  pending_call *call = &impl_info->call_queue[0];
  future_t fut = call_interpreter(&call->call);

  if (fut.status == FUTURE_READY) {
    execution_record *rec = call->record;
    rec->status = SCHEDULER_RESULT_DONE;
    rec->returned = call->call._result;

    if (call->call.args.method->descriptor->return_type.repr_kind == TYPE_KIND_REFERENCE && rec->returned.obj) {
      // Create a handle
      rec->js_handle = make_js_handle(vm, rec->returned.obj);
    } else {
      rec->js_handle = -1; // no handle here
    }

    arrdel(impl_info->call_queue, 0);
    free(call->call.args.args); // free the copied arguments
  }

  // otherwise, we need to save the future
  impl_info->wakeup_info = (void *)fut.wakeup;
}

void scheduler_push_execution_record(rr_scheduler *scheduler, scheduler_polled_info_t info) {
  if (unlikely(!info.thread_info)) {
    return;
  }

  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?

  thread_info *impl_info = (thread_info *)info.thread_info;

  impl_info->is_running = false;
  if (arrlen(impl_info->call_queue) == 0) {
    // Look for the thread in the round robin and remove it
    for (int i = 0; i < arrlen(I->round_robin); i++) {
      if (I->round_robin[i] == impl_info) {
        arrdel(I->round_robin, i);
        break;
      }
    }
    free_thread_info(scheduler, impl_info);
  }

  pthread_mutex_unlock(&I->mutex); // todo: check result?
}

// basically just the two methods pasted together into one locking function
scheduler_polled_info_t scheduler_push_execution_record_and_repoll(rr_scheduler *scheduler, scheduler_polled_info_t info) {
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?

  thread_info *impl_info = (thread_info *)info.thread_info;

  // push
  if (likely(impl_info)) {
    impl_info->is_running = false;
    if (arrlen(impl_info->call_queue) == 0) {
      // Look for the thread in the round robin and remove it
      for (int i = 0; i < arrlen(I->round_robin); i++) {
        if (I->round_robin[i] == impl_info) {
          arrdel(I->round_robin, i);
          break;
        }
      }
      free_thread_info(scheduler, impl_info);
    }
  }

  // poll
  if (arrlen(I->round_robin) == 0 || only_daemons_running(I->round_robin)) {
    pthread_mutex_unlock(&I->mutex); // todo: check result?
    return SCHEDULER_POLLED_DONE;
  }

  scheduler_polled_info_t result = { SCHEDULER_RESULT_MORE, nullptr, 0 };
  impl_info = get_next_thr(I);
  if (!impl_info) {
    // returned nullptr; no threads are available to run
    result.may_sleep_us = rr_scheduler_may_sleep_us(scheduler);
  } else {
    // else, return it
    result.thread_info = impl_info;
    impl_info->is_running = true;
  }

  pthread_mutex_unlock(&I->mutex); // todo: check result?
  return result;
}

static u64 rr_scheduler_may_sleep_us(rr_scheduler *scheduler) {
  u64 min = UINT64_MAX;
  impl *I = scheduler->_impl;
  u64 time = get_unix_us();

  // Check all infos for wakeup times
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    thread_info *info = I->round_robin[i];

    if (arrlen(info->call_queue) > 0) {
      if (is_sleeping(info, time)) {
        u64 wakeup_time = info->wakeup_info->wakeup_us;
        //        if (wakeup_time == 0) {
        //          wakeup_time = time + 10 * 1000000; // 10 seconds instead of infinite sleep just to be safe
        //        }

        if (wakeup_time != 0 && wakeup_time < min) {
          min = wakeup_time;
        }
      } else {
        return 0; // at least one thing is waiting, and not sleeping
      }
    }
  }

  return min > time ? min - time : 0;
}

// only call this with the lock held
static bool only_daemons_running(thread_info **thread_info) {
  // Use thread_is_daemon to check
  for (int i = 0; i < arrlen(thread_info); i++) {
    if (!thread_is_daemon(thread_info[i]->thread)) {
      return false;
    }
  }
  return true;
}

static thread_info *get_or_create_thread_info(impl *impl, vm_thread *thread) {
  for (int i = 0; i < arrlen(impl->round_robin); i++) {
    if (impl->round_robin[i]->thread == thread) {
      return impl->round_robin[i];
    }
  }

  thread_info *info = calloc(1, sizeof(thread_info));
  info->thread = thread;
  arrput(impl->round_robin, info);
  return info;
}

scheduler_status_t rr_scheduler_execute_immediately(execution_record *record) {
  // Find the thread in question and synchronously execute all pending calls up to this record
  rr_scheduler *scheduler = record->vm->scheduler;
  impl *I = scheduler->_impl;

  // todo: i'm basically just putting a lock around this... this is very bad but it should technically work
  pthread_mutex_lock(&I->mutex); // todo: check result?

  for (int i = 0; i < arrlen(I->round_robin); ++i) {
    if (I->round_robin[i]->thread == record->thread) {
      thread_info *info = I->round_robin[i];
      while (arrlen(info->call_queue) > 0) {
        pending_call *call = &info->call_queue[0];
        info->thread->stack.synchronous_depth++; // force it to be synchronous
        future_t fut = call_interpreter(&call->call);
        info->thread->stack.synchronous_depth--;

        if (fut.status == FUTURE_NOT_READY) {
          // Raise IllegalStateException
          raise_vm_exception(record->thread, STR("java/lang/IllegalStateException"),
                             STR("Cannot synchronously execute this method"));
          pthread_mutex_unlock(&I->mutex); // todo: check result?
          return SCHEDULER_RESULT_INVAL;
        }

        arrdel(info->call_queue, 0);
        if (call->record == record) {
          pthread_mutex_unlock(&I->mutex); // todo: check result?
          return SCHEDULER_RESULT_DONE;
        }
      }

      break;
    }
  }

  pthread_mutex_unlock(&I->mutex); // todo: check result?
  return SCHEDULER_RESULT_DONE;
}

static bool is_nth_arg_reference(cp_method *method, int i) {
  if (method->access_flags & ACCESS_STATIC) {
    return method->descriptor->args[i].repr_kind == TYPE_KIND_REFERENCE;
  }
  return i == 0 || method->descriptor->args[i - 1].repr_kind == TYPE_KIND_REFERENCE;
}

void rr_scheduler_enumerate_gc_roots(rr_scheduler *scheduler, object **stbds_vector) {
  // Iterate through all pending_calls and add object arguments as roots
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?

  for (int i = 0; i < arrlen(I->round_robin); i++) {
    thread_info *info = I->round_robin[i];
    for (int j = 0; j < arrlen(info->call_queue); j++) {
      pending_call *call = &info->call_queue[j];
      for (int k = 0; k < method_argc(call->call.args.method); k++) {
        if (is_nth_arg_reference(call->call.args.method, k)) {
          arrput(stbds_vector, &call->call.args.args[k].obj);
        }
      }
    }
  }

  pthread_mutex_unlock(&I->mutex); // todo: check result?
}

// todo: how to synchronize this? just obtain the lock?
execution_record *rr_scheduler_run(rr_scheduler *scheduler, call_interpreter_t call) {
  impl *I = scheduler->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?

  vm_thread *thread = call.args.thread;
  thread_info *info = get_or_create_thread_info(scheduler->_impl, thread);

  // Copy the arguments object
  stack_value *args_copy = calloc(method_argc(call.args.method), sizeof(stack_value));
  memcpy(args_copy, call.args.args, sizeof(stack_value) * method_argc(call.args.method));
  call.args.args = args_copy;

  pending_call pending = {.call = call, .record = calloc(1, sizeof(execution_record))};
  execution_record *rec = pending.record;
  rec->status = SCHEDULER_RESULT_MORE;
  rec->vm = scheduler->vm;
  rec->thread = thread;
  rec->_impl = scheduler->_impl;

  arrput(info->call_queue, pending);
  arrput(((impl *)scheduler->_impl)->executions, rec);

  pthread_mutex_unlock(&I->mutex); // todo: check result?
  return pending.record;
}

/// the raw operation which assumes the lock is held already
static void free_execution_record_impl(execution_record *record) {
  impl *I = record->_impl;
  if (record->js_handle != -1) {
    drop_js_handle(record->vm, record->js_handle);
  }
  for (int i = 0; i < arrlen(I->executions); i++) {
    if (I->executions[i] == record) {
      arrdelswap(I->executions, i);
      break;
    }
  }
  free(record);
}

// todo: how to synchronize this? just obtain the lock?
// todo: this is never getting called except for specific instances in the shutdown sequence...
// todo: this api very susceptible to uaf/memory leaks ^^ (rn finished records are never freed)
void free_execution_record(execution_record *record) {
  impl *I = record->_impl;
  pthread_mutex_lock(&I->mutex); // todo: check result?
  free_execution_record_impl(record);
  pthread_mutex_unlock(&I->mutex); // todo: check result?
}