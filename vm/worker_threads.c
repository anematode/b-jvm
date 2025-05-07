//
// Created by Max Cai on 3/6/25.
//

#include "worker_threads.h"

void worker_thread_pool_init(worker_thread_pool *pool) {
  pool->num_registered = 0;
  pool->num_arrived = 0;

  pthread_mutex_init(&pool->mutex, nullptr); // todo: check return values?
  pthread_cond_init(&pool->cond, nullptr);
}

void worker_thread_pool_uninit(worker_thread_pool *pool) {
  pthread_mutex_destroy(&pool->mutex); // todo: check return values?
  pthread_cond_destroy(&pool->cond);
}

void worker_thread_pool_register(worker_thread_pool *pool) { // todo: should this do anything else?
  pthread_mutex_lock(&pool->mutex); // todo: check return values?
  pool->num_registered++;
  pthread_mutex_unlock(&pool->mutex);
}

void worker_thread_pool_deregister(worker_thread_pool *pool) { // todo: should this do anything else?
  pthread_mutex_lock(&pool->mutex); // todo: check return values?
  pool->num_registered--;
  pthread_cond_broadcast(&pool->cond); // in case someone was waiting for this to GC
  pthread_mutex_unlock(&pool->mutex);
}

int thread_pool_lock(worker_thread_pool *pool) {
  return pthread_mutex_lock(&pool->mutex);
}

int thread_pool_unlock(worker_thread_pool *pool) {
  return pthread_mutex_unlock(&pool->mutex);
}

int arrive_await_all_suspended(worker_thread_pool *pool) {
  pool->num_arrived++;
  pthread_cond_broadcast(&pool->cond); // technically unnecessary, but can't hurt
  while (pool->num_arrived < pool->num_registered) {
    int err = pthread_cond_wait(&pool->cond, &pool->mutex);
    if (err) return err;
  }
  return 0;
}

int arrive_await_gc_finished(worker_thread_pool *pool, vm *vm) {
  pool->num_arrived++;
  pthread_cond_broadcast(&pool->cond);
  while (should_gc_pause(vm)) {
    int err = pthread_cond_wait(&pool->cond, &pool->mutex);
    if (err) return err;
  }
  pool->num_arrived--;
  return 0;
}
int reset_notify_gc_finished(worker_thread_pool *pool, vm *vm) {
  pool->num_arrived--;
  __atomic_store_n(&vm->should_gc_pause, false, __ATOMIC_RELEASE);
  pthread_cond_broadcast(&pool->cond);
  return 0;
}