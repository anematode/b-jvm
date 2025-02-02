//
// Created by alec on 12/21/24.
//

#ifndef BJVM_TESTS_COMMON_H
#define BJVM_TESTS_COMMON_H

#include "../src/bjvm.h"

#include <memory>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>

using std::string;
using std::optional;
using std::vector;
using std::basic_string_view;

namespace Bjvm::Tests {
static inline std::string_view to_string_view(heap_string str) {
    return {str.chars, (size_t)str.len};
}

    static inline std::string_view to_string_view(slice str) {
        return {str.chars, (size_t)str.len};
    }

int load_classfile(slice filename, void *param, u8 **bytes,
                          size_t *len);
    std::unique_ptr<bjvm_vm, void(*)(bjvm_vm*)> CreateTestVM(bjvm_vm_options options = bjvm_default_vm_options());
  optional<vector<u8>> ResolveClassPath(string const& filename, vector<string> const& extra_paths);
  std::vector<std::string> ListDirectory(const std::string &path,
                                         bool recursive);
  bool EndsWith(const std::string &s, const std::string &suffix);

    std::optional<std::vector<u8>> ReadFile(const std::string &file);

struct TestCaseResult {
  std::string stdout_;
  std::string stderr_;
};

TestCaseResult run_test_case(std::string classpath, bool capture_stdio = true, std::string main_class = "Main");
}

#endif // BJVM_TESTS_COMMON_H
