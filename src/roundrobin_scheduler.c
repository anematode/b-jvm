//
// Created by Cowpox on 2/2/25.
//

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
} impl;

void rr_scheduler_init(rr_scheduler *scheduler, bjvm_vm *vm){
  scheduler->vm = vm;
  scheduler->_impl = calloc(1, sizeof(impl));
}

void rr_scheduler_uninit(rr_scheduler *scheduler){
  free(scheduler->_impl);
}

static bool is_sleeping(thread_info * info) {
  return info->wakeup_info && info->wakeup_info->kind == RR_WAKEUP_SLEEP;
}

static bool is_waiting_on_monitor(thread_info *info) {
  if (info->wakeup_info && info->wakeup_info->kind == RR_WAKEUP_MONITORENTER) {
    bjvm_thread *thread = info->thread;
    bjvm_obj_header *obj = bjvm_deref_js_handle(thread->vm, info->wakeup_info->monitored_object);
    lock_record *lock = inspect_lock(obj);
    return lock;
  }
  return false;
}

static thread_info * get_next_thr(impl *impl) {
  assert(impl->round_robin && "No threads to run");
  thread_info *info = impl->round_robin[0], *first = info;
  do {
    memmove(impl->round_robin, impl->round_robin + 1, sizeof(thread_info *) * (arrlen(impl->round_robin) - 1));
    impl->round_robin[arrlen(impl->round_robin) - 1] = info;
    if (!is_sleeping(info) && !is_waiting_on_monitor(info)) {
      return info;
    }
  } while (info != first);

  // all are sleeping or waiting for a monitor to exit, find the one with the minimum sleep time
  int best_i = 0;
  u64 closest = UINT64_MAX;
  for (int i = 0; i < arrlen(impl->round_robin); i++) {
    thread_info *cand = impl->round_robin[i];
    if (is_sleeping(cand) && cand->wakeup_info->wakeup_us < closest) {
      info = cand;
      best_i = i;
      closest = info->wakeup_info->wakeup_us;
    }
  }

  memmove(impl->round_robin + best_i, impl->round_robin + best_i + 1, sizeof(thread_info *) * (arrlen(impl->round_robin) - 1 - best_i));
  impl->round_robin[arrlen(impl->round_robin) - 1] = info;
  return info;
}


u64 get_unix_us(void) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  u64 time = tv.tv_sec * 1000000 + tv.tv_usec;
  return time;
}

u64 rr_scheduler_may_sleep_us(rr_scheduler *scheduler) {
  s64 min = INT64_MAX;
  impl *I = scheduler->_impl;
  // Check all infos for wakeup times
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    thread_info *info = I->round_robin[i];
    if (arrlen(info->call_queue) > 0) {
      if (is_sleeping(info)) {
        if ((s64)info->wakeup_info->wakeup_us < min) {
          min = (s64)info->wakeup_info->wakeup_us;
        }
      } else if (is_waiting_on_monitor(info)) {
        // exclude from consideration
      } else {
        return 0; // at least one thing is waiting, and not sleeping
      }
    }
  }
  min -= (s64)get_unix_us();
  return min >= 0 ? min : 0;
}

void free_wakeup(bjvm_thread * thread, rr_wakeup_info * wakeup_info) {
  if (!wakeup_info)
    return;
  if (wakeup_info->kind == RR_WAKEUP_MONITORENTER) {
    bjvm_drop_js_handle(thread->vm, wakeup_info->monitored_object);
  }
  free(wakeup_info);
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
  if (is_sleeping(info)) {
    if (time < info->wakeup_info->wakeup_us) {
      return SCHEDULER_RESULT_MORE;
    }
  }

  thread->yield_at_time = time + MICROSECONDS_TO_RUN;

  if (arrlen(info->call_queue) == 0) {
    arrpop(impl->round_robin);
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

    memmove(info->call_queue, info->call_queue + 1, sizeof(pending_call) * (arrlen(info->call_queue) - 1));
    arrsetlen(info->call_queue, arrlen(info->call_queue) - 1);

    if (arrlen(info->call_queue) == 0) {
      arrpop(impl->round_robin);
    }
  } else {
    free_wakeup(thread, info->wakeup_info);
    info->wakeup_info = (void*)fut.wakeup;
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

        memmove(info->call_queue, info->call_queue + 1, sizeof(pending_call) * (arrlen(info->call_queue) - 1));
        arrsetlen(info->call_queue, arrlen(info->call_queue) - 1);

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

  arrput(info->call_queue, pending);
  return pending.record;
}

void free_execution_record(execution_record *record) {
  if (record->js_handle != -1) {
    bjvm_drop_js_handle(record->vm, record->js_handle);
  }
  free(record);
}