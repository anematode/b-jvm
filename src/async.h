//
// Created by alec on 1/16/25.
//

#ifndef ASYNC_H
#define ASYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "util.h"
#include <assert.h>
#include <stddef.h>
#include <types.h>
#include <util.h>

#ifdef __cplusplus
#define maybe_extern_begin extern "C" {
#define maybe_extern_end }
#else
#define maybe_extern_begin
#define maybe_extern_end
#endif

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

#define PUSH_PRAGMA(x)                                                                                                 \
  _Pragma("GCC diagnostic push");                                                                                      \
  _Pragma(x);
#define POP_PRAGMA _Pragma("GCC diagnostic pop");

/// Declares an async function.  Should be followed by a block containing any
/// locals that the async function needs (accessibly via self->).
#define DECLARE_ASYNC(return_type, name, locals, arguments, invoked_async_methods)                                     \
  typedef return_type name##_return_t;                                                                                 \
  DECLARE_ASYNC_VOID(name, return_type _result; locals ;                      \
                     , arguments, invoked_async_methods)

/// Declares a static async function.  Should be followed by a block containing any
/// locals that the async function needs (accessibly via self->).
#define DECLARE_STATIC_ASYNC(return_type, name, locals, arguments, invoked_async_methods)                              \
  typedef return_type name##_return_t;                                                                                 \
  DECLARE_STATIC_ASYNC_VOID(name, return_type _result; locals;, arguments, invoked_async_methods)

// async declaration mini dsl
#define invoked_methods(...) __VA_ARGS__
#define invoked_method(name) name##_t name;
#define locals(...) __VA_ARGS__
#define arguments(...) __VA_ARGS__

#define get_async_result(method_name) (self->invoked_async_methods.method_name._result)

#ifdef __cplusplus
// in c++, empty structs/unions have a size of 1 -- so if we detect this, replace with an int[0]
extern "C++" {
#include <type_traits>
template <typename T> struct pick_or_zero_sized {
  using type = std::conditional_t<(sizeof(T) > 1), T, int[0]>;
};

template <typename T> using pick_or_zero_sized_t = typename pick_or_zero_sized<T>::type;
}

#define FixTypeSize(name) pick_or_zero_sized_t<name>
#else
#define FixTypeSize(name) name
#endif

#ifdef __clang__
#define PUSH_EXTERN_C PUSH_PRAGMA("GCC diagnostic ignored \"-Wextern-c-compat\"");
#define POP_EXTERN_C POP_PRAGMA
#else
#define PUSH_EXTERN_C
#define POP_EXTERN_C
#endif

/// Declares an async function that returns nothing.  Should be followed by a
/// block containing any locals that the async function needs (accessibly via
/// self->).
#define DECLARE_ASYNC_VOID(name, locals, arguments, invoked_async_methods_)                                            \
  DECLARE_ASYNC_VOID_(, name, locals, arguments, invoked_async_methods_)

/// Declares a static function that returns nothing.  Should be followed by a
/// block containing any locals that the async function needs (accessibly via
/// self->)
#define DECLARE_STATIC_ASYNC_VOID(name, locals, arguments, invoked_async_methods_)                                     \
  DECLARE_ASYNC_VOID_(static, name, locals, arguments, invoked_async_methods_)

