//
// Created by alec on 1/16/25.
//

#ifndef ASYNC_H
#define ASYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <assert.h>

typedef enum { FUTURE_NOT_READY, FUTURE_READY } future_status;

struct async_wakeup_info;
typedef struct async_wakeup_info async_wakeup_info;

typedef struct future {
  future_status status;
  async_wakeup_info *wakeup;
} future_t;

#define future_not_ready(wakeup)                                                                                       \
  (future_t) { FUTURE_NOT_READY, (wakeup) }
#define future_ready()                                                                                                 \
  (future_t) { FUTURE_READY, nullptr }

#define start_counter(counter_name, start_value) enum { counter_name = __COUNTER__ - (start_value) };
#define get_counter_value(counter_name, target_name) enum { target_name = __COUNTER__ - (counter_name) - 1 };

#define DEBRACKET(X) ESC(ISH X)
#define ISH(...) ISH __VA_ARGS__
#define ESC(...) ESC_(__VA_ARGS__)
#define ESC_(...) VAN##__VA_ARGS__
#define VANISH

/// Declares an async function.  Should be followed by a block containing any
/// locals that the async function needs (accessibly via self->).
#define DECLARE_ASYNC(return_type, name, locals, arguments, invoked_async_methods)                                     \
  typedef return_type name##_return_t;                                                                                 \
  DECLARE_ASYNC_VOID(name, return_type _result; locals ;                      \
                     , arguments, invoked_async_methods)

// async declaration mini dsl
#define invoked_methods(...) __VA_ARGS__
#define invoked_method(name) name##_t name;
#define locals(...) __VA_ARGS__
#define arguments(...) __VA_ARGS__

#define get_async_result(method_name) (self->invoked_async_methods.method_name._result)
/// Declares an async function that returns nothing.  Should be followed by a
/// block containing any locals that the async function needs (accessibly via
/// self->).
#define DECLARE_ASYNC_VOID(name, locals, arguments, invoked_async_methods_)                                            \
  struct name##_s;                                                                                                     \
  typedef struct name##_s name##_t;                                                                                    \
  struct name##_args {                                                                                                 \
    arguments;                                                                                                         \
  };                                                                                                                   \
  union name##_invoked_async_methods {                                                         \
    invoked_async_methods_;                                                                                            \
  };                                                                                                                   \
  future_t name(name##_t *self);                                                                                       \
  struct name##_s {                                                                                                    \
    struct name##_args args;                                                                                           \
    int _state;                                                                                                        \
    locals;                                                                                                            \
    union name##_invoked_async_methods invoked_async_methods;                                                          \
  };

/// Defines a async function, with a custom starting label idx
/// for cases. Should be followed by a block containing the code of the async
/// function.  MUST end with ASYNC_END, or ASYNC_END_VOID if the function is
/// guaranteed to call ASYNC_RETURN() before it reaches the end statement.
#define DEFINE_ASYNC_SL(name, start_idx)                                                                               \
  future_t name(name##_t *self) {                                                                                      \
    assert(self);                                                                                                      \
    struct name##_args *args = &self->args;                                                                            \
    start_counter(label_counter, (start_idx) + 1);                                                                     \
    self->_state = (self->_state == 0) ? (start_idx) : self->_state;                                                   \
    switch (self->_state) {                                                                                            \
    case (start_idx):                                                                                                  \
      *self = (typeof(*self)){.args = self->args, ._state = self->_state};

/// Defines a value-returning async function. Should be followed by a block
/// containing the code of the async function.  MUST end with ASYNC_END, or
/// ASYNC_END_VOID if the function is guaranteed to call ASYNC_RETURN() before
/// it reaches the end statement. Use DEFINE_ASYNC_SL if this is nested in
/// another switch/case
#define DEFINE_ASYNC(name) DEFINE_ASYNC_SL(name, 0)

/// Begins a block of code that will be executed asynchronously from inside
/// another block. DO NOT USE STACK VARIABLES FROM BEFORE AWAIT() AFTER AWAIT.
#define AWAIT_INNER(context, method_name, ...)                                                                         \
  do {                                                                                                                 \
    get_counter_value(label_counter, state_index);                                                                     \
    (context)->_state = 0;                                                                                             \
    (context)->args = (struct method_name##_args){__VA_ARGS__};                                                        \
  case state_index:                                                                                                    \
    args = &self->args;                                                                                                \
    future_t __fut = method_name(context);                                                                             \
    if (__fut.status == FUTURE_NOT_READY) {                                                                            \
      self->_state = state_index;                                                                                      \
      return __fut;                                                                                                    \
    }                                                                                                                  \
  } while (0)

#define AWAIT_FUTURE_EXPR(expr, ...)                                                                                   \
  do {                                                                                                                 \
    get_counter_value(label_counter, state_index);                                                                     \
  case state_index:                                                                                                    \
    args = &self->args;                                                                                                \
    future_t __fut = (expr);                                                                                           \
    if (__fut.status == FUTURE_NOT_READY) {                                                                            \
      self->_state = state_index;                                                                                      \
      return __fut;                                                                                                    \
    }                                                                                                                  \
  } while (0)

#define AWAIT(method_name, ...) AWAIT_INNER(&self->invoked_async_methods.method_name, method_name, __VA_ARGS__)

/// Ends an async value-returning function, returning the given value.  Must be
/// used inside an async function.
#define ASYNC_END(return_value)                                                                                        \
  default:                                                                                                             \
    self->_result = (return_value);                                                                                    \
    self->_state = 0;                                                                                                  \
    return future_ready();                                                                                             \
    }                                                                                                                  \
    }

/// Ends an async void-returning function.  Must be used inside an async
/// function.
#define ASYNC_END_VOID()                                                                                               \
  default:                                                                                                             \
    self->_state = 0;                                                                                                  \
    return future_ready();                                                                                             \
    }                                                                                                                  \
    }

/// Returns from an async function.
#define ASYNC_RETURN(return_value)                                                                                     \
  do {                                                                                                                 \
    self->_state = 0;                                                                                                  \
    self->_result = (return_value);                                                                                    \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                                                                \
  } while (0)

#define ASYNC_RETURN_VOID()                                                                                            \
  do {                                                                                                                 \
    self->_state = 0;                                                                                                  \
    return (future_t){FUTURE_READY, .wakeup = nullptr};                                                                \
  } while (0)

/// Yields control back to the caller.  Must be used inside an async function.
/// Takes a pointer to the runtime-specific async_wakeup_info struct.
#define ASYNC_YIELD(waker)                                                                                             \
  do {                                                                                                                 \
    get_counter_value(label_counter, state_index);                                                                     \
    self->_state = state_index;                                                                                        \
    return future_not_ready((waker));                                                                                  \
  case state_index:;                                                                                                   \
    args = &self->args;                                                                                                \
  } while (0)

#ifdef __cplusplus
}
#endif
#endif // ASYNC_H
