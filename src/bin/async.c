#include <async.h>
#include <stdio.h>

struct async_wakeup_info {
  int delay;
};

DECLARE_ASYNC(int, my_inner_future, int _current_value; async_wakeup_info my_wakeup;, int a);

DEFINE_ASYNC(int, my_inner_future, int a) {
  for (self->_current_value = 0; self->_current_value < 3; ) {
    self->my_wakeup.delay = self->_current_value++;
    ASYNC_YIELD(&self->my_wakeup);
  }

  ASYNC_END(5);
};

DECLARE_ASYNC_VOID(fetch_data, int _res; my_inner_future_t _inner_future, int a);

DEFINE_ASYNC_VOID(fetch_data)
{
    self->_res = 0;

  AWAIT(my_inner_future(&self->_inner_future, 0));
  self->_res += self->_inner_future._result;
  printf("self->_inner_future._result: %d\n", self->_inner_future._result);

  AWAIT(my_inner_future(&self->_inner_future, 1));
  self->_res += self->_inner_future._current_value;

  AWAIT(my_inner_future(&self->_inner_future, 2));
  self->_res += self->_inner_future._current_value;


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
      printf("FUTURE is NOT READY. (poll %d), delay %d\n", i, fut.wakeup->delay);
    }
  }

  return 0;
}