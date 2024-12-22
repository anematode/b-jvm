#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <optional>
#include <array>

#include <bjvm.h>
#include <arrays.h>

#include "tests-common.h"

using namespace Bjvm::Tests;
using std::array;

constexpr static int testArrayDimensions[] = {7, 8, 9};

TEST_CASE("Multi-dimensional boolean array") {
  auto vm = CreateTestVM(false);
  auto thr = bjvm_create_thread(vm.get(), bjvm_default_thread_options());

  auto kind = bjvm_primitive_classdesc(thr, BJVM_TYPE_KIND_BOOLEAN);
  for (int i = 0; i < 3; i++) {
    kind = make_array_classdesc(thr, kind);
  }

  auto array = CreateArray(thr, kind, testArrayDimensions, 3);

  for (int i = 0; i < testArrayDimensions[0]; i++) {
    auto array1 = ReferenceArrayLoad(array, i);

    for (int j = 0; j < testArrayDimensions[1]; j++) {
      auto array2 = ReferenceArrayLoad(array1, j);

      for (int k = 0; k < testArrayDimensions[2]; k++) {
        auto val = ByteArrayLoad(array2, k);
        REQUIRE(val == 0);
      }
    }
  }

  bjvm_free_thread(thr);
}