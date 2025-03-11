#include "doctest/doctest.h"
#include <climits>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <unordered_map>

#include <arrays.h>
#include <bjvm.h>

#include "tests-common.h"

using namespace Bjvm::Tests;
using std::array;

constexpr static int testArrayDimensions[] = {7, 8, 9};

TEST_CASE("Multi-dimensional boolean array") {
  auto vm = CreateTestVM();
  auto thr = create_main_thread(vm.get(), default_thread_options());

  auto kind = primitive_classdesc(thr, TYPE_KIND_BOOLEAN);
  for (int i = 0; i < 3; i++) {
    kind = get_or_create_array_classdesc(thr, kind);
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

  free_thread(thr);
}