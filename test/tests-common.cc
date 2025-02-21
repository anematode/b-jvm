//
// Created by alec on 12/21/24.
//

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#include "tests-common.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <roundrobin_scheduler.h>
#include <unistd.h>
#include <unordered_map>

using std::ifstream;
using std::optional;
using std::string;
using std::string_view;
using std::vector;

namespace Bjvm::Tests {
std::unique_ptr<vm, void (*)(vm *)> CreateTestVM(vm_options options) {
  vm *vm = create_vm(options);
  return {vm, free_vm};
}

std::vector<std::string> ListDirectory(const std::string &path, bool recursive) {
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

std::unordered_map<std::string, int> method_sigs;

void print_method_sigs() {
  std::vector<std::pair<std::string, int>> method_sigs_vec;
  for (const auto &sig : method_sigs) {
    method_sigs_vec.emplace_back(sig.first, sig.second);
  }
  // Sort so most common are first
  std::sort(method_sigs_vec.begin(), method_sigs_vec.end(), [](const auto &a, const auto &b) {
    return a.second > b.second;
  });
  // Print
  for (const auto &sig : method_sigs_vec) {
    printf("%s\t%d\n", sig.first.c_str(), sig.second);
  }
}

TestCaseResult run_test_case(std::string classpath, bool capture_stdio, std::string main_class, std::string input) {
  printf("Classpath: %s\n", classpath.c_str());
  vm_options options = default_vm_options();

  TestCaseResult result{};
  result.stdin_ = input;

  options.classpath = (slice){.chars = (char *)classpath.c_str(), .len = static_cast<u16>(classpath.size())};

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

  vm *vm = create_vm(options);
  if (!vm) {
    fprintf(stderr, "Failed to create VM");
    return result;
  }
  vm_thread *thread = create_main_thread(vm, default_thread_options());

  slice m{.chars = (char *)main_class.c_str(), .len = static_cast<u16>(main_class.size())};

  classdesc *desc = bootstrap_lookup_class(thread, m);
  if (!desc) {
    fprintf(stderr, "Failed to find main class %.*s\n", fmt_slice(m));
    throw std::runtime_error("Failed to find main class");
  }
  stack_value args[1] = {{.obj = nullptr}};

  cp_method *method;

  initialize_class_t pox = {.args = {thread, desc}};
  future_t f = initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);

  method = method_lookup(desc, STR("main"), STR("([Ljava/lang/String;)V"), false, false);

  call_interpreter_synchronous(thread, method, args); // void, no result

  if (thread->current_exception) {
    method =
        method_lookup(thread->current_exception->descriptor, STR("toString"), STR("()Ljava/lang/String;"), true, false);
    stack_value to_string_args[1] = {{.obj = thread->current_exception}};
    thread->current_exception = nullptr;

    stack_value to_string_invoke_args = call_interpreter_synchronous(thread, method, to_string_args);
    heap_string read;
    REQUIRE(!read_string_to_utf8(thread, &read, to_string_invoke_args.obj));

    std::cout << "Exception thrown!\n" << read.chars << '\n' << '\n';
    free_heap_str(read);

    // Then call printStackTrace ()V
    method = method_lookup(to_string_args[0].obj->descriptor, STR("printStackTrace"), STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, to_string_args);
  }

  // Iterate through all classes and methods and accumulate the signatures
  hash_table_iterator it;
  classdesc *value;
  for (it = hash_table_get_iterator(&vm->classes);
    hash_table_iterator_has_next(it, nullptr, nullptr, (void**) &value);
    hash_table_iterator_next(&it)) {
    for (int i = 0; i < value->methods_count; i++) {
      cp_method *method = value->methods + i;
      std::string sig { (char)field_to_kind(&method->descriptor->return_type) };
      if (!(method->access_flags & ACCESS_STATIC)) {
        sig += "L";
      }
      for (int j = 0; j < method->descriptor->args_count; j++) {
        sig.push_back(field_to_kind(&method->descriptor->args[j]));
      }
      method_sigs[sig]++;
    }
  }

  free_thread(thread);
  free_vm(vm);

  return result;
}

#ifdef EMSCRIPTEN
static void busy_wait(double us) { EM_ASM("const endAt = Date.now() + $0 / 1000; while (Date.now() < endAt) {}", us); }
#endif

ScheduledTestCaseResult run_scheduled_test_case(std::string classpath, bool capture_stdio, std::string main_class,
                                                std::string input) {
  printf("Classpath: %s\n", classpath.c_str());
  vm_options options = default_vm_options();

  ScheduledTestCaseResult result{};
  result.stdin_ = input;

  options.classpath = (slice){.chars = (char *)classpath.c_str(), .len = static_cast<u16>(classpath.size())};

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

  vm *vm = create_vm(options);
  if (!vm) {
    fprintf(stderr, "Failed to create VM");
    return result;
  }
  vm_thread *thread = create_main_thread(vm, default_thread_options());

  slice m{.chars = (char *)main_class.c_str(), .len = static_cast<u16>(main_class.size())};

  classdesc *desc = bootstrap_lookup_class(thread, m);
  if (!desc) {
    fprintf(stderr, "Failed to find main class %.*s\n", fmt_slice(m));
    throw std::runtime_error("Failed to find main class");
  }

  cp_method *method;

  initialize_class_t pox = {.args = {thread, desc}};
  future_t f = initialize_class(&pox);
  CHECK(f.status == FUTURE_READY);

  method = method_lookup(desc, STR("main"), STR("([Ljava/lang/String;)V"), false, false);

  rr_scheduler scheduler;
  rr_scheduler_init(&scheduler, vm);
  vm->scheduler = &scheduler;

  stack_value args[1] = {{.obj = nullptr}};

  call_interpreter_t ctx = {{thread, method, args}};
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
#ifdef EMSCRIPTEN
      // long-term, we will use the JS scheduler instead of this hack
      busy_wait(sleep_for);
#else
      usleep(sleep_for);
#endif
    }
  }

  if (thread->current_exception) {
    method =
        method_lookup(thread->current_exception->descriptor, STR("toString"), STR("()Ljava/lang/String;"), true, false);
    stack_value to_string_args[1] = {{.obj = thread->current_exception}};
    thread->current_exception = nullptr;

    stack_value to_string_invoke_args = call_interpreter_synchronous(thread, method, to_string_args);
    heap_string read;
    REQUIRE(!read_string_to_utf8(thread, &read, to_string_invoke_args.obj));

    std::cout << "Exception thrown!\n" << read.chars << '\n' << '\n';
    free_heap_str(read);

    // Then call printStackTrace ()V
    method = method_lookup(to_string_args[0].obj->descriptor, STR("printStackTrace"), STR("()V"), true, false);
    call_interpreter_synchronous(thread, method, to_string_args);
    if (capture_stdio)
      std::cerr << result.stderr_;
  }

  free_thread(thread);
  free_vm(vm);
  rr_scheduler_uninit(&scheduler);

  return result;
}

} // namespace Bjvm::Tests