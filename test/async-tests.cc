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

DECLARE_ASYNC_VOID(test_yield, async_wakeup_info info;, int index);
DEFINE_ASYNC_VOID(test_yield, int index) {
  self->info.index = index;
  ASYNC_YIELD(&self->info);
  ASYNC_END_VOID();
}

namespace a {
DECLARE_ASYNC_VOID(test_method, test_yield_t ctx);
DEFINE_ASYNC_VOID(test_method) {
  AWAIT(test_yield(&self->ctx, 1));
  AWAIT(test_yield(&self->ctx, 2));
  AWAIT(test_yield(&self->ctx, 3));

  ASYNC_END_VOID();
}

TEST_CASE("Top-level await") {
  test_method_t tm = {};
  future_t fut;

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}
}

namespace b {
DECLARE_ASYNC_VOID(test_method, int i; test_yield_t ctx);
DEFINE_ASYNC_VOID(test_method) {
  for (self->i = 1; self->i <= 3; self->i++) {
    AWAIT(test_yield(&self->ctx, self->i));
  }

  ASYNC_END_VOID();
}

TEST_CASE("For loop") {
  test_method_t tm = {};
  future_t fut;

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}
}

namespace c {
DECLARE_ASYNC_VOID(test_method, test_yield_t ctx; test_method_t *callee;, int i)
DEFINE_ASYNC_VOID(test_method, int i) {
  if (i == 0)
    ASYNC_RETURN_VOID();

  self->callee = new test_method_t{};
  AWAIT(test_method(self->callee, i - 1));
  delete self->callee;
  self->callee = nullptr;

  AWAIT(test_yield(&self->ctx, i));

  ASYNC_END_VOID();
}

TEST_CASE("Recursion") {
  test_method_t tm = {};
  future_t fut;

  fut = test_method(&tm, 3);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 1);

  fut = test_method(&tm, 3);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 2);

  fut = test_method(&tm, 3);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 3);

  fut = test_method(&tm, 3);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
}
}

namespace d {
struct skibidi {
  std::string abc;
  std::string def;
};

DECLARE_ASYNC(skibidi, test_method, test_yield_t ctx; test_method_t *callee;);
DEFINE_ASYNC(skibidi, test_method) {

  AWAIT(test_yield(&self->ctx, 6));

  ASYNC_END((skibidi{"abc", "def"}));
}

TEST_CASE("Fat return type") {
  test_method_t tm = {};
  future_t fut;

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_NOT_READY);
  REQUIRE(fut.wakeup->index == 6);

  fut = test_method(&tm);
  REQUIRE(fut.status == FUTURE_READY);
  REQUIRE(fut.wakeup == nullptr);
  REQUIRE(tm._result.abc == "abc");
  REQUIRE(tm._result.def == "def");
}
}