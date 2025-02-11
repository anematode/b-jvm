// TODO better memory management. It's horrible rn

#include "roundrobin_scheduler.h"

#include "exceptions.h"

typedef struct {
  call_interpreter_t call;
  execution_record *record;
} pending_call;

typedef struct {
  bjvm_thread *thread;
  // pending calls for this thread (processed in order)
  pending_call *call_queue;
  rr_wakeup_info *wakeup_info;
} thread_info;

typedef struct {
  thread_info **round_robin; // Threads are cycled here
  execution_record **executions;
} impl;

void free_thread_info(thread_info * info) {
  arrfree(info->call_queue);
  free(info);
}

void rr_scheduler_init(rr_scheduler *scheduler, bjvm_vm *vm) {
  scheduler->vm = vm;
  scheduler->_impl = calloc(1, sizeof(impl));
}

void rr_scheduler_uninit(rr_scheduler *scheduler) {
  impl *I = scheduler->_impl;
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    free_thread_info(I->round_robin[i]);
  }
  for (int i = 0; i < arrlen(I->executions); i++) {
    free(I->executions[i]);
  }
  arrfree(I->executions);
  arrfree(I->round_robin);
  free(I);
}

static bool is_sleeping(thread_info * info, s64 time) {
  return info->wakeup_info && info->wakeup_info->kind == RR_WAKEUP_SLEEP && (s64)info->wakeup_info->wakeup_us > time;
}

u64 get_unix_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  u64 time = tv.tv_sec * 1000000 + tv.tv_usec;
  return time;
}

static thread_info * get_next_thr(impl *impl) {
  assert(impl->round_robin && "No threads to run");
  thread_info *info = impl->round_robin[0], *first = info;
  s64 time = (s64)get_unix_us();
  do {
    info = impl->round_robin[0];
    arrdel(impl->round_robin, 0);
    arrput(impl->round_robin, info);
    if (!is_sleeping(info, time)) {
      return info;
    }
  } while (info != first);

  // all are sleeping, find the one with the minimum sleep time
  int best_i = 0;
  u64 closest = UINT64_MAX;
  for (int i = 0; i < arrlen(impl->round_robin); i++) {
    if (impl->round_robin[i]->wakeup_info->wakeup_us < closest) {
      info = impl->round_robin[i];
      best_i = i;
      closest = info->wakeup_info->wakeup_us;
    }
  }

  thread_info *best = impl->round_robin[best_i];
  arrdel(impl->round_robin, best_i);
  arrput(impl->round_robin, best);
  return info;
}

u64 rr_scheduler_may_sleep_us(rr_scheduler *scheduler) {
  s64 min = INT64_MAX;
  impl *I = scheduler->_impl;
  s64 time = (s64)get_unix_us();

  // Check all infos for wakeup times
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    thread_info *info = I->round_robin[i];

    if (arrlen(info->call_queue) > 0) {
      if (is_sleeping(info, time)) {
        if ((s64)info->wakeup_info->wakeup_us < min) {
          min = (s64)info->wakeup_info->wakeup_us;
        }
      } else {
        return 0; // at least one thing is waiting, and not sleeping
      }
    }
  }
  min -= time;
  return min >= 0 ? min : 0;
}

void unshift(impl* I, thread_info * info) {
  if (arrlen(info->call_queue) > 0) {
    arrdel(info->call_queue, 0);
  }
  if (arrlen(info->call_queue) == 0) {
    // Look for the thread in the round robin and remove it
    for (int i = 0; i < arrlen(I->round_robin); i++) {
      if (I->round_robin[i] == info) {
        arrdel(I->round_robin, i);
        break;
      }
    }
    free_thread_info(info);
  }
}

