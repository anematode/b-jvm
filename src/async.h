//
// Created by alec on 1/16/25.
//

#ifndef ASYNC_H
#define ASYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>

typedef enum { FUTURE_NOT_READY, FUTURE_READY } future_status;

struct async_wakeup_info;
typedef struct async_wakeup_info async_wakeup_info;

typedef struct future {
  future_status status;
  async_wakeup_info *wakeup;
} future_t;

#define future_not_ready(wakeup)                                               \
  (future_t) { FUTURE_NOT_READY, (wakeup) }
#define future_ready()                                                         \
  (future_t) { FUTURE_READY, nullptr }

#define start_counter(counter_name, start_value)                               \
  enum { counter_name = __COUNTER__ - (start_value) };
#define get_counter_value(counter_name, target_name)                           \
  enum { target_name = __COUNTER__ - (counter_name) - 1 };

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

/// Defines a void-returning async function, with a custom starting label idx
/// for cases. Should be followed by a block containing the code of the async
/// function.  Must end with ASYNC_END_VOID before closing bracket.
#define DEFINE_ASYNC_VOID_SL(name, start_idx, ...)                             \
  future_t name(name##_t *self, ##__VA_ARGS__) {                               \
    start_counter(label_counter, start_idx);                                   \
    switch (self->_state) {                                                    \
    case 0:                                                                    \
      *self = (typeof(*self)){0};

/// Defines a void-returning async function. Should be followed by a block
/// containing the code of the async
/// function.  Must end with ASYNC_END_VOID before closing bracket.  Use
/// DEFINE_ASYNC_VOID_SL if this is nested in another switch/case
#define DEFINE_ASYNC_VOID(name, ...)                                           \
  DEFINE_ASYNC_VOID_SL(name, 1, ##__VA_ARGS__)

/// Defines a value-returning async function, with a custom starting label idx
/// for cases. Should be followed by a block containing the code of the async
/// function.  MUST end with ASYNC_END, or ASYNC_END_VOID if the function is
/// guaranteed to call ASYNC_RETURN() before it reaches the end statement.
#define DEFINE_ASYNC_SL(return_type, name, start_idx, ...)                     \
  future_t name(name##_t *self, ##__VA_ARGS__) {                               \
    printf("Self: %p\n", self);                                                \
    if ((uintptr_t)self < 9)                                                   \
      *(char *)1 = 0;                                                          \
    start_counter(label_counter, start_idx);                                   \
    switch (self->_state) {                                                    \
    case 0:                                                                    \
      *self = (typeof(*self)){0};

/// Defines a value-returning async function. Should be followed by a block
/// containing the code of the async function.  MUST end with ASYNC_END, or
/// ASYNC_END_VOID if the function is guaranteed to call ASYNC_RETURN() before
/// it reaches the end statement. Use DEFINE_ASYNC_SL if this is nested in
/// another switch/case
#define DEFINE_ASYNC(return_type, name, ...)                                   \
  DEFINE_ASYNC_SL(return_type, name, 1, ##__VA_ARGS__)

/// Begins a block of code that will be executed asynchronously from inside
/// another block. DO NOT USE STACK VARIABLES FROM BEFORE AWAIT() AFTER AWAIT.
#define AWAIT(fut_expr)                                                        \
  do {                                                                         \
    get_counter_value(label_counter, state_index);                             \
  case state_index:                                                            \
    future_t __fut = (fut_expr);                                               \
    if (__fut.status == FUTURE_NOT_READY) {                                    \
      self->_state = state_index;                                              \
      return __fut;                                                            \
    }                                                                          \
  } while (0)

/// Ends an async value-returning function, returning the given value.  Must be
/// used inside an async function.
#define ASYNC_END(return_value)                                                \
  default:                                                                     \
    self->_result = (return_value);                                            \
    self->_state = 0;                                                          \
    return future_ready();                                                     \
    }                                                                          \
    }

/// Ends an async void-returning function.  Must be used inside an async
/// function.
#define ASYNC_END_VOID()                                                       \
  default:                                                                     \
    self->_state = 0;                                                          \
    return future_ready();                                                     \
    }                                                                          \
    }

/// Returns from an async function.
#define ASYNC_RETURN(return_value)                                             \
  do {                                                                         \
    self->_state = 0;                                                          \
    self->_result = (return_value);                                            \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                        \
  } while (0)

#define ASYNC_RETURN_VOID()                                                    \
  do {                                                                         \
    self->_state = 0;                                                          \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                        \
  } while (0)

/// Yields control back to the caller.  Must be used inside an async function.
/// Takes a pointer to the runtime-specific async_wakeup_info struct.
#define ASYNC_YIELD(waker)                                                     \
  do {                                                                         \
    get_counter_value(label_counter, state_index);                             \
    self->_state = state_index;                                                \
    return future_not_ready((waker));                                          \
  case state_index:;                                                           \
  } while (0)

#ifdef __cplusplus
}
#endif
#endif // ASYNC_H
