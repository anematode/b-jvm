// Benchmarks. We'll use this to track the VM performance over time.

#include "tests-common.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

using namespace Bjvm::Tests;

TEST_CASE("Benchmarks", "[bench]") {
  BENCHMARK("Big decimal") {
    auto result = run_test_case("test_files/bench_big_decimal/", true);
  };

  BENCHMARK("Stack trace") {
    auto result = run_test_case("test_files/bench_stack_trace/", true);
  };

  /*
  BENCHMARK("JSON") {

  };
  */
}