#define DECLARE_ASYNC_VOID_(method_mods, name, locals, arguments, invoked_async_methods_)                              \
  maybe_extern_begin;                                                                                                  \
  struct name##_s;                                                                                                     \
  typedef struct name##_s name##_t;                                                                                    \
  PUSH_EXTERN_C;                                                                                                       \
  struct name##_args {                                                                                                 \
    arguments;                                                                                                         \
  };                                                                                                                   \
  union name##_invoked_async_methods {                                                                                 \
    invoked_async_methods_;                                                                                            \
  };                                                                                                                   \
  POP_EXTERN_C;                                                                                                        \
  method_mods future_t name(void *self_);                                                                              \
  struct name##_s {                                                                                                    \
    FixTypeSize(struct name##_args) args;                                                                              \
    u32 _state;                                                                                                        \
    locals;                                                                                                            \
    FixTypeSize(union name##_invoked_async_methods) invoked_async_methods;                                             \
  };                                                                                                                   \
  maybe_extern_end;

// deal with the fallout of FixTypeSize
#ifdef __cplusplus
extern "C++" {
  template <typename T> T ZeroInternalState_(T t) {
    if constexpr (sizeof(t.args) == 0)
      return (T){._state = t._state};
    else
      return (T){.args = t.args, ._state = t._state};
  }
}

#define DoArgsDecl(name) [[maybe_unused]] auto args = &self->args;
#define ZeroInternalState(thing) thing = ZeroInternalState_(thing);
#else
#define DoArgsDecl(name) [[maybe_unused]] struct name##_args *args = &self->args;
#define ZeroInternalState(thing) thing = (typeof(thing)){.args = (thing).args, ._state = (thing)._state};
#endif

#define DEFINE_ASYNC_(modifiers, prelude, name)                                                                        \
  maybe_extern_begin;                                                                                                  \
  modifiers future_t name(void *self_) {                                                                               \
    name##_t *self = (name##_t *)self_;                                                                                \
    assert(self);                                                                                                      \
    prelude(name);                                                                                                     \
    start_counter(label_counter, (0) + 1);                                                                             \
    self->_state = (self->_state == 0) ? (0) : self->_state;                                                           \
    switch (self->_state) {                                                                                            \
    case (0):                                                                                                          \
      ZeroInternalState(*self);

#define cached_state_prelude(name)                                                                                     \
  _DECLARE_CACHED_STATE(name);                                                                                         \
  _RELOAD_CACHED_STATE();

/// Defines a value-returning async function. Should be followed by a block
/// containing the code of the async function.  MUST end with ASYNC_END, or
/// ASYNC_END_VOID if the function is guaranteed to call ASYNC_RETURN() before
/// it reaches the end statement. Use DEFINE_ASYNC_SL if this is nested in
/// another switch/case
#define DEFINE_ASYNC(name) DEFINE_ASYNC_(, cached_state_prelude, name)

/// Defines a static value-returning async function. Should be followed by a block
/// containing the code of the async function.  MUST end with ASYNC_END, or
/// ASYNC_END_VOID if the function is guaranteed to call ASYNC_RETURN() before
/// it reaches the end statement. Use DEFINE_ASYNC_SL if this is nested in
/// another switch/case
#define DEFINE_STATIC_ASYNC(name) DEFINE_ASYNC_(static, cached_state_prelude, name)

/// reload the cached state from the self pointer
#define _RELOAD_CACHED_STATE()                                                                                         \
  do {                                                                                                                 \
    args = &self->args;                                                                                                \
  } while (0)

/// used to cache state on the stack for easy access -- must be reloaded in _RELOAD_CACHED_STATE
#define _DECLARE_CACHED_STATE(method_name) DoArgsDecl(method_name);

#define do_maybe_reload_state()                                                                                        \
  if (unlikely(self->_state == state_index))                                                                           \
    _RELOAD_CACHED_STATE();

/// Begins a block of code that will be executed asynchronously from inside
/// another block. DO NOT USE STACK VARIABLES FROM BEFORE AWAIT() AFTER AWAIT.
#define AWAIT_INNER(context, method_name, ...) AWAIT_INNER_(do_maybe_reload_state, context, method_name, __VA_ARGS__)
#define AWAIT_INNER_(after_label, context, method_name, ...)                                                           \
  do {                                                                                                                 \
    get_counter_value(label_counter, state_index);                                                                     \
    (context)->_state = 0;                                                                                             \
    (context)->args = (struct method_name##_args){__VA_ARGS__};                                                        \
    PUSH_PRAGMA("GCC diagnostic ignored \"-Wimplicit-fallthrough\"");                                                  \
    PUSH_PRAGMA("GCC diagnostic ignored \"-Wswitch\"");                                                                \
  case state_index:                                                                                                    \
    /* if we've fallen through to this point, we don't need to reload the state */                                     \
    after_label();                                                                                                     \
    future_t __fut = method_name(context);                                                                             \
    if (__fut.status == FUTURE_NOT_READY) {                                                                            \
      self->_state = state_index;                                                                                      \
      return __fut;                                                                                                    \
    }                                                                                                                  \
    POP_PRAGMA;                                                                                                        \
    POP_PRAGMA;                                                                                                        \
  } while (0)

#define AWAIT_FUTURE_EXPR(expr, ...)                                                                                   \
  do {                                                                                                                 \
    get_counter_value(label_counter, state_index);                                                                     \
    PUSH_PRAGMA("GCC diagnostic ignored \"-Wimplicit-fallthrough\"");                                                  \
  case state_index:                                                                                                    \
    /* if we've fallen through to this point, we don't need to reload the state */                                     \
    if (unlikely(self->_state == state_index))                                                                         \
      _RELOAD_CACHED_STATE();                                                                                          \
    future_t __fut = (expr);                                                                                           \
    if (__fut.status == FUTURE_NOT_READY) {                                                                            \
      self->_state = state_index;                                                                                      \
      return __fut;                                                                                                    \
    }                                                                                                                  \
    POP_PRAGMA;                                                                                                        \
  } while (0)

#define AWAIT(method_name, ...) AWAIT_INNER(&self->invoked_async_methods.method_name, method_name, __VA_ARGS__)

/// Calls an async method that is guaranteed to be ready immediately.  Will UNREACHABLE()
/// if the future is not ready.  Usable as an expression from a non-async fn -- does not interact with async method
/// machinery.
#define AWAIT_READY(method_name, ...)                                                                                  \
  ({                                                                                                                   \
    method_name##_t __ctx = {.args = {__VA_ARGS__}, ._state = 0};                                                      \
    future_t __fut = method_name(&__ctx);                                                                              \
    if (unlikely(__fut.status != FUTURE_READY))                                                                        \
      UNREACHABLE(#method_name " called via AWAIT_READY, but it tried to block.");                                     \
    __ctx._result;                                                                                                     \
  })

/// Ends an async value-returning function, returning the given value.  Must be
/// used inside an async function.
#define ASYNC_END(return_value)                                                                                        \
  self->_result = (return_value);                                                                                      \
  self->_state = 0;                                                                                                    \
  return future_ready();                                                                                               \
  default:                                                                                                             \
    UNREACHABLE();                                                                                                     \
    }                                                                                                                  \
    }                                                                                                                  \
    maybe_extern_end;

/// Ends an async void-returning function.  Must be used inside an async
/// function.
#define ASYNC_END_VOID()                                                                                               \
  self->_state = 0;                                                                                                    \
  return future_ready();                                                                                               \
  default:                                                                                                             \
    UNREACHABLE();                                                                                                     \
    }                                                                                                                  \
    }                                                                                                                  \
    maybe_extern_end;

/// Returns from an async function.
#define ASYNC_RETURN(return_value)                                                                                     \
  do {                                                                                                                 \
    self->_state = 0;                                                                                                  \
    self->_result = (return_value);                                                                                    \
    return future_ready();                                                                                             \
  } while (0)

#define ASYNC_RETURN_VOID()                                                                                            \
  do {                                                                                                                 \
    self->_state = 0;                                                                                                  \
    return future_ready();                                                                                             \
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
