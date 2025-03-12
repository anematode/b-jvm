//
// Created by alec on 12/21/24.
//

#ifdef EMSCRIPTEN
#include <emscripten/emscripten.h>
#endif

#include "tests-common.h"

#include "doctest/doctest.h"

#include <algorithm>
#include <arrays.h>
#include <cached_classdescs.h>
#include <fstream>
#include <iostream>
#include <objects.h>
#include <optional>
#include <profiler.h>
#include <string>
#include <utility>
#include <vector>

#include <roundrobin_scheduler.h>
#include <worker_threads.h>
#include <unistd.h>
#include <unordered_map>

using std::ifstream;
using std::optional;
using std::string;
using std::string_view;
using std::vector;

namespace Bjvm::Tests {
// todo: use actual c++ RAII class instead of this scuff
static void tear_down_vm(vm *vm) {
  auto *scheduler = (rr_scheduler *)vm->scheduler;
  if (scheduler)
    rr_scheduler_uninit(scheduler);
  free_vm(vm);
  delete scheduler; // scuff scuff scuff
}

// todo: use actual c++ RAII class instead of this scuff
std::unique_ptr<vm, void (*)(vm *)> CreateTestVM(vm_options options) {
  vm *vm = create_vm(options);
  auto *scheduler = new rr_scheduler{};
  rr_scheduler_init(scheduler, vm);
  vm->scheduler = scheduler;
  return {vm, tear_down_vm};
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

std::string ReadFileAsString(const std::string &file) {
  auto data = ReadFile(file);
  if (!data) {
    return {};
  }
  return {data->begin(), data->end()};
}

std::unordered_map<std::string, int> method_sigs;

void print_method_sigs() {
  std::vector<std::pair<std::string, int>> method_sigs_vec;
  for (const auto &sig : method_sigs) {
    method_sigs_vec.emplace_back(sig.first, sig.second);
  }
  // Sort so most common are first
  std::sort(method_sigs_vec.begin(), method_sigs_vec.end(),
            [](const auto &a, const auto &b) { return a.second > b.second; });
  // Print
  for (const auto &sig : method_sigs_vec) {
    printf("%s\t%d\n", sig.first.c_str(), sig.second);
  }
}

ScheduledTestCaseResult run_test_case(std::string classpath, bool capture_stdio, std::string main_class,
                                      std::string input, std::vector<std::string> args, vm_options options) {
  return run_scheduled_test_case(std::move(classpath), capture_stdio, std::move(main_class), std::move(input),
                                 std::move(args), options);
}

#ifdef EMSCRIPTEN
static void busy_wait(double us) { EM_ASM("const endAt = Date.now() + $0 / 1000; while (Date.now() < endAt) {}", us); }
#endif


void *worker_thread_run_until_completion(void *param) {
  auto *scheduler = (rr_scheduler *)param;
  auto *vm = scheduler->vm;
  auto *result = new ScheduledTestCaseResult {};

  // chop chop time to work
  worker_thread_pool_register(&scheduler->thread_pool);
  scheduler_polled_info_t task = scheduler_poll(scheduler); // initialize before entering loop
  for (;;result->yield_count++) {
    gc_pause_if_requested(vm);

    if (task.current_status == SCHEDULER_RESULT_DONE) {
      break; // probably, idk
    }

    if (task.thread_info) {
      scheduler_execute(vm, task, scheduler->preemption_us);
      task = scheduler_push_execution_record_and_repoll(scheduler, task);
      continue;
    }

    u64 sleep_for = task.may_sleep_us;
    if (sleep_for) { // we've been told to sleep
      result->sleep_count++;
      result->us_slept += sleep_for;
#ifdef EMSCRIPTEN
      // long-term, we will use the JS scheduler instead of this hack
      busy_wait(sleep_for);
#else
      usleep(sleep_for);
#endif
    }

    task = scheduler_push_execution_record_and_repoll(scheduler, task);
  }
  worker_thread_pool_deregister(&scheduler->thread_pool);

  return result;
}

ScheduledTestCaseResult run_scheduled_test_case(std::string classpath, bool capture_stdio, std::string main_class,
                                                std::string input, std::vector<std::string> string_args,
                                                vm_options options) {
  printf("Classpath: %s\n", classpath.c_str());

  ScheduledTestCaseResult result{};
  result.stdin_ = input;

  options.classpath = (slice){.chars = (char *)classpath.c_str(), .len = static_cast<u16>(classpath.size())};

  // todo: stdio functions need to be synchronized
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

  rr_scheduler scheduler;
  rr_scheduler_init(&scheduler, vm);
  vm->scheduler = &scheduler;

  // todo: should we register as a worker thread here? or only later when we actually call interpreter

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

  handle *string_args_as_object =
      make_handle(thread, CreateObjectArray1D(thread, cached_classes(thread->vm)->string, (int)string_args.size()));
  for (size_t i = 0; i < string_args.size(); i++) {
    object str = MakeJStringFromCString(thread, string_args[i].c_str(), true);
    ReferenceArrayStore(string_args_as_object->obj, (int)i, str);
  }

  stack_value args[1] = {{.obj = string_args_as_object->obj}};
  drop_handle(thread, string_args_as_object);

  call_interpreter_t ctx = {{thread, method, args}};
  execution_record *record = rr_scheduler_run(&scheduler, ctx);

//  // do some work just to get settled
//  for (int i=0; i<5; i++) {
//    scheduler_polled_info_t task = scheduler_poll(&scheduler);
//    scheduler_execute(vm, task, scheduler.preemption_us);
//    scheduler_push_execution_record(&scheduler, task);
//  }

  pthread_t thread_1;
  pthread_create(&thread_1, nullptr, worker_thread_run_until_completion, &scheduler);

//  pthread_t thread_2;
//  pthread_create(&thread_2, nullptr, worker_thread_run_until_completion, &scheduler);
//
//  ScheduledTestCaseResult *result_2;
//  pthread_join(thread_2, reinterpret_cast<void **>(&result_2));
//  result.yield_count += result_2->yield_count;
//  result.sleep_count += result_2->sleep_count;
//  result.us_slept += result_2->us_slept;

  ScheduledTestCaseResult *result_1;
  pthread_join(thread_1, reinterpret_cast<void **>(&result_1));
  result.yield_count += result_1->yield_count;
  result.sleep_count += result_1->sleep_count;
  result.us_slept += result_1->us_slept;
  delete result_1;

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

  rr_scheduler_uninit(&scheduler);
  free_thread(thread);
  free_vm(vm);

  return result;
}

} // namespace Bjvm::Tests