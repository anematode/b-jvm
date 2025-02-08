//
// Created by alec on 12/21/24.
//

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#include "tests-common.h"

#include "catch2/catch_test_macros.hpp"

#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <roundrobin_scheduler.h>
#include <unistd.h>

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

  u32 length = *reinterpret_cast<u32 *>(length_and_data);
  u8 *data = reinterpret_cast<u8 *>(length_and_data) + 4;

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

std::optional<std::vector<u8>> ReadFile(const std::string &file) {
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

  u32 length = *reinterpret_cast<u32 *>(length_and_data);
  u8 *data = reinterpret_cast<u8 *>(length_and_data) + 4;

  std::vector result(data, data + length);
  free(length_and_data);
  return result;
#else // !EMSCRIPTEN
  std::vector<u8> result;
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
                             std::string main_class, std::string input) {
  bjvm_vm_options options = bjvm_default_vm_options();

  TestCaseResult result { };
  result.stdin_ = input;

  options.classpath = (slice){.chars = (char *)classpath.c_str(),
                              .len = static_cast<u16>(classpath.size())};

  options.read_stdin = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (TestCaseResult *) param;
    int remaining = result->stdin_.length();
    int num_bytes = std::min(len, remaining);
    result->stdin_.copy(buf, num_bytes);
    result->stdin_ = result->stdin_.substr(num_bytes);
    return num_bytes;
  } : nullptr;
  options.poll_available_stdin = capture_stdio ? +[](void *param) {
    auto *result = (TestCaseResult *) param;
    return (int) result->stdin_.length();
  } : nullptr;
  options.write_stdout = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (TestCaseResult *) param;
    result->stdout_.append(buf, len);
  } : nullptr;
  options.write_stderr = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (TestCaseResult *) param;
    result->stderr_.append(buf, len);
  } : nullptr;
  options.stdio_override_param = &result;

  bjvm_vm *vm = bjvm_create_vm(options);
  if (!vm) {
    fprintf(stderr, "Failed to create VM");
    return result;
  }
  bjvm_thread *thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  slice m{.chars = (char *)main_class.c_str(),
          .len = static_cast<u16>(main_class.size())};

  bjvm_classdesc *desc = bootstrap_lookup_class(thread, m);
  if (!desc) {
    fprintf(stderr, "Failed to find main class %.*s\n", fmt_slice(m));
    throw std::runtime_error("Failed to find main class");
  }
  bjvm_stack_value args[1] = {{.obj = nullptr}};

  bjvm_cp_method *method;

  bjvm_initialize_class_t pox = { .args = {thread, desc}};
  future_t f = bjvm_initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);

  method = bjvm_method_lookup(desc, STR("main"), STR("([Ljava/lang/String;)V"),
                              false, false);

  call_interpreter_synchronous(thread, method, args); // void, no result

  if (thread->current_exception) {
    method =
        bjvm_method_lookup(thread->current_exception->descriptor, STR("toString"),
                           STR("()Ljava/lang/String;"), true, false);
    bjvm_stack_value to_string_args[1] = {{.obj = thread->current_exception}};
    thread->current_exception = nullptr;

    bjvm_stack_value to_string_invoke_args = call_interpreter_synchronous(thread, method, to_string_args);
    heap_string read;
    REQUIRE(!read_string_to_utf8(thread, &read, to_string_invoke_args.obj));

    std::cout << "Exception thrown!\n" << read.chars << '\n' << '\n';
    free_heap_str(read);

    // Then call printStackTrace ()V
    method = bjvm_method_lookup(to_string_args[0].obj->descriptor, STR("printStackTrace"),
                                STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, to_string_args);
  }

  bjvm_free_thread(thread);
  bjvm_free_vm(vm);

  return result;
}

ScheduledTestCaseResult run_scheduled_test_case(std::string classpath, bool capture_stdio,
                             std::string main_class, std::string input) {
  bjvm_vm_options options = bjvm_default_vm_options();

  ScheduledTestCaseResult result { };
  result.stdin_ = input;

  options.classpath = (slice){.chars = (char *)classpath.c_str(),
                                  .len = static_cast<u16>(classpath.size())};

  options.read_stdin = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (ScheduledTestCaseResult *) param;
    int remaining = result->stdin_.length();
    int num_bytes = std::min(len, remaining);
    result->stdin_.copy(buf, num_bytes);
    result->stdin_ = result->stdin_.substr(num_bytes);
    return num_bytes;
  } : nullptr;
  options.poll_available_stdin = capture_stdio ? +[](void *param) {
    auto *result = (ScheduledTestCaseResult *) param;
    return (int) result->stdin_.length();
  } : nullptr;
  options.write_stdout = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (ScheduledTestCaseResult *) param;
    result->stdout_.append(buf, len);
  } : nullptr;
  options.write_stderr = capture_stdio ? +[](char *buf, int len, void *param) {
    auto *result = (ScheduledTestCaseResult *) param;
    result->stderr_.append(buf, len);
  } : nullptr;
  options.stdio_override_param = &result;

  bjvm_vm *vm = bjvm_create_vm(options);
  if (!vm) {
    fprintf(stderr, "Failed to create VM");
    return result;
  }
  bjvm_thread *thread = bjvm_create_thread(vm, bjvm_default_thread_options());

  slice m{.chars = (char *)main_class.c_str(),
              .len = static_cast<u16>(main_class.size())};

  bjvm_classdesc *desc = bootstrap_lookup_class(thread, m);
  if (!desc) {
    fprintf(stderr, "Failed to find main class %.*s\n", fmt_slice(m));
    throw std::runtime_error("Failed to find main class");
  }

  bjvm_cp_method *method;

  bjvm_initialize_class_t pox = { .args = {thread, desc}};
  future_t f = bjvm_initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);

  method = bjvm_method_lookup(desc, STR("main"), STR("([Ljava/lang/String;)V"),
                              false, false);

  rr_scheduler scheduler;
  rr_scheduler_init(&scheduler, vm);
  vm->scheduler = &scheduler;

  bjvm_stack_value args[1] = {{.obj = nullptr}};

  call_interpreter_t ctx = {{ thread, method, args }};
  rr_scheduler_run(&scheduler, ctx);

  while (true) {
    auto status = rr_scheduler_step(&scheduler);
    if (status == SCHEDULER_RESULT_DONE) {
      break;
    }

    result.yield_count++;

    u64 sleep_for = rr_scheduler_may_sleep_us(&scheduler);
    if (sleep_for) {
      result.sleep_count++;
      result.ms_slept += sleep_for;
      printf("Sleeping for: %llu µs\n", sleep_for);
      usleep(sleep_for);
    }
  }

  if (thread->current_exception) {
    method =
        bjvm_method_lookup(thread->current_exception->descriptor, STR("toString"),
                           STR("()Ljava/lang/String;"), true, false);
    bjvm_stack_value to_string_args[1] = {{.obj = thread->current_exception}};
    thread->current_exception = nullptr;

    bjvm_stack_value to_string_invoke_args = call_interpreter_synchronous(thread, method, to_string_args);
    heap_string read;
    REQUIRE(!read_string_to_utf8(thread, &read, to_string_invoke_args.obj));

    std::cout << "Exception thrown!\n" << read.chars << '\n' << '\n';
    free_heap_str(read);

    // Then call printStackTrace ()V
    method = bjvm_method_lookup(to_string_args[0].obj->descriptor, STR("printStackTrace"),
                                STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, to_string_args);
  }

  bjvm_free_thread(thread);
  bjvm_free_vm(vm);

  return result;
}

} // namespace Bjvm::Tests