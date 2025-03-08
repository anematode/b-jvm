//
// Created by Max Cai on 3/6/25.
//

#ifndef BJVM_WORKER_THREADS_H
#define BJVM_WORKER_THREADS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "bjvm.h"

#include <pthread.h>

typedef struct {
  pthread_mutex_t mutex;
  pthread_cond_t cond;
  u32 num_registered; // guarded by mutex; count of all threads which are expected to do work
  u32 num_arrived;    // guarded by mutex; needs to be equal to block for gc
} worker_thread_pool;

void worker_thread_pool_init(worker_thread_pool *pool);
void worker_thread_pool_uninit(worker_thread_pool *pool);

// todo: functions for adding or removing threads to this pool
void worker_thread_pool_register(worker_thread_pool *pool);

int thread_pool_lock(worker_thread_pool *pool);
int thread_pool_unlock(worker_thread_pool *pool);

// must only be called while holding the lock
int arrive_await_all_suspended(worker_thread_pool *pool);       // arrives and waits for all threads to arrive
int arrive_await_gc_finished(worker_thread_pool *pool, vm *vm); // arrives and waits until the gc is done, decrements
int reset_notify_gc_finished(worker_thread_pool *pool, vm *vm); // marks gc done, decrements, and notifies

#ifdef __cplusplus
}
#endif

#endif // BJVM_WORKER_THREADS_H
