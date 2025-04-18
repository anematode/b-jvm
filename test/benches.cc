// Benchmarks. We'll use this to track the VM performance over time.

#include "tests-common.h"

#include "doctest/doctest.h"

using namespace Bjvm::Tests;

// until we figure out how to benchmark
#define BENCHMARK(x) if (0)

TEST_CASE("Benchmarks") {
  BENCHMARK("Big decimal") { auto result = run_test_case("test_files/bench_big_decimal/", true); };

  BENCHMARK("Stack trace") { auto result = run_test_case("test_files/bench_stack_trace/", true); };

  BENCHMARK("JSON startup") {
    auto result = run_test_case("test_files/json:test_files/json/gson-2.11.0.jar:test_files/"
                                "json/jackson-core-2.18.2.jar:test_files/json/"
                                "jackson-annotations-2.18.2.jar:test_files/json/"
                                "jackson-databind-2.18.2.jar",
                                true, "GsonExample");
  };

  BENCHMARK("Advanced lambda") { auto result = run_test_case("test_files/advanced_lambda", true); };

  BENCHMARK("Cfg fuck") { auto result = run_test_case("test_files/cfg_fuck", true); };
}