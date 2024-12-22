//
// Created by alec on 12/21/24.
//

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#include "tests-common.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

using std::ifstream;
using std::optional;
using std::string;
using std::string_view;
using std::vector;

namespace Bjvm::Tests {
static int load_classfile(bjvm_utf8 filename, void *param, uint8_t **bytes,
                          size_t *len);
int preregister_all_classes(bjvm_vm *vm);

std::unique_ptr<bjvm_vm, void (*)(bjvm_vm *)>
CreateTestVM(bool preregister, bjvm_vm_options options,
             const char **classpath) {
  options.load_classfile = load_classfile;
  options.load_classfile_param = classpath;
  bjvm_vm *vm = bjvm_create_vm(options);
  if (preregister)
    preregister_all_classes(vm);
  return {vm, bjvm_free_vm};
}

optional<vector<uint8_t>>
ResolveClassPath(string const &filename,
                 std::vector<string> const &extra_paths) {
  std::vector<string> paths = extra_paths;
  paths.emplace_back("jre8/"); // for testing

  for (const auto &path : paths) {
    optional<vector<uint8_t>> file_data = ReadFile(path + filename);
    if (file_data.has_value()) {
      return file_data;
    }
  }

  return {};
}

std::vector<std::string> ListDirectory(const std::string &path,
                                       bool recursive) {
#ifdef EMSCRIPTEN
  void *length_and_data = EM_ASM_PTR(
      {
        // Credit: https://stackoverflow.com/a/5827895
        const fs = require('fs');
        const path = require('path');
        function *walkSync(dir, recursive) {
          const files = fs.readdirSync(dir, {withFileTypes : true});
          for (const file of files) {
            if (file.isDirectory() && recursive) {
              yield *walkSync(path.join(dir, file.name), recursive);
            } else {
              yield path.join(dir, file.name);
            }
          }
        }

        var s = "";
        for (const filePath of walkSync(UTF8ToString($0), $1))
          s += filePath + "\n";

        const length = s.length;
        const result = _malloc(length + 4);
        Module.HEAPU32[result >> 2] = length;
        Module.HEAPU8.set(new TextEncoder().encode(s), result + 4);

        return result;
      },
      path.c_str(), recursive);

  uint32_t length = *reinterpret_cast<uint32_t *>(length_and_data);
  uint8_t *data = reinterpret_cast<uint8_t *>(length_and_data) + 4;

  std::string s(data, data + length);
  free(length_and_data);

  std::vector<std::string> result;
  size_t start = 0;

  for (size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\n') {
      result.push_back(s.substr(start, i - start));
      start = i + 1;
    }
  }

  return result;
#else // !EMSCRIPTEN
  using namespace std::filesystem;

  // Recursively list files (TODO: fix recursive = false)
  std::vector<std::string> result;

  (void)recursive;

  for (const auto &entry : recursive_directory_iterator(path)) {
    if (entry.is_regular_file()) {
      result.push_back(entry.path().string());
    }
  }

  return result;
#endif
}

bool EndsWith(const std::string &s, const std::string &suffix) {
  if (s.size() < suffix.size()) {
    return false;
  }
  return s.substr(s.size() - suffix.size()) == suffix;
}

int preregister_all_classes(bjvm_vm *vm) {
  auto files = ListDirectory("jre8", true);
  int file_count = 0;
  for (auto file : files) {
    if (!EndsWith(file, ".class")) {
      continue;
    }

    vector<uint8_t> read = ReadFile(file).value();

    file = file.substr(5); // remove "jre8/"
    bjvm_utf8 filename = {.chars = (char *)file.c_str(),
                          .len = (int)file.size()};
    bjvm_vm_preregister_classfile(vm, filename, read.data(), read.size());
    file_count++;
  }
  return file_count;
}

static int load_classfile(bjvm_utf8 filename, void *param, uint8_t **bytes,
                          size_t *len) {
  const char **classpath = (const char **)param;
  const char **classpath_end = classpath;

  vector<string> extra_paths;
  while (classpath && *classpath_end) {
    extra_paths.emplace_back(*classpath_end);
    classpath_end++;
  }

  string filename_sv(filename.chars, filename.len);

  auto file_data = ResolveClassPath(filename_sv, extra_paths);
  if (file_data.has_value()) {
    *bytes = (uint8_t *)malloc(file_data->size());
    memcpy(*bytes, file_data->data(), file_data->size());
    *len = file_data->size();
    return 0;
  }

  return -1;
}

std::optional<std::vector<uint8_t>> ReadFile(const std::string &file) {
#ifdef EMSCRIPTEN
  bool exists = EM_ASM_INT(
      {
        const fs = require('fs');
        return fs.existsSync(UTF8ToString($0));
      },
      file.c_str());
  if (!exists)
    return {};

  void *length_and_data = EM_ASM_PTR(
      {
        const fs = require('fs');
        const buffer = fs.readFileSync(UTF8ToString($0));
        const length = buffer.length;

        const result = _malloc(length + 4);
        Module.HEAPU32[result >> 2] = length;
        Module.HEAPU8.set(buffer, result + 4);

        return result;
      },
      file.c_str());

  uint32_t length = *reinterpret_cast<uint32_t *>(length_and_data);
  uint8_t *data = reinterpret_cast<uint8_t *>(length_and_data) + 4;

  std::vector result(data, data + length);
  free(length_and_data);
  return result;
#else // !EMSCRIPTEN
  std::vector<uint8_t> result;
  if (std::filesystem::exists(file)) {
    std::ifstream ifs(file, std::ios::binary | std::ios::ate);
    std::ifstream::pos_type pos = ifs.tellg();

    result.resize(pos);
    ifs.seekg(0, std::ios::beg);
    ifs.read(reinterpret_cast<char *>(result.data()), pos);
  } else {
    return {};
  }
  return result;
#endif
}
} // namespace Bjvm::Tests