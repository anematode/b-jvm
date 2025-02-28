// TODO better memory management. It's horrible rn

#include "roundrobin_scheduler.h"

#include "exceptions.h"

typedef struct {
  call_interpreter_t call;
  execution_record *record;
} pending_call;

typedef struct {
  vm_thread *thread;
  // pending calls for this thread (processed in order)
  pending_call *call_queue;
  rr_wakeup_info *wakeup_info;
} thread_info;

typedef struct {
  thread_info **round_robin; // Threads are cycled here
  execution_record **executions;
} impl;

void monitor_notify_one(rr_scheduler *scheduler, obj_header *monitor) {
  // iterate through the threads and find one that is waiting on the monitor
  impl *I = scheduler->_impl;
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    rr_wakeup_info *wakeup_info = I->round_robin[i]->wakeup_info;
    if (!wakeup_info)
      continue;
    if (wakeup_info->kind == RR_MONITOR_WAIT && wakeup_info->monitor_wakeup.monitor->obj == monitor) {
      wakeup_info->monitor_wakeup.ready = true;
      return;
    }
  }
}

void monitor_notify_all(rr_scheduler *scheduler, obj_header *monitor) {
  // iterate through the threads and find all that are waiting on the monitor
  impl *I = scheduler->_impl;
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    rr_wakeup_info *wakeup_info = I->round_robin[i]->wakeup_info;
    if (!wakeup_info)
      continue;
    if (wakeup_info->kind == RR_MONITOR_WAIT && wakeup_info->monitor_wakeup.monitor->obj == monitor) {
      wakeup_info->monitor_wakeup.ready = true;
    }
  }
}

void monitor_exit_handler(rr_scheduler *scheduler, obj_header *monitor) {
  // iterate through the threads and find all that are waiting to enter
  impl *I = scheduler->_impl;
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    rr_wakeup_info *wakeup_info = I->round_robin[i]->wakeup_info;
    if (!wakeup_info)
      continue;
    if (wakeup_info->kind == RR_MONITOR_ENTER_WAITING && wakeup_info->monitor_wakeup.monitor->obj == monitor) {
      wakeup_info->monitor_wakeup.ready = true;
      return; // we only need to notify one waiter at most (but it probably wouldn't hurt either way)
    }
  }
}

void free_thread_info(rr_scheduler *scheduler, thread_info *info) {
  // technically doesn't need to acquire the monitor to notify, since scheduler is god
  info->thread->thread_obj->eetop = 0; // set the eetop to nullptr
  monitor_notify_all(scheduler, (obj_header *) info->thread->thread_obj);

  arrfree(info->call_queue);
  free(info);
}

void rr_scheduler_init(rr_scheduler *scheduler, vm *vm) {
  scheduler->vm = vm;
  scheduler->_impl = calloc(1, sizeof(impl));
}

void rr_scheduler_uninit(rr_scheduler *scheduler) {
  impl *I = scheduler->_impl;
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    free_thread_info(scheduler, I->round_robin[i]);
  }
  for (int i = 0; i < arrlen(I->executions); i++) {
    free(I->executions[i]);
  }
  arrfree(I->executions);
  arrfree(I->round_robin);
  free(I);
}

static bool is_sleeping(thread_info *info, u64 time) {
  if (!info->wakeup_info)
    return false;
  if (info->wakeup_info->kind == RR_WAKEUP_SLEEP
      || (info->wakeup_info->kind == RR_THREAD_PARK && !query_unpark_permit(info->thread))
      || (info->wakeup_info->kind == RR_MONITOR_WAIT && !info->wakeup_info->monitor_wakeup.ready)
      || (info->wakeup_info->kind == RR_MONITOR_ENTER_WAITING && !info->wakeup_info->monitor_wakeup.ready)) {
    u64 wakeup = info->wakeup_info->wakeup_us;
    // montitor enter is non-interruptible by Java language spec
    bool interrupted = info->thread->thread_obj->interrupted && info->wakeup_info->kind != RR_MONITOR_ENTER_WAITING;
    return !interrupted && (wakeup == 0 || wakeup >= time);
  } else {
    return false; // blocking on something else which presumably can resume soon
  }
}

static thread_info *get_next_thr(impl *impl) {
  assert(impl->round_robin && "No threads to run");
  thread_info *info = nullptr;
  u64 time = get_unix_us();
  for (int i = 0; i < arrlen(impl->round_robin); ++i) {
    info = impl->round_robin[0];
    arrdel(impl->round_robin, 0);
    arrput(impl->round_robin, info);
    if (!is_sleeping(info, time)) {
      return info;
    }
  }

  // all are sleeping, find the one with the minimum sleep time
  int best_i = 0;
  u64 closest = UINT64_MAX;
  for (int i = 0; i < arrlen(impl->round_robin); i++) {
    rr_wakeup_info *wakeup = impl->round_robin[i]->wakeup_info;
    if (wakeup->wakeup_us < closest) {
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
  u64 min = UINT64_MAX;
  impl *I = scheduler->_impl;
  u64 time = get_unix_us();

  // Check all infos for wakeup times
  for (int i = 0; i < arrlen(I->round_robin); i++) {
    thread_info *info = I->round_robin[i];

    if (arrlen(info->call_queue) > 0) {
      if (is_sleeping(info, time)) {
        u64 wakeup_time = info->wakeup_info->wakeup_us;
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

void unshift(rr_scheduler *scheduler, impl *I, thread_info *info) {
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
    free_thread_info(scheduler, info);
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

  vm_thread *thread = info->thread;
  const int MICROSECONDS_TO_RUN = 30000;

  thread->fuel = 50000;

  // If the thread is sleeping, check if it's time to wake up
  if (is_sleeping(info, time)) {
    if (time < info->wakeup_info->wakeup_us) {
      return SCHEDULER_RESULT_MORE;
    }
  }

  // else, we start calling it
  info->wakeup_info = nullptr;

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

    if (field_to_kind(&call->call.args.method->descriptor->return_type) == TYPE_KIND_REFERENCE && rec->returned.obj) {
      // Create a handle
      rec->js_handle = make_js_handle(scheduler->vm, rec->returned.obj);
    } else {
      rec->js_handle = -1; // no handle here
    }

    free(call->call.args.args); // free the copied arguments
    unshift(scheduler, impl, info);
  } else {
    info->wakeup_info = (void *)fut.wakeup;
  }

  return arrlen(impl->round_robin) ? SCHEDULER_RESULT_MORE : SCHEDULER_RESULT_DONE;
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

bool is_nth_arg_reference(cp_method * method, int i) {
  if (method->access_flags & ACCESS_STATIC) {
    return field_to_kind(&method->descriptor->args[i]) == TYPE_KIND_REFERENCE;
  }
  return i == 0 || field_to_kind(&method->descriptor->args[i - 1]) == TYPE_KIND_REFERENCE;
}

void rr_scheduler_enumerate_gc_roots(rr_scheduler *scheduler, object **stbds_vector) {
  // Iterate through all pending_calls and add object arguments as roots
  impl *I = scheduler->_impl;
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
}

execution_record *rr_scheduler_run(rr_scheduler *scheduler, call_interpreter_t call) {
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
  return pending.record;
}

void free_execution_record(execution_record *record) {
  if (record->js_handle != -1) {
    drop_js_handle(record->vm, record->js_handle);
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