scheduler_status_t rr_scheduler_step(rr_scheduler *scheduler) {
  impl *impl = scheduler->_impl;

  if (arrlen(impl->round_robin) == 0)
    return SCHEDULER_RESULT_DONE;

  u64 time = get_unix_us();
  thread_info *info = get_next_thr(impl);
  if (!info)
    return SCHEDULER_RESULT_DONE;

  bjvm_thread *thread = info->thread;
  const int MICROSECONDS_TO_RUN = 30000;

  thread->fuel = 50000;

  // If the thread is sleeping, check if it's time to wake up
  if (is_sleeping(info, (s64)time)) {
    if (time < info->wakeup_info->wakeup_us) {
      return SCHEDULER_RESULT_MORE;
    }
  }

  thread->yield_at_time = time + MICROSECONDS_TO_RUN;

  if (arrlen(info->call_queue) == 0) {
    (void)arrpop(impl->round_robin);
    return rr_scheduler_step(scheduler);
  }

  pending_call *call = &info->call_queue[0];
  future_t fut = call_interpreter(&call->call);

  if (fut.status == FUTURE_READY) {
    execution_record *rec = call->record;
    rec->status = SCHEDULER_RESULT_DONE;
    rec->returned = call->call._result;

    if (field_to_kind(&call->call.args.method->descriptor->return_type) == BJVM_TYPE_KIND_REFERENCE &&
      rec->returned.obj) {
      // Create a handle
      rec->js_handle = bjvm_make_js_handle(scheduler->vm, rec->returned.obj);
    } else {
      rec->js_handle = -1;  // no handle here
    }

    free(call->call.args.args);  // free the copied arguments

    unshift(impl, info);
  } else {
    info->wakeup_info = (void*) fut.wakeup;
  }

  return arrlen(impl->round_robin) ? SCHEDULER_RESULT_MORE : SCHEDULER_RESULT_DONE;
}

static thread_info *get_or_create_thread_info(impl *impl, bjvm_thread *thread) {
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

  for (int i = 0; i < arrlen(I->round_robin); ++i) {
    if (I->round_robin[i]->thread == record->thread) {
      thread_info *info = I->round_robin[i];
      while (arrlen(info->call_queue) > 0) {
        pending_call *call = &info->call_queue[0];
        info->thread->synchronous_depth++;  // force it to be synchronous
        future_t fut = call_interpreter(&call->call);
        info->thread->synchronous_depth--;

        if (fut.status == FUTURE_NOT_READY) {
          // Raise IllegalStateException
          bjvm_raise_vm_exception(record->thread, STR("java/lang/IllegalStateException"),
            STR("Cannot synchronously execute this method"));
          return SCHEDULER_RESULT_INVAL;
        }

        arrdel(info->call_queue, 0);
        if (call->record == record) {
          return SCHEDULER_RESULT_DONE;
        }
      }

      break;
    }
  }

  return SCHEDULER_RESULT_DONE;
}

execution_record *rr_scheduler_run(rr_scheduler *scheduler, call_interpreter_t call) {
  bjvm_thread *thread = call.args.thread;
  thread_info *info = get_or_create_thread_info(scheduler->_impl, thread);

  // Copy the arguments object
  bjvm_stack_value *args_copy = calloc(bjvm_argc(call.args.method), sizeof(bjvm_stack_value));
  memcpy(args_copy, call.args.args, sizeof(bjvm_stack_value) * bjvm_argc(call.args.method));
  call.args.args = args_copy;

  pending_call pending = {.call = call, .record = calloc(1, sizeof(execution_record))};
  execution_record *rec = pending.record;
  rec->status = SCHEDULER_RESULT_MORE;
  rec->vm = scheduler->vm;
  rec->thread = thread;
  rec->_impl = scheduler->_impl;

  arrput(info->call_queue, pending);
  arrput(((impl*)scheduler->_impl)->executions, rec);
  return pending.record;
}

void free_execution_record(execution_record *record) {
  if (record->js_handle != -1) {
    bjvm_drop_js_handle(record->vm, record->js_handle);
  }
  impl *I = record->_impl;
  for (int i = 0; i < arrlen(I->executions); i++) {
    if (I->executions[i] == record) {
      arrdelswap(I->executions, i);
      break;
    }
  }
  free(record);
}