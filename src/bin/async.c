#include <async.h>
#include <stdio.h>

typedef struct {
  int delay;
} async_wakeup_info;

DECLARE_ASYNC(
  int, my_inner_future,
  locals(
    int _current_value;
    async_wakeup_info my_wakeup;
    ),
  arguments(
    int a;
   ),
   invoked_methods()
);

DEFINE_ASYNC(my_inner_future) {
  for (self->_current_value = 0; self->_current_value < 3;) {
    self->my_wakeup.delay = self->_current_value++;
    ASYNC_YIELD(&self->my_wakeup);
  }

  ASYNC_END(5);
};

DECLARE_ASYNC_VOID(fetch_data,
  locals(
    int _res;
  ),
  arguments(
    int a;
  ),
  invoked_methods(
      invoked_method(my_inner_future)
  )
);

DEFINE_ASYNC(fetch_data) {
  self->_res = 0;

  AWAIT(my_inner_future, 0);
  self->_res += get_async_result(my_inner_future);
  printf("self->_inner_future._result: %d\n",
         get_async_result(my_inner_future));

  AWAIT(my_inner_future, 1);
  self->_res += get_async_result(my_inner_future);

  AWAIT(my_inner_future, 2);
  self->_res += get_async_result(my_inner_future);

  printf("state: %d\n", self->_res);

  ASYNC_END_VOID();
};

int main(void) {
  // Create an instance of the async state
  fetch_data_t my_fetch = {0}; // zero-initialize everything

  for (int i = 0; i < 20; i++) {
    future_t fut = fetch_data(&my_fetch);
    if (fut.status == FUTURE_READY) {
      printf("FUTURE is READY! Value = %d\n", my_fetch._res);
      break;
    } else {
      printf("FUTURE is NOT READY. (poll %d), delay %d\n", i,
             ((async_wakeup_info *) fut.wakeup)->delay);
    }
  }

  return 0;
}