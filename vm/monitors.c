//
// Created by Max Cai on 2/7/25.
//

#include "monitors.h"

#define NOT_HELD_TID (-1)

DEFINE_ASYNC(monitor_acquire) {
  // since this is a single-threaded vm, we don't need atomic operations
  self->handle = make_handle(args->thread, args->obj);
  monitor_data *allocated_data = nullptr; // lazily allocated

  header_word volatile *shared_header = &self->handle->obj->header_word;
  assert((uintptr_t)shared_header % 8 == 0); // should be aligned due to how bump_allocate works

  header_word fetched_header;

  __atomic_load(shared_header, &fetched_header, __ATOMIC_ACQUIRE);
  monitor_data *data;
  for (;;) { // loop until a monitor data is initialized
    data = inspect_monitor(&fetched_header);
    if (likely(data)) {
      break; // the monitor exists
    }
    // we need to allocate one ourselves
    if (!allocated_data)
      allocated_data = allocate_monitor(args->thread);
    if (unlikely(!allocated_data)) { // oom
      drop_handle(args->thread, self->handle);
      out_of_memory(args->thread);
      ASYNC_RETURN(-1);
    }

    allocated_data->mark_word = fetched_header.mark_word;
    allocated_data->tid = NOT_HELD_TID; // not owned (as a microöptimisation, we could claim it here beforehand)
    allocated_data->hold_count = 0;

    header_word proposed_header = {.expanded_data = allocated_data};

    // try to put it in- loop again if CAS fails
    if (__atomic_compare_exchange(shared_header, &fetched_header, &proposed_header, false, __ATOMIC_ACQ_REL,
                                  __ATOMIC_ACQUIRE)) {
      break; // success
    }
  }

  // now, a monitor is guaranteed to exist
  for (;;) {
    monitor_data *lock =
        __atomic_load_n((monitor_data **)&self->handle->obj->header_word, __ATOMIC_ACQUIRE); // must refetch
    assert(lock);
    s32 read_tid = NOT_HELD_TID;

    // try to acquire mutex- loop again if CAS fails
    if (__atomic_compare_exchange_n(&lock->tid, &read_tid, args->thread->tid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      lock->hold_count = 1;
      break; // success
    }

    if (read_tid == args->thread->tid) {
      // we already own the monitor
      lock->hold_count++; // I think (?) this works under the C memory model, but if not, just use volatile
      break;
    }

    // since the strong CAS failed, we need to wait on the read_tid
    self->wakeup_info.kind = RR_WAKEUP_YIELDING;
    ASYNC_YIELD((void *)&self->wakeup_info);
  }

  printf("(tid %d = %p) acquiring hold count %d\n", args->thread->tid, args->thread, __atomic_load_n((monitor_data **)&self->handle->obj->header_word, __ATOMIC_ACQUIRE)->hold_count);

  // done acquiring the monitor
  drop_handle(args->thread, self->handle);
  ASYNC_END(0);
}

DEFINE_ASYNC(monitor_reacquire_hold_count) {
  // since this is a single-threaded vm, we don't need atomic operations
  self->handle = make_handle(args->thread, args->obj);

  // just sanity check, can remove
  header_word volatile *shared_header = &self->handle->obj->header_word;
  assert((uintptr_t)shared_header % 8 == 0); // should be aligned due to how bump_allocate works
  header_word fetched_header;
  __atomic_load(shared_header, &fetched_header, __ATOMIC_ACQUIRE);
  assert(inspect_monitor(&fetched_header) != nullptr);

  printf("trying to reacquire hold count %d\n", args->hold_count);

  // a monitor should be guaranteed to exist
  for (;;) {
    monitor_data *lock =
        __atomic_load_n((monitor_data **)&self->handle->obj->header_word, __ATOMIC_ACQUIRE); // must refetch
    assert(lock);
    s32 read_tid = NOT_HELD_TID;

    // try to acquire mutex- loop again if CAS fails
    // todo: are these memory semantics even correct
    if (__atomic_compare_exchange_n(&lock->tid, &read_tid, args->thread->tid, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
      lock->hold_count = args->hold_count;
      break; // success
    }

    if (read_tid == args->thread->tid) {
      // we already own the monitor, this should be illegal
      drop_handle(args->thread, self->handle);
      ASYNC_RETURN(-1);
    }

    // since the strong CAS failed, we need to wait on the read_tid
    self->wakeup_info.kind = RR_WAKEUP_YIELDING;
    ASYNC_YIELD((void *)&self->wakeup_info);
  }

  // done acquiring the monitor
  drop_handle(args->thread, self->handle);
  ASYNC_END(0);
}

u32 current_thread_hold_count(vm_thread *thread, obj_header *obj) {
  // no handles necessary because no GC (i hope)
  header_word fetched_header;
  __atomic_load(&obj->header_word, &fetched_header, __ATOMIC_ACQUIRE);
  monitor_data *lock = inspect_monitor(&fetched_header);
  if (unlikely(!lock))
    return 0;

  s32 tid = __atomic_load_n(&lock->tid, __ATOMIC_ACQUIRE);
  if (tid != thread->tid)
    return 0;
  return lock->hold_count;
}

u32 monitor_release_all_hold_count(vm_thread *thread, obj_header *obj) {
  // no handles necessary because no GC (i hope)
  header_word fetched_header;
  __atomic_load(&obj->header_word, &fetched_header, __ATOMIC_ACQUIRE);
  monitor_data *lock = inspect_monitor(&fetched_header);

  // todo: error code enum? or just always cause an InternalError/IllegalMonitorStateException
  s32 tid = __atomic_load_n(&lock->tid, __ATOMIC_ACQUIRE);
  if (unlikely(!lock))
    return 0;
  if (unlikely(tid != thread->tid))
    return 0;
  if (unlikely(lock->hold_count == 0))
    return 0;

  u32 hold_count = lock->hold_count;
  lock->hold_count = 0;
  __atomic_store_n(&lock->tid, NOT_HELD_TID, __ATOMIC_RELEASE);
  return hold_count;
}

int monitor_release(vm_thread *thread, obj_header *obj) {
  // since this is a single-threaded vm, we don't need atomic operations
  // no handles necessary because no GC (i hope)
  header_word fetched_header;
  __atomic_load(&obj->header_word, &fetched_header, __ATOMIC_ACQUIRE);
  monitor_data *lock = inspect_monitor(&fetched_header);

  printf("(tid %d = %p) releasing hold count %d\n", thread->tid, thread, lock->hold_count);

  // todo: error code enum? or just always cause an InternalError/IllegalMonitorStateException
  s32 tid = __atomic_load_n(&lock->tid, __ATOMIC_ACQUIRE);
  if (unlikely(!lock))
    return -1;
  if (unlikely(tid != thread->tid))
    return -1;
  if (unlikely(lock->hold_count == 0))
    return -1;

  u32 new_hold_count = --lock->hold_count;

  if (new_hold_count == 0) {
    __atomic_store_n(&lock->tid, NOT_HELD_TID, __ATOMIC_RELEASE);
  }
  return 0;
}
