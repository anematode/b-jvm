#ifndef IO_NATIVES_H
#define IO_NATIVES_H

#include <natives.h>
#include <stddef.h>

#define ThrowIOExceptionM(fmt, ...)                                                                                    \
  do {                                                                                                                 \
    char msg[1024];                                                                                                    \
    size_t size = snprintf(msg, 1024, fmt, __VA_ARGS__);                                                               \
    bjvm_utf8 msg_slice = {msg, size};                                                                                 \
    bjvm_raise_vm_exception(thread, STR("java/io/IOException"), msg_slice);                                            \
  } while (0)

#endif