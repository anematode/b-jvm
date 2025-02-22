//
// Created by Cowpox on 2/20/25.
//

#include "jit_allocator.h"

#include <stdio.h>
#include <stdlib.h>

#ifdef __APPLE__
#include <mach/mach.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/mman.h>

void *jit_arena; // initialized with mmap
constexpr int JIT_SIZE = 1 << 24;

struct jit_function {
  void *addr;                              // callable function pointer, using native calling convention
  void (*free)(struct jit_function *self); // release resources
};

struct jit_context {
  char *insts;
  size_t insts_bytes;
  size_t write_offset; // where to put it in the arena.
};

int jit_writing_callback(void *context) {
  auto ctx = (struct jit_context *)context;
  memcpy(jit_arena, ctx->insts, ctx->insts_bytes);
  return 0;
}

PTHREAD_JIT_WRITE_ALLOW_CALLBACKS_NP(jit_writing_callback);

void initialize_jit_arena() {
  if (jit_arena)
    return;

  jit_arena = mmap(NULL, JIT_SIZE, PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
  if (jit_arena == MAP_FAILED) {
    perror("mmap");
    exit(1);
  }

  struct jit_context ctx;
  char shellcode[] = {
      0x00, 0x00, 0x01, 0x0b, // add w0, w0, w1
      0xc0, 0x03, 0x5f, 0xd6  // ret
  };
  ctx.insts = shellcode;
  ctx.insts_bytes = sizeof(shellcode);
  pthread_jit_write_with_callback_np(jit_writing_callback, &ctx);

  typedef int (*add_fn)(int, int);
  add_fn add = (add_fn)jit_arena;
  printf("add(1, 2) = %d\n", add(1, 2));
}

#endif