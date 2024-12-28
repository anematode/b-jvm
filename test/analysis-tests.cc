// Make sure the analysis gets through all of the JDK successfully.

#include "tests-common.h"
#include "../src/analysis.h"

#include <catch2/catch_test_macros.hpp>

using namespace Bjvm::Tests;

TEST_CASE("Analysis passes on valid bytecode") {
  auto files = ListDirectory("jre8", true);
  for (const auto &file : files) {
    if (!EndsWith(file, ".class"))
      continue;
    auto contents = ReadFile(file).value();
    bjvm_classdesc cls;
    parse_result_t result = bjvm_parse_classfile(contents.data(), contents.size(), &cls);
    assert(result == 0);

    for (int i = 0; i < cls.methods_count; ++i) {
      auto *method = cls.methods + i;
      if (!method->code)
        continue;
      heap_string error;
      int status = bjvm_analyze_method_code(method, &error);
      assert(status == 0);
      auto *analy = static_cast<bjvm_code_analysis *>(method->code_analysis);
      bjvm_scan_basic_blocks(method->code, analy);
      bjvm_compute_dominator_tree(analy);

      // All Java code should be reducible because it uses structured control
      // flow!
      int failed_to_reduce = bjvm_attempt_reduce_cfg(analy);
      assert(failed_to_reduce == 0);
    }

    bjvm_free_classfile(cls);
  }
}

TEST_CASE("Analysis fuzzing") {
  // Ensure the analysis system doesn't hit UB/rejects things before passing
  // broken things on TODO
}