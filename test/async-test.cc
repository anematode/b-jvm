//
// Created by alec on 1/17/25.
//

#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include "../src/async.h"

struct async_wakeup_info {
  int index;
};

DECLARE_ASYNC_VOID(test_yield, locals(async_wakeup_info info;), arguments(int index),);
DEFINE_ASYNC(test_yield) {
  self->info.index = args->index;
  ASYNC_YIELD(&self->info);
  ASYNC_END_VOID();
}

DECLARE_ASYNC_VOID(a_test_method, locals(), arguments(), invoked_methods(invoked_method(test_yield)));
DEFINE_ASYNC(a_test_method) {
  AWAIT(test_yield, 1);
  AWAIT(test_yield, 2);
  AWAIT(test_yield, 3);

  ASYNC_END_VOID();
}

TEST_CASE("Top-level await") {
  a_test_method_t tm = {};
  future_t fut;

  fut = a_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = a_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = a_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = a_test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}


DECLARE_ASYNC_VOID(b_test_method, locals(int i), arguments(), invoked_methods(invoked_method(test_yield)));
DEFINE_ASYNC(b_test_method) {
  for (self->i = 1; self->i <= 3; self->i++) {
    AWAIT(test_yield, self->i);
  }

  ASYNC_END_VOID();
}

TEST_CASE("For loop") {
  b_test_method_t tm = {};
  future_t fut;

  fut = b_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = b_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = b_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = b_test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}

DECLARE_ASYNC_VOID(c_test_method, locals(c_test_method_t *callee), arguments(int i), invoked_methods(invoked_method(test_yield)));
DEFINE_ASYNC(c_test_method) {
  if (args->i == 0)
    ASYNC_RETURN_VOID();

  self->callee = new c_test_method_t{};
  AWAIT_INNER(self->callee, c_test_method, args->i - 1);
  delete self->callee;
  self->callee = nullptr;

  AWAIT(test_yield, args->i);

  ASYNC_END_VOID();
}

TEST_CASE("Recursion") {
  c_test_method_t tm = {.args = {3}};
  future_t fut;

  fut = c_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = c_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = c_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = c_test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}

struct skibidi {
  std::string abc;
  std::string def;
};

DECLARE_ASYNC(skibidi, d_test_method, locals(d_test_method_t *callee), arguments(), invoked_methods(invoked_method(test_yield)));
DEFINE_ASYNC(d_test_method) {

  AWAIT(test_yield, 6);

  ASYNC_END(((struct skibidi){"abc", "def"}));
}

TEST_CASE("Fat return type") {
  d_test_method_t tm = {};
  future_t fut;

  fut = d_test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 6);

  fut = d_test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
  REQUIRE(tm._result.abc == "abc");
  REQUIRE(tm._result.def == "def");
}