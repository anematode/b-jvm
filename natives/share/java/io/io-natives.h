#ifndef IO_NATIVES_H
#define IO_NATIVES_H

#include <natives-dsl.h>
#include <stddef.h>

#define ThrowIOExceptionM(fmt, ...)                                                                                    \
  do {                                                                                                                 \
    char msg[1024];                                                                                                    \
    size_t size = snprintf(msg, 1024, fmt, __VA_ARGS__);                                                               \
    slice msg_slice = {msg, size};                                                                                 \
    bjvm_raise_vm_exception(thread, STR("java/io/IOException"), msg_slice);                                            \
  } while (0)

#endif