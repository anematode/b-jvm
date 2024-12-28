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

namespace Bjvm::Tests {
int load_classfile(bjvm_utf8 filename, void *param, uint8_t **bytes,
                          size_t *len);
    std::unique_ptr<bjvm_vm, void(*)(bjvm_vm*)> CreateTestVM(bool preregister = false,
                                                             bjvm_vm_options options = bjvm_default_vm_options());
  optional<vector<uint8_t>> ResolveClassPath(string const& filename, vector<string> const& extra_paths);
  std::vector<std::string> ListDirectory(const std::string &path,
                                         bool recursive);
  bool EndsWith(const std::string &s, const std::string &suffix);

    std::optional<std::vector<uint8_t>> ReadFile(const std::string &file);

struct TestCaseResult {
  std::string stdout_;
  std::string stderr_;
};

TestCaseResult run_test_case(std::string classpath, bool capture_stdio = true, std::string main_class = "Main");
}

#endif // BJVM_TESTS_COMMON_H
