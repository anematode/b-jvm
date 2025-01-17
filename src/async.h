//
// Created by alec on 1/16/25.
//

#ifndef ASYNC_H
#define ASYNC_H

#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef enum { FUTURE_NOT_READY, FUTURE_READY } future_status;

struct async_wakeup_info;
typedef struct async_wakeup_info async_wakeup_info;

typedef struct future {
  future_status status;
  async_wakeup_info *wakeup;
} future_t;

#define NOARG

/// Declares an async function.  Should be followed by a block containing any
/// locals that the async function needs (accessibly via self->).
#define DECLARE_ASYNC(return_type, name, locals, ...)                          \
  struct name##_s;                                                             \
  typedef struct name##_s name##_t;                                            \
  future_t name(name##_t *self, ##__VA_ARGS__);                                \
  struct name##_s {                                                            \
    int _state;                                                                \
    return_type _result;                                                       \
    locals;                                                                    \
  };
/// Declares an async function that returns nothing.  Should be followed by a
/// block containing any locals that the async function needs (accessibly via
/// self->).
#define DECLARE_ASYNC_VOID(name, locals, ...)                                  \
  struct name##_s;                                                             \
  typedef struct name##_s name##_t;                                            \
  future_t name(name##_t *self, ##__VA_ARGS__);                                \
  struct name##_s {                                                            \
    int _state;                                                                \
    locals;                                                                    \
  };

/// Defines a void-returning async function.  Should be followed by a block
/// containing the code of the async function.  Must end with ASYNC_END_VOID
/// before closing bracket.
#define DEFINE_ASYNC_VOID(name, ...)                                           \
  future_t name(name##_t *self, ##__VA_ARGS__) {                               \
    future_t __fut;                                                            \
    async_wakeup_info *__wakeup = NULL;                                        \
    switch (self->_state) {                                                    \
    case 0:                                                                    \
      *self = (typeof(*self)){0};                                              \
      {

/// Defines a value-returning async function.  Should be followed by a block
/// containing the code of the async function.  MUST end with ASYNC_END, or
/// ASYNC_END_VOID if the function is guaranteed to call ASYNC_RETURN() before
/// it reaches the end statement.
#define DEFINE_ASYNC(return_type, name, ...)                                   \
  future_t name(name##_t *self, ##__VA_ARGS__) {                               \
    future_t __fut;                                                            \
    async_wakeup_info *__wakeup = NULL;                                        \
    switch (self->_state) {                                                    \
    case 0:                                                                    \
      *self = (typeof(*self)){0};                                              \
      {

/// Begins a block of code that will be executed asynchronously.  Must be used
/// inside an async function.
#define AWAIT(fut_expr)                                                        \
  }                                                                            \
  do {                                                                         \
  case __LINE__:;                                                              \
    __fut = (fut_expr);                                                        \
    if (__fut.status == FUTURE_NOT_READY) {                                    \
      self->_state = __LINE__;                                                 \
      return __fut;                                                            \
    }                                                                          \
  } while (0);                                                                 \
  {

/// Ends an async value-returning function, returning the given value.  Must be
/// used inside an async function.
#define ASYNC_END(return_value)                                                \
  default:;                                                                    \
    {                                                                          \
      self->_result = (return_value);                                          \
      return (future_t){FUTURE_READY, .wakeup = nullptr};                      \
    }}                                                                          \
    }                                                                          \
    }

/// Ends an async void-returning function.  Must be used inside an async
/// function.
#define ASYNC_END_VOID()                                                       \
  default:;                                                                    \
    {                                                                          \
      return (future_t){FUTURE_READY, .wakeup = nullptr};                      \
    }}                                                                          \
    }                                                                          \
    }

/// Returns from an async function.
#define ASYNC_RETURN(return_value)                                             \
  do {                                                                         \
    self->_result = (return_value);                                            \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                        \
  } while (0)

#define ASYNC_RETURN_VOID()                                                    \
  do {                                                                         \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                        \
  } while (0)

/// Yields control back to the caller.  Must be used inside an async function.
/// Takes a pointer to the runtime-specific async_wakeup_info struct.
#define ASYNC_YIELD(waker)                                                     \
  do {                                                                         \
    __wakeup = (waker);                                                        \
    self->_state = __LINE__;                                                   \
    return (future_t){FUTURE_NOT_READY, .wakeup = __wakeup};                   \
  case __LINE__:;                                                              \
  } while (0)

#endif // ASYNC_H
