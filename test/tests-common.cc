//
// Created by alec on 12/21/24.
//

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#include "tests-common.h"

#include "natives.h"

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using std::ifstream;
using std::optional;
using std::string;
using std::string_view;
using std::vector;

namespace Bjvm::Tests {

std::unique_ptr<bjvm_vm, void (*)(bjvm_vm *)>
CreateTestVM(bjvm_vm_options options) {
  bjvm_vm *vm = bjvm_create_vm(options);
  return {vm, bjvm_free_vm};
}

optional<vector<uint8_t>>
ResolveClassPath(string const &filename,
                 std::vector<string> const &extra_paths) {
  std::vector<string> paths = extra_paths;
  paths.emplace_back("jdk23/"); // for testing

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

int load_classfile(bjvm_utf8 filename, void *param, uint8_t **bytes,
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

TestCaseResult run_test_case(std::string classpath, bool capture_stdio,
                             std::string main_class) {
  bjvm_vm_options options = bjvm_default_vm_options();

  TestCaseResult result{};

  options.classpath = (bjvm_utf8){.chars = (char *)classpath.c_str(),
                                  .len = (int)classpath.size()};
  options.write_stdout = capture_stdio ? +[](int ch, void *param) {
    auto *result = (TestCaseResult *)param;
    result->stdout_ += (char)ch;
  } : nullptr;
  options.write_stderr = capture_stdio ? +[](int ch, void *param) {
    auto *result = (TestCaseResult *)param;
    result->stderr_ += (char)ch;
  } : nullptr;
  options.write_byte_param = &result;

  bjvm_vm *vm = bjvm_create_vm(options);
  if (!vm) {
    fprintf(stderr, "Failed to create VM");
    return result;
  }
  bjvm_thread *thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  bjvm_utf8 m{.chars = (char *)main_class.c_str(),
              .len = (int)main_class.size()};

  bjvm_classdesc *desc = bootstrap_lookup_class(thread, m);
  if (!desc) {
    return result;
  }
  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method;

  bjvm_initialize_class_t pox;
  future_t f = bjvm_initialize_class(&pox, thread, desc);
  assert(f.status == FUTURE_READY);

  method = bjvm_method_lookup(desc, STR("main"), STR("([Ljava/lang/String;)V"),
                              false, false);

  bjvm_thread_run_root(thread, method, args, nullptr);

  if (thread->current_exception) {
    method =
        bjvm_method_lookup(thread->current_exception->descriptor, STR("toString"),
                           STR("()Ljava/lang/String;"), true, false);
    bjvm_stack_value args[1] = {{.obj = thread->current_exception}}, result;
    thread->current_exception = nullptr;
    bjvm_thread_run_root(thread, method, args, &result);
    heap_string read = AsHeapString(result.obj, on_oom);
    std::cout << "Exception thrown!\n" << read.chars << '\n' << '\n';
    free_heap_str(read);

    // Then call printStackTrace ()V
    method = bjvm_method_lookup(args[0].obj->descriptor, STR("printStackTrace"),
                                STR("()V"), true, false);
    bjvm_thread_run_root(thread, method, args, nullptr);
  }

  bjvm_free_thread(thread);
  bjvm_free_vm(vm);

  return result;

  on_oom:
  UNREACHABLE();
}

} // namespace Bjvm::Tests