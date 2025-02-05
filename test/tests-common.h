//
// Created by alec on 12/21/24.
//

#ifndef BJVM_TESTS_COMMON_H
#define BJVM_TESTS_COMMON_H

#include <bjvm.h>

#include <memory>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>

#include <catch2/matchers/catch_matchers_container_properties.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <catch2/matchers/catch_matchers_vector.hpp>

using std::string;
using std::optional;
using std::vector;
using std::basic_string_view;

using namespace Catch::Matchers;

namespace Bjvm::Tests {
static inline std::string_view to_string_view(heap_string str) {
    return {str.chars, (size_t)str.len};
}

    static inline std::string_view to_string_view(slice str) {
        return {str.chars, (size_t)str.len};
    }


    std::unique_ptr<bjvm_vm, void(*)(bjvm_vm*)> CreateTestVM(bjvm_vm_options options = bjvm_default_vm_options());
  std::vector<std::string> ListDirectory(const std::string &path,
                                         bool recursive);
  bool EndsWith(const std::string &s, const std::string &suffix);

    std::optional<std::vector<u8>> ReadFile(const std::string &file);

struct TestCaseResult {
  std::string_view stdin_;
  std::string stdout_;
  std::string stderr_;
};

TestCaseResult run_test_case(std::string classpath, bool capture_stdio = true, std::string main_class = "Main", std::string input = "");
}

#endif // BJVM_TESTS_COMMON_H
