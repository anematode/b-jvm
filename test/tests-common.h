//
// Created by alec on 12/21/24.
//

#ifndef BJVM_TESTS_COMMON_H
#define BJVM_TESTS_COMMON_H

#undef __APPLE__

#include <bjvm.h>

#include <memory>
#include <filesystem>
#include <vector>
#include <string>
#include <optional>

using std::string;
using std::optional;
using std::vector;

namespace Bjvm::Tests {
    std::unique_ptr<bjvm_vm, void(*)(bjvm_vm*)> CreateTestVM(bool preregister = false,
                                                             bjvm_vm_options options = bjvm_default_vm_options(),
                                                             const char** classpath = nullptr);
  optional<vector<uint8_t>> ResolveClassPath(string const& filename, vector<string> const& extra_paths);
  std::vector<std::string> ListDirectory(const std::string &path,
                                         bool recursive);
  bool EndsWith(const std::string &s, const std::string &suffix);
}

#endif // BJVM_TESTS_COMMON_H
