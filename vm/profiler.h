//
// Created by Cowpox on 2/22/25.
//

#ifndef PROFILER_H
#define PROFILER_H

#include "bjvm.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct profiler_s profiler;

// Immediately attaches a profiler to the given thread. Should be called from the main thread.
EMSCRIPTEN_KEEPALIVE
profiler *launch_profiler(vm_thread *thread);

EMSCRIPTEN_KEEPALIVE
void finish_profiler(profiler* profiler);

// Calls finish_profiler, then reads the profiler's data into a heap allocated string and frees the profiler. Should be
// called from the main thread.
EMSCRIPTEN_KEEPALIVE
char *read_profiler(profiler* profiler);

#ifdef __cplusplus
}
#endif

#endif //PROFILER_H
