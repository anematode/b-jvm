#include <profiler.h>
#include <stdatomic.h>
#include <unistd.h>

#if !defined(EMSCRIPTEN) || defined(__EMSCRIPTEN_PTHREADS__)
#define PTHREADS_SUPPORTED
#endif

#ifdef PTHREADS_SUPPORTED
#include <pthread.h>
#endif

typedef struct profiler_s {
  vm_thread *thread;

  volatile int waiting_for_exit;
  string_hash_table counts;

  int samples;

  char * volatile result;
  volatile bool initialized;
  volatile bool finished;

#ifdef PTHREADS_SUPPORTED
  pthread_t pthread;
#endif
} profiler;

// The profiler continuously scans the thread->frames and records the methods that are currently being executed.
// The entries are stored in a hash map with the stack frames as the key. The key is a string that is the concatenation
// of all the POINTERS to the methods. (4 bytes on 32-bit, 8 bytes on 64-bit.) We count up samples from 0.
//
// This is very dumb, but we don't rly care. Invalid stack frames are removed in a fixup pass.

void profiler_main(profiler *prof) {
  vm_thread *thread = prof->thread;
  prof->initialized = true;

#define MAX_DEPTH 300
  char key[sizeof(cp_method*) * MAX_DEPTH];
  while (!__atomic_load_n(&prof->waiting_for_exit, __ATOMIC_SEQ_CST)) {
    prof->samples++;
    u32 count = arrlen(thread->stack.frames);
    if (count > MAX_DEPTH) {
      count = MAX_DEPTH;
    }
    u32 depth = 0;
    for (; depth < count; ++depth) {
      stack_frame *frame = thread->stack.frames[depth];
      // Check whether the frame is in the bounds of the frame buffer
      if ((char *)frame < thread->stack.frame_buffer ||
        (char *)frame >= thread->stack.frame_buffer + thread->stack.frame_buffer_used) {
        break;  // corrupted entry
      }
      cp_method *method = get_frame_method(frame);
      memcpy(key + sizeof(cp_method*) * depth, &method, sizeof(cp_method*));
    }

    // Now hash and insert the key
    if (depth > 0) {
      u32 new_count = (u32)(uintptr_t)hash_table_lookup(&prof->counts, key, (int)(depth * sizeof(cp_method*)));
      (void)hash_table_insert(&prof->counts, key, (int)(depth * sizeof(cp_method*)), (void *)((uintptr_t)new_count + 1));
    }

    // Now sleep (or in Emscripten, busy wait) until the next sample
#ifdef EMSCRIPTEN
    for (int i = 0; i < 100000; ++i) {
      if (__atomic_load_n(&prof->waiting_for_exit, __ATOMIC_SEQ_CST)) {
        break;
      }
    }
#else
    usleep(100);
#endif
  }

  // Now list all methods in the VM. This is an unsafe access to the class hash table that we'll need to fix once
  // we truly support multithreading.
  string_hash_table methods = make_hash_table(nullptr, 0.75, 1000);

  classdesc *cd;
  hash_table_iterator it = hash_table_get_iterator(&thread->vm->classes);
  while (hash_table_iterator_has_next(it, nullptr, nullptr, (void **)&cd)) {
    for (int i = 0; i < cd->methods_count; ++i) {
      cp_method *method = cd->methods + i;
      char method_key[sizeof(void *)];
      memcpy(method_key, &method, sizeof(void *));
      (void)hash_table_insert(&methods, method_key, sizeof(method_key), (void *)1);
    }
    hash_table_iterator_next(&it);
  }

  string_builder flamegraph;
  string_builder_init(&flamegraph);

  {
    hash_table_iterator it = hash_table_get_iterator(&prof->counts);
    char *key;
    size_t key_len;
    void *count;

    while (hash_table_iterator_has_next(it, &key, &key_len, &count)) {
      // Check whether the key represents a sequence of valid methods, and discard if not
      int depth = (int)(key_len / sizeof(cp_method*));
      for (int i = 0; i < depth; ++i) {
        cp_method *method;
        memcpy(&method, key + i * sizeof(cp_method*), sizeof(cp_method*));
        if (!hash_table_contains(&methods, (char *)&method, sizeof(cp_method*))) {
          goto next;
        }
      }

      // Now print the methods one by one in flamegraph format. Example:
      // java/lang/StringBuilder.append;java/lang/System.arraycopy 100
      for (int i = 0; i < depth; ++i) {
        cp_method *method;
        memcpy(&method, key + i * sizeof(cp_method*), sizeof(cp_method*));

        char *suffix = method->access_flags & ACCESS_NATIVE ? "_[k]" : "";
        string_builder_append(&flamegraph, "%.*s:%.*s%s",
          fmt_slice(method->my_class->name), fmt_slice(method->name), suffix);
        if (i == depth - 1) {
          string_builder_append(&flamegraph, " %d\n", (int)(uintptr_t)count);
        } else {
          string_builder_append(&flamegraph, ";");
        }
      }

      next:
      hash_table_iterator_next(&it);
    }
  }

  free_hash_table(methods);
  free_hash_table(prof->counts);
  prof->finished = true;
  prof->result = flamegraph.data;  // freed by the caller
  __atomic_thread_fence(__ATOMIC_SEQ_CST);

  __atomic_store_n(&prof->waiting_for_exit, 0, __ATOMIC_SEQ_CST);
}

profiler *launch_profiler(vm_thread *thread) {
  if (thread->profiler) {
    fprintf(stderr, "Thread already has a profiler");
    return nullptr;
  }

  profiler *prof = calloc(1, sizeof(profiler));
  prof->thread = thread;
  prof->waiting_for_exit = false;
  prof->counts = make_hash_table(nullptr, 0.75, 1000);
  thread->profiler = prof;

#ifdef PTHREADS_SUPPORTED
  pthread_create(&prof->pthread, nullptr, (void *(*)(void *))profiler_main, prof);

  while (!prof->initialized) {
    // Wait for the profiler to initialize
  }
#endif

  return prof;
}

void finish_profiler(profiler* prof) {
  if (!prof || prof->finished) {
    return;
  }
  prof->thread->profiler = nullptr;
  __atomic_store_n(&prof->waiting_for_exit, 1, __ATOMIC_SEQ_CST);

#ifdef PTHREADS_SUPPORTED
  pthread_join(prof->pthread, nullptr);
#endif
}

char *read_profiler(profiler* prof) {
  if (!prof) {
    return nullptr;
  }
  finish_profiler(prof);

  char *result = strdup(prof->result);
  arrfree(prof->result);
  free(prof);
  return result;